// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "log.h"
#include "timens.h"

#define OFFSET (36000)

struct thread_args {
	char *tst_name;
	struct timespec *now;
};

static void *tcheck(void *_args)
{
	struct thread_args *args = _args;
	struct timespec *now = args->now, tst;
	int i;

	for (i = 0; i < 2; i++) {
		_gettime(CLOCK_MONOTONIC, &tst, i);
		if (labs(tst.tv_sec - now->tv_sec) > 5) {
			pr_fail("%s: in-thread: unexpected value: %ld (%ld)\n",
				args->tst_name, tst.tv_sec, now->tv_sec);
			return (void *)1UL;
		}
	}
	return NULL;
}

static int check_in_thread(char *tst_name, struct timespec *now)
{
	struct thread_args args = {
		.tst_name = tst_name,
		.now = now,
	};
	pthread_t th;
	void *retval;

	if (pthread_create(&th, NULL, tcheck, &args))
		return pr_perror("thread");
	if (pthread_join(th, &retval))
		return pr_perror("pthread_join");
	return !(retval == NULL);
}

static int check(char *tst_name, struct timespec *now)
{
	struct timespec tst;
	int i;

	for (i = 0; i < 2; i++) {
		_gettime(CLOCK_MONOTONIC, &tst, i);
		if (labs(tst.tv_sec - now->tv_sec) > 5)
			return pr_fail("%s: unexpected value: %ld (%ld)\n",
					tst_name, tst.tv_sec, now->tv_sec);
	}
	if (check_in_thread(tst_name, now))
		return 1;
	ksft_test_result_pass("%s\n", tst_name);
	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec now;
	int status;
	pid_t pid;

	if (argc > 1) {
		char *endptr;

		ksft_cnt.ksft_pass = 1;
		now.tv_sec = strtoul(argv[1], &endptr, 0);
		if (*endptr != 0)
			return pr_perror("strtoul");

		return check("child after exec", &now);
	}

	nscheck();

	ksft_set_plan(4);

	clock_gettime(CLOCK_MONOTONIC, &now);

	if (unshare_timens())
		return 1;

	if (_settime(CLOCK_MONOTONIC, OFFSET))
		return 1;

	if (check("parent before vfork", &now))
		return 1;

	pid = vfork();
	if (pid < 0)
		return pr_perror("fork");

	if (pid == 0) {
		char now_str[64];
		char *cargv[] = {"exec", now_str, NULL};
		char *cenv[] = {NULL};

		/* Check for proper vvar offsets after execve. */
		snprintf(now_str, sizeof(now_str), "%ld", now.tv_sec + OFFSET);
		execve("/proc/self/exe", cargv, cenv);
		pr_perror("execve");
		_exit(1);
	}

	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("waitpid");

	if (status)
		ksft_exit_fail();
	ksft_inc_pass_cnt();
	ksft_test_result_pass("wait for child\n");

	/* Check that we are still in the source timens. */
	if (check("parent after vfork", &now))
		return 1;

	ksft_exit_pass();
	return 0;
}
