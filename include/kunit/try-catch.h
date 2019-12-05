/* SPDX-License-Identifier: GPL-2.0 */
/*
 * An API to allow a function, that may fail, to be executed, and recover in a
 * controlled manner.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#ifndef _KUNIT_TRY_CATCH_H
#define _KUNIT_TRY_CATCH_H

#include <linux/types.h>

typedef void (*kunit_try_catch_func_t)(void *);

struct completion;
struct kunit;

/**
 * struct kunit_try_catch - provides a generic way to run code which might fail.
 * @test: The test case that is currently being executed.
 * @try_completion: Completion that the control thread waits on while test runs.
 * @try_result: Contains any errno obtained while running test case.
 * @try: The function, the test case, to attempt to run.
 * @catch: The function called if @try bails out.
 * @context: used to pass user data to the try and catch functions.
 *
 * kunit_try_catch provides a generic, architecture independent way to execute
 * an arbitrary function of type kunit_try_catch_func_t which may bail out by
 * calling kunit_try_catch_throw(). If kunit_try_catch_throw() is called, @try
 * is stopped at the site of invocation and @catch is called.
 *
 * struct kunit_try_catch provides a generic interface for the functionality
 * needed to implement kunit->abort() which in turn is needed for implementing
 * assertions. Assertions allow stating a precondition for a test simplifying
 * how test cases are written and presented.
 *
 * Assertions are like expectations, except they abort (call
 * kunit_try_catch_throw()) when the specified condition is not met. This is
 * useful when you look at a test case as a logical statement about some piece
 * of code, where assertions are the premises for the test case, and the
 * conclusion is a set of predicates, rather expectations, that must all be
 * true. If your premises are violated, it does not makes sense to continue.
 */
struct kunit_try_catch {
	/* private: internal use only. */
	struct kunit *test;
	struct completion *try_completion;
	int try_result;
	kunit_try_catch_func_t try;
	kunit_try_catch_func_t catch;
	void *context;
};

void kunit_try_catch_init(struct kunit_try_catch *try_catch,
			  struct kunit *test,
			  kunit_try_catch_func_t try,
			  kunit_try_catch_func_t catch);

void kunit_try_catch_run(struct kunit_try_catch *try_catch, void *context);

void __noreturn kunit_try_catch_throw(struct kunit_try_catch *try_catch);

static inline int kunit_try_catch_get_result(struct kunit_try_catch *try_catch)
{
	return try_catch->try_result;
}

/*
 * Exposed for testing only.
 */
void kunit_generic_try_catch_init(struct kunit_try_catch *try_catch);

#endif /* _KUNIT_TRY_CATCH_H */
