// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/rv.h>
#include <rv/kunit.h>
/*
 * XXX: include required headers, e.g.,
 * #include <linux/sched.h>
 */
#include "test_bak_kunit_kunit.h"

#if IS_REACHABLE(CONFIG_RV_MON_TEST_BAK_KUNIT)

static void rv_test_test_bak_kunit(struct kunit *test)
{
	struct rv_kunit_ctx *ctx = test->priv;
	/*
	 * If you need to create task_structs with rv_kunit_alloc_mock_task()
	 * do it BEFORE preparing the test.
	 */

	prepare_test(test, &rv_test_bak_kunit_ops.mon);

	/*
	 * XXX: write the test here
	 * e.g.
	 * RV_KUNIT_EXPECT_REACTION_HERE(test, ctx)
	 *	rv_test_bak_kunit_ops.handle_event(args);
	 */
}

#else
#define rv_test_test_bak_kunit rv_test_stub
#endif
