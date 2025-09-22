/*	$OpenBSD: kcov.c,v 1.18 2024/08/23 12:56:26 anton Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/kcov.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct context {
	int c_fd;
	int c_mode;
	unsigned long c_bufsize;
};

static int test_close(struct context *);
static int test_coverage(struct context *);
static int test_dying(struct context *);
static int test_exec(struct context *);
static int test_fdsend(struct context *);
static int test_fork(struct context *);
static int test_open(struct context *);
static int test_remote(struct context *);
static int test_remote_close(struct context *);
static int test_remote_interrupt(struct context *);
static int test_state(struct context *);

static int check_coverage(const unsigned long *, int, unsigned long, int);
static void do_syscall(void);
static void dump(const unsigned long *, int mode);
static void kcov_disable(int);
static void kcov_enable(int, int);
static int kcov_open(void);
static __dead void usage(void);

static const char *self;

int
main(int argc, char *argv[])
{
	struct {
		const char *name;
		int (*fn)(struct context *);
		int coverage;		/* test must produce coverage */
	} tests[] = {
		{ "close",		test_close,		0 },
		{ "coverage",		test_coverage,		1 },
		{ "dying",		test_dying,		-1 },
		{ "exec",		test_exec,		1 },
		{ "fdsend",		test_fdsend,		-1 },
		{ "fork",		test_fork,		1 },
		{ "open",		test_open,		0 },
		{ "remote",		test_remote,		1 },
		{ "remote-close",	test_remote_close,	0 },
		{ "remote-interrupt",	test_remote_interrupt,	-1 },
		{ "state",		test_state,		1 },
		{ NULL,			NULL,			0 },
	};
	struct context ctx;
	const char *errstr;
	unsigned long *cover, frac;
	int c, i;
	int error = 0;
	int prereq = 0;
	int reexec = 0;
	int verbose = 0;

	self = argv[0];

	memset(&ctx, 0, sizeof(ctx));
	ctx.c_bufsize = 256 << 10;

	while ((c = getopt(argc, argv, "b:Em:pv")) != -1)
		switch (c) {
		case 'b':
			frac = strtonum(optarg, 1, 100, &errstr);
			if (frac == 0)
				errx(1, "buffer size fraction %s", errstr);
			else if (frac > ctx.c_bufsize)
				errx(1, "buffer size fraction too large");
			ctx.c_bufsize /= frac;
			break;
		case 'E':
			reexec = 1;
			break;
		case 'm':
			if (strcmp(optarg, "pc") == 0)
				ctx.c_mode = KCOV_MODE_TRACE_PC;
			else if (strcmp(optarg, "cmp") == 0)
				ctx.c_mode = KCOV_MODE_TRACE_CMP;
			else
				errx(1, "unknown mode %s", optarg);
			break;
		case 'p':
			prereq = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (prereq) {
		ctx.c_fd = kcov_open();
		close(ctx.c_fd);
		return 0;
	}

	if (reexec) {
		do_syscall();
		return 0;
	}

	if (ctx.c_mode == 0 || argc != 1)
		usage();
	for (i = 0; tests[i].name != NULL; i++)
		if (strcmp(argv[0], tests[i].name) == 0)
			break;
	if (tests[i].name == NULL)
		errx(1, "%s: no such test", argv[0]);

	ctx.c_fd = kcov_open();
	if (ioctl(ctx.c_fd, KIOSETBUFSIZE, &ctx.c_bufsize) == -1)
		err(1, "ioctl: KIOSETBUFSIZE");
	cover = mmap(NULL, ctx.c_bufsize * sizeof(unsigned long),
	    PROT_READ | PROT_WRITE, MAP_SHARED, ctx.c_fd, 0);
	if (cover == MAP_FAILED)
		err(1, "mmap");

	*cover = 0;
	error = tests[i].fn(&ctx);
	if (verbose)
		dump(cover, ctx.c_mode);
	if (check_coverage(cover, ctx.c_mode, ctx.c_bufsize, tests[i].coverage))
		error = 1;

	if (munmap(cover, ctx.c_bufsize * sizeof(unsigned long)) == -1)
		err(1, "munmap");
	if (ctx.c_fd != -1) {
		if (close(ctx.c_fd) == -1)
			err(1, "close");
	}

	return error;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: kcov [-Epv] [-b fraction] -m mode test\n");
	exit(1);
}

static void
do_syscall(void)
{
	getpid();
}

static int
check_coverage(const unsigned long *cover, int mode, unsigned long maxsize,
    int nonzero)
{
	unsigned long arg1, arg2, exp, i, pc, type;
	int error = 0;

	if (nonzero == -1) {
		return 0;
	} else if (nonzero && cover[0] == 0) {
		warnx("coverage empty (count=0)");
		return 1;
	} else if (!nonzero && cover[0] != 0) {
		warnx("coverage not empty (count=%lu)", *cover);
		return 1;
	} else if (cover[0] >= maxsize) {
		warnx("coverage overflow (count=%lu, max=%lu)",
		    *cover, maxsize);
		return 1;
	}

	if (mode == KCOV_MODE_TRACE_CMP) {
		if (*cover * 4 >= maxsize) {
			warnx("coverage cmp overflow (count=%lu, max=%lu)",
			    *cover * 4, maxsize);
			return 1;
		}

		for (i = 0; i < cover[0]; i++) {
			type = cover[i * 4 + 1];
			arg1 = cover[i * 4 + 2];
			arg2 = cover[i * 4 + 3];
			pc = cover[i * 4 + 4];

			exp = type >> 1;
			if (exp <= 3)
				continue;

			warnx("coverage cmp invalid size (i=%lu, exp=%lx, "
			    "const=%ld, arg1=%lu, arg2=%lu, pc=%p)\n",
			    i, exp, type & 0x1, arg1, arg2, (void *)pc);
			error = 1;
		}
	}

	return error;
}

static void
dump(const unsigned long *cover, int mode)
{
	unsigned long i;
	int stride = 1;

	if (mode == KCOV_MODE_TRACE_CMP)
		stride = 4;

	for (i = 0; i < cover[0]; i++)
		printf("%p\n", (void *)cover[i * stride + stride]);
}

static int
kcov_open(void)
{
	int fd;

	fd = open("/dev/kcov", O_RDWR);
	if (fd == -1)
		err(1, "open: /dev/kcov");
	return fd;
}

static void
kcov_enable(int fd, int mode)
{
	if (ioctl(fd, KIOENABLE, &mode) == -1)
		err(1, "ioctl: KIOENABLE");
}

static void
kcov_disable(int fd)
{
	if (ioctl(fd, KIODISABLE) == -1)
		err(1, "ioctl: KIODISABLE");
}

/*
 * Close before mmap.
 */
static int
test_close(struct context *ctx)
{
	int fd;

	fd = kcov_open();
	close(fd);
	return 0;
}

/*
 * Coverage of current thread.
 */
static int
test_coverage(struct context *ctx)
{
	kcov_enable(ctx->c_fd, ctx->c_mode);
	do_syscall();
	kcov_disable(ctx->c_fd);
	return 0;
}

static void *
closer(void *arg)
{
	struct context *ctx = arg;

	close(ctx->c_fd);
	return NULL;
}

/*
 * Close kcov descriptor in another thread during tracing.
 */
static int
test_dying(struct context *ctx)
{
	pthread_t th;
	int error;

	kcov_enable(ctx->c_fd, ctx->c_mode);

	if ((error = pthread_create(&th, NULL, closer, (void *)ctx)))
		errc(1, error, "pthread_create");
	if ((error = pthread_join(th, NULL)))
		errc(1, error, "pthread_join");

	error = 0;
	if (close(ctx->c_fd) == -1) {
		if (errno != EBADF)
			err(1, "close");
	} else {
		warnx("expected kcov descriptor to be closed");
		error = 1;
	}
	ctx->c_fd = -1;

	return error;
}

/*
 * Coverage of thread after exec.
 */
static int
test_exec(struct context *ctx)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		kcov_enable(ctx->c_fd, ctx->c_mode);
		execlp(self, self, "-E", NULL);
		_exit(1);
	}

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (WIFSIGNALED(status)) {
		warnx("terminated by signal (%d)", WTERMSIG(status));
		return 1;
	} else if (WEXITSTATUS(status) != 0) {
		warnx("non-zero exit (%d)", WEXITSTATUS(status));
		return 1;
	}

	/* Upon exit, the kcov descriptor must be reusable again. */
	kcov_enable(ctx->c_fd, ctx->c_mode);
	kcov_disable(ctx->c_fd);

	return 0;
}

/*
 * File descriptor send/receive is not allowed since remote coverage is tied to
 * the current process.
 */
static int
test_fdsend(struct context *ctx)
{
	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		unsigned char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct cmsghdr *cmsg;
	int pair[2];

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == -1)
		err(1, "socketpair");

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = ctx->c_fd;
	if (sendmsg(pair[1], &msg, 0) != -1)
		errx(1, "sendmsg: expected error");

	close(pair[0]);
	close(pair[1]);
	return 0;
}

/*
 * Coverage of thread after fork.
 */
static int
test_fork(struct context *ctx)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		kcov_enable(ctx->c_fd, ctx->c_mode);
		do_syscall();
		_exit(0);
	}

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (WIFSIGNALED(status)) {
		warnx("terminated by signal (%d)", WTERMSIG(status));
		return 1;
	} else if (WEXITSTATUS(status) != 0) {
		warnx("non-zero exit (%d)", WEXITSTATUS(status));
		return 1;
	}

	/* Upon exit, the kcov descriptor must be reusable again. */
	kcov_enable(ctx->c_fd, ctx->c_mode);
	kcov_disable(ctx->c_fd);

	return 0;
}

/*
 * Open /dev/kcov more than once.
 */
static int
test_open(struct context *ctx)
{
	unsigned long *cover;
	int fd;
	int error = 0;

	fd = kcov_open();
	if (ioctl(fd, KIOSETBUFSIZE, &ctx->c_bufsize) == -1)
		err(1, "ioctl: KIOSETBUFSIZE");
	cover = mmap(NULL, ctx->c_bufsize * sizeof(unsigned long),
	    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (cover == MAP_FAILED)
		err(1, "mmap");

	kcov_enable(fd, ctx->c_mode);
	do_syscall();
	kcov_disable(fd);

	error = check_coverage(cover, ctx->c_mode, ctx->c_bufsize, 1);

	if (munmap(cover, ctx->c_bufsize * sizeof(unsigned long)))
		err(1, "munmap");
	close(fd);

	return error;
}

/*
 * Remote taskq coverage. One reliable way to trigger a task on behalf of the
 * running process is to monitor a kqueue file descriptor using kqueue.
 */
static int
test_remote(struct context *ctx)
{
	struct kio_remote_attach remote = {
		.subsystem	= KCOV_REMOTE_COMMON,
		.id		= 0,
	};
	struct kevent kev;
	int kq1, kq2, pip[2];
	int x = 0;

	if (ioctl(ctx->c_fd, KIOREMOTEATTACH, &remote) == -1)
		err(1, "ioctl: KIOREMOTEATTACH");
	kcov_enable(ctx->c_fd, ctx->c_mode);

	kq1 = kqueue();
	if (kq1 == -1)
		err(1, "kqueue");
	kq2 = kqueue();
	if (kq1 == -1)
		err(1, "kqueue");
	EV_SET(&kev, kq2, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (kevent(kq1, &kev, 1, NULL, 0, NULL) == -1)
		err(1, "kqueue");

	if (pipe(pip) == -1)
		err(1, "pipe");

	EV_SET(&kev, pip[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (kevent(kq2, &kev, 1, NULL, 0, NULL) == -1)
		err(1, "kqueue");
	(void)write(pip[1], &x, sizeof(x));

	if (kevent(kq1, NULL, 0, &kev, 1, NULL) == -1)
		err(1, "kevent");

	kcov_disable(ctx->c_fd);

	return 0;
}

/*
 * Close with remote coverage enabled.
 */
static int
test_remote_close(struct context *ctx)
{
	struct kio_remote_attach remote = {
		.subsystem	= KCOV_REMOTE_COMMON,
		.id		= 0,
	};

	if (ioctl(ctx->c_fd, KIOREMOTEATTACH, &remote) == -1)
		err(1, "ioctl: KIOREMOTEATTACH");
	kcov_enable(ctx->c_fd, ctx->c_mode);
	if (close(ctx->c_fd) == -1)
		err(1, "close");
	ctx->c_fd = kcov_open();
	return 0;
}

/*
 * Remote interrupt coverage. There's no reliable way to enter a remote section
 * in interrupt context. This test can however by used to examine the coverage
 * collected in interrupt context:
 *
 *     $ until [ -s cov ]; do kcov -v -m pc remote-interrupt >cov; done
 */
static int
test_remote_interrupt(struct context *ctx)
{
	struct kio_remote_attach remote = {
		.subsystem	= KCOV_REMOTE_COMMON,
		.id		= 0,
	};
	int i;

	if (ioctl(ctx->c_fd, KIOREMOTEATTACH, &remote) == -1)
		err(1, "ioctl: KIOREMOTEATTACH");
	kcov_enable(ctx->c_fd, ctx->c_mode);

	for (i = 0; i < 100; i++)
		(void)getpid();

	kcov_disable(ctx->c_fd);

	return 0;
}

/*
 * State transitions.
 */
static int
test_state(struct context *ctx)
{
	if (ioctl(ctx->c_fd, KIOENABLE, &ctx->c_mode) == -1) {
		warn("KIOSETBUFSIZE -> KIOENABLE");
		return 1;
	}
	if (ioctl(ctx->c_fd, KIODISABLE) == -1) {
		warn("KIOENABLE -> KIODISABLE");
		return 1;
	}
	if (ioctl(ctx->c_fd, KIOSETBUFSIZE, 0) != -1) {
		warnx("KIOSETBUFSIZE -> KIOSETBUFSIZE");
		return 1;
	}
	if (ioctl(ctx->c_fd, KIODISABLE) != -1) {
		warnx("KIOSETBUFSIZE -> KIODISABLE");
		return 1;
	}

	kcov_enable(ctx->c_fd, ctx->c_mode);
	if (ioctl(ctx->c_fd, KIOENABLE, &ctx->c_mode) != -1) {
		warnx("KIOENABLE -> KIOENABLE");
		return 1;
	}
	if (ioctl(ctx->c_fd, KIOSETBUFSIZE, 0) != -1) {
		warnx("KIOENABLE -> KIOSETBUFSIZE");
		return 1;
	}
	kcov_disable(ctx->c_fd);

	return 0;
}
