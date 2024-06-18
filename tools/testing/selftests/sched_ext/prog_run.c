/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <bpf/bpf.h>
#include <sched.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "prog_run.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status setup(void **ctx)
{
	struct prog_run *skel;

	skel = prog_run__open_and_load();
	if (!skel) {
		SCX_ERR("Failed to open and load skel");
		return SCX_TEST_FAIL;
	}
	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct prog_run *skel = ctx;
	struct bpf_link *link;
	int prog_fd, err = 0;

	prog_fd = bpf_program__fd(skel->progs.prog_run_syscall);
	if (prog_fd < 0) {
		SCX_ERR("Failed to get BPF_PROG_RUN prog");
		return SCX_TEST_FAIL;
	}

	LIBBPF_OPTS(bpf_test_run_opts, topts);

	link = bpf_map__attach_struct_ops(skel->maps.prog_run_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		close(prog_fd);
		return SCX_TEST_FAIL;
	}

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	SCX_EQ(err, 0);

	/* Assumes uei.kind is written last */
	while (skel->data->uei.kind == EXIT_KIND(SCX_EXIT_NONE))
		sched_yield();

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG_BPF));
	SCX_EQ(skel->data->uei.exit_code, 0xdeadbeef);
	close(prog_fd);
	bpf_link__destroy(link);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct prog_run *skel = ctx;

	prog_run__destroy(skel);
}

struct scx_test prog_run = {
	.name = "prog_run",
	.description = "Verify we can call into a scheduler with BPF_PROG_RUN, and invoke kfuncs",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&prog_run)
