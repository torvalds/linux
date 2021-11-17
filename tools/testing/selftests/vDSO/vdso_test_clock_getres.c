// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 * vdso_clock_getres.c: Sample code to test clock_getres.
 * Copyright (c) 2019 Arm Ltd.
 *
 * Compile with:
 * gcc -std=gnu99 vdso_clock_getres.c
 *
 * Tested on ARM, ARM64, MIPS32, x86 (32-bit and 64-bit),
 * Power (32-bit and 64-bit), S390x (32-bit and 64-bit).
 * Might work on other architectures.
 */

#define _GNU_SOURCE
#include <elf.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "../kselftest.h"

static long syscall_clock_getres(clockid_t _clkid, struct timespec *_ts)
{
	long ret;

	ret = syscall(SYS_clock_getres, _clkid, _ts);

	return ret;
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
 * This function calls clock_getres in vdso and by system call
 * with different values for clock_id.
 *
 * Example of output:
 *
 * clock_id: CLOCK_REALTIME [PASS]
 * clock_id: CLOCK_BOOTTIME [PASS]
 * clock_id: CLOCK_TAI [PASS]
 * clock_id: CLOCK_REALTIME_COARSE [PASS]
 * clock_id: CLOCK_MONOTONIC [PASS]
 * clock_id: CLOCK_MONOTONIC_RAW [PASS]
 * clock_id: CLOCK_MONOTONIC_COARSE [PASS]
 */
static inline int vdso_test_clock(unsigned int clock_id)
{
	struct timespec x, y;

	printf("clock_id: %s", vdso_clock_name[clock_id]);
	clock_getres(clock_id, &x);
	syscall_clock_getres(clock_id, &y);

	if ((x.tv_sec != y.tv_sec) || (x.tv_nsec != y.tv_nsec)) {
		printf(" [FAIL]\n");
		return KSFT_FAIL;
	}

	printf(" [PASS]\n");
	return KSFT_PASS;
}

int main(int argc, char **argv)
{
	int ret;

#if _POSIX_TIMERS > 0

#ifdef CLOCK_REALTIME
	ret = vdso_test_clock(CLOCK_REALTIME);
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
	if (ret > 0)
		return KSFT_FAIL;

	return KSFT_PASS;
}
