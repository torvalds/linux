/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1987, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)yes.c	8.1 (Berkeley) 6/6/93";
#else
static const char rcsid[] = "$FreeBSD$";
#endif
#endif /* not lint */

#include <capsicum_helpers.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	char buf[8192];
	char y[2] = { 'y', '\n' };
	char * exp = y;
	size_t buflen = 0;
	size_t explen = sizeof(y);
	size_t more;
	ssize_t ret;

	if (caph_limit_stdio() < 0 || caph_enter() < 0)
		err(1, "capsicum");

	if (argc > 1) {
		exp = argv[1];
		explen = strlen(exp) + 1;
		exp[explen - 1] = '\n';
	}

	if (explen <= sizeof(buf)) {
		while (buflen < sizeof(buf) - explen) {
			memcpy(buf + buflen, exp, explen);
			buflen += explen;
		}
		exp = buf;
		explen = buflen;
	}

	more = explen;
	while ((ret = write(STDOUT_FILENO, exp + (explen - more), more)) > 0)
		if ((more -= ret) == 0)
			more = explen;

	err(1, "stdout");
	/*NOTREACHED*/
}
