/*	$NetBSD: lseek.c,v 1.4 1997/01/22 00:38:10 cgd Exp $	*/

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
 *	@(#)lseek.c	8.1 (Berkeley) 6/11/93
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

#include "stand.h"

off_t
lseek(int fd, off_t offset, int where)
{
    off_t bufpos, filepos, target;
    struct open_file *f = &files[fd];

    if ((unsigned)fd >= SOPEN_MAX || f->f_flags == 0) {
	errno = EBADF;
	return (-1);
    }

    if (f->f_flags & F_RAW) {
	/*
	 * On RAW devices, update internal offset.
	 */
	switch (where) {
	case SEEK_SET:
	    f->f_offset = offset;
	    break;
	case SEEK_CUR:
	    f->f_offset += offset;
	    break;
	default:
	    errno = EOFFSET;
	    return (-1);
	}
	return (f->f_offset);
    }

    /*
     * If there is some unconsumed data in the readahead buffer and it
     * contains the desired offset, simply adjust the buffer offset and
     * length.  We don't bother with SEEK_END here, since the code to
     * handle it would fail in the same cases where the non-readahead
     * code fails (namely, for streams which cannot seek backward and whose
     * size isn't known in advance).
     */
    if (f->f_ralen != 0 && where != SEEK_END) {
	if ((filepos = (f->f_ops->fo_seek)(f, (off_t)0, SEEK_CUR)) == -1)
	    return (-1);
	bufpos = filepos - f->f_ralen;
	switch (where) {
	case SEEK_SET:
	    target = offset;
	    break;
	case SEEK_CUR:
	    target = bufpos + offset;
	    break;
	default:
	    errno = EINVAL;
	    return (-1);
	}
	if (bufpos <= target && target < filepos) {
	    f->f_raoffset += target - bufpos;
	    f->f_ralen -= target - bufpos;
	    return (target);
	}
    }

    /*
     * If this is a relative seek, we need to correct the offset for
     * bytes that we have already read but the caller doesn't know
     * about.
     */
    if (where == SEEK_CUR)
	offset -= f->f_ralen;

    /* 
     * Invalidate the readahead buffer.
     */
    f->f_ralen = 0;

    return (f->f_ops->fo_seek)(f, offset, where);
}
