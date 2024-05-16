// SPDX-License-Identifier: GPL-2.0

#include <linux/limits.h>
#include <signal.h>

#include "../kselftest.h"
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


#define T(x) { x, #x }
struct cpuset_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cpuset_perms_object_allow),
	T(test_cpuset_perms_object_deny),
	T(test_cpuset_perms_subtree),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
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
			ret = EXIT_FAILURE;
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	return ret;
}
