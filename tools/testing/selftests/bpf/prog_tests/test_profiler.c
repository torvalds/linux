// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include "progs/profiler.h"
#include "profiler1.skel.h"
#include "profiler2.skel.h"
#include "profiler3.skel.h"

static int sanity_run(struct bpf_program *prog)
{
	LIBBPF_OPTS(bpf_test_run_opts, test_attr);
	__u64 args[] = {1, 2, 3};
	int err, prog_fd;

	prog_fd = bpf_program__fd(prog);
	test_attr.ctx_in = args;
	test_attr.ctx_size_in = sizeof(args);
	err = bpf_prog_test_run_opts(prog_fd, &test_attr);
	if (!ASSERT_OK(err, "test_run"))
		return -1;

	if (!ASSERT_OK(test_attr.retval, "test_run retval"))
		return -1;

	return 0;
}

void test_test_profiler(void)
{
	struct profiler1 *profiler1_skel = NULL;
	struct profiler2 *profiler2_skel = NULL;
	struct profiler3 *profiler3_skel = NULL;
	__u32 duration = 0;
	int err;

	profiler1_skel = profiler1__open_and_load();
	if (CHECK(!profiler1_skel, "profiler1_skel_load", "profiler1 skeleton failed\n"))
		goto cleanup;

	err = profiler1__attach(profiler1_skel);
	if (CHECK(err, "profiler1_attach", "profiler1 attach failed: %d\n", err))
		goto cleanup;

	if (sanity_run(profiler1_skel->progs.raw_tracepoint__sched_process_exec))
		goto cleanup;

	profiler2_skel = profiler2__open_and_load();
	if (CHECK(!profiler2_skel, "profiler2_skel_load", "profiler2 skeleton failed\n"))
		goto cleanup;

	err = profiler2__attach(profiler2_skel);
	if (CHECK(err, "profiler2_attach", "profiler2 attach failed: %d\n", err))
		goto cleanup;

	if (sanity_run(profiler2_skel->progs.raw_tracepoint__sched_process_exec))
		goto cleanup;

	profiler3_skel = profiler3__open_and_load();
	if (CHECK(!profiler3_skel, "profiler3_skel_load", "profiler3 skeleton failed\n"))
		goto cleanup;

	err = profiler3__attach(profiler3_skel);
	if (CHECK(err, "profiler3_attach", "profiler3 attach failed: %d\n", err))
		goto cleanup;

	if (sanity_run(profiler3_skel->progs.raw_tracepoint__sched_process_exec))
		goto cleanup;
cleanup:
	profiler1__destroy(profiler1_skel);
	profiler2__destroy(profiler2_skel);
	profiler3__destroy(profiler3_skel);
}
