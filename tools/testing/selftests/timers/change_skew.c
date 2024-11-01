/* ADJ_FREQ Skew change test
 *		by: john stultz (johnstul@us.ibm.com)
 *		(C) Copyright IBM 2012
 *		Licensed under the GPLv2
 *
 *  NOTE: This is a meta-test which cranks the ADJ_FREQ knob and
 *  then uses other tests to detect problems. Thus this test requires
 *  that the raw_skew, inconsistency-check and nanosleep tests be
 *  present in the same directory it is run from.
 *
 *  To build:
 *	$ gcc change_skew.c -o change_skew -lrt
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
#include <sys/time.h>
#include <sys/timex.h>
#include <time.h>
#include "../kselftest.h"

#define NSEC_PER_SEC 1000000000LL


int change_skew_test(int ppm)
{
	struct timex tx;
	int ret;

	tx.modes = ADJ_FREQUENCY;
	tx.freq = ppm << 16;

	ret = adjtimex(&tx);
	if (ret < 0) {
		printf("Error adjusting freq\n");
		return ret;
	}

	ret = system("./raw_skew");
	ret |= system("./inconsistency-check");
	ret |= system("./nanosleep");

	return ret;
}


int main(int argc, char **argv)
{
	struct timex tx;
	int i, ret;

	int ppm[5] = {0, 250, 500, -250, -500};

	/* Kill ntpd */
	ret = system("killall -9 ntpd");

	/* Make sure there's no offset adjustment going on */
	tx.modes = ADJ_OFFSET;
	tx.offset = 0;
	ret = adjtimex(&tx);

	if (ret < 0) {
		printf("Maybe you're not running as root?\n");
		return -1;
	}

	for (i = 0; i < 5; i++) {
		printf("Using %i ppm adjustment\n", ppm[i]);
		ret = change_skew_test(ppm[i]);
		if (ret)
			break;
	}

	/* Set things back */
	tx.modes = ADJ_FREQUENCY;
	tx.offset = 0;
	adjtimex(&tx);

	if (ret) {
		printf("[FAIL]");
		return ksft_exit_fail();
	}
	printf("[OK]");
	return ksft_exit_pass();
}
