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

/*
 * Read a per-CPU dl_server param (runtime or period) from debugfs.
 * Returns the value in nanoseconds, or -1 on failure.
 */
static long read_server_param(const char *server, const char *param, int cpu)
{
	char path[128];
	long value = -1;
	FILE *fp;

	snprintf(path, sizeof(path),
		 "/sys/kernel/debug/sched/%s_server/cpu%d/%s",
		 server, cpu, param);
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	if (fscanf(fp, "%ld", &value) != 1)
		value = -1;
	fclose(fp);

	return value;
}

/*
 * Write a per-CPU dl_server param to debugfs. Returns 0 on success.
 */
static int write_server_param(const char *server, const char *param,
			      int cpu, long value)
{
	char path[128];
	FILE *fp;
	int ret = 0;

	snprintf(path, sizeof(path),
		 "/sys/kernel/debug/sched/%s_server/cpu%d/%s",
		 server, cpu, param);
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	if (fprintf(fp, "%ld", value) < 0)
		ret = -1;
	if (fclose(fp) != 0)
		ret = -1;

	return ret;
}

static int read_fair_runtime_all(int nr_cpus, long *runtimes)
{
	int i;

	for (i = 0; i < nr_cpus; i++) {
		runtimes[i] = read_server_param("fair", "runtime", i);
		if (runtimes[i] <= 0)
			return -1;
	}

	return 0;
}

static int write_fair_runtime_all(int nr_cpus, long value)
{
	int i;

	for (i = 0; i < nr_cpus; i++) {
		if (write_server_param("fair", "runtime", i, value) < 0) {
			SCX_ERR("Failed to write fair_server runtime on CPU %d", i);
			return -1;
		}
	}

	return 0;
}

/*
 * Restore per-CPU fair_server runtimes.
 */
static int restore_fair_runtime_all(int nr_cpus, const long *runtimes)
{
	int ret = 0;
	int i;

	for (i = 0; i < nr_cpus; i++) {
		if (write_server_param("fair", "runtime", i, runtimes[i]) < 0) {
			SCX_ERR("Failed to restore fair_server runtime on CPU %d", i);
			ret = -1;
		}
	}

	return ret;
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
	long doubled_bw[MAX_CPUS];
	long original_runtime[MAX_CPUS], doubled_runtime;
	enum scx_test_status ret;
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

	/*
	 * Validate auto-register/unregister of dl_server bandwidth reservations.
	 *
	 * Doubling fair_server's runtime doubles its bw contribution. With a
	 * full-mode BPF scheduler (minimal_ops), the kernel should detach
	 * fair_server and attach ext_server, dropping total_bw back to its
	 * pre-customization (default ext_server-only) value. On unload, the
	 * fair_server reservation should come back with its customized runtime
	 * preserved, so total_bw doubles again.
	 */
	if (read_fair_runtime_all(test_ctx->nr_cpus, original_runtime) < 0) {
		fprintf(stderr, "Skipping attach/detach validation: debugfs not accessible\n");
		return SCX_TEST_PASS;
	}
	doubled_runtime = original_runtime[0] * 2;

	fprintf(stderr,
		"Setting fair_server runtime to %ld ns on all CPUs (orig %ld)\n",
		doubled_runtime, original_runtime[0]);

	if (write_fair_runtime_all(test_ctx->nr_cpus, doubled_runtime) < 0) {
		ret = SCX_TEST_FAIL;
		goto restore;
	}

	if (fetch_verify_total_bw(doubled_bw, test_ctx->nr_cpus) < 0) {
		SCX_ERR("Failed to get stable values after doubling fair runtime");
		ret = SCX_TEST_FAIL;
		goto restore;
	}

	/*
	 * After doubling the runtime, fair_server's bw contribution must grow.
	 * We don't assert exactly 2x, because the kernel's to_ratio() truncates
	 * the value, so 2 * to_ratio(period, runtime) and
	 * to_ratio(period, 2 * runtime) can differ.
	 */
	for (i = 0; i < test_ctx->nr_cpus; i++) {
		if (doubled_bw[i] <= test_ctx->baseline_bw[i]) {
			SCX_ERR("CPU%d: fair did not increase total_bw (baseline=%ld, doubled=%ld)",
				i, test_ctx->baseline_bw[i], doubled_bw[i]);
			ret = SCX_TEST_FAIL;
			goto restore;
		}
	}

	link = bpf_map__attach_struct_ops(test_ctx->skel->maps.minimal_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler for detach test");
		ret = SCX_TEST_FAIL;
		goto restore;
	}

	if (fetch_verify_total_bw(loaded_bw, test_ctx->nr_cpus) < 0) {
		SCX_ERR("Failed to get stable values with BPF loaded (detach test)");
		bpf_link__destroy(link);
		ret = SCX_TEST_FAIL;
		goto restore;
	}

	/*
	 * In full mode the customized fair_server is detached and ext_server is
	 * attached at its default runtime, total_bw must match baseline.
	 */
	for (i = 0; i < test_ctx->nr_cpus; i++) {
		if (loaded_bw[i] != test_ctx->baseline_bw[i]) {
			SCX_ERR("CPU%d: expected bw %ld (fair detached, ext default), got %ld",
				i, test_ctx->baseline_bw[i], loaded_bw[i]);
			bpf_link__destroy(link);
			ret = SCX_TEST_FAIL;
			goto restore;
		}
	}

	bpf_link__destroy(link);

	if (fetch_verify_total_bw(unloaded_bw, test_ctx->nr_cpus) < 0) {
		SCX_ERR("Failed to get stable values after BPF unload (detach test)");
		ret = SCX_TEST_FAIL;
		goto restore;
	}

	/*
	 * After unload, fair_server is re-attached with its preserved 2x
	 * runtime, so total_bw should return to the doubled value.
	 */
	for (i = 0; i < test_ctx->nr_cpus; i++) {
		if (unloaded_bw[i] != doubled_bw[i]) {
			SCX_ERR("CPU%d: BPF unloaded: expected %ld (fair restored at 2x), got %ld",
				i, doubled_bw[i], unloaded_bw[i]);
			ret = SCX_TEST_FAIL;
			goto restore;
		}
	}

	fprintf(stderr,
		"dl_server attach/detach with customized fair runtime verified\n");
	ret = SCX_TEST_PASS;

restore:
	if (restore_fair_runtime_all(test_ctx->nr_cpus, original_runtime) < 0)
		SCX_ERR("Failed to fully restore per-CPU fair_server runtimes");

	return ret;
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
