// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CLONE_PIDFD
#define CLONE_PIDFD 0x00001000
#endif

static int do_child(void *args)
{
	printf("%d\n", getpid());
	_exit(EXIT_SUCCESS);
}

static pid_t pidfd_clone(int flags, int *pidfd)
{
	size_t stack_size = 1024;
	char *stack[1024] = { 0 };

#ifdef __ia64__
	return __clone2(do_child, stack, stack_size, flags | SIGCHLD, NULL, pidfd);
#else
	return clone(do_child, stack + stack_size, flags | SIGCHLD, NULL, pidfd);
#endif
}

static inline int sys_pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
					unsigned int flags)
{
	return syscall(__NR_pidfd_send_signal, pidfd, sig, info, flags);
}

static int pidfd_metadata_fd(pid_t pid, int pidfd)
{
	int procfd, ret;
	char path[100];

	snprintf(path, sizeof(path), "/proc/%d", pid);
	procfd = open(path, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	if (procfd < 0) {
		warn("Failed to open %s\n", path);
		return -1;
	}

	/*
	 * Verify that the pid has not been recycled and our /proc/<pid> handle
	 * is still valid.
	 */
	ret = sys_pidfd_send_signal(pidfd, 0, NULL, 0);
	if (ret < 0) {
		switch (errno) {
		case EPERM:
			/* Process exists, just not allowed to signal it. */
			break;
		default:
			warn("Failed to signal process\n");
			close(procfd);
			procfd = -1;
		}
	}

	return procfd;
}

int main(int argc, char *argv[])
{
	int pidfd = 0, ret = EXIT_FAILURE;
	char buf[4096] = { 0 };
	pid_t pid;
	int procfd, statusfd;
	ssize_t bytes;

	pid = pidfd_clone(CLONE_PIDFD, &pidfd);
	if (pid < 0)
		exit(ret);

	procfd = pidfd_metadata_fd(pid, pidfd);
	close(pidfd);
	if (procfd < 0)
		goto out;

	statusfd = openat(procfd, "status", O_RDONLY | O_CLOEXEC);
	close(procfd);
	if (statusfd < 0)
		goto out;

	bytes = read(statusfd, buf, sizeof(buf));
	if (bytes > 0)
		bytes = write(STDOUT_FILENO, buf, bytes);
	close(statusfd);
	ret = EXIT_SUCCESS;

out:
	(void)wait(NULL);

	exit(ret);
}
