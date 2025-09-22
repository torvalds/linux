/* $OpenBSD: fenv.c,v 1.4 2022/12/27 17:10:07 jmc Exp $ */
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
 * $FreeBSD: head/lib/msun/aarch64/fenv.h 280857 2015-03-30 16:42:08Z emaste $
 */

#include <fenv.h>
#include <machine/ieeefp.h>

#define	__get_fcsr(r)	asm volatile("frcsr %0" : "=r" (r))
#define	__set_fcsr(r)	asm volatile("fscsr %0" : "+r" (r))
#define	__get_flags(r)	asm volatile("frflags %0" : "=r" (r))
#define	__set_flags(r)	asm volatile("fsflags %0" : "+r" (r))
#define	__get_frm(r)	asm volatile("frrm %0" : "=r" (r))
#define	__set_frm(r)	asm volatile("fsrm %0" : "+r"(r) )

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
	__get_flags(r);
	r &= ~excepts;
	__set_flags(r);
	return (0);
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
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;
	__get_flags(r);
	*flagp = r & excepts;
	return (0);
}

/*
 * The feraiseexcept() function raises the supported floating-point exceptions
 * represented by the argument `excepts'.
 */
int
feraiseexcept(int excepts)
{
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;
	__get_flags(r);
	r |= excepts;
	__set_flags(r);
	return (0);
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
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;
	__get_flags(r);
	r &= ~excepts;
	r |= *flagp & excepts;
	__set_flags(r);
	return (0);
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
	fexcept_t r;

	excepts &= FE_ALL_EXCEPT;
	__get_flags(r);
	return (r & excepts);
}
DEF_STD(fetestexcept);

/*
 * The fegetround() function gets the current rounding direction.
 */
int
fegetround(void)
{
	fenv_t r;

	__get_frm(r);
	return (r & _ROUND_MASK);
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
		return (-1);
	__set_frm(round);
	return (0);
}
DEF_STD(fesetround);

/*
 * The fegetenv() function attempts to store the current floating-point
 * environment in the object pointed to by envp.
 */
int
fegetenv(fenv_t *envp)
{
	fenv_t r;

	__get_fcsr(r);
	*envp = r;

	return (0);
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

	__get_fcsr(r);
	*envp  = r;
	r &= ~FE_ALL_EXCEPT;
	__set_fcsr(r);

	return (0);
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

	fenv_t r;
	r = *envp;
	__set_fcsr(r);
	return (0);
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

	__get_fcsr(r);
	fesetenv(envp);
	feraiseexcept(r & FE_ALL_EXCEPT);
	return (0);
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
