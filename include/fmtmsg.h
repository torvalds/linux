/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Mike Barcroft <mike@FreeBSD.org>
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef _FMTMSG_H_
#define	_FMTMSG_H_

/* Source of condition is... */
#define	MM_HARD		0x0001	/* ...hardware. */
#define	MM_SOFT		0x0002	/* ...software. */
#define	MM_FIRM		0x0004	/* ...firmware. */

/* Condition detected by... */
#define	MM_APPL		0x0010	/* ...application. */
#define	MM_UTIL		0x0020	/* ...utility. */
#define	MM_OPSYS	0x0040	/* ...operating system. */

/* Display on... */
#define	MM_PRINT	0x0100	/* ...standard error. */
#define	MM_CONSOLE	0x0200	/* ...system console. */

#define	MM_RECOVER	0x1000	/* Recoverable error. */
#define	MM_NRECOV	0x2000	/* Non-recoverable error. */

/* Severity levels. */
#define	MM_NOSEV	0	/* No severity level provided. */
#define	MM_HALT		1	/* Error causing application to halt. */
#define	MM_ERROR	2	/* Non-fault fault. */
#define	MM_WARNING	3	/* Unusual non-error condition. */
#define	MM_INFO		4	/* Informative message. */

/* Null options. */
#define	MM_NULLLBL	(char *)0
#define	MM_NULLSEV	0
#define	MM_NULLMC	0L
#define	MM_NULLTXT	(char *)0
#define	MM_NULLACT	(char *)0
#define	MM_NULLTAG	(char *)0

/* Return values. */
#define	MM_OK		0	/* Success. */
#define	MM_NOMSG	1	/* Failed to output to stderr. */
#define	MM_NOCON	2	/* Failed to output to console. */
#define	MM_NOTOK	3	/* Failed to output anything. */

int	fmtmsg(long, const char *, int, const char *, const char *,
	    const char *);

#endif /* !_FMTMSG_H_ */
