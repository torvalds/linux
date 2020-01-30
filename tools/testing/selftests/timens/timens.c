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
#include <time.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "log.h"
#include "timens.h"

/*
 * Test shouldn't be run for a day, so add 10 days to child
 * time and check parent's time to be in the same day.
 */
#define DAY_IN_SEC			(60*60*24)
#define TEN_DAYS_IN_SEC			(10*DAY_IN_SEC)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct test_clock {
	clockid_t id;
	char *name;
	/*
	 * off_id is -1 if a clock has own offset, or it contains an index
	 * which contains a right offset of this clock.
	 */
	int off_id;
	time_t offset;
};

#define ct(clock, off_id)	{ clock, #clock, off_id }
static struct test_clock clocks[] = {
	ct(CLOCK_BOOTTIME, -1),
	ct(CLOCK_BOOTTIME_ALARM, 1),
	ct(CLOCK_MONOTONIC, -1),
	ct(CLOCK_MONOTONIC_COARSE, 1),
	ct(CLOCK_MONOTONIC_RAW, 1),
};
#undef ct

static int child_ns, parent_ns = -1;

static int switch_ns(int fd)
{
	if (setns(fd, CLONE_NEWTIME)) {
		pr_perror("setns()");
		return -1;
	}

	return 0;
}

static int init_namespaces(void)
{
	char path[] = "/proc/self/ns/time_for_children";
	struct stat st1, st2;

	if (parent_ns == -1) {
		parent_ns = open(path, O_RDONLY);
		if (parent_ns <= 0)
			return pr_perror("Unable to open %s", path);
	}

	if (fstat(parent_ns, &st1))
		return pr_perror("Unable to stat the parent timens");

	if (unshare_timens())
		return  -1;

	child_ns = open(path, O_RDONLY);
	if (child_ns <= 0)
		return pr_perror("Unable to open %s", path);

	if (fstat(child_ns, &st2))
		return pr_perror("Unable to stat the timens");

	if (st1.st_ino == st2.st_ino)
		return pr_perror("The same child_ns after CLONE_NEWTIME");

	return 0;
}

static int test_gettime(clockid_t clock_index, bool raw_syscall, time_t offset)
{
	struct timespec child_ts_new, parent_ts_old, cur_ts;
	char *entry = raw_syscall ? "syscall" : "vdso";
	double precision = 0.0;

	if (check_skip(clocks[clock_index].id))
		return 0;

	switch (clocks[clock_index].id) {
	case CLOCK_MONOTONIC_COARSE:
	case CLOCK_MONOTONIC_RAW:
		precision = -2.0;
		break;
	}

	if (switch_ns(parent_ns))
		return pr_err("switch_ns(%d)", child_ns);

	if (_gettime(clocks[clock_index].id, &parent_ts_old, raw_syscall))
		return -1;

	child_ts_new.tv_nsec = parent_ts_old.tv_nsec;
	child_ts_new.tv_sec = parent_ts_old.tv_sec + offset;

	if (switch_ns(child_ns))
		return pr_err("switch_ns(%d)", child_ns);

	if (_gettime(clocks[clock_index].id, &cur_ts, raw_syscall))
		return -1;

	if (difftime(cur_ts.tv_sec, child_ts_new.tv_sec) < precision) {
		ksft_test_result_fail(
			"Child's %s (%s) time has not changed: %lu -> %lu [%lu]\n",
			clocks[clock_index].name, entry, parent_ts_old.tv_sec,
			child_ts_new.tv_sec, cur_ts.tv_sec);
		return -1;
	}

	if (switch_ns(parent_ns))
		return pr_err("switch_ns(%d)", parent_ns);

	if (_gettime(clocks[clock_index].id, &cur_ts, raw_syscall))
		return -1;

	if (difftime(cur_ts.tv_sec, parent_ts_old.tv_sec) > DAY_IN_SEC) {
		ksft_test_result_fail(
			"Parent's %s (%s) time has changed: %lu -> %lu [%lu]\n",
			clocks[clock_index].name, entry, parent_ts_old.tv_sec,
			child_ts_new.tv_sec, cur_ts.tv_sec);
		/* Let's play nice and put it closer to original */
		clock_settime(clocks[clock_index].id, &cur_ts);
		return -1;
	}

	ksft_test_result_pass("Passed for %s (%s)\n",
				clocks[clock_index].name, entry);
	return 0;
}

int main(int argc, char *argv[])
{
	unsigned int i;
	time_t offset;
	int ret = 0;

	nscheck();

	check_config_posix_timers();

	ksft_set_plan(ARRAY_SIZE(clocks) * 2);

	if (init_namespaces())
		return 1;

	/* Offsets have to be set before tasks enter the namespace. */
	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		if (clocks[i].off_id != -1)
			continue;
		offset = TEN_DAYS_IN_SEC + i * 1000;
		clocks[i].offset = offset;
		if (_settime(clocks[i].id, offset))
			return 1;
	}

	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		if (clocks[i].off_id != -1)
			offset = clocks[clocks[i].off_id].offset;
		else
			offset = clocks[i].offset;
		ret |= test_gettime(i, true, offset);
		ret |= test_gettime(i, false, offset);
	}

	if (ret)
		ksft_exit_fail();

	ksft_exit_pass();
	return !!ret;
}
