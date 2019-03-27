/*-
 * Copyright (c) 2013 Andrew Turner <andrew@FreeBSD.ORG>
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

#ifdef _FENV_MANGLE_H_
#error Only include fenv-mangle.h once
#endif

#define	_FENV_MANGLE_H_

#ifndef FENV_MANGLE
#error FENV_MANGLE is undefined
#endif

#define	feclearexcept	FENV_MANGLE(feclearexcept)
#define	fegetexceptflag	FENV_MANGLE(fegetexceptflag)
#define	fesetexceptflag	FENV_MANGLE(fesetexceptflag)
#define	feraiseexcept	FENV_MANGLE(feraiseexcept)
#define	fetestexcept	FENV_MANGLE(fetestexcept)
#define	fegetround	FENV_MANGLE(fegetround)
#define	fesetround	FENV_MANGLE(fesetround)
#define	fegetenv	FENV_MANGLE(fegetenv)
#define	feholdexcept	FENV_MANGLE(feholdexcept)
#define	fesetenv	FENV_MANGLE(fesetenv)
#define	feupdateenv	FENV_MANGLE(feupdateenv)
#define	feenableexcept	FENV_MANGLE(feenableexcept)
#define	fedisableexcept	FENV_MANGLE(fedisableexcept)
#define	fegetexcept	FENV_MANGLE(fegetexcept)

