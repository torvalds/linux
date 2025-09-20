/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_test.h"

static bool setup_called = false;
static bool run_called = false;
static bool cleanup_called = false;

static int context = 10;

static enum scx_test_status setup(void **ctx)
{
	setup_called = true;
	*ctx = &context;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	int *arg = ctx;

	SCX_ASSERT(setup_called);
	SCX_ASSERT(!run_called && !cleanup_called);
	SCX_EQ(*arg, context);

	run_called = true;
	return SCX_TEST_PASS;
}

static void cleanup (void *ctx)
{
	SCX_BUG_ON(!run_called || cleanup_called, "Wrong callbacks invoked");
}

struct scx_test example = {
	.name		= "example",
	.description	= "Validate the basic function of the test suite itself",
	.setup		= setup,
	.run		= run,
	.cleanup	= cleanup,
};
REGISTER_SCX_TEST(&example)
