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


/* This is the definition of known test names, with their functions */
static struct test test_names[] = {
	/* add new tests here */
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
	printf("Exiting with status %d\n", !!ret);
	return !!ret;
}
