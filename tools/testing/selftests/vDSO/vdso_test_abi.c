// SPDX-License-Identifier: GPL-2.0
/*
 * vdso_full_test.c: Sample code to test all the timers.
 * Copyright (c) 2019 Arm Ltd.
 *
 * Compile with:
 * gcc -std=gnu99 vdso_full_test.c parse_vdso.c
 *
 */

#include <stdint.h>
#include <elf.h>
#include <stdio.h>
#include <time.h>
#include <sys/auxv.h>
#include <sys/time.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

#include "../kselftest.h"
#include "vdso_config.h"
#include "vdso_call.h"
#include "parse_vdso.h"

static const char *version;
static const char **name;

/* The same as struct __kernel_timespec */
struct vdso_timespec64 {
	uint64_t tv_sec;
	uint64_t tv_nsec;
};

typedef long (*vdso_gettimeofday_t)(struct timeval *tv, struct timezone *tz);
typedef long (*vdso_clock_gettime_t)(clockid_t clk_id, struct timespec *ts);
typedef long (*vdso_clock_gettime64_t)(clockid_t clk_id, struct vdso_timespec64 *ts);
typedef long (*vdso_clock_getres_t)(clockid_t clk_id, struct timespec *ts);
typedef time_t (*vdso_time_t)(time_t *t);

static const char * const vdso_clock_name[] = {
	[CLOCK_REALTIME]		= "CLOCK_REALTIME",
	[CLOCK_MONOTONIC]		= "CLOCK_MONOTONIC",
	[CLOCK_PROCESS_CPUTIME_ID]	= "CLOCK_PROCESS_CPUTIME_ID",
	[CLOCK_THREAD_CPUTIME_ID]	= "CLOCK_THREAD_CPUTIME_ID",
	[CLOCK_MONOTONIC_RAW]		= "CLOCK_MONOTONIC_RAW",
	[CLOCK_REALTIME_COARSE]		= "CLOCK_REALTIME_COARSE",
	[CLOCK_MONOTONIC_COARSE]	= "CLOCK_MONOTONIC_COARSE",
	[CLOCK_BOOTTIME]		= "CLOCK_BOOTTIME",
	[CLOCK_REALTIME_ALARM]		= "CLOCK_REALTIME_ALARM",
	[CLOCK_BOOTTIME_ALARM]		= "CLOCK_BOOTTIME_ALARM",
	[10 /* CLOCK_SGI_CYCLE */]	= "CLOCK_SGI_CYCLE",
	[CLOCK_TAI]			= "CLOCK_TAI",
};

static void vdso_test_gettimeofday(void)
{
	/* Find gettimeofday. */
	vdso_gettimeofday_t vdso_gettimeofday =
		(vdso_gettimeofday_t)vdso_sym(version, name[0]);

	if (!vdso_gettimeofday) {
		ksft_print_msg("Couldn't find %s\n", name[0]);
		ksft_test_result_skip("%s\n", name[0]);
		return;
	}

	struct timeval tv;
	long ret = VDSO_CALL(vdso_gettimeofday, 2, &tv, 0);

	if (ret == 0) {
		ksft_print_msg("The time is %lld.%06lld\n",
			       (long long)tv.tv_sec, (long long)tv.tv_usec);
		ksft_test_result_pass("%s\n", name[0]);
	} else {
		ksft_test_result_fail("%s\n", name[0]);
	}
}

static void vdso_test_clock_gettime64(clockid_t clk_id)
{
	/* Find clock_gettime64. */
	vdso_clock_gettime64_t vdso_clock_gettime64 =
		(vdso_clock_gettime64_t)vdso_sym(version, name[5]);

	if (!vdso_clock_gettime64) {
		ksft_print_msg("Couldn't find %s\n", name[5]);
		ksft_test_result_skip("%s %s\n", name[5],
				      vdso_clock_name[clk_id]);
		return;
	}

	struct vdso_timespec64 ts;
	long ret = VDSO_CALL(vdso_clock_gettime64, 2, clk_id, &ts);

	if (ret == 0) {
		ksft_print_msg("The time is %lld.%06lld\n",
			       (long long)ts.tv_sec, (long long)ts.tv_nsec);
		ksft_test_result_pass("%s %s\n", name[5],
				      vdso_clock_name[clk_id]);
	} else {
		ksft_test_result_fail("%s %s\n", name[5],
				      vdso_clock_name[clk_id]);
	}
}

static void vdso_test_clock_gettime(clockid_t clk_id)
{
	/* Find clock_gettime. */
	vdso_clock_gettime_t vdso_clock_gettime =
		(vdso_clock_gettime_t)vdso_sym(version, name[1]);

	if (!vdso_clock_gettime) {
		ksft_print_msg("Couldn't find %s\n", name[1]);
		ksft_test_result_skip("%s %s\n", name[1],
				      vdso_clock_name[clk_id]);
		return;
	}

	struct timespec ts;
	long ret = VDSO_CALL(vdso_clock_gettime, 2, clk_id, &ts);

	if (ret == 0) {
		ksft_print_msg("The time is %lld.%06lld\n",
			       (long long)ts.tv_sec, (long long)ts.tv_nsec);
		ksft_test_result_pass("%s %s\n", name[1],
				      vdso_clock_name[clk_id]);
	} else {
		ksft_test_result_fail("%s %s\n", name[1],
				      vdso_clock_name[clk_id]);
	}
}

static void vdso_test_time(void)
{
	/* Find time. */
	vdso_time_t vdso_time =
		(vdso_time_t)vdso_sym(version, name[2]);

	if (!vdso_time) {
		ksft_print_msg("Couldn't find %s\n", name[2]);
		ksft_test_result_skip("%s\n", name[2]);
		return;
	}

	long ret = VDSO_CALL(vdso_time, 1, NULL);

	if (ret > 0) {
		ksft_print_msg("The time in hours since January 1, 1970 is %lld\n",
				(long long)(ret / 3600));
		ksft_test_result_pass("%s\n", name[2]);
	} else {
		ksft_test_result_fail("%s\n", name[2]);
	}
}

static void vdso_test_clock_getres(clockid_t clk_id)
{
	int clock_getres_fail = 0;

	/* Find clock_getres. */
	vdso_clock_getres_t vdso_clock_getres =
		(vdso_clock_getres_t)vdso_sym(version, name[3]);

	if (!vdso_clock_getres) {
		ksft_print_msg("Couldn't find %s\n", name[3]);
		ksft_test_result_skip("%s %s\n", name[3],
				      vdso_clock_name[clk_id]);
		return;
	}

	struct timespec ts, sys_ts;
	long ret = VDSO_CALL(vdso_clock_getres, 2, clk_id, &ts);

	if (ret == 0) {
		ksft_print_msg("The vdso resolution is %lld %lld\n",
			       (long long)ts.tv_sec, (long long)ts.tv_nsec);
	} else {
		clock_getres_fail++;
	}

	ret = syscall(SYS_clock_getres, clk_id, &sys_ts);

	ksft_print_msg("The syscall resolution is %lld %lld\n",
			(long long)sys_ts.tv_sec, (long long)sys_ts.tv_nsec);

	if ((sys_ts.tv_sec != ts.tv_sec) || (sys_ts.tv_nsec != ts.tv_nsec))
		clock_getres_fail++;

	if (clock_getres_fail > 0) {
		ksft_test_result_fail("%s %s\n", name[3],
				      vdso_clock_name[clk_id]);
	} else {
		ksft_test_result_pass("%s %s\n", name[3],
				      vdso_clock_name[clk_id]);
	}
}

/*
 * This function calls vdso_test_clock_gettime and vdso_test_clock_getres
 * with different values for clock_id.
 */
static inline void vdso_test_clock(clockid_t clock_id)
{
	ksft_print_msg("clock_id: %s\n", vdso_clock_name[clock_id]);

	vdso_test_clock_gettime(clock_id);
	vdso_test_clock_gettime64(clock_id);

	vdso_test_clock_getres(clock_id);
}

#define VDSO_TEST_PLAN	29

int main(int argc, char **argv)
{
	unsigned long sysinfo_ehdr = getauxval(AT_SYSINFO_EHDR);

	ksft_print_header();

	if (!sysinfo_ehdr)
		ksft_exit_skip("AT_SYSINFO_EHDR is not present!\n");

	ksft_set_plan(VDSO_TEST_PLAN);

	version = versions[VDSO_VERSION];
	name = (const char **)&names[VDSO_NAMES];

	ksft_print_msg("[vDSO kselftest] VDSO_VERSION: %s\n", version);

	vdso_init_from_sysinfo_ehdr(getauxval(AT_SYSINFO_EHDR));

	vdso_test_gettimeofday();

	vdso_test_clock(CLOCK_REALTIME);
	vdso_test_clock(CLOCK_BOOTTIME);
	vdso_test_clock(CLOCK_TAI);
	vdso_test_clock(CLOCK_REALTIME_COARSE);
	vdso_test_clock(CLOCK_MONOTONIC);
	vdso_test_clock(CLOCK_MONOTONIC_RAW);
	vdso_test_clock(CLOCK_MONOTONIC_COARSE);
	vdso_test_clock(CLOCK_PROCESS_CPUTIME_ID);
	vdso_test_clock(CLOCK_THREAD_CPUTIME_ID);

	vdso_test_time();

	ksft_finished();
}
