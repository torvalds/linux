/*	$OpenBSD: snprintf.c,v 1.7 2019/05/11 16:56:47 deraadt Exp $	*/
/*	$NetBSD: printf.c,v 1.10 1996/11/30 04:19:21 gwr Exp $	*/

/*-
 * Copyright (c) 1993
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
 *	@(#)printf.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/types.h>
#include <sys/stdarg.h>

#include "stand.h"

extern void kprintn(void (*)(int), u_long, int);
extern void kdoprnt(void (*)(int), const char *, va_list);

static void sputchar(int);

static char *sbuf, *sbuf_end;

void
sputchar(int c)
{
	if (sbuf < sbuf_end)
		*sbuf = c;
	sbuf++;
}

int
snprintf(char *buf, size_t len, const char *fmt, ...)
{
	va_list ap;

	sbuf = buf;
	sbuf_end = sbuf + len;
	va_start(ap, fmt);
	kdoprnt(sputchar, fmt, ap);
	va_end(ap);

	if (sbuf < sbuf_end)
		*sbuf = '\0';
	else if (len > 0)
		*(sbuf_end - 1) = '\0';

	return sbuf - buf;
}
