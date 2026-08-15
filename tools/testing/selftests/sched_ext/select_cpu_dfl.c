/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "select_cpu_dfl.bpf.skel.h"
#include "scx_test.h"

#define NUM_CHILDREN 1028

struct select_cpu_dfl_ctx {
	struct select_cpu_dfl	*skel;
	struct bpf_link		*link;
};

static enum scx_test_status setup(void **ctx)
{
	struct select_cpu_dfl_ctx *tctx;

	tctx = malloc(sizeof(*tctx));
	SCX_FAIL_IF(!tctx, "Failed to allocate test context");
	tctx->link = NULL;

	tctx->skel = select_cpu_dfl__open();
	if (!tctx->skel) {
		free(tctx);
		SCX_FAIL("Failed to open");
	}
	SCX_ENUM_INIT(tctx->skel);
	if (select_cpu_dfl__load(tctx->skel)) {
		select_cpu_dfl__destroy(tctx->skel);
		free(tctx);
		SCX_FAIL("Failed to load skel");
	}

	*ctx = tctx;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct select_cpu_dfl_ctx *tctx = ctx;
	pid_t pids[NUM_CHILDREN];
	int i, status, nforked = 0;

	tctx->link = bpf_map__attach_struct_ops(tctx->skel->maps.select_cpu_dfl_ops);
	SCX_FAIL_IF(!tctx->link, "Failed to attach scheduler");

	for (i = 0; i < NUM_CHILDREN; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			sleep(1);
			exit(0);
		}
		if (pids[i] > 0)
			nforked++;
	}

	for (i = 0; i < NUM_CHILDREN; i++) {
		if (pids[i] <= 0)
			continue;
		SCX_EQ(waitpid(pids[i], &status, 0), pids[i]);
		SCX_EQ(status, 0);
	}

	SCX_GT(nforked, 0);
	SCX_ASSERT(!tctx->skel->bss->saw_local);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct select_cpu_dfl_ctx *tctx = ctx;

	if (tctx->link)
		bpf_link__destroy(tctx->link);
	select_cpu_dfl__destroy(tctx->skel);
	free(tctx);
}

struct scx_test select_cpu_dfl = {
	.name = "select_cpu_dfl",
	.description = "Verify the default ops.select_cpu() dispatches tasks "
		       "when idles cores are found, and skips ops.enqueue()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&select_cpu_dfl)
