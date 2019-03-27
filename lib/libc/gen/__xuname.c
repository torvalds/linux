/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994
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
 *
 * From: @(#)uname.c	8.1 (Berkeley) 1/4/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int
__xuname(int namesize, void *namebuf)
{
	int mib[2], rval;
	size_t len;
	char *p, *q;
	int oerrno;

	rval = 0;
	q = (char *)namebuf;

	mib[0] = CTL_KERN;

	if ((p = getenv("UNAME_s")))
		strlcpy(q, p, namesize);
	else {
		mib[1] = KERN_OSTYPE;
		len = namesize;
		oerrno = errno;
		if (sysctl(mib, 2, q, &len, NULL, 0) == -1) {
			if (errno == ENOMEM)
				errno = oerrno;
			else
				rval = -1;
		}
		q[namesize - 1] = '\0';
	}
	q += namesize;

	mib[1] = KERN_HOSTNAME;
	len = namesize;
	oerrno = errno;
	if (sysctl(mib, 2, q, &len, NULL, 0) == -1) {
		if (errno == ENOMEM)
			errno = oerrno;
		else
			rval = -1;
	}
	q[namesize - 1] = '\0';
	q += namesize;

	if ((p = getenv("UNAME_r")))
		strlcpy(q, p, namesize);
	else {
		mib[1] = KERN_OSRELEASE;
		len = namesize;
		oerrno = errno;
		if (sysctl(mib, 2, q, &len, NULL, 0) == -1) {
			if (errno == ENOMEM)
				errno = oerrno;
			else
				rval = -1;
		}
		q[namesize - 1] = '\0';
	}
	q += namesize;

	if ((p = getenv("UNAME_v")))
		strlcpy(q, p, namesize);
	else {

		/*
		 * The version may have newlines in it, turn them into
		 * spaces.
		 */
		mib[1] = KERN_VERSION;
		len = namesize;
		oerrno = errno;
		if (sysctl(mib, 2, q, &len, NULL, 0) == -1) {
			if (errno == ENOMEM)
				errno = oerrno;
			else
				rval = -1;
		}
		q[namesize - 1] = '\0';
		for (p = q; len--; ++p) {
			if (*p == '\n' || *p == '\t') {
				if (len > 1)
					*p = ' ';
				else
					*p = '\0';
			}
		}
	}
	q += namesize;

	if ((p = getenv("UNAME_m")))
		strlcpy(q, p, namesize);
	else {
		mib[0] = CTL_HW;
		mib[1] = HW_MACHINE;
		len = namesize;
		oerrno = errno;
		if (sysctl(mib, 2, q, &len, NULL, 0) == -1) {
			if (errno == ENOMEM)
				errno = oerrno;
			else
				rval = -1;
		}
		q[namesize - 1] = '\0';
	}

	return (rval);
}
