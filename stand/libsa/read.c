/*	$NetBSD: read.c,v 1.8 1997/01/22 00:38:12 cgd Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	@(#)read.c	8.1 (Berkeley) 6/11/93
 *
 *
 * Copyright (c) 1989, 1990, 1991 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Author: Alessandro Forin
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include "stand.h"

ssize_t
read(int fd, void *dest, size_t bcount)
{
	struct open_file *f = &files[fd];
	size_t resid;

	if ((unsigned)fd >= SOPEN_MAX || !(f->f_flags & F_READ)) {
		errno = EBADF;
		return (-1);
	}
	if (f->f_flags & F_RAW) {
		twiddle(4);
		errno = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
		    btodb(f->f_offset), bcount, dest, &resid);
		if (errno)
			return (-1);
		f->f_offset += resid;
		return (resid);
	}

	/*
	 * Optimise reads from regular files using a readahead buffer.
	 * If the request can't be satisfied from the current buffer contents,
	 * check to see if it should be bypassed, or refill the buffer and
	 * complete the request.
	 */
	resid = bcount;
	for (;;) {
		size_t	ccount, cresid;
		/* how much can we supply? */
		ccount = imin(f->f_ralen, resid);
		if (ccount > 0) {
			bcopy(f->f_rabuf + f->f_raoffset, dest, ccount);
			f->f_raoffset += ccount;
			f->f_ralen -= ccount;
			resid -= ccount;
			if (resid == 0)
				return (bcount);
			dest = (char *)dest + ccount;
		}

		/* will filling the readahead buffer again not help? */
		if (f->f_rabuf == NULL || resid >= SOPEN_RASIZE) {
			/*
			 * bypass the rest of the request and leave the
			 * buffer empty
			 */
			errno = (f->f_ops->fo_read)(f, dest, resid, &cresid);
			if (errno != 0)
				return (-1);
			return (bcount - cresid);
		}

		/* fetch more data */
		errno = (f->f_ops->fo_read)(f, f->f_rabuf, SOPEN_RASIZE,
		    &cresid);
		if (errno != 0)
			return (-1);
		f->f_raoffset = 0;
		f->f_ralen = SOPEN_RASIZE - cresid;
		/* no more data, return what we had */
		if (f->f_ralen == 0)
			return (bcount - resid);
	}
}
