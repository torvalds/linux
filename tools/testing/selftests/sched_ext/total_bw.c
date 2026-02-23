// SPDX-License-Identifier: GPL-2.0
/*
 * Test to verify that total_bw value remains consistent across all CPUs
 * in different BPF program states.
 *
 * Copyright (C) 2025 NVIDIA Corporation.
 */
#include <bpf/bpf.h>
#include <errno.h>
#include <pthread.h>
#include <scx/common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "minimal.bpf.skel.h"
#include "scx_test.h"

#define MAX_CPUS 512
#define STRESS_DURATION_SEC 5

struct total_bw_ctx {
	struct minimal *skel;
	long baseline_bw[MAX_CPUS];
	int nr_cpus;
};

static void *cpu_stress_thread(void *arg)
{
	volatile int i;
	time_t end_time = time(NULL) + STRESS_DURATION_SEC;

	while (time(NULL) < end_time)
		for (i = 0; i < 1000000; i++)
			;

	return NULL;
}

/*
 * The first enqueue on a CPU causes the DL server to start, for that
 * reason run stressor threads in the hopes it schedules on all CPUs.
 */
static int run_cpu_stress(int nr_cpus)
{
	pthread_t *threads;
	int i, ret = 0;

	threads = calloc(nr_cpus, sizeof(pthread_t));
	if (!threads)
		return -ENOMEM;

	/* Create threads to run on each CPU */
	for (i = 0; i < nr_cpus; i++) {
		if (pthread_create(&threads[i], NULL, cpu_stress_thread, NULL)) {
			ret = -errno;
			fprintf(stderr, "Failed to create thread %d: %s\n", i, strerror(-ret));
			break;
		}
	}

	/* Wait for all threads to complete */
	for (i = 0; i < nr_cpus; i++) {
		if (threads[i])
			pthread_join(threads[i], NULL);
	}

	free(threads);
	return ret;
}

static int read_total_bw_values(long *bw_values, int max_cpus)
{
	FILE *fp;
	char line[256];
	int cpu_count = 0;

	fp = fopen("/sys/kernel/debug/sched/debug", "r");
	if (!fp) {
		SCX_ERR("Failed to open debug file");
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *bw_str = strstr(line, "total_bw");

		if (bw_str) {
			bw_str = strchr(bw_str, ':');
			if (bw_str) {
				/* Only store up to max_cpus values */
				if (cpu_count < max_cpus)
					bw_values[cpu_count] = atol(bw_str + 1);
				cpu_count++;
			}
		}
	}

	fclose(fp);
	return cpu_count;
}

static bool verify_total_bw_consistency(long *bw_values, int count)
{
	int i;
	long first_value;

	if (count <= 0)
		return false;

	first_value = bw_values[0];

	for (i = 1; i < count; i++) {
		if (bw_values[i] != first_value) {
			SCX_ERR("Inconsistent total_bw: CPU0=%ld, CPU%d=%ld",
				first_value, i, bw_values[i]);
			return false;
		}
	}

	return true;
}

static int fetch_verify_total_bw(long *bw_values, int nr_cpus)
{
	int attempts = 0;
	int max_attempts = 10;
	int count;

	/*
	 * The first enqueue on a CPU causes the DL server to start, for that
	 * reason run stressor threads in the hopes it schedules on all CPUs.
	 */
	if (run_cpu_stress(nr_cpus) < 0) {
		SCX_ERR("Failed to run CPU stress");
		return -1;
	}

	/* Try multiple times to get stable values */
	while (attempts < max_attempts) {
		count = read_total_bw_values(bw_values, nr_cpus);
		fprintf(stderr, "Read %d total_bw values (testing %d CPUs)\n", count, nr_cpus);
		/* If system has more CPUs than we're testing, that's OK */
		if (count < nr_cpus) {
			SCX_ERR("Expected at least %d CPUs, got %d", nr_cpus, count);
			attempts++;
			sleep(1);
			continue;
		}

		/* Only verify the CPUs we're testing */
		if (verify_total_bw_consistency(bw_values, nr_cpus)) {
			fprintf(stderr, "Values are consistent: %ld\n", bw_values[0]);
			return 0;
		}

		attempts++;
		sleep(1);
	}

	return -1;
}

static enum scx_test_status setup(void **ctx)
{
	struct total_bw_ctx *test_ctx;

	if (access("/sys/kernel/debug/sched/debug", R_OK) != 0) {
		fprintf(stderr, "Skipping test: debugfs sched/debug not accessible\n");
		return SCX_TEST_SKIP;
	}

	test_ctx = calloc(1, sizeof(*test_ctx));
	if (!test_ctx)
		return SCX_TEST_FAIL;

	test_ctx->nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (test_ctx->nr_cpus <= 0) {
		free(test_ctx);
		return SCX_TEST_FAIL;
	}

	/* If system has more CPUs than MAX_CPUS, just test the first MAX_CPUS */
	if (test_ctx->nr_cpus > MAX_CPUS)
		test_ctx->nr_cpus = MAX_CPUS;

	/* Test scenario 1: BPF program not loaded */
	/* Read and verify baseline total_bw before loading BPF program */
	fprintf(stderr, "BPF prog initially not loaded, reading total_bw values\n");
	if (fetch_verify_total_bw(test_ctx->baseline_bw, test_ctx->nr_cpus) < 0) {
		SCX_ERR("Failed to get stable baseline values");
		free(test_ctx);
		return SCX_TEST_FAIL;
	}

	/* Load the BPF skeleton */
	test_ctx->skel = minimal__open();
	if (!test_ctx->skel) {
		free(test_ctx);
		return SCX_TEST_FAIL;
	}

	SCX_ENUM_INIT(test_ctx->skel);
	if (minimal__load(test_ctx->skel)) {
		minimal__destroy(test_ctx->skel);
		free(test_ctx);
		return SCX_TEST_FAIL;
	}

	*ctx = test_ctx;
	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct total_bw_ctx *test_ctx = ctx;
	struct bpf_link *link;
	long loaded_bw[MAX_CPUS];
	long unloaded_bw[MAX_CPUS];
	int i;

	/* Test scenario 2: BPF program loaded */
	link = bpf_map__attach_struct_ops(test_ctx->skel->maps.minimal_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		return SCX_TEST_FAIL;
	}

	fprintf(stderr, "BPF program loaded, reading total_bw values\n");
	if (fetch_verify_total_bw(loaded_bw, test_ctx->nr_cpus) < 0) {
		SCX_ERR("Failed to get stable values with BPF loaded");
		bpf_link__destroy(link);
		return SCX_TEST_FAIL;
	}
	bpf_link__destroy(link);

	/* Test scenario 3: BPF program unloaded */
	fprintf(stderr, "BPF program unloaded, reading total_bw values\n");
	if (fetch_verify_total_bw(unloaded_bw, test_ctx->nr_cpus) < 0) {
		SCX_ERR("Failed to get stable values after BPF unload");
		return SCX_TEST_FAIL;
	}

	/* Verify all three scenarios have the same total_bw values */
	for (i = 0; i < test_ctx->nr_cpus; i++) {
		if (test_ctx->baseline_bw[i] != loaded_bw[i]) {
			SCX_ERR("CPU%d: baseline_bw=%ld != loaded_bw=%ld",
				i, test_ctx->baseline_bw[i], loaded_bw[i]);
			return SCX_TEST_FAIL;
		}

		if (test_ctx->baseline_bw[i] != unloaded_bw[i]) {
			SCX_ERR("CPU%d: baseline_bw=%ld != unloaded_bw=%ld",
				i, test_ctx->baseline_bw[i], unloaded_bw[i]);
			return SCX_TEST_FAIL;
		}
	}

	fprintf(stderr, "All total_bw values are consistent across all scenarios\n");
	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct total_bw_ctx *test_ctx = ctx;

	if (test_ctx) {
		if (test_ctx->skel)
			minimal__destroy(test_ctx->skel);
		free(test_ctx);
	}
}

struct scx_test total_bw = {
	.name = "total_bw",
	.description = "Verify total_bw consistency across BPF program states",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&total_bw)
