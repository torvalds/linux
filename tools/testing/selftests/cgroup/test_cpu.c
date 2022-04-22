// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <linux/limits.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include "../kselftest.h"
#include "cgroup_util.h"

struct cpu_hog_func_param {
	int nprocs;
	struct timespec ts;
};

/*
 * This test creates two nested cgroups with and without enabling
 * the cpu controller.
 */
static int test_cpucg_subtree_control(const char *root)
{
	char *parent = NULL, *child = NULL, *parent2 = NULL, *child2 = NULL;
	int ret = KSFT_FAIL;

	// Create two nested cgroups with the cpu controller enabled.
	parent = cg_name(root, "cpucg_test_0");
	if (!parent)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	child = cg_name(parent, "cpucg_test_child");
	if (!child)
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_read_strstr(child, "cgroup.controllers", "cpu"))
		goto cleanup;

	// Create two nested cgroups without enabling the cpu controller.
	parent2 = cg_name(root, "cpucg_test_1");
	if (!parent2)
		goto cleanup;

	if (cg_create(parent2))
		goto cleanup;

	child2 = cg_name(parent2, "cpucg_test_child");
	if (!child2)
		goto cleanup;

	if (cg_create(child2))
		goto cleanup;

	if (!cg_read_strstr(child2, "cgroup.controllers", "cpu"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_destroy(child);
	free(child);
	cg_destroy(child2);
	free(child2);
	cg_destroy(parent);
	free(parent);
	cg_destroy(parent2);
	free(parent2);

	return ret;
}

static void *hog_cpu_thread_func(void *arg)
{
	while (1)
		;

	return NULL;
}

static struct timespec
timespec_sub(const struct timespec *lhs, const struct timespec *rhs)
{
	struct timespec zero = {
		.tv_sec = 0,
		.tv_nsec = 0,
	};
	struct timespec ret;

	if (lhs->tv_sec < rhs->tv_sec)
		return zero;

	ret.tv_sec = lhs->tv_sec - rhs->tv_sec;

	if (lhs->tv_nsec < rhs->tv_nsec) {
		if (ret.tv_sec == 0)
			return zero;

		ret.tv_sec--;
		ret.tv_nsec = NSEC_PER_SEC - rhs->tv_nsec + lhs->tv_nsec;
	} else
		ret.tv_nsec = lhs->tv_nsec - rhs->tv_nsec;

	return ret;
}

static int hog_cpus_timed(const char *cgroup, void *arg)
{
	const struct cpu_hog_func_param *param =
		(struct cpu_hog_func_param *)arg;
	struct timespec ts_run = param->ts;
	struct timespec ts_remaining = ts_run;
	int i, ret;

	for (i = 0; i < param->nprocs; i++) {
		pthread_t tid;

		ret = pthread_create(&tid, NULL, &hog_cpu_thread_func, NULL);
		if (ret != 0)
			return ret;
	}

	while (ts_remaining.tv_sec > 0 || ts_remaining.tv_nsec > 0) {
		struct timespec ts_total;

		ret = nanosleep(&ts_remaining, NULL);
		if (ret && errno != EINTR)
			return ret;

		ret = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_total);
		if (ret != 0)
			return ret;

		ts_remaining = timespec_sub(&ts_run, &ts_total);
	}

	return 0;
}

/*
 * Creates a cpu cgroup, burns a CPU for a few quanta, and verifies that
 * cpu.stat shows the expected output.
 */
static int test_cpucg_stats(const char *root)
{
	int ret = KSFT_FAIL;
	long usage_usec, user_usec, system_usec;
	long usage_seconds = 2;
	long expected_usage_usec = usage_seconds * USEC_PER_SEC;
	char *cpucg;

	cpucg = cg_name(root, "cpucg_test");
	if (!cpucg)
		goto cleanup;

	if (cg_create(cpucg))
		goto cleanup;

	usage_usec = cg_read_key_long(cpucg, "cpu.stat", "usage_usec");
	user_usec = cg_read_key_long(cpucg, "cpu.stat", "user_usec");
	system_usec = cg_read_key_long(cpucg, "cpu.stat", "system_usec");
	if (usage_usec != 0 || user_usec != 0 || system_usec != 0)
		goto cleanup;

	struct cpu_hog_func_param param = {
		.nprocs = 1,
		.ts = {
			.tv_sec = usage_seconds,
			.tv_nsec = 0,
		},
	};
	if (cg_run(cpucg, hog_cpus_timed, (void *)&param))
		goto cleanup;

	usage_usec = cg_read_key_long(cpucg, "cpu.stat", "usage_usec");
	user_usec = cg_read_key_long(cpucg, "cpu.stat", "user_usec");
	if (user_usec <= 0)
		goto cleanup;

	if (!values_close(usage_usec, expected_usage_usec, 1))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_destroy(cpucg);
	free(cpucg);

	return ret;
}

#define T(x) { x, #x }
struct cpucg_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cpucg_subtree_control),
	T(test_cpucg_stats),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "cpu"))
		if (cg_write(root, "cgroup.subtree_control", "+cpu"))
			ksft_exit_skip("Failed to set cpu controller\n");

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		switch (tests[i].fn(root)) {
		case KSFT_PASS:
			ksft_test_result_pass("%s\n", tests[i].name);
			break;
		case KSFT_SKIP:
			ksft_test_result_skip("%s\n", tests[i].name);
			break;
		default:
			ret = EXIT_FAILURE;
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	return ret;
}
