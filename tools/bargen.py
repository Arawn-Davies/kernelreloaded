#!/usr/bin/env python3
"""Draw the bottom button bar as assets/bottombar.png.

The bar was hand-made artwork whose glyphs sat off-centre in their rings -- the
triangle most obviously, because a triangle's optical centre is not its bounding
box centre. Drawing it from a script makes the geometry explicit and repeatable,
and lets the labels use the same typeface as the rest of the UI instead of
whatever the original was set in.

Committed to assets/ like the other artwork; build.sh copies it into loader/.
Regenerate with:  tools/bargen.py
"""

import os
import subprocess

W, H = 640, 34
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

INK = "#cfe6ff"      # glyph and label colour, matching the panel text
RULE = "#2a4a6a"     # the separator along the top edge
LABEL_PT = 13

R = 8                # ring radius
STROKE = 1.4

# left edge of each group, and its label
GROUPS = [(28, "Select", "cross"), (270, "Options", "triangle"), (500, "Back", "circle")]


def ring(cx, cy):
    return [f"stroke {INK}", f"stroke-width {STROKE}", "fill none",
            f"circle {cx},{cy} {cx + R},{cy}"]


def cross(cx, cy):
    """A cross fits its ring squarely: equal arms, inset from the rim."""
    a = R * 0.44
    return [f"stroke {INK}", f"stroke-width {STROKE}", "fill none",
            f"line {cx - a},{cy - a} {cx + a},{cy + a}",
            f"line {cx - a},{cy + a} {cx + a},{cy - a}"]


def triangle(cx, cy):
    """A triangle's CENTROID is a third of the way up from its base, not half.

    Centring its bounding box -- which is what the old artwork did -- leaves it
    visibly low in the ring. Placing the centroid on the ring's centre is what
    reads as centred.
    """
    h = R * 1.10
    half = h * 0.58
    apex = cy - (2.0 * h / 3.0)
    base = cy + (h / 3.0)
    return [f"stroke {INK}", f"stroke-width {STROKE}", "fill none",
            f"polyline {cx},{apex} {cx - half},{base} {cx + half},{base} {cx},{apex}"]


def circle_glyph(cx, cy):
    r = R * 0.44
    return [f"stroke {INK}", f"stroke-width {STROKE}", "fill none",
            f"circle {cx},{cy} {cx + r},{cy}"]


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(here, "assets", "bottombar.png")

    cy = H / 2.0 + 1          # the rule occupies the top row
    draw = [f"stroke {RULE}", "stroke-width 1", f"line 0,0 {W},0"]

    label_cmds = []
    for x, label, kind in GROUPS:
        cx = x + R
        draw += ring(cx, cy)
        draw += {"cross": cross, "triangle": triangle, "circle": circle_glyph}[kind](cx, cy)
        # Label sits after the ring, vertically centred on it. -stroke none
        # matters: the stroke set for the ring persists into -annotate and
        # outlines every letter, which is why the labels came out looking bold.
        label_cmds += ["-stroke", "none", "-fill", INK, "-font", FONT, "-pointsize", str(LABEL_PT),
                       "-annotate", f"+{int(cx + R + 9)}+{int(cy + LABEL_PT * 0.36)}", label]

    cmd = ["convert", "-size", f"{W}x{H}", "xc:transparent",
           "-stroke", INK, "-draw", " ".join(draw)] + label_cmds + ["PNG32:" + out]
    subprocess.run(cmd, check=True)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
