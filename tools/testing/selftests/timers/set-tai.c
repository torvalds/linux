/* Set tai offset
 *              by: John Stultz <john.stultz@linaro.org>
 *              (C) Copyright Linaro 2013
 *              Licensed under the GPLv2
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
#include "../kselftest.h"

int set_tai(int offset)
{
	struct timex tx;

	memset(&tx, 0, sizeof(tx));

	tx.modes = ADJ_TAI;
	tx.constant = offset;

	return adjtimex(&tx);
}

int get_tai(void)
{
	struct timex tx;

	memset(&tx, 0, sizeof(tx));

	adjtimex(&tx);
	return tx.tai;
}

int main(int argc, char **argv)
{
	int i, ret;

	ret = get_tai();
	printf("tai offset started at %i\n", ret);

	printf("Checking tai offsets can be properly set: ");
	for (i = 1; i <= 60; i++) {
		ret = set_tai(i);
		ret = get_tai();
		if (ret != i) {
			printf("[FAILED] expected: %i got %i\n", i, ret);
			return ksft_exit_fail();
		}
	}
	printf("[OK]\n");
	return ksft_exit_pass();
}
