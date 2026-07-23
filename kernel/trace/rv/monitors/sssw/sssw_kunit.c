// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <trace/events/sched.h>
#include "sssw_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_SSSW)

static void rv_test_sssw(struct kunit *test)
{
	struct task_struct *target = rv_kunit_alloc_mock_task(test);
	struct task_struct *other = rv_kunit_alloc_mock_task(test);
	struct rv_kunit_ctx *ctx = test->priv;

	prepare_test(test, &rv_sssw_ops.mon);

	/* Suspend without setting to sleepable */
	rv_sssw_ops.handle_sched_set_state(NULL, target, TASK_RUNNING);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sssw_ops.handle_sched_switch(NULL, 0, target, other, TASK_INTERRUPTIBLE);

	/* Switch in after suspension without wakeup */
	rv_sssw_ops.handle_sched_wakeup(NULL, target);
	rv_sssw_ops.handle_sched_set_state(NULL, target, TASK_INTERRUPTIBLE);
	rv_sssw_ops.handle_sched_switch(NULL, 0, target, other, TASK_INTERRUPTIBLE);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sssw_ops.handle_sched_switch(NULL, 0, other, target, TASK_RUNNING);
}

#else
#define rv_test_sssw rv_test_stub
#endif
