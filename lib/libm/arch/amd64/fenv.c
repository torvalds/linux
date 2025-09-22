/*	$OpenBSD: fenv.c,v 1.6 2022/12/27 17:10:07 jmc Exp $	*/
/*	$NetBSD: fenv.c,v 1.1 2010/07/31 21:47:53 joerg Exp $	*/

/*-
 * Copyright (c) 2004-2005 David Schultz <das (at) FreeBSD.ORG>
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
 */

#include <fenv.h>
#include <machine/fpu.h>

/*
 * The following constant represents the default floating-point environment
 * (that is, the one installed at program startup) and has type pointer to
 * const-qualified fenv_t.
 *
 * It can be used as an argument to the functions within the <fenv.h> header
 * that manage the floating-point environment, namely fesetenv() and
 * feupdateenv().
 *
 * x87 fpu registers are 16bit wide. The upper bits, 31-16, are marked as
 * RESERVED.
 */
fenv_t __fe_dfl_env = {
	{
		0xffff0000 | __INITIAL_NPXCW__,	/* Control word register */
		0xffff0000,			/* Status word register */
		0xffffffff,			/* Tag word register */
		{
			0x00000000,
			0x00000000,
			0x00000000,
			0xffff0000
		}
	},
	__INITIAL_MXCSR__			/* MXCSR register */
};


/*
 * The feclearexcept() function clears the supported floating-point exceptions
 * represented by `excepts'.
 */
int
feclearexcept(int excepts)
{
	fenv_t fenv;
	unsigned int mxcsr;

	excepts &= FE_ALL_EXCEPT;

	/* Store the current x87 floating-point environment */
	__asm__ volatile ("fnstenv %0" : "=m" (fenv));

	/* Clear the requested floating-point exceptions */
	fenv.__x87.__status &= ~excepts;

	/* Load the x87 floating-point environment */
	__asm__ volatile ("fldenv %0" : : "m" (fenv));

	/* Same for SSE environment */
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
	mxcsr &= ~excepts;
	__asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

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
	unsigned short status;
	unsigned int mxcsr;

	excepts &= FE_ALL_EXCEPT;

	/* Store the current x87 status register */
	__asm__ volatile ("fnstsw %0" : "=am" (status));

	/* Store the MXCSR register */
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));

	/* Store the results in flagp */
	*flagp = (status | mxcsr) & excepts;

	return (0);
}

/*
 * The feraiseexcept() function raises the supported floating-point exceptions
 * represented by the argument `excepts'.
 *
 * The standard explicitly allows us to execute an instruction that has the
 * exception as a side effect, but we choose to manipulate the status register
 * directly.
 *
 * The validation of input is being deferred to fesetexceptflag().
 */
int
feraiseexcept(int excepts)
{
	excepts &= FE_ALL_EXCEPT;

	fesetexceptflag((fexcept_t *)&excepts, excepts);
	__asm__ volatile ("fwait");

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
	fenv_t fenv;
	unsigned int mxcsr;

	excepts &= FE_ALL_EXCEPT;

	/* Store the current x87 floating-point environment */
	__asm__ volatile ("fnstenv %0" : "=m" (fenv));

	/* Set the requested status flags */
	fenv.__x87.__status &= ~excepts;
	fenv.__x87.__status |= *flagp & excepts;

	/* Load the x87 floating-point environment */
	__asm__ volatile ("fldenv %0" : : "m" (fenv));

	/* Same for SSE environment */
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
	mxcsr &= ~excepts;
	mxcsr |= *flagp & excepts;
	__asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

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
	unsigned short status;
	unsigned int mxcsr;

	excepts &= FE_ALL_EXCEPT;

	/* Store the current x87 status register */
	__asm__ volatile ("fnstsw %0" : "=am" (status));

	/* Store the MXCSR register state */
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));

	return ((status | mxcsr) & excepts);
}
DEF_STD(fetestexcept);

/*
 * The fegetround() function gets the current rounding direction.
 */
int
fegetround(void)
{
	unsigned short control;

	/*
	 * We assume that the x87 and the SSE unit agree on the
	 * rounding mode.  Reading the control word on the x87 turns
	 * out to be about 5 times faster than reading it on the SSE
	 * unit on an Opteron 244.
	 */
	__asm__ volatile ("fnstcw %0" : "=m" (control));

	return (control & _X87_ROUND_MASK);
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
	unsigned short control;
	unsigned int mxcsr;

	/* Check whether requested rounding direction is supported */
	if (round & ~_X87_ROUND_MASK)
		return (-1);

	/* Store the current x87 control word register */
	__asm__ volatile ("fnstcw %0" : "=m" (control));

	/* Set the rounding direction */
	control &= ~_X87_ROUND_MASK;
	control |= round;

	/* Load the x87 control word register */
	__asm__ volatile ("fldcw %0" : : "m" (control));

	/* Same for the SSE environment */
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
	mxcsr &= ~(_X87_ROUND_MASK << _SSE_ROUND_SHIFT);
	mxcsr |= round << _SSE_ROUND_SHIFT;
	__asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

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
	/* Store the current x87 floating-point environment */
	__asm__ volatile ("fnstenv %0" : "=m" (*envp));

	/* Store the MXCSR register state */
	__asm__ volatile ("stmxcsr %0" : "=m" (envp->__mxcsr));

	/*
	 * When an FNSTENV instruction is executed, all pending exceptions are
	 * essentially lost (either the x87 FPU status register is cleared or
	 * all exceptions are masked).
	 *
	 * 8.6 X87 FPU EXCEPTION SYNCHRONIZATION -
	 * Intel(R) 64 and IA-32 Architectures Softare Developer's Manual - Vol1
	 */
	__asm__ volatile ("fldcw %0" : : "m" (envp->__x87.__control));

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
	unsigned int mxcsr;

	/* Store the current x87 floating-point environment */
	__asm__ volatile ("fnstenv %0" : "=m" (*envp));

	/* Clear all exception flags in FPU */
	__asm__ volatile ("fnclex");

	/* Store the MXCSR register state */
	__asm__ volatile ("stmxcsr %0" : "=m" (envp->__mxcsr));

	/* Clear exception flags in MXCSR */
	mxcsr = envp->__mxcsr;
	mxcsr &= ~FE_ALL_EXCEPT;

	/* Mask all exceptions */
	mxcsr |= FE_ALL_EXCEPT << _SSE_MASK_SHIFT;

	/* Store the MXCSR register */
	__asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

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
	/* Load the x87 floating-point environment */
	__asm__ volatile ("fldenv %0" : : "m" (*envp));

	/* Store the MXCSR register */
	__asm__ volatile ("ldmxcsr %0" : : "m" (envp->__mxcsr));

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
	unsigned short status;
	unsigned int mxcsr;

	/* Store the x87 status register */
	__asm__ volatile ("fnstsw %0" : "=am" (status));

	/* Store the MXCSR register */
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));

	/* Install new floating-point environment */
	fesetenv(envp);

	/* Raise any previously accumulated exceptions */
	feraiseexcept(status | mxcsr);

	return (0);
}
DEF_STD(feupdateenv);

/*
 * The following functions are extensions to the standard
 */
int
feenableexcept(int mask)
{
	unsigned int mxcsr, omask;
	unsigned short control;

	mask &= FE_ALL_EXCEPT;

	__asm__ volatile ("fnstcw %0" : "=m" (control));
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));

	omask = ~(control | (mxcsr >> _SSE_MASK_SHIFT)) & FE_ALL_EXCEPT;
	control &= ~mask;
	__asm__ volatile ("fldcw %0" : : "m" (control));

	mxcsr &= ~(mask << _SSE_MASK_SHIFT);
	__asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

	return (omask);
}

int
fedisableexcept(int mask)
{
	unsigned int mxcsr, omask;
	unsigned short control;

	mask &= FE_ALL_EXCEPT;

	__asm__ volatile ("fnstcw %0" : "=m" (control));
	__asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));

	omask = ~(control | (mxcsr >> _SSE_MASK_SHIFT)) & FE_ALL_EXCEPT;
	control |= mask;
	__asm__ volatile ("fldcw %0" : : "m" (control));

	mxcsr |= mask << _SSE_MASK_SHIFT;
	__asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

	return (omask);
}

int
fegetexcept(void)
{
	unsigned short control;

	/*
	 * We assume that the masks for the x87 and the SSE unit are
	 * the same.
	 */
	__asm__ volatile ("fnstcw %0" : "=m" (control));

	return (~control & FE_ALL_EXCEPT);
}
