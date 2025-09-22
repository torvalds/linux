/*	$OpenBSD: open.c,v 1.11 2016/03/14 23:08:06 krw Exp $	*/
/*	$NetBSD: open.c,v 1.12 1996/09/30 16:01:21 ws Exp $	*/

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
 *	@(#)open.c	8.1 (Berkeley) 6/11/93
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

#include "stand.h"

struct open_file files[SOPEN_MAX];

/*
 *	File primitives proper
 */

int
#ifndef __INTERNAL_LIBSA_CREAD
open(const char *fname, int mode)
#else
oopen(const char *fname, int mode)
#endif
{
	struct open_file *f;
	int fd, i, error;
	char *file;

	/* find a free file descriptor */
	for (fd = 0, f = files; fd < SOPEN_MAX; fd++, f++)
		if (f->f_flags == 0)
			goto fnd;
	errno = EMFILE;
	return (-1);
fnd:
	/*
	 * Try to open the device.
	 * Convert open mode (0,1,2) to F_READ, F_WRITE.
	 */
	f->f_flags = mode + 1;
	f->f_dev = NULL;
	f->f_ops = NULL;
	file = NULL;
	error = devopen(f, fname, &file);
	if (error ||
	    (((f->f_flags & F_NODEV) == 0) && f->f_dev == NULL))
		goto err;

	/* see if we opened a raw device; otherwise, 'file' is the file name. */
	if (file == NULL || *file == '\0') {
		f->f_flags |= F_RAW;
		return (fd);
	}

	/* pass file name to the different filesystem open routines */
	for (i = 0; i < nfsys; i++) {
		/* convert mode (0,1,2) to FREAD, FWRITE. */
		error = (file_system[i].open)(file, f);
		if (error == 0) {
			f->f_ops = &file_system[i];
			return (fd);
		}
		if (error == ENOENT || error == ENOTDIR)
			break;
	}
	if (!error)
		error = ENOENT;

	f->f_dev->dv_close(f);
err:
	f->f_flags = 0;
	errno = error;
	return (-1);
}
