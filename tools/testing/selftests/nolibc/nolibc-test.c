// SPDX-License-Identifier: GPL-2.0

/* platform-specific include files coming from the compiler */
#include <limits.h>

/* libc-specific include files
 * The program may be built in 2 ways:
 *   $(CC) -nostdlib -include /path/to/nolibc.h => NOLIBC already defined
 *   $(CC) -nostdlib -I/path/to/nolibc/sysroot
 */
#ifndef NOLIBC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

/* will be used by nolibc by getenv() */
char **environ;

/* definition of a series of tests */
struct test {
	const char *name;              // test name
	int (*func)(int min, int max); // handler
};

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
	default:
		return itoa(err);
	}
}

static int pad_spc(int llen, int cnt, const char *fmt, ...)
{
	va_list args;
	int len;
	int ret;

	for (len = 0; len < cnt - llen; len++)
		putchar(' ');

	va_start(args, fmt);
	ret = vfprintf(stdout, fmt, args);
	va_end(args);
	return ret < 0 ? ret : ret + len;
}

/* The tests below are intended to be used by the macroes, which evaluate
 * expression <expr>, print the status to stdout, and update the "ret"
 * variable to count failures. The functions themselves return the number
 * of failures, thus either 0 or 1.
 */

#define EXPECT_ZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_zr(expr, llen); } while (0)

static int expect_zr(int expr, int llen)
{
	int ret = !(expr == 0);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_NZ(cond, expr, val)			\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_nz(expr, llen; } while (0)

static int expect_nz(int expr, int llen)
{
	int ret = !(expr != 0);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_EQ(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_eq(expr, llen, val); } while (0)

static int expect_eq(int expr, int llen, int val)
{
	int ret = !(expr == val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_NE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_ne(expr, llen, val); } while (0)

static int expect_ne(int expr, int llen, int val)
{
	int ret = !(expr != val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_GE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_ge(expr, llen, val); } while (0)

static int expect_ge(int expr, int llen, int val)
{
	int ret = !(expr >= val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_GT(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_gt(expr, llen, val); } while (0)

static int expect_gt(int expr, int llen, int val)
{
	int ret = !(expr > val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_LE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_le(expr, llen, val); } while (0)

static int expect_le(int expr, int llen, int val)
{
	int ret = !(expr <= val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_LT(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_lt(expr, llen, val); } while (0)

static int expect_lt(int expr, int llen, int val)
{
	int ret = !(expr < val);

	llen += printf(" = %d ", expr);
	pad_spc(llen, 40, ret ? "[FAIL]\n" : " [OK]\n");
	return ret;
}


#define EXPECT_SYSZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_syszr(expr, llen); } while (0)

static int expect_syszr(int expr, int llen)
{
	int ret = 0;

	if (expr) {
		ret = 1;
		llen += printf(" = %d %s ", expr, errorname(errno));
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += printf(" = %d ", expr);
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_SYSEQ(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_syseq(expr, llen, val); } while (0)

static int expect_syseq(int expr, int llen, int val)
{
	int ret = 0;

	if (expr != val) {
		ret = 1;
		llen += printf(" = %d %s ", expr, errorname(errno));
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += printf(" = %d ", expr);
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_SYSNE(cond, expr, val)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_sysne(expr, llen, val); } while (0)

static int expect_sysne(int expr, int llen, int val)
{
	int ret = 0;

	if (expr == val) {
		ret = 1;
		llen += printf(" = %d %s ", expr, errorname(errno));
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += printf(" = %d ", expr);
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_SYSER(cond, expr, expret, experr)			\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_syserr(expr, expret, experr, llen); } while (0)

static int expect_syserr(int expr, int expret, int experr, int llen)
{
	int ret = 0;
	int _errno = errno;

	llen += printf(" = %d %s ", expr, errorname(_errno));
	if (expr != expret || _errno != experr) {
		ret = 1;
		llen += printf(" != (%d %s) ", expret, errorname(experr));
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_PTRZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_ptrzr(expr, llen); } while (0)

static int expect_ptrzr(const void *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%p> ", expr);
	if (expr) {
		ret = 1;
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_PTRNZ(cond, expr)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_ptrnz(expr, llen); } while (0)

static int expect_ptrnz(const void *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%p> ", expr);
	if (!expr) {
		ret = 1;
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STRZR(cond, expr)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_strzr(expr, llen); } while (0)

static int expect_strzr(const char *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (expr) {
		ret = 1;
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STRNZ(cond, expr)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_strnz(expr, llen); } while (0)

static int expect_strnz(const char *expr, int llen)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (!expr) {
		ret = 1;
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STREQ(cond, expr, cmp)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_streq(expr, llen, cmp); } while (0)

static int expect_streq(const char *expr, int llen, const char *cmp)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (strcmp(expr, cmp) != 0) {
		ret = 1;
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
	}
	return ret;
}


#define EXPECT_STRNE(cond, expr, cmp)				\
	do { if (!cond) pad_spc(llen, 40, "[SKIPPED]\n"); else ret += expect_strne(expr, llen, cmp); } while (0)

static int expect_strne(const char *expr, int llen, const char *cmp)
{
	int ret = 0;

	llen += printf(" = <%s> ", expr);
	if (strcmp(expr, cmp) == 0) {
		ret = 1;
		llen += pad_spc(llen, 40, "[FAIL]\n");
	} else {
		llen += pad_spc(llen, 40, " [OK]\n");
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

/* Run syscall tests between IDs <min> and <max>.
 * Return 0 on success, non-zero on failure.
 */
int run_syscall(int min, int max)
{
	struct stat stat_buf;
	int test;
	int tmp;
	int ret = 0;
	void *p1, *p2;

	for (test = min; test >= 0 && test <= max; test++) {
		int llen = 0; // line length

		/* avoid leaving empty lines below, this will insert holes into
		 * test numbers.
		 */
		switch (test + __LINE__ + 1) {
		CASE_TEST(getpid);            EXPECT_SYSNE(1, getpid(), -1); break;
		CASE_TEST(getppid);           EXPECT_SYSNE(1, getppid(), -1); break;
		CASE_TEST(gettid);            EXPECT_SYSNE(1, gettid(), -1); break;
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
		CASE_TEST(chmod_net);         EXPECT_SYSZR(1, chmod("/proc/self/net", 0555)); break;
		CASE_TEST(chmod_self);        EXPECT_SYSER(1, chmod("/proc/self", 0555), -1, EPERM); break;
		CASE_TEST(chown_self);        EXPECT_SYSER(1, chown("/proc/self", 0, 0), -1, EPERM); break;
		CASE_TEST(chroot_root);       EXPECT_SYSZR(1, chroot("/")); break;
		CASE_TEST(chroot_blah);       EXPECT_SYSER(1, chroot("/proc/self/blah"), -1, ENOENT); break;
		CASE_TEST(chroot_exe);        EXPECT_SYSER(1, chroot("/proc/self/exe"), -1, ENOTDIR); break;
		CASE_TEST(close_m1);          EXPECT_SYSER(1, close(-1), -1, EBADF); break;
		CASE_TEST(close_dup);         EXPECT_SYSZR(1, close(dup(0))); break;
		CASE_TEST(dup_0);             tmp = dup(0);  EXPECT_SYSNE(1, tmp, -1); close(tmp); break;
		CASE_TEST(dup_m1);            tmp = dup(-1); EXPECT_SYSER(1, tmp, -1, EBADF); if (tmp != -1) close(tmp); break;
		CASE_TEST(dup2_0);            tmp = dup2(0, 100);  EXPECT_SYSNE(1, tmp, -1); close(tmp); break;
		CASE_TEST(dup2_m1);           tmp = dup2(-1, 100); EXPECT_SYSER(1, tmp, -1, EBADF); if (tmp != -1) close(tmp); break;
		CASE_TEST(dup3_0);            tmp = dup3(0, 100, 0);  EXPECT_SYSNE(1, tmp, -1); close(tmp); break;
		CASE_TEST(dup3_m1);           tmp = dup3(-1, 100, 0); EXPECT_SYSER(1, tmp, -1, EBADF); if (tmp != -1) close(tmp); break;
		CASE_TEST(execve_root);       EXPECT_SYSER(1, execve("/", (char*[]){ [0] = "/", [1] = NULL }, NULL), -1, EACCES); break;
		CASE_TEST(getdents64_root);   EXPECT_SYSNE(1, test_getdents64("/"), -1); break;
		CASE_TEST(getdents64_null);   EXPECT_SYSER(1, test_getdents64("/dev/null"), -1, ENOTDIR); break;
		CASE_TEST(gettimeofday_null); EXPECT_SYSZR(1, gettimeofday(NULL, NULL)); break;
		CASE_TEST(gettimeofday_bad1); EXPECT_SYSER(1, gettimeofday((void *)1, NULL), -1, EFAULT); break;
		CASE_TEST(gettimeofday_bad2); EXPECT_SYSER(1, gettimeofday(NULL, (void *)1), -1, EFAULT); break;
		CASE_TEST(gettimeofday_bad2); EXPECT_SYSER(1, gettimeofday(NULL, (void *)1), -1, EFAULT); break;
		CASE_TEST(ioctl_tiocinq);     EXPECT_SYSZR(1, ioctl(0, TIOCINQ, &tmp)); break;
		CASE_TEST(ioctl_tiocinq);     EXPECT_SYSZR(1, ioctl(0, TIOCINQ, &tmp)); break;
		CASE_TEST(link_root1);        EXPECT_SYSER(1, link("/", "/"), -1, EEXIST); break;
		CASE_TEST(link_blah);         EXPECT_SYSER(1, link("/proc/self/blah", "/blah"), -1, ENOENT); break;
		CASE_TEST(link_dir);          EXPECT_SYSER(1, link("/", "/blah"), -1, EPERM); break;
		CASE_TEST(link_cross);        EXPECT_SYSER(1, link("/proc/self/net", "/blah"), -1, EXDEV); break;
		CASE_TEST(lseek_m1);          EXPECT_SYSER(1, lseek(-1, 0, SEEK_SET), -1, EBADF); break;
		CASE_TEST(lseek_0);           EXPECT_SYSER(1, lseek(0, 0, SEEK_SET), -1, ESPIPE); break;
		CASE_TEST(mkdir_root);        EXPECT_SYSER(1, mkdir("/", 0755), -1, EEXIST); break;
		CASE_TEST(open_tty);          EXPECT_SYSNE(1, tmp = open("/dev/null", 0), -1); if (tmp != -1) close(tmp); break;
		CASE_TEST(open_blah);         EXPECT_SYSER(1, tmp = open("/proc/self/blah", 0), -1, ENOENT); if (tmp != -1) close(tmp); break;
		CASE_TEST(poll_null);         EXPECT_SYSZR(1, poll(NULL, 0, 0)); break;
		CASE_TEST(poll_stdout);       EXPECT_SYSNE(1, ({ struct pollfd fds = { 1, POLLOUT, 0}; poll(&fds, 1, 0); }), -1); break;
		CASE_TEST(poll_fault);        EXPECT_SYSER(1, poll((void *)1, 1, 0), -1, EFAULT); break;
		CASE_TEST(read_badf);         EXPECT_SYSER(1, read(-1, &tmp, 1), -1, EBADF); break;
		CASE_TEST(sched_yield);       EXPECT_SYSZR(1, sched_yield()); break;
		CASE_TEST(select_null);       EXPECT_SYSZR(1, ({ struct timeval tv = { 0 }; select(0, NULL, NULL, NULL, &tv); })); break;
		CASE_TEST(select_stdout);     EXPECT_SYSNE(1, ({ fd_set fds; FD_ZERO(&fds); FD_SET(1, &fds); select(2, NULL, &fds, NULL, NULL); }), -1); break;
		CASE_TEST(select_fault);      EXPECT_SYSER(1, select(1, (void *)1, NULL, NULL, 0), -1, EFAULT); break;
		CASE_TEST(stat_blah);         EXPECT_SYSER(1, stat("/proc/self/blah", &stat_buf), -1, ENOENT); break;
		CASE_TEST(stat_fault);        EXPECT_SYSER(1, stat(NULL, &stat_buf), -1, EFAULT); break;
		CASE_TEST(symlink_root);      EXPECT_SYSER(1, symlink("/", "/"), -1, EEXIST); break;
		CASE_TEST(unlink_root);       EXPECT_SYSER(1, unlink("/"), -1, EISDIR); break;
		CASE_TEST(unlink_blah);       EXPECT_SYSER(1, unlink("/proc/self/blah"), -1, ENOENT); break;
		CASE_TEST(wait_child);        EXPECT_SYSER(1, wait(&tmp), -1, ECHILD); break;
		CASE_TEST(waitpid_min);       EXPECT_SYSER(1, waitpid(INT_MIN, &tmp, WNOHANG), -1, ESRCH); break;
		CASE_TEST(waitpid_child);     EXPECT_SYSER(1, waitpid(getpid(), &tmp, WNOHANG), -1, ECHILD); break;
		CASE_TEST(write_badf);        EXPECT_SYSER(1, write(-1, &tmp, 1), -1, EBADF); break;
		CASE_TEST(write_zero);        EXPECT_SYSZR(1, write(1, &tmp, 0)); break;
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
		int llen = 0; // line length

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
		case __LINE__:
			return ret; /* must be last */
		/* note: do not set any defaults so as to permit holes above */
		}
	}
	return ret;
}


/* This is the definition of known test names, with their functions */
static struct test test_names[] = {
	/* add new tests here */
	{ .name = "syscall",   .func = run_syscall  },
	{ .name = "stdlib",    .func = run_stdlib   },
	{ 0 }
};

int main(int argc, char **argv, char **envp)
{
	int min = 0;
	int max = __INT_MAX__;
	int ret = 0;
	int err;
	int idx;
	char *test;

	environ = envp;

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
					min = 0; max = __INT_MAX__;
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
	}

	printf("Exiting with status %d\n", !!ret);
	return !!ret;
}
