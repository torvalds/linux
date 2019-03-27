/*	$NetBSD: open.c,v 1.16 1997/01/28 09:41:03 pk Exp $	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "stand.h"

struct fs_ops *exclusive_file_system;

struct open_file files[SOPEN_MAX];

static int
o_gethandle(void)
{
	int fd;

	for (fd = 0; fd < SOPEN_MAX; fd++)
		if (files[fd].f_flags == 0)
			return (fd);
	return (-1);
}

static void
o_rainit(struct open_file *f)
{
	f->f_rabuf = malloc(SOPEN_RASIZE);
	f->f_ralen = 0;
	f->f_raoffset = 0;
}

int
open(const char *fname, int mode)
{
	struct fs_ops *fs;
	struct open_file *f;
	int fd, i, error, besterror;
	const char *file;

	if ((fd = o_gethandle()) == -1) {
		errno = EMFILE;
		return (-1);
	}

	f = &files[fd];
	f->f_flags = mode + 1;
	f->f_dev = NULL;
	f->f_ops = NULL;
	f->f_offset = 0;
	f->f_devdata = NULL;
	file = NULL;

	if (exclusive_file_system != NULL) {
		fs = exclusive_file_system;
		error = (fs->fo_open)(fname, f);
		if (error == 0)
			goto ok;
		goto err;
	}

	error = devopen(f, fname, &file);
	if (error ||
	    (((f->f_flags & F_NODEV) == 0) && f->f_dev == NULL))
		goto err;

	/* see if we opened a raw device; otherwise, 'file' is the file name. */
	if (file == NULL || *file == '\0') {
		f->f_flags |= F_RAW;
		f->f_rabuf = NULL;
		return (fd);
	}

	/* pass file name to the different filesystem open routines */
	besterror = ENOENT;
	for (i = 0; file_system[i] != NULL; i++) {
		fs = file_system[i];
		error = (fs->fo_open)(file, f);
		if (error == 0)
			goto ok;
		if (error != EINVAL)
			besterror = error;
	}
	error = besterror;

	if ((f->f_flags & F_NODEV) == 0 && f->f_dev != NULL)
		f->f_dev->dv_close(f);
	if (error)
		devclose(f);

err:
	f->f_flags = 0;
	errno = error;
	return (-1);

ok:
	f->f_ops = fs;
	o_rainit(f);
	return (fd);
}
