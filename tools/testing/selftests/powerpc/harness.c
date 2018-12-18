/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/stat.h>

#include "subunit.h"
#include "utils.h"

#define KILL_TIMEOUT	5

static uint64_t timeout = 120;

int run_test(int (test_function)(void), char *name)
{
	bool terminated;
	int rc, status;
	pid_t pid;

	/* Make sure output is flushed before forking */
	fflush(stdout);

	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		exit(test_function());
	} else if (pid == -1) {
		perror("fork");
		return 1;
	}

	setpgid(pid, pid);

	/* Wake us up in timeout seconds */
	alarm(timeout);
	terminated = false;

wait:
	rc = waitpid(pid, &status, 0);
	if (rc == -1) {
		if (errno != EINTR) {
			printf("unknown error from waitpid\n");
			return 1;
		}

		if (terminated) {
			printf("!! force killing %s\n", name);
			kill(-pid, SIGKILL);
			return 1;
		} else {
			printf("!! killing %s\n", name);
			kill(-pid, SIGTERM);
			terminated = true;
			alarm(KILL_TIMEOUT);
			goto wait;
		}
	}

	/* Kill anything else in the process group that is still running */
	kill(-pid, SIGTERM);

	if (WIFEXITED(status))
		status = WEXITSTATUS(status);
	else {
		if (WIFSIGNALED(status))
			printf("!! child died by signal %d\n", WTERMSIG(status));
		else
			printf("!! child died by unknown cause\n");

		status = 1; /* Signal or other */
	}

	return status;
}

static void sig_handler(int signum)
{
	/* Just wake us up from waitpid */
}

static struct sigaction sig_action = {
	.sa_handler = sig_handler,
};

void test_harness_set_timeout(uint64_t time)
{
	timeout = time;
}

int test_harness(int (test_function)(void), char *name)
{
	int rc;

	test_start(name);
	test_set_git_version(GIT_VERSION);

	if (sigaction(SIGINT, &sig_action, NULL)) {
		perror("sigaction (sigint)");
		test_error(name);
		return 1;
	}

	if (sigaction(SIGALRM, &sig_action, NULL)) {
		perror("sigaction (sigalrm)");
		test_error(name);
		return 1;
	}

	rc = run_test(test_function, name);

	if (rc == MAGIC_SKIP_RETURN_VALUE) {
		test_skip(name);
		/* so that skipped test is not marked as failed */
		rc = 0;
	} else
		test_finish(name, rc);

	return rc;
}
