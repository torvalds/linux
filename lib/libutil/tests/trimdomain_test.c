/*
 * Copyright (C) 2005 Brooks Davis. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <errno.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TESTDOMAIN ".domain.example.com"
#define TESTHOST "testhost"
#define TESTFQDN "testhost" TESTDOMAIN

int failures = 0;
int tests = 0;

/*
 * Evily override gethostname(3) so trimdomain always gets the same result.
 * This makes the tests much easier to write and less likely to fail on
 * oddly configured systems.
 */
int
gethostname(char *name, size_t namelen)
{
	if (strlcpy(name, TESTFQDN, namelen) > namelen) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	return (0);
}

void
testit(const char *input, int hostsize, const char *output, const char *test)
{
	char *testhost;
	const char *expected = (output == NULL) ? input : output;

	testhost = strdup(input);
	trimdomain(testhost, hostsize < 0 ? (int)strlen(testhost) : hostsize);
	tests++;
	if (strcmp(testhost, expected) != 0) {
		printf("not ok %d - %s\n", tests, test);
		printf("# %s -> %s (expected %s)\n", input, testhost, expected);
	} else
		printf("ok %d - %s\n", tests, test);
	free(testhost);
	return;
}

int
main(void)
{

	printf("1..5\n");

	testit(TESTFQDN, -1, TESTHOST, "self");
	testit("XXX" TESTDOMAIN, -1, "XXX", "different host, same domain");
	testit("XXX" TESTDOMAIN, 1, NULL, "short hostsize");
	testit("bogus.example.net", -1, NULL, "arbitrary host");
	testit("XXX." TESTFQDN, -1, NULL, "domain is local hostname");

	return (0);
}
