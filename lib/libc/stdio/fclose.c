/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993 The Regents of the University of California.
 * Copyright (c) 2013 Mariusz Zaborski <oshogbo@FreeBSD.org>
 * All rights reserved.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)fclose.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "un-namespace.h"
#include <spinlock.h>
#include "libc_private.h"
#include "local.h"

static int
cleanfile(FILE *fp, bool c)
{
	int r;

	r = fp->_flags & __SWR ? __sflush(fp) : 0;
	if (c) {
		if (fp->_close != NULL && (*fp->_close)(fp->_cookie) < 0)
			r = EOF;
	}

	if (fp->_flags & __SMBF)
		free((char *)fp->_bf._base);
	if (HASUB(fp))
		FREEUB(fp);
	if (HASLB(fp))
		FREELB(fp);
	fp->_file = -1;
	fp->_r = fp->_w = 0;	/* Mess up if reaccessed. */

	/*
	 * Lock the spinlock used to protect __sglue list walk in
	 * __sfp().  The __sfp() uses fp->_flags == 0 test as an
	 * indication of the unused FILE.
	 *
	 * Taking the lock prevents possible compiler or processor
	 * reordering of the writes performed before the final _flags
	 * cleanup, making sure that we are done with the FILE before
	 * it is considered available.
	 */
	STDIO_THREAD_LOCK();
	fp->_flags = 0;		/* Release this FILE for reuse. */
	STDIO_THREAD_UNLOCK();

	return (r);
}

int
fdclose(FILE *fp, int *fdp)
{
	int r, err;

	if (fdp != NULL)
		*fdp = -1;

	if (fp->_flags == 0) {	/* not open! */
		errno = EBADF;
		return (EOF);
	}

	FLOCKFILE_CANCELSAFE(fp);
	r = 0;
	if (fp->_close != __sclose) {
		r = EOF;
		errno = EOPNOTSUPP;
	} else if (fp->_file < 0) {
		r = EOF;
		errno = EBADF;
	}
	if (r == EOF) {
		err = errno;
		(void)cleanfile(fp, true);
		errno = err;
	} else {
		if (fdp != NULL)
			*fdp = fp->_file;
		r = cleanfile(fp, false);
	}
	FUNLOCKFILE_CANCELSAFE();

	return (r);
}

int
fclose(FILE *fp)
{
	int r;

	if (fp->_flags == 0) {	/* not open! */
		errno = EBADF;
		return (EOF);
	}

	FLOCKFILE_CANCELSAFE(fp);
	r = cleanfile(fp, true);
	FUNLOCKFILE_CANCELSAFE();

	return (r);
}
