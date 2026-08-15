/* On-screen boot log. See bootlog.h for why this exists. */

#include <string.h>

#include "bootlog.h"

#include "graphic.h"

/* Wide enough for the longest line the loader actually prints -- the kernel
 * command line is the worst case and is clipped when drawn, not here. */
#define BOOTLOG_COLS 128

static char lines[BOOTLOG_LINES][BOOTLOG_COLS];

/* Index of the oldest line, and how many are held. Once the buffer is full,
 * first advances and count stays at BOOTLOG_LINES. */
static int first;
static int count;

/* Length of the newest line, so a kprintf() that does not end in a newline is
 * continued rather than starting a line of its own. The loader does this in a
 * few places -- "Loading module x..." then "ok" -- and splitting those would
 * waste half the panel. */
static int partial;

static int active;

/* Repaint so the window shows what has just been logged.
 *
 * Without this the panel is only as fresh as the last graphic_paint(), which
 * comes from the progress bar -- so any stage that reports no percentage, such
 * as module loading, leaves the screen frozen on the previous stage's last
 * line. A hang there then reads as a hang in the stage before it, which is
 * exactly the wrong answer to the only question this window exists to answer.
 *
 * Once per completed line, not per character, and guarded against re-entry
 * because graphic_paint() and everything it calls may kprintf(). */
static int painting;

static void repaint(void)
{
	if (!active || painting) {
		return;
	}
	painting = 1;
	graphic_repaint();
	painting = 0;
}

static void startLine(void)
{
	int slot;

	if (count < BOOTLOG_LINES) {
		slot = (first + count) % BOOTLOG_LINES;
		count++;
	} else {
		slot = first;
		first = (first + 1) % BOOTLOG_LINES;
	}
	lines[slot][0] = '\0';
	partial = 0;
}

static void appendChar(char c)
{
	int slot;

	if (count == 0) {
		startLine();
	}
	slot = (first + count - 1) % BOOTLOG_LINES;

	if (partial < (BOOTLOG_COLS - 1)) {
		lines[slot][partial] = c;
		partial++;
		lines[slot][partial] = '\0';
	}
	/* Past the end the character is dropped rather than wrapped: a wrapped
	 * line would push a real one off the top of a 16-line panel. */
}

void bootlogAppend(const char *text)
{
	const char *p;

	if (text == NULL) {
		return;
	}

	for (p = text; *p != '\0'; p++) {
		if (*p == '\n') {
			/* Deferred: start the next line only when something is actually
			 * written to it, so a trailing newline does not leave a blank
			 * line at the bottom of the panel. */
			partial = BOOTLOG_COLS;
			continue;
		}
		if (*p == '\r') {
			continue;
		}
		if (partial >= BOOTLOG_COLS) {
			startLine();
		}
		appendChar(*p);
	}

	/* Once per kprintf(), after the text is in the buffer -- not when the next
	 * line starts. The line that matters most is the last one before a hang,
	 * and that one has no successor to trigger the repaint. */
	repaint();
}

void bootlogBegin(void)
{
	active = 1;
}

int bootlogActive(void)
{
	return active;
}

int bootlogCount(void)
{
	return count;
}

const char *bootlogLine(int i)
{
	if ((i < 0) || (i >= count)) {
		return NULL;
	}
	return lines[(first + i) % BOOTLOG_LINES];
}
