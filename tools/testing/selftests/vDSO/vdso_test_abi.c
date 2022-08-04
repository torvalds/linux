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

extern void *vdso_sym(const char *version, const char *name);
extern void vdso_init_from_sysinfo_ehdr(uintptr_t base);
extern void vdso_init_from_auxv(void *auxv);

static const char *version;
static const char **name;

typedef long (*vdso_gettimeofday_t)(struct timeval *tv, struct timezone *tz);
typedef long (*vdso_clock_gettime_t)(clockid_t clk_id, struct timespec *ts);
typedef long (*vdso_clock_getres_t)(clockid_t clk_id, struct timespec *ts);
typedef time_t (*vdso_time_t)(time_t *t);

static int vdso_test_gettimeofday(void)
{
	/* Find gettimeofday. */
	vdso_gettimeofday_t vdso_gettimeofday =
		(vdso_gettimeofday_t)vdso_sym(version, name[0]);

	if (!vdso_gettimeofday) {
		printf("Could not find %s\n", name[0]);
		return KSFT_SKIP;
	}

	struct timeval tv;
	long ret = vdso_gettimeofday(&tv, 0);

	if (ret == 0) {
		printf("The time is %lld.%06lld\n",
		       (long long)tv.tv_sec, (long long)tv.tv_usec);
	} else {
		printf("%s failed\n", name[0]);
		return KSFT_FAIL;
	}

	return KSFT_PASS;
}

static int vdso_test_clock_gettime(clockid_t clk_id)
{
	/* Find clock_gettime. */
	vdso_clock_gettime_t vdso_clock_gettime =
		(vdso_clock_gettime_t)vdso_sym(version, name[1]);

	if (!vdso_clock_gettime) {
		printf("Could not find %s\n", name[1]);
		return KSFT_SKIP;
	}

	struct timespec ts;
	long ret = vdso_clock_gettime(clk_id, &ts);

	if (ret == 0) {
		printf("The time is %lld.%06lld\n",
		       (long long)ts.tv_sec, (long long)ts.tv_nsec);
	} else {
		printf("%s failed\n", name[1]);
		return KSFT_FAIL;
	}

	return KSFT_PASS;
}

static int vdso_test_time(void)
{
	/* Find time. */
	vdso_time_t vdso_time =
		(vdso_time_t)vdso_sym(version, name[2]);

	if (!vdso_time) {
		printf("Could not find %s\n", name[2]);
		return KSFT_SKIP;
	}

	long ret = vdso_time(NULL);

	if (ret > 0) {
		printf("The time in hours since January 1, 1970 is %lld\n",
				(long long)(ret / 3600));
	} else {
		printf("%s failed\n", name[2]);
		return KSFT_FAIL;
	}

	return KSFT_PASS;
}

static int vdso_test_clock_getres(clockid_t clk_id)
{
	/* Find clock_getres. */
	vdso_clock_getres_t vdso_clock_getres =
		(vdso_clock_getres_t)vdso_sym(version, name[3]);

	if (!vdso_clock_getres) {
		printf("Could not find %s\n", name[3]);
		return KSFT_SKIP;
	}

	struct timespec ts, sys_ts;
	long ret = vdso_clock_getres(clk_id, &ts);

	if (ret == 0) {
		printf("The resolution is %lld %lld\n",
		       (long long)ts.tv_sec, (long long)ts.tv_nsec);
	} else {
		printf("%s failed\n", name[3]);
		return KSFT_FAIL;
	}

	ret = syscall(SYS_clock_getres, clk_id, &sys_ts);

	if ((sys_ts.tv_sec != ts.tv_sec) || (sys_ts.tv_nsec != ts.tv_nsec)) {
		printf("%s failed\n", name[3]);
		return KSFT_FAIL;
	}

	return KSFT_PASS;
}

const char *vdso_clock_name[12] = {
	"CLOCK_REALTIME",
	"CLOCK_MONOTONIC",
	"CLOCK_PROCESS_CPUTIME_ID",
	"CLOCK_THREAD_CPUTIME_ID",
	"CLOCK_MONOTONIC_RAW",
	"CLOCK_REALTIME_COARSE",
	"CLOCK_MONOTONIC_COARSE",
	"CLOCK_BOOTTIME",
	"CLOCK_REALTIME_ALARM",
	"CLOCK_BOOTTIME_ALARM",
	"CLOCK_SGI_CYCLE",
	"CLOCK_TAI",
};

/*
 * This function calls vdso_test_clock_gettime and vdso_test_clock_getres
 * with different values for clock_id.
 */
static inline int vdso_test_clock(clockid_t clock_id)
{
	int ret0, ret1;

	ret0 = vdso_test_clock_gettime(clock_id);
	/* A skipped test is considered passed */
	if (ret0 == KSFT_SKIP)
		ret0 = KSFT_PASS;

	ret1 = vdso_test_clock_getres(clock_id);
	/* A skipped test is considered passed */
	if (ret1 == KSFT_SKIP)
		ret1 = KSFT_PASS;

	ret0 += ret1;

	printf("clock_id: %s", vdso_clock_name[clock_id]);

	if (ret0 > 0)
		printf(" [FAIL]\n");
	else
		printf(" [PASS]\n");

	return ret0;
}

int main(int argc, char **argv)
{
	unsigned long sysinfo_ehdr = getauxval(AT_SYSINFO_EHDR);
	int ret;

	if (!sysinfo_ehdr) {
		printf("AT_SYSINFO_EHDR is not present!\n");
		return KSFT_SKIP;
	}

	version = versions[VDSO_VERSION];
	name = (const char **)&names[VDSO_NAMES];

	printf("[vDSO kselftest] VDSO_VERSION: %s\n", version);

	vdso_init_from_sysinfo_ehdr(getauxval(AT_SYSINFO_EHDR));

	ret = vdso_test_gettimeofday();

#if _POSIX_TIMERS > 0

#ifdef CLOCK_REALTIME
	ret += vdso_test_clock(CLOCK_REALTIME);
#endif

#ifdef CLOCK_BOOTTIME
	ret += vdso_test_clock(CLOCK_BOOTTIME);
#endif

#ifdef CLOCK_TAI
	ret += vdso_test_clock(CLOCK_TAI);
#endif

#ifdef CLOCK_REALTIME_COARSE
	ret += vdso_test_clock(CLOCK_REALTIME_COARSE);
#endif

#ifdef CLOCK_MONOTONIC
	ret += vdso_test_clock(CLOCK_MONOTONIC);
#endif

#ifdef CLOCK_MONOTONIC_RAW
	ret += vdso_test_clock(CLOCK_MONOTONIC_RAW);
#endif

#ifdef CLOCK_MONOTONIC_COARSE
	ret += vdso_test_clock(CLOCK_MONOTONIC_COARSE);
#endif

#endif

	ret += vdso_test_time();

	if (ret > 0)
		return KSFT_FAIL;

	return KSFT_PASS;
}
