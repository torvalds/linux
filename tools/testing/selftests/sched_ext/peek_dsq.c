// SPDX-License-Identifier: GPL-2.0
/*
 * Test for DSQ operations including create, destroy, and peek operations.
 *
 * Copyright (c) 2025 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025 Ryan Newton <ryan.newton@alum.mit.edu>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sched.h>
#include "peek_dsq.bpf.skel.h"
#include "scx_test.h"

#define NUM_WORKERS 4

static bool workload_running = true;
static pthread_t workload_threads[NUM_WORKERS];

/**
 * Background workload thread that sleeps and wakes rapidly to exercise
 * the scheduler's enqueue operations and ensure DSQ operations get tested.
 */
static void *workload_thread_fn(void *arg)
{
	while (workload_running) {
		/* Sleep for a very short time to trigger scheduler activity */
		usleep(1000); /* 1ms sleep */
		/* Yield to ensure we go through the scheduler */
		sched_yield();
	}
	return NULL;
}

static enum scx_test_status setup(void **ctx)
{
	struct peek_dsq *skel;

	skel = peek_dsq__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(peek_dsq__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static int print_observed_pids(struct bpf_map *map, int max_samples, const char *dsq_name)
{
	long count = 0;

	printf("Observed %s DSQ peek pids:\n", dsq_name);
	for (int i = 0; i < max_samples; i++) {
		long pid;
		int err;

		err = bpf_map_lookup_elem(bpf_map__fd(map), &i, &pid);
		if (err == 0) {
			if (pid == 0) {
				printf("  Sample %d: NULL peek\n", i);
			} else if (pid > 0) {
				printf("  Sample %d: pid %ld\n", i, pid);
				count++;
			}
		} else {
			printf("  Sample %d: error reading pid (err=%d)\n", i, err);
		}
	}
	printf("Observed ~%ld pids in the %s DSQ(s)\n", count, dsq_name);
	return count;
}

static enum scx_test_status run(void *ctx)
{
	struct peek_dsq *skel = ctx;
	bool failed = false;
	int seconds = 3;
	int err;

	/* Enable the scheduler to test DSQ operations */
	printf("Enabling scheduler to test DSQ insert operations...\n");

	struct bpf_link *link =
		bpf_map__attach_struct_ops(skel->maps.peek_dsq_ops);

	if (!link) {
		SCX_ERR("Failed to attach struct_ops");
		return SCX_TEST_FAIL;
	}

	printf("Starting %d background workload threads...\n", NUM_WORKERS);
	workload_running = true;
	for (int i = 0; i < NUM_WORKERS; i++) {
		err = pthread_create(&workload_threads[i], NULL, workload_thread_fn, NULL);
		if (err) {
			SCX_ERR("Failed to create workload thread %d: %s", i, strerror(err));
			/* Stop already created threads */
			workload_running = false;
			for (int j = 0; j < i; j++)
				pthread_join(workload_threads[j], NULL);
			bpf_link__destroy(link);
			return SCX_TEST_FAIL;
		}
	}

	printf("Waiting for enqueue events.\n");
	sleep(seconds);
	while (skel->data->enqueue_count <= 0) {
		printf(".");
		fflush(stdout);
		sleep(1);
		seconds++;
		if (seconds >= 30) {
			printf("\n\u2717 Timeout waiting for enqueue events\n");
			/* Stop workload threads and cleanup */
			workload_running = false;
			for (int i = 0; i < NUM_WORKERS; i++)
				pthread_join(workload_threads[i], NULL);
			bpf_link__destroy(link);
			return SCX_TEST_FAIL;
		}
	}

	workload_running = false;
	for (int i = 0; i < NUM_WORKERS; i++) {
		err = pthread_join(workload_threads[i], NULL);
		if (err) {
			SCX_ERR("Failed to join workload thread %d: %s", i, strerror(err));
			bpf_link__destroy(link);
			return SCX_TEST_FAIL;
		}
	}
	printf("Background workload threads stopped.\n");

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_NONE));

	/* Detach the scheduler */
	bpf_link__destroy(link);

	printf("Enqueue/dispatch count over %d seconds: %d / %d\n", seconds,
		skel->data->enqueue_count, skel->data->dispatch_count);
	printf("Debug: ksym_exists=%d\n",
	       skel->bss->debug_ksym_exists);

	/* Check DSQ insert result */
	printf("DSQ insert test done on cpu: %d\n", skel->data->insert_test_cpu);
	if (skel->data->insert_test_cpu != -1)
		printf("\u2713 DSQ insert succeeded !\n");
	else {
		printf("\u2717 DSQ insert failed or not attempted\n");
		failed = true;
	}

	/* Check DSQ peek results */
	printf("  DSQ peek result 1 (before insert): %d\n",
	       skel->data->dsq_peek_result1);
	if (skel->data->dsq_peek_result1 == 0)
		printf("\u2713 DSQ peek verification success: peek returned NULL!\n");
	else {
		printf("\u2717 DSQ peek verification failed\n");
		failed = true;
	}

	printf("  DSQ peek result 2 (after insert): %ld\n",
	       skel->data->dsq_peek_result2);
	printf("  DSQ peek result 2, expected: %ld\n",
	       skel->data->dsq_peek_result2_expected);
	if (skel->data->dsq_peek_result2 ==
	    skel->data->dsq_peek_result2_expected)
		printf("\u2713 DSQ peek verification success: peek returned the inserted task!\n");
	else {
		printf("\u2717 DSQ peek verification failed\n");
		failed = true;
	}

	printf("  Inserted test task -> pid: %ld\n", skel->data->dsq_inserted_pid);
	printf("  DSQ peek result 2 -> pid: %ld\n", skel->data->dsq_peek_result2_pid);

	int pid_count;

	pid_count = print_observed_pids(skel->maps.peek_results,
					skel->data->max_samples, "DSQ pool");
	printf("Total non-null peek observations: %ld out of %ld\n",
	       skel->data->successful_peeks, skel->data->total_peek_attempts);

	if (skel->bss->debug_ksym_exists && pid_count == 0) {
		printf("\u2717 DSQ pool test failed: no successful peeks in native mode\n");
		failed = true;
	}
	if (skel->bss->debug_ksym_exists && pid_count > 0)
		printf("\u2713 DSQ pool test success: observed successful peeks in native mode\n");

	if (failed)
		return SCX_TEST_FAIL;
	else
		return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct peek_dsq *skel = ctx;

	if (workload_running) {
		workload_running = false;
		for (int i = 0; i < NUM_WORKERS; i++)
			pthread_join(workload_threads[i], NULL);
	}

	peek_dsq__destroy(skel);
}

struct scx_test peek_dsq = {
	.name = "peek_dsq",
	.description =
		"Test DSQ create/destroy operations and future peek functionality",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&peek_dsq)
