/* Time inconsistency check test
 *		by: john stultz (johnstul@us.ibm.com)
 *		(C) Copyright IBM 2003, 2004, 2005, 2012
 *		(C) Copyright Linaro Limited 2015
 *		Licensed under the GPLv2
 *
 *  To build:
 *	$ gcc inconsistency-check.c -o inconsistency-check -lrt
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
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <string.h>
#include <signal.h>
#include "../kselftest.h"

#define CALLS_PER_LOOP 64
#define NSEC_PER_SEC 1000000000ULL

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

/* returns 1 if a <= b, 0 otherwise */
static inline int in_order(struct timespec a, struct timespec b)
{
	/* use unsigned to avoid false positives on 2038 rollover */
	if ((unsigned long)a.tv_sec < (unsigned long)b.tv_sec)
		return 1;
	if ((unsigned long)a.tv_sec > (unsigned long)b.tv_sec)
		return 0;
	if (a.tv_nsec > b.tv_nsec)
		return 0;
	return 1;
}



int consistency_test(int clock_type, unsigned long seconds)
{
	struct timespec list[CALLS_PER_LOOP];
	int i, inconsistent;
	long now, then;
	time_t t;
	char *start_str;

	clock_gettime(clock_type, &list[0]);
	now = then = list[0].tv_sec;

	/* timestamp start of test */
	t = time(0);
	start_str = ctime(&t);

	while (seconds == -1 || now - then < seconds) {
		inconsistent = -1;

		/* Fill list */
		for (i = 0; i < CALLS_PER_LOOP; i++)
			clock_gettime(clock_type, &list[i]);

		/* Check for inconsistencies */
		for (i = 0; i < CALLS_PER_LOOP - 1; i++)
			if (!in_order(list[i], list[i+1]))
				inconsistent = i;

		/* display inconsistency */
		if (inconsistent >= 0) {
			unsigned long long delta;

			printf("\%s\n", start_str);
			for (i = 0; i < CALLS_PER_LOOP; i++) {
				if (i == inconsistent)
					printf("--------------------\n");
				printf("%lu:%lu\n", list[i].tv_sec,
							list[i].tv_nsec);
				if (i == inconsistent + 1)
					printf("--------------------\n");
			}
			delta = list[inconsistent].tv_sec * NSEC_PER_SEC;
			delta += list[inconsistent].tv_nsec;
			delta -= list[inconsistent+1].tv_sec * NSEC_PER_SEC;
			delta -= list[inconsistent+1].tv_nsec;
			printf("Delta: %llu ns\n", delta);
			fflush(0);
			/* timestamp inconsistency*/
			t = time(0);
			printf("%s\n", ctime(&t));
			printf("[FAILED]\n");
			return -1;
		}
		now = list[0].tv_sec;
	}
	printf("[OK]\n");
	return 0;
}


int main(int argc, char *argv[])
{
	int clockid, opt;
	int userclock = CLOCK_REALTIME;
	int maxclocks = NR_CLOCKIDS;
	int runtime = 10;
	struct timespec ts;

	/* Process arguments */
	while ((opt = getopt(argc, argv, "t:c:")) != -1) {
		switch (opt) {
		case 't':
			runtime = atoi(optarg);
			break;
		case 'c':
			userclock = atoi(optarg);
			maxclocks = userclock + 1;
			break;
		default:
			printf("Usage: %s [-t <secs>] [-c <clockid>]\n", argv[0]);
			printf("	-t: Number of seconds to run\n");
			printf("	-c: clockid to use (default, all clockids)\n");
			exit(-1);
		}
	}

	setbuf(stdout, NULL);

	for (clockid = userclock; clockid < maxclocks; clockid++) {

		if (clockid == CLOCK_HWSPECIFIC)
			continue;

		if (!clock_gettime(clockid, &ts)) {
			printf("Consistent %-30s ", clockstring(clockid));
			if (consistency_test(clockid, runtime))
				return ksft_exit_fail();
		}
	}
	return ksft_exit_pass();
}
