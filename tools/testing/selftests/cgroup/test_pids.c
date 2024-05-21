// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../kselftest.h"
#include "cgroup_util.h"

static int run_success(const char *cgroup, void *arg)
{
	return 0;
}

static int run_pause(const char *cgroup, void *arg)
{
	return pause();
}

/*
 * This test checks that pids.max prevents forking new children above the
 * specified limit in the cgroup.
 */
static int test_pids_max(const char *root)
{
	int ret = KSFT_FAIL;
	char *cg_pids;
	int pid;

	cg_pids = cg_name(root, "pids_test");
	if (!cg_pids)
		goto cleanup;

	if (cg_create(cg_pids))
		goto cleanup;

	if (cg_read_strcmp(cg_pids, "pids.max", "max\n"))
		goto cleanup;

	if (cg_write(cg_pids, "pids.max", "2"))
		goto cleanup;

	if (cg_enter_current(cg_pids))
		goto cleanup;

	pid = cg_run_nowait(cg_pids, run_pause, NULL);
	if (pid < 0)
		goto cleanup;

	if (cg_run_nowait(cg_pids, run_success, NULL) != -1 || errno != EAGAIN)
		goto cleanup;

	if (kill(pid, SIGINT))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	cg_destroy(cg_pids);
	free(cg_pids);

	return ret;
}

/*
 * This test checks that pids.events are counted in cgroup associated with pids.max
 */
static int test_pids_events(const char *root)
{
	int ret = KSFT_FAIL;
	char *cg_parent = NULL, *cg_child = NULL;
	int pid;

	cg_parent = cg_name(root, "pids_parent");
	cg_child = cg_name(cg_parent, "pids_child");
	if (!cg_parent || !cg_child)
		goto cleanup;

	if (cg_create(cg_parent))
		goto cleanup;
	if (cg_write(cg_parent, "cgroup.subtree_control", "+pids"))
		goto cleanup;
	if (cg_create(cg_child))
		goto cleanup;

	if (cg_write(cg_parent, "pids.max", "2"))
		goto cleanup;

	if (cg_read_strcmp(cg_child, "pids.max", "max\n"))
		goto cleanup;

	if (cg_enter_current(cg_child))
		goto cleanup;

	pid = cg_run_nowait(cg_child, run_pause, NULL);
	if (pid < 0)
		goto cleanup;

	if (cg_run_nowait(cg_child, run_success, NULL) != -1 || errno != EAGAIN)
		goto cleanup;

	if (kill(pid, SIGINT))
		goto cleanup;

	if (cg_read_key_long(cg_child, "pids.events", "max ") != 0)
		goto cleanup;
	if (cg_read_key_long(cg_parent, "pids.events", "max ") != 1)
		goto cleanup;


	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (cg_child)
		cg_destroy(cg_child);
	if (cg_parent)
		cg_destroy(cg_parent);
	free(cg_child);
	free(cg_parent);

	return ret;
}



#define T(x) { x, #x }
struct pids_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_pids_max),
	T(test_pids_events),
};
#undef T

int main(int argc, char **argv)
{
	char root[PATH_MAX];

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tests));
	if (cg_find_unified_root(root, sizeof(root), NULL))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	/*
	 * Check that pids controller is available:
	 * pids is listed in cgroup.controllers
	 */
	if (cg_read_strstr(root, "cgroup.controllers", "pids"))
		ksft_exit_skip("pids controller isn't available\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "pids"))
		if (cg_write(root, "cgroup.subtree_control", "+pids"))
			ksft_exit_skip("Failed to set pids controller\n");

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
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
