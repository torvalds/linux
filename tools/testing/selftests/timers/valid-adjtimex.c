/* valid adjtimex test
 *              by: John Stultz <john.stultz@linaro.org>
 *              (C) Copyright Linaro 2015
 *              Licensed under the GPLv2
 *
 *  This test validates adjtimex interface with valid
 *  and invalid test data.
 *
 *  Usage: valid-adjtimex
 *
 *  To build:
 *	$ gcc valid-adjtimex.c -o valid-adjtimex -lrt
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
#include <include/vdso/time64.h>
#include "../kselftest.h"

#define ADJ_SETOFFSET 0x0100

#include <sys/syscall.h>
int clock_adjtime(clockid_t id, struct timex *tx)
{
	return syscall(__NR_clock_adjtime, id, tx);
}


/* clear NTP time_status & time_state */
int clear_time_state(void)
{
	struct timex tx;
	int ret;

	tx.modes = ADJ_STATUS;
	tx.status = 0;
	ret = adjtimex(&tx);
	return ret;
}

#define NUM_FREQ_VALID 32
#define NUM_FREQ_OUTOFRANGE 4
#define NUM_FREQ_INVALID 2

#define SHIFTED_PPM (1 << 16)

long valid_freq[NUM_FREQ_VALID] = {
	 -499 * SHIFTED_PPM,
	 -450 * SHIFTED_PPM,
	 -400 * SHIFTED_PPM,
	 -350 * SHIFTED_PPM,
	 -300 * SHIFTED_PPM,
	 -250 * SHIFTED_PPM,
	 -200 * SHIFTED_PPM,
	 -150 * SHIFTED_PPM,
	 -100 * SHIFTED_PPM,
	  -75 * SHIFTED_PPM,
	  -50 * SHIFTED_PPM,
	  -25 * SHIFTED_PPM,
	  -10 * SHIFTED_PPM,
	   -5 * SHIFTED_PPM,
	   -1 * SHIFTED_PPM,
	-1000,
	    1 * SHIFTED_PPM,
	    5 * SHIFTED_PPM,
	   10 * SHIFTED_PPM,
	   25 * SHIFTED_PPM,
	   50 * SHIFTED_PPM,
	   75 * SHIFTED_PPM,
	  100 * SHIFTED_PPM,
	  150 * SHIFTED_PPM,
	  200 * SHIFTED_PPM,
	  250 * SHIFTED_PPM,
	  300 * SHIFTED_PPM,
	  350 * SHIFTED_PPM,
	  400 * SHIFTED_PPM,
	  450 * SHIFTED_PPM,
	  499 * SHIFTED_PPM,
};

long outofrange_freq[NUM_FREQ_OUTOFRANGE] = {
	-1000 * SHIFTED_PPM,
	 -550 * SHIFTED_PPM,
	  550 * SHIFTED_PPM,
	 1000 * SHIFTED_PPM,
};

#define LONG_MAX (~0UL>>1)
#define LONG_MIN (-LONG_MAX - 1)

long invalid_freq[NUM_FREQ_INVALID] = {
	LONG_MAX,
	LONG_MIN,
};

int validate_freq(void)
{
	struct timex tx;
	int ret, pass = 0;
	int i;

	clear_time_state();

	memset(&tx, 0, sizeof(struct timex));
	/* Set the leap second insert flag */

	printf("Testing ADJ_FREQ... ");
	fflush(stdout);
	for (i = 0; i < NUM_FREQ_VALID; i++) {
		tx.modes = ADJ_FREQUENCY;
		tx.freq = valid_freq[i];

		ret = adjtimex(&tx);
		if (ret < 0) {
			printf("[FAIL]\n");
			printf("Error: adjtimex(ADJ_FREQ, %ld - %ld ppm\n",
				valid_freq[i], valid_freq[i]>>16);
			pass = -1;
			goto out;
		}
		tx.modes = 0;
		ret = adjtimex(&tx);
		if (tx.freq != valid_freq[i]) {
			printf("Warning: freq value %ld not what we set it (%ld)!\n",
					tx.freq, valid_freq[i]);
		}
	}
	for (i = 0; i < NUM_FREQ_OUTOFRANGE; i++) {
		tx.modes = ADJ_FREQUENCY;
		tx.freq = outofrange_freq[i];

		ret = adjtimex(&tx);
		if (ret < 0) {
			printf("[FAIL]\n");
			printf("Error: adjtimex(ADJ_FREQ, %ld - %ld ppm\n",
				outofrange_freq[i], outofrange_freq[i]>>16);
			pass = -1;
			goto out;
		}
		tx.modes = 0;
		ret = adjtimex(&tx);
		if (tx.freq == outofrange_freq[i]) {
			printf("[FAIL]\n");
			printf("ERROR: out of range value %ld actually set!\n",
					tx.freq);
			pass = -1;
			goto out;
		}
	}


	if (sizeof(long) == 8) { /* this case only applies to 64bit systems */
		for (i = 0; i < NUM_FREQ_INVALID; i++) {
			tx.modes = ADJ_FREQUENCY;
			tx.freq = invalid_freq[i];
			ret = adjtimex(&tx);
			if (ret >= 0) {
				printf("[FAIL]\n");
				printf("Error: No failure on invalid ADJ_FREQUENCY %ld\n",
					invalid_freq[i]);
				pass = -1;
				goto out;
			}
		}
	}

	printf("[OK]\n");
out:
	/* reset freq to zero */
	tx.modes = ADJ_FREQUENCY;
	tx.freq = 0;
	ret = adjtimex(&tx);

	return pass;
}


int set_offset(long long offset, int use_nano)
{
	struct timex tmx = {};
	int ret;

	tmx.modes = ADJ_SETOFFSET;
	if (use_nano) {
		tmx.modes |= ADJ_NANO;

		tmx.time.tv_sec = offset / NSEC_PER_SEC;
		tmx.time.tv_usec = offset % NSEC_PER_SEC;

		if (offset < 0 && tmx.time.tv_usec) {
			tmx.time.tv_sec -= 1;
			tmx.time.tv_usec += NSEC_PER_SEC;
		}
	} else {
		tmx.time.tv_sec = offset / USEC_PER_SEC;
		tmx.time.tv_usec = offset % USEC_PER_SEC;

		if (offset < 0 && tmx.time.tv_usec) {
			tmx.time.tv_sec -= 1;
			tmx.time.tv_usec += USEC_PER_SEC;
		}
	}

	ret = clock_adjtime(CLOCK_REALTIME, &tmx);
	if (ret < 0) {
		printf("(sec: %ld  usec: %ld) ", tmx.time.tv_sec, tmx.time.tv_usec);
		printf("[FAIL]\n");
		return -1;
	}
	return 0;
}

int set_bad_offset(long sec, long usec, int use_nano)
{
	struct timex tmx = {};
	int ret;

	tmx.modes = ADJ_SETOFFSET;
	if (use_nano)
		tmx.modes |= ADJ_NANO;

	tmx.time.tv_sec = sec;
	tmx.time.tv_usec = usec;
	ret = clock_adjtime(CLOCK_REALTIME, &tmx);
	if (ret >= 0) {
		printf("Invalid (sec: %ld  usec: %ld) did not fail! ", tmx.time.tv_sec, tmx.time.tv_usec);
		printf("[FAIL]\n");
		return -1;
	}
	return 0;
}

int validate_set_offset(void)
{
	printf("Testing ADJ_SETOFFSET... ");
	fflush(stdout);

	/* Test valid values */
	if (set_offset(NSEC_PER_SEC - 1, 1))
		return -1;

	if (set_offset(-NSEC_PER_SEC + 1, 1))
		return -1;

	if (set_offset(-NSEC_PER_SEC - 1, 1))
		return -1;

	if (set_offset(5 * NSEC_PER_SEC, 1))
		return -1;

	if (set_offset(-5 * NSEC_PER_SEC, 1))
		return -1;

	if (set_offset(5 * NSEC_PER_SEC + NSEC_PER_SEC / 2, 1))
		return -1;

	if (set_offset(-5 * NSEC_PER_SEC - NSEC_PER_SEC / 2, 1))
		return -1;

	if (set_offset(USEC_PER_SEC - 1, 0))
		return -1;

	if (set_offset(-USEC_PER_SEC + 1, 0))
		return -1;

	if (set_offset(-USEC_PER_SEC - 1, 0))
		return -1;

	if (set_offset(5 * USEC_PER_SEC, 0))
		return -1;

	if (set_offset(-5 * USEC_PER_SEC, 0))
		return -1;

	if (set_offset(5 * USEC_PER_SEC + USEC_PER_SEC / 2, 0))
		return -1;

	if (set_offset(-5 * USEC_PER_SEC - USEC_PER_SEC / 2, 0))
		return -1;

	/* Test invalid values */
	if (set_bad_offset(0, -1, 1))
		return -1;
	if (set_bad_offset(0, -1, 0))
		return -1;
	if (set_bad_offset(0, 2 * NSEC_PER_SEC, 1))
		return -1;
	if (set_bad_offset(0, 2 * USEC_PER_SEC, 0))
		return -1;
	if (set_bad_offset(0, NSEC_PER_SEC, 1))
		return -1;
	if (set_bad_offset(0, USEC_PER_SEC, 0))
		return -1;
	if (set_bad_offset(0, -NSEC_PER_SEC, 1))
		return -1;
	if (set_bad_offset(0, -USEC_PER_SEC, 0))
		return -1;

	printf("[OK]\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (validate_freq())
		ksft_exit_fail();

	if (validate_set_offset())
		ksft_exit_fail();

	ksft_exit_pass();
}
