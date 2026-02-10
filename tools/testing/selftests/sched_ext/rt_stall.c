// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 NVIDIA Corporation.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <linux/sched.h>
#include <signal.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include <unistd.h>
#include "rt_stall.bpf.skel.h"
#include "scx_test.h"
#include "../kselftest.h"

#define CORE_ID		0	/* CPU to pin tasks to */
#define RUN_TIME        5	/* How long to run the test in seconds */

/* Simple busy-wait function for test tasks */
static void process_func(void)
{
	while (1) {
		/* Busy wait */
		for (volatile unsigned long i = 0; i < 10000000UL; i++)
			;
	}
}

/* Set CPU affinity to a specific core */
static void set_affinity(int cpu)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
		perror("sched_setaffinity");
		exit(EXIT_FAILURE);
	}
}

/* Set task scheduling policy and priority */
static void set_sched(int policy, int priority)
{
	struct sched_param param;

	param.sched_priority = priority;
	if (sched_setscheduler(0, policy, &param) != 0) {
		perror("sched_setscheduler");
		exit(EXIT_FAILURE);
	}
}

/* Get process runtime from /proc/<pid>/stat */
static float get_process_runtime(int pid)
{
	char path[256];
	FILE *file;
	long utime, stime;
	int fields;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	file = fopen(path, "r");
	if (file == NULL) {
		perror("Failed to open stat file");
		return -1;
	}

	/* Skip the first 13 fields and read the 14th and 15th */
	fields = fscanf(file,
			"%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
			&utime, &stime);
	fclose(file);

	if (fields != 2) {
		fprintf(stderr, "Failed to read stat file\n");
		return -1;
	}

	/* Calculate the total time spent in the process */
	long total_time = utime + stime;
	long ticks_per_second = sysconf(_SC_CLK_TCK);
	float runtime_seconds = total_time * 1.0 / ticks_per_second;

	return runtime_seconds;
}

static enum scx_test_status setup(void **ctx)
{
	struct rt_stall *skel;

	skel = rt_stall__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(rt_stall__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static bool sched_stress_test(bool is_ext)
{
	/*
	 * We're expecting the EXT task to get around 5% of CPU time when
	 * competing with the RT task (small 1% fluctuations are expected).
	 *
	 * However, the EXT task should get at least 4% of the CPU to prove
	 * that the EXT deadline server is working correctly. A percentage
	 * less than 4% indicates a bug where RT tasks can potentially
	 * stall SCHED_EXT tasks, causing the test to fail.
	 */
	const float expected_min_ratio = 0.04; /* 4% */
	const char *class_str = is_ext ? "EXT" : "FAIR";

	float ext_runtime, rt_runtime, actual_ratio;
	int ext_pid, rt_pid;

	ksft_print_header();
	ksft_set_plan(1);

	/* Create and set up a EXT task */
	ext_pid = fork();
	if (ext_pid == 0) {
		set_affinity(CORE_ID);
		process_func();
		exit(0);
	} else if (ext_pid < 0) {
		perror("fork task");
		ksft_exit_fail();
	}

	/* Create an RT task */
	rt_pid = fork();
	if (rt_pid == 0) {
		set_affinity(CORE_ID);
		set_sched(SCHED_FIFO, 50);
		process_func();
		exit(0);
	} else if (rt_pid < 0) {
		perror("fork for RT task");
		ksft_exit_fail();
	}

	/* Let the processes run for the specified time */
	sleep(RUN_TIME);

	/* Get runtime for the EXT task */
	ext_runtime = get_process_runtime(ext_pid);
	if (ext_runtime == -1)
		ksft_exit_fail_msg("Error getting runtime for %s task (PID %d)\n",
				   class_str, ext_pid);
	ksft_print_msg("Runtime of %s task (PID %d) is %f seconds\n",
		       class_str, ext_pid, ext_runtime);

	/* Get runtime for the RT task */
	rt_runtime = get_process_runtime(rt_pid);
	if (rt_runtime == -1)
		ksft_exit_fail_msg("Error getting runtime for RT task (PID %d)\n", rt_pid);
	ksft_print_msg("Runtime of RT task (PID %d) is %f seconds\n", rt_pid, rt_runtime);

	/* Kill the processes */
	kill(ext_pid, SIGKILL);
	kill(rt_pid, SIGKILL);
	waitpid(ext_pid, NULL, 0);
	waitpid(rt_pid, NULL, 0);

	/* Verify that the scx task got enough runtime */
	actual_ratio = ext_runtime / (ext_runtime + rt_runtime);
	ksft_print_msg("%s task got %.2f%% of total runtime\n",
		       class_str, actual_ratio * 100);

	if (actual_ratio >= expected_min_ratio) {
		ksft_test_result_pass("PASS: %s task got more than %.2f%% of runtime\n",
				      class_str, expected_min_ratio * 100);
		return true;
	}
	ksft_test_result_fail("FAIL: %s task got less than %.2f%% of runtime\n",
			      class_str, expected_min_ratio * 100);
	return false;
}

static enum scx_test_status run(void *ctx)
{
	struct rt_stall *skel = ctx;
	struct bpf_link *link = NULL;
	bool res;
	int i;

	/*
	 * Test if the dl_server is working both with and without the
	 * sched_ext scheduler attached.
	 *
	 * This ensures all the scenarios are covered:
	 *   - fair_server stop -> ext_server start
	 *   - ext_server stop -> fair_server stop
	 */
	for (i = 0; i < 4; i++) {
		bool is_ext = i % 2;

		if (is_ext) {
			memset(&skel->data->uei, 0, sizeof(skel->data->uei));
			link = bpf_map__attach_struct_ops(skel->maps.rt_stall_ops);
			SCX_FAIL_IF(!link, "Failed to attach scheduler");
		}
		res = sched_stress_test(is_ext);
		if (is_ext) {
			SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_NONE));
			bpf_link__destroy(link);
		}

		if (!res)
			ksft_exit_fail();
	}

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct rt_stall *skel = ctx;

	rt_stall__destroy(skel);
}

struct scx_test rt_stall = {
	.name = "rt_stall",
	.description = "Verify that RT tasks cannot stall SCHED_EXT tasks",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&rt_stall)
