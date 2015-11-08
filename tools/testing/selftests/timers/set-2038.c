/* Time bounds setting test
 *		by: john stultz (johnstul@us.ibm.com)
 *		(C) Copyright IBM 2012
 *		Licensed under the GPLv2
 *
 *  NOTE: This is a meta-test which sets the time to edge cases then
 *  uses other tests to detect problems. Thus this test requires that
 *  the inconsistency-check and nanosleep tests be present in the same
 *  directory it is run from.
 *
 *  To build:
 *	$ gcc set-2038.c -o set-2038 -lrt
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
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
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

#define NSEC_PER_SEC 1000000000LL

#define KTIME_MAX	((long long)~((unsigned long long)1 << 63))
#define KTIME_SEC_MAX	(KTIME_MAX / NSEC_PER_SEC)

#define YEAR_1901 (-0x7fffffffL)
#define YEAR_1970 1
#define YEAR_2038 0x7fffffffL			/*overflows 32bit time_t */
#define YEAR_2262 KTIME_SEC_MAX			/*overflows 64bit ktime_t */
#define YEAR_MAX  ((long long)((1ULL<<63)-1))	/*overflows 64bit time_t */

int is32bits(void)
{
	return (sizeof(long) == 4);
}

int settime(long long time)
{
	struct timeval now;
	int ret;

	now.tv_sec = (time_t)time;
	now.tv_usec  = 0;

	ret = settimeofday(&now, NULL);

	printf("Setting time to 0x%lx: %d\n", (long)time, ret);
	return ret;
}

int do_tests(void)
{
	int ret;

	ret = system("date");
	ret = system("./inconsistency-check -c 0 -t 20");
	ret |= system("./nanosleep");
	ret |= system("./nsleep-lat");
	return ret;

}

int main(int argc, char *argv[])
{
	int ret = 0;
	int opt, dangerous = 0;
	time_t start;

	/* Process arguments */
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			dangerous = 1;
		}
	}

	start = time(0);

	/* First test that crazy values don't work */
	if (!settime(YEAR_1901)) {
		ret = -1;
		goto out;
	}
	if (!settime(YEAR_MAX)) {
		ret = -1;
		goto out;
	}
	if (!is32bits() && !settime(YEAR_2262)) {
		ret = -1;
		goto out;
	}

	/* Now test behavior near edges */
	settime(YEAR_1970);
	ret = do_tests();
	if (ret)
		goto out;

	settime(YEAR_2038 - 600);
	ret = do_tests();
	if (ret)
		goto out;

	/* The rest of the tests can blowup on 32bit systems */
	if (is32bits() && !dangerous)
		goto out;
	/* Test rollover behavior 32bit edge */
	settime(YEAR_2038 - 10);
	ret = do_tests();
	if (ret)
		goto out;

	settime(YEAR_2262 - 600);
	ret = do_tests();

out:
	/* restore clock */
	settime(start);
	if (ret)
		return ksft_exit_fail();
	return ksft_exit_pass();
}
