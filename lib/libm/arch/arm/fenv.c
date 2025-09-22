/*	$OpenBSD: fenv.c,v 1.6 2022/12/27 17:10:07 jmc Exp $	*/

/*
 * Copyright (c) 2011 Martynas Venckus <martynas@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fenv.h>
#include <machine/ieeefp.h>

extern	fp_except	_softfloat_float_exception_flags;
extern	fp_except	_softfloat_float_exception_mask;
extern	fp_rnd		_softfloat_float_rounding_mode;

/*
 * The following constant represents the default floating-point environment
 * (that is, the one installed at program startup) and has type pointer to
 * const-qualified fenv_t.
 *
 * It can be used as an argument to the functions within the <fenv.h> header
 * that manage the floating-point environment, namely fesetenv() and
 * feupdateenv().
 */
fenv_t __fe_dfl_env = {
	0,
	0,
	0
};

/*
 * The feclearexcept() function clears the supported floating-point exceptions
 * represented by `excepts'.
 */
int
feclearexcept(int excepts)
{
	unsigned int fpscr;

	excepts &= FE_ALL_EXCEPT;

	/* Clear the requested floating-point exceptions */
	_softfloat_float_exception_flags &= ~excepts;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr &= ~excepts;
	__asm volatile("vmsr fpscr, %0" :: "r" (fpscr));

	return 0;
}
DEF_STD(feclearexcept);

/*
 * The fegetexceptflag() function stores an implementation-defined
 * representation of the states of the floating-point status flags indicated by
 * the argument excepts in the object pointed to by the argument flagp.
 */
int
fegetexceptflag(fexcept_t *flagp, int excepts)
{
	unsigned int fpscr;

	excepts &= FE_ALL_EXCEPT;

	/* Store the results in flagp */
	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr |= _softfloat_float_exception_flags;
	*flagp = fpscr & excepts;

	return 0;
}

/*
 * The feraiseexcept() function raises the supported floating-point exceptions
 * represented by the argument `excepts'.
 */
int
feraiseexcept(int excepts)
{
	excepts &= FE_ALL_EXCEPT;

	fesetexceptflag((fexcept_t *)&excepts, excepts);

	return 0;
}
DEF_STD(feraiseexcept);

/*
 * This function sets the floating-point status flags indicated by the argument
 * `excepts' to the states stored in the object pointed to by `flagp'. It does
 * NOT raise any floating-point exceptions, but only sets the state of the flags.
 */
int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	unsigned int fpscr;

	excepts &= FE_ALL_EXCEPT;

	/* Set the requested status flags */
	_softfloat_float_exception_flags &= ~excepts;
	_softfloat_float_exception_flags |= *flagp & excepts;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr &= ~excepts;
	fpscr |= *flagp & excepts;
	__asm volatile("vmsr fpscr, %0" :: "r" (fpscr));

	return 0;
}
DEF_STD(fesetexceptflag);

/*
 * The fetestexcept() function determines which of a specified subset of the
 * floating-point exception flags are currently set. The `excepts' argument
 * specifies the floating-point status flags to be queried.
 */
int
fetestexcept(int excepts)
{
	unsigned int fpscr;

	excepts &= FE_ALL_EXCEPT;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr |= _softfloat_float_exception_flags;

	return fpscr & excepts;
}
DEF_STD(fetestexcept);

/*
 * The fegetround() function gets the current rounding direction.
 */
int
fegetround(void)
{
	unsigned int fpscr;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));

	return (fpscr >> 22) & _ROUND_MASK;
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
	unsigned int fpscr;

	/* Check whether requested rounding direction is supported */
	if (round & ~_ROUND_MASK)
		return -1;

	/* Set the rounding direction */
	_softfloat_float_rounding_mode &= ~_ROUND_MASK;
	_softfloat_float_rounding_mode |= round;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr &= ~(_ROUND_MASK << 22);
	fpscr |= round << 22;
	__asm volatile("vmsr fpscr, %0" :: "r" (fpscr));

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
	unsigned int fpscr;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr |= _softfloat_float_exception_flags;

	/* Store the current floating-point sticky flags */
	envp->__sticky = fpscr & FE_ALL_EXCEPT;

	/* Store the current floating-point masks */
	envp->__mask = 0;

	/* Store the current floating-point control register */
	envp->__round = (fpscr >> 22) & _ROUND_MASK;

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
	unsigned int fpscr;

	/* Store the current floating-point environment */
	fegetenv(envp);

	/* Clear exception flags */
	_softfloat_float_exception_flags &= ~FE_ALL_EXCEPT;

	/* Mask all exceptions */
	_softfloat_float_exception_mask &= ~FE_ALL_EXCEPT;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr &= ~(FE_ALL_EXCEPT << 8 | FE_ALL_EXCEPT);
	__asm volatile("vmsr fpscr, %0" :: "r" (fpscr));

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
	unsigned int fpscr;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr &= ~(_ROUND_MASK << 22 | FE_ALL_EXCEPT << 8 | FE_ALL_EXCEPT);

	/* Load the floating-point sticky flags */
	fpscr |= envp->__sticky & FE_ALL_EXCEPT;
	_softfloat_float_exception_flags = envp->__sticky & FE_ALL_EXCEPT;

	/* Load the floating-point masks */
	fpscr |= (envp->__mask & FE_ALL_EXCEPT) << 8;
	_softfloat_float_exception_mask = envp->__mask & FE_ALL_EXCEPT;

	/* Load the floating-point rounding mode */
	fpscr |= (envp->__round & _ROUND_MASK) << 22;
	_softfloat_float_rounding_mode = envp->__round & _ROUND_MASK;

	__asm volatile("vmsr fpscr, %0" :: "r" (fpscr));

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
	unsigned int fpscr;

	__asm volatile("vmrs %0, fpscr" : "=r" (fpscr));
	fpscr |= _softfloat_float_exception_flags;

	/* Install new floating-point environment */
	fesetenv(envp);

	/* Raise any previously accumulated exceptions */
	feraiseexcept(fpscr & FE_ALL_EXCEPT);

	return 0;
}
DEF_STD(feupdateenv);

/*
 * The following functions are extensions to the standard
 */
int
feenableexcept(int mask)
{
	return -1;
}

int
fedisableexcept(int mask)
{
	return 0;
}

int
fegetexcept(void)
{
	return 0;
}
