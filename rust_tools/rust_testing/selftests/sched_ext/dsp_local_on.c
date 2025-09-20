/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <unistd.h>
#include "dsp_local_on.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct dsp_local_on *skel;

	skel = dsp_local_on__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);

	skel->rodata->nr_cpus = libbpf_num_possible_cpus();
	SCX_FAIL_IF(dsp_local_on__load(skel), "Failed to load skel");
	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct dsp_local_on *skel = ctx;
	struct bpf_link *link;

	link = bpf_map__attach_struct_ops(skel->maps.dsp_local_on_ops);
	SCX_FAIL_IF(!link, "Failed to attach struct_ops");

	/* Just sleeping is fine, plenty of scheduling events happening */
	sleep(1);

	bpf_link__destroy(link);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct dsp_local_on *skel = ctx;

	dsp_local_on__destroy(skel);
}

struct scx_test dsp_local_on = {
	.name = "dsp_local_on",
	.description = "Verify we can directly dispatch tasks to a local DSQs "
		       "from ops.dispatch()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&dsp_local_on)
