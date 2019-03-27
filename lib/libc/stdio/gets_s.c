/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2017, 2018
 *	Cyril S. E. Schubert
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include "un-namespace.h"
#include "libc_private.h"
#include "local.h"

static inline char *
_gets_s(char *buf, rsize_t n)
{
	int c;
	char *s;

	ORIENT(stdin, -1);
	for (s = buf, n--; (c = __sgetc(stdin)) != '\n' && n > 0 ; n--) {
		if (c == EOF) {
			if (s == buf) {
				return (NULL);
			} else
				break;
		} else
			*s++ = c;
	}

	/*
 	 * If end of buffer reached, discard until \n or eof.
	 * Then throw an error.
	 */
	if (n == 0) {
		/* discard */
		while ((c = __sgetc(stdin)) != '\n' && c != EOF);
		/* throw the error after lock released prior to exit */
		__throw_constraint_handler_s("gets_s : end of buffer", E2BIG);
		return (NULL);
	}
	*s = 0;
	return (buf);
}

/* ISO/IEC 9899:2011 K.3.7.4.1 */
char *
gets_s(char *buf, rsize_t n)
{
	char *ret;
	if (buf == NULL) {
		__throw_constraint_handler_s("gets_s : str is NULL", EINVAL);
		return(NULL);
	} else if (n > RSIZE_MAX) {
		__throw_constraint_handler_s("gets_s : n > RSIZE_MAX",
			EINVAL);
		return(NULL);
	} else if (n == 0) {
		__throw_constraint_handler_s("gets_s : n == 0", EINVAL);
		return(NULL);
	}

	FLOCKFILE_CANCELSAFE(stdin);
	ret = _gets_s(buf, n);
	FUNLOCKFILE_CANCELSAFE();
	return (ret);
}
