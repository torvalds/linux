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

typedef	__uint64_t	fenv_t;
typedef	__uint64_t	fexcept_t;

/* Exception flags */
#define	FE_INVALID	0x00000200
#define	FE_DIVBYZERO	0x00000040
#define	FE_OVERFLOW	0x00000100
#define	FE_UNDERFLOW	0x00000080
#define	FE_INEXACT	0x00000020
#define	FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | \
			 FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

/*
 * Rounding modes
 *
 * We can't just use the hardware bit values here, because that would
 * make FE_UPWARD and FE_DOWNWARD negative, which is not allowed.
 */
#define	FE_TONEAREST	0x0
#define	FE_TOWARDZERO	0x1
#define	FE_UPWARD	0x2
#define	FE_DOWNWARD	0x3
#define	_ROUND_MASK	(FE_TONEAREST | FE_DOWNWARD | \
			 FE_UPWARD | FE_TOWARDZERO)
#define	_ROUND_SHIFT	30

__BEGIN_DECLS

/* Default floating-point environment */
extern const fenv_t	__fe_dfl_env;
#define	FE_DFL_ENV	(&__fe_dfl_env)

/* We need to be able to map status flag positions to mask flag positions */
#define _FPUSW_SHIFT	18
#define	_ENABLE_MASK	(FE_ALL_EXCEPT << _FPUSW_SHIFT)

#define	__ldxfsr(__r)	__asm __volatile("ldx %0, %%fsr" : : "m" (__r))
#define	__stxfsr(__r)	__asm __volatile("stx %%fsr, %0" : "=m" (*(__r)))

__fenv_static __inline int
feclearexcept(int __excepts)
{
	fexcept_t __r;

	__stxfsr(&__r);
	__r &= ~__excepts;
	__ldxfsr(__r);
	return (0);
}

__fenv_static inline int
fegetexceptflag(fexcept_t *__flagp, int __excepts)
{
	fexcept_t __r;

	__stxfsr(&__r);
	*__flagp = __r & __excepts;
	return (0);
}

__fenv_static inline int
fesetexceptflag(const fexcept_t *__flagp, int __excepts)
{
	fexcept_t __r;

	__stxfsr(&__r);
	__r &= ~__excepts;
	__r |= *__flagp & __excepts;
	__ldxfsr(__r);
	return (0);
}

/*
 * It seems to be worthwhile to inline this function even when the
 * arguments are not compile-time constants.  Perhaps this depends
 * on the register window.
 */
__fenv_static inline int
feraiseexcept(int __excepts)
{
	volatile double d;

	/*
	 * With a compiler that supports the FENV_ACCESS pragma
	 * properly, simple expressions like '0.0 / 0.0' should
	 * be sufficient to generate traps.  Unfortunately, we
	 * need to bring a volatile variable into the equation
	 * to prevent incorrect optimizations.
	 */
	if (__excepts & FE_INVALID) {
		d = 0.0;
		d = 0.0 / d;
	}
	if (__excepts & FE_DIVBYZERO) {
		d = 0.0;
		d = 1.0 / d;
	}
	if (__excepts & FE_OVERFLOW) {
		d = 0x1.ffp1023;
		d *= 2.0;
	}
	if (__excepts & FE_UNDERFLOW) {
		d = 0x1p-1022;
		d /= 0x1p1023;
	}
	if (__excepts & FE_INEXACT) {
		d = 0x1p-1022;
		d += 1.0;
	}
	return (0);
}

__fenv_static inline int
fetestexcept(int __excepts)
{
	fexcept_t __r;

	__stxfsr(&__r);
	return (__r & __excepts);
}

__fenv_static inline int
fegetround(void)
{
	fenv_t __r;

	__stxfsr(&__r);
	return ((__r >> _ROUND_SHIFT) & _ROUND_MASK);
}

__fenv_static inline int
fesetround(int __round)
{
	fenv_t __r;

	if (__round & ~_ROUND_MASK)
		return (-1);
	__stxfsr(&__r);
	__r &= ~(_ROUND_MASK << _ROUND_SHIFT);
	__r |= __round << _ROUND_SHIFT;
	__ldxfsr(__r);
	return (0);
}

__fenv_static inline int
fegetenv(fenv_t *__envp)
{

	__stxfsr(__envp);
	return (0);
}

__fenv_static inline int
feholdexcept(fenv_t *__envp)
{
	fenv_t __r;

	__stxfsr(&__r);
	*__envp = __r;
	__r &= ~(FE_ALL_EXCEPT | _ENABLE_MASK);
	__ldxfsr(__r);
	return (0);
}

__fenv_static inline int
fesetenv(const fenv_t *__envp)
{

	__ldxfsr(*__envp);
	return (0);
}

__fenv_static inline int
feupdateenv(const fenv_t *__envp)
{
	fexcept_t __r;

	__stxfsr(&__r);
	__ldxfsr(*__envp);
	feraiseexcept(__r & FE_ALL_EXCEPT);
	return (0);
}

#if __BSD_VISIBLE

/* We currently provide no external definitions of the functions below. */

static inline int
feenableexcept(int __mask)
{
	fenv_t __old_r, __new_r;

	__stxfsr(&__old_r);
	__new_r = __old_r | ((__mask & FE_ALL_EXCEPT) << _FPUSW_SHIFT);
	__ldxfsr(__new_r);
	return ((__old_r >> _FPUSW_SHIFT) & FE_ALL_EXCEPT);
}

static inline int
fedisableexcept(int __mask)
{
	fenv_t __old_r, __new_r;

	__stxfsr(&__old_r);
	__new_r = __old_r & ~((__mask & FE_ALL_EXCEPT) << _FPUSW_SHIFT);
	__ldxfsr(__new_r);
	return ((__old_r >> _FPUSW_SHIFT) & FE_ALL_EXCEPT);
}

static inline int
fegetexcept(void)
{
	fenv_t __r;

	__stxfsr(&__r);
	return ((__r & _ENABLE_MASK) >> _FPUSW_SHIFT);
}

#endif /* __BSD_VISIBLE */

__END_DECLS

#endif	/* !_FENV_H_ */
