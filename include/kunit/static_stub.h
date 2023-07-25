/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit function redirection (static stubbing) API.
 *
 * Copyright (C) 2022, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */
#ifndef _KUNIT_STATIC_STUB_H
#define _KUNIT_STATIC_STUB_H

#if !IS_ENABLED(CONFIG_KUNIT)

/* If CONFIG_KUNIT is not enabled, these stubs quietly disappear. */
#define KUNIT_STATIC_STUB_REDIRECT(real_fn_name, args...) do {} while (0)

#else

#include <kunit/test.h>
#include <kunit/test-bug.h>

#include <linux/compiler.h> /* for {un,}likely() */
#include <linux/sched.h> /* for task_struct */


/**
 * KUNIT_STATIC_STUB_REDIRECT() - call a replacement 'static stub' if one exists
 * @real_fn_name: The name of this function (as an identifier, not a string)
 * @args: All of the arguments passed to this function
 *
 * This is a function prologue which is used to allow calls to the current
 * function to be redirected by a KUnit test. KUnit tests can call
 * kunit_activate_static_stub() to pass a replacement function in. The
 * replacement function will be called by KUNIT_STATIC_STUB_REDIRECT(), which
 * will then return from the function. If the caller is not in a KUnit context,
 * the function will continue execution as normal.
 *
 * Example:
 *
 * .. code-block:: c
 *
 *	int real_func(int n)
 *	{
 *		KUNIT_STATIC_STUB_REDIRECT(real_func, n);
 *		return 0;
 *	}
 *
 *	int replacement_func(int n)
 *	{
 *		return 42;
 *	}
 *
 *	void example_test(struct kunit *test)
 *	{
 *		kunit_activate_static_stub(test, real_func, replacement_func);
 *		KUNIT_EXPECT_EQ(test, real_func(1), 42);
 *	}
 *
 */
#define KUNIT_STATIC_STUB_REDIRECT(real_fn_name, args...)		\
do {									\
	typeof(&real_fn_name) replacement;				\
	struct kunit *current_test = kunit_get_current_test();		\
									\
	if (likely(!current_test))					\
		break;							\
									\
	replacement = kunit_hooks.get_static_stub_address(current_test,	\
							&real_fn_name);	\
									\
	if (unlikely(replacement))					\
		return replacement(args);				\
} while (0)

/* Helper function for kunit_activate_static_stub(). The macro does
 * typechecking, so use it instead.
 */
void __kunit_activate_static_stub(struct kunit *test,
				  void *real_fn_addr,
				  void *replacement_addr);

/**
 * kunit_activate_static_stub() - replace a function using static stubs.
 * @test: A pointer to the 'struct kunit' test context for the current test.
 * @real_fn_addr: The address of the function to replace.
 * @replacement_addr: The address of the function to replace it with.
 *
 * When activated, calls to real_fn_addr from within this test (even if called
 * indirectly) will instead call replacement_addr. The function pointed to by
 * real_fn_addr must begin with the static stub prologue in
 * KUNIT_STATIC_STUB_REDIRECT() for this to work. real_fn_addr and
 * replacement_addr must have the same type.
 *
 * The redirection can be disabled again with kunit_deactivate_static_stub().
 */
#define kunit_activate_static_stub(test, real_fn_addr, replacement_addr) do {	\
	typecheck_fn(typeof(&real_fn_addr), replacement_addr);			\
	__kunit_activate_static_stub(test, real_fn_addr, replacement_addr);	\
} while (0)


/**
 * kunit_deactivate_static_stub() - disable a function redirection
 * @test: A pointer to the 'struct kunit' test context for the current test.
 * @real_fn_addr: The address of the function to no-longer redirect
 *
 * Deactivates a redirection configured with kunit_activate_static_stub(). After
 * this function returns, calls to real_fn_addr() will execute the original
 * real_fn, not any previously-configured replacement.
 */
void kunit_deactivate_static_stub(struct kunit *test, void *real_fn_addr);

#endif
#endif
