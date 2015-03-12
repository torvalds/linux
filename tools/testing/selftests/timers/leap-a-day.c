/* Leap second stress test
 *              by: John Stultz (john.stultz@linaro.org)
 *              (C) Copyright IBM 2012
 *              (C) Copyright 2013, 2015 Linaro Limited
 *              Licensed under the GPLv2
 *
 *  This test signals the kernel to insert a leap second
 *  every day at midnight GMT. This allows for stessing the
 *  kernel's leap-second behavior, as well as how well applications
 *  handle the leap-second discontinuity.
 *
 *  Usage: leap-a-day [-s] [-i <num>]
 *
 *  Options:
 *	-s:	Each iteration, set the date to 10 seconds before midnight GMT.
 *		This speeds up the number of leapsecond transitions tested,
 *		but because it calls settimeofday frequently, advancing the
 *		time by 24 hours every ~16 seconds, it may cause application
 *		disruption.
 *
 *	-i:	Number of iterations to run (default: infinite)
 *
 *  Other notes: Disabling NTP prior to running this is advised, as the two
 *		 may conflict in their commands to the kernel.
 *
 *  To build:
 *	$ gcc leap-a-day.c -o leap-a-day -lrt
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
#include <unistd.h>
#ifdef KTEST
#include "../kselftest.h"
#else
static inline int ksft_exit_pass(void)
{
	exit(0);
}
static inline int ksft_exit_fail(void)
{
	exit(1);
}
#endif

#define NSEC_PER_SEC 1000000000ULL
#define CLOCK_TAI 11

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

char *time_state_str(int state)
{
	switch (state) {
	case TIME_OK:	return "TIME_OK";
	case TIME_INS:	return "TIME_INS";
	case TIME_DEL:	return "TIME_DEL";
	case TIME_OOP:	return "TIME_OOP";
	case TIME_WAIT:	return "TIME_WAIT";
	case TIME_BAD:	return "TIME_BAD";
	}
	return "ERROR";
}

/* clear NTP time_status & time_state */
int clear_time_state(void)
{
	struct timex tx;
	int ret;

	/*
	 * We have to call adjtime twice here, as kernels
	 * prior to 6b1859dba01c7 (included in 3.5 and
	 * -stable), had an issue with the state machine
	 * and wouldn't clear the STA_INS/DEL flag directly.
	 */
	tx.modes = ADJ_STATUS;
	tx.status = STA_PLL;
	ret = adjtimex(&tx);

	/* Clear maxerror, as it can cause UNSYNC to be set */
	tx.modes = ADJ_MAXERROR;
	tx.maxerror = 0;
	ret = adjtimex(&tx);

	/* Clear the status */
	tx.modes = ADJ_STATUS;
	tx.status = 0;
	ret = adjtimex(&tx);

	return ret;
}

/* Make sure we cleanup on ctrl-c */
void handler(int unused)
{
	clear_time_state();
	exit(0);
}

/* Test for known hrtimer failure */
void test_hrtimer_failure(void)
{
	struct timespec now, target;

	clock_gettime(CLOCK_REALTIME, &now);
	target = timespec_add(now, NSEC_PER_SEC/2);
	clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &target, NULL);
	clock_gettime(CLOCK_REALTIME, &now);

	if (!in_order(target, now))
		printf("ERROR: hrtimer early expiration failure observed.\n");
}

int main(int argc, char **argv)
{
	int settime = 0;
	int tai_time = 0;
	int insert = 1;
	int iterations = -1;
	int opt;

	/* Process arguments */
	while ((opt = getopt(argc, argv, "sti:")) != -1) {
		switch (opt) {
		case 's':
			printf("Setting time to speed up testing\n");
			settime = 1;
			break;
		case 'i':
			iterations = atoi(optarg);
			break;
		case 't':
			tai_time = 1;
			break;
		default:
			printf("Usage: %s [-s] [-i <iterations>]\n", argv[0]);
			printf("	-s: Set time to right before leap second each iteration\n");
			printf("	-i: Number of iterations\n");
			printf("	-t: Print TAI time\n");
			exit(-1);
		}
	}

	/* Make sure TAI support is present if -t was used */
	if (tai_time) {
		struct timespec ts;

		if (clock_gettime(CLOCK_TAI, &ts)) {
			printf("System doesn't support CLOCK_TAI\n");
			ksft_exit_fail();
		}
	}

	signal(SIGINT, handler);
	signal(SIGKILL, handler);

	if (iterations < 0)
		printf("This runs continuously. Press ctrl-c to stop\n");
	else
		printf("Running for %i iterations. Press ctrl-c to stop\n", iterations);

	printf("\n");
	while (1) {
		int ret;
		struct timespec ts;
		struct timex tx;
		time_t now, next_leap;

		/* Get the current time */
		clock_gettime(CLOCK_REALTIME, &ts);

		/* Calculate the next possible leap second 23:59:60 GMT */
		next_leap = ts.tv_sec;
		next_leap += 86400 - (next_leap % 86400);

		if (settime) {
			struct timeval tv;

			tv.tv_sec = next_leap - 10;
			tv.tv_usec = 0;
			settimeofday(&tv, NULL);
			printf("Setting time to %s", ctime(&tv.tv_sec));
		}

		/* Reset NTP time state */
		clear_time_state();

		/* Set the leap second insert flag */
		tx.modes = ADJ_STATUS;
		if (insert)
			tx.status = STA_INS;
		else
			tx.status = STA_DEL;
		ret = adjtimex(&tx);
		if (ret < 0) {
			printf("Error: Problem setting STA_INS/STA_DEL!: %s\n",
							time_state_str(ret));
			return ksft_exit_fail();
		}

		/* Validate STA_INS was set */
		tx.modes = 0;
		ret = adjtimex(&tx);
		if (tx.status != STA_INS && tx.status != STA_DEL) {
			printf("Error: STA_INS/STA_DEL not set!: %s\n",
							time_state_str(ret));
			return ksft_exit_fail();
		}

		if (tai_time) {
			printf("Using TAI time,"
				" no inconsistencies should be seen!\n");
		}

		printf("Scheduling leap second for %s", ctime(&next_leap));

		/* Wake up 3 seconds before leap */
		ts.tv_sec = next_leap - 3;
		ts.tv_nsec = 0;

		while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL))
			printf("Something woke us up, returning to sleep\n");

		/* Validate STA_INS is still set */
		tx.modes = 0;
		ret = adjtimex(&tx);
		if (tx.status != STA_INS && tx.status != STA_DEL) {
			printf("Something cleared STA_INS/STA_DEL, setting it again.\n");
			tx.modes = ADJ_STATUS;
			if (insert)
				tx.status = STA_INS;
			else
				tx.status = STA_DEL;
			ret = adjtimex(&tx);
		}

		/* Check adjtimex output every half second */
		now = tx.time.tv_sec;
		while (now < next_leap + 2) {
			char buf[26];
			struct timespec tai;

			tx.modes = 0;
			ret = adjtimex(&tx);

			if (tai_time) {
				clock_gettime(CLOCK_TAI, &tai);
				printf("%ld sec, %9ld ns\t%s\n",
						tai.tv_sec,
						tai.tv_nsec,
						time_state_str(ret));
			} else {
				ctime_r(&tx.time.tv_sec, buf);
				buf[strlen(buf)-1] = 0; /*remove trailing\n */

				printf("%s + %6ld us (%i)\t%s\n",
						buf,
						tx.time.tv_usec,
						tx.tai,
						time_state_str(ret));
			}
			now = tx.time.tv_sec;
			/* Sleep for another half second */
			ts.tv_sec = 0;
			ts.tv_nsec = NSEC_PER_SEC / 2;
			clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
		}
		/* Switch to using other mode */
		insert = !insert;

		/* Note if kernel has known hrtimer failure */
		test_hrtimer_failure();

		printf("Leap complete\n\n");

		if ((iterations != -1) && !(--iterations))
			break;
	}

	clear_time_state();
	return ksft_exit_pass();
}
