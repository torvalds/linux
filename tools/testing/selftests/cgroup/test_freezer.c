/* SPDX-License-Identifier: GPL-2.0 */
#include <stdbool.h>
#include <linux/limits.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <string.h>
#include <sys/wait.h>

#include "../kselftest.h"
#include "cgroup_util.h"

#define DEBUG
#ifdef DEBUG
#define debug(args...) fprintf(stderr, args)
#else
#define debug(args...)
#endif

/*
 * Check if the cgroup is frozen by looking at the cgroup.events::frozen value.
 */
static int cg_check_frozen(const char *cgroup, bool frozen)
{
	if (frozen) {
		if (cg_read_strstr(cgroup, "cgroup.events", "frozen 1") != 0) {
			debug("Cgroup %s isn't frozen\n", cgroup);
			return -1;
		}
	} else {
		/*
		 * Check the cgroup.events::frozen value.
		 */
		if (cg_read_strstr(cgroup, "cgroup.events", "frozen 0") != 0) {
			debug("Cgroup %s is frozen\n", cgroup);
			return -1;
		}
	}

	return 0;
}

/*
 * Freeze the given cgroup.
 */
static int cg_freeze_nowait(const char *cgroup, bool freeze)
{
	return cg_write(cgroup, "cgroup.freeze", freeze ? "1" : "0");
}

/*
 * Prepare for waiting on cgroup.events file.
 */
static int cg_prepare_for_wait(const char *cgroup)
{
	int fd, ret = -1;

	fd = inotify_init1(0);
	if (fd == -1) {
		debug("Error: inotify_init1() failed\n");
		return fd;
	}

	ret = inotify_add_watch(fd, cg_control(cgroup, "cgroup.events"),
				IN_MODIFY);
	if (ret == -1) {
		debug("Error: inotify_add_watch() failed\n");
		close(fd);
		fd = -1;
	}

	return fd;
}

/*
 * Wait for an event. If there are no events for 10 seconds,
 * treat this an error.
 */
static int cg_wait_for(int fd)
{
	int ret = -1;
	struct pollfd fds = {
		.fd = fd,
		.events = POLLIN,
	};

	while (true) {
		ret = poll(&fds, 1, 10000);

		if (ret == -1) {
			if (errno == EINTR)
				continue;
			debug("Error: poll() failed\n");
			break;
		}

		if (ret > 0 && fds.revents & POLLIN) {
			ret = 0;
			break;
		}
	}

	return ret;
}

/*
 * Attach a task to the given cgroup and wait for a cgroup frozen event.
 * All transient events (e.g. populated) are ignored.
 */
static int cg_enter_and_wait_for_frozen(const char *cgroup, int pid,
					bool frozen)
{
	int fd, ret = -1;
	int attempts;

	fd = cg_prepare_for_wait(cgroup);
	if (fd < 0)
		return fd;

	ret = cg_enter(cgroup, pid);
	if (ret)
		goto out;

	for (attempts = 0; attempts < 10; attempts++) {
		ret = cg_wait_for(fd);
		if (ret)
			break;

		ret = cg_check_frozen(cgroup, frozen);
		if (ret)
			continue;
	}

out:
	close(fd);
	return ret;
}

/*
 * Freeze the given cgroup and wait for the inotify signal.
 * If there are no events in 10 seconds, treat this as an error.
 * Then check that the cgroup is in the desired state.
 */
static int cg_freeze_wait(const char *cgroup, bool freeze)
{
	int fd, ret = -1;

	fd = cg_prepare_for_wait(cgroup);
	if (fd < 0)
		return fd;

	ret = cg_freeze_nowait(cgroup, freeze);
	if (ret) {
		debug("Error: cg_freeze_nowait() failed\n");
		goto out;
	}

	ret = cg_wait_for(fd);
	if (ret)
		goto out;

	ret = cg_check_frozen(cgroup, freeze);
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

/*
 * A simple test for the cgroup freezer: populated the cgroup with 100
 * running processes and freeze it. Then unfreeze it. Then it kills all
 * processes and destroys the cgroup.
 */
static int test_cgfreezer_simple(const char *root)
{
	int ret = KSFT_FAIL;
	char *cgroup = NULL;
	int i;

	cgroup = cg_name(root, "cg_test_simple");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	for (i = 0; i < 100; i++)
		cg_run_nowait(cgroup, child_fn, NULL);

	if (cg_wait_for_proc_count(cgroup, 100))
		goto cleanup;

	if (cg_check_frozen(cgroup, false))
		goto cleanup;

	if (cg_freeze_wait(cgroup, true))
		goto cleanup;

	if (cg_freeze_wait(cgroup, false))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
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
 * Then it tries to freeze and unfreeze the whole tree.
 */
static int test_cgfreezer_tree(const char *root)
{
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

	cg_run_nowait(cgroup[2], child_fn, NULL);
	cg_run_nowait(cgroup[7], child_fn, NULL);
	cg_run_nowait(cgroup[9], child_fn, NULL);
	cg_run_nowait(cgroup[9], child_fn, NULL);
	cg_run_nowait(cgroup[9], child_fn, NULL);

	/*
	 * Wait until all child processes will enter
	 * corresponding cgroups.
	 */

	if (cg_wait_for_proc_count(cgroup[2], 1) ||
	    cg_wait_for_proc_count(cgroup[7], 1) ||
	    cg_wait_for_proc_count(cgroup[9], 3))
		goto cleanup;

	/*
	 * Freeze B.
	 */
	if (cg_freeze_wait(cgroup[1], true))
		goto cleanup;

	/*
	 * Freeze F.
	 */
	if (cg_freeze_wait(cgroup[5], true))
		goto cleanup;

	/*
	 * Freeze G.
	 */
	if (cg_freeze_wait(cgroup[6], true))
		goto cleanup;

	/*
	 * Check that A and E are not frozen.
	 */
	if (cg_check_frozen(cgroup[0], false))
		goto cleanup;

	if (cg_check_frozen(cgroup[4], false))
		goto cleanup;

	/*
	 * Freeze A. Check that A, B and E are frozen.
	 */
	if (cg_freeze_wait(cgroup[0], true))
		goto cleanup;

	if (cg_check_frozen(cgroup[1], true))
		goto cleanup;

	if (cg_check_frozen(cgroup[4], true))
		goto cleanup;

	/*
	 * Unfreeze B, F and G
	 */
	if (cg_freeze_nowait(cgroup[1], false))
		goto cleanup;

	if (cg_freeze_nowait(cgroup[5], false))
		goto cleanup;

	if (cg_freeze_nowait(cgroup[6], false))
		goto cleanup;

	/*
	 * Check that C and H are still frozen.
	 */
	if (cg_check_frozen(cgroup[2], true))
		goto cleanup;

	if (cg_check_frozen(cgroup[7], true))
		goto cleanup;

	/*
	 * Unfreeze A. Check that A, C and K are not frozen.
	 */
	if (cg_freeze_wait(cgroup[0], false))
		goto cleanup;

	if (cg_check_frozen(cgroup[2], false))
		goto cleanup;

	if (cg_check_frozen(cgroup[9], false))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (i = 9; i >= 0 && cgroup[i]; i--) {
		cg_destroy(cgroup[i]);
		free(cgroup[i]);
	}

	return ret;
}

/*
 * A fork bomb emulator.
 */
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
 * The test runs a fork bomb in a cgroup and tries to freeze it.
 * Then it kills all processes and checks that cgroup isn't populated
 * anymore.
 */
static int test_cgfreezer_forkbomb(const char *root)
{
	int ret = KSFT_FAIL;
	char *cgroup = NULL;

	cgroup = cg_name(root, "cg_forkbomb_test");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	cg_run_nowait(cgroup, forkbomb_fn, NULL);

	usleep(100000);

	if (cg_freeze_wait(cgroup, true))
		goto cleanup;

	if (cg_killall(cgroup))
		goto cleanup;

	if (cg_wait_for_proc_count(cgroup, 0))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

/*
 * The test creates a cgroups and freezes it. Then it creates a child cgroup
 * and populates it with a task. After that it checks that the child cgroup
 * is frozen and the parent cgroup remains frozen too.
 */
static int test_cgfreezer_mkdir(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent, *child = NULL;
	int pid;

	parent = cg_name(root, "cg_test_mkdir_A");
	if (!parent)
		goto cleanup;

	child = cg_name(parent, "cg_test_mkdir_B");
	if (!child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_freeze_wait(parent, true))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	pid = cg_run_nowait(child, child_fn, NULL);
	if (pid < 0)
		goto cleanup;

	if (cg_wait_for_proc_count(child, 1))
		goto cleanup;

	if (cg_check_frozen(child, true))
		goto cleanup;

	if (cg_check_frozen(parent, true))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	free(child);
	if (parent)
		cg_destroy(parent);
	free(parent);
	return ret;
}

/*
 * The test creates two nested cgroups, freezes the parent
 * and removes the child. Then it checks that the parent cgroup
 * remains frozen and it's possible to create a new child
 * without unfreezing. The new child is frozen too.
 */
static int test_cgfreezer_rmdir(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent, *child = NULL;

	parent = cg_name(root, "cg_test_rmdir_A");
	if (!parent)
		goto cleanup;

	child = cg_name(parent, "cg_test_rmdir_B");
	if (!child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_freeze_wait(parent, true))
		goto cleanup;

	if (cg_destroy(child))
		goto cleanup;

	if (cg_check_frozen(parent, true))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_check_frozen(child, true))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	free(child);
	if (parent)
		cg_destroy(parent);
	free(parent);
	return ret;
}

/*
 * The test creates two cgroups: A and B, runs a process in A
 * and performs several migrations:
 * 1) A (running) -> B (frozen)
 * 2) B (frozen) -> A (running)
 * 3) A (frozen) -> B (frozen)
 *
 * On each step it checks the actual state of both cgroups.
 */
static int test_cgfreezer_migrate(const char *root)
{
	int ret = KSFT_FAIL;
	char *cgroup[2] = {0};
	int pid;

	cgroup[0] = cg_name(root, "cg_test_migrate_A");
	if (!cgroup[0])
		goto cleanup;

	cgroup[1] = cg_name(root, "cg_test_migrate_B");
	if (!cgroup[1])
		goto cleanup;

	if (cg_create(cgroup[0]))
		goto cleanup;

	if (cg_create(cgroup[1]))
		goto cleanup;

	pid = cg_run_nowait(cgroup[0], child_fn, NULL);
	if (pid < 0)
		goto cleanup;

	if (cg_wait_for_proc_count(cgroup[0], 1))
		goto cleanup;

	/*
	 * Migrate from A (running) to B (frozen)
	 */
	if (cg_freeze_wait(cgroup[1], true))
		goto cleanup;

	if (cg_enter_and_wait_for_frozen(cgroup[1], pid, true))
		goto cleanup;

	if (cg_check_frozen(cgroup[0], false))
		goto cleanup;

	/*
	 * Migrate from B (frozen) to A (running)
	 */
	if (cg_enter_and_wait_for_frozen(cgroup[0], pid, false))
		goto cleanup;

	if (cg_check_frozen(cgroup[1], true))
		goto cleanup;

	/*
	 * Migrate from A (frozen) to B (frozen)
	 */
	if (cg_freeze_wait(cgroup[0], true))
		goto cleanup;

	if (cg_enter_and_wait_for_frozen(cgroup[1], pid, true))
		goto cleanup;

	if (cg_check_frozen(cgroup[0], true))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup[0])
		cg_destroy(cgroup[0]);
	free(cgroup[0]);
	if (cgroup[1])
		cg_destroy(cgroup[1]);
	free(cgroup[1]);
	return ret;
}

/*
 * The test checks that ptrace works with a tracing process in a frozen cgroup.
 */
static int test_cgfreezer_ptrace(const char *root)
{
	int ret = KSFT_FAIL;
	char *cgroup = NULL;
	siginfo_t siginfo;
	int pid;

	cgroup = cg_name(root, "cg_test_ptrace");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	pid = cg_run_nowait(cgroup, child_fn, NULL);
	if (pid < 0)
		goto cleanup;

	if (cg_wait_for_proc_count(cgroup, 1))
		goto cleanup;

	if (cg_freeze_wait(cgroup, true))
		goto cleanup;

	if (ptrace(PTRACE_SEIZE, pid, NULL, NULL))
		goto cleanup;

	if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL))
		goto cleanup;

	waitpid(pid, NULL, 0);

	/*
	 * Cgroup has to remain frozen, however the test task
	 * is in traced state.
	 */
	if (cg_check_frozen(cgroup, true))
		goto cleanup;

	if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &siginfo))
		goto cleanup;

	if (ptrace(PTRACE_DETACH, pid, NULL, NULL))
		goto cleanup;

	if (cg_check_frozen(cgroup, true))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

/*
 * Check if the process is stopped.
 */
static int proc_check_stopped(int pid)
{
	char buf[PAGE_SIZE];
	int len;

	len = proc_read_text(pid, 0, "stat", buf, sizeof(buf));
	if (len == -1) {
		debug("Can't get %d stat\n", pid);
		return -1;
	}

	if (strstr(buf, "(test_freezer) T ") == NULL) {
		debug("Process %d in the unexpected state: %s\n", pid, buf);
		return -1;
	}

	return 0;
}

/*
 * Test that it's possible to freeze a cgroup with a stopped process.
 */
static int test_cgfreezer_stopped(const char *root)
{
	int pid, ret = KSFT_FAIL;
	char *cgroup = NULL;

	cgroup = cg_name(root, "cg_test_stopped");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	pid = cg_run_nowait(cgroup, child_fn, NULL);

	if (cg_wait_for_proc_count(cgroup, 1))
		goto cleanup;

	if (kill(pid, SIGSTOP))
		goto cleanup;

	if (cg_check_frozen(cgroup, false))
		goto cleanup;

	if (cg_freeze_wait(cgroup, true))
		goto cleanup;

	if (cg_freeze_wait(cgroup, false))
		goto cleanup;

	if (proc_check_stopped(pid))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

/*
 * Test that it's possible to freeze a cgroup with a ptraced process.
 */
static int test_cgfreezer_ptraced(const char *root)
{
	int pid, ret = KSFT_FAIL;
	char *cgroup = NULL;
	siginfo_t siginfo;

	cgroup = cg_name(root, "cg_test_ptraced");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	pid = cg_run_nowait(cgroup, child_fn, NULL);

	if (cg_wait_for_proc_count(cgroup, 1))
		goto cleanup;

	if (ptrace(PTRACE_SEIZE, pid, NULL, NULL))
		goto cleanup;

	if (ptrace(PTRACE_INTERRUPT, pid, NULL, NULL))
		goto cleanup;

	waitpid(pid, NULL, 0);

	if (cg_check_frozen(cgroup, false))
		goto cleanup;

	if (cg_freeze_wait(cgroup, true))
		goto cleanup;

	/*
	 * cg_check_frozen(cgroup, true) will fail here,
	 * because the task in in the TRACEd state.
	 */
	if (cg_freeze_wait(cgroup, false))
		goto cleanup;

	if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &siginfo))
		goto cleanup;

	if (ptrace(PTRACE_DETACH, pid, NULL, NULL))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

static int vfork_fn(const char *cgroup, void *arg)
{
	int pid = vfork();

	if (pid == 0)
		while (true)
			sleep(1);

	return pid;
}

/*
 * Test that it's possible to freeze a cgroup with a process,
 * which called vfork() and is waiting for a child.
 */
static int test_cgfreezer_vfork(const char *root)
{
	int ret = KSFT_FAIL;
	char *cgroup = NULL;

	cgroup = cg_name(root, "cg_test_vfork");
	if (!cgroup)
		goto cleanup;

	if (cg_create(cgroup))
		goto cleanup;

	cg_run_nowait(cgroup, vfork_fn, NULL);

	if (cg_wait_for_proc_count(cgroup, 2))
		goto cleanup;

	if (cg_freeze_wait(cgroup, true))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (cgroup)
		cg_destroy(cgroup);
	free(cgroup);
	return ret;
}

#define T(x) { x, #x }
struct cgfreezer_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cgfreezer_simple),
	T(test_cgfreezer_tree),
	T(test_cgfreezer_forkbomb),
	T(test_cgfreezer_mkdir),
	T(test_cgfreezer_rmdir),
	T(test_cgfreezer_migrate),
	T(test_cgfreezer_ptrace),
	T(test_cgfreezer_stopped),
	T(test_cgfreezer_ptraced),
	T(test_cgfreezer_vfork),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
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
