// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <trace/events/sched.h>
#include "sco_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_SCO)

static void rv_test_sco(struct kunit *test)
{
	struct task_struct *target = rv_kunit_alloc_mock_task(test);
	struct rv_kunit_ctx *ctx = test->priv;

	prepare_test(test, &rv_sco_ops.mon);

	/* Ensure we keep the same per-cpu monitor */
	guard(migrate)();

	/* Set state while scheduling */
	rv_sco_ops.handle_sched_set_state(NULL, target, TASK_INTERRUPTIBLE);
	rv_sco_ops.handle_schedule_entry(NULL, false);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sco_ops.handle_sched_set_state(NULL, target, TASK_INTERRUPTIBLE);
}

#else
#define rv_test_sco rv_test_stub
#endif
