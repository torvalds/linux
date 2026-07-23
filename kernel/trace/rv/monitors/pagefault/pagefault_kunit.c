// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
#include <linux/sched/deadline.h>
#include <linux/sched/rt.h>
#include "pagefault_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_PAGEFAULT)

static void rv_test_pagefault(struct kunit *test)
{
	struct task_struct *target = rv_kunit_alloc_mock_task(test);
	struct rv_kunit_ctx *ctx = test->priv;

	prepare_test(test, &rv_pagefault_ops.mon);

	/* Initial pagefault when non-RT to start the model without failure */
	target->policy = SCHED_NORMAL;
	target->prio = MAX_RT_PRIO + 20;
	rv_pagefault_ops.handle_task_newtask(NULL, target, 0);
	rv_mock_current(target);
	rv_pagefault_ops.handle_page_fault(NULL, 0, NULL, 0);

	/* RT task has a page fault */
	target->policy = SCHED_FIFO;
	target->prio = MAX_RT_PRIO - 1;
	RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
		rv_pagefault_ops.handle_page_fault(NULL, 0, NULL, 0);
}

#else
#define rv_test_pagefault rv_test_stub
#endif
