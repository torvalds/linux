/*-
 * Copyright (c) 2010 Jilles Tjoelker
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

#include <sys/select.h>

#include <err.h>
#include <stdio.h>
#include <unistd.h>

/*
 * Check that pipes can be selected for writing in the reverse direction.
 */
int
main(void)
{
	int pip[2];
	fd_set set;
	int n;

	if (pipe(pip) == -1)
		err(1, "FAIL: pipe");

	FD_ZERO(&set);
	FD_SET(pip[0], &set);
	n = select(pip[1] + 1, NULL, &set, NULL, &(struct timeval){ 0, 0 });
	if (n != 1)
		errx(1, "FAIL: select initial reverse direction");

	n = write(pip[0], "x", 1);
	if (n != 1)
		err(1, "FAIL: write reverse direction");

	FD_ZERO(&set);
	FD_SET(pip[0], &set);
	n = select(pip[1] + 1, NULL, &set, NULL, &(struct timeval){ 0, 0 });
	if (n != 1)
		errx(1, "FAIL: select reverse direction after write");

	printf("PASS\n");

	return (0);
}
