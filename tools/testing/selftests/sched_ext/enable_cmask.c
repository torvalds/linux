// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Tejun Heo <tj@kernel.org> */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "enable_cmask.bpf.skel.h"
#include "scx_test.h"

#define SCHED_EXT 7
#define NR_CHILDREN 8
#define MAX_CPUS 1024

static int cpus[MAX_CPUS];
static int nr_cpus;

static void spin_ms(int ms)
{
	struct timespec start, now;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while ((now.tv_sec - start.tv_sec) * 1000 +
		 (now.tv_nsec - start.tv_nsec) / 1000000 < ms);
}

static int pin(pid_t pid, int idx)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpus[idx % nr_cpus], &set);
	return sched_setaffinity(pid, sizeof(set), &set);
}

/*
 * Pin, switch to SCHED_EXT for a class-switch enable, fork a grandchild that
 * inherits the policy for a fork-path enable, then change affinity a few times
 * while running for set_cmask() on live tasks.
 */
static int child(int idx)
{
	struct sched_param param = {};
	int i, status;
	pid_t pid;

	if (pin(0, idx) || sched_setscheduler(0, SCHED_EXT, &param))
		return 1;

	pid = fork();
	if (pid < 0)
		return 1;
	if (!pid) {
		spin_ms(20);
		return 0;
	}

	for (i = 1; i <= 4; i++) {
		if (pin(0, idx + i))
			return 1;
		spin_ms(5);
	}

	return waitpid(pid, &status, 0) == pid && !status ? 0 : 1;
}

static enum scx_test_status run(void *ctx)
{
	struct enable_cmask *skel;
	struct bpf_link *link;
	pid_t pids[NR_CHILDREN];
	cpu_set_t set;
	int i, status, failed = 0;

	if (!__COMPAT_struct_has_field("scx_enable_args", "cmask_arena_addr"))
		return SCX_TEST_SKIP;

	SCX_FAIL_IF(sched_getaffinity(0, sizeof(set), &set), "Failed to read affinity");
	for (i = 0; i < MAX_CPUS && i < CPU_SETSIZE; i++)
		if (CPU_ISSET(i, &set))
			cpus[nr_cpus++] = i;
	if (nr_cpus < 2)
		return SCX_TEST_SKIP;

	skel = enable_cmask__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(enable_cmask__load(skel), "Failed to load skel");

	link = bpf_map__attach_struct_ops(skel->maps.enable_cmask_ops);
	SCX_FAIL_IF(!link, "Failed to attach struct_ops");

	for (i = 0; i < NR_CHILDREN; i++) {
		pids[i] = fork();
		SCX_FAIL_IF(pids[i] < 0, "Failed to fork");
		if (!pids[i])
			exit(child(i));
	}

	/* affinity changes from the outside race with the children's own */
	for (i = 0; i < NR_CHILDREN; i++)
		pin(pids[i], i + NR_CHILDREN);

	for (i = 0; i < NR_CHILDREN; i++) {
		if (waitpid(pids[i], &status, 0) != pids[i] || status)
			failed++;
	}

	bpf_link__destroy(link);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));
	SCX_EQ(failed, 0);
	SCX_GE(skel->bss->nr_enable, 2 * NR_CHILDREN);
	SCX_EQ(skel->bss->nr_initial_set_cmask, skel->bss->nr_enable);
	SCX_GT(skel->bss->nr_set_cmask, skel->bss->nr_initial_set_cmask);
	SCX_GE(skel->bss->nr_set_weight, skel->bss->nr_enable);
	printf("enable=%lu initial_set_cmask=%lu set_cmask=%lu set_weight=%lu\n",
	       (unsigned long)skel->bss->nr_enable,
	       (unsigned long)skel->bss->nr_initial_set_cmask,
	       (unsigned long)skel->bss->nr_set_cmask,
	       (unsigned long)skel->bss->nr_set_weight);

	enable_cmask__destroy(skel);
	return SCX_TEST_PASS;
}

struct scx_test enable_cmask = {
	.name = "enable_cmask",
	.description = "Check the cid-form ops.enable() cmask and the set_cmask() after it",
	.run = run,
};
REGISTER_SCX_TEST(&enable_cmask)
