// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "kselftest_harness.h"
#include "../pidfd/pidfd.h"

/*
 * Test that a process can become PID 1 (init) in a new PID namespace
 * created via unshare() and joined via setns().
 *
 * Flow:
 *  1. Parent creates a pipe for synchronization.
 *  2. Parent forks a child.
 *  3. Parent calls unshare(CLONE_NEWPID) to create a new PID namespace.
 *  4. Parent signals the child via the pipe.
 *  5. Child opens parent's /proc/<ppid>/ns/pid_for_children and calls
 *     setns(fd, CLONE_NEWPID) to join the new namespace.
 *  6. Child forks a grandchild.
 *  7. Grandchild verifies getpid() == 1.
 */
TEST(pidns_init_via_setns)
{
	pid_t child, parent_pid;
	int pipe_fd[2];
	char buf;

	if (geteuid())
		ASSERT_EQ(0, unshare(CLONE_NEWUSER));

	parent_pid = getpid();

	ASSERT_EQ(0, pipe(pipe_fd));

	child = fork();
	ASSERT_GE(child, 0);

	if (child == 0) {
		char path[256];
		int nsfd;
		pid_t grandchild;

		close(pipe_fd[1]);

		/* Wait for parent to complete unshare */
		ASSERT_EQ(1, read_nointr(pipe_fd[0], &buf, 1));
		close(pipe_fd[0]);

		snprintf(path, sizeof(path),
			 "/proc/%d/ns/pid_for_children", parent_pid);
		nsfd = open(path, O_RDONLY);
		ASSERT_GE(nsfd, 0);

		ASSERT_EQ(0, setns(nsfd, CLONE_NEWPID));
		close(nsfd);

		grandchild = fork();
		ASSERT_GE(grandchild, 0);

		if (grandchild == 0) {
			/* Should be init (PID 1) in the new namespace */
			if (getpid() != 1)
				_exit(1);
			_exit(0);
		}

		ASSERT_EQ(0, wait_for_pid(grandchild));
		_exit(0);
	}

	close(pipe_fd[0]);

	ASSERT_EQ(0, unshare(CLONE_NEWPID));

	/* Signal child that the new PID namespace is ready */
	buf = 0;
	ASSERT_EQ(1, write_nointr(pipe_fd[1], &buf, 1));
	close(pipe_fd[1]);

	ASSERT_EQ(0, wait_for_pid(child));
}

/*
 * Similar to pidns_init_via_setns, but:
 *  1. Parent enters a new PID namespace right from the start to be able to
 *     later freely use pid 1001 in it.
 *  2. After forking child, parent also calls unshare(CLONE_NEWUSER)
 *     before unshare(CLONE_NEWPID) so that new old and new pid namespaces have
 *     different user namespace owners.
 *  3. Child uses clone3() with set_tid={1, 1001} instead of fork() and
 *     grandchild checks that it gets desired pids .
 *
 * Flow:
 *  1. Test process creates a new PID namespace and forks a wrapper
 *     (PID 1 in the outer namespace).
 *  2. Wrapper forks a child.
 *  3. Wrapper calls unshare(CLONE_NEWUSER) + unshare(CLONE_NEWPID)
 *     to create an inner PID namespace.
 *  4. Wrapper signals the child via pipe.
 *  5. Child opens wrapper's /proc/<pid>/ns/pid_for_children and calls
 *     setns(fd, CLONE_NEWPID) to join the inner namespace.
 *  6. Child calls clone3() with set_tid={1, 1001}.
 *  7. Grandchild verifies its NSpid ends with "1001 1".
 */

pid_t set_tid[] = {1, 1001};

static int pidns_init_via_setns_set_tid_grandchild(struct __test_metadata *_metadata)
{
	char *line = NULL;
	size_t len = 0;
	int found = 0;
	FILE *gf;

	gf = fopen("/proc/self/status", "r");
	ASSERT_NE(gf, NULL);

	while (getline(&line, &len, gf) != -1) {
		if (strncmp(line, "NSpid:", 6) != 0)
			continue;

		for (int i = 0; i < 2; i++) {
			char *last = strrchr(line, '\t');
			pid_t pid;

			ASSERT_NE(last, NULL);
			ASSERT_EQ(sscanf(last, "%d", &pid), 1);
			ASSERT_EQ(pid, set_tid[i]);
			*last = '\0';
		}

		found = true;
		break;
	}

	free(line);
	fclose(gf);
	ASSERT_TRUE(found);
	return 0;
}

static int pidns_init_via_setns_set_tid_child(struct __test_metadata *_metadata,
					      pid_t parent_pid, int pipe_fd[2])
{
	struct __clone_args args = {
		.exit_signal	= SIGCHLD,
		.set_tid	= ptr_to_u64(set_tid),
		.set_tid_size	= 2,
	};
	pid_t grandchild;
	char path[256];
	char buf;
	int nsfd;

	close(pipe_fd[1]);

	ASSERT_EQ(1, read_nointr(pipe_fd[0], &buf, 1));
	close(pipe_fd[0]);

	snprintf(path, sizeof(path),
		 "/proc/%d/ns/pid_for_children", parent_pid);
	nsfd = open(path, O_RDONLY);
	ASSERT_GE(nsfd, 0);

	ASSERT_EQ(0, setns(nsfd, CLONE_NEWPID));
	close(nsfd);

	grandchild = sys_clone3(&args, sizeof(args));
	ASSERT_GE(grandchild, 0);

	if (grandchild == 0)
		_exit(pidns_init_via_setns_set_tid_grandchild(_metadata));

	ASSERT_EQ(0, wait_for_pid(grandchild));
	return 0;
}

static int pidns_init_via_setns_set_tid_wrapper(struct __test_metadata *_metadata)
{
	int pipe_fd[2];
	pid_t child, parent_pid;
	char buf;
	FILE *f;

	/*
	 * We are PID 1 inside the new namespace, but /proc is
	 * mounted from the host.  Read our host-visible PID so
	 * the child can reach our pid_for_children via /proc.
	 */
	f = fopen("/proc/self/stat", "r");
	ASSERT_NE(f, NULL);
	ASSERT_EQ(fscanf(f, "%d", &parent_pid), 1);
	ASSERT_EQ(0, pipe(pipe_fd));

	child = fork();
	ASSERT_GE(child, 0);

	if (child == 0)
		_exit(pidns_init_via_setns_set_tid_child(_metadata, parent_pid, pipe_fd));

	close(pipe_fd[0]);

	ASSERT_EQ(0, unshare(CLONE_NEWUSER));
	ASSERT_EQ(0, unshare(CLONE_NEWPID));

	buf = 0;
	ASSERT_EQ(1, write_nointr(pipe_fd[1], &buf, 1));
	close(pipe_fd[1]);

	ASSERT_EQ(0, wait_for_pid(child));

	fclose(f);
	return 0;
}

TEST(pidns_init_via_setns_set_tid)
{
	pid_t wrapper;

	if (geteuid())
		SKIP(return, "This test needs root to run!");

	ASSERT_EQ(0, unshare(CLONE_NEWPID));

	wrapper = fork();
	ASSERT_GE(wrapper, 0);

	if (wrapper == 0)
		_exit(pidns_init_via_setns_set_tid_wrapper(_metadata));

	ASSERT_EQ(0, wait_for_pid(wrapper));
}

TEST_HARNESS_MAIN
