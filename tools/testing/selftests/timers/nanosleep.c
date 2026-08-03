/* Make sure timers don't return early
 *              by: john stultz (johnstul@us.ibm.com)
 *		    John Stultz (john.stultz@linaro.org)
 *              (C) Copyright IBM 2012
 *              (C) Copyright Linaro 2013 2015
 *              Licensed under the GPLv2
 *
 *  To build:
 *	$ gcc nanosleep.c -o nanosleep -lrt
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <string.h>
#include <signal.h>
#include "clock-helpers.h"
#include "kselftest.h"

/* returns 1 if a <= b, 0 otherwise */
static inline int in_order(struct timespec a, struct timespec b)
{
	if (a.tv_sec < b.tv_sec)
		return 1;
	if (a.tv_sec > b.tv_sec)
		return 0;
	if (a.tv_nsec > b.tv_nsec)
		return 0;
	return 1;
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

int nanosleep_test(int clockid, long long ns)
{
	struct timespec now, target, rel;

	/* First check abs time */
	if (clock_gettime(clockid, &now))
		return KSFT_SKIP;
	target = timespec_add(now, ns);

	if (clock_nanosleep(clockid, TIMER_ABSTIME, &target, NULL))
		return KSFT_SKIP;
	clock_gettime(clockid, &now);

	if (!in_order(target, now))
		return KSFT_FAIL;

	/* Second check reltime */
	clock_gettime(clockid, &now);
	rel.tv_sec = 0;
	rel.tv_nsec = 0;
	rel = timespec_add(rel, ns);
	target = timespec_add(now, ns);
	clock_nanosleep(clockid, 0, &rel, NULL);
	clock_gettime(clockid, &now);

	if (!in_order(target, now))
		return KSFT_FAIL;
	return KSFT_PASS;
}

static void dummy_event_handler(int val)
{
	/* No action needed */
}

static int nanosleep_test_remaining(int clockid)
{
	struct timespec rqtp = {}, rmtp = {};
	struct itimerspec itimer = {};
	struct sigaction sa = {};
	timer_t timer;
	int ret;

	sa.sa_handler = dummy_event_handler;
	ret = sigaction(SIGALRM, &sa, NULL);
	if (ret)
		return KSFT_FAIL;

	ret = timer_create(clockid, NULL, &timer);
	if (ret)
		return KSFT_FAIL;

	itimer.it_value.tv_nsec = NSEC_PER_SEC / 4;
	ret = timer_settime(timer, 0, &itimer, NULL);
	if (ret)
		return KSFT_FAIL;

	rqtp.tv_nsec = NSEC_PER_SEC / 2;
	ret = clock_nanosleep(clockid, 0, &rqtp, &rmtp);

	if (timer_delete(timer)) {
		ksft_exit_fail_msg("Unable to delete the timeout timer for %s. "
				   "This might interfere with following testcases.\n",
				   clock_name(clockid));
	}

	if (ret != EINTR)
		return KSFT_FAIL;

	sa.sa_handler = SIG_DFL;
	ret = sigaction(SIGALRM, &sa, NULL);
	if (ret)
		return KSFT_FAIL;

	if (!in_order((struct timespec) {}, rmtp))
		return KSFT_FAIL;

	if (!in_order(rmtp, rqtp))
		return KSFT_FAIL;

	return KSFT_PASS;
}

static void nanosleep_test_clock(clockid_t clockid)
{
	long long length = 10;
	int ret;

	while (length <= (NSEC_PER_SEC * 10)) {
		ret = nanosleep_test(clockid, length);
		if (ret != KSFT_PASS) {
			ksft_test_result_report(ret, "%s\n", clock_name(clockid));
			ksft_test_result_skip("%s (remaining)\n", clock_name(clockid));
			return;
		}

		length *= 100;
	}
	ksft_test_result_pass("%s\n", clock_name(clockid));

	ret = nanosleep_test_remaining(clockid);
	ksft_test_result_report(ret, "%s (remaining)\n", clock_name(clockid));
}

int main(int argc, char **argv)
{
	int clockid;

	static const clockid_t tested_clocks[] = {
		CLOCK_REALTIME,
		CLOCK_MONOTONIC,
		CLOCK_BOOTTIME,
		CLOCK_BOOTTIME_ALARM,
		CLOCK_REALTIME_ALARM,
		CLOCK_TAI,
	};

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tested_clocks) * 2);

	for (size_t clock_index = 0; clock_index < ARRAY_SIZE(tested_clocks); clock_index++) {
		clockid = tested_clocks[clock_index];

		fflush(stdout);

		nanosleep_test_clock(clockid);
	}
	ksft_finished();
}
