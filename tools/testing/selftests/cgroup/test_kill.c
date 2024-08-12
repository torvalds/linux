/* SPDX-License-Identifier: GPL-2.0 */

#include <errno.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "../kselftest.h"
#include "../pidfd/pidfd.h"
#include "cgroup_util.h"

/*
 * Kill the given cgroup and wait for the inotify signal.
 * If there are no events in 10 seconds, treat this as an error.
 * Then check that the cgroup is in the desired state.
 */
static int cg_kill_wait(const char *cgroup)
{
	int fd, ret = -1;

	fd = cg_prepare_for_wait(cgroup);
	if (fd < 0)
		return fd;

	ret = cg_write(cgroup, "cgroup.kill", "1");
	if (ret)
		goto out;

	ret = cg_wait_for(fd);
	if (ret)
		goto out;

out:
	close(fd);
	return ret;
}

/*
 * A simple process running in a sleep loop until being
 * re-parented.
 */
static int child_fn(const char *cgroup, void *arg)
{
	int ppid = getppid();

	while (getppid() == ppid)
		usleep(1000);

	return getppid() == ppid;
}

static int test_cgkill_simple(const char *root)
{
	pid_t pids[100];
	int ret = KSFT_FAIL;
	char *cgroup = NULL;
	int i;

	cgroup = cg_name(root, "cg_test_simple");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	for (i = 0; i < 100; i++)
		pids[i] = cg_run_nowait(cgroup, child_fn, NULL);

	if (cg_wait_for_proc_count(cgroup, 100))
		goto cleanup;

	if (cg_read_strcmp(cgroup, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_kill_wait(cgroup))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (i = 0; i < 100; i++)
		wait_for_pid(pids[i]);

	if (ret == KSFT_PASS &&
	    cg_read_strcmp(cgroup, "cgroup.events", "populated 0\n"))
		ret = KSFT_FAIL;

	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

/*
 * The test creates the following hierarchy:
 *       A
 *    / / \ \
 *   B  E  I K
 *  /\  |
 * C  D F
 *      |
 *      G
 *      |
 *      H
 *
 * with a process in C, H and 3 processes in K.
 * Then it tries to kill the whole tree.
 */
static int test_cgkill_tree(const char *root)
{
	pid_t pids[5];
	char *cgroup[10] = {0};
	int ret = KSFT_FAIL;
	int i;

	cgroup[0] = cg_name(root, "cg_test_tree_A");
	if (!cgroup[0])
		goto cleanup;

	cgroup[1] = cg_name(cgroup[0], "B");
	if (!cgroup[1])
		goto cleanup;

	cgroup[2] = cg_name(cgroup[1], "C");
	if (!cgroup[2])
		goto cleanup;

	cgroup[3] = cg_name(cgroup[1], "D");
	if (!cgroup[3])
		goto cleanup;

	cgroup[4] = cg_name(cgroup[0], "E");
	if (!cgroup[4])
		goto cleanup;

	cgroup[5] = cg_name(cgroup[4], "F");
	if (!cgroup[5])
		goto cleanup;

	cgroup[6] = cg_name(cgroup[5], "G");
	if (!cgroup[6])
		goto cleanup;

	cgroup[7] = cg_name(cgroup[6], "H");
	if (!cgroup[7])
		goto cleanup;

	cgroup[8] = cg_name(cgroup[0], "I");
	if (!cgroup[8])
		goto cleanup;

	cgroup[9] = cg_name(cgroup[0], "K");
	if (!cgroup[9])
		goto cleanup;

	for (i = 0; i < 10; i++)
		if (cg_create(cgroup[i]))
			goto cleanup;

	pids[0] = cg_run_nowait(cgroup[2], child_fn, NULL);
	pids[1] = cg_run_nowait(cgroup[7], child_fn, NULL);
	pids[2] = cg_run_nowait(cgroup[9], child_fn, NULL);
	pids[3] = cg_run_nowait(cgroup[9], child_fn, NULL);
	pids[4] = cg_run_nowait(cgroup[9], child_fn, NULL);

	/*
	 * Wait until all child processes will enter
	 * corresponding cgroups.
	 */

	if (cg_wait_for_proc_count(cgroup[2], 1) ||
	    cg_wait_for_proc_count(cgroup[7], 1) ||
	    cg_wait_for_proc_count(cgroup[9], 3))
		goto cleanup;

	/*
	 * Kill A and check that we get an empty notification.
	 */
	if (cg_kill_wait(cgroup[0]))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (i = 0; i < 5; i++)
		wait_for_pid(pids[i]);

	if (ret == KSFT_PASS &&
	    cg_read_strcmp(cgroup[0], "cgroup.events", "populated 0\n"))
		ret = KSFT_FAIL;

	for (i = 9; i >= 0 && cgroup[i]; i--) {
		cg_destroy(cgroup[i]);
		free(cgroup[i]);
	}

	return ret;
}

static int forkbomb_fn(const char *cgroup, void *arg)
{
	int ppid;

	fork();
	fork();

	ppid = getppid();

	while (getppid() == ppid)
		usleep(1000);

	return getppid() == ppid;
}

/*
 * The test runs a fork bomb in a cgroup and tries to kill it.
 */
static int test_cgkill_forkbomb(const char *root)
{
	int ret = KSFT_FAIL;
	char *cgroup = NULL;
	pid_t pid = -ESRCH;

	cgroup = cg_name(root, "cg_forkbomb_test");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	pid = cg_run_nowait(cgroup, forkbomb_fn, NULL);
	if (pid < 0)
		goto cleanup;

	usleep(100000);

	if (cg_kill_wait(cgroup))
		goto cleanup;

	if (cg_wait_for_proc_count(cgroup, 0))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (pid > 0)
		wait_for_pid(pid);

	if (ret == KSFT_PASS &&
	    cg_read_strcmp(cgroup, "cgroup.events", "populated 0\n"))
		ret = KSFT_FAIL;

	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

#define T(x) { x, #x }
struct cgkill_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cgkill_simple),
	T(test_cgkill_tree),
	T(test_cgkill_forkbomb),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root), NULL))
		ksft_exit_skip("cgroup v2 isn't mounted\n");
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
