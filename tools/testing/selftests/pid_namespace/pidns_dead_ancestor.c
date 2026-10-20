// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kselftest_harness.h"

#define STACK_SIZE (1024 * 1024)

struct pidns_init_args {
	int ready_read;
	int ready_write;
	int release_read;
	int release_write;
};

static int pidns_init(void *data)
{
	struct pidns_init_args *args = data;
	char release;
	int error = 0;

	close(args->ready_read);
	close(args->release_write);
	if (unshare(CLONE_NEWPID))
		error = errno;
	if (write(args->ready_write, &error, sizeof(error)) != sizeof(error))
		return 1;
	close(args->ready_write);
	if (error)
		return 0;
	if (read(args->release_read, &release, sizeof(release)) != sizeof(release))
		return 1;
	close(args->release_read);
	return 0;
}

static int create_unborn_pidns(void)
{
	struct pidns_init_args args;
	char path[64], release = 0;
	void *stack;
	pid_t child;
	int ready[2], release_pipe[2], error, status, nsfd = -1;
	ssize_t n;

	if (pipe(ready))
		return -1;
	if (pipe(release_pipe)) {
		error = errno;
		close(ready[0]);
		close(ready[1]);
		errno = error;
		return -1;
	}

	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (stack == MAP_FAILED) {
		error = errno;
		close(ready[0]);
		close(ready[1]);
		close(release_pipe[0]);
		close(release_pipe[1]);
		errno = error;
		return -1;
	}

	args = (struct pidns_init_args) {
		.ready_read = ready[0],
		.ready_write = ready[1],
		.release_read = release_pipe[0],
		.release_write = release_pipe[1],
	};
	child = clone(pidns_init, stack + STACK_SIZE, CLONE_NEWPID | SIGCHLD,
		      &args);
	if (child < 0)
		goto err_munmap;

	close(ready[1]);
	close(release_pipe[0]);
	n = read(ready[0], &error, sizeof(error));
	if (n != sizeof(error)) {
		error = n < 0 ? errno : EIO;
		goto release_child;
	}
	if (error)
		goto release_child;

	snprintf(path, sizeof(path), "/proc/%d/ns/pid_for_children", child);
	nsfd = open(path, O_RDONLY | O_CLOEXEC);
	if (nsfd < 0) {
		error = errno;
		goto release_child;
	}

release_child:
	write(release_pipe[1], &release, sizeof(release));
	close(release_pipe[1]);
	close(ready[0]);
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status)) {
		if (!error)
			error = ECHILD;
		if (nsfd >= 0)
			close(nsfd);
		nsfd = -1;
	}
	munmap(stack, STACK_SIZE);
	if (error) {
		errno = error;
		return -1;
	}
	return nsfd;

err_munmap:
	error = errno;
	munmap(stack, STACK_SIZE);
	close(ready[0]);
	close(ready[1]);
	close(release_pipe[0]);
	close(release_pipe[1]);
	errno = error;
	return -1;
}

TEST(pidns_dead_ancestor_rejects_fork)
{
	int original_fd, target_fd, fork_errno, ret;
	pid_t child;

	target_fd = create_unborn_pidns();
	if (target_fd < 0) {
		if (errno == ENOENT)
			SKIP(return, "PID namespace handle unavailable through procfs\n");
		if (errno == EPERM || errno == EACCES || errno == EINVAL)
			SKIP(return, "PID namespace creation requires privileges\n");
		ASSERT_GE(target_fd, 0);
	}

	original_fd = open("/proc/self/ns/pid_for_children", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(original_fd, 0);
	ret = setns(target_fd, CLONE_NEWPID);
	if (ret) {
		if (errno == EPERM || errno == EACCES)
			SKIP(return, "setns into PID namespace requires privileges\n");
	}
	ASSERT_EQ(0, ret);

	child = fork();
	fork_errno = errno;
	if (child == 0)
		_exit(0);

	if (child > 0)
		ASSERT_EQ(child, waitpid(child, NULL, 0));
	ret = setns(original_fd, CLONE_NEWPID);
	close(original_fd);
	close(target_fd);
	ASSERT_EQ(0, ret);

	ASSERT_EQ(-1, child);
	ASSERT_EQ(ENOMEM, fork_errno);
}

TEST_HARNESS_MAIN
