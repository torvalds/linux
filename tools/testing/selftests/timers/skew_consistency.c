/* ADJ_FREQ Skew consistency test
 *		by: john stultz (johnstul@us.ibm.com)
 *		(C) Copyright IBM 2012
 *		Licensed under the GPLv2
 *
 *  NOTE: This is a meta-test which cranks the ADJ_FREQ knob back
 *  and forth and watches for consistency problems. Thus this test requires
 *  that the inconsistency-check tests be present in the same directory it
 *  is run from.
 *
 *  To build:
 *	$ gcc skew_consistency.c -o skew_consistency -lrt
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
#include <sys/time.h>
#include <sys/timex.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include "../kselftest.h"

int main(int argc, char **argv)
{
	struct timex tx;
	int ret, ppm;
	pid_t pid;


	printf("Running Asynchronous Frequency Changing Tests...\n");

	pid = fork();
	if (!pid)
		return system("./inconsistency-check -c 1 -t 600");

	ppm = 500;
	ret = 0;

	while (pid != waitpid(pid, &ret, WNOHANG)) {
		ppm = -ppm;
		tx.modes = ADJ_FREQUENCY;
		tx.freq = ppm << 16;
		adjtimex(&tx);
		usleep(500000);
	}

	/* Set things back */
	tx.modes = ADJ_FREQUENCY;
	tx.offset = 0;
	adjtimex(&tx);


	if (ret) {
		printf("[FAILED]\n");
		ksft_exit_fail();
	}
	printf("[OK]\n");
	ksft_exit_pass();
}
