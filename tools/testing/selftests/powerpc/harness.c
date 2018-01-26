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

#define TIMEOUT		120
#define KILL_TIMEOUT	5


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
	alarm(TIMEOUT);
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

static void alarm_handler(int signum)
{
	/* Jut wake us up from waitpid */
}

static struct sigaction alarm_action = {
	.sa_handler = alarm_handler,
};

int test_harness(int (test_function)(void), char *name)
{
	int rc;

	test_start(name);
	test_set_git_version(GIT_VERSION);

	if (sigaction(SIGALRM, &alarm_action, NULL)) {
		perror("sigaction");
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

static char auxv[4096];

void *get_auxv_entry(int type)
{
	ElfW(auxv_t) *p;
	void *result;
	ssize_t num;
	int fd;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd == -1) {
		perror("open");
		return NULL;
	}

	result = NULL;

	num = read(fd, auxv, sizeof(auxv));
	if (num < 0) {
		perror("read");
		goto out;
	}

	if (num > sizeof(auxv)) {
		printf("Overflowed auxv buffer\n");
		goto out;
	}

	p = (ElfW(auxv_t) *)auxv;

	while (p->a_type != AT_NULL) {
		if (p->a_type == type) {
			result = (void *)p->a_un.a_val;
			break;
		}

		p++;
	}
out:
	close(fd);
	return result;
}
