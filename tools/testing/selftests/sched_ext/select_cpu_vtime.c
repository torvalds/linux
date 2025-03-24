/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "select_cpu_vtime.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct select_cpu_vtime *skel;

	skel = select_cpu_vtime__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(select_cpu_vtime__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct select_cpu_vtime *skel = ctx;
	struct bpf_link *link;

	SCX_ASSERT(!skel->bss->consumed);

	link = bpf_map__attach_struct_ops(skel->maps.select_cpu_vtime_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	sleep(1);

	SCX_ASSERT(skel->bss->consumed);

	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct select_cpu_vtime *skel = ctx;

	select_cpu_vtime__destroy(skel);
}

struct scx_test select_cpu_vtime = {
	.name = "select_cpu_vtime",
	.description = "Test doing direct vtime-dispatching from "
		       "ops.select_cpu(), to a non-built-in DSQ",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&select_cpu_vtime)
