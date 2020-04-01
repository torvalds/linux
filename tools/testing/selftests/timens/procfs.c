// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <time.h>

#include "log.h"
#include "timens.h"

/*
 * Test shouldn't be run for a day, so add 10 days to child
 * time and check parent's time to be in the same day.
 */
#define MAX_TEST_TIME_SEC		(60*5)
#define DAY_IN_SEC			(60*60*24)
#define TEN_DAYS_IN_SEC			(10*DAY_IN_SEC)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int child_ns, parent_ns;

static int switch_ns(int fd)
{
	if (setns(fd, CLONE_NEWTIME))
		return pr_perror("setns()");

	return 0;
}

static int init_namespaces(void)
{
	char path[] = "/proc/self/ns/time_for_children";
	struct stat st1, st2;

	parent_ns = open(path, O_RDONLY);
	if (parent_ns <= 0)
		return pr_perror("Unable to open %s", path);

	if (fstat(parent_ns, &st1))
		return pr_perror("Unable to stat the parent timens");

	if (unshare_timens())
		return -1;

	child_ns = open(path, O_RDONLY);
	if (child_ns <= 0)
		return pr_perror("Unable to open %s", path);

	if (fstat(child_ns, &st2))
		return pr_perror("Unable to stat the timens");

	if (st1.st_ino == st2.st_ino)
		return pr_err("The same child_ns after CLONE_NEWTIME");

	if (_settime(CLOCK_BOOTTIME, TEN_DAYS_IN_SEC))
		return -1;

	return 0;
}

static int read_proc_uptime(struct timespec *uptime)
{
	unsigned long up_sec, up_nsec;
	FILE *proc;

	proc = fopen("/proc/uptime", "r");
	if (proc == NULL) {
		pr_perror("Unable to open /proc/uptime");
		return -1;
	}

	if (fscanf(proc, "%lu.%02lu", &up_sec, &up_nsec) != 2) {
		if (errno) {
			pr_perror("fscanf");
			return -errno;
		}
		pr_err("failed to parse /proc/uptime");
		return -1;
	}
	fclose(proc);

	uptime->tv_sec = up_sec;
	uptime->tv_nsec = up_nsec;
	return 0;
}

static int check_uptime(void)
{
	struct timespec uptime_new, uptime_old;
	time_t uptime_expected;
	double prec = MAX_TEST_TIME_SEC;

	if (switch_ns(parent_ns))
		return pr_err("switch_ns(%d)", parent_ns);

	if (read_proc_uptime(&uptime_old))
		return 1;

	if (switch_ns(child_ns))
		return pr_err("switch_ns(%d)", child_ns);

	if (read_proc_uptime(&uptime_new))
		return 1;

	uptime_expected = uptime_old.tv_sec + TEN_DAYS_IN_SEC;
	if (fabs(difftime(uptime_new.tv_sec, uptime_expected)) > prec) {
		pr_fail("uptime in /proc/uptime: old %ld, new %ld [%ld]",
			uptime_old.tv_sec, uptime_new.tv_sec,
			uptime_old.tv_sec + TEN_DAYS_IN_SEC);
		return 1;
	}

	ksft_test_result_pass("Passed for /proc/uptime\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	nscheck();

	ksft_set_plan(1);

	if (init_namespaces())
		return 1;

	ret |= check_uptime();

	if (ret)
		ksft_exit_fail();
	ksft_exit_pass();
	return ret;
}
