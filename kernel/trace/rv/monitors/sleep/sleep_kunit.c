// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <trace/events/syscalls.h>
#include <trace/events/sched.h>
#include <uapi/linux/futex.h>
#include "sleep_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_SLEEP)

static void rv_test_sleep(struct kunit *test)
{
	struct task_struct *target = rv_kunit_alloc_mock_task(test);
	struct task_struct *other = rv_kunit_alloc_mock_task(test);
	struct rv_kunit_ctx *ctx = test->priv;
	unsigned long args[6] = {0};
	struct pt_regs regs = {0};

	prepare_test(test, &rv_sleep_ops.mon);
	target->policy = SCHED_FIFO;
	target->prio = MAX_RT_PRIO - 2;
	other->policy = SCHED_FIFO;
	other->prio = MAX_RT_PRIO - 1;
	rv_sleep_ops.handle_task_newtask(NULL, target, 0);

	/* RT task sleeps on a non RT-friendly nanosleep */
	rv_mock_current(target);
	args[0] = CLOCK_REALTIME;
	syscall_set_arguments(target, &regs, args);
#ifdef __NR_clock_nanosleep
	rv_sleep_ops.handle_sys_enter(NULL, &regs, __NR_clock_nanosleep);
#elif defined(__NR_clock_nanosleep_time64)
	rv_sleep_ops.handle_sys_enter(NULL, &regs, __NR_clock_nanosleep_time64);
#endif
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sleep_ops.handle_sched_set_state(NULL, target, TASK_INTERRUPTIBLE);
	rv_sleep_ops.handle_sys_exit(NULL, NULL, 0);

	/* RT task woken up by lower priority task */
	args[1] = FUTEX_WAIT;
	syscall_set_arguments(target, &regs, args);
	rv_mock_current(target);
#ifdef __NR_futex
	rv_sleep_ops.handle_sys_enter(NULL, &regs, __NR_futex);
#elif defined(__NR_futex_time64)
	rv_sleep_ops.handle_sys_enter(NULL, &regs, __NR_futex_time64);
#endif
	rv_sleep_ops.handle_sched_set_state(NULL, target, TASK_INTERRUPTIBLE);
	rv_mock_current(other);
	rv_sleep_ops.handle_sched_waking(NULL, target);
	rv_mock_current(target);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sleep_ops.handle_sched_exit(NULL, true);
}

#else
#define rv_test_sleep rv_test_stub
#endif
