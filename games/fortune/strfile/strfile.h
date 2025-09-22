/*	$OpenBSD: strfile.h,v 1.4 2003/06/03 03:01:39 millert Exp $	*/
/*	$NetBSD: strfile.h,v 1.3 1995/03/23 08:28:49 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ken Arnold.
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
 *	@(#)strfile.h	8.1 (Berkeley) 5/31/93
 */

#define	STR_ENDSTRING(line,tbl) \
	((line)[0] == (tbl).str_delim && (line)[1] == '\n')

typedef struct {				/* information table */
#define	VERSION		2
	u_int32_t	str_version;		/* version number */
	u_int32_t	str_numstr;		/* # of strings in the file */
	u_int32_t	str_longlen;		/* length of longest string */
	u_int32_t	str_shortlen;		/* length of shortest string */
#define	STR_RANDOM	0x1			/* randomized pointers */
#define	STR_ORDERED	0x2			/* ordered pointers */
#define	STR_ROTATED	0x4			/* rot-13'd text */
	u_int32_t	str_flags;		/* bit field for flags */
	u_int8_t	stuff[4];		/* long aligned space */
#define	str_delim	stuff[0]		/* delimiting character */
} STRFILE;
