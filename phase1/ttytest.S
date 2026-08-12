/*
 * Static terminal-setup test -- no libc.
 *
 * Everything structural works: static, dynamic, large, fork, COW. What is left
 * is what bash does at startup that ls and e2fsck do not, and the first thing
 * a shell does is interrogate its terminal. ls even reported "write error",
 * which points at the same place.
 *
 * TCGETS on fd 0. If the console tty cannot answer that, a shell is dead in
 * the water before it prints anything.
 */
	.text
	.globl	__start
	.globl	_start
__start:
_start:
	li	$2, 4054		/* __NR_ioctl */
	li	$4, 0			/* fd 0 */
	li	$5, 0x540d		/* TCGETS */
	la	$6, termbuf
	syscall

	/* a3 is 0 on success, non-zero on error. */
	bne	$7, $0, failed
	nop

	li	$2, 4004
	li	$4, 1
	la	$5, okmsg
	li	$6, 16
	syscall
	b	spin
	nop

failed:
	li	$2, 4004
	li	$4, 1
	la	$5, failmsg
	li	$6, 16
	syscall

spin:
	b	spin
	nop

	.data
okmsg:	.ascii	"TCGETS-OK\n     "
	.byte	10
failmsg:.ascii	"TCGETS-FAILED\n "
	.byte	10
termbuf:
	.space	64
