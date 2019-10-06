/* Set tz value
 *              by: John Stultz <john.stultz@linaro.org>
 *              (C) Copyright Linaro 2016
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

int set_tz(int min, int dst)
{
	struct timezone tz;

	tz.tz_minuteswest = min;
	tz.tz_dsttime = dst;

	return settimeofday(0, &tz);
}

int get_tz_min(void)
{
	struct timezone tz;
	struct timeval tv;

	memset(&tz, 0, sizeof(tz));
	gettimeofday(&tv, &tz);
	return tz.tz_minuteswest;
}

int get_tz_dst(void)
{
	struct timezone tz;
	struct timeval tv;

	memset(&tz, 0, sizeof(tz));
	gettimeofday(&tv, &tz);
	return tz.tz_dsttime;
}

int main(int argc, char **argv)
{
	int i, ret;
	int min, dst;

	min = get_tz_min();
	dst = get_tz_dst();
	printf("tz_minuteswest started at %i, dst at %i\n", min, dst);

	printf("Checking tz_minuteswest can be properly set: ");
	fflush(stdout);
	for (i = -15*60; i < 15*60; i += 30) {
		ret = set_tz(i, dst);
		ret = get_tz_min();
		if (ret != i) {
			printf("[FAILED] expected: %i got %i\n", i, ret);
			goto err;
		}
	}
	printf("[OK]\n");

	printf("Checking invalid tz_minuteswest values are caught: ");
	fflush(stdout);

	if (!set_tz(-15*60-1, dst)) {
		printf("[FAILED] %i didn't return failure!\n", -15*60-1);
		goto err;
	}

	if (!set_tz(15*60+1, dst)) {
		printf("[FAILED] %i didn't return failure!\n", 15*60+1);
		goto err;
	}

	if (!set_tz(-24*60, dst)) {
		printf("[FAILED] %i didn't return failure!\n", -24*60);
		goto err;
	}

	if (!set_tz(24*60, dst)) {
		printf("[FAILED] %i didn't return failure!\n", 24*60);
		goto err;
	}

	printf("[OK]\n");

	set_tz(min, dst);
	return ksft_exit_pass();

err:
	set_tz(min, dst);
	return ksft_exit_fail();
}
