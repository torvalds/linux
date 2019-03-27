/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
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

#include <sys/cdefs.h>
__SCCSID("@(#)confstr.c	8.1 (Berkeley) 6/4/93");
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>


size_t
confstr(int name, char *buf, size_t len)
{
	const char *p;
	const char UPE[] = "unsupported programming environment";

	switch (name) {
	case _CS_PATH:
		p = _PATH_STDPATH;
		goto docopy;

		/*
		 * POSIX/SUS ``Programming Environments'' stuff
		 *
		 * We don't support more than one programming environment
		 * on any platform (yet), so we just return the empty
		 * string for the environment we are compiled for,
		 * and the string "unsupported programming environment"
		 * for anything else.  (The Standard says that if these
		 * values are used on a system which does not support
		 * this environment -- determined via sysconf() -- then
		 * the value we return is unspecified.  So, we return
		 * something which will cause obvious breakage.)
		 */
	case _CS_POSIX_V6_ILP32_OFF32_CFLAGS:
	case _CS_POSIX_V6_ILP32_OFF32_LDFLAGS:
	case _CS_POSIX_V6_ILP32_OFF32_LIBS:
	case _CS_POSIX_V6_LPBIG_OFFBIG_CFLAGS:
	case _CS_POSIX_V6_LPBIG_OFFBIG_LDFLAGS:
	case _CS_POSIX_V6_LPBIG_OFFBIG_LIBS:
		/*
		 * These two environments are never supported.
		 */
		p = UPE;
		goto docopy;

	case _CS_POSIX_V6_ILP32_OFFBIG_CFLAGS:
	case _CS_POSIX_V6_ILP32_OFFBIG_LDFLAGS:
	case _CS_POSIX_V6_ILP32_OFFBIG_LIBS:
		if (sizeof(long) * CHAR_BIT == 32 &&
		    sizeof(off_t) > sizeof(long))
			p = "";
		else
			p = UPE;
		goto docopy;

	case _CS_POSIX_V6_LP64_OFF64_CFLAGS:
	case _CS_POSIX_V6_LP64_OFF64_LDFLAGS:
	case _CS_POSIX_V6_LP64_OFF64_LIBS:
		if (sizeof(long) * CHAR_BIT >= 64 &&
		    sizeof(void *) * CHAR_BIT >= 64 &&
		    sizeof(int) * CHAR_BIT >= 32 &&
		    sizeof(off_t) >= sizeof(long))
			p = "";
		else
			p = UPE;
		goto docopy;

	case _CS_POSIX_V6_WIDTH_RESTRICTED_ENVS:
		/* XXX - should have more complete coverage */
		if (sizeof(long) * CHAR_BIT >= 64)
			p = "_POSIX_V6_LP64_OFF64";
		else
			p = "_POSIX_V6_ILP32_OFFBIG";
		goto docopy;

docopy:
		if (len != 0 && buf != NULL)
			strlcpy(buf, p, len);
		return (strlen(p) + 1);

	default:
		errno = EINVAL;
		return (0);
	}
	/* NOTREACHED */
}
