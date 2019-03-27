/*-
 * Copyright (c) 2001 Robert N. M. Watson
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

#include <sys/types.h>

#include <stdio.h>
#include <unistd.h>

#include "scenario.h"

int
main(int argc, char *argv[])
{
	int error;

	fprintf(stderr, "test capabilities: ");
#ifdef SETSUGID_SUPPORTED
	fprintf(stderr, "[SETSUGID_SUPPORTED] ");
#endif
#ifdef SETSUGID_SUPPORTED_BUT_NO_LIBC_STUB
	fprintf(stderr, "[SETSUGID_SUPPORTED_BUT_NO_LIBC_STUB] ");
#endif
#ifdef CHECK_CRED_SET
	fprintf(stderr, "[CHECK_CRED_SET] ");
#endif
	fprintf(stderr, "\n");

	error = setugid(1);
	if (error) {
		perror("setugid");
		fprintf(stderr,
		    "This test suite requires options REGRESSION\n");
		return (-1);
	}

	enact_scenarios();

	return (0);
}

