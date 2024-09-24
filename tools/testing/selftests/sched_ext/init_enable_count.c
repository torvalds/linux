/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include "scx_test.h"
#include "init_enable_count.bpf.skel.h"

#define SCHED_EXT 7

static struct init_enable_count *
open_load_prog(bool global)
{
	struct init_enable_count *skel;

	skel = init_enable_count__open();
	SCX_BUG_ON(!skel, "Failed to open skel");

	if (!global)
		skel->struct_ops.init_enable_count_ops->flags |= SCX_OPS_SWITCH_PARTIAL;

	SCX_BUG_ON(init_enable_count__load(skel), "Failed to load skel");

	return skel;
}

static enum scx_test_status run_test(bool global)
{
	struct init_enable_count *skel;
	struct bpf_link *link;
	const u32 num_children = 5, num_pre_forks = 1024;
	int ret, i, status;
	struct sched_param param = {};
	pid_t pids[num_pre_forks];

	skel = open_load_prog(global);

	/*
	 * Fork a bunch of children before we attach the scheduler so that we
	 * ensure (at least in practical terms) that there are more tasks that
	 * transition from SCHED_OTHER -> SCHED_EXT than there are tasks that
	 * take the fork() path either below or in other processes.
	 */
	for (i = 0; i < num_pre_forks; i++) {
		pids[i] = fork();
		SCX_FAIL_IF(pids[i] < 0, "Failed to fork child");
		if (pids[i] == 0) {
			sleep(1);
			exit(0);
		}
	}

	link = bpf_map__attach_struct_ops(skel->maps.init_enable_count_ops);
	SCX_FAIL_IF(!link, "Failed to attach struct_ops");

	for (i = 0; i < num_pre_forks; i++) {
		SCX_FAIL_IF(waitpid(pids[i], &status, 0) != pids[i],
			    "Failed to wait for pre-forked child\n");

		SCX_FAIL_IF(status != 0, "Pre-forked child %d exited with status %d\n", i,
			    status);
	}

	bpf_link__destroy(link);
	SCX_GE(skel->bss->init_task_cnt, num_pre_forks);
	SCX_GE(skel->bss->exit_task_cnt, num_pre_forks);

	link = bpf_map__attach_struct_ops(skel->maps.init_enable_count_ops);
	SCX_FAIL_IF(!link, "Failed to attach struct_ops");

	/* SCHED_EXT children */
	for (i = 0; i < num_children; i++) {
		pids[i] = fork();
		SCX_FAIL_IF(pids[i] < 0, "Failed to fork child");

		if (pids[i] == 0) {
			ret = sched_setscheduler(0, SCHED_EXT, &param);
			SCX_BUG_ON(ret, "Failed to set sched to sched_ext");

			/*
			 * Reset to SCHED_OTHER for half of them. Counts for
			 * everything should still be the same regardless, as
			 * ops.disable() is invoked even if a task is still on
			 * SCHED_EXT before it exits.
			 */
			if (i % 2 == 0) {
				ret = sched_setscheduler(0, SCHED_OTHER, &param);
				SCX_BUG_ON(ret, "Failed to reset sched to normal");
			}
			exit(0);
		}
	}
	for (i = 0; i < num_children; i++) {
		SCX_FAIL_IF(waitpid(pids[i], &status, 0) != pids[i],
			    "Failed to wait for SCX child\n");

		SCX_FAIL_IF(status != 0, "SCX child %d exited with status %d\n", i,
			    status);
	}

	/* SCHED_OTHER children */
	for (i = 0; i < num_children; i++) {
		pids[i] = fork();
		if (pids[i] == 0)
			exit(0);
	}

	for (i = 0; i < num_children; i++) {
		SCX_FAIL_IF(waitpid(pids[i], &status, 0) != pids[i],
			    "Failed to wait for normal child\n");

		SCX_FAIL_IF(status != 0, "Normal child %d exited with status %d\n", i,
			    status);
	}

	bpf_link__destroy(link);

	SCX_GE(skel->bss->init_task_cnt, 2 * num_children);
	SCX_GE(skel->bss->exit_task_cnt, 2 * num_children);

	if (global) {
		SCX_GE(skel->bss->enable_cnt, 2 * num_children);
		SCX_GE(skel->bss->disable_cnt, 2 * num_children);
	} else {
		SCX_EQ(skel->bss->enable_cnt, num_children);
		SCX_EQ(skel->bss->disable_cnt, num_children);
	}
	/*
	 * We forked a ton of tasks before we attached the scheduler above, so
	 * this should be fine. Technically it could be flaky if a ton of forks
	 * are happening at the same time in other processes, but that should
	 * be exceedingly unlikely.
	 */
	SCX_GT(skel->bss->init_transition_cnt, skel->bss->init_fork_cnt);
	SCX_GE(skel->bss->init_fork_cnt, 2 * num_children);

	init_enable_count__destroy(skel);

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	enum scx_test_status status;

	status = run_test(true);
	if (status != SCX_TEST_PASS)
		return status;

	return run_test(false);
}

struct scx_test init_enable_count = {
	.name = "init_enable_count",
	.description = "Verify we do the correct amount of counting of init, "
		       "enable, etc callbacks.",
	.run = run,
};
REGISTER_SCX_TEST(&init_enable_count)
