/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 - 1999
 *	HD Associates, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by HD Associates, Inc
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
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
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

#include "prutil.h"

int memlock(int argc, char *argv[])
{
	int e = 0;

	/* Is memory locking configured?
	 */
	errno = 0;
	if (sysconf(_SC_MEMLOCK) == -1) {
		if (errno != 0) {
			/* This isn't valid - may be a standard violation
			 */
			quit("(should not happen) sysconf(_SC_MEMLOCK)");
		}
		else {
			fprintf(stderr,
			"Memory locking is not supported in this environment.\n");
			e = -1;
		}
	}

	/* Lock yourself in memory:
	 */
	if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
		perror("mlockall(MCL_CURRENT | MCL_FUTURE)");
		e = errno;
	}
	else if (munlockall() == -1) {
		perror("munlockall");
		e = errno;
	}

	return e;
}

#ifdef NO_MEMLOCK
int mlockall(int flags)
{
	return EOPNOTSUPP;
}

int munlockall(void)
{
	return EOPNOTSUPP;
}
	

#endif

#ifdef STANDALONE_TESTS
int main(int argc, char *argv[]) { return memlock(argc, argv); }
#endif
