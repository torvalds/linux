/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
static char sccsid[] = "@(#)ftell.c	8.2 (Berkeley) 5/4/95";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include "un-namespace.h"
#include "local.h"
#include "libc_private.h"

/*
 * standard ftell function.
 */
long
ftell(FILE *fp)
{
	off_t rv;

	rv = ftello(fp);
	if (rv > LONG_MAX) {
		errno = EOVERFLOW;
		return (-1);
	}
	return (rv);
}

/*
 * ftello: return current offset.
 */
off_t
ftello(FILE *fp)
{
	fpos_t rv;
	int ret;

	FLOCKFILE(fp);
	ret = _ftello(fp, &rv);
	FUNLOCKFILE(fp);
	if (ret)
		return (-1);
	if (rv < 0) {   /* Unspecified value because of ungetc() at 0 */
		errno = ESPIPE;
		return (-1);
	}
	return (rv);
}

int
_ftello(FILE *fp, fpos_t *offset)
{
	fpos_t pos;
	size_t n;

	if (fp->_seek == NULL) {
		errno = ESPIPE;			/* historic practice */
		return (1);
	}

	/*
	 * Find offset of underlying I/O object, then
	 * adjust for buffered bytes.
	 */
	if (!(fp->_flags & __SRD) && (fp->_flags & __SWR) &&
	    fp->_p != NULL && fp->_p - fp->_bf._base > 0 &&
	    ((fp->_flags & __SAPP) || (fp->_flags2 & __S2OAP))) {
		pos = _sseek(fp, (fpos_t)0, SEEK_END);
		if (pos == -1)
			return (1);
	} else if (fp->_flags & __SOFF)
		pos = fp->_offset;
	else {
		pos = _sseek(fp, (fpos_t)0, SEEK_CUR);
		if (pos == -1)
			return (1);
	}
	if (fp->_flags & __SRD) {
		/*
		 * Reading.  Any unread characters (including
		 * those from ungetc) cause the position to be
		 * smaller than that in the underlying object.
		 */
		if ((pos -= (HASUB(fp) ? fp->_ur : fp->_r)) < 0) {
			fp->_flags |= __SERR;
			errno = EIO;
			return (1);
		}
		if (HASUB(fp))
			pos -= fp->_r;  /* Can be negative at this point. */
	} else if ((fp->_flags & __SWR) && fp->_p != NULL &&
	    (n = fp->_p - fp->_bf._base) > 0) {
		/*
		 * Writing.  Any buffered characters cause the
		 * position to be greater than that in the
		 * underlying object.
		 */
		if (pos > OFF_MAX - n) {
			errno = EOVERFLOW;
			return (1);
		}
		pos += n;
	}
	*offset = pos;
	return (0);
}
