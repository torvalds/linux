/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Ed Schouten <ed@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>

#define	LEN_PATH_DEV	(sizeof(_PATH_DEV) - 1)

char *
ctermid(char *s)
{
	static char def[sizeof(_PATH_DEV) + SPECNAMELEN];
	struct stat sb;
	size_t dlen;
	int sverrno;

	if (s == NULL) {
		s = def;
		dlen = sizeof(def) - LEN_PATH_DEV;
	} else
		dlen = L_ctermid - LEN_PATH_DEV;
	strcpy(s, _PATH_TTY);

	/* Attempt to perform a lookup of the actual TTY pathname. */
	sverrno = errno;
	if (stat(_PATH_TTY, &sb) == 0 && S_ISCHR(sb.st_mode))
		(void)sysctlbyname("kern.devname", s + LEN_PATH_DEV,
		    &dlen, &sb.st_rdev, sizeof(sb.st_rdev));
	errno = sverrno;
	return (s);
}

char *
ctermid_r(char *s)
{

	return (s != NULL ? ctermid(s) : NULL);
}
