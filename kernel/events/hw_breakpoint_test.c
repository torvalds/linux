// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for hw_breakpoint constraints accounting logic.
 *
 * Copyright (C) 2022, Google LLC.
 */

#include <kunit/test.h>
#include <linux/cpumask.h>
#include <linux/hw_breakpoint.h>
#include <linux/kthread.h>
#include <linux/perf_event.h>
#include <asm/hw_breakpoint.h>

#define TEST_REQUIRES_BP_SLOTS(test, slots)						\
	do {										\
		if ((slots) > get_test_bp_slots()) {					\
			kunit_skip((test), "Requires breakpoint slots: %d > %d", slots,	\
				   get_test_bp_slots());				\
		}									\
	} while (0)

#define TEST_EXPECT_NOSPC(expr) KUNIT_EXPECT_EQ(test, -ENOSPC, PTR_ERR(expr))

#define MAX_TEST_BREAKPOINTS 512

static char break_vars[MAX_TEST_BREAKPOINTS];
static struct perf_event *test_bps[MAX_TEST_BREAKPOINTS];
static struct task_struct *__other_task;

static struct perf_event *register_test_bp(int cpu, struct task_struct *tsk, int idx)
{
	struct perf_event_attr attr = {};

	if (WARN_ON(idx < 0 || idx >= MAX_TEST_BREAKPOINTS))
		return NULL;

	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)&break_vars[idx];
	attr.bp_len = HW_BREAKPOINT_LEN_1;
	attr.bp_type = HW_BREAKPOINT_RW;
	return perf_event_create_kernel_counter(&attr, cpu, tsk, NULL, NULL);
}

static void unregister_test_bp(struct perf_event **bp)
{
	if (WARN_ON(IS_ERR(*bp)))
		return;
	if (WARN_ON(!*bp))
		return;
	unregister_hw_breakpoint(*bp);
	*bp = NULL;
}

static int get_test_bp_slots(void)
{
	static int slots;

	if (!slots)
		slots = hw_breakpoint_slots(TYPE_DATA);

	return slots;
}

static void fill_one_bp_slot(struct kunit *test, int *id, int cpu, struct task_struct *tsk)
{
	struct perf_event *bp = register_test_bp(cpu, tsk, *id);

	KUNIT_ASSERT_NOT_NULL(test, bp);
	KUNIT_ASSERT_FALSE(test, IS_ERR(bp));
	KUNIT_ASSERT_NULL(test, test_bps[*id]);
	test_bps[(*id)++] = bp;
}

/*
 * Fills up the given @cpu/@tsk with breakpoints, only leaving @skip slots free.
 *
 * Returns true if this can be called again, continuing at @id.
 */
static bool fill_bp_slots(struct kunit *test, int *id, int cpu, struct task_struct *tsk, int skip)
{
	for (int i = 0; i < get_test_bp_slots() - skip; ++i)
		fill_one_bp_slot(test, id, cpu, tsk);

	return *id + get_test_bp_slots() <= MAX_TEST_BREAKPOINTS;
}

static int dummy_kthread(void *arg)
{
	return 0;
}

static struct task_struct *get_other_task(struct kunit *test)
{
	struct task_struct *tsk;

	if (__other_task)
		return __other_task;

	tsk = kthread_create(dummy_kthread, NULL, "hw_breakpoint_dummy_task");
	KUNIT_ASSERT_FALSE(test, IS_ERR(tsk));
	__other_task = tsk;
	return __other_task;
}

static int get_test_cpu(int num)
{
	int cpu;

	WARN_ON(num < 0);

	for_each_online_cpu(cpu) {
		if (num-- <= 0)
			break;
	}

	return cpu;
}

/* ===== Test cases ===== */

static void test_one_cpu(struct kunit *test)
{
	int idx = 0;

	fill_bp_slots(test, &idx, get_test_cpu(0), NULL, 0);
	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
}

static void test_many_cpus(struct kunit *test)
{
	int idx = 0;
	int cpu;

	/* Test that CPUs are independent. */
	for_each_online_cpu(cpu) {
		bool do_continue = fill_bp_slots(test, &idx, cpu, NULL, 0);

		TEST_EXPECT_NOSPC(register_test_bp(cpu, NULL, idx));
		if (!do_continue)
			break;
	}
}

static void test_one_task_on_all_cpus(struct kunit *test)
{
	int idx = 0;

	fill_bp_slots(test, &idx, -1, current, 0);
	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
	/* Remove one and adding back CPU-target should work. */
	unregister_test_bp(&test_bps[0]);
	fill_one_bp_slot(test, &idx, get_test_cpu(0), NULL);
}

static void test_two_tasks_on_all_cpus(struct kunit *test)
{
	int idx = 0;

	/* Test that tasks are independent. */
	fill_bp_slots(test, &idx, -1, current, 0);
	fill_bp_slots(test, &idx, -1, get_other_task(test), 0);

	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(-1, get_other_task(test), idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), get_other_task(test), idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
	/* Remove one from first task and adding back CPU-target should not work. */
	unregister_test_bp(&test_bps[0]);
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
}

static void test_one_task_on_one_cpu(struct kunit *test)
{
	int idx = 0;

	fill_bp_slots(test, &idx, get_test_cpu(0), current, 0);
	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
	/*
	 * Remove one and adding back CPU-target should work; this case is
	 * special vs. above because the task's constraints are CPU-dependent.
	 */
	unregister_test_bp(&test_bps[0]);
	fill_one_bp_slot(test, &idx, get_test_cpu(0), NULL);
}

static void test_one_task_mixed(struct kunit *test)
{
	int idx = 0;

	TEST_REQUIRES_BP_SLOTS(test, 3);

	fill_one_bp_slot(test, &idx, get_test_cpu(0), current);
	fill_bp_slots(test, &idx, -1, current, 1);
	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));

	/* Transition from CPU-dependent pinned count to CPU-independent. */
	unregister_test_bp(&test_bps[0]);
	unregister_test_bp(&test_bps[1]);
	fill_one_bp_slot(test, &idx, get_test_cpu(0), NULL);
	fill_one_bp_slot(test, &idx, get_test_cpu(0), NULL);
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
}

static void test_two_tasks_on_one_cpu(struct kunit *test)
{
	int idx = 0;

	fill_bp_slots(test, &idx, get_test_cpu(0), current, 0);
	fill_bp_slots(test, &idx, get_test_cpu(0), get_other_task(test), 0);

	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(-1, get_other_task(test), idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), get_other_task(test), idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
	/* Can still create breakpoints on some other CPU. */
	fill_bp_slots(test, &idx, get_test_cpu(1), NULL, 0);
}

static void test_two_tasks_on_one_all_cpus(struct kunit *test)
{
	int idx = 0;

	fill_bp_slots(test, &idx, get_test_cpu(0), current, 0);
	fill_bp_slots(test, &idx, -1, get_other_task(test), 0);

	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(-1, get_other_task(test), idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), get_other_task(test), idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
	/* Cannot create breakpoints on some other CPU either. */
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(1), NULL, idx));
}

static void test_task_on_all_and_one_cpu(struct kunit *test)
{
	int tsk_on_cpu_idx, cpu_idx;
	int idx = 0;

	TEST_REQUIRES_BP_SLOTS(test, 3);

	fill_bp_slots(test, &idx, -1, current, 2);
	/* Transitioning from only all CPU breakpoints to mixed. */
	tsk_on_cpu_idx = idx;
	fill_one_bp_slot(test, &idx, get_test_cpu(0), current);
	fill_one_bp_slot(test, &idx, -1, current);

	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));

	/* We should still be able to use up another CPU's slots. */
	cpu_idx = idx;
	fill_one_bp_slot(test, &idx, get_test_cpu(1), NULL);
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(1), NULL, idx));

	/* Transitioning back to task target on all CPUs. */
	unregister_test_bp(&test_bps[tsk_on_cpu_idx]);
	/* Still have a CPU target breakpoint in get_test_cpu(1). */
	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	/* Remove it and try again. */
	unregister_test_bp(&test_bps[cpu_idx]);
	fill_one_bp_slot(test, &idx, -1, current);

	TEST_EXPECT_NOSPC(register_test_bp(-1, current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), current, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(0), NULL, idx));
	TEST_EXPECT_NOSPC(register_test_bp(get_test_cpu(1), NULL, idx));
}

static struct kunit_case hw_breakpoint_test_cases[] = {
	KUNIT_CASE(test_one_cpu),
	KUNIT_CASE(test_many_cpus),
	KUNIT_CASE(test_one_task_on_all_cpus),
	KUNIT_CASE(test_two_tasks_on_all_cpus),
	KUNIT_CASE(test_one_task_on_one_cpu),
	KUNIT_CASE(test_one_task_mixed),
	KUNIT_CASE(test_two_tasks_on_one_cpu),
	KUNIT_CASE(test_two_tasks_on_one_all_cpus),
	KUNIT_CASE(test_task_on_all_and_one_cpu),
	{},
};

static int test_init(struct kunit *test)
{
	/* Most test cases want 2 distinct CPUs. */
	if (num_online_cpus() < 2)
		kunit_skip(test, "not enough cpus");

	/* Want the system to not use breakpoints elsewhere. */
	if (hw_breakpoint_is_used())
		kunit_skip(test, "hw breakpoint already in use");

	return 0;
}

static void test_exit(struct kunit *test)
{
	for (int i = 0; i < MAX_TEST_BREAKPOINTS; ++i) {
		if (test_bps[i])
			unregister_test_bp(&test_bps[i]);
	}

	if (__other_task) {
		kthread_stop(__other_task);
		__other_task = NULL;
	}

	/* Verify that internal state agrees that no breakpoints are in use. */
	KUNIT_EXPECT_FALSE(test, hw_breakpoint_is_used());
}

static struct kunit_suite hw_breakpoint_test_suite = {
	.name = "hw_breakpoint",
	.test_cases = hw_breakpoint_test_cases,
	.init = test_init,
	.exit = test_exit,
};

kunit_test_suites(&hw_breakpoint_test_suite);

MODULE_AUTHOR("Marco Elver <elver@google.com>");
