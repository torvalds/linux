// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <trace/events/sched.h>
#include "sts_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_STS)

static void rv_test_sts(struct kunit *test)
{
	struct task_struct *target = rv_kunit_alloc_mock_task(test);
	struct task_struct *other = rv_kunit_alloc_mock_task(test);
	struct rv_kunit_ctx *ctx = test->priv;

	prepare_test(test, &rv_sts_ops.mon);
	/* Per-CPU monitor, make sure we don't change CPU mid-test */
	guard(migrate)();

	/* Switch without disabling interrupts */
	rv_sts_ops.handle_schedule_exit(NULL, false);
	rv_sts_ops.handle_schedule_entry(NULL, false);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sts_ops.handle_sched_switch(NULL, 0, target, other, TASK_RUNNING);

	rv_sts_ops.handle_schedule_exit(NULL, false);

	/* Schedule from interrupt context */
	rv_sts_ops.handle_schedule_entry(NULL, false);
	rv_sts_ops.handle_irq_disable(NULL, 0, 0);
	rv_sts_ops.handle_irq_entry(NULL, 0, NULL);
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_sts_ops.handle_sched_switch(NULL, 0, target, other, TASK_RUNNING);
	rv_sts_ops.handle_irq_enable(NULL, 0, 0);
}

#else
#define rv_test_sts rv_test_stub
#endif
