// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Andrea Righi <arighi@nvidia.com>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "allowed_cpus.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct allowed_cpus *skel;

	skel = allowed_cpus__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(allowed_cpus__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct allowed_cpus *skel = ctx;
	struct bpf_link *link;

	link = bpf_map__attach_struct_ops(skel->maps.allowed_cpus_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	/* Just sleeping is fine, plenty of scheduling events happening */
	sleep(1);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_NONE));
	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct allowed_cpus *skel = ctx;

	allowed_cpus__destroy(skel);
}

struct scx_test allowed_cpus = {
	.name = "allowed_cpus",
	.description = "Verify scx_bpf_select_cpu_and()",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&allowed_cpus)
