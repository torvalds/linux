/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996-1999
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
#define _POSIX_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <unistd.h>
#include <stdio.h>

int p26(int ac, char *av[])
{
	int ret = 0;

	#ifndef _POSIX_VERSION
	printf("POSIX is not supported.\n");
	ret = -1;
	#else	/* _POSIX_VERSION */

	#if (_POSIX_VERSION == 198808L)
	printf("POSIX.1 is supported but not POSIX.1B (FIPS 151-1)\n");
	#elif (_POSIX_VERSION == 199009L)
	printf("POSIX.1 is supported but not POSIX.1B (FIPS 151-2)\n");
	#elif (_POSIX_VERSION >= 199309L)
	printf("POSIX.1 and POSIX.1B are supported.\n");
	#else
	printf("_POSIX_VERSION (%ld) not 198808, 199009, or >= 199309.\n",
		_POSIX_VERSION);
	ret = -1;
	#endif

	#endif	/* _POSIX_VERSION */
	return ret;
}
#ifdef STANDALONE_TESTS
int main(int argc, char *argv[]) { return p26(argc, argv); }
#endif
