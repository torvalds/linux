// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
/*
 * XXX: include required headers, e.g.,
 * #include <linux/sched.h>
 */
#include "%%MODEL_NAME%%_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_%%MODEL_NAME_UP%%)

static void rv_test_%%MODEL_NAME%%(struct kunit *test)
{
	struct rv_kunit_ctx *ctx = test->priv;
	/*
	 * If you need to create task_structs with rv_kunit_alloc_mock_task()
	 * do it BEFORE preparing the test.
	 */

	prepare_test(test, &%%STRUCT_NAME%%.mon);

	/*
	 * XXX: write the test here
	 * e.g.
	 * RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
	 *	%%STRUCT_NAME%%.handle_event(args);
	 */
}

#else
#define rv_test_%%MODEL_NAME%% rv_test_stub
#endif
