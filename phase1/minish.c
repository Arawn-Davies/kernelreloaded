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

#define sys_read(fd, buf, n)    sys(4003, (long)(fd), (long)(buf), (long)(n))
#define sys_write(fd, buf, n)   sys(4004, (long)(fd), (long)(buf), (long)(n))
#define sys_exit(code)          sys(4001, (long)(code), 0, 0)
#define sys_fork()              sys(4002, 0, 0, 0)
#define sys_execve(p, av, ev)   sys(4011, (long)(p), (long)(av), (long)(ev))
#define sys_wait4(pid, st)      sys(4114, (long)(pid), (long)(st), 0)
#define sys_chdir(p)            sys(4012, (long)(p), 0, 0)
#define sys_open(p, fl)         sys(4005, (long)(p), (long)(fl), 0)
#define sys_nanosleep(ts, r)    sys(4166, (long)(ts), (long)(r), 0)

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

void __start(void)
{
	static char line[512];
	static char *argv[32];
	static char *envp[] = { "PATH=/bin:/sbin:/usr/bin", 0 };

	/* Prefer romcons's own tty over /dev/console. console=romcons does not
	 * reliably make romcons the preferred console, and the GS console has no
	 * input side at all -- the kernel installs a dummy keyboard driver -- so
	 * reads there return EOF forever. /dev/ttyS0 is TTY_MAJOR minor 64, which
	 * is romcons, and its input comes from the SIO RX FIFO that the PCSX2 log
	 * window's input box writes into. */
	{
		long fd = sys_open("/dev/ttyS0", 2 /* O_RDWR */);
		if (fd >= 0) {
			in_fd = out_fd = (int)fd;
			puts_("\nminish: using /dev/ttyS0 (romcons/SIO)\n");
		}
	}

	puts_("minish: no libc, PS2 Linux\n");

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

		if (streq(argv[0], "exit"))
			sys_exit(0);

		if (streq(argv[0], "cd")) {
			if (argc > 1 && sys_chdir(argv[1]) < 0)
				puts_("cd: failed\n");
			continue;
		}

		pid = sys_fork();
		if (pid == 0) {
			long err;

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
