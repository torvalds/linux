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
#include "minimal.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct minimal *skel;

	skel = minimal__open_and_load();
	if (!skel) {
		SCX_ERR("Failed to open and load skel");
		return SCX_TEST_FAIL;
	}
	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct minimal *skel = ctx;
	struct bpf_link *link;

	link = bpf_map__attach_struct_ops(skel->maps.minimal_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		return SCX_TEST_FAIL;
	}

	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct minimal *skel = ctx;

	minimal__destroy(skel);
}

struct scx_test minimal = {
	.name = "minimal",
	.description = "Verify we can load a fully minimal scheduler",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&minimal)
