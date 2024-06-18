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
#include "select_cpu_dispatch_bad_dsq.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct select_cpu_dispatch_bad_dsq *skel;

	skel = select_cpu_dispatch_bad_dsq__open_and_load();
	SCX_FAIL_IF(!skel, "Failed to open and load skel");
	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct select_cpu_dispatch_bad_dsq *skel = ctx;
	struct bpf_link *link;

	link = bpf_map__attach_struct_ops(skel->maps.select_cpu_dispatch_bad_dsq_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	sleep(1);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_ERROR));
	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct select_cpu_dispatch_bad_dsq *skel = ctx;

	select_cpu_dispatch_bad_dsq__destroy(skel);
}

struct scx_test select_cpu_dispatch_bad_dsq = {
	.name = "select_cpu_dispatch_bad_dsq",
	.description = "Verify graceful failure if we direct-dispatch to a "
		       "bogus DSQ in ops.select_cpu()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&select_cpu_dispatch_bad_dsq)
