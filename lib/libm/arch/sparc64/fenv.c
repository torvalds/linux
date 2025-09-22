/*	$OpenBSD: fenv.c,v 1.7 2022/12/27 17:10:07 jmc Exp $	*/
/*	$NetBSD: fenv.c,v 1.1 2011/01/31 00:19:33 christos Exp $	*/

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
 */

#include <fenv.h>

/*
 * The following constant represents the default floating-point environment
 * (that is, the one installed at program startup) and has type pointer to
 * const-qualified fenv_t.
 *
 * It can be used as an argument to the functions within the <fenv.h> header
 * that manage the floating-point environment, namely fesetenv() and
 * feupdateenv().
 */
fenv_t __fe_dfl_env = 0;

/*
 * The feclearexcept() function clears the supported floating-point exceptions
 * represented by `excepts'.
 */
int
feclearexcept(int excepts)
{
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	r &= ~excepts;

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (r));

	return 0;
}
DEF_STD(feclearexcept);

/*
 * The fegetexceptflag() function stores an implementation-defined
 * representation of the states of the floating-point status flags indicated
 * by the argument excepts in the object pointed to by the argument flagp.
 */
int
fegetexceptflag(fexcept_t *flagp, int excepts)
{
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	*flagp = r & excepts;

	return 0;
}


/*
 * This function sets the floating-point status flags indicated by the argument
 * `excepts' to the states stored in the object pointed to by `flagp'. It does
 * NOT raise any floating-point exceptions, but only sets the state of the flags.
 */
int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	r &= ~excepts;
	r |= *flagp & excepts;

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (r));

	return 0;
}
DEF_STD(fesetexceptflag);

/*
 * The feraiseexcept() function raises the supported floating-point exceptions
 * represented by the argument `excepts'.
 *
 * The order in which these floating-point exceptions are raised is unspecified
 * (by the standard).
 */
int
feraiseexcept(int excepts)
{
	volatile double d;

	excepts &= FE_ALL_EXCEPT;

	/*
	 * With a compiler that supports the FENV_ACCESS pragma properly, simple
	 * expressions like '0.0 / 0.0' should be sufficient to generate traps.
	 * Unfortunately, we need to bring a volatile variable into the equation
	 * to prevent incorrect optimizations.
	 */
	if (excepts & FE_INVALID) {
		d = 0.0;
		d = 0.0 / d;
	}
	if (excepts & FE_DIVBYZERO) {
		d = 0.0;
		d = 1.0 / d;
	}
	if (excepts & FE_OVERFLOW) {
		d = 0x1.ffp1023;
		d *= 2.0;
	}
	if (excepts & FE_UNDERFLOW) {
		d = 0x1p-1022;
		d /= 0x1p1023;
	}
	if (excepts & FE_INEXACT) {
		d = 0x1p-1022;
		d += 1.0;
	}

	return 0;
}
DEF_STD(feraiseexcept);

/*
 * The fetestexcept() function determines which of a specified subset of the
 * floating-point exception flags are currently set. The `excepts' argument
 * specifies the floating-point status flags to be queried.
 */
int
fetestexcept(int excepts)
{
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	return r & excepts;
}
DEF_STD(fetestexcept);

/*
 * The fegetround() function gets the current rounding direction.
 */
int
fegetround(void)
{
	fenv_t r;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	return (r >> _ROUND_SHIFT) & _ROUND_MASK;
}
DEF_STD(fegetround);

/*
 * The fesetround() function establishes the rounding direction represented by
 * its argument `round'. If the argument is not equal to the value of a rounding
 * direction macro, the rounding direction is not changed.
 */
int
fesetround(int round)
{
	fenv_t r;

	if (round & ~_ROUND_MASK)
		return -1;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	r &= ~(_ROUND_MASK << _ROUND_SHIFT);
	r |= round << _ROUND_SHIFT;

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (r));

	return 0;
}
DEF_STD(fesetround);

/*
 * The fegetenv() function attempts to store the current floating-point
 * environment in the object pointed to by envp.
 */
int
fegetenv(fenv_t *envp)
{
	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (*envp));

	return 0;
}
DEF_STD(fegetenv);


/*
 * The feholdexcept() function saves the current floating-point environment
 * in the object pointed to by envp, clears the floating-point status flags, and
 * then installs a non-stop (continue on floating-point exceptions) mode, if
 * available, for all floating-point exceptions.
 */
int
feholdexcept(fenv_t *envp)
{
	fenv_t r;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	*envp = r;
	r &= ~(FE_ALL_EXCEPT | (FE_ALL_EXCEPT << _MASK_SHIFT));

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (r));

	return 0;
}
DEF_STD(feholdexcept);

/*
 * The fesetenv() function attempts to establish the floating-point environment
 * represented by the object pointed to by envp. The argument `envp' points
 * to an object set by a call to fegetenv() or feholdexcept(), or equal a
 * floating-point environment macro. The fesetenv() function does not raise
 * floating-point exceptions, but only installs the state of the floating-point
 * status flags represented through its argument.
 */
int
fesetenv(const fenv_t *envp)
{
	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (*envp));

	return 0;
}
DEF_STD(fesetenv);


/*
 * The feupdateenv() function saves the currently raised floating-point
 * exceptions in its automatic storage, installs the floating-point environment
 * represented by the object pointed to by `envp', and then raises the saved
 * floating-point exceptions. The argument `envp' shall point to an object set
 * by a call to feholdexcept() or fegetenv(), or equal a floating-point
 * environment macro.
 */
int
feupdateenv(const fenv_t *envp)
{
	fexcept_t r;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (*envp));

	feraiseexcept(r & FE_ALL_EXCEPT);

	return 0;
}
DEF_STD(feupdateenv);

/*
 * The following functions are extensions to the standard
 */
int
feenableexcept(int mask)
{
	fenv_t old_r, new_r;

	mask &= FE_ALL_EXCEPT;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (old_r));

	new_r = old_r | (mask << _MASK_SHIFT);

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (new_r));

	return (old_r >> _MASK_SHIFT) & FE_ALL_EXCEPT;
}

int
fedisableexcept(int mask)
{
	fenv_t old_r, new_r;

	mask &= FE_ALL_EXCEPT;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (old_r));

	new_r = old_r & ~(mask << _MASK_SHIFT);

	/* Load floating-point state register */
	__asm__ volatile ("ldx %0, %%fsr" : : "m" (new_r));

	return (old_r >> _MASK_SHIFT) & FE_ALL_EXCEPT;
}

int
fegetexcept(void)
{
	fenv_t r;

	/* Save floating-point state register */
	__asm__ volatile ("stx %%fsr, %0" : "=m" (r));

	return (r & (FE_ALL_EXCEPT << _MASK_SHIFT)) >> _MASK_SHIFT;
}
