// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Valve Corporation.
 * Author: Changwoo Min <changwoo@igalia.com>
 */

#include <test_progs.h>
#include <sys/syscall.h>
#include "test_ctx.skel.h"

void test_exe_ctx(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	cpu_set_t old_cpuset, target_cpuset;
	struct test_ctx *skel;
	int err, prog_fd;

	/* 1. Pin the current process to CPU 0. */
	if (sched_getaffinity(0, sizeof(old_cpuset), &old_cpuset) == 0) {
		CPU_ZERO(&target_cpuset);
		CPU_SET(0, &target_cpuset);
		ASSERT_OK(sched_setaffinity(0, sizeof(target_cpuset),
					    &target_cpuset), "setaffinity");
	}

	skel = test_ctx__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto restore_affinity;

	err = test_ctx__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* 2. When we run this, the kernel will execute the BPF prog on CPU 0. */
	prog_fd = bpf_program__fd(skel->progs.trigger_all_contexts);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(err, "test_run_trigger");

	/* 3. Wait for the local CPU's softirq/tasklet to finish. */
	for (int i = 0; i < 1000; i++) {
		if (skel->bss->count_task > 0 &&
		    skel->bss->count_hardirq > 0 &&
		    skel->bss->count_softirq > 0)
			break;
		usleep(1000); /* Wait 1ms per iteration, up to 1 sec total */
	}

	/* On CPU 0, these should now all be non-zero. */
	ASSERT_GT(skel->bss->count_task, 0, "task_ok");
	ASSERT_GT(skel->bss->count_hardirq, 0, "hardirq_ok");
	ASSERT_GT(skel->bss->count_softirq, 0, "softirq_ok");

cleanup:
	test_ctx__destroy(skel);

restore_affinity:
	ASSERT_OK(sched_setaffinity(0, sizeof(old_cpuset), &old_cpuset),
		  "restore_affinity");
}
