/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE

/* libc-specific include files
 * The program may be built in 3 ways:
 *   $(CC) -nostdlib -include /path/to/nolibc.h => NOLIBC already defined
 *   $(CC) -nostdlib -I/path/to/nolibc/sysroot  => _NOLIBC_* guards are present
 *   $(CC) with default libc                    => NOLIBC* never defined
 */
#ifndef NOLIBC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _NOLIBC_STDIO_H
/* standard libcs need more includes */
#include <linux/reboot.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#endif
#endif

/* will be used by nolibc by getenv() */
char **environ;

/* definition of a series of tests */
struct test {
	const char *name;              /* test name */
	int (*func)(int min, int max); /* handler */
};

#ifndef _NOLIBC_STDLIB_H
char *itoa(int i)
{
	static char buf[12];
	int ret;

	ret = snprintf(buf, sizeof(buf), "%d", i);
	return (ret >= 0 && ret < sizeof(buf)) ? buf : "#err";
}
#endif

#define CASE_ERR(err) \
	case err: return #err

/* returns the error name (e.g. "ENOENT") for common errors, "SUCCESS" for 0,
 * or the decimal value for less common ones.
 */
const char *errorname(int err)
{
	switch (err) {
	case 0: return "SUCCESS";
	CASE_ERR(EPERM);
	CASE_ERR(ENOENT);
	CASE_ERR(ESRCH);
	CASE_ERR(EINTR);
	CASE_ERR(EIO);
	CASE_ERR(ENXIO);
	CASE_ERR(E2BIG);
	CASE_ERR(ENOEXEC);
	CASE_ERR(EBADF);
	CASE_ERR(ECHILD);
	CASE_ERR(EAGAIN);
	CASE_ERR(ENOMEM);
	CASE_ERR(EACCES);
	CASE_ERR(EFAULT);
	CASE_ERR(ENOTBLK);
	CASE_ERR(EBUSY);
	CASE_ERR(EEXIST);
	CASE_ERR(EXDEV);
	CASE_ERR(ENODEV);
	CASE_ERR(ENOTDIR);
	CASE_ERR(EISDIR);
	CASE_ERR(EINVAL);
	CASE_ERR(ENFILE);
	CASE_ERR(EMFILE);
	CASE_ERR(ENOTTY);
	CASE_ERR(ETXTBSY);
	CASE_ERR(EFBIG);
	CASE_ERR(ENOSPC);
	CASE_ERR(ESPIPE);
	CASE_ERR(EROFS);
	CASE_ERR(EMLINK);
	CASE_ERR(EPIPE);
	CASE_ERR(EDOM);
	CASE_ERR(ERANGE);
	CASE_ERR(ENOSYS);
	CASE_ERR(EOVERFLOW);
	default:
		return itoa(err);
	}
}

static void putcharn(char c, size_t n)
{
	char buf[64];

	memset(buf, c, n);
	buf[n] = '\0';
	fputs(buf, stdout);
}

static int pad_spc(int llen, int cnt, const char *fmt, ...)
{
	va_list args;
	int ret;

	putcharn(' ', cnt - llen);

	va_start(args, fmt);
	ret = vfprintf(stdout, fmt, args);
	va_end(args);
	return ret < 0 ? ret : ret + cnt - llen;
}

/* The tests below are intended to be used by the macroes, which evaluate
 * expression <expr>, print the status to stdout, and update the "ret"
 * variable to count failures. The functions themselves return the number
 * of failures, thus either 0 or 1.
 */

#define EXPECT_ZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_zr(expr, llen); } while (0)

static int expect_zr(int expr, int llen)
{
	int ret = !(expr == 0);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_NZ(cond, expr, val)			\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_nz(expr, llen; } while (0)

static int expect_nz(int expr, int llen)
{
	int ret = !(expr != 0);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_EQ(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_eq(expr, llen, val); } while (0)

static int expect_eq(uint64_t expr, int llen, uint64_t val)
{
	int ret = !(expr == val);

	llen += printf(" = %lld ", (long long)expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_NE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_ne(expr, llen, val); } while (0)

static int expect_ne(int expr, int llen, int val)
{
	int ret = !(expr != val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_GE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_ge(expr, llen, val); } while (0)

static int expect_ge(int expr, int llen, int val)
{
	int ret = !(expr >= val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_GT(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_gt(expr, llen, val); } while (0)

static int expect_gt(int expr, int llen, int val)
{
	int ret = !(expr > val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_LE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_le(expr, llen, val); } while (0)

static int expect_le(int expr, int llen, int val)
{
	int ret = !(expr <= val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_LT(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_lt(expr, llen, val); } while (0)

static int expect_lt(int expr, int llen, int val)
{
	int ret = !(expr < val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_SYSZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_syszr(expr, llen); } while (0)

static int expect_syszr(int expr, int llen)
{
	int ret = 0;

	if (expr) {
		ret = 1;
		llen += printf(" = %d %s ", expr, errorname(errno));
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += printf(" = %d ", expr);
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_SYSEQ(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_syseq(expr, llen, val); } while (0)

static int expect_syseq(int expr, int llen, int val)
{
	int ret = 0;

	if (expr != val) {
		ret = 1;
		llen += printf(" = %d %s ", expr, errorname(errno));
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += printf(" = %d ", expr);
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_SYSNE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_sysne(expr, llen, val); } while (0)

static int expect_sysne(int expr, int llen, int val)
{
	int ret = 0;

	if (expr == val) {
		ret = 1;
		llen += printf(" = %d %s ", expr, errorname(errno));
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += printf(" = %d ", expr);
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_SYSER2(cond, expr, expret, experr1, experr2)		\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_syserr2(expr, expret, experr1, experr2, llen); } while (0)

#define EXPECT_SYSER(cond, expr, expret, experr)			\
	EXPECT_SYSER2(cond, expr, expret, experr, 0)

static int expect_syserr2(int expr, int expret, int experr1, int experr2, int llen)
{
	int ret = 0;
	int _errno = errno;

	llen += printf(" = %d %s ", expr, errorname(_errno));
	if (expr != expret || (_errno != experr1 && _errno != experr2)) {
		ret = 1;
		if (experr2 == 0)
			llen += printf(" != (%d %s) ", expret, errorname(experr1));
		else
			llen += printf(" != (%d %s %s) ", expret, errorname(experr1), errorname(experr2));
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_PTRZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_ptrzr(expr, llen); } while (0)

static int expect_ptrzr(const void *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%p> ", expr);
	if (expr) {
		ret = 1;
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_PTRNZ(cond, expr)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_ptrnz(expr, llen); } while (0)

static int expect_ptrnz(const void *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%p> ", expr);
	if (!expr) {
		ret = 1;
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STRZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_strzr(expr, llen); } while (0)

static int expect_strzr(const char *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (expr) {
		ret = 1;
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STRNZ(cond, expr)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_strnz(expr, llen); } while (0)

static int expect_strnz(const char *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (!expr) {
		ret = 1;
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STREQ(cond, expr, cmp)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_streq(expr, llen, cmp); } while (0)

static int expect_streq(const char *expr, int llen, const char *cmp)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (strcmp(expr, cmp) != 0) {
		ret = 1;
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STRNE(cond, expr, cmp)				\
	do { if (!cond) pad_spc(llen, 64, "[SKIPPED]\n"); else ret += expect_strne(expr, llen, cmp); } while (0)

static int expect_strne(const char *expr, int llen, const char *cmp)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (strcmp(expr, cmp) == 0) {
		ret = 1;
		llen += pad_spc(llen, 64, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 64, " [OK]\n");
	}
	return ret;
}


/* declare tests based on line numbers. There must be exactly one test per line. */
#define CASE_TEST(name) \
	case __LINE__: llen += printf("%d %s", test, #name);


/* used by some syscall tests below */
int test_getdents64(const char *dir)
{
	char buffer[4096];
	int fd, ret;
	int err;

	ret = fd = open(dir, O_RDONLY | O_DIRECTORY, 0);
	if (ret < 0)
		return ret;

	ret = getdents64(fd, (void *)buffer, sizeof(buffer));
	err = errno;
	close(fd);

	errno = err;
	return ret;
}

static int test_getpagesize(void)
{
	long x = getpagesize();
	int c;

	if (x < 0)
		return x;

#if defined(__x86_64__) || defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
	/*
	 * x86 family is always 4K page.
	 */
	c = (x == 4096);
#elif defined(__aarch64__)
	/*
	 * Linux aarch64 supports three values of page size: 4K, 16K, and 64K
	 * which are selected at kernel compilation time.
	 */
	c = (x == 4096 || x == (16 * 1024) || x == (64 * 1024));
#else
	/*
	 * Assuming other architectures must have at least 4K page.
	 */
	c = (x >= 4096);
#endif

	return !c;
}

static int test_fork(void)
{
	int status;
	pid_t pid;

	/* flush the printf buffer to avoid child flush it */
	fflush(stdout);
	fflush(stderr);

	pid = fork();

	switch (pid) {
	case -1:
		return 1;

	case 0:
		exit(123);

	default:
		pid = waitpid(pid, &status, 0);

		return pid == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 123;
	}
}

static int test_stat_timestamps(void)
{
	struct stat st;

	if (sizeof(st.st_atim.tv_sec) != sizeof(st.st_atime))
		return 1;

	if (stat("/proc/self/", &st))
		return 1;

	if (st.st_atim.tv_sec != st.st_atime || st.st_atim.tv_nsec > 1000000000)
		return 1;

	if (st.st_mtim.tv_sec != st.st_mtime || st.st_mtim.tv_nsec > 1000000000)
		return 1;

	if (st.st_ctim.tv_sec != st.st_ctime || st.st_ctim.tv_nsec > 1000000000)
		return 1;

	return 0;
}

/* Run syscall tests between IDs <min> and <max>.
 * Return 0 on success, non-zero on failure.
 */
int run_syscall(int min, int max)
{
	struct timeval tv;
	struct timezone tz;
	struct stat stat_buf;
	int euid0;
	int proc;
	int test;
	int tmp;
	int ret = 0;
	void *p1, *p2;

	/* <proc> indicates whether or not /proc is mounted */
	proc = stat("/proc", &stat_buf) == 0;

	/* this will be used to skip certain tests that can't be run unprivileged */
	euid0 = geteuid() == 0;

	for (test = min; test >= 0 && test <= max; test++) {
		int llen = 0; /* line length */

		/* avoid leaving empty lines below, this will insert holes into
		 * test numbers.
		 */
		switch (test + __LINE__ + 1) {
		CASE_TEST(getpid);            EXPECT_SYSNE(1, getpid(), -1); break;
		CASE_TEST(getppid);           EXPECT_SYSNE(1, getppid(), -1); break;
#ifdef NOLIBC
		CASE_TEST(gettid);            EXPECT_SYSNE(1, gettid(), -1); break;
#endif
		CASE_TEST(getpgid_self);      EXPECT_SYSNE(1, getpgid(0), -1); break;
		CASE_TEST(getpgid_bad);       EXPECT_SYSER(1, getpgid(-1), -1, ESRCH); break;
		CASE_TEST(kill_0);            EXPECT_SYSZR(1, kill(getpid(), 0)); break;
		CASE_TEST(kill_CONT);         EXPECT_SYSZR(1, kill(getpid(), 0)); break;
		CASE_TEST(kill_BADPID);       EXPECT_SYSER(1, kill(INT_MAX, 0), -1, ESRCH); break;
		CASE_TEST(sbrk);              if ((p1 = p2 = sbrk(4096)) != (void *)-1) p2 = sbrk(-4096); EXPECT_SYSZR(1, (p2 == (void *)-1) || p2 == p1); break;
		CASE_TEST(brk);               EXPECT_SYSZR(1, brk(sbrk(0))); break;
		CASE_TEST(chdir_root);        EXPECT_SYSZR(1, chdir("/")); break;
		CASE_TEST(chdir_dot);         EXPECT_SYSZR(1, chdir(".")); break;
		CASE_TEST(chdir_blah);        EXPECT_SYSER(1, chdir("/blah"), -1, ENOENT); break;
		CASE_TEST(chmod_net);         EXPECT_SYSZR(proc, chmod("/proc/self/net", 0555)); break;
		CASE_TEST(chmod_self);        EXPECT_SYSER(proc, chmod("/proc/self", 0555), -1, EPERM); break;
		CASE_TEST(chown_self);        EXPECT_SYSER(proc, chown("/proc/self", 0, 0), -1, EPERM); break;
		CASE_TEST(chroot_root);       EXPECT_SYSZR(euid0, chroot("/")); break;
		CASE_TEST(chroot_blah);       EXPECT_SYSER(1, chroot("/proc/self/blah"), -1, ENOENT); break;
		CASE_TEST(chroot_exe);        EXPECT_SYSER(proc, chroot("/proc/self/exe"), -1, ENOTDIR); break;
		CASE_TEST(close_m1);          EXPECT_SYSER(1, close(-1), -1, EBADF); break;
		CASE_TEST(close_dup);         EXPECT_SYSZR(1, close(dup(0))); break;
		CASE_TEST(dup_0);             tmp = dup(0);  EXPECT_SYSNE(1, tmp, -1); close(tmp); break;
		CASE_TEST(dup_m1);            tmp = dup(-1); EXPECT_SYSER(1, tmp, -1, EBADF); if (tmp != -1) close(tmp); break;
		CASE_TEST(dup2_0);            tmp = dup2(0, 100);  EXPECT_SYSNE(1, tmp, -1); close(tmp); break;
		CASE_TEST(dup2_m1);           tmp = dup2(-1, 100); EXPECT_SYSER(1, tmp, -1, EBADF); if (tmp != -1) close(tmp); break;
		CASE_TEST(dup3_0);            tmp = dup3(0, 100, 0);  EXPECT_SYSNE(1, tmp, -1); close(tmp); break;
		CASE_TEST(dup3_m1);           tmp = dup3(-1, 100, 0); EXPECT_SYSER(1, tmp, -1, EBADF); if (tmp != -1) close(tmp); break;
		CASE_TEST(execve_root);       EXPECT_SYSER(1, execve("/", (char*[]){ [0] = "/", [1] = NULL }, NULL), -1, EACCES); break;
		CASE_TEST(fork);              EXPECT_SYSZR(1, test_fork()); break;
		CASE_TEST(getdents64_root);   EXPECT_SYSNE(1, test_getdents64("/"), -1); break;
		CASE_TEST(getdents64_null);   EXPECT_SYSER(1, test_getdents64("/dev/null"), -1, ENOTDIR); break;
		CASE_TEST(gettimeofday_tv);   EXPECT_SYSZR(1, gettimeofday(&tv, NULL)); break;
		CASE_TEST(gettimeofday_tv_tz);EXPECT_SYSZR(1, gettimeofday(&tv, &tz)); break;
		CASE_TEST(getpagesize);       EXPECT_SYSZR(1, test_getpagesize()); break;
		CASE_TEST(ioctl_tiocinq);     EXPECT_SYSZR(1, ioctl(0, TIOCINQ, &tmp)); break;
		CASE_TEST(ioctl_tiocinq);     EXPECT_SYSZR(1, ioctl(0, TIOCINQ, &tmp)); break;
		CASE_TEST(link_root1);        EXPECT_SYSER(1, link("/", "/"), -1, EEXIST); break;
		CASE_TEST(link_blah);         EXPECT_SYSER(1, link("/proc/self/blah", "/blah"), -1, ENOENT); break;
		CASE_TEST(link_dir);          EXPECT_SYSER(euid0, link("/", "/blah"), -1, EPERM); break;
		CASE_TEST(link_cross);        EXPECT_SYSER(proc, link("/proc/self/net", "/blah"), -1, EXDEV); break;
		CASE_TEST(lseek_m1);          EXPECT_SYSER(1, lseek(-1, 0, SEEK_SET), -1, EBADF); break;
		CASE_TEST(lseek_0);           EXPECT_SYSER(1, lseek(0, 0, SEEK_SET), -1, ESPIPE); break;
		CASE_TEST(mkdir_root);        EXPECT_SYSER(1, mkdir("/", 0755), -1, EEXIST); break;
		CASE_TEST(open_tty);          EXPECT_SYSNE(1, tmp = open("/dev/null", 0), -1); if (tmp != -1) close(tmp); break;
		CASE_TEST(open_blah);         EXPECT_SYSER(1, tmp = open("/proc/self/blah", 0), -1, ENOENT); if (tmp != -1) close(tmp); break;
		CASE_TEST(poll_null);         EXPECT_SYSZR(1, poll(NULL, 0, 0)); break;
		CASE_TEST(poll_stdout);       EXPECT_SYSNE(1, ({ struct pollfd fds = { 1, POLLOUT, 0}; poll(&fds, 1, 0); }), -1); break;
		CASE_TEST(poll_fault);        EXPECT_SYSER(1, poll((void *)1, 1, 0), -1, EFAULT); break;
		CASE_TEST(prctl);             EXPECT_SYSER(1, prctl(PR_SET_NAME, (unsigned long)NULL, 0, 0, 0), -1, EFAULT); break;
		CASE_TEST(read_badf);         EXPECT_SYSER(1, read(-1, &tmp, 1), -1, EBADF); break;
		CASE_TEST(sched_yield);       EXPECT_SYSZR(1, sched_yield()); break;
		CASE_TEST(select_null);       EXPECT_SYSZR(1, ({ struct timeval tv = { 0 }; select(0, NULL, NULL, NULL, &tv); })); break;
		CASE_TEST(select_stdout);     EXPECT_SYSNE(1, ({ fd_set fds; FD_ZERO(&fds); FD_SET(1, &fds); select(2, NULL, &fds, NULL, NULL); }), -1); break;
		CASE_TEST(select_fault);      EXPECT_SYSER(1, select(1, (void *)1, NULL, NULL, 0), -1, EFAULT); break;
		CASE_TEST(stat_blah);         EXPECT_SYSER(1, stat("/proc/self/blah", &stat_buf), -1, ENOENT); break;
		CASE_TEST(stat_fault);        EXPECT_SYSER(1, stat(NULL, &stat_buf), -1, EFAULT); break;
		CASE_TEST(stat_timestamps);   EXPECT_SYSZR(1, test_stat_timestamps()); break;
		CASE_TEST(symlink_root);      EXPECT_SYSER(1, symlink("/", "/"), -1, EEXIST); break;
		CASE_TEST(unlink_root);       EXPECT_SYSER(1, unlink("/"), -1, EISDIR); break;
		CASE_TEST(unlink_blah);       EXPECT_SYSER(1, unlink("/proc/self/blah"), -1, ENOENT); break;
		CASE_TEST(wait_child);        EXPECT_SYSER(1, wait(&tmp), -1, ECHILD); break;
		CASE_TEST(waitpid_min);       EXPECT_SYSER(1, waitpid(INT_MIN, &tmp, WNOHANG), -1, ESRCH); break;
		CASE_TEST(waitpid_child);     EXPECT_SYSER(1, waitpid(getpid(), &tmp, WNOHANG), -1, ECHILD); break;
		CASE_TEST(write_badf);        EXPECT_SYSER(1, write(-1, &tmp, 1), -1, EBADF); break;
		CASE_TEST(write_zero);        EXPECT_SYSZR(1, write(1, &tmp, 0)); break;
		CASE_TEST(syscall_noargs);    EXPECT_SYSEQ(1, syscall(__NR_getpid), getpid()); break;
		CASE_TEST(syscall_args);      EXPECT_SYSER(1, syscall(__NR_statx, 0, NULL, 0, 0, NULL), -1, EFAULT); break;
		case __LINE__:
			return ret; /* must be last */
		/* note: do not set any defaults so as to permit holes above */
		}
	}
	return ret;
}

int run_stdlib(int min, int max)
{
	int test;
	int tmp;
	int ret = 0;
	void *p1, *p2;

	for (test = min; test >= 0 && test <= max; test++) {
		int llen = 0; /* line length */

		/* avoid leaving empty lines below, this will insert holes into
		 * test numbers.
		 */
		switch (test + __LINE__ + 1) {
		CASE_TEST(getenv_TERM);        EXPECT_STRNZ(1, getenv("TERM")); break;
		CASE_TEST(getenv_blah);        EXPECT_STRZR(1, getenv("blah")); break;
		CASE_TEST(setcmp_blah_blah);   EXPECT_EQ(1, strcmp("blah", "blah"), 0); break;
		CASE_TEST(setcmp_blah_blah2);  EXPECT_NE(1, strcmp("blah", "blah2"), 0); break;
		CASE_TEST(setncmp_blah_blah);  EXPECT_EQ(1, strncmp("blah", "blah", 10), 0); break;
		CASE_TEST(setncmp_blah_blah4); EXPECT_EQ(1, strncmp("blah", "blah4", 4), 0); break;
		CASE_TEST(setncmp_blah_blah5); EXPECT_NE(1, strncmp("blah", "blah5", 5), 0); break;
		CASE_TEST(setncmp_blah_blah6); EXPECT_NE(1, strncmp("blah", "blah6", 6), 0); break;
		CASE_TEST(strchr_foobar_o);    EXPECT_STREQ(1, strchr("foobar", 'o'), "oobar"); break;
		CASE_TEST(strchr_foobar_z);    EXPECT_STRZR(1, strchr("foobar", 'z')); break;
		CASE_TEST(strrchr_foobar_o);   EXPECT_STREQ(1, strrchr("foobar", 'o'), "obar"); break;
		CASE_TEST(strrchr_foobar_z);   EXPECT_STRZR(1, strrchr("foobar", 'z')); break;
		CASE_TEST(memcmp_20_20);       EXPECT_EQ(1, memcmp("aaa\x20", "aaa\x20", 4), 0); break;
		CASE_TEST(memcmp_20_60);       EXPECT_LT(1, memcmp("aaa\x20", "aaa\x60", 4), 0); break;
		CASE_TEST(memcmp_60_20);       EXPECT_GT(1, memcmp("aaa\x60", "aaa\x20", 4), 0); break;
		CASE_TEST(memcmp_20_e0);       EXPECT_LT(1, memcmp("aaa\x20", "aaa\xe0", 4), 0); break;
		CASE_TEST(memcmp_e0_20);       EXPECT_GT(1, memcmp("aaa\xe0", "aaa\x20", 4), 0); break;
		CASE_TEST(memcmp_80_e0);       EXPECT_LT(1, memcmp("aaa\x80", "aaa\xe0", 4), 0); break;
		CASE_TEST(memcmp_e0_80);       EXPECT_GT(1, memcmp("aaa\xe0", "aaa\x80", 4), 0); break;
		CASE_TEST(limit_int8_max);          EXPECT_EQ(1, INT8_MAX,         (int8_t)          0x7f); break;
		CASE_TEST(limit_int8_min);          EXPECT_EQ(1, INT8_MIN,         (int8_t)          0x80); break;
		CASE_TEST(limit_uint8_max);         EXPECT_EQ(1, UINT8_MAX,        (uint8_t)         0xff); break;
		CASE_TEST(limit_int16_max);         EXPECT_EQ(1, INT16_MAX,        (int16_t)         0x7fff); break;
		CASE_TEST(limit_int16_min);         EXPECT_EQ(1, INT16_MIN,        (int16_t)         0x8000); break;
		CASE_TEST(limit_uint16_max);        EXPECT_EQ(1, UINT16_MAX,       (uint16_t)        0xffff); break;
		CASE_TEST(limit_int32_max);         EXPECT_EQ(1, INT32_MAX,        (int32_t)         0x7fffffff); break;
		CASE_TEST(limit_int32_min);         EXPECT_EQ(1, INT32_MIN,        (int32_t)         0x80000000); break;
		CASE_TEST(limit_uint32_max);        EXPECT_EQ(1, UINT32_MAX,       (uint32_t)        0xffffffff); break;
		CASE_TEST(limit_int64_max);         EXPECT_EQ(1, INT64_MAX,        (int64_t)         0x7fffffffffffffff); break;
		CASE_TEST(limit_int64_min);         EXPECT_EQ(1, INT64_MIN,        (int64_t)         0x8000000000000000); break;
		CASE_TEST(limit_uint64_max);        EXPECT_EQ(1, UINT64_MAX,       (uint64_t)        0xffffffffffffffff); break;
		CASE_TEST(limit_int_least8_max);    EXPECT_EQ(1, INT_LEAST8_MAX,   (int_least8_t)    0x7f); break;
		CASE_TEST(limit_int_least8_min);    EXPECT_EQ(1, INT_LEAST8_MIN,   (int_least8_t)    0x80); break;
		CASE_TEST(limit_uint_least8_max);   EXPECT_EQ(1, UINT_LEAST8_MAX,  (uint_least8_t)   0xff); break;
		CASE_TEST(limit_int_least16_max);   EXPECT_EQ(1, INT_LEAST16_MAX,  (int_least16_t)   0x7fff); break;
		CASE_TEST(limit_int_least16_min);   EXPECT_EQ(1, INT_LEAST16_MIN,  (int_least16_t)   0x8000); break;
		CASE_TEST(limit_uint_least16_max);  EXPECT_EQ(1, UINT_LEAST16_MAX, (uint_least16_t)  0xffff); break;
		CASE_TEST(limit_int_least32_max);   EXPECT_EQ(1, INT_LEAST32_MAX,  (int_least32_t)   0x7fffffff); break;
		CASE_TEST(limit_int_least32_min);   EXPECT_EQ(1, INT_LEAST32_MIN,  (int_least32_t)   0x80000000); break;
		CASE_TEST(limit_uint_least32_max);  EXPECT_EQ(1, UINT_LEAST32_MAX, (uint_least32_t)  0xffffffffU); break;
		CASE_TEST(limit_int_least64_min);   EXPECT_EQ(1, INT_LEAST64_MIN,  (int_least64_t)   0x8000000000000000LL); break;
		CASE_TEST(limit_int_least64_max);   EXPECT_EQ(1, INT_LEAST64_MAX,  (int_least64_t)   0x7fffffffffffffffLL); break;
		CASE_TEST(limit_uint_least64_max);  EXPECT_EQ(1, UINT_LEAST64_MAX, (uint_least64_t)  0xffffffffffffffffULL); break;
		CASE_TEST(limit_int_fast8_max);     EXPECT_EQ(1, INT_FAST8_MAX,    (int_fast8_t)     0x7f); break;
		CASE_TEST(limit_int_fast8_min);     EXPECT_EQ(1, INT_FAST8_MIN,    (int_fast8_t)     0x80); break;
		CASE_TEST(limit_uint_fast8_max);    EXPECT_EQ(1, UINT_FAST8_MAX,   (uint_fast8_t)    0xff); break;
		CASE_TEST(limit_int_fast16_min);    EXPECT_EQ(1, INT_FAST16_MIN,   (int_fast16_t)    INTPTR_MIN); break;
		CASE_TEST(limit_int_fast16_max);    EXPECT_EQ(1, INT_FAST16_MAX,   (int_fast16_t)    INTPTR_MAX); break;
		CASE_TEST(limit_uint_fast16_max);   EXPECT_EQ(1, UINT_FAST16_MAX,  (uint_fast16_t)   UINTPTR_MAX); break;
		CASE_TEST(limit_int_fast32_min);    EXPECT_EQ(1, INT_FAST32_MIN,   (int_fast32_t)    INTPTR_MIN); break;
		CASE_TEST(limit_int_fast32_max);    EXPECT_EQ(1, INT_FAST32_MAX,   (int_fast32_t)    INTPTR_MAX); break;
		CASE_TEST(limit_uint_fast32_max);   EXPECT_EQ(1, UINT_FAST32_MAX,  (uint_fast32_t)   UINTPTR_MAX); break;
		CASE_TEST(limit_int_fast64_min);    EXPECT_EQ(1, INT_FAST64_MIN,   (int_fast64_t)    INT64_MIN); break;
		CASE_TEST(limit_int_fast64_max);    EXPECT_EQ(1, INT_FAST64_MAX,   (int_fast64_t)    INT64_MAX); break;
		CASE_TEST(limit_uint_fast64_max);   EXPECT_EQ(1, UINT_FAST64_MAX,  (uint_fast64_t)   UINT64_MAX); break;
#if __SIZEOF_LONG__ == 8
		CASE_TEST(limit_intptr_min);        EXPECT_EQ(1, INTPTR_MIN,       (intptr_t)        0x8000000000000000LL); break;
		CASE_TEST(limit_intptr_max);        EXPECT_EQ(1, INTPTR_MAX,       (intptr_t)        0x7fffffffffffffffLL); break;
		CASE_TEST(limit_uintptr_max);       EXPECT_EQ(1, UINTPTR_MAX,      (uintptr_t)       0xffffffffffffffffULL); break;
		CASE_TEST(limit_ptrdiff_min);       EXPECT_EQ(1, PTRDIFF_MIN,      (ptrdiff_t)       0x8000000000000000LL); break;
		CASE_TEST(limit_ptrdiff_max);       EXPECT_EQ(1, PTRDIFF_MAX,      (ptrdiff_t)       0x7fffffffffffffffLL); break;
		CASE_TEST(limit_size_max);          EXPECT_EQ(1, SIZE_MAX,         (size_t)          0xffffffffffffffffULL); break;
#elif __SIZEOF_LONG__ == 4
		CASE_TEST(limit_intptr_min);        EXPECT_EQ(1, INTPTR_MIN,       (intptr_t)        0x80000000); break;
		CASE_TEST(limit_intptr_max);        EXPECT_EQ(1, INTPTR_MAX,       (intptr_t)        0x7fffffff); break;
		CASE_TEST(limit_uintptr_max);       EXPECT_EQ(1, UINTPTR_MAX,      (uintptr_t)       0xffffffffU); break;
		CASE_TEST(limit_ptrdiff_min);       EXPECT_EQ(1, PTRDIFF_MIN,      (ptrdiff_t)       0x80000000); break;
		CASE_TEST(limit_ptrdiff_max);       EXPECT_EQ(1, PTRDIFF_MAX,      (ptrdiff_t)       0x7fffffff); break;
		CASE_TEST(limit_size_max);          EXPECT_EQ(1, SIZE_MAX,         (size_t)          0xffffffffU); break;
#else
# warning "__SIZEOF_LONG__ is undefined"
#endif /* __SIZEOF_LONG__ */
		case __LINE__:
			return ret; /* must be last */
		/* note: do not set any defaults so as to permit holes above */
		}
	}
	return ret;
}

#define EXPECT_VFPRINTF(c, expected, fmt, ...)				\
	ret += expect_vfprintf(llen, c, expected, fmt, ##__VA_ARGS__)

static int expect_vfprintf(int llen, size_t c, const char *expected, const char *fmt, ...)
{
	int ret, fd, w, r;
	char buf[100];
	FILE *memfile;
	va_list args;

	fd = memfd_create("vfprintf", 0);
	if (fd == -1) {
		pad_spc(llen, 64, "[FAIL]\n");
		return 1;
	}

	memfile = fdopen(fd, "w+");
	if (!memfile) {
		pad_spc(llen, 64, "[FAIL]\n");
		return 1;
	}

	va_start(args, fmt);
	w = vfprintf(memfile, fmt, args);
	va_end(args);

	if (w != c) {
		llen += printf(" written(%d) != %d", w, (int) c);
		pad_spc(llen, 64, "[FAIL]\n");
		return 1;
	}

	fflush(memfile);
	lseek(fd, 0, SEEK_SET);

	r = read(fd, buf, sizeof(buf) - 1);
	buf[r] = '\0';

	fclose(memfile);

	if (r != w) {
		llen += printf(" written(%d) != read(%d)", w, r);
		pad_spc(llen, 64, "[FAIL]\n");
		return 1;
	}

	llen += printf(" \"%s\" = \"%s\"", expected, buf);
	ret = strncmp(expected, buf, c);

	pad_spc(llen, 64, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}

static int run_vfprintf(int min, int max)
{
	int test;
	int tmp;
	int ret = 0;
	void *p1, *p2;

	for (test = min; test >= 0 && test <= max; test++) {
		int llen = 0; /* line length */

		/* avoid leaving empty lines below, this will insert holes into
		 * test numbers.
		 */
		switch (test + __LINE__ + 1) {
		CASE_TEST(empty);        EXPECT_VFPRINTF(0, "", ""); break;
		CASE_TEST(simple);       EXPECT_VFPRINTF(3, "foo", "foo"); break;
		CASE_TEST(string);       EXPECT_VFPRINTF(3, "foo", "%s", "foo"); break;
		CASE_TEST(number);       EXPECT_VFPRINTF(4, "1234", "%d", 1234); break;
		CASE_TEST(negnumber);    EXPECT_VFPRINTF(5, "-1234", "%d", -1234); break;
		CASE_TEST(unsigned);     EXPECT_VFPRINTF(5, "12345", "%u", 12345); break;
		CASE_TEST(char);         EXPECT_VFPRINTF(1, "c", "%c", 'c'); break;
		CASE_TEST(hex);          EXPECT_VFPRINTF(1, "f", "%x", 0xf); break;
		CASE_TEST(pointer);      EXPECT_VFPRINTF(3, "0x1", "%p", (void *) 0x1); break;
		case __LINE__:
			return ret; /* must be last */
		/* note: do not set any defaults so as to permit holes above */
		}
	}
	return ret;
}

static int smash_stack(void)
{
	char buf[100];
	volatile char *ptr = buf;
	size_t i;

	for (i = 0; i < 200; i++)
		ptr[i] = 'P';

	return 1;
}

static int run_protection(int min, int max)
{
	pid_t pid;
	int llen = 0, status;

	llen += printf("0 -fstackprotector ");

#if !defined(_NOLIBC_STACKPROTECTOR)
	llen += printf("not supported");
	pad_spc(llen, 64, "[SKIPPED]\n");
	return 0;
#endif

#if defined(_NOLIBC_STACKPROTECTOR)
	if (!__stack_chk_guard) {
		llen += printf("__stack_chk_guard not initialized");
		pad_spc(llen, 64, "[FAIL]\n");
		return 1;
	}
#endif

	pid = -1;
	pid = fork();

	switch (pid) {
	case -1:
		llen += printf("fork()");
		pad_spc(llen, 64, "[FAIL]\n");
		return 1;

	case 0:
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
		smash_stack();
		return 1;

	default:
		pid = waitpid(pid, &status, 0);

		if (pid == -1 || !WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
			llen += printf("waitpid()");
			pad_spc(llen, 64, "[FAIL]\n");
			return 1;
		}
		pad_spc(llen, 64, " [OK]\n");
		return 0;
	}
}

/* prepare what needs to be prepared for pid 1 (stdio, /dev, /proc, etc) */
int prepare(void)
{
	struct stat stat_buf;

	/* It's possible that /dev doesn't even exist or was not mounted, so
	 * we'll try to create it, mount it, or create minimal entries into it.
	 * We want at least /dev/null and /dev/console.
	 */
	if (stat("/dev/.", &stat_buf) == 0 || mkdir("/dev", 0755) == 0) {
		if (stat("/dev/console", &stat_buf) != 0 ||
		    stat("/dev/null", &stat_buf) != 0) {
			/* try devtmpfs first, otherwise fall back to manual creation */
			if (mount("/dev", "/dev", "devtmpfs", 0, 0) != 0) {
				mknod("/dev/console", 0600 | S_IFCHR, makedev(5, 1));
				mknod("/dev/null",    0666 | S_IFCHR, makedev(1, 3));
			}
		}
	}

	/* If no /dev/console was found before calling init, stdio is closed so
	 * we need to reopen it from /dev/console. If it failed above, it will
	 * still fail here and we cannot emit a message anyway.
	 */
	if (close(dup(1)) == -1) {
		int fd = open("/dev/console", O_RDWR);

		if (fd >= 0) {
			if (fd != 0)
				dup2(fd, 0);
			if (fd != 1)
				dup2(fd, 1);
			if (fd != 2)
				dup2(fd, 2);
			if (fd > 2)
				close(fd);
			puts("\nSuccessfully reopened /dev/console.");
		}
	}

	/* try to mount /proc if not mounted. Silently fail otherwise */
	if (stat("/proc/.", &stat_buf) == 0 || mkdir("/proc", 0755) == 0) {
		if (stat("/proc/self", &stat_buf) != 0)
			mount("/proc", "/proc", "proc", 0, 0);
	}

	return 0;
}

/* This is the definition of known test names, with their functions */
static const struct test test_names[] = {
	/* add new tests here */
	{ .name = "syscall",    .func = run_syscall    },
	{ .name = "stdlib",     .func = run_stdlib     },
	{ .name = "vfprintf",   .func = run_vfprintf   },
	{ .name = "protection", .func = run_protection },
	{ 0 }
};

int main(int argc, char **argv, char **envp)
{
	int min = 0;
	int max = INT_MAX;
	int ret = 0;
	int err;
	int idx;
	char *test;

	environ = envp;

	/* when called as init, it's possible that no console was opened, for
	 * example if no /dev file system was provided. We'll check that fd#1
	 * was opened, and if not we'll attempt to create and open /dev/console
	 * and /dev/null that we'll use for later tests.
	 */
	if (getpid() == 1)
		prepare();

	/* the definition of a series of tests comes from either argv[1] or the
	 * "NOLIBC_TEST" environment variable. It's made of a comma-delimited
	 * series of test names and optional ranges:
	 *    syscall:5-15[:.*],stdlib:8-10
	 */
	test = argv[1];
	if (!test)
		test = getenv("NOLIBC_TEST");

	if (test) {
		char *comma, *colon, *dash, *value;

		do {
			comma = strchr(test, ',');
			if (comma)
				*(comma++) = '\0';

			colon = strchr(test, ':');
			if (colon)
				*(colon++) = '\0';

			for (idx = 0; test_names[idx].name; idx++) {
				if (strcmp(test, test_names[idx].name) == 0)
					break;
			}

			if (test_names[idx].name) {
				/* The test was named, it will be called at least
				 * once. We may have an optional range at <colon>
				 * here, which defaults to the full range.
				 */
				do {
					min = 0; max = INT_MAX;
					value = colon;
					if (value && *value) {
						colon = strchr(value, ':');
						if (colon)
							*(colon++) = '\0';

						dash = strchr(value, '-');
						if (dash)
							*(dash++) = '\0';

						/* support :val: :min-max: :min-: :-max: */
						if (*value)
							min = atoi(value);
						if (!dash)
							max = min;
						else if (*dash)
							max = atoi(dash);

						value = colon;
					}

					/* now's time to call the test */
					printf("Running test '%s'\n", test_names[idx].name);
					err = test_names[idx].func(min, max);
					ret += err;
					printf("Errors during this test: %d\n\n", err);
				} while (colon && *colon);
			} else
				printf("Ignoring unknown test name '%s'\n", test);

			test = comma;
		} while (test && *test);
	} else {
		/* no test mentioned, run everything */
		for (idx = 0; test_names[idx].name; idx++) {
			printf("Running test '%s'\n", test_names[idx].name);
			err = test_names[idx].func(min, max);
			ret += err;
			printf("Errors during this test: %d\n\n", err);
		}
	}

	printf("Total number of errors: %d\n", ret);

	if (getpid() == 1) {
		/* we're running as init, there's no other process on the
		 * system, thus likely started from a VM for a quick check.
		 * Exiting will provoke a kernel panic that may be reported
		 * as an error by Qemu or the hypervisor, while stopping
		 * cleanly will often be reported as a success. This allows
		 * to use the output of this program for bisecting kernels.
		 */
		printf("Leaving init with final status: %d\n", !!ret);
		if (ret == 0)
			reboot(LINUX_REBOOT_CMD_POWER_OFF);
#if defined(__x86_64__)
		/* QEMU started with "-device isa-debug-exit -no-reboot" will
		 * exit with status code 2N+1 when N is written to 0x501. We
		 * hard-code the syscall here as it's arch-dependent.
		 */
#if defined(_NOLIBC_SYS_H)
		else if (my_syscall3(__NR_ioperm, 0x501, 1, 1) == 0)
#else
		else if (ioperm(0x501, 1, 1) == 0)
#endif
			__asm__ volatile ("outb %%al, %%dx" :: "d"(0x501), "a"(0));
		/* if it does nothing, fall back to the regular panic */
#endif
	}

	printf("Exiting with status %d\n", !!ret);
	return !!ret;
}
