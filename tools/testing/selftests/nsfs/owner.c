// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#define NSIO    0xb7
#define NS_GET_USERNS   _IO(NSIO, 0x1)

#define pr_err(fmt, ...) \
		({ \
			fprintf(stderr, "%s:%d:" fmt ": %m\n", \
				__func__, __LINE__, ##__VA_ARGS__); \
			1; \
		})

int main(int argc, char *argvp[])
{
	int pfd[2], ns, uns, init_uns;
	struct stat st1, st2;
	char path[128];
	pid_t pid;
	char c;

	if (pipe(pfd))
		return 1;

	pid = fork();
	if (pid < 0)
		return pr_err("fork");
	if (pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (unshare(CLONE_NEWUTS | CLONE_NEWUSER))
			return pr_err("unshare");
		close(pfd[0]);
		close(pfd[1]);
		while (1)
			sleep(1);
		return 0;
	}
	close(pfd[1]);
	if (read(pfd[0], &c, 1) != 0)
		return pr_err("Unable to read from pipe");
	close(pfd[0]);

	snprintf(path, sizeof(path), "/proc/%d/ns/uts", pid);
	ns = open(path, O_RDONLY);
	if (ns < 0)
		return pr_err("Unable to open %s", path);

	uns = ioctl(ns, NS_GET_USERNS);
	if (uns < 0)
		return pr_err("Unable to get an owning user namespace");

	if (fstat(uns, &st1))
		return pr_err("fstat");

	snprintf(path, sizeof(path), "/proc/%d/ns/user", pid);
	if (stat(path, &st2))
		return pr_err("stat");

	if (st1.st_ino != st2.st_ino)
		return pr_err("NS_GET_USERNS returned a wrong namespace");

	init_uns = ioctl(uns, NS_GET_USERNS);
	if (uns < 0)
		return pr_err("Unable to get an owning user namespace");

	if (ioctl(init_uns, NS_GET_USERNS) >= 0 || errno != EPERM)
		return pr_err("Don't get EPERM");

	if (unshare(CLONE_NEWUSER))
		return pr_err("unshare");

	if (ioctl(ns, NS_GET_USERNS) >= 0 || errno != EPERM)
		return pr_err("Don't get EPERM");
	if (ioctl(init_uns, NS_GET_USERNS) >= 0 || errno != EPERM)
		return pr_err("Don't get EPERM");

	kill(pid, SIGKILL);
	wait(NULL);
	return 0;
}
