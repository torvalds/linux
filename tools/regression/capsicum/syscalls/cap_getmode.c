/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/capsicum.h>
#include <sys/procdesc.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "misc.h"

int
main(void)
{
	unsigned int mode;
	pid_t pid;
	int pfd;

	printf("1..27\n");

	mode = 666;
	CHECK(cap_getmode(&mode) == 0);
	/* If cap_getmode() succeeded mode should be modified. */
	CHECK(mode != 666);
	/* We are not in capability mode. */
	CHECK(mode == 0);

	/* Expect EFAULT. */
	errno = 0;
	CHECK(cap_getmode(NULL) == -1);
	CHECK(errno == EFAULT);
	errno = 0;
	CHECK(cap_getmode((void *)(uintptr_t)0xdeadc0de) == -1);
	CHECK(errno == EFAULT);

	/* If parent is not in capability mode, child after fork() also won't be. */
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork() failed");
	case 0:
		mode = 666;
		CHECK(cap_getmode(&mode) == 0);
		/* If cap_getmode() succeeded mode should be modified. */
		CHECK(mode != 666);
		/* We are not in capability mode. */
		CHECK(mode == 0);
		exit(0);
	default:
		if (waitpid(pid, NULL, 0) == -1)
			err(1, "waitpid() failed");
	}

	/* If parent is not in capability mode, child after pdfork() also won't be. */
	pid = pdfork(&pfd, 0);
	switch (pid) {
	case -1:
		err(1, "pdfork() failed");
	case 0:
		mode = 666;
		CHECK(cap_getmode(&mode) == 0);
		/* If cap_getmode() succeeded mode should be modified. */
		CHECK(mode != 666);
		/* We are not in capability mode. */
		CHECK(mode == 0);
		exit(0);
	default:
		if (pdwait(pfd) == -1)
			err(1, "pdwait() failed");
		close(pfd);
	}

	/* In capability mode... */

	CHECK(cap_enter() == 0);

	mode = 666;
	CHECK(cap_getmode(&mode) == 0);
	/* If cap_getmode() succeeded mode should be modified. */
	CHECK(mode != 666);
	/* We are in capability mode. */
	CHECK(mode == 1);

	/* Expect EFAULT. */
	errno = 0;
	CHECK(cap_getmode(NULL) == -1);
	CHECK(errno == EFAULT);
	errno = 0;
	CHECK(cap_getmode((void *)(uintptr_t)0xdeadc0de) == -1);
	CHECK(errno == EFAULT);

	/* If parent is in capability mode, child after fork() also will be. */
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork() failed");
	case 0:
		mode = 666;
		CHECK(cap_getmode(&mode) == 0);
		/* If cap_getmode() succeeded mode should be modified. */
		CHECK(mode != 666);
		/* We are in capability mode. */
		CHECK(mode == 1);
		exit(0);
	default:
		/*
		 * wait(2) and friends are not permitted in the capability mode,
		 * so we can only just wait for a while.
		 */
		sleep(1);
	}

	/* If parent is in capability mode, child after pdfork() also will be. */
	pid = pdfork(&pfd, 0);
	switch (pid) {
	case -1:
		err(1, "pdfork() failed");
	case 0:
		mode = 666;
		CHECK(cap_getmode(&mode) == 0);
		/* If cap_getmode() succeeded mode should be modified. */
		CHECK(mode != 666);
		/* We are in capability mode. */
		CHECK(mode == 1);
		exit(0);
	default:
		if (pdwait(pfd) == -1)
			err(1, "pdwait() failed");
		close(pfd);
	}

	exit(0);
}
