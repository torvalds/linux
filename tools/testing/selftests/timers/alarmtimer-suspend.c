/* alarmtimer suspend test
 *		John Stultz (john.stultz@linaro.org)
 *              (C) Copyright Linaro 2013
 *              Licensed under the GPLv2
 *
 *   This test makes sure the alarmtimer & RTC wakeup code is
 *   functioning.
 *
 *  To build:
 *	$ gcc alarmtimer-suspend.c -o alarmtimer-suspend -lrt
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
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <include/vdso/time64.h>
#include <errno.h>
#include "../kselftest.h"

#define UNREASONABLE_LAT (NSEC_PER_SEC * 5) /* hopefully we resume in 5 secs */

#define SUSPEND_SECS 15
int alarmcount;
int alarm_clock_id;
struct timespec start_time;


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
	}
	return "UNKNOWN_CLOCKID";
}


long long timespec_sub(struct timespec a, struct timespec b)
{
	long long ret = NSEC_PER_SEC * b.tv_sec + b.tv_nsec;

	ret -= NSEC_PER_SEC * a.tv_sec + a.tv_nsec;
	return ret;
}

int final_ret;

void sigalarm(int signo)
{
	long long delta_ns;
	struct timespec ts;

	clock_gettime(alarm_clock_id, &ts);
	alarmcount++;

	delta_ns = timespec_sub(start_time, ts);
	delta_ns -= NSEC_PER_SEC * SUSPEND_SECS * alarmcount;

	printf("ALARM(%i): %ld:%ld latency: %lld ns ", alarmcount, ts.tv_sec,
							ts.tv_nsec, delta_ns);

	if (delta_ns > UNREASONABLE_LAT) {
		printf("[FAIL]\n");
		final_ret = -1;
	} else
		printf("[OK]\n");

}

int main(void)
{
	timer_t tm1;
	struct itimerspec its1, its2;
	struct sigevent se;
	struct sigaction act;
	int signum = SIGRTMAX;

	/* Set up signal handler: */
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = sigalarm;
	sigaction(signum, &act, NULL);

	/* Set up timer: */
	memset(&se, 0, sizeof(se));
	se.sigev_notify = SIGEV_SIGNAL;
	se.sigev_signo = signum;
	se.sigev_value.sival_int = 0;

	for (alarm_clock_id = CLOCK_REALTIME_ALARM;
			alarm_clock_id <= CLOCK_BOOTTIME_ALARM;
			alarm_clock_id++) {

		alarmcount = 0;
		if (timer_create(alarm_clock_id, &se, &tm1) == -1) {
			printf("timer_create failed, %s unsupported?: %s\n",
					clockstring(alarm_clock_id), strerror(errno));
			break;
		}

		clock_gettime(alarm_clock_id, &start_time);
		printf("Start time (%s): %ld:%ld\n", clockstring(alarm_clock_id),
				start_time.tv_sec, start_time.tv_nsec);
		printf("Setting alarm for every %i seconds\n", SUSPEND_SECS);
		its1.it_value = start_time;
		its1.it_value.tv_sec += SUSPEND_SECS;
		its1.it_interval.tv_sec = SUSPEND_SECS;
		its1.it_interval.tv_nsec = 0;

		timer_settime(tm1, TIMER_ABSTIME, &its1, &its2);

		while (alarmcount < 5)
			sleep(1); /* First 5 alarms, do nothing */

		printf("Starting suspend loops\n");
		while (alarmcount < 10) {
			int ret;

			sleep(3);
			ret = system("echo mem > /sys/power/state");
			if (ret)
				break;
		}
		timer_delete(tm1);
	}
	if (final_ret)
		ksft_exit_fail();
	ksft_exit_pass();
}
