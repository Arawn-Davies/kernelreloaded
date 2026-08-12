/*
 * Static fork test -- no libc, no ld.so, no relocation.
 *
 * Every binary that works so far (statictest, ls, e2fsck) is single-process.
 * bash forks. fork means copy-on-write, which is exactly where a residual MMU
 * bug would live, so this isolates it: fork, have parent and child each write
 * a distinct marker, then both spin.
 *
 *   both markers  -> fork and COW are fine, and bash's problem is elsewhere
 *   one marker    -> the child died, and COW is still wrong
 *   faults        -> reproduced the corruption with 40 lines of assembly
 */
	.text
	.globl	__start
	.globl	_start
__start:
_start:
	li	$2, 4002		/* __NR_fork */
	syscall

	/* v0 = 0 in the child, pid in the parent. a3 != 0 means error. */
	beq	$2, $0, child
	nop

parent:
	li	$2, 4004
	li	$4, 1
	la	$5, pmsg
	li	$6, 16
	syscall
pspin:
	b	pspin
	nop

child:
	li	$2, 4004
	li	$4, 1
	la	$5, cmsg
	li	$6, 16
	syscall
	/* Touch a private page after the fork: this is what actually exercises
	 * copy-on-write, since the page is shared read-only until written. */
	la	$8, scratch
	li	$9, 0x5a5a5a5a
	sw	$9, 0($8)
	lw	$10, 0($8)

	li	$2, 4004
	li	$4, 1
	la	$5, wmsg
	li	$6, 16
	syscall
cspin:
	b	cspin
	nop

	.data
pmsg:	.ascii	"FORK-PARENT-OK\n "
cmsg:	.ascii	"FORK-CHILD-OK\n  "
wmsg:	.ascii	"FORK-COW-WRITE\n "
scratch:
	.word	0
