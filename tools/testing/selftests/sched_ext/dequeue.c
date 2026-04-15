// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 NVIDIA Corporation.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <sched.h>
#include <pthread.h>
#include "scx_test.h"
#include "dequeue.bpf.skel.h"

#define NUM_WORKERS 8
#define AFFINITY_HAMMER_MS 500

/*
 * Worker function that creates enqueue/dequeue events via CPU work and
 * sleep.
 */
static void worker_fn(int id)
{
	int i;
	volatile int sum = 0;

	for (i = 0; i < 1000; i++) {
		volatile int j;

		/* Do some work to trigger scheduling events */
		for (j = 0; j < 10000; j++)
			sum += j;

		/* Sleep to trigger dequeue */
		usleep(1000 + (id * 100));
	}

	exit(0);
}

/*
 * This thread changes workers' affinity from outside so that some changes
 * hit tasks while they are still in the scheduler's queue and trigger
 * property-change dequeues.
 */
static void *affinity_hammer_fn(void *arg)
{
	pid_t *pids = arg;
	cpu_set_t cpuset;
	int i = 0, n = NUM_WORKERS;
	struct timespec start, now;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		int w = i % n;
		int cpu = (i / n) % 4;

		CPU_ZERO(&cpuset);
		CPU_SET(cpu, &cpuset);
		sched_setaffinity(pids[w], sizeof(cpuset), &cpuset);
		i++;

		/* Check elapsed time every 256 iterations to limit gettime cost */
		if ((i & 255) == 0) {
			long long elapsed_ms;

			clock_gettime(CLOCK_MONOTONIC, &now);
			elapsed_ms = (now.tv_sec - start.tv_sec) * 1000LL +
				     (now.tv_nsec - start.tv_nsec) / 1000000;
			if (elapsed_ms >= AFFINITY_HAMMER_MS)
				break;
		}
	}
	return NULL;
}

static enum scx_test_status run_scenario(struct dequeue *skel, u32 scenario,
					 const char *scenario_name)
{
	struct bpf_link *link;
	pid_t pids[NUM_WORKERS];
	pthread_t hammer;

	int i, status;
	u64 enq_start, deq_start,
	    dispatch_deq_start, change_deq_start, bpf_queue_full_start;
	u64 enq_delta, deq_delta,
	    dispatch_deq_delta, change_deq_delta, bpf_queue_full_delta;

	/* Set the test scenario */
	skel->bss->test_scenario = scenario;

	/* Record starting counts */
	enq_start = skel->bss->enqueue_cnt;
	deq_start = skel->bss->dequeue_cnt;
	dispatch_deq_start = skel->bss->dispatch_dequeue_cnt;
	change_deq_start = skel->bss->change_dequeue_cnt;
	bpf_queue_full_start = skel->bss->bpf_queue_full;

	link = bpf_map__attach_struct_ops(skel->maps.dequeue_ops);
	SCX_FAIL_IF(!link, "Failed to attach struct_ops for scenario %s", scenario_name);

	/* Fork worker processes to generate enqueue/dequeue events */
	for (i = 0; i < NUM_WORKERS; i++) {
		pids[i] = fork();
		SCX_FAIL_IF(pids[i] < 0, "Failed to fork worker %d", i);

		if (pids[i] == 0) {
			worker_fn(i);
			/* Should not reach here */
			exit(1);
		}
	}

	/*
	 * Run an "affinity hammer" so that some property changes hit tasks
	 * while they are still in BPF custody (e.g., in user DSQ or BPF
	 * queue), triggering SCX_DEQ_SCHED_CHANGE dequeues.
	 */
	SCX_FAIL_IF(pthread_create(&hammer, NULL, affinity_hammer_fn, pids) != 0,
		    "Failed to create affinity hammer thread");
	pthread_join(hammer, NULL);

	/* Wait for all workers to complete */
	for (i = 0; i < NUM_WORKERS; i++) {
		SCX_FAIL_IF(waitpid(pids[i], &status, 0) != pids[i],
			    "Failed to wait for worker %d", i);
		SCX_FAIL_IF(status != 0, "Worker %d exited with status %d", i, status);
	}

	bpf_link__destroy(link);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));

	/* Calculate deltas */
	enq_delta = skel->bss->enqueue_cnt - enq_start;
	deq_delta = skel->bss->dequeue_cnt - deq_start;
	dispatch_deq_delta = skel->bss->dispatch_dequeue_cnt - dispatch_deq_start;
	change_deq_delta = skel->bss->change_dequeue_cnt - change_deq_start;
	bpf_queue_full_delta = skel->bss->bpf_queue_full - bpf_queue_full_start;

	printf("%s:\n", scenario_name);
	printf("  enqueues: %lu\n", (unsigned long)enq_delta);
	printf("  dequeues: %lu (dispatch: %lu, property_change: %lu)\n",
	       (unsigned long)deq_delta,
	       (unsigned long)dispatch_deq_delta,
	       (unsigned long)change_deq_delta);
	printf("  BPF queue full: %lu\n", (unsigned long)bpf_queue_full_delta);

	/*
	 * Validate enqueue/dequeue lifecycle tracking.
	 *
	 * For scenarios 0, 1, 3, 4 (local and global DSQs from
	 * ops.select_cpu() and ops.enqueue()), both enqueues and dequeues
	 * should be 0 because tasks bypass the BPF scheduler entirely:
	 * tasks never enter BPF scheduler's custody.
	 *
	 * For scenarios 2, 5, 6 (user DSQ or BPF internal queue) we expect
	 * both enqueues and dequeues.
	 *
	 * The BPF code does strict state machine validation with
	 * scx_bpf_error() to ensure the workflow semantics are correct.
	 *
	 * If we reach this point without errors, the semantics are
	 * validated correctly.
	 */
	if (scenario == 0 || scenario == 1 ||
	    scenario == 3 || scenario == 4) {
		/* Tasks bypass BPF scheduler completely */
		SCX_EQ(enq_delta, 0);
		SCX_EQ(deq_delta, 0);
		SCX_EQ(dispatch_deq_delta, 0);
		SCX_EQ(change_deq_delta, 0);
	} else {
		/*
		 * User DSQ from ops.enqueue() or ops.select_cpu(): tasks
		 * enter BPF scheduler's custody.
		 *
		 * Also validate 1:1 enqueue/dequeue pairing.
		 */
		SCX_GT(enq_delta, 0);
		SCX_GT(deq_delta, 0);
		SCX_EQ(enq_delta, deq_delta);
	}

	return SCX_TEST_PASS;
}

static enum scx_test_status setup(void **ctx)
{
	struct dequeue *skel;

	skel = dequeue__open();
	SCX_FAIL_IF(!skel, "Failed to open skel");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(dequeue__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct dequeue *skel = ctx;
	enum scx_test_status status;

	status = run_scenario(skel, 0, "Scenario 0: Local DSQ from ops.select_cpu()");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, 1, "Scenario 1: Global DSQ from ops.select_cpu()");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, 2, "Scenario 2: User DSQ from ops.select_cpu()");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, 3, "Scenario 3: Local DSQ from ops.enqueue()");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, 4, "Scenario 4: Global DSQ from ops.enqueue()");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, 5, "Scenario 5: User DSQ from ops.enqueue()");
	if (status != SCX_TEST_PASS)
		return status;

	status = run_scenario(skel, 6, "Scenario 6: BPF queue from ops.enqueue()");
	if (status != SCX_TEST_PASS)
		return status;

	printf("\n=== Summary ===\n");
	printf("Total enqueues: %lu\n", (unsigned long)skel->bss->enqueue_cnt);
	printf("Total dequeues: %lu\n", (unsigned long)skel->bss->dequeue_cnt);
	printf("  Dispatch dequeues: %lu (no flag, normal workflow)\n",
	       (unsigned long)skel->bss->dispatch_dequeue_cnt);
	printf("  Property change dequeues: %lu (SCX_DEQ_SCHED_CHANGE flag)\n",
	       (unsigned long)skel->bss->change_dequeue_cnt);
	printf("  BPF queue full: %lu\n",
	       (unsigned long)skel->bss->bpf_queue_full);
	printf("\nAll scenarios passed - no state machine violations detected\n");
	printf("-> Validated: Local DSQ dispatch bypasses BPF scheduler\n");
	printf("-> Validated: Global DSQ dispatch bypasses BPF scheduler\n");
	printf("-> Validated: User DSQ dispatch triggers ops.dequeue() callbacks\n");
	printf("-> Validated: Dispatch dequeues have no flags (normal workflow)\n");
	printf("-> Validated: Property change dequeues have SCX_DEQ_SCHED_CHANGE flag\n");
	printf("-> Validated: No duplicate enqueues or invalid state transitions\n");

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct dequeue *skel = ctx;

	dequeue__destroy(skel);
}

struct scx_test dequeue_test = {
	.name = "dequeue",
	.description = "Verify ops.dequeue() semantics",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};

REGISTER_SCX_TEST(&dequeue_test)
