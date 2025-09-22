/*	$OpenBSD: confstr.c,v 1.10 2013/03/07 06:00:18 guenther Exp $ */
/*-
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

#include <errno.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>


static const char v6_width_restricted_envs[] = {
#ifdef __LP64__
	"POSIX_V6_LP64_OFF64\n"
	"POSIX_V6_LPBIG_OFFBIG"
#else
	"POSIX_V6_ILP32_OFFBIG"
#endif
};

static const char v7_width_restricted_envs[] = {
#ifdef __LP64__
	"POSIX_V7_LP64_OFF64\n"
	"POSIX_V7_LPBIG_OFFBIG"
#else
	"POSIX_V7_ILP32_OFFBIG"
#endif
};

size_t
confstr(int name, char *buf, size_t len)
{
	switch (name) {
	case _CS_PATH:
		return (strlcpy(buf, _PATH_STDPATH, len) + 1);

	/* no configuration-defined value */
	case _CS_POSIX_V6_ILP32_OFF32_CFLAGS:
	case _CS_POSIX_V6_ILP32_OFF32_LDFLAGS:
	case _CS_POSIX_V6_ILP32_OFF32_LIBS:
	case _CS_POSIX_V7_ILP32_OFF32_CFLAGS:
	case _CS_POSIX_V7_ILP32_OFF32_LDFLAGS:
	case _CS_POSIX_V7_ILP32_OFF32_LIBS:
		return (0);

	/* these are either NULL or empty, depending on the platform */
	case _CS_POSIX_V6_ILP32_OFFBIG_CFLAGS:
	case _CS_POSIX_V6_ILP32_OFFBIG_LDFLAGS:
	case _CS_POSIX_V6_ILP32_OFFBIG_LIBS:
	case _CS_POSIX_V7_ILP32_OFFBIG_CFLAGS:
	case _CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS:
	case _CS_POSIX_V7_ILP32_OFFBIG_LIBS:
#ifdef __LP64__
		return (0);
#else
		if (len > 0)
			buf[0] = '\0';
		return (1);
#endif
	case _CS_POSIX_V6_LP64_OFF64_CFLAGS:
	case _CS_POSIX_V6_LP64_OFF64_LDFLAGS:
	case _CS_POSIX_V6_LP64_OFF64_LIBS:
	case _CS_POSIX_V6_LPBIG_OFFBIG_CFLAGS:
	case _CS_POSIX_V6_LPBIG_OFFBIG_LDFLAGS:
	case _CS_POSIX_V6_LPBIG_OFFBIG_LIBS:
	case _CS_POSIX_V7_LP64_OFF64_CFLAGS:
	case _CS_POSIX_V7_LP64_OFF64_LDFLAGS:
	case _CS_POSIX_V7_LP64_OFF64_LIBS:
	case _CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS:
	case _CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS:
	case _CS_POSIX_V7_LPBIG_OFFBIG_LIBS:
#ifdef __LP64__
		if (len > 0)
			buf[0] = '\0';
		return (1);
#else
		return (0);
#endif

	/* zero length strings */
	case _CS_POSIX_V7_THREADS_CFLAGS:
	case _CS_V6_ENV:
	case _CS_V7_ENV:
		if (len > 0)
			buf[0] = '\0';
		return (1);

	case _CS_POSIX_V7_THREADS_LDFLAGS:
		return (strlcpy(buf, "-lpthread", len) + 1);

	case _CS_POSIX_V6_WIDTH_RESTRICTED_ENVS:
		return (strlcpy(buf, v6_width_restricted_envs, len) + 1);
	
	case _CS_POSIX_V7_WIDTH_RESTRICTED_ENVS:
		return (strlcpy(buf, v7_width_restricted_envs, len) + 1);

	default:
		errno = EINVAL;
		return (0);
	}
	/* NOTREACHED */
}
