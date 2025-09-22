/*	$OpenBSD: fdclose.c,v 1.2 2025/08/04 01:44:33 dlg Exp $ */
/*-
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

#include <errno.h>
#include <stdio.h>
#include "local.h"

int
fdclose(FILE *fp, int *fdp)
{
	int r, err;

	if (fdp != NULL)
		*fdp = -1;

	if (fp->_flags == 0) {	/* not open! */
		errno = EBADF;
		return EOF;
	}

	FLOCKFILE(fp);
	r = 0;
	if (fp->_close != __sclose) {
		r = EOF;
		err = EOPNOTSUPP;
	} else if (fp->_file < 0) {
		r = EOF;
		err = EBADF;
	}
	if (r == EOF) {
		(void)__cleanfile(fp, 1);
		errno = err;
	} else {
		if (fdp != NULL)
			*fdp = fp->_file;
		r = __cleanfile(fp, 0);
	}
	FUNLOCKFILE(fp);
	__relefile(fp);

	return r;
}
