// SPDX-License-Identifier: GPL-2.0
/*
 * An API to allow a function, that may fail, to be executed, and recover in a
 * controlled manner.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>
#include <kunit/try-catch.h>
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/sysctl.h>

void __noreturn kunit_try_catch_throw(struct kunit_try_catch *try_catch)
{
	try_catch->try_result = -EFAULT;
	complete_and_exit(try_catch->try_completion, -EFAULT);
}

static int kunit_generic_run_threadfn_adapter(void *data)
{
	struct kunit_try_catch *try_catch = data;

	try_catch->try(try_catch->context);

	complete_and_exit(try_catch->try_completion, 0);
}

static unsigned long kunit_test_timeout(void)
{
	unsigned long timeout_msecs;

	/*
	 * TODO(brendanhiggins@google.com): We should probably have some type of
	 * variable timeout here. The only question is what that timeout value
	 * should be.
	 *
	 * The intention has always been, at some point, to be able to label
	 * tests with some type of size bucket (unit/small, integration/medium,
	 * large/system/end-to-end, etc), where each size bucket would get a
	 * default timeout value kind of like what Bazel does:
	 * https://docs.bazel.build/versions/master/be/common-definitions.html#test.size
	 * There is still some debate to be had on exactly how we do this. (For
	 * one, we probably want to have some sort of test runner level
	 * timeout.)
	 *
	 * For more background on this topic, see:
	 * https://mike-bland.com/2011/11/01/small-medium-large.html
	 */
	if (sysctl_hung_task_timeout_secs) {
		/*
		 * If sysctl_hung_task is active, just set the timeout to some
		 * value less than that.
		 *
		 * In regards to the above TODO, if we decide on variable
		 * timeouts, this logic will likely need to change.
		 */
		timeout_msecs = (sysctl_hung_task_timeout_secs - 1) *
				MSEC_PER_SEC;
	} else {
		timeout_msecs = 300 * MSEC_PER_SEC; /* 5 min */
	}

	return timeout_msecs;
}

void kunit_try_catch_run(struct kunit_try_catch *try_catch, void *context)
{
	DECLARE_COMPLETION_ONSTACK(try_completion);
	struct kunit *test = try_catch->test;
	struct task_struct *task_struct;
	int exit_code, time_remaining;

	try_catch->context = context;
	try_catch->try_completion = &try_completion;
	try_catch->try_result = 0;
	task_struct = kthread_run(kunit_generic_run_threadfn_adapter,
				  try_catch,
				  "kunit_try_catch_thread");
	if (IS_ERR(task_struct)) {
		try_catch->catch(try_catch->context);
		return;
	}

	time_remaining = wait_for_completion_timeout(&try_completion,
						     kunit_test_timeout());
	if (time_remaining == 0) {
		kunit_err(test, "try timed out\n");
		try_catch->try_result = -ETIMEDOUT;
	}

	exit_code = try_catch->try_result;

	if (!exit_code)
		return;

	if (exit_code == -EFAULT)
		try_catch->try_result = 0;
	else if (exit_code == -EINTR)
		kunit_err(test, "wake_up_process() was never called\n");
	else if (exit_code)
		kunit_err(test, "Unknown error: %d\n", exit_code);

	try_catch->catch(try_catch->context);
}

void kunit_try_catch_init(struct kunit_try_catch *try_catch,
			  struct kunit *test,
			  kunit_try_catch_func_t try,
			  kunit_try_catch_func_t catch)
{
	try_catch->test = test;
	try_catch->try = try;
	try_catch->catch = catch;
}
