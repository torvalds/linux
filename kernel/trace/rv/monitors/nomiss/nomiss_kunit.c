// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <trace/events/sched.h>
#include "nomiss_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_NOMISS)

static void rv_test_nomiss(struct kunit *test)
{
	struct task_struct *target = rv_kunit_alloc_mock_task(test);
	struct task_struct *other = rv_kunit_alloc_mock_task(test);
	struct rv_kunit_ctx *ctx = test->priv;

	prepare_test(test, &rv_nomiss_ops.mon);

	target->pid = 99;
	target->policy = SCHED_DEADLINE;
	target->dl.runtime = 10000;
	target->dl.dl_deadline = 20000;

	rv_nomiss_ops.handle_newtask(NULL, target, 0);

	/* Task gets preempted and can't terminate before deadline */
	rv_nomiss_ops.handle_sched_switch(NULL, 0, other, target, TASK_RUNNING);
	rv_nomiss_ops.handle_dl_replenish(NULL, &target->dl, 0, DL_TASK);
	udelay(10);
	rv_nomiss_ops.handle_sched_switch(NULL, 0, target, other, TASK_RUNNING);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx) {
		udelay(15 + *rv_nomiss_ops.deadline_thresh / 1000);
		rv_nomiss_ops.handle_sched_switch(NULL, 0, other, target, TASK_RUNNING);
	}
}

#else
#define rv_test_nomiss rv_test_stub
#endif
