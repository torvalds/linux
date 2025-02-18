/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "enq_last_no_enq_fails.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct enq_last_no_enq_fails *skel;

	skel = enq_last_no_enq_fails__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(enq_last_no_enq_fails__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct enq_last_no_enq_fails *skel = ctx;
	struct bpf_link *link;

	link = bpf_map__attach_struct_ops(skel->maps.enq_last_no_enq_fails_ops);
	if (!link) {
		SCX_ERR("Incorrectly failed at attaching scheduler");
		return SCX_TEST_FAIL;
	}
	if (!skel->bss->exit_kind) {
		SCX_ERR("Incorrectly stayed loaded");
		return SCX_TEST_FAIL;
	}

	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct enq_last_no_enq_fails *skel = ctx;

	enq_last_no_enq_fails__destroy(skel);
}

struct scx_test enq_last_no_enq_fails = {
	.name = "enq_last_no_enq_fails",
	.description = "Verify we eject a scheduler if we specify "
		       "the SCX_OPS_ENQ_LAST flag without defining "
		       "ops.enqueue()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&enq_last_no_enq_fails)
