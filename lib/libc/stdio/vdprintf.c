/*	$OpenBSD: vdprintf.c,v 1.4 2025/08/08 15:58:53 yasuoka Exp $	*/
/*	$FreeBSD: src/lib/libc/stdio/vdprintf.c,v 1.4 2012/11/17 01:49:40 svnexp Exp $ */

/*-
 * Copyright (c) 2009 David Schultz <das@FreeBSD.org>
 * All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "local.h"

static int
__dwrite(void *cookie, const char *buf, int n)
{
	int *fdp = cookie;
	return (write(*fdp, buf, n));
}

int
vdprintf(int fd, const char * __restrict fmt, va_list ap)
{
	FILE f = FILEINIT(__SWR);
	unsigned char buf[BUFSIZ];
	int ret;

	f._p = buf;
	f._w = sizeof(buf);
	f._bf._base = buf;
	f._bf._size = sizeof(buf);
	f._cookie = &fd;
	f._write = __dwrite;

	if ((ret = __vfprintf(&f, fmt, ap)) < 0)
		return ret;

	return __sflush(&f) ? EOF : ret;
}
DEF_WEAK(vdprintf);
