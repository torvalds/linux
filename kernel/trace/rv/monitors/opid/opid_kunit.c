// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <trace/events/sched.h>
#include "opid_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_OPID)

static void rv_test_opid(struct kunit *test)
{
	struct rv_kunit_ctx *ctx = test->priv;

	prepare_test(test, &rv_opid_ops.mon);

	/* Ensure we keep the same per-cpu monitor */
	guard(migrate)();
	KUNIT_EXPECT_TRUE(test, preemptible());

	/* Wakeup with preemption and interrupts enabled */
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_opid_ops.handle_sched_waking(NULL, NULL);

	/* Need resched with interrupts enabled */
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx) {
		scoped_guard(preempt)
			rv_opid_ops.handle_sched_need_resched(NULL, NULL, 0, TIF_NEED_RESCHED);
	}
}

#else
#define rv_test_opid rv_test_stub
#endif
