/*-
 * Copyright (c) 2007 Sean C. Farley <scf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


const char value1[] = "Large ------------------ value";
const char value2[] = "Small -- value";
char nameValuePair[] = "less=more";
const char name[] = "PATH";
const char name2[] = "SHELL";
const int MaxIterations = 1000000;
const char Tabs[] = "\t\t\t";


static int
report_time(const char *action, struct timeval *startTime,
    struct timeval *endTime)
{
	int actionLen;
	int numTabs;

	actionLen = strlen(action);
	numTabs = 3 - actionLen / 8;

	return (printf("Time spent executing %s:%.*s%f\n", action, numTabs, Tabs,
	    (endTime->tv_sec - startTime->tv_sec) +
	    (double)(endTime->tv_usec - startTime->tv_usec) / 1000000));
}


int
main(int argc, char **argv)
{
	int iterations;
	struct rusage endUsage;
	struct rusage startUsage;

	/*
	 * getenv() on the existing environment.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		if (getenv(name) == NULL)
			err(EXIT_FAILURE, "getenv(name)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("getenv(name)", &startUsage.ru_utime, &endUsage.ru_utime);


	/*
	 * setenv() a variable with a large value.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		if (setenv(name, value1, 1) == -1)
			err(EXIT_FAILURE, "setenv(name, value1, 1)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("setenv(name, value1, 1)", &startUsage.ru_utime,
	    &endUsage.ru_utime);


	/*
	 * getenv() the new variable on the new environment.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		/* Set large value to variable. */
		if (getenv(name) == NULL)
			err(EXIT_FAILURE, "getenv(name)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("getenv(name)", &startUsage.ru_utime, &endUsage.ru_utime);


	/*
	 * getenv() a different variable on the new environment.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		/* Set large value to variable. */
		if (getenv(name2) == NULL)
			err(EXIT_FAILURE, "getenv(name2)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("getenv(name2)", &startUsage.ru_utime, &endUsage.ru_utime);


	/*
	 * setenv() a variable with a small value.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		if (setenv(name, value2, 1) == -1)
			err(EXIT_FAILURE, "setenv(name, value2, 1)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("setenv(name, value2, 1)", &startUsage.ru_utime,
	    &endUsage.ru_utime);


	/*
	 * getenv() a different variable on the new environment.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		/* Set large value to variable. */
		if (getenv(name2) == NULL)
			err(EXIT_FAILURE, "getenv(name)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("getenv(name)", &startUsage.ru_utime, &endUsage.ru_utime);


	/*
	 * getenv() a different variable on the new environment.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		/* Set large value to variable. */
		if (getenv(name2) == NULL)
			err(EXIT_FAILURE, "getenv(name2)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("getenv(name2)", &startUsage.ru_utime, &endUsage.ru_utime);


	/*
	 * putenv() a variable with a small value.
	 */
	getrusage(RUSAGE_SELF, &startUsage);

	/* Iterate over setting variable. */
	for (iterations = 0; iterations < MaxIterations; iterations++)
		if (putenv(nameValuePair) == -1)
			err(EXIT_FAILURE, "putenv(nameValuePair)");

	getrusage(RUSAGE_SELF, &endUsage);

	report_time("putenv(nameValuePair)", &startUsage.ru_utime,
	    &endUsage.ru_utime);


	exit(EXIT_SUCCESS);
}
