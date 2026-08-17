#!/usr/bin/env python3
"""Rasterise a TTF into a glyph atlas plus metrics, for loader/font.c.

WHY THIS EXISTS

Dynamic text in the loader goes through gsKit_fontm_print_scaled, which draws
from GSFONTM -- the PS2's own BIOS ROM font. That is one fixed bitmap typeface
whose only knob is scale, and gsKit_fontm offers no way to measure a rendered
string. So a centred title, a right-aligned value in the System Info panel, or
simply text that is known not to overrun its box are all impossible: every
position in graphic.cpp is a hardcoded column tuned by eye.

An atlas fixes both halves at once. The glyphs become an ordinary texture drawn
with gsKit_prim_sprite_texture -- the same call the starfield and the menu
highlight already use -- and the per-glyph advances make measurement arithmetic.

WHY OFFLINE

The atlas is generated here and committed under assets/, like every other piece
of artwork, rather than built from the TTF each time. Rasterising needs
ImageMagick, and the build image deliberately carries only what the EE build
needs. phase1/png2logo.py takes the same approach for the boot logo.

Output is a 32-bit RGBA PNG, which is exactly what png2rgb already accepts, so
the atlas rides the existing png2rgb -> bin2s -> getTexture() path with no new
texture plumbing.

USAGE
    tools/fontgen.py                       # defaults: DejaVu Sans, 18pt
    tools/fontgen.py --size 12 --name small
"""

import argparse
import os
import re
import subprocess
import sys

FIRST, LAST = 32, 126  # printable ASCII; the loader shows nothing else

# Antialiasing spills outside the advance box: measured against DejaVu Sans,
# 'e', 's' and 'o' all put ink one pixel PAST their advance. A cell exactly one
# advance wide therefore clips that column, and clips a different amount per
# glyph -- which shows up as a right-aligned column whose right edge wobbles.
# Pad every cell and draw the glyph inset by the same amount; layout still
# advances by the advance, so spacing is unchanged.
PAD = 2


def im_escape(text):
    """Make a string safe for -annotate.

    ImageMagick expands percent escapes in annotation text, and treats a leading
    @ as "read from this file". Callers here always wrap the glyph in bars, so @
    is never leading; percent and backslash still need escaping.
    """
    return text.replace("\\", "\\\\").replace("%", "%%")


def imagemagick_metrics(font, size, text):
    """Ask ImageMagick for the metrics of a string.

    -debug annotate makes it print the block we want to stderr. It is the only
    way to get an advance width out of the CLI, and it is what makes
    proportional spacing possible at all.
    """
    out = subprocess.run(
        ["convert", "-debug", "annotate", "xc:", "-font", font,
         "-pointsize", str(size), "-annotate", "0", im_escape(text), "null:"],
        capture_output=True, text=True).stderr
    got = {}
    for key in ("width", "height", "ascent", "descent", "origin"):
        m = re.search(rf"{key}: ([-0-9.]+)", out)
        if m:
            got[key] = float(m.group(1))
    if "origin" not in got:
        sys.exit(f"ImageMagick reported no metrics for {text!r} -- is the font path right?")
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    ap.add_argument("--size", type=int, default=18)
    ap.add_argument("--name", default="font")
    ap.add_argument("--atlas-width", type=int, default=512)
    args = ap.parse_args()

    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    png = os.path.join(here, "assets", f"{args.name}.png")
    hdr = os.path.join(here, "loader", f"{args.name}data.h")

    if not os.path.exists(args.font):
        sys.exit(f"font not found: {args.font}")

    # Global metrics. 'M' is arbitrary; ascent/descent do not vary per glyph.
    g = imagemagick_metrics(args.font, args.size, "M")
    ascent = int(round(g["ascent"]))
    descent = int(round(-g["descent"]))
    line_h = ascent + descent

    print(f"font   {os.path.basename(args.font)} @ {args.size}pt")
    print(f"line   {line_h}px (ascent {ascent}, descent {descent})")

    # A lone space produces no metrics block at all, so advances are measured
    # differentially: the width of "|c|" minus the width of "||". That works for
    # every printable character including space, and keeps the glyph off the
    # front of the string where a leading @ would be read as a filename.
    bar = imagemagick_metrics(args.font, args.size, "||")["origin"]

    # Per-glyph advances, and a tile for each.
    tmp = os.path.join("/tmp", f"fontgen-{os.getpid()}")
    os.makedirs(tmp, exist_ok=True)
    glyphs = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        m = imagemagick_metrics(args.font, args.size, "|" + ch + "|")
        adv = int(round(m["origin"] - bar))
        if adv <= 0:
            adv = 1
        tile = os.path.join(tmp, f"{code}.png")
        # Cell is advance + 2*PAD wide with the glyph inset by PAD, so nothing
        # is clipped. The renderer blits the whole cell at pen-PAD and then
        # advances the pen by the advance alone.
        subprocess.run(
            ["convert", "-size", f"{adv + 2 * PAD}x{line_h}", "xc:transparent",
             "-font", args.font, "-pointsize", str(args.size),
             "-fill", "white", "-annotate", f"+{PAD}+{ascent}", im_escape(ch),
             "PNG32:" + tile], check=True)
        # Right side bearing: how far the INK stops short of the advance. Right
        # alignment on advances aligns the advance boxes, but the eye reads the
        # ink, so a column of values ending in different letters looks ragged
        # even when it is mathematically correct. Recording this lets the
        # renderer align on ink instead.
        # %@ is the trim bounding box, WxH+X+Y. Parsing "info:-" instead picks
        # up the canvas geometry that follows it and yields nonsense.
        out = subprocess.run(["convert", tile, "-format", "%@", "info:"],
                             capture_output=True, text=True).stdout.strip()
        m = re.match(r"(\d+)x(\d+)\+(-?\d+)\+(-?\d+)", out)
        if m:
            iw, ox = int(m.group(1)), int(m.group(3))
            rsb = adv - ((ox + iw) - PAD)
        else:
            rsb = 0          # blank cell, e.g. space
        if code == 32:
            rsb = 0      # a space has no ink; never align on it
        glyphs.append((code, adv, tile, rsb))

    # Pack left to right, wrapping at the atlas width.
    rows, row, x = [], [], 0
    for code, adv, tile, rsb in glyphs:
        cell = adv + 2 * PAD
        if x + cell > args.atlas_width:
            rows.append((row, x)); row, x = [], 0
        row.append((code, adv, tile, x, rsb)); x += cell
    if row:
        rows.append((row, x))

    height = len(rows) * line_h
    # The GS wants power-of-two dimensions for a texture it will sample.
    pot = 1
    while pot < height:
        pot *= 2

    print(f"atlas  {args.atlas_width}x{pot} ({len(glyphs)} glyphs, {len(rows)} rows)")

    placed = []
    cmd = ["convert", "-size", f"{args.atlas_width}x{pot}", "xc:transparent"]
    for r, (row, _w) in enumerate(rows):
        for code, adv, tile, x, rsb in row:
            y = r * line_h
            cmd += [tile, "-geometry", f"+{x}+{y}", "-composite"]
            placed.append((code, x, y, adv, rsb))
    cmd.append("PNG32:" + png)
    subprocess.run(cmd, check=True)

    with open(hdr, "w") as f:
        f.write(f"""/* Generated by tools/fontgen.py -- do not edit.
 *
 * {os.path.basename(args.font)} at {args.size}pt, ASCII {FIRST}..{LAST}.
 * Regenerate with:  tools/fontgen.py --size {args.size} --name {args.name}
 *
 * Cell width is the glyph's ADVANCE, so drawing is: blit the cell at the pen,
 * then advance the pen by the same amount. No bearing arithmetic, and a string
 * width is the sum of the advances -- which is the measurement gsKit_fontm
 * could never give us.
 */
#ifndef __{args.name.upper()}DATA_H__
#define __{args.name.upper()}DATA_H__

#define {args.name.upper()}_FIRST      {FIRST}
#define {args.name.upper()}_LAST       {LAST}
#define {args.name.upper()}_LINEHEIGHT {line_h}
#define {args.name.upper()}_ASCENT     {ascent}
#define {args.name.upper()}_PAD        {PAD}

/* x, y is the CELL's top-left; the cell is advance + 2*PAD wide with the glyph
 * inset by PAD, so antialiasing that spills past the advance box is not clipped.
 * Draw the cell at pen - PAD and advance the pen by advance alone.
 *
 * rsb lets a right-hand column align on the ink rather than the advance box. */
typedef struct {{
\tunsigned short x;
\tunsigned short y;
\tunsigned char  advance;
\tsigned char    rsb;      /* how far the ink stops short of the advance */
}} glyph_t;

static const glyph_t {args.name}Glyphs[{LAST - FIRST + 1}] = {{
""")
        for code, x, y, adv, rsb in placed:
            ch = chr(code).replace("\\", "\\\\").replace("*/", "")
            f.write(f"\t{{ {x:4d}, {y:4d}, {adv:3d}, {rsb:3d} }},  /* {code:3d} '{ch}' */\n")
        f.write("};\n\n#endif\n")

    for _c, _a, tile, _r in glyphs:
        os.unlink(tile)
    os.rmdir(tmp)
    print(f"wrote  {png}")
    print(f"wrote  {hdr}")


if __name__ == "__main__":
    main()
