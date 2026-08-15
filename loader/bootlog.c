/* On-screen boot log. See bootlog.h for why this exists. */

#include <string.h>

#include "bootlog.h"

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
