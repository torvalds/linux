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

#ifndef	_FENV_H_
#define	_FENV_H_

#include <sys/_types.h>

#ifndef	__fenv_static
#define	__fenv_static	static
#endif

typedef	__uint32_t	fenv_t;
typedef	__uint32_t	fexcept_t;

/* Exception flags */
#define	FE_INVALID	0x0001
#define	FE_DIVBYZERO	0x0002
#define	FE_OVERFLOW	0x0004
#define	FE_UNDERFLOW	0x0008
#define	FE_INEXACT	0x0010
#ifdef __ARM_PCS_VFP
#define	FE_DENORMAL	0x0080
#define	FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | \
			 FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW | FE_DENORMAL)
#else
#define	FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | \
			 FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)
#endif

/* Rounding modes */
#define	VFP_FE_TONEAREST	0x00000000
#define	VFP_FE_UPWARD		0x00400000
#define	VFP_FE_DOWNWARD		0x00800000
#define	VFP_FE_TOWARDZERO	0x00c00000

#ifdef __ARM_PCS_VFP
#define	FE_TONEAREST	VFP_FE_TONEAREST
#define	FE_UPWARD	VFP_FE_UPWARD
#define	FE_DOWNWARD	VFP_FE_DOWNWARD
#define	FE_TOWARDZERO	VFP_FE_TOWARDZERO
#else
#define	FE_TONEAREST	0x0000
#define	FE_TOWARDZERO	0x0001
#define	FE_UPWARD	0x0002
#define	FE_DOWNWARD	0x0003
#endif
#define	_ROUND_MASK	(FE_TONEAREST | FE_DOWNWARD | \
			 FE_UPWARD | FE_TOWARDZERO)
__BEGIN_DECLS

/* Default floating-point environment */
extern const fenv_t	__fe_dfl_env;
#define	FE_DFL_ENV	(&__fe_dfl_env)

/* We need to be able to map status flag positions to mask flag positions */
#ifndef __ARM_PCS_VFP
#define	_FPUSW_SHIFT	16
#define	_ENABLE_MASK	(FE_ALL_EXCEPT << _FPUSW_SHIFT)
#endif

#ifndef __ARM_PCS_VFP

int feclearexcept(int __excepts);
int fegetexceptflag(fexcept_t *__flagp, int __excepts);
int fesetexceptflag(const fexcept_t *__flagp, int __excepts);
int feraiseexcept(int __excepts);
int fetestexcept(int __excepts);
int fegetround(void);
int fesetround(int __round);
int fegetenv(fenv_t *__envp);
int feholdexcept(fenv_t *__envp);
int fesetenv(const fenv_t *__envp);
int feupdateenv(const fenv_t *__envp);
#if __BSD_VISIBLE
int feenableexcept(int __mask);
int fedisableexcept(int __mask);
int fegetexcept(void);
#endif

#else	/* __ARM_PCS_VFP */

#define	vmrs_fpscr(__r)	__asm __volatile("vmrs %0, fpscr" : "=&r"(__r))
#define	vmsr_fpscr(__r)	__asm __volatile("vmsr fpscr, %0" : : "r"(__r))

#define _FPU_MASK_SHIFT	8

__fenv_static inline int
feclearexcept(int __excepts)
{
	fexcept_t __fpsr;

	vmrs_fpscr(__fpsr);
	__fpsr &= ~__excepts;
	vmsr_fpscr(__fpsr);
	return (0);
}

__fenv_static inline int
fegetexceptflag(fexcept_t *__flagp, int __excepts)
{
	fexcept_t __fpsr;

	vmrs_fpscr(__fpsr);
	*__flagp = __fpsr & __excepts;
	return (0);
}

__fenv_static inline int
fesetexceptflag(const fexcept_t *__flagp, int __excepts)
{
	fexcept_t __fpsr;

	vmrs_fpscr(__fpsr);
	__fpsr &= ~__excepts;
	__fpsr |= *__flagp & __excepts;
	vmsr_fpscr(__fpsr);
	return (0);
}

__fenv_static inline int
feraiseexcept(int __excepts)
{
	fexcept_t __ex = __excepts;

	fesetexceptflag(&__ex, __excepts);	/* XXX */
	return (0);
}

__fenv_static inline int
fetestexcept(int __excepts)
{
	fexcept_t __fpsr;

	vmrs_fpscr(__fpsr);
	return (__fpsr & __excepts);
}

__fenv_static inline int
fegetround(void)
{
	fenv_t __fpsr;

	vmrs_fpscr(__fpsr);
	return (__fpsr & _ROUND_MASK);
}

__fenv_static inline int
fesetround(int __round)
{
	fenv_t __fpsr;

	vmrs_fpscr(__fpsr);
	__fpsr &= ~(_ROUND_MASK);
	__fpsr |= __round;
	vmsr_fpscr(__fpsr);
	return (0);
}

__fenv_static inline int
fegetenv(fenv_t *__envp)
{

	vmrs_fpscr(*__envp);
	return (0);
}

__fenv_static inline int
feholdexcept(fenv_t *__envp)
{
	fenv_t __env;

	vmrs_fpscr(__env);
	*__envp = __env;
	__env &= ~(FE_ALL_EXCEPT);
	vmsr_fpscr(__env);
	return (0);
}

__fenv_static inline int
fesetenv(const fenv_t *__envp)
{

	vmsr_fpscr(*__envp);
	return (0);
}

__fenv_static inline int
feupdateenv(const fenv_t *__envp)
{
	fexcept_t __fpsr;

	vmrs_fpscr(__fpsr);
	vmsr_fpscr(*__envp);
	feraiseexcept(__fpsr & FE_ALL_EXCEPT);
	return (0);
}

#if __BSD_VISIBLE

/* We currently provide no external definitions of the functions below. */

__fenv_static inline int
feenableexcept(int __mask)
{
	fenv_t __old_fpsr, __new_fpsr;

	vmrs_fpscr(__old_fpsr);
	__new_fpsr = __old_fpsr |
	    ((__mask & FE_ALL_EXCEPT) << _FPU_MASK_SHIFT);
	vmsr_fpscr(__new_fpsr);
	return ((__old_fpsr >> _FPU_MASK_SHIFT) & FE_ALL_EXCEPT);
}

__fenv_static inline int
fedisableexcept(int __mask)
{
	fenv_t __old_fpsr, __new_fpsr;

	vmrs_fpscr(__old_fpsr);
	__new_fpsr = __old_fpsr &
	    ~((__mask & FE_ALL_EXCEPT) << _FPU_MASK_SHIFT);
	vmsr_fpscr(__new_fpsr);
	return ((__old_fpsr >> _FPU_MASK_SHIFT) & FE_ALL_EXCEPT);
}

__fenv_static inline int
fegetexcept(void)
{
	fenv_t __fpsr;

	vmrs_fpscr(__fpsr);
	return (__fpsr & FE_ALL_EXCEPT);
}

#endif /* __BSD_VISIBLE */

#endif	/* __ARM_PCS_VFP */

__END_DECLS

#endif	/* !_FENV_H_ */
