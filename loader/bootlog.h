#ifndef _BOOTLOG_H_
#define _BOOTLOG_H_

/* On-screen boot log.
 *
 * Everything the loader already reports through kprintf() goes to SIO, which
 * needs a hardware modification to read. On a console without one there is no
 * way to tell a hang during module loading from one during SBIOS relocation
 * from one during the kernel read -- they all look like a stopped screen. This
 * keeps the last few lines and draws them in a panel during the load, so the
 * last thing printed before a hang is visible on the television.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Number of lines kept and shown. */
#define BOOTLOG_LINES 16

/** Add text to the log. Splits on newlines; over-long lines are truncated.
 *
 * Safe to call before the display exists -- it only writes to a static buffer
 * and never touches the GS. */
void bootlogAppend(const char *text);

/** Start showing the log panel. Called when the boot leaves the menu. */
void bootlogBegin(void);

/** True once bootlogBegin() has been called. */
int bootlogActive(void);

/** Line i of the log, oldest first, or NULL past the end. */
const char *bootlogLine(int i);

/** How many lines are currently held. */
int bootlogCount(void);

#ifdef __cplusplus
}
#endif

#endif
