#ifndef _LKL_TEST_H
#define _LKL_TEST_H

#define TEST_SUCCESS	0
#define TEST_FAILURE	1
#define TEST_SKIP	2
#define TEST_TODO	3
#define TEST_BAILOUT	4

struct lkl_test {
	const char *name;
	int (*fn)();
	void *arg1, *arg2, *arg3;
};

/**
 * Simple wrapper to initialize a test entry.
 * @name - test name, it assume test function is named test_@name
 * @vargs - arguments to be passed to the function
 */
#define LKL_TEST(name, ...) { #name, lkl_test_##name, __VA_ARGS__ }

/**
 * lkl_test_run - run a test suite
 *
 * @tests - the list of tests to run
 * @nr - number of tests
 * @fmt - format string to be used for suite name
 */
int lkl_test_run(const struct lkl_test *tests, int nr, const char *fmt, ...);

/**
 * lkl_test_log - store a string in the test log buffer
 * @str - the string to log (can be non-NULL terminated)
 * @len - the string length
 */
void lkl_test_log(const char *str, int len);

/**
 * lkl_test_logf - printf like function to store into the test log buffer
 * @fmt - printf format string
 * @vargs - arguments to the format string
 */
int lkl_test_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * LKL_TEST_CALL - create a test function as for a LKL call
 *
 * The test function will be named lkl_test_@name and will return
 * TEST_SUCCESS if the called functions returns @expect. Otherwise
 * will return TEST_FAILUIRE.
 *
 * @name - test name; must be unique because it is part of the the
 * test function; the test function will be named
 * @call - function to call
 * @expect - expected return value for success
 * @args - arguments to pass to the LKL call
 */
#define LKL_TEST_CALL(name, call, expect, ...)				\
	static int lkl_test_##name(void)				\
	{								\
		long ret;						\
									\
		ret = call(__VA_ARGS__);				\
		lkl_test_logf("%s(%s) = %ld %s\n", #call, #__VA_ARGS__, \
			ret, ret < 0 ? lkl_strerror(ret) : "");		\
		return (ret == expect) ? TEST_SUCCESS : TEST_FAILURE;	\
	}

/**
 * lkl_test_get_log - return a copy of the log
 *
 * The caller is resposible for freeing the returned buffer with free().
 */
char *lkl_test_get_log(void);


#endif /* _LKL_TEST_H */
