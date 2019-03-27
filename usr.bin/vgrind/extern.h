/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
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
 *      @(#)extern.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

extern bool     _escaped;             /* if last character was an escape */
extern char    *s_start;               /* start of the current string */
extern char    *l_acmbeg;              /* string introducing a comment */
extern char    *l_acmend;              /* string ending a comment */
extern char    *l_blkbeg;              /* string beginning of a block */
extern char    *l_blkend;              /* string ending a block */
extern char    *l_chrbeg;              /* delimiter for character constant */
extern char    *l_chrend;              /* delimiter for character constant */
extern char    *l_combeg;              /* string introducing a comment */
extern char    *l_comend;              /* string ending a comment */
extern char     l_escape;              /* character used to escape characters */
extern char    *l_keywds[];    	       /* keyword table address */
extern bool     l_onecase;             /* upper and lower case are equivalent */
extern char    *l_prcbeg;              /* regular expr for procedure begin */
extern char    *l_strbeg;              /* delimiter for string constant */
extern char    *l_strend;              /* delimiter for string constant */
extern bool     l_toplex;              /* procedures only defined at top lex level */
extern const char *language;           /* the language indicator */

#include <sys/cdefs.h>

__BEGIN_DECLS
extern int      STRNCMP(char *, char *, int);
extern char    *convexp(char *);
extern char    *expmatch(char *, char *, char *);
__END_DECLS

