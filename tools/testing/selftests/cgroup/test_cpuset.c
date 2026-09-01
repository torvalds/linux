// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <assert.h>
#include <linux/limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "kselftest.h"
#include "cgroup_util.h"

static int idle_process_fn(const char *cgroup, void *arg)
{
	(void)pause();
	return 0;
}

static int do_migration_fn(const char *cgroup, void *arg)
{
	int object_pid = (int)(size_t)arg;

	if (setuid(TEST_UID))
		return EXIT_FAILURE;

	// XXX checking /proc/$pid/cgroup would be quicker than wait
	if (cg_enter(cgroup, object_pid) ||
	    cg_wait_for_proc_count(cgroup, 1))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

static int do_controller_fn(const char *cgroup, void *arg)
{
	const char *child = cgroup;
	const char *parent = arg;

	if (setuid(TEST_UID))
		return EXIT_FAILURE;

	if (!cg_read_strstr(child, "cgroup.controllers", "cpuset"))
		return EXIT_FAILURE;

	if (cg_write(parent, "cgroup.subtree_control", "+cpuset"))
		return EXIT_FAILURE;

	if (cg_read_strstr(child, "cgroup.controllers", "cpuset"))
		return EXIT_FAILURE;

	if (cg_write(parent, "cgroup.subtree_control", "-cpuset"))
		return EXIT_FAILURE;

	if (!cg_read_strstr(child, "cgroup.controllers", "cpuset"))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

/*
 * Migrate a process between two sibling cgroups.
 * The success should only depend on the parent cgroup permissions and not the
 * migrated process itself (cpuset controller is in place because it uses
 * security_task_setscheduler() in cgroup v1).
 *
 * Deliberately don't set cpuset.cpus in children to avoid definining migration
 * permissions between two different cpusets.
 */
static int test_cpuset_perms_object(const char *root, bool allow)
{
	char *parent = NULL, *child_src = NULL, *child_dst = NULL;
	char *parent_procs = NULL, *child_src_procs = NULL, *child_dst_procs = NULL;
	const uid_t test_euid = TEST_UID;
	int object_pid = 0;
	int ret = KSFT_FAIL;

	parent = cg_name(root, "cpuset_test_0");
	if (!parent)
		goto cleanup;
	parent_procs = cg_name(parent, "cgroup.procs");
	if (!parent_procs)
		goto cleanup;
	if (cg_create(parent))
		goto cleanup;

	child_src = cg_name(parent, "cpuset_test_1");
	if (!child_src)
		goto cleanup;
	child_src_procs = cg_name(child_src, "cgroup.procs");
	if (!child_src_procs)
		goto cleanup;
	if (cg_create(child_src))
		goto cleanup;

	child_dst = cg_name(parent, "cpuset_test_2");
	if (!child_dst)
		goto cleanup;
	child_dst_procs = cg_name(child_dst, "cgroup.procs");
	if (!child_dst_procs)
		goto cleanup;
	if (cg_create(child_dst))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpuset"))
		goto cleanup;

	if (cg_read_strstr(child_src, "cgroup.controllers", "cpuset") ||
	    cg_read_strstr(child_dst, "cgroup.controllers", "cpuset"))
		goto cleanup;

	/* Enable permissions along src->dst tree path */
	if (chown(child_src_procs, test_euid, -1) ||
	    chown(child_dst_procs, test_euid, -1))
		goto cleanup;

	if (allow && chown(parent_procs, test_euid, -1))
		goto cleanup;

	/* Fork a privileged child as a test object */
	object_pid = cg_run_nowait(child_src, idle_process_fn, NULL);
	if (object_pid < 0)
		goto cleanup;

	/* Carry out migration in a child process that can drop all privileges
	 * (including capabilities), the main process must remain privileged for
	 * cleanup.
	 * Child process's cgroup is irrelevant but we place it into child_dst
	 * as hacky way to pass information about migration target to the child.
	 */
	if (allow ^ (cg_run(child_dst, do_migration_fn, (void *)(size_t)object_pid) == EXIT_SUCCESS))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (object_pid > 0) {
		(void)kill(object_pid, SIGTERM);
		(void)clone_reap(object_pid, WEXITED);
	}

	cg_destroy(child_dst);
	free(child_dst_procs);
	free(child_dst);

	cg_destroy(child_src);
	free(child_src_procs);
	free(child_src);

	cg_destroy(parent);
	free(parent_procs);
	free(parent);

	return ret;
}

static int test_cpuset_perms_object_allow(const char *root)
{
	return test_cpuset_perms_object(root, true);
}

static int test_cpuset_perms_object_deny(const char *root)
{
	return test_cpuset_perms_object(root, false);
}

/*
 * Migrate a process between parent and child implicitely
 * Implicit migration happens when a controller is enabled/disabled.
 *
 */
static int test_cpuset_perms_subtree(const char *root)
{
	char *parent = NULL, *child = NULL;
	char *parent_procs = NULL, *parent_subctl = NULL, *child_procs = NULL;
	const uid_t test_euid = TEST_UID;
	int object_pid = 0;
	int ret = KSFT_FAIL;

	parent = cg_name(root, "cpuset_test_0");
	if (!parent)
		goto cleanup;
	parent_procs = cg_name(parent, "cgroup.procs");
	if (!parent_procs)
		goto cleanup;
	parent_subctl = cg_name(parent, "cgroup.subtree_control");
	if (!parent_subctl)
		goto cleanup;
	if (cg_create(parent))
		goto cleanup;

	child = cg_name(parent, "cpuset_test_1");
	if (!child)
		goto cleanup;
	child_procs = cg_name(child, "cgroup.procs");
	if (!child_procs)
		goto cleanup;
	if (cg_create(child))
		goto cleanup;

	/* Enable permissions as in a delegated subtree */
	if (chown(parent_procs, test_euid, -1) ||
	    chown(parent_subctl, test_euid, -1) ||
	    chown(child_procs, test_euid, -1))
		goto cleanup;

	/* Put a privileged child in the subtree and modify controller state
	 * from an unprivileged process, the main process remains privileged
	 * for cleanup.
	 * The unprivileged child runs in subtree too to avoid parent and
	 * internal-node constraing violation.
	 */
	object_pid = cg_run_nowait(child, idle_process_fn, NULL);
	if (object_pid < 0)
		goto cleanup;

	if (cg_run(child, do_controller_fn, parent) != EXIT_SUCCESS)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (object_pid > 0) {
		(void)kill(object_pid, SIGTERM);
		(void)clone_reap(object_pid, WEXITED);
	}

	cg_destroy(child);
	free(child_procs);
	free(child);

	cg_destroy(parent);
	free(parent_subctl);
	free(parent_procs);
	free(parent);

	return ret;
}

static int get_cpu_affinity(cpu_set_t *mask)
{
	CPU_ZERO(mask);
	return sched_getaffinity(0, sizeof(*mask), mask);
}

static int cpu_set_equal(cpu_set_t *dst, unsigned long mask)
{
	cpu_set_t expected;

	CPU_ZERO(&expected);
	assert(sizeof(mask) < CPU_SETSIZE);

	for (int cpu = 0; cpu < sizeof(mask) * 8; ++cpu)
		if ((1UL << cpu) & mask)
			CPU_SET(cpu, &expected);

	return CPU_EQUAL(&expected, dst);
}

enum test_phase {
	AFFINITY_SETUP,
	AFFINITY_CONTROLLER_DISABLED,
	AFFINITY_COMPLETE,
	AFFINITY_ERROR
};

struct thread_args {
	const char *cgroup;
	cpu_set_t *affinity_before;
	cpu_set_t *affinity_after;
	int affinity_before_ready;
};

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static enum test_phase test_phase;

static void *affinity_thread_fn(void *arg)
{
	struct thread_args *args = (struct thread_args *)arg;

	if (cg_enter_current_thread(args->cgroup))
		goto fail;

	if (get_cpu_affinity(args->affinity_before) != 0)
		goto fail;

	pthread_mutex_lock(&test_mutex);
	args->affinity_before_ready = 1;
	pthread_cond_broadcast(&test_cond);

	while (test_phase < AFFINITY_CONTROLLER_DISABLED)
		pthread_cond_wait(&test_cond, &test_mutex);
	pthread_mutex_unlock(&test_mutex);

	if (get_cpu_affinity(args->affinity_after) != 0)
		goto fail;


	return NULL;

fail:
	pthread_mutex_lock(&test_mutex);
	test_phase = AFFINITY_ERROR;
	pthread_cond_broadcast(&test_cond);
	pthread_mutex_unlock(&test_mutex);
	return NULL;
}

/*
 * Test that disabling cpuset controller properly updates thread affinity.
 *
 * This test exposes a bug in cpuset_attach() where threads in child cgroups
 * don't get their affinity updated when the cpuset controller is disabled.
 *
 * Setup:
 * - Create parent cgroup with cpuset.cpus=0-1
 * - Create child A with cpuset.cpus=0-1
 * - Create child B with cpuset.cpus=1
 * - Place multithreaded process: group leader + thread_a in A, thread_b in B
 * - Disable cpuset controller on parent
 *
 * Expected: thread_b's affinity should expand from {1} to {0-1}
 * Buggy: thread_b's affinity remains {1}
 */
static int test_cpuset_affinity_on_controller_disable(const char *root)
{
	char *parent = NULL, *child_a = NULL, *child_b = NULL;
	pthread_t thread_a, thread_b;
	int thread_a_created = 0, thread_b_created = 0;
	cpu_set_t affinity_a_before, affinity_a_after;
	cpu_set_t affinity_b_before, affinity_b_after;
	int ret = KSFT_FAIL;

	parent = cg_name(root, "cpuset_affinity_test");
	if (!parent)
		goto cleanup;
	if (cg_create(parent))
		goto cleanup;
	if (cg_write(parent, "cgroup.type", "threaded"))
		goto cleanup;

	child_a = cg_name(parent, "A");
	if (!child_a)
		goto cleanup;
	if (cg_create(child_a))
		goto cleanup;
	if (cg_write(child_a, "cgroup.type", "threaded"))
		goto cleanup;

	child_b = cg_name(parent, "B");
	if (!child_b)
		goto cleanup;
	if (cg_create(child_b))
		goto cleanup;
	if (cg_write(child_b, "cgroup.type", "threaded"))
		goto cleanup;

	/* Now enable cpuset controller in parent */
	if (cg_write(parent, "cgroup.subtree_control", "+cpuset"))
		goto skip;

	/*
	 * Set CPU affinity constraints
	 * Skip the test if the setting of "cpuset.cpus" fails as the test
	 * system may not have CPU 1.
	 */
	if (cg_write(parent, "cpuset.cpus", "0-1"))
		goto skip;
	if (cg_write(child_a, "cpuset.cpus", "0-1"))
		goto skip;
	if (cg_write(child_b, "cpuset.cpus", "1"))
		goto skip;

	/* Move group leader (main thread) to child A */
	if (cg_enter_current(child_a))
		goto cleanup;

	/* Create threads - they will move themselves to their respective cgroups */
	test_phase = AFFINITY_SETUP;

	struct thread_args args_a = {
		.cgroup = child_a,
		.affinity_before = &affinity_a_before,
		.affinity_after = &affinity_a_after,
		.affinity_before_ready = 0,
	};
	if (pthread_create(&thread_a, NULL, affinity_thread_fn, &args_a))
		goto cleanup;
	thread_a_created = 1;

	struct thread_args args_b = {
		.cgroup = child_b,
		.affinity_before = &affinity_b_before,
		.affinity_after = &affinity_b_after,
		.affinity_before_ready = 0,
	};
	if (pthread_create(&thread_b, NULL, affinity_thread_fn, &args_b))
		goto cleanup_threads;
	thread_b_created = 1;

	pthread_mutex_lock(&test_mutex);
	while ((test_phase < AFFINITY_ERROR) &&
	       (args_a.affinity_before_ready + args_b.affinity_before_ready < 2))
		pthread_cond_wait(&test_cond, &test_mutex);

	/* If a thread failed during setup, bail out */
	if (test_phase == AFFINITY_ERROR) {
		pthread_mutex_unlock(&test_mutex);
		goto cleanup_threads;
	}
	pthread_mutex_unlock(&test_mutex);

	if (!cpu_set_equal(&affinity_a_before, 0x3)) {
		ksft_print_msg("FAIL: thread_a initial affinity incorrect\n");
		goto cleanup_threads;
	}

	if (!cpu_set_equal(&affinity_b_before, 0x2)) {
		ksft_print_msg("FAIL: thread_b initial affinity incorrect\n");
		goto cleanup_threads;
	}

	/* Disable cpuset controller - this should trigger affinity update */
	if (cg_write(parent, "cgroup.subtree_control", "-cpuset"))
		goto cleanup_threads;

	/* Signal threads to save their final affinity and exit */
	pthread_mutex_lock(&test_mutex);
	test_phase = AFFINITY_CONTROLLER_DISABLED;
	pthread_cond_broadcast(&test_cond);
	pthread_mutex_unlock(&test_mutex);

	pthread_join(thread_a, NULL);
	pthread_join(thread_b, NULL);

	/* Verify thread affinities AFTER disabling controller */
	if (!cpu_set_equal(&affinity_a_after, 0x3)) {
		ksft_print_msg("FAIL: thread_a final affinity incorrect\n");
		goto cleanup;
	}

	if (!cpu_set_equal(&affinity_b_after, 0x3)) {
		ksft_print_msg("FAIL: thread_b affinity did not expand to {0-1}\n");
		goto cleanup;
	}

	ret = KSFT_PASS;
	goto cleanup;

skip:
	ret = KSFT_SKIP;
	goto cleanup;

cleanup_threads:
	pthread_mutex_lock(&test_mutex);
	test_phase = AFFINITY_COMPLETE;
	pthread_cond_broadcast(&test_cond);
	pthread_mutex_unlock(&test_mutex);

	if (thread_a_created)
		pthread_join(thread_a, NULL);
	if (thread_b_created)
		pthread_join(thread_b, NULL);

cleanup:
	/* Move back to root before cleanup */
	cg_enter_current(root);

	cg_destroy(child_b);
	free(child_b);
	cg_destroy(child_a);
	free(child_a);
	cg_destroy(parent);
	free(parent);

	return ret;
}


#define T(x) { x, #x }
struct cpuset_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cpuset_perms_object_allow),
	T(test_cpuset_perms_object_deny),
	T(test_cpuset_perms_subtree),
	T(test_cpuset_affinity_on_controller_disable),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tests));
	if (cg_find_unified_root(root, sizeof(root), NULL))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "cpuset"))
		if (cg_write(root, "cgroup.subtree_control", "+cpuset"))
			ksft_exit_skip("Failed to set cpuset controller\n");

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		switch (tests[i].fn(root)) {
		case KSFT_PASS:
			ksft_test_result_pass("%s\n", tests[i].name);
			break;
		case KSFT_SKIP:
			ksft_test_result_skip("%s\n", tests[i].name);
			break;
		default:
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	ksft_finished();
}
