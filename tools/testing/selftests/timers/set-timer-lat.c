/* set_timer latency test
 *		John Stultz (john.stultz@linaro.org)
 *              (C) Copyright Linaro 2014
 *              Licensed under the GPLv2
 *
 *   This test makes sure the set_timer api is correct
 *
 *  To build:
 *	$ gcc set-timer-lat.c -o set-timer-lat -lrt
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
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include "../kselftest.h"

#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			1
#define CLOCK_PROCESS_CPUTIME_ID	2
#define CLOCK_THREAD_CPUTIME_ID		3
#define CLOCK_MONOTONIC_RAW		4
#define CLOCK_REALTIME_COARSE		5
#define CLOCK_MONOTONIC_COARSE		6
#define CLOCK_BOOTTIME			7
#define CLOCK_REALTIME_ALARM		8
#define CLOCK_BOOTTIME_ALARM		9
#define CLOCK_HWSPECIFIC		10
#define CLOCK_TAI			11
#define NR_CLOCKIDS			12


#define NSEC_PER_SEC 1000000000ULL
#define UNRESONABLE_LATENCY 40000000 /* 40ms in nanosecs */

#define TIMER_SECS 1
int alarmcount;
int clock_id;
struct timespec start_time;
long long max_latency_ns;
int timer_fired_early;

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


long long timespec_sub(struct timespec a, struct timespec b)
{
	long long ret = NSEC_PER_SEC * b.tv_sec + b.tv_nsec;

	ret -= NSEC_PER_SEC * a.tv_sec + a.tv_nsec;
	return ret;
}


void sigalarm(int signo)
{
	long long delta_ns;
	struct timespec ts;

	clock_gettime(clock_id, &ts);
	alarmcount++;

	delta_ns = timespec_sub(start_time, ts);
	delta_ns -= NSEC_PER_SEC * TIMER_SECS * alarmcount;

	if (delta_ns < 0)
		timer_fired_early = 1;

	if (delta_ns > max_latency_ns)
		max_latency_ns = delta_ns;
}

void describe_timer(int flags, int interval)
{
	printf("%-22s %s %s ",
			clockstring(clock_id),
			flags ? "ABSTIME":"RELTIME",
			interval ? "PERIODIC":"ONE-SHOT");
}

int setup_timer(int clock_id, int flags, int interval, timer_t *tm1)
{
	struct sigevent se;
	struct itimerspec its1, its2;
	int err;

	/* Set up timer: */
	memset(&se, 0, sizeof(se));
	se.sigev_notify = SIGEV_SIGNAL;
	se.sigev_signo = SIGRTMAX;
	se.sigev_value.sival_int = 0;

	max_latency_ns = 0;
	alarmcount = 0;
	timer_fired_early = 0;

	err = timer_create(clock_id, &se, tm1);
	if (err) {
		if ((clock_id == CLOCK_REALTIME_ALARM) ||
		    (clock_id == CLOCK_BOOTTIME_ALARM)) {
			printf("%-22s %s missing CAP_WAKE_ALARM?    : [UNSUPPORTED]\n",
					clockstring(clock_id),
					flags ? "ABSTIME":"RELTIME");
			/* Indicate timer isn't set, so caller doesn't wait */
			return 1;
		}
		printf("%s - timer_create() failed\n", clockstring(clock_id));
		return -1;
	}

	clock_gettime(clock_id, &start_time);
	if (flags) {
		its1.it_value = start_time;
		its1.it_value.tv_sec += TIMER_SECS;
	} else {
		its1.it_value.tv_sec = TIMER_SECS;
		its1.it_value.tv_nsec = 0;
	}
	its1.it_interval.tv_sec = interval;
	its1.it_interval.tv_nsec = 0;

	err = timer_settime(*tm1, flags, &its1, &its2);
	if (err) {
		printf("%s - timer_settime() failed\n", clockstring(clock_id));
		return -1;
	}

	return 0;
}

int check_timer_latency(int flags, int interval)
{
	int err = 0;

	describe_timer(flags, interval);
	printf("timer fired early: %7d : ", timer_fired_early);
	if (!timer_fired_early) {
		printf("[OK]\n");
	} else {
		printf("[FAILED]\n");
		err = -1;
	}

	describe_timer(flags, interval);
	printf("max latency: %10lld ns : ", max_latency_ns);

	if (max_latency_ns < UNRESONABLE_LATENCY) {
		printf("[OK]\n");
	} else {
		printf("[FAILED]\n");
		err = -1;
	}
	return err;
}

int check_alarmcount(int flags, int interval)
{
	describe_timer(flags, interval);
	printf("count: %19d : ", alarmcount);
	if (alarmcount == 1) {
		printf("[OK]\n");
		return 0;
	}
	printf("[FAILED]\n");
	return -1;
}

int do_timer(int clock_id, int flags)
{
	timer_t tm1;
	const int interval = TIMER_SECS;
	int err;

	err = setup_timer(clock_id, flags, interval, &tm1);
	/* Unsupported case - return 0 to not fail the test */
	if (err)
		return err == 1 ? 0 : err;

	while (alarmcount < 5)
		sleep(1);

	timer_delete(tm1);
	return check_timer_latency(flags, interval);
}

int do_timer_oneshot(int clock_id, int flags)
{
	timer_t tm1;
	const int interval = 0;
	struct timeval timeout;
	int err;

	err = setup_timer(clock_id, flags, interval, &tm1);
	/* Unsupported case - return 0 to not fail the test */
	if (err)
		return err == 1 ? 0 : err;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 5;
	do {
		err = select(0, NULL, NULL, NULL, &timeout);
	} while (err == -1 && errno == EINTR);

	timer_delete(tm1);
	err = check_timer_latency(flags, interval);
	err |= check_alarmcount(flags, interval);
	return err;
}

int main(void)
{
	struct sigaction act;
	int signum = SIGRTMAX;
	int ret = 0;

	/* Set up signal handler: */
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sigalarm;
	sigaction(signum, &act, NULL);

	printf("Setting timers for every %i seconds\n", TIMER_SECS);
	for (clock_id = 0; clock_id < NR_CLOCKIDS; clock_id++) {

		if ((clock_id == CLOCK_PROCESS_CPUTIME_ID) ||
				(clock_id == CLOCK_THREAD_CPUTIME_ID) ||
				(clock_id == CLOCK_MONOTONIC_RAW) ||
				(clock_id == CLOCK_REALTIME_COARSE) ||
				(clock_id == CLOCK_MONOTONIC_COARSE) ||
				(clock_id == CLOCK_HWSPECIFIC))
			continue;

		ret |= do_timer(clock_id, TIMER_ABSTIME);
		ret |= do_timer(clock_id, 0);
		ret |= do_timer_oneshot(clock_id, TIMER_ABSTIME);
		ret |= do_timer_oneshot(clock_id, 0);
	}
	if (ret)
		ksft_exit_fail();
	ksft_exit_pass();
}
