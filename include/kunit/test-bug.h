/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit API allowing dynamic analysis tools to interact with KUnit tests
 *
 * Copyright (C) 2020, Google LLC.
 * Author: Uriel Guajardo <urielguajardo@google.com>
 */

#ifndef _KUNIT_TEST_BUG_H
#define _KUNIT_TEST_BUG_H

#if IS_BUILTIN(CONFIG_KUNIT)

#include <linux/jump_label.h> /* For static branch */
#include <linux/sched.h>

/* Static key if KUnit is running any tests. */
DECLARE_STATIC_KEY_FALSE(kunit_running);

/**
 * kunit_get_current_test() - Return a pointer to the currently running
 *			      KUnit test.
 *
 * If a KUnit test is running in the current task, returns a pointer to its
 * associated struct kunit. This pointer can then be passed to any KUnit
 * function or assertion. If no test is running (or a test is running in a
 * different task), returns NULL.
 *
 * This function is safe to call even when KUnit is disabled. If CONFIG_KUNIT
 * is not enabled, it will compile down to nothing and will return quickly no
 * test is running.
 */
static inline struct kunit *kunit_get_current_test(void)
{
	if (!static_branch_unlikely(&kunit_running))
		return NULL;

	return current->kunit_test;
}


/**
 * kunit_fail_current_test() - If a KUnit test is running, fail it.
 *
 * If a KUnit test is running in the current task, mark that test as failed.
 *
 * This macro will only work if KUnit is built-in (though the tests
 * themselves can be modules). Otherwise, it compiles down to nothing.
 */
#define kunit_fail_current_test(fmt, ...) do {					\
		if (static_branch_unlikely(&kunit_running)) {			\
			__kunit_fail_current_test(__FILE__, __LINE__,		\
						  fmt, ##__VA_ARGS__);		\
		}								\
	} while (0)


extern __printf(3, 4) void __kunit_fail_current_test(const char *file, int line,
						    const char *fmt, ...);

#else

static inline struct kunit *kunit_get_current_test(void) { return NULL; }

/* We define this with an empty helper function so format string warnings work */
#define kunit_fail_current_test(fmt, ...) \
		__kunit_fail_current_test(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

static inline __printf(3, 4) void __kunit_fail_current_test(const char *file, int line,
							    const char *fmt, ...)
{
}

#endif

#endif /* _KUNIT_TEST_BUG_H */
