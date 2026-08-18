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
#include "clock-helpers.h"
#include "kselftest.h"

#define UNRESONABLE_LATENCY (40 * NSEC_PER_MSEC)

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
		return KSFT_SKIP;
	if (clock_nanosleep(clockid, 0, &target, NULL))
		return KSFT_SKIP;

	count = 10;

	/* First check relative latency */
	if (clock_gettime(clockid, &start))
		return KSFT_FAIL;

	for (i = 0; i < count; i++) {
		if (clock_nanosleep(clockid, 0, &target, NULL))
			return KSFT_FAIL;
	}

	if (clock_gettime(clockid, &end))
		return KSFT_FAIL;

	if (((timespec_sub(start, end)/count)-ns) > UNRESONABLE_LATENCY) {
		ksft_print_msg("Large rel latency: %lld ns :", (timespec_sub(start, end)/count)-ns);
		return KSFT_FAIL;
	}

	/* Next check absolute latency */
	for (i = 0; i < count; i++) {
		if (clock_gettime(clockid, &start))
			return KSFT_FAIL;
		target = timespec_add(start, ns);
		if (clock_nanosleep(clockid, TIMER_ABSTIME, &target, NULL))
			return KSFT_FAIL;
		if (clock_gettime(clockid, &end))
			return KSFT_FAIL;
		latency += timespec_sub(target, end);
	}

	if (latency/count > UNRESONABLE_LATENCY) {
		ksft_print_msg("Large abs latency: %lld ns :", latency/count);
		return KSFT_FAIL;
	}

	return KSFT_PASS;
}

int main(int argc, char **argv)
{
	long long length;
	int clockid, ret;

	static const clockid_t tested_clocks[] = {
		CLOCK_REALTIME,
		CLOCK_MONOTONIC,
		CLOCK_BOOTTIME,
		CLOCK_BOOTTIME_ALARM,
		CLOCK_REALTIME_ALARM,
		CLOCK_TAI,
	};

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tested_clocks));

	for (size_t clock_index = 0; clock_index < ARRAY_SIZE(tested_clocks); clock_index++) {
		clockid = tested_clocks[clock_index];

		length = 10;
		while (length <= (NSEC_PER_SEC * 10)) {
			ret = nanosleep_lat_test(clockid, length);
			if (ret)
				break;
			length *= 100;

		}

		ksft_test_result_report(ret, "%s\n", clock_name(clockid));
	}

	ksft_finished();
}
