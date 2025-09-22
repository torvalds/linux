/*	$OpenBSD: sys.h,v 1.18 2016/05/10 11:53:54 schwarze Exp $	*/
/*	$NetBSD: sys.h,v 1.25 2016/04/11 18:56:31 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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
 *	@(#)sys.h	8.1 (Berkeley) 6/4/93
 */

/*
 * sys.h: Put all the stupid compiler and system dependencies here...
 */
#ifndef _h_sys
#define	_h_sys

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

#if !defined(__attribute__) && defined(__cplusplus)
# define __attribute__(A)
#endif

#ifndef __BEGIN_DECLS
# ifdef  __cplusplus
#  define __BEGIN_DECLS  extern "C" {
#  define __END_DECLS    }
# else
#  define __BEGIN_DECLS
#  define __END_DECLS
# endif
#endif

#define protected __dso_hidden

#ifndef __arraycount
# define __arraycount(a) (sizeof(a) / sizeof(*(a)))
#endif

#include <stdio.h>

#ifndef HAVE_STRLCAT
#define	strlcat libedit_strlcat
size_t	strlcat(char *dst, const char *src, size_t size);
#endif

#ifndef HAVE_STRLCPY
#define	strlcpy libedit_strlcpy
size_t	strlcpy(char *dst, const char *src, size_t size);
#endif

#ifndef HAVE_GETLINE
#define	getline libedit_getline
ssize_t	getline(char **line, size_t *len, FILE *fp);
#endif

#define	REGEX		/* Use POSIX.2 regular expression functions */
#undef	REGEXP		/* Use UNIX V8 regular expression functions */

#if defined(__sun)
extern int tgetent(char *, const char *);
extern int tgetflag(char *);
extern int tgetnum(char *);
extern int tputs(const char *, int, int (*)(int));
extern char* tgoto(const char*, int, int);
extern char* tgetstr(char*, char**);
#endif

#endif /* _h_sys */
