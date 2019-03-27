/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 David Schultz <das@FreeBSD.ORG>
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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <machine/fpu.h>

#define	__fenv_static
#include "fenv.h"

#ifdef __GNUC_GNU_INLINE__
#error "This file must be compiled with C99 'inline' semantics"
#endif

const fenv_t __fe_dfl_env = {
	{ 0xffff0000 | __INITIAL_FPUCW__,
	  0xffff0000,
	  0xffffffff,
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff }
	},
	__INITIAL_MXCSR__
};

extern inline int feclearexcept(int __excepts);
extern inline int fegetexceptflag(fexcept_t *__flagp, int __excepts);

int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	fenv_t env;

	__fnstenv(&env.__x87);
	env.__x87.__status &= ~excepts;
	env.__x87.__status |= *flagp & excepts;
	__fldenv(&env.__x87);

	__stmxcsr(&env.__mxcsr);
	env.__mxcsr &= ~excepts;
	env.__mxcsr |= *flagp & excepts;
	__ldmxcsr(&env.__mxcsr);

	return (0);
}

int
feraiseexcept(int excepts)
{
	fexcept_t ex = excepts;

	fesetexceptflag(&ex, excepts);
	__fwait();
	return (0);
}

extern inline int fetestexcept(int __excepts);
extern inline int fegetround(void);
extern inline int fesetround(int __round);

int
fegetenv(fenv_t *envp)
{

	__fnstenv(&envp->__x87);
	__stmxcsr(&envp->__mxcsr);
	/*
	 * fnstenv masks all exceptions, so we need to restore the
	 * control word to avoid this side effect.
	 */
	__fldcw(&envp->__x87.__control);
	return (0);
}

int
feholdexcept(fenv_t *envp)
{
	__uint32_t mxcsr;

	__stmxcsr(&mxcsr);
	__fnstenv(&envp->__x87);
	__fnclex();
	envp->__mxcsr = mxcsr;
	mxcsr &= ~FE_ALL_EXCEPT;
	mxcsr |= FE_ALL_EXCEPT << _SSE_EMASK_SHIFT;
	__ldmxcsr(&mxcsr);
	return (0);
}

extern inline int fesetenv(const fenv_t *__envp);

int
feupdateenv(const fenv_t *envp)
{
	__uint32_t mxcsr;
	__uint16_t status;

	__fnstsw(&status);
	__stmxcsr(&mxcsr);
	fesetenv(envp);
	feraiseexcept((mxcsr | status) & FE_ALL_EXCEPT);
	return (0);
}

int
__feenableexcept(int mask)
{
	__uint32_t mxcsr, omask;
	__uint16_t control;

	mask &= FE_ALL_EXCEPT;
	__fnstcw(&control);
	__stmxcsr(&mxcsr);
	omask = ~(control | mxcsr >> _SSE_EMASK_SHIFT) & FE_ALL_EXCEPT;
	control &= ~mask;
	__fldcw(&control);
	mxcsr &= ~(mask << _SSE_EMASK_SHIFT);
	__ldmxcsr(&mxcsr);
	return (omask);
}

int
__fedisableexcept(int mask)
{
	__uint32_t mxcsr, omask;
	__uint16_t control;

	mask &= FE_ALL_EXCEPT;
	__fnstcw(&control);
	__stmxcsr(&mxcsr);
	omask = ~(control | mxcsr >> _SSE_EMASK_SHIFT) & FE_ALL_EXCEPT;
	control |= mask;
	__fldcw(&control);
	mxcsr |= mask << _SSE_EMASK_SHIFT;
	__ldmxcsr(&mxcsr);
	return (omask);
}

__weak_reference(__feenableexcept, feenableexcept);
__weak_reference(__fedisableexcept, fedisableexcept);
