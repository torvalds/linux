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
#ifdef __mips_soft_float
#define	_FPUSW_SHIFT	16
#define	FE_INVALID	0x0001
#define	FE_DIVBYZERO	0x0002
#define	FE_OVERFLOW	0x0004
#define	FE_UNDERFLOW	0x0008
#define	FE_INEXACT	0x0010
#else
#define	_FCSR_CAUSE_SHIFT	10
#define	FE_INVALID	0x0040
#define	FE_DIVBYZERO	0x0020
#define	FE_OVERFLOW	0x0010
#define	FE_UNDERFLOW	0x0008
#define	FE_INEXACT	0x0004
#endif
#define	FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | \
			 FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

/* Rounding modes */
#define	FE_TONEAREST	0x0000
#define	FE_TOWARDZERO	0x0001
#define	FE_UPWARD	0x0002
#define	FE_DOWNWARD	0x0003
#define	_ROUND_MASK	(FE_TONEAREST | FE_DOWNWARD | \
			 FE_UPWARD | FE_TOWARDZERO)
__BEGIN_DECLS

/* Default floating-point environment */
extern const fenv_t	__fe_dfl_env;
#define	FE_DFL_ENV	(&__fe_dfl_env)

/* We need to be able to map status flag positions to mask flag positions */
#define	_ENABLE_SHIFT	5
#define	_ENABLE_MASK	(FE_ALL_EXCEPT << _ENABLE_SHIFT)

#if !defined(__mips_soft_float) && !defined(__mips_hard_float)
#error compiler didnt set soft/hard float macros
#endif

#ifndef	__mips_soft_float
#define	__cfc1(__fcsr)	__asm __volatile("cfc1 %0, $31" : "=r" (__fcsr))
#define	__ctc1(__fcsr)	__asm __volatile("ctc1 %0, $31" :: "r" (__fcsr))
#endif

#ifdef __mips_soft_float
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
#else
__fenv_static inline int
feclearexcept(int __excepts)
{
	fexcept_t fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__cfc1(fcsr);
	fcsr &= ~(__excepts | (__excepts << _FCSR_CAUSE_SHIFT));
	__ctc1(fcsr);

	return (0);
}

__fenv_static inline int
fegetexceptflag(fexcept_t *__flagp, int __excepts)
{
	fexcept_t fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__cfc1(fcsr);
	*__flagp = fcsr & __excepts;

	return (0);
}

__fenv_static inline int
fesetexceptflag(const fexcept_t *__flagp, int __excepts)
{
	fexcept_t fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__cfc1(fcsr);
	fcsr &= ~__excepts;
	fcsr |= *__flagp & __excepts;
	__ctc1(fcsr);

	return (0);
}

__fenv_static inline int
feraiseexcept(int __excepts)
{
	fexcept_t fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__cfc1(fcsr);
	fcsr |= __excepts | (__excepts << _FCSR_CAUSE_SHIFT);
	__ctc1(fcsr);

	return (0);
}

__fenv_static inline int
fetestexcept(int __excepts)
{
	fexcept_t fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__cfc1(fcsr);

	return (fcsr & __excepts);
}

__fenv_static inline int
fegetround(void)
{
	fexcept_t fcsr;

	__cfc1(fcsr);

	return (fcsr & _ROUND_MASK);
}

__fenv_static inline int
fesetround(int __round)
{
	fexcept_t fcsr;

	if (__round & ~_ROUND_MASK)
		return (-1);

	__cfc1(fcsr);
	fcsr &= ~_ROUND_MASK;
	fcsr |= __round;
	__ctc1(fcsr);

	return (0);
}

__fenv_static inline int
fegetenv(fenv_t *__envp)
{

	__cfc1(*__envp);

	return (0);
}

__fenv_static inline int
feholdexcept(fenv_t *__envp)
{
	fexcept_t fcsr;

	__cfc1(fcsr);
	*__envp = fcsr;
	fcsr &= ~(FE_ALL_EXCEPT | _ENABLE_MASK);
	__ctc1(fcsr);

	return (0);
}

__fenv_static inline int
fesetenv(const fenv_t *__envp)
{

	__ctc1(*__envp);

	return (0);
}

__fenv_static inline int
feupdateenv(const fenv_t *__envp)
{
	fexcept_t fcsr;

	__cfc1(fcsr);
	fesetenv(__envp);
	feraiseexcept(fcsr);

	return (0);
}
#endif /* !__mips_soft_float */

#if __BSD_VISIBLE

/* We currently provide no external definitions of the functions below. */

#ifdef __mips_soft_float
int feenableexcept(int __mask);
int fedisableexcept(int __mask);
int fegetexcept(void);
#else
static inline int
feenableexcept(int __mask)
{
	fenv_t __old_fcsr, __new_fcsr;

	__cfc1(__old_fcsr);
	__new_fcsr = __old_fcsr | (__mask & FE_ALL_EXCEPT) << _ENABLE_SHIFT;
	__ctc1(__new_fcsr);

	return ((__old_fcsr >> _ENABLE_SHIFT) & FE_ALL_EXCEPT);
}

static inline int
fedisableexcept(int __mask)
{
	fenv_t __old_fcsr, __new_fcsr;

	__cfc1(__old_fcsr);
	__new_fcsr = __old_fcsr & ~((__mask & FE_ALL_EXCEPT) << _ENABLE_SHIFT);
	__ctc1(__new_fcsr);

	return ((__old_fcsr >> _ENABLE_SHIFT) & FE_ALL_EXCEPT);
}

static inline int
fegetexcept(void)
{
	fexcept_t fcsr;

	__cfc1(fcsr);

	return ((fcsr & _ENABLE_MASK) >> _ENABLE_SHIFT);
}

#endif /* !__mips_soft_float */

#endif /* __BSD_VISIBLE */

__END_DECLS

#endif	/* !_FENV_H_ */
