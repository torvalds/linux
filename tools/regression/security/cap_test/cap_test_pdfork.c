/*-
 * Copyright (c) 2009-2011 Robert N. M. Watson
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

/*
 * Test routines to make sure a variety of system calls are or are not
 * available in capability mode.  The goal is not to see if they work, just
 * whether or not they return the expected ECAPMODE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <sys/capsium.h>
#include <sys/errno.h>
#include <sys/procdesc.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <time.h>

#include "cap_test.h"

int
test_pdfork(void)
{
	struct stat stat;
	int success = PASSED;
	int pd, error;
	pid_t pid;
	time_t now;

	//cap_enter();

	pid = pdfork(&pd, 0);
	if (pid < 0)
		err(-1, "pdfork");

	else if (pid == 0) {
		/*
		 * Child process.
		 *
		 * pd should not be a valid process descriptor.
		 */
		error = pdgetpid(pd, &pid);
		if (error != -1)
			FAILX("pdgetpid succeeded");
		else if (errno != EBADF)
			FAIL("pdgetpid failed, but errno != EBADF");

		exit(success);
	}

	/* Parent process. Ensure that [acm]times have been set correctly. */
	REQUIRE(fstat(pd, &stat));

	now = time(NULL);
	CHECK(now != (time_t)-1);

	CHECK(now >= stat.st_birthtime);
	CHECK((now - stat.st_birthtime) < 2);
	CHECK(stat.st_birthtime == stat.st_atime);
	CHECK(stat.st_atime == stat.st_ctime);
	CHECK(stat.st_ctime == stat.st_mtime);

	/* Wait for the child to finish. */
	error = pdgetpid(pd, &pid);
	CHECK(error == 0);
	CHECK(pid > 0);

	int status;
	while (waitpid(pid, &status, 0) != pid) {}
	if ((success == PASSED) && WIFEXITED(status))
		success = WEXITSTATUS(status);
	else
		success = FAILED;

	return (success);
}
