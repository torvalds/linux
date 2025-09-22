/*	$OpenBSD: fwrite.c,v 1.12 2015/08/31 02:53:57 guenther Exp $ */
/*-
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "local.h"
#include "fvwrite.h"

#define MUL_NO_OVERFLOW	(1UL << (sizeof(size_t) * 4))

/*
 * Write `count' objects (each size `size') from memory to the given file.
 * Return the number of whole objects written.
 */
size_t
fwrite(const void *buf, size_t size, size_t count, FILE *fp)
{
	size_t n;
	struct __suio uio;
	struct __siov iov;
	int ret;

	/*
	 * Extension:  Catch integer overflow
	 */
	if ((size >= MUL_NO_OVERFLOW || count >= MUL_NO_OVERFLOW) &&
	    size > 0 && SIZE_MAX / size < count) {
		errno = EOVERFLOW;
		fp->_flags |= __SERR;
		return (0);
	}

	/*
	 * ANSI and SUSv2 require a return value of 0 if size or count are 0.
	 */
	if ((n = count * size) == 0)
		return (0);

	iov.iov_base = (void *)buf;
	uio.uio_resid = iov.iov_len = n;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;

	/*
	 * The usual case is success (__sfvwrite returns 0);
	 * skip the divide if this happens, since divides are
	 * generally slow and since this occurs whenever size==0.
	 */
	FLOCKFILE(fp);
	_SET_ORIENTATION(fp, -1);
	ret = __sfvwrite(fp, &uio);
	FUNLOCKFILE(fp);
	if (ret == 0)
		return (count);
	return ((n - uio.uio_resid) / size);
}
DEF_STRONG(fwrite);
