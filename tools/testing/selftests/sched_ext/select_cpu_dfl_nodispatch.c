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
#include "select_cpu_dfl_nodispatch.bpf.skel.h"
#include "scx_test.h"

#define NUM_CHILDREN 1028

static enum scx_test_status setup(void **ctx)
{
	struct select_cpu_dfl_nodispatch *skel;

	skel = select_cpu_dfl_nodispatch__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(select_cpu_dfl_nodispatch__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct select_cpu_dfl_nodispatch *skel = ctx;
	struct bpf_link *link;
	pid_t pids[NUM_CHILDREN];
	int i, status;

	link = bpf_map__attach_struct_ops(skel->maps.select_cpu_dfl_nodispatch_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	for (i = 0; i < NUM_CHILDREN; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			sleep(1);
			exit(0);
		}
	}

	for (i = 0; i < NUM_CHILDREN; i++) {
		SCX_EQ(waitpid(pids[i], &status, 0), pids[i]);
		SCX_EQ(status, 0);
	}

	SCX_ASSERT(skel->bss->saw_local);

	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct select_cpu_dfl_nodispatch *skel = ctx;

	select_cpu_dfl_nodispatch__destroy(skel);
}

struct scx_test select_cpu_dfl_nodispatch = {
	.name = "select_cpu_dfl_nodispatch",
	.description = "Verify behavior of scx_bpf_select_cpu_dfl() in "
		       "ops.select_cpu()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&select_cpu_dfl_nodispatch)
