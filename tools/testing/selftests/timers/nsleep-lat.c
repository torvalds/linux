/* Measure nanosleep timer latency
 *              by: john stultz (john.stultz@linaro.org)
 *		(C) Copyright Linaro 2013
 *              Licensed under the GPLv2
 *
 *  To build:
 *	$ gcc nsleep-lat.c -o nsleep-lat -lrt
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <string.h>
#include <signal.h>
#include <include/vdso/time64.h>
#include "../kselftest.h"

#define UNRESONABLE_LATENCY 40000000 /* 40ms in nanosecs */

/* CLOCK_HWSPECIFIC == CLOCK_SGI_CYCLE (Deprecated) */
#define CLOCK_HWSPECIFIC		10

#define UNSUPPORTED 0xf00f

char *clockstring(int clockid)
{
	switch (clockid) {
	case CLOCK_REALTIME:
		return "CLOCK_REALTIME";
	case CLOCK_MONOTONIC:
		return "CLOCK_MONOTONIC";
	case CLOCK_PROCESS_CPUTIME_ID:
		return "CLOCK_PROCESS_CPUTIME_ID";
	case CLOCK_THREAD_CPUTIME_ID:
		return "CLOCK_THREAD_CPUTIME_ID";
	case CLOCK_MONOTONIC_RAW:
		return "CLOCK_MONOTONIC_RAW";
	case CLOCK_REALTIME_COARSE:
		return "CLOCK_REALTIME_COARSE";
	case CLOCK_MONOTONIC_COARSE:
		return "CLOCK_MONOTONIC_COARSE";
	case CLOCK_BOOTTIME:
		return "CLOCK_BOOTTIME";
	case CLOCK_REALTIME_ALARM:
		return "CLOCK_REALTIME_ALARM";
	case CLOCK_BOOTTIME_ALARM:
		return "CLOCK_BOOTTIME_ALARM";
	case CLOCK_TAI:
		return "CLOCK_TAI";
	};
	return "UNKNOWN_CLOCKID";
}

struct timespec timespec_add(struct timespec ts, unsigned long long ns)
{
	ts.tv_nsec += ns;
	while (ts.tv_nsec >= NSEC_PER_SEC) {
		ts.tv_nsec -= NSEC_PER_SEC;
		ts.tv_sec++;
	}
	return ts;
}


long long timespec_sub(struct timespec a, struct timespec b)
{
	long long ret = NSEC_PER_SEC * b.tv_sec + b.tv_nsec;

	ret -= NSEC_PER_SEC * a.tv_sec + a.tv_nsec;
	return ret;
}

int nanosleep_lat_test(int clockid, long long ns)
{
	struct timespec start, end, target;
	long long latency = 0;
	int i, count;

	target.tv_sec = ns/NSEC_PER_SEC;
	target.tv_nsec = ns%NSEC_PER_SEC;

	if (clock_gettime(clockid, &start))
		return UNSUPPORTED;
	if (clock_nanosleep(clockid, 0, &target, NULL))
		return UNSUPPORTED;

	count = 10;

	/* First check relative latency */
	clock_gettime(clockid, &start);
	for (i = 0; i < count; i++)
		clock_nanosleep(clockid, 0, &target, NULL);
	clock_gettime(clockid, &end);

	if (((timespec_sub(start, end)/count)-ns) > UNRESONABLE_LATENCY) {
		ksft_print_msg("Large rel latency: %lld ns :", (timespec_sub(start, end)/count)-ns);
		return -1;
	}

	/* Next check absolute latency */
	for (i = 0; i < count; i++) {
		clock_gettime(clockid, &start);
		target = timespec_add(start, ns);
		clock_nanosleep(clockid, TIMER_ABSTIME, &target, NULL);
		clock_gettime(clockid, &end);
		latency += timespec_sub(target, end);
	}

	if (latency/count > UNRESONABLE_LATENCY) {
		ksft_print_msg("Large abs latency: %lld ns :", latency/count);
		return -1;
	}

	return 0;
}

#define SKIPPED_CLOCK_COUNT 3

int main(int argc, char **argv)
{
	long long length;
	int clockid, ret;
	int max_clocks = CLOCK_TAI + 1;

	ksft_print_header();
	ksft_set_plan(max_clocks - CLOCK_REALTIME - SKIPPED_CLOCK_COUNT);

	for (clockid = CLOCK_REALTIME; clockid < max_clocks; clockid++) {

		/* Skip cputime clockids since nanosleep won't increment cputime */
		if (clockid == CLOCK_PROCESS_CPUTIME_ID ||
				clockid == CLOCK_THREAD_CPUTIME_ID ||
				clockid == CLOCK_HWSPECIFIC)
			continue;

		length = 10;
		while (length <= (NSEC_PER_SEC * 10)) {
			ret = nanosleep_lat_test(clockid, length);
			if (ret)
				break;
			length *= 100;

		}

		if (ret == UNSUPPORTED) {
			ksft_test_result_skip("%s\n", clockstring(clockid));
		} else {
			ksft_test_result(ret >= 0, "%s\n",
					 clockstring(clockid));
		}
	}

	ksft_finished();
}
