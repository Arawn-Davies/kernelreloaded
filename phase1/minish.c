/*
 * minish -- a shell with no libc, for PS2 Linux under PCSX2.
 *
 * bash cannot be rebuilt here: the phase 1 toolchain is kernel-only, with no
 * libc.a, no crt1.o and no headers, and a modern cross-glibc will not run on a
 * 2.4 kernel. But every primitive a shell needs is already proven working by
 * the tests in TESTS.md -- static execution, fork, copy-on-write, signals,
 * terminal ioctls and heap growth -- so the shell itself can simply be written
 * without a libc.
 *
 * This also covers execve, the one primitive none of those tests exercised.
 *
 * Build: ee-gcc -nostdlib -static -mno-abicalls -fno-pic -G0 -O2 \
 *               -o minish minish.c
 */

/* o32 syscall: number in v0, args in a0..a3, a3 nonzero on error. */
static long sys(long n, long a, long b, long c)
{
	register long v0 __asm__("$2") = n;
	register long a0 __asm__("$4") = a;
	register long a1 __asm__("$5") = b;
	register long a2 __asm__("$6") = c;
	register long a3 __asm__("$7");

	__asm__ __volatile__("syscall"
		: "+r"(v0), "=r"(a3)
		: "r"(a0), "r"(a1), "r"(a2)
		: "memory", "$1", "$3", "$8", "$9", "$10", "$11", "$12",
		  "$13", "$14", "$15", "$24", "$25");

	return a3 ? -v0 : v0;
}

/* o32 passes the first four arguments in a0..a3 and the rest on the stack, so
 * a five-argument call needs its own frame with arg 5 at 16($sp). */
static long sys5(long n, long a, long b, long c, long d, long e)
{
	register long v0 __asm__("$2") = n;
	register long a0 __asm__("$4") = a;
	register long a1 __asm__("$5") = b;
	register long a2 __asm__("$6") = c;
	register long a3 __asm__("$7") = d;
	register long t0 __asm__("$8") = e;

	__asm__ __volatile__(
		"addiu\t$sp,$sp,-32\n\t"
		"sw\t$8,16($sp)\n\t"
		"syscall\n\t"
		"addiu\t$sp,$sp,32"
		: "+r"(v0), "+r"(a3)
		: "r"(a0), "r"(a1), "r"(a2), "r"(t0)
		: "memory", "$1", "$3", "$9", "$10", "$11", "$12",
		  "$13", "$14", "$15", "$24", "$25");

	return a3 ? -v0 : v0;
}

#define sys_read(fd, buf, n)    sys(4003, (long)(fd), (long)(buf), (long)(n))
#define sys_write(fd, buf, n)   sys(4004, (long)(fd), (long)(buf), (long)(n))
#define sys_exit(code)          sys(4001, (long)(code), 0, 0)
#define sys_fork()              sys(4002, 0, 0, 0)
#define sys_execve(p, av, ev)   sys(4011, (long)(p), (long)(av), (long)(ev))
#define sys_wait4(pid, st)      sys(4114, (long)(pid), (long)(st), 0)
#define sys_chdir(p)            sys(4012, (long)(p), 0, 0)
#define sys_open(p, fl)         sys(4005, (long)(p), (long)(fl), 0)
#define sys_close(fd)           sys(4006, (long)(fd), 0, 0)

/* reboot(magic1, magic2, cmd, arg) -- four arguments, so it goes through the
 * five-argument helper with a harmless extra zero. */
#define LINUX_REBOOT_MAGIC1     0xfee1dead
#define LINUX_REBOOT_MAGIC2     672274793
#define LINUX_REBOOT_CMD_RESTART   0x01234567
#define LINUX_REBOOT_CMD_HALT      0xcdef0123
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc
#define sys_reboot(cmd) \
	sys5(4088, (long)LINUX_REBOOT_MAGIC1, (long)LINUX_REBOOT_MAGIC2, \
	     (long)(cmd), 0, 0)
#define sys_nanosleep(ts, r)    sys(4166, (long)(ts), (long)(r), 0)
/* mount takes five arguments; the fifth goes on the stack, and this helper
 * only passes three. Flags and data are both 0 here, so a4/a5 land as zero
 * often enough to be a coin toss -- pass them explicitly instead. */
#define sys_mount(src, tgt, fs, fl, dt) \
	sys5(4021, (long)(src), (long)(tgt), (long)(fs), (long)(fl), (long)(dt))
#define sys_dup2(o, n)          sys(4063, (long)(o), (long)(n), 0)
#define sys_setsid()            sys(4066, 0, 0, 0)
#define sys_ioctl(fd, rq, a)    sys(4054, (long)(fd), (long)(rq), (long)(a))

/* MIPS ioctl numbers are not the x86 ones: 0x540e is TCSETS here, and
 * TIOCSCTTY is 0x5480. Getting that wrong is silent -- the call just returns
 * EFAULT and no controlling terminal is ever assigned. */
#define TCGETS                  0x540d
#define TCSETS                  0x540e
#define TIOCSCTTY               0x5480

/* asm-mips/termbits.h: c_iflag, c_oflag, c_cflag, c_lflag are the first four
 * words, so c_lflag sits at offset 12 whatever NCCS happens to be. */
#define TERMIOS_LFLAG           3
#define T_ISIG                  0000001
#define T_ICANON                0000002
#define T_ECHO                  0000010
#define T_ECHOE                 0000020
#define T_ECHOK                 0000040

/* Everything goes through these so the shell can move itself onto whichever
 * device actually has a working input side. */
static int in_fd = 0, out_fd = 1;

static void puts_(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	sys_write(out_fd, s, p - s);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static int has_slash(const char *s)
{
	while (*s)
		if (*s++ == '/')
			return 1;
	return 0;
}

/* dst = a + b, truncated to max. */
static void copy2(char *dst, const char *a, const char *b, int max)
{
	int i = 0;

	while (*a && i < max - 1)
		dst[i++] = *a++;
	while (*b && i < max - 1)
		dst[i++] = *b++;
	dst[i] = 0;
}

static void putn(long v)
{
	char buf[12];
	int i = sizeof(buf);

	if (v < 0) {
		puts_("-");
		v = -v;
	}
	buf[--i] = 0;
	do {
		buf[--i] = '0' + (int)(v % 10);
		v /= 10;
	} while (v);
	puts_(&buf[i]);
}

/* Split in place on spaces and tabs. Returns the argument count. */
static int split(char *line, char **argv, int max)
{
	int n = 0;
	char *p = line;

	while (*p && n < max - 1) {
		while (*p == ' ' || *p == '\t')
			*p++ = 0;
		if (!*p)
			break;
		argv[n++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
	}
	argv[n] = 0;
	return n;
}

static void shell(void)
{
	static char line[512];
	static char *argv[32];
	/* bash wants more than PATH: without TERM it probes, and without HOME
	 * it complains on every startup file it cannot find. */
	static char *envp[] = {
		"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
		"HOME=/",
		"TERM=linux",
		"SHELL=/bin/bash",
		"PS1=ps2$ ",
		0
	};

	for (;;) {
		long n, pid, status;
		int argc;
		char *p;

		puts_("# ");

		/* Retry rather than give up. The first version spun forever on
		 * the first EOF, which meant input arriving later could never be
		 * seen -- the same silent-death trap bash fell into. A short sleep
		 * keeps the poll from pegging the EE. */
		{
			static long complained;
			struct { long sec, nsec; } ts;

			for (;;) {
				n = sys_read(in_fd, line, sizeof(line) - 1);
				if (n > 0)
					break;
				if (!complained) {
					complained = 1;
					puts_("minish: read gave nothing, polling for input\n");
				}
				ts.sec = 0;
				ts.nsec = 50000000;   /* 50 ms */
				sys_nanosleep(&ts, 0);
			}
		}

		line[n] = 0;
		for (p = line; *p; p++)
			if (*p == '\n' || *p == '\r')
				*p = 0;

		/* Echo the line back. The tty's own echo is not visible in the
		 * PCSX2 log, so without this there is no way to tell a command
		 * that arrived intact from one that arrived mangled. */
		puts_("minish: got [");
		puts_(line);
		puts_("]\n");

		argc = split(line, argv, 32);
		if (argc == 0)
			continue;

		/* Exiting is pointless here -- this shell is all there is, and
		 * leaving just abandons the console -- so treat it as a request
		 * to shut the machine down, which is what it means on a PS2. */
		if (streq(argv[0], "exit") || streq(argv[0], "poweroff") ||
		    streq(argv[0], "halt")) {
			puts_("minish: powering off\n");
			sys_reboot(LINUX_REBOOT_CMD_POWER_OFF);
			/* Older setups can only halt; take that over hanging. */
			sys_reboot(LINUX_REBOOT_CMD_HALT);
			puts_("minish: poweroff refused, staying up\n");
			continue;
		}

		if (streq(argv[0], "reboot")) {
			puts_("minish: rebooting\n");
			sys_reboot(LINUX_REBOOT_CMD_RESTART);
			puts_("minish: reboot refused, staying up\n");
			continue;
		}

		if (streq(argv[0], "cd")) {
			if (argc > 1 && sys_chdir(argv[1]) < 0)
				puts_("cd: failed\n");
			continue;
		}

		pid = sys_fork();
		if (pid == 0) {
			long err;

			/* Hand the child the tty we know reads, on the three
			 * descriptors it will look for. Without this a child
			 * inherits whatever init was given for /dev/console,
			 * and bash in particular reads fd 0, sees EOF and
			 * exits without a word. */
			if (in_fd != 0) {
				sys_dup2(in_fd, 0);
				sys_dup2(in_fd, 1);
				sys_dup2(in_fd, 2);
			}

			/* No PATH search in execve, so do it here: a bare "ls"
			 * would otherwise always fail, which looks exactly like
			 * a broken input path and is not one. */
			err = sys_execve(argv[0], argv, envp);
			if (!has_slash(argv[0])) {
				static const char *dirs[] = {
					"/bin/", "/sbin/", "/usr/bin/",
					"/usr/sbin/", 0
				};
				static char path[256];
				int i;

				for (i = 0; dirs[i]; i++) {
					copy2(path, dirs[i], argv[0],
					      sizeof(path));
					err = sys_execve(path, argv, envp);
				}
			}
			puts_("minish: exec failed, errno ");
			putn(-err);
			puts_("\n");
			sys_exit(127);
		}
		if (pid < 0) {
			puts_("minish: fork failed\n");
			continue;
		}
		sys_wait4(pid, &status);
	}
}

/* Move this process onto dev and make it the controlling terminal, so that a
 * child shell gets a real session rather than inheriting init's. */
static int take_tty(const char *dev)
{
	long fd = sys_open(dev, 2 /* O_RDWR */);

	if (fd < 0)
		return 0;

	in_fd = out_fd = (int)fd;
	sys_setsid();
	sys_ioctl((int)fd, TIOCSCTTY, 0);

	/* Ask for line-at-a-time with echo rather than trusting whatever the
	 * driver came up with. Without ICANON a read returns as soon as any
	 * character is available, so a typed word arrives in fragments and
	 * each fragment is treated as a command of its own. */
	{
		static long t[16];

		if (sys_ioctl((int)fd, TCGETS, (long)t) >= 0) {
			t[TERMIOS_LFLAG] |= T_ISIG | T_ICANON | T_ECHO |
					    T_ECHOE | T_ECHOK;
			sys_ioctl((int)fd, TCSETS, (long)t);
		}
	}

	sys_dup2((int)fd, 0);
	sys_dup2((int)fd, 1);
	sys_dup2((int)fd, 2);
	return 1;
}

/* Hand the console to bash, and take it back if bash will not stay.
 *
 * The point of minish was always to prove the primitives, not to be the shell.
 * bash is right there in the initrd; run it, and only fall back to the builtin
 * loop if it dies -- reporting how, since a shell that exits on the spot is
 * exactly the failure that hid the missing tty receive path for so long.
 */
static void run_bash(void)
{
	static char *av[] = { "-bash", "-i", 0 };
	static char *ev[] = {
		"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
		"HOME=/",
		"TERM=linux",
		"SHELL=/bin/bash",
		"PS1=ps2:\\w\\$ ",
		0
	};
	long pid, status = 0;

	pid = sys_fork();
	if (pid < 0)
		return;

	if (pid == 0) {
		sys_execve("/bin/bash", av, ev);
		sys_execve("/bin/sh", av, ev);
		sys_exit(127);
	}

	sys_wait4(pid, &status);

	puts_("\nminish: bash exited, status ");
	putn((status >> 8) & 0xff);
	if (status & 0x7f) {
		puts_(", signal ");
		putn(status & 0x7f);
	}
	puts_(" -- falling back to the builtin shell\n");
}

void __start(void)
{
	long pid;

	/* Nothing else is going to: there is no /etc/fstab processing and no
	 * init scripts here, and w, ps, df and free all fail without it. */
	sys_mount("proc", "/proc", "proc", 0, 0);

	/* Two consoles, two shells.
	 *
	 * /dev/ttyS0 is romcons -- TTY_MAJOR minor 64 -- whose input arrives
	 * through SB_GETCHAR from the SIO RX FIFO that the PCSX2 log window's
	 * input box writes into. Handy under an emulator, absent on real
	 * hardware unless something is wired to the SIO port.
	 *
	 * /dev/tty1 is the GS framebuffer console, driven by a USB keyboard
	 * through the input layer and keybdev. That is the one that works on a
	 * real PS2 with a monitor and a keyboard plugged in.
	 *
	 * Neither is guaranteed to exist, so run whichever opens.
	 */
	pid = sys_fork();
	if (pid == 0) {
		if (!take_tty("/dev/tty1"))
			sys_exit(1);
		puts_("\nminish: GS console on /dev/tty1, USB keyboard\n");
		run_bash();
		shell();
		sys_exit(0);
	}

	if (!take_tty("/dev/ttyS0"))
		puts_("minish: no /dev/ttyS0, falling back to inherited fds\n");
	else
		puts_("\nminish: using /dev/ttyS0 (romcons/SIO)\n");

	puts_("minish: no libc, PS2 Linux\n");
	run_bash();
	shell();
	sys_exit(0);
}
