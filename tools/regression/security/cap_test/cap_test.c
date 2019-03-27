/*-
 * Copyright (c) 2008-2011 Robert N. M. Watson
 * Copyright (c) 2011 Jonathan Anderson
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/wait.h>

#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cap_test.h"

/* Initialize a named test. Requires test_NAME() function to be declared. */
#define	TEST_INIT(name)	{ #name, test_##name, FAILED }

/* All of the tests that can be run. */
struct test all_tests[] = {
	TEST_INIT(capmode),
	TEST_INIT(capabilities),
	TEST_INIT(fcntl),
	TEST_INIT(pdfork),
	TEST_INIT(pdkill),
	TEST_INIT(relative),
	TEST_INIT(sysctl),
};
int test_count = sizeof(all_tests) / sizeof(struct test);

int
main(int argc, char *argv[])
{

	/*
	 * If no tests have been specified at the command line, run them all.
	 */
	if (argc == 1) {
		printf("1..%d\n", test_count);

		for (int i = 0; i < test_count; i++)
			execute(i + 1, all_tests + i);
		return (0);
	}

	/*
	 * Otherwise, run only the specified tests.
	 */
	printf("1..%d\n", argc - 1);
	for (int i = 1; i < argc; i++)
	{
		int found = 0;
		for (int j = 0; j < test_count; j++) {
			if (strncmp(argv[i], all_tests[j].t_name,
			    strlen(argv[i])) == 0) {
				found = 1;
				execute(i, all_tests + j);
				break;
			}
		}

		if (found == 0)
			errx(-1, "No such test '%s'", argv[i]);
	}

	return (0);
}

int
execute(int id, struct test *t) {
	int result;

	pid_t pid = fork();
	if (pid < 0)
		err(-1, "fork");
	if (pid) {
		/* Parent: wait for result from child. */
		int status;
		while (waitpid(pid, &status, 0) != pid) {}
		if (WIFEXITED(status))
			result = WEXITSTATUS(status);
		else
			result = FAILED;
	} else {
		/* Child process: run the test. */
		exit(t->t_run());
	}

	printf("%s %d - %s\n",
		(result == PASSED) ? "ok" : "not ok",
		id, t->t_name);

	return (result);
}
