/*	$OpenBSD: fenv.h,v 1.4 2011/05/25 21:46:49 martynas Exp $	*/
/*	$NetBSD: fenv.h,v 1.1.2.2 2011/02/08 16:19:41 bouyer Exp $	*/

/*-
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

#ifndef	_SPARC64_FENV_H_
#define	_SPARC64_FENV_H_

/*
 * Each symbol representing a floating point exception expands to an integer
 * constant expression with values, such that bitwise-inclusive ORs of _all
 * combinations_ of the constants result in distinct values.
 *
 * We use such values that allow direct bitwise operations on FPU registers.
 */
#define	FE_INEXACT		0x020
#define	FE_DIVBYZERO		0x040
#define	FE_UNDERFLOW		0x080
#define	FE_OVERFLOW		0x100
#define	FE_INVALID		0x200

/*
 * The following symbol is simply the bitwise-inclusive OR of all floating-point
 * exception constants defined above.
 */
#define	FE_ALL_EXCEPT		(FE_INEXACT | FE_DIVBYZERO | FE_UNDERFLOW | \
				 FE_OVERFLOW | FE_INVALID)
#define	_MASK_SHIFT		18

/*
 * Each symbol representing the rounding direction, expands to an integer
 * constant expression whose value is distinct non-negative value.
 *
 * We use such values that allow direct bitwise operations on FPU registers.
 */
#define	FE_TONEAREST		0x0
#define	FE_TOWARDZERO		0x1
#define	FE_UPWARD		0x2
#define	FE_DOWNWARD		0x3

/*
 * The following symbol is simply the bitwise-inclusive OR of all floating-point
 * rounding direction constants defined above.
 */
#define	_ROUND_MASK		(FE_TONEAREST | FE_TOWARDZERO | FE_UPWARD | \
				 FE_DOWNWARD)
#define	_ROUND_SHIFT		30

/*
 * fenv_t represents the entire floating-point environment.
 */
typedef	unsigned long		fenv_t;

/*
 * The following constant represents the default floating-point environment
 * (that is, the one installed at program startup) and has type pointer to
 * const-qualified fenv_t.
 *
 * It can be used as an argument to the functions within the <fenv.h> header
 * that manage the floating-point environment, namely fesetenv() and
 * feupdateenv().
 */
__BEGIN_DECLS
extern	fenv_t			__fe_dfl_env;
__END_DECLS
#define	FE_DFL_ENV		((const fenv_t *)&__fe_dfl_env)

/*
 * fexcept_t represents the floating-point status flags collectively, including
 * any status the implementation associates with the flags.
 *
 * A floating-point status flag is a system variable whose value is set (but
 * never cleared) when a floating-point exception is raised, which occurs as a
 * side effect of exceptional floating-point arithmetic to provide auxiliary
 * information.
 *
 * A floating-point control mode is a system variable whose value may be set by
 * the user to affect the subsequent behavior of floating-point arithmetic.
 */
typedef	unsigned long		fexcept_t;

#endif	/* !_SPARC64_FENV_H_ */
