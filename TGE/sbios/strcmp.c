/* strcmp for the TGE SBIOS.
 *
 * The SBIOS links -nostdlib and supplies its own string routines, one per file
 * (strlen.S, strcpy.S, strncpy.S, memcpy.S, memset.S, memcmp.S) -- but never
 * had a strcmp. smod.c/misc.c call it, and older toolchains satisfied that
 * from libgcc or by inlining a builtin. gcc 15 does neither here, so the link
 * fails with "undefined reference to `strcmp'".
 *
 * Plain C rather than hand-written MIPS: the neighbouring routines are
 * quadword-optimised assembly from newlib, but strcmp is called only on short
 * module-name strings during startup, so clarity beats speed and there is no
 * assembly to get subtly wrong.
 */

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}

	return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}
