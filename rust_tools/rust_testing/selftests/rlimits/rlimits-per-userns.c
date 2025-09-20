// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Alexey Gladkov <gladkov.alexey@gmail.com>
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#define NR_CHILDS 2

static char *service_prog;
static uid_t user   = 60000;
static uid_t group  = 60000;

static void setrlimit_nproc(rlim_t n)
{
	pid_t pid = getpid();
	struct rlimit limit = {
		.rlim_cur = n,
		.rlim_max = n
	};

	warnx("(pid=%d): Setting RLIMIT_NPROC=%ld", pid, n);

	if (setrlimit(RLIMIT_NPROC, &limit) < 0)
		err(EXIT_FAILURE, "(pid=%d): setrlimit(RLIMIT_NPROC)", pid);
}

static pid_t fork_child(void)
{
	pid_t pid = fork();

	if (pid < 0)
		err(EXIT_FAILURE, "fork");

	if (pid > 0)
		return pid;

	pid = getpid();

	warnx("(pid=%d): New process starting ...", pid);

	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0)
		err(EXIT_FAILURE, "(pid=%d): prctl(PR_SET_PDEATHSIG)", pid);

	signal(SIGUSR1, SIG_DFL);

	warnx("(pid=%d): Changing to uid=%d, gid=%d", pid, user, group);

	if (setgid(group) < 0)
		err(EXIT_FAILURE, "(pid=%d): setgid(%d)", pid, group);
	if (setuid(user) < 0)
		err(EXIT_FAILURE, "(pid=%d): setuid(%d)", pid, user);

	warnx("(pid=%d): Service running ...", pid);

	warnx("(pid=%d): Unshare user namespace", pid);
	if (unshare(CLONE_NEWUSER) < 0)
		err(EXIT_FAILURE, "unshare(CLONE_NEWUSER)");

	char *const argv[] = { "service", NULL };
	char *const envp[] = { "I_AM_SERVICE=1", NULL };

	warnx("(pid=%d): Executing real service ...", pid);

	execve(service_prog, argv, envp);
	err(EXIT_FAILURE, "(pid=%d): execve", pid);
}

int main(int argc, char **argv)
{
	size_t i;
	pid_t child[NR_CHILDS];
	int wstatus[NR_CHILDS];
	int childs = NR_CHILDS;
	pid_t pid;

	if (getenv("I_AM_SERVICE")) {
		pause();
		exit(EXIT_SUCCESS);
	}

	service_prog = argv[0];
	pid = getpid();

	warnx("(pid=%d) Starting testcase", pid);

	/*
	 * This rlimit is not a problem for root because it can be exceeded.
	 */
	setrlimit_nproc(1);

	for (i = 0; i < NR_CHILDS; i++) {
		child[i] = fork_child();
		wstatus[i] = 0;
		usleep(250000);
	}

	while (1) {
		for (i = 0; i < NR_CHILDS; i++) {
			if (child[i] <= 0)
				continue;

			errno = 0;
			pid_t ret = waitpid(child[i], &wstatus[i], WNOHANG);

			if (!ret || (!WIFEXITED(wstatus[i]) && !WIFSIGNALED(wstatus[i])))
				continue;

			if (ret < 0 && errno != ECHILD)
				warn("(pid=%d): waitpid(%d)", pid, child[i]);

			child[i] *= -1;
			childs -= 1;
		}

		if (!childs)
			break;

		usleep(250000);

		for (i = 0; i < NR_CHILDS; i++) {
			if (child[i] <= 0)
				continue;
			kill(child[i], SIGUSR1);
		}
	}

	for (i = 0; i < NR_CHILDS; i++) {
		if (WIFEXITED(wstatus[i]))
			warnx("(pid=%d): pid %d exited, status=%d",
				pid, -child[i], WEXITSTATUS(wstatus[i]));
		else if (WIFSIGNALED(wstatus[i]))
			warnx("(pid=%d): pid %d killed by signal %d",
				pid, -child[i], WTERMSIG(wstatus[i]));

		if (WIFSIGNALED(wstatus[i]) && WTERMSIG(wstatus[i]) == SIGUSR1)
			continue;

		warnx("(pid=%d): Test failed", pid);
		exit(EXIT_FAILURE);
	}

	warnx("(pid=%d): Test passed", pid);
	exit(EXIT_SUCCESS);
}
