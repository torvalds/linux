/*-
 * Copyright (c) 2011 Giovanni Trematerra <giovanni.trematerra@gmail.com>
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

/*
 * $FreeBSD$
 * Test conformance to stat(2) SUSv4 description:
 *  "For all other file types defined in this volume of POSIX.1-2008, the
 *  structure members st_mode, st_ino, st_dev, st_uid, st_gid, st_atim,
 *  st_ctim, and st_mtim shall have meaningful values ...".
 * Check that st_dev and st_ino are meaningful.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
	int pipefd[2];
	struct stat st1, st2;

	if (pipe(pipefd) == -1)
		err(1, "FAIL: pipe");

	if (fstat(pipefd[0], &st1) == -1)
		err(1, "FAIL: fstat st1");
	if (fstat(pipefd[1], &st2) == -1)
		err(1, "FAIL: fstat st2");
	if (st1.st_dev != st2.st_dev || st1.st_dev == 0 || st2.st_dev == 0)
		errx(1, "FAIL: wrong dev number %ju %ju",
		    (uintmax_t)st1.st_dev, (uintmax_t)st2.st_dev);
	if (st1.st_ino == st2.st_ino)
		errx(1, "FAIL: inode numbers are equal: %ju",
		    (uintmax_t)st1.st_ino);

	close(pipefd[0]);
	close(pipefd[1]);
	printf("PASS\n");

	return (0);
}
