// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <dlfcn.h>

#include "log.h"
#include "timens.h"

typedef int (*vgettime_t)(clockid_t, struct timespec *);

vgettime_t vdso_clock_gettime;

static void fill_function_pointers(void)
{
	void *vdso = dlopen("linux-vdso.so.1",
			    RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso)
		vdso = dlopen("linux-gate.so.1",
			      RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso)
		vdso = dlopen("linux-vdso32.so.1",
			      RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso)
		vdso = dlopen("linux-vdso64.so.1",
			      RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso) {
		pr_err("[WARN]\tfailed to find vDSO\n");
		return;
	}

	vdso_clock_gettime = (vgettime_t)dlsym(vdso, "__vdso_clock_gettime");
	if (!vdso_clock_gettime)
		vdso_clock_gettime = (vgettime_t)dlsym(vdso, "__kernel_clock_gettime");
	if (!vdso_clock_gettime)
		pr_err("Warning: failed to find clock_gettime in vDSO\n");

}

static void test(clock_t clockid, char *clockstr, bool in_ns)
{
	struct timespec tp, start;
	long i = 0;
	const int timeout = 3;

	vdso_clock_gettime(clockid, &start);
	tp = start;
	for (tp = start; start.tv_sec + timeout > tp.tv_sec ||
			 (start.tv_sec + timeout == tp.tv_sec &&
			  start.tv_nsec > tp.tv_nsec); i++) {
		vdso_clock_gettime(clockid, &tp);
	}

	ksft_test_result_pass("%s:\tclock: %10s\tcycles:\t%10ld\n",
			      in_ns ? "ns" : "host", clockstr, i);
}

int main(int argc, char *argv[])
{
	time_t offset = 10;
	int nsfd;

	ksft_print_header();

	ksft_set_plan(8);

	fill_function_pointers();

	test(CLOCK_MONOTONIC, "monotonic", false);
	test(CLOCK_MONOTONIC_COARSE, "monotonic-coarse", false);
	test(CLOCK_MONOTONIC_RAW, "monotonic-raw", false);
	test(CLOCK_BOOTTIME, "boottime", false);

	nscheck();

	if (unshare_timens())
		return 1;

	nsfd = open("/proc/self/ns/time_for_children", O_RDONLY);
	if (nsfd < 0)
		return pr_perror("Can't open a time namespace");

	if (_settime(CLOCK_MONOTONIC, offset))
		return 1;
	if (_settime(CLOCK_BOOTTIME, offset))
		return 1;

	if (setns(nsfd, CLONE_NEWTIME))
		return pr_perror("setns");

	test(CLOCK_MONOTONIC, "monotonic", true);
	test(CLOCK_MONOTONIC_COARSE, "monotonic-coarse", true);
	test(CLOCK_MONOTONIC_RAW, "monotonic-raw", true);
	test(CLOCK_BOOTTIME, "boottime", true);

	ksft_exit_pass();
	return 0;
}
