/*-
 * Copyright (c) 2004-2005 David Schultz <das@FreeBSD.ORG>
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
#define	FE_INVALID	0x0010
#define	FE_DIVBYZERO	0x0008
#define	FE_OVERFLOW	0x0004
#define	FE_UNDERFLOW	0x0002
#define	FE_INEXACT	0x0001
#define	FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | \
			 FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

/*
 * RISC-V Rounding modes
 */
#define	_ROUND_SHIFT	5
#define	FE_TONEAREST	(0x00 << _ROUND_SHIFT)
#define	FE_TOWARDZERO	(0x01 << _ROUND_SHIFT)
#define	FE_DOWNWARD	(0x02 << _ROUND_SHIFT)
#define	FE_UPWARD	(0x03 << _ROUND_SHIFT)
#define	_ROUND_MASK	(FE_TONEAREST | FE_DOWNWARD | \
			 FE_UPWARD | FE_TOWARDZERO)

__BEGIN_DECLS

/* Default floating-point environment */
extern const fenv_t	__fe_dfl_env;
#define	FE_DFL_ENV	(&__fe_dfl_env)

#if !defined(__riscv_float_abi_soft) && !defined(__riscv_float_abi_double)
#if defined(__riscv_float_abi_single)
#error single precision floating point ABI not supported
#else
#error compiler did not set soft/hard float macros
#endif
#endif

#ifndef __riscv_float_abi_soft
#define	__rfs(__fcsr)	__asm __volatile("csrr %0, fcsr" : "=r" (__fcsr))
#define	__wfs(__fcsr)	__asm __volatile("csrw fcsr, %0" :: "r" (__fcsr))
#endif

#ifdef __riscv_float_abi_soft
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

	__asm __volatile("csrc fflags, %0" :: "r"(__excepts));

	return (0);
}

__fenv_static inline int
fegetexceptflag(fexcept_t *__flagp, int __excepts)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	*__flagp = __fcsr & __excepts;

	return (0);
}

__fenv_static inline int
fesetexceptflag(const fexcept_t *__flagp, int __excepts)
{
	fexcept_t __fcsr;

	__fcsr = *__flagp;
	__asm __volatile("csrc fflags, %0" :: "r"(__excepts));
	__asm __volatile("csrs fflags, %0" :: "r"(__fcsr & __excepts));

	return (0);
}

__fenv_static inline int
feraiseexcept(int __excepts)
{

	__asm __volatile("csrs fflags, %0" :: "r"(__excepts));

	return (0);
}

__fenv_static inline int
fetestexcept(int __excepts)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);

	return (__fcsr & __excepts);
}

__fenv_static inline int
fegetround(void)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);

	return (__fcsr & _ROUND_MASK);
}

__fenv_static inline int
fesetround(int __round)
{
	fexcept_t __fcsr;

	if (__round & ~_ROUND_MASK)
		return (-1);

	__rfs(__fcsr);
	__fcsr &= ~_ROUND_MASK;
	__fcsr |= __round;
	__wfs(__fcsr);

	return (0);
}

__fenv_static inline int
fegetenv(fenv_t *__envp)
{

	__rfs(*__envp);

	return (0);
}

__fenv_static inline int
feholdexcept(fenv_t *__envp)
{

	/* No exception traps. */

	return (-1);
}

__fenv_static inline int
fesetenv(const fenv_t *__envp)
{

	__wfs(*__envp);

	return (0);
}

__fenv_static inline int
feupdateenv(const fenv_t *__envp)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	__wfs(*__envp);
	feraiseexcept(__fcsr & FE_ALL_EXCEPT);

	return (0);
}
#endif /* !__riscv_float_abi_soft */

#if __BSD_VISIBLE

/* We currently provide no external definitions of the functions below. */

#ifdef __riscv_float_abi_soft
int feenableexcept(int __mask);
int fedisableexcept(int __mask);
int fegetexcept(void);
#else
static inline int
feenableexcept(int __mask)
{

	/* No exception traps. */

	return (-1);
}

static inline int
fedisableexcept(int __mask)
{

	/* No exception traps. */

	return (0);
}

static inline int
fegetexcept(void)
{

	/* No exception traps. */

	return (0);
}
#endif /* !__riscv_float_abi_soft */

#endif /* __BSD_VISIBLE */

__END_DECLS

#endif	/* !_FENV_H_ */
