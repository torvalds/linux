/*	$OpenBSD: fenv.h,v 1.2 2011/05/25 21:46:49 martynas Exp $	*/
/*	$NetBSD: fenv.h,v 1.2.4.1 2011/02/08 16:18:55 bouyer Exp $	*/

/*
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_FENV_H_
#define	_FENV_H_

#include <sys/cdefs.h>
#include <machine/fenv.h>

__BEGIN_DECLS

int	feclearexcept(int);
int	fegetexceptflag(fexcept_t *, int);
int	feraiseexcept(int);
int	fesetexceptflag(const fexcept_t *, int);
int	fetestexcept(int);

int	fegetround(void);
int	fesetround(int);

int	fegetenv(fenv_t *);
int	feholdexcept(fenv_t *);
int	fesetenv(const fenv_t *);
int	feupdateenv(const fenv_t *);

#if	__BSD_VISIBLE
int	feenableexcept(int);
int	fedisableexcept(int);
int	fegetexcept(void);
#endif	/* __BSD_VISIBLE */

__END_DECLS

#endif	/* ! _FENV_H_ */
