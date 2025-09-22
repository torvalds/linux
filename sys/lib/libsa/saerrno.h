/*	$OpenBSD: saerrno.h,v 1.9 2014/11/19 20:28:56 miod Exp $	*/
/*	$NetBSD: saerrno.h,v 1.6 1995/09/18 21:19:45 pk Exp $	*/

/*
 * Copyright (c) 1988, 1993
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
 *	@(#)saerrno.h	8.1 (Berkeley) 6/11/93
 */

#include <sys/errno.h>

extern int errno;

/* special stand error codes */
#define	EADAPT	(ELAST+1)	/* bad adaptor */
#define	ECTLR	(ELAST+2)	/* bad controller */
#define	EUNIT	(ELAST+3)	/* bad drive */
#define	EPART	(ELAST+4)	/* bad partition */
#define	ERDLAB	(ELAST+5)	/* can't read disk label */
#define	EOFFSET	(ELAST+6)	/* relative seek not supported */
#define	EBSE	(ELAST+7)	/* bad sector error */
#define	EECC	(ELAST+8)	/* uncorrectable ecc error */
#define	EHER	(ELAST+9)	/* hard error */
#define	ESALAST	(ELAST+9)

const char *strerror(int err);
