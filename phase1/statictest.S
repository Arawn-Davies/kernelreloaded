/*
 * Static, libc-free, relocation-free userspace test.
 *
 * Build:  ee-gcc -nostdlib -static -mno-abicalls -fno-pic -G0 \
 *                 -o statictest statictest.S
 *
 * -mno-abicalls -fno-pic matters: without them "la" compiles to a GOT load
 * through $gp, which nothing has set up at process entry with -nostdlib, so
 * the pointer is garbage. With them the address is materialised with
 * lui/addiu and the binary needs no relocation of any kind.
 *
 * Install as /statictest in an initrd and boot with init=/statictest. It also
 * needs /dev/ttyS0 to exist -- the stock initrd has tty0..tty8 and console but
 * no node for romcons, which is TTY_MAJOR with minor_start 64:
 *
 *     debugfs -w image <<< $'cd /dev\nmknod ttyS0 c 4 64\nsif ttyS0 mode 020666'
 *
 * RESULT: prints on both consoles and spins at its link address with zero
 * faults, while dynamically linked bash takes ~880k faults on address 0x3.
 * Userspace and the MMU are sound; the remaining bug is in the dynamic path.
 *
 * Writes the same marker three ways so the two consoles mirror each other:
 *   fd 1        -- whatever /dev/console maps to (the kernel opens it for init)
 *   /dev/tty0   -- the GS framebuffer console, i.e. the screen
 *   /dev/ttyS0  -- romcons (TTY_MAJOR, minor_start 64), i.e. SIO -> PCSX2 log
 *
 * Then it spins. The spin is the real measurement: a busy loop shows up in the
 * EE state dump as pc parked in useg, which proves userspace runs even if
 * every console path is broken. Nothing here touches ld.so.
 */
	.text
	.globl	__start
	.globl	_start
__start:
_start:
	/* write(1, msg, 25) */
	li	$2, 4004
	li	$4, 1
	la	$5, msg
	li	$6, 25
	syscall

	/* fd = open("/dev/tty0", O_WRONLY) ; write(fd, msg, 25) */
	li	$2, 4005
	la	$4, tty0path
	li	$5, 1
	li	$6, 0
	syscall
	move	$4, $2
	li	$2, 4004
	la	$5, msg
	li	$6, 25
	syscall

	/* fd = open("/dev/ttyS0", O_WRONLY) ; write(fd, msg, 25) */
	li	$2, 4005
	la	$4, ttyspath
	li	$5, 1
	li	$6, 0
	syscall
	move	$4, $2
	li	$2, 4004
	la	$5, msg
	li	$6, 25
	syscall

spin:
	b	spin
	nop

	.data
msg:
	.ascii	"STATIC-USERSPACE-RUNNING\n"
tty0path:
	.asciiz	"/dev/tty0"
ttyspath:
	.asciiz	"/dev/ttyS0"
