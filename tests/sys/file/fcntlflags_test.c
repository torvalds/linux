/*-
 * Copyright (c) 2013 Jilles Tjoelker
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

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/*
 * O_ACCMODE is currently defined incorrectly. This is what it should be.
 * Various code depends on the incorrect value.
 */
#define CORRECT_O_ACCMODE (O_ACCMODE | O_EXEC)

static int testnum;

static void
subtests(const char *path, int omode, const char *omodetext)
{
	int fd, flags1, flags2, flags3;

	fd = open(path, omode);
	if (fd == -1)
		printf("not ok %d - open(\"%s\", %s) failed\n",
		    testnum++, path, omodetext);
	else
		printf("ok %d - open(\"%s\", %s) succeeded\n",
		    testnum++, path, omodetext);
	flags1 = fcntl(fd, F_GETFL);
	if (flags1 == -1)
		printf("not ok %d - fcntl(F_GETFL) failed\n", testnum++);
	else if ((flags1 & CORRECT_O_ACCMODE) == omode)
		printf("ok %d - fcntl(F_GETFL) gave correct result\n",
		    testnum++);
	else
		printf("not ok %d - fcntl(F_GETFL) gave incorrect result "
		    "(%#x & %#x != %#x)\n",
		    testnum++, flags1, CORRECT_O_ACCMODE, omode);
	if (fcntl(fd, F_SETFL, flags1) == -1)
		printf("not ok %d - fcntl(F_SETFL) same flags failed\n",
		    testnum++);
	else
		printf("ok %d - fcntl(F_SETFL) same flags succeeded\n",
		    testnum++);
	flags2 = fcntl(fd, F_GETFL);
	if (flags2 == -1)
		printf("not ok %d - fcntl(F_GETFL) failed\n", testnum++);
	else if (flags2 == flags1)
		printf("ok %d - fcntl(F_GETFL) gave same result\n",
		    testnum++);
	else
		printf("not ok %d - fcntl(F_SETFL) caused fcntl(F_GETFL) to "
		    "change from %#x to %#x\n",
		    testnum++, flags1, flags2);
	if (fcntl(fd, F_SETFL, flags2 | O_NONBLOCK) == -1)
		printf("not ok %d - fcntl(F_SETFL) O_NONBLOCK failed\n",
		    testnum++);
	else
		printf("ok %d - fcntl(F_SETFL) O_NONBLOCK succeeded\n",
		    testnum++);
	flags3 = fcntl(fd, F_GETFL);
	if (flags3 == -1)
		printf("not ok %d - fcntl(F_GETFL) failed\n", testnum++);
	else if (flags3 == (flags2 | O_NONBLOCK))
		printf("ok %d - fcntl(F_GETFL) gave expected result\n",
		    testnum++);
	else
		printf("not ok %d - fcntl(F_SETFL) gave unexpected result "
		    "(%#x != %#x)\n",
		    testnum++, flags3, flags2 | O_NONBLOCK);
	(void)close(fd);
}

int
main(int argc __unused, char **argv __unused)
{
	printf("1..24\n");
	testnum = 1;
	subtests("/dev/null", O_RDONLY, "O_RDONLY");
	subtests("/dev/null", O_WRONLY, "O_WRONLY");
	subtests("/dev/null", O_RDWR, "O_RDWR");
	subtests("/bin/sh", O_EXEC, "O_EXEC");
	return (0);
}
