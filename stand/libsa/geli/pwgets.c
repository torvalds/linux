/*	$NetBSD: gets.c,v 1.6 1995/10/11 21:16:57 pk Exp $	*/

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
 *	@(#)gets.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "stand.h"

/* gets() with constrained input length, for passwords */

void
pwgets(char *buf, int n, int hide)
{
    int c;
    char *lp;

    for (lp = buf;;)
	switch (c = getchar() & 0177) {
	case '\n':
	case '\r':
	    *lp = '\0';
	    putchar('\n');
	    return;
	case '\b':
	case '\177':
	    if (lp > buf) {
		lp--;
		if (hide == 0) {
			putchar('\b');
			putchar(' ');
			putchar('\b');
		}
	    }
	    break;
	case 'u'&037:
	case 'w'&037:
	    lp = buf;
	    putchar('\n');
	    break;
	default:
	    if ((n < 1) || ((lp - buf) < n - 1)) {
		*lp++ = c;
		if (hide == 0) {
			putchar('*');
		}
	    }
	}
    /*NOTREACHED*/
}
