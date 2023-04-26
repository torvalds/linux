/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit API providing hooks for non-test code to interact with tests.
 *
 * Copyright (C) 2020, Google LLC.
 * Author: Uriel Guajardo <urielguajardo@google.com>
 */

#ifndef _KUNIT_TEST_BUG_H
#define _KUNIT_TEST_BUG_H

#if IS_ENABLED(CONFIG_KUNIT)

#include <linux/jump_label.h> /* For static branch */
#include <linux/sched.h>

/* Static key if KUnit is running any tests. */
DECLARE_STATIC_KEY_FALSE(kunit_running);

/* Hooks table: a table of function pointers filled in when kunit loads */
extern struct kunit_hooks_table {
	__printf(3, 4) void (*fail_current_test)(const char*, int, const char*, ...);
	void *(*get_static_stub_address)(struct kunit *test, void *real_fn_addr);
} kunit_hooks;

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
 */
#define kunit_fail_current_test(fmt, ...) do {					\
		if (static_branch_unlikely(&kunit_running)) {			\
			/* Guaranteed to be non-NULL when kunit_running true*/	\
			kunit_hooks.fail_current_test(__FILE__, __LINE__,	\
						  fmt, ##__VA_ARGS__);		\
		}								\
	} while (0)

#else

static inline struct kunit *kunit_get_current_test(void) { return NULL; }

#define kunit_fail_current_test(fmt, ...) do {} while (0)

#endif

#endif /* _KUNIT_TEST_BUG_H */
