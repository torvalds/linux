/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ttydefaults.h	8.4 (Berkeley) 1/21/94
 * $FreeBSD$
 */

/*
 * System wide defaults for terminal state.
 */
#ifndef _SYS_TTYDEFAULTS_H_
#define	_SYS_TTYDEFAULTS_H_

/*
 * Defaults on "first" open.
 */
#define	TTYDEF_IFLAG	(BRKINT	| ICRNL	| IMAXBEL | IXON | IXANY)
#define	TTYDEF_OFLAG	(OPOST | ONLCR)
#define	TTYDEF_LFLAG_NOECHO (ICANON | ISIG | IEXTEN)
#define	TTYDEF_LFLAG_ECHO (TTYDEF_LFLAG_NOECHO \
	| ECHO | ECHOE | ECHOKE | ECHOCTL)
#define	TTYDEF_LFLAG TTYDEF_LFLAG_ECHO
#define	TTYDEF_CFLAG	(CREAD | CS8 | HUPCL)
#define	TTYDEF_SPEED	(B9600)

/*
 * Control Character Defaults
 */
/*
 * XXX: A lot of code uses lowercase characters, but control-character
 * conversion is actually only valid when applied to uppercase
 * characters. We just treat lowercase characters as if they were
 * inserted as uppercase.
 */
#define	CTRL(x) ((x) >= 'a' && (x) <= 'z' ? \
	((x) - 'a' + 1) : (((x) - 'A' + 1) & 0x7f))
#define	CEOF		CTRL('D')
#define	CEOL		0xff		/* XXX avoid _POSIX_VDISABLE */
#define	CERASE		CTRL('?')
#define	CERASE2		CTRL('H')
#define	CINTR		CTRL('C')
#define	CSTATUS		CTRL('T')
#define	CKILL		CTRL('U')
#define	CMIN		1
#define	CQUIT		CTRL('\\')
#define	CSUSP		CTRL('Z')
#define	CTIME		0
#define	CDSUSP		CTRL('Y')
#define	CSTART		CTRL('Q')
#define	CSTOP		CTRL('S')
#define	CLNEXT		CTRL('V')
#define	CDISCARD	CTRL('O')
#define	CWERASE		CTRL('W')
#define	CREPRINT	CTRL('R')
#define	CEOT		CEOF
/* compat */
#define	CBRK		CEOL
#define	CRPRNT		CREPRINT
#define	CFLUSH		CDISCARD

/* PROTECTED INCLUSION ENDS HERE */
#endif /* !_SYS_TTYDEFAULTS_H_ */

/*
 * #define TTYDEFCHARS to include an array of default control characters.
 */
#ifdef TTYDEFCHARS

#include <sys/cdefs.h>
#include <sys/_termios.h>

static const cc_t ttydefchars[] = {
	CEOF, CEOL, CEOL, CERASE, CWERASE, CKILL, CREPRINT, CERASE2, CINTR,
	CQUIT, CSUSP, CDSUSP, CSTART, CSTOP, CLNEXT, CDISCARD, CMIN, CTIME,
	CSTATUS, _POSIX_VDISABLE
};
_Static_assert(sizeof(ttydefchars) / sizeof(cc_t) == NCCS,
    "Size of ttydefchars does not match NCCS");

#undef TTYDEFCHARS
#endif /* TTYDEFCHARS */
