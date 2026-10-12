// SPDX-License-Identifier: GPL-2.0
/*
 * Verify that ops.dequeue() is called for tasks leaving BPF custody through
 * an SCX-internal cross-CPU migration (move_remote_task_to_local_dsq()).
 *
 * Copyright (c) 2026 Google LLC.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include "scx_test.h"
#include "dequeue_remote.bpf.skel.h"

#define MAX_WORKERS	64
#define RUN_MS		2000

static int nr_cpus;

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* mix of short bursts and sleeps to generate lots of enqueues and wakeups */
static void worker_fn(int id)
{
	long long end = now_ms() + RUN_MS;
	volatile unsigned long sum = 0;

	while (now_ms() < end) {
		unsigned long j;

		for (j = 0; j < 20000 + id * 1000; j++)
			sum += j;
		if (id & 1)
			usleep(100);
		else
			sched_yield();
	}

	exit(0);
}

static enum scx_test_status run_scenario(struct dequeue_remote *skel,
					 bool use_move_to_local,
					 const char *name)
{
	enum scx_test_status ret = SCX_TEST_PASS;
	struct bpf_link *link;
	pid_t pids[MAX_WORKERS];
	int nr_workers, nr_forked, i;

	nr_workers = 2 * nr_cpus;
	if (nr_workers < 4)
		nr_workers = 4;
	if (nr_workers > MAX_WORKERS)
		nr_workers = MAX_WORKERS;

	skel->bss->test_use_move_to_local = use_move_to_local;
	skel->bss->enqueue_cnt = 0;
	skel->bss->dequeue_cnt = 0;
	skel->bss->dispatch_dequeue_cnt = 0;
	skel->bss->change_dequeue_cnt = 0;
	skel->bss->remote_dispatch_cnt = 0;
	skel->bss->remote_running_cnt = 0;
	skel->bss->missed_dequeue_cnt = 0;
	skel->bss->core_sched_exec_dequeue_cnt = 0;
	memset(&skel->data->uei, 0, sizeof(skel->data->uei));

	link = bpf_map__attach_struct_ops(skel->maps.dequeue_remote_ops);
	SCX_FAIL_IF(!link, "Failed to attach struct_ops for %s", name);

	fflush(stdout);
	fflush(stderr);

	for (nr_forked = 0; nr_forked < nr_workers; nr_forked++) {
		pid_t pid = fork();

		if (pid < 0) {
			SCX_ERR("Failed to fork worker %d", nr_forked);
			ret = SCX_TEST_FAIL;
			break;
		}
		if (pid == 0)
			worker_fn(nr_forked);
		pids[nr_forked] = pid;
	}

	/* on failure, kill the remaining workers but still reap them */
	for (i = 0; i < nr_forked; i++) {
		int status;

		if (ret != SCX_TEST_PASS)
			kill(pids[i], SIGKILL);

		if (waitpid(pids[i], &status, 0) != pids[i]) {
			SCX_ERR("Failed to wait for worker %d", i);
			ret = SCX_TEST_FAIL;
		} else if (ret == SCX_TEST_PASS && status != 0) {
			SCX_ERR("Worker %d exited with status %d", i, status);
			ret = SCX_TEST_FAIL;
		}
	}

	bpf_link__destroy(link);

	if (ret != SCX_TEST_PASS)
		return ret;

	printf("%s:\n", name);
	printf("  workers: %d\n", nr_workers);
	printf("  enqueues: %lu\n", (unsigned long)skel->bss->enqueue_cnt);
	printf("  dequeues: %lu (dispatch: %lu, property_change: %lu)\n",
	       (unsigned long)skel->bss->dequeue_cnt,
	       (unsigned long)skel->bss->dispatch_dequeue_cnt,
	       (unsigned long)skel->bss->change_dequeue_cnt);
	if (!use_move_to_local)
		printf("  remote SCX_DSQ_LOCAL_ON dispatches: %lu\n",
		       (unsigned long)skel->bss->remote_dispatch_cnt);
	printf("  ran on a CPU other than the enqueue CPU: %lu\n",
	       (unsigned long)skel->bss->remote_running_cnt);
	printf("  ran without ops.dequeue(): %lu\n",
	       (unsigned long)skel->bss->missed_dequeue_cnt);
	printf("  late SCX_DEQ_CORE_SCHED_EXEC dequeues: %lu\n",
	       (unsigned long)skel->bss->core_sched_exec_dequeue_cnt);

	if (skel->data->uei.kind != EXIT_KIND(SCX_EXIT_UNREG))
		SCX_ERR("Scheduler exited with kind=%lld: %s",
			(long long)skel->data->uei.kind, skel->data->uei.msg);
	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));

	/* the test is meaningless if no task was moved across CPUs */
	SCX_GT(skel->bss->remote_running_cnt, 0);
	SCX_EQ(skel->bss->enqueue_cnt, skel->bss->dequeue_cnt);

	return SCX_TEST_PASS;
}

static enum scx_test_status setup(void **ctx)
{
	struct dequeue_remote *skel;
	cpu_set_t cpus;

	SCX_FAIL_IF(sched_getaffinity(0, sizeof(cpus), &cpus),
		    "Failed to get CPU affinity");
	nr_cpus = CPU_COUNT(&cpus);
	if (nr_cpus < 2) {
		fprintf(stderr, "Skipping: requires at least 2 usable CPUs\n");
		return SCX_TEST_SKIP;
	}

	skel = SCX_OPS_OPEN(dequeue_remote_ops, dequeue_remote);
	SCX_OPS_LOAD(skel, dequeue_remote_ops, dequeue_remote, uei);

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct dequeue_remote *skel = ctx;
	enum scx_test_status status;

	status = run_scenario(skel, false,
			      "BPF queue -> SCX_DSQ_LOCAL_ON | cpu");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, true,
			      "user DSQ -> scx_bpf_dsq_move_to_local()");
	if (status != SCX_TEST_PASS)
		return status;

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct dequeue_remote *skel = ctx;

	dequeue_remote__destroy(skel);
}

struct scx_test dequeue_remote_test = {
	.name = "dequeue_remote",
	.description = "Verify ops.dequeue() on SCX-internal cross-CPU migrations",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};

REGISTER_SCX_TEST(&dequeue_remote_test)
