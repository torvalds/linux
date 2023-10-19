// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <linux/limits.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include "../kselftest.h"
#include "cgroup_util.h"

enum hog_clock_type {
	// Count elapsed time using the CLOCK_PROCESS_CPUTIME_ID clock.
	CPU_HOG_CLOCK_PROCESS,
	// Count elapsed time using system wallclock time.
	CPU_HOG_CLOCK_WALL,
};

struct cpu_hogger {
	char *cgroup;
	pid_t pid;
	long usage;
};

struct cpu_hog_func_param {
	int nprocs;
	struct timespec ts;
	enum hog_clock_type clock_type;
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
	struct timespec ts_start;
	int i, ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts_start);
	if (ret != 0)
		return ret;

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

		if (param->clock_type == CPU_HOG_CLOCK_PROCESS) {
			ret = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_total);
			if (ret != 0)
				return ret;
		} else {
			struct timespec ts_current;

			ret = clock_gettime(CLOCK_MONOTONIC, &ts_current);
			if (ret != 0)
				return ret;

			ts_total = timespec_sub(&ts_current, &ts_start);
		}

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
		.clock_type = CPU_HOG_CLOCK_PROCESS,
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

static int
run_cpucg_weight_test(
		const char *root,
		pid_t (*spawn_child)(const struct cpu_hogger *child),
		int (*validate)(const struct cpu_hogger *children, int num_children))
{
	int ret = KSFT_FAIL, i;
	char *parent = NULL;
	struct cpu_hogger children[3] = {NULL};

	parent = cg_name(root, "cpucg_test_0");
	if (!parent)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(children); i++) {
		children[i].cgroup = cg_name_indexed(parent, "cpucg_child", i);
		if (!children[i].cgroup)
			goto cleanup;

		if (cg_create(children[i].cgroup))
			goto cleanup;

		if (cg_write_numeric(children[i].cgroup, "cpu.weight",
					50 * (i + 1)))
			goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(children); i++) {
		pid_t pid = spawn_child(&children[i]);
		if (pid <= 0)
			goto cleanup;
		children[i].pid = pid;
	}

	for (i = 0; i < ARRAY_SIZE(children); i++) {
		int retcode;

		waitpid(children[i].pid, &retcode, 0);
		if (!WIFEXITED(retcode))
			goto cleanup;
		if (WEXITSTATUS(retcode))
			goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(children); i++)
		children[i].usage = cg_read_key_long(children[i].cgroup,
				"cpu.stat", "usage_usec");

	if (validate(children, ARRAY_SIZE(children)))
		goto cleanup;

	ret = KSFT_PASS;
cleanup:
	for (i = 0; i < ARRAY_SIZE(children); i++) {
		cg_destroy(children[i].cgroup);
		free(children[i].cgroup);
	}
	cg_destroy(parent);
	free(parent);

	return ret;
}

static pid_t weight_hog_ncpus(const struct cpu_hogger *child, int ncpus)
{
	long usage_seconds = 10;
	struct cpu_hog_func_param param = {
		.nprocs = ncpus,
		.ts = {
			.tv_sec = usage_seconds,
			.tv_nsec = 0,
		},
		.clock_type = CPU_HOG_CLOCK_WALL,
	};
	return cg_run_nowait(child->cgroup, hog_cpus_timed, (void *)&param);
}

static pid_t weight_hog_all_cpus(const struct cpu_hogger *child)
{
	return weight_hog_ncpus(child, get_nprocs());
}

static int
overprovision_validate(const struct cpu_hogger *children, int num_children)
{
	int ret = KSFT_FAIL, i;

	for (i = 0; i < num_children - 1; i++) {
		long delta;

		if (children[i + 1].usage <= children[i].usage)
			goto cleanup;

		delta = children[i + 1].usage - children[i].usage;
		if (!values_close(delta, children[0].usage, 35))
			goto cleanup;
	}

	ret = KSFT_PASS;
cleanup:
	return ret;
}

/*
 * First, this test creates the following hierarchy:
 * A
 * A/B     cpu.weight = 50
 * A/C     cpu.weight = 100
 * A/D     cpu.weight = 150
 *
 * A separate process is then created for each child cgroup which spawns as
 * many threads as there are cores, and hogs each CPU as much as possible
 * for some time interval.
 *
 * Once all of the children have exited, we verify that each child cgroup
 * was given proportional runtime as informed by their cpu.weight.
 */
static int test_cpucg_weight_overprovisioned(const char *root)
{
	return run_cpucg_weight_test(root, weight_hog_all_cpus,
			overprovision_validate);
}

static pid_t weight_hog_one_cpu(const struct cpu_hogger *child)
{
	return weight_hog_ncpus(child, 1);
}

static int
underprovision_validate(const struct cpu_hogger *children, int num_children)
{
	int ret = KSFT_FAIL, i;

	for (i = 0; i < num_children - 1; i++) {
		if (!values_close(children[i + 1].usage, children[0].usage, 15))
			goto cleanup;
	}

	ret = KSFT_PASS;
cleanup:
	return ret;
}

/*
 * First, this test creates the following hierarchy:
 * A
 * A/B     cpu.weight = 50
 * A/C     cpu.weight = 100
 * A/D     cpu.weight = 150
 *
 * A separate process is then created for each child cgroup which spawns a
 * single thread that hogs a CPU. The testcase is only run on systems that
 * have at least one core per-thread in the child processes.
 *
 * Once all of the children have exited, we verify that each child cgroup
 * had roughly the same runtime despite having different cpu.weight.
 */
static int test_cpucg_weight_underprovisioned(const char *root)
{
	// Only run the test if there are enough cores to avoid overprovisioning
	// the system.
	if (get_nprocs() < 4)
		return KSFT_SKIP;

	return run_cpucg_weight_test(root, weight_hog_one_cpu,
			underprovision_validate);
}

static int
run_cpucg_nested_weight_test(const char *root, bool overprovisioned)
{
	int ret = KSFT_FAIL, i;
	char *parent = NULL, *child = NULL;
	struct cpu_hogger leaf[3] = {NULL};
	long nested_leaf_usage, child_usage;
	int nprocs = get_nprocs();

	if (!overprovisioned) {
		if (nprocs < 4)
			/*
			 * Only run the test if there are enough cores to avoid overprovisioning
			 * the system.
			 */
			return KSFT_SKIP;
		nprocs /= 4;
	}

	parent = cg_name(root, "cpucg_test");
	child = cg_name(parent, "cpucg_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;
	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;
	if (cg_write(child, "cgroup.subtree_control", "+cpu"))
		goto cleanup;
	if (cg_write(child, "cpu.weight", "1000"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(leaf); i++) {
		const char *ancestor;
		long weight;

		if (i == 0) {
			ancestor = parent;
			weight = 1000;
		} else {
			ancestor = child;
			weight = 5000;
		}
		leaf[i].cgroup = cg_name_indexed(ancestor, "cpucg_leaf", i);
		if (!leaf[i].cgroup)
			goto cleanup;

		if (cg_create(leaf[i].cgroup))
			goto cleanup;

		if (cg_write_numeric(leaf[i].cgroup, "cpu.weight", weight))
			goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(leaf); i++) {
		pid_t pid;
		struct cpu_hog_func_param param = {
			.nprocs = nprocs,
			.ts = {
				.tv_sec = 10,
				.tv_nsec = 0,
			},
			.clock_type = CPU_HOG_CLOCK_WALL,
		};

		pid = cg_run_nowait(leaf[i].cgroup, hog_cpus_timed,
				(void *)&param);
		if (pid <= 0)
			goto cleanup;
		leaf[i].pid = pid;
	}

	for (i = 0; i < ARRAY_SIZE(leaf); i++) {
		int retcode;

		waitpid(leaf[i].pid, &retcode, 0);
		if (!WIFEXITED(retcode))
			goto cleanup;
		if (WEXITSTATUS(retcode))
			goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(leaf); i++) {
		leaf[i].usage = cg_read_key_long(leaf[i].cgroup,
				"cpu.stat", "usage_usec");
		if (leaf[i].usage <= 0)
			goto cleanup;
	}

	nested_leaf_usage = leaf[1].usage + leaf[2].usage;
	if (overprovisioned) {
		if (!values_close(leaf[0].usage, nested_leaf_usage, 15))
			goto cleanup;
	} else if (!values_close(leaf[0].usage * 2, nested_leaf_usage, 15))
		goto cleanup;


	child_usage = cg_read_key_long(child, "cpu.stat", "usage_usec");
	if (child_usage <= 0)
		goto cleanup;
	if (!values_close(child_usage, nested_leaf_usage, 1))
		goto cleanup;

	ret = KSFT_PASS;
cleanup:
	for (i = 0; i < ARRAY_SIZE(leaf); i++) {
		cg_destroy(leaf[i].cgroup);
		free(leaf[i].cgroup);
	}
	cg_destroy(child);
	free(child);
	cg_destroy(parent);
	free(parent);

	return ret;
}

/*
 * First, this test creates the following hierarchy:
 * A
 * A/B     cpu.weight = 1000
 * A/C     cpu.weight = 1000
 * A/C/D   cpu.weight = 5000
 * A/C/E   cpu.weight = 5000
 *
 * A separate process is then created for each leaf, which spawn nproc threads
 * that burn a CPU for a few seconds.
 *
 * Once all of those processes have exited, we verify that each of the leaf
 * cgroups have roughly the same usage from cpu.stat.
 */
static int
test_cpucg_nested_weight_overprovisioned(const char *root)
{
	return run_cpucg_nested_weight_test(root, true);
}

/*
 * First, this test creates the following hierarchy:
 * A
 * A/B     cpu.weight = 1000
 * A/C     cpu.weight = 1000
 * A/C/D   cpu.weight = 5000
 * A/C/E   cpu.weight = 5000
 *
 * A separate process is then created for each leaf, which nproc / 4 threads
 * that burns a CPU for a few seconds.
 *
 * Once all of those processes have exited, we verify that each of the leaf
 * cgroups have roughly the same usage from cpu.stat.
 */
static int
test_cpucg_nested_weight_underprovisioned(const char *root)
{
	return run_cpucg_nested_weight_test(root, false);
}

/*
 * This test creates a cgroup with some maximum value within a period, and
 * verifies that a process in the cgroup is not overscheduled.
 */
static int test_cpucg_max(const char *root)
{
	int ret = KSFT_FAIL;
	long usage_usec, user_usec;
	long usage_seconds = 1;
	long expected_usage_usec = usage_seconds * USEC_PER_SEC;
	char *cpucg;

	cpucg = cg_name(root, "cpucg_test");
	if (!cpucg)
		goto cleanup;

	if (cg_create(cpucg))
		goto cleanup;

	if (cg_write(cpucg, "cpu.max", "1000"))
		goto cleanup;

	struct cpu_hog_func_param param = {
		.nprocs = 1,
		.ts = {
			.tv_sec = usage_seconds,
			.tv_nsec = 0,
		},
		.clock_type = CPU_HOG_CLOCK_WALL,
	};
	if (cg_run(cpucg, hog_cpus_timed, (void *)&param))
		goto cleanup;

	usage_usec = cg_read_key_long(cpucg, "cpu.stat", "usage_usec");
	user_usec = cg_read_key_long(cpucg, "cpu.stat", "user_usec");
	if (user_usec <= 0)
		goto cleanup;

	if (user_usec >= expected_usage_usec)
		goto cleanup;

	if (values_close(usage_usec, expected_usage_usec, 95))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_destroy(cpucg);
	free(cpucg);

	return ret;
}

/*
 * This test verifies that a process inside of a nested cgroup whose parent
 * group has a cpu.max value set, is properly throttled.
 */
static int test_cpucg_max_nested(const char *root)
{
	int ret = KSFT_FAIL;
	long usage_usec, user_usec;
	long usage_seconds = 1;
	long expected_usage_usec = usage_seconds * USEC_PER_SEC;
	char *parent, *child;

	parent = cg_name(root, "cpucg_parent");
	child = cg_name(parent, "cpucg_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cpu.max", "1000"))
		goto cleanup;

	struct cpu_hog_func_param param = {
		.nprocs = 1,
		.ts = {
			.tv_sec = usage_seconds,
			.tv_nsec = 0,
		},
		.clock_type = CPU_HOG_CLOCK_WALL,
	};
	if (cg_run(child, hog_cpus_timed, (void *)&param))
		goto cleanup;

	usage_usec = cg_read_key_long(child, "cpu.stat", "usage_usec");
	user_usec = cg_read_key_long(child, "cpu.stat", "user_usec");
	if (user_usec <= 0)
		goto cleanup;

	if (user_usec >= expected_usage_usec)
		goto cleanup;

	if (values_close(usage_usec, expected_usage_usec, 95))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_destroy(child);
	free(child);
	cg_destroy(parent);
	free(parent);

	return ret;
}

#define T(x) { x, #x }
struct cpucg_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cpucg_subtree_control),
	T(test_cpucg_stats),
	T(test_cpucg_weight_overprovisioned),
	T(test_cpucg_weight_underprovisioned),
	T(test_cpucg_nested_weight_overprovisioned),
	T(test_cpucg_nested_weight_underprovisioned),
	T(test_cpucg_max),
	T(test_cpucg_max_nested),
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
