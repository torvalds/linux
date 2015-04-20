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

#define NSEC_PER_SEC 1000000000L

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

long valid_freq[NUM_FREQ_VALID] = {
	-499<<16,
	-450<<16,
	-400<<16,
	-350<<16,
	-300<<16,
	-250<<16,
	-200<<16,
	-150<<16,
	-100<<16,
	-75<<16,
	-50<<16,
	-25<<16,
	-10<<16,
	-5<<16,
	-1<<16,
	-1000,
	1<<16,
	5<<16,
	10<<16,
	25<<16,
	50<<16,
	75<<16,
	100<<16,
	150<<16,
	200<<16,
	250<<16,
	300<<16,
	350<<16,
	400<<16,
	450<<16,
	499<<16,
};

long outofrange_freq[NUM_FREQ_OUTOFRANGE] = {
	-1000<<16,
	-550<<16,
	550<<16,
	1000<<16,
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


int main(int argc, char **argv)
{
	if (validate_freq())
		return ksft_exit_fail();

	return ksft_exit_pass();
}
