/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)err.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _ERR_H_
#define	_ERR_H_

/*
 * Don't use va_list in the err/warn prototypes.   Va_list is typedef'd in two
 * places (<machine/varargs.h> and <machine/stdarg.h>), so if we include one
 * of them here we may collide with the utility's includes.  It's unreasonable
 * for utilities to have to include one of them to include err.h, so we get
 * __va_list from <sys/_types.h> and use it.
 */
#include <sys/cdefs.h>
#include <sys/_types.h>

__NULLABILITY_PRAGMA_PUSH

__BEGIN_DECLS
void	err(int, const char *, ...) __dead2 __printf0like(2, 3);
void	verr(int, const char *, __va_list) __dead2 __printf0like(2, 0);
void	errc(int, int, const char *, ...) __dead2 __printf0like(3, 4);
void	verrc(int, int, const char *, __va_list) __dead2
	    __printf0like(3, 0);
void	errx(int, const char *, ...) __dead2 __printf0like(2, 3);
void	verrx(int, const char *, __va_list) __dead2 __printf0like(2, 0);
void	warn(const char *, ...) __printf0like(1, 2);
void	vwarn(const char *, __va_list) __printf0like(1, 0);
void	warnc(int, const char *, ...) __printf0like(2, 3);
void	vwarnc(int, const char *, __va_list) __printf0like(2, 0);
void	warnx(const char *, ...) __printflike(1, 2);
void	vwarnx(const char *, __va_list) __printf0like(1, 0);
void	err_set_file(void *);
void	err_set_exit(void (* _Nullable)(int));
__END_DECLS
__NULLABILITY_PRAGMA_POP

#endif /* !_ERR_H_ */
