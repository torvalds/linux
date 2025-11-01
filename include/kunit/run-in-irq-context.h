/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper function for testing code in interrupt contexts
 *
 * Copyright 2025 Google LLC
 */
#ifndef _KUNIT_RUN_IN_IRQ_CONTEXT_H
#define _KUNIT_RUN_IN_IRQ_CONTEXT_H

#include <kunit/test.h>
#include <linux/timekeeping.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>

#define KUNIT_IRQ_TEST_HRTIMER_INTERVAL us_to_ktime(5)

struct kunit_irq_test_state {
	bool (*func)(void *test_specific_state);
	void *test_specific_state;
	bool task_func_reported_failure;
	bool hardirq_func_reported_failure;
	bool softirq_func_reported_failure;
	unsigned long hardirq_func_calls;
	unsigned long softirq_func_calls;
	struct hrtimer timer;
	struct work_struct bh_work;
};

static enum hrtimer_restart kunit_irq_test_timer_func(struct hrtimer *timer)
{
	struct kunit_irq_test_state *state =
		container_of(timer, typeof(*state), timer);

	WARN_ON_ONCE(!in_hardirq());
	state->hardirq_func_calls++;

	if (!state->func(state->test_specific_state))
		state->hardirq_func_reported_failure = true;

	hrtimer_forward_now(&state->timer, KUNIT_IRQ_TEST_HRTIMER_INTERVAL);
	queue_work(system_bh_wq, &state->bh_work);
	return HRTIMER_RESTART;
}

static void kunit_irq_test_bh_work_func(struct work_struct *work)
{
	struct kunit_irq_test_state *state =
		container_of(work, typeof(*state), bh_work);

	WARN_ON_ONCE(!in_serving_softirq());
	state->softirq_func_calls++;

	if (!state->func(state->test_specific_state))
		state->softirq_func_reported_failure = true;
}

/*
 * Helper function which repeatedly runs the given @func in task, softirq, and
 * hardirq context concurrently, and reports a failure to KUnit if any
 * invocation of @func in any context returns false.  @func is passed
 * @test_specific_state as its argument.  At most 3 invocations of @func will
 * run concurrently: one in each of task, softirq, and hardirq context.
 *
 * The main purpose of this interrupt context testing is to validate fallback
 * code paths that run in contexts where the normal code path cannot be used,
 * typically due to the FPU or vector registers already being in-use in kernel
 * mode.  These code paths aren't covered when the test code is executed only by
 * the KUnit test runner thread in task context.  The reason for the concurrency
 * is because merely using hardirq context is not sufficient to reach a fallback
 * code path on some architectures; the hardirq actually has to occur while the
 * FPU or vector unit was already in-use in kernel mode.
 *
 * Another purpose of this testing is to detect issues with the architecture's
 * irq_fpu_usable() and kernel_fpu_begin/end() or equivalent functions,
 * especially in softirq context when the softirq may have interrupted a task
 * already using kernel-mode FPU or vector (if the arch didn't prevent that).
 * Crypto functions are often executed in softirqs, so this is important.
 */
static inline void kunit_run_irq_test(struct kunit *test, bool (*func)(void *),
				      int max_iterations,
				      void *test_specific_state)
{
	struct kunit_irq_test_state state = {
		.func = func,
		.test_specific_state = test_specific_state,
	};
	unsigned long end_jiffies;

	/*
	 * Set up a hrtimer (the way we access hardirq context) and a work
	 * struct for the BH workqueue (the way we access softirq context).
	 */
	hrtimer_setup_on_stack(&state.timer, kunit_irq_test_timer_func,
			       CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	INIT_WORK_ONSTACK(&state.bh_work, kunit_irq_test_bh_work_func);

	/* Run for up to max_iterations or 1 second, whichever comes first. */
	end_jiffies = jiffies + HZ;
	hrtimer_start(&state.timer, KUNIT_IRQ_TEST_HRTIMER_INTERVAL,
		      HRTIMER_MODE_REL_HARD);
	for (int i = 0; i < max_iterations && !time_after(jiffies, end_jiffies);
	     i++) {
		if (!func(test_specific_state))
			state.task_func_reported_failure = true;
	}

	/* Cancel the timer and work. */
	hrtimer_cancel(&state.timer);
	flush_work(&state.bh_work);

	/* Sanity check: the timer and BH functions should have been run. */
	KUNIT_EXPECT_GT_MSG(test, state.hardirq_func_calls, 0,
			    "Timer function was not called");
	KUNIT_EXPECT_GT_MSG(test, state.softirq_func_calls, 0,
			    "BH work function was not called");

	/* Check for incorrect hash values reported from any context. */
	KUNIT_EXPECT_FALSE_MSG(
		test, state.task_func_reported_failure,
		"Incorrect hash values reported from task context");
	KUNIT_EXPECT_FALSE_MSG(
		test, state.hardirq_func_reported_failure,
		"Incorrect hash values reported from hardirq context");
	KUNIT_EXPECT_FALSE_MSG(
		test, state.softirq_func_reported_failure,
		"Incorrect hash values reported from softirq context");
}

#endif /* _KUNIT_RUN_IN_IRQ_CONTEXT_H */
