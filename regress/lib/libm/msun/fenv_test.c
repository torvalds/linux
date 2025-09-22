/*	$OpenBSD: fenv_test.c,v 1.7 2021/12/13 18:04:28 deraadt Exp $	*/
/*-
 * Copyright (c) 2004 David Schultz <das@FreeBSD.org>
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

#include "macros.h"

/*
 * Test the correctness and C99-compliance of various fenv.h features.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <err.h>
#include <fenv.h>
#include <float.h>
#ifndef __OpenBSD__
#include <libutil.h>
#endif
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test-utils.h"

#define	NEXCEPTS	(sizeof(std_excepts) / sizeof(std_excepts[0]))

static const int std_excepts[] = {
	FE_INVALID,
	FE_DIVBYZERO,
	FE_OVERFLOW,
	FE_UNDERFLOW,
	FE_INEXACT,
};

/* init_exceptsets() initializes this to the power set of std_excepts[] */
static int std_except_sets[1 << NEXCEPTS];

#pragma STDC FENV_ACCESS ON

/*
 * Initialize std_except_sets[] to the power set of std_excepts[]
 */
static __attribute__((constructor)) void
do_setup(void)
{
	unsigned i, j, sr;

	/* Avoid double output after fork() */
	setvbuf(stdout, NULL, _IONBF, 0);

	for (i = 0; i < 1 << NEXCEPTS; i++) {
		for (sr = i, j = 0; sr != 0; sr >>= 1, j++)
			std_except_sets[i] |= std_excepts[j] & ((~sr & 1) - 1);
	}
}

/*
 * Raise a floating-point exception without relying on the standard
 * library routines, which we are trying to test.
 *
 * XXX We can't raise an {over,under}flow without also raising an
 * inexact exception.
 */
static void
raiseexcept(int excepts)
{
	volatile double d;

	/*
	 * With a compiler that supports the FENV_ACCESS pragma
	 * properly, simple expressions like '0.0 / 0.0' should
	 * be sufficient to generate traps.  Unfortunately, we
	 * need to bring a volatile variable into the equation
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
		d = DBL_MAX;
		d *= 2.0;
	}
	if (excepts & FE_UNDERFLOW) {
		d = DBL_MIN;
		d /= DBL_MAX;
	}
	if (excepts & FE_INEXACT) {
		d = DBL_MIN;
		d += 1.0;
	}

	/*
	 * On the x86 (and some other architectures?) the FPU and
	 * integer units are decoupled.  We need to execute an FWAIT
	 * or a floating-point instruction to get synchronous exceptions.
	 */
	d = 1.0;
	d += 1.0;
}

/*
 * Determine the current rounding mode without relying on the fenv
 * routines.  This function may raise an inexact exception.
 */
static int
getround(void)
{
	volatile double d, e;

	/*
	 * This test works just as well with 0.0 - 0.0, except on ia64
	 * where 0.0 - 0.0 gives the wrong sign when rounding downwards.
	 * For ia32 use a volatile double to force 64 bit rounding.
	 * Otherwise the i387 would use its internal 80 bit stack.
	 */
	d = 1.0;
	d -= 1.0;
	if (copysign(1.0, d) < 0.0)
		return (FE_DOWNWARD);

	d = 1.0;
	e = d + (DBL_EPSILON * 3.0 / 4.0);
	if (e == 1.0)
		return (FE_TOWARDZERO);
	e = d + (DBL_EPSILON * 1.0 / 4.0);
	if (e > 1.0)
		return (FE_UPWARD);

	return (FE_TONEAREST);
}

static void
trap_handler(int sig)
{

	ATF_CHECK_EQ(SIGFPE, sig);
	_exit(0);
}

/*
 * This tests checks the default FP environment, so it must be first.
 * The memcmp() test below may be too much to ask for, since there
 * could be multiple machine-specific default environments.
 */
ATF_TC_WITHOUT_HEAD(dfl_env);
ATF_TC_BODY(dfl_env, tc)
{
#ifndef NO_STRICT_DFL_ENV
	fenv_t env;

	fegetenv(&env);
	/* Print the default environment for debugging purposes. */
	hexdump(&env, sizeof(env), "current fenv ", HD_OMIT_CHARS);
	hexdump(FE_DFL_ENV, sizeof(env), "default fenv ", HD_OMIT_CHARS);
	CHECK_FP_EXCEPTIONS(0, FE_ALL_EXCEPT);
#ifdef __amd64__
	/*
	 * Compare the fields that the AMD [1] and Intel [2] specs say will be
	 * set once fnstenv returns.
	 *
	 * Not all amd64 capable processors implement the fnstenv instruction
	 * by zero'ing out the env.__x87.__other field (example: AMD Opteron
	 * 6308). The AMD64/x64 specs aren't explicit on what the
	 * env.__x87.__other field will contain after fnstenv is executed, so
	 * the values in env.__x87.__other could be filled with arbitrary
	 * data depending on how the CPU implements fnstenv.
	 *
	 * 1. http://support.amd.com/TechDocs/26569_APM_v5.pdf
	 * 2. http://www.intel.com/Assets/en_US/PDF/manual/253666.pdf
	 */
	ATF_CHECK(memcmp(&env.__mxcsr, &FE_DFL_ENV->__mxcsr,
	    sizeof(env.__mxcsr)) == 0);
	ATF_CHECK(memcmp(&env.__x87.__control, &FE_DFL_ENV->__x87.__control,
	    sizeof(env.__x87.__control)) == 0);
	ATF_CHECK(memcmp(&env.__x87.__status, &FE_DFL_ENV->__x87.__status,
	    sizeof(env.__x87.__status)) == 0);
	ATF_CHECK(memcmp(&env.__x87.__tag, &FE_DFL_ENV->__x87.__tag,
	    sizeof(env.__x87.__tag)) == 0);
#else
	ATF_CHECK_EQ(0, memcmp(&env, FE_DFL_ENV, sizeof(env)));
#endif

#endif
	CHECK_FP_EXCEPTIONS(0, FE_ALL_EXCEPT);
}

/*
 * Test fetestexcept() and feclearexcept().
 */
ATF_TC_WITHOUT_HEAD(fetestclearexcept);
ATF_TC_BODY(fetestclearexcept, tc)
{
	int excepts, i;

	for (i = 0; i < 1 << NEXCEPTS; i++)
		ATF_CHECK_EQ(0, fetestexcept(std_except_sets[i]));
	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		/* FE_ALL_EXCEPT might be special-cased, as on i386. */
		raiseexcept(excepts);
		ATF_CHECK_EQ(excepts, fetestexcept(excepts));
		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
		ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));

		raiseexcept(excepts);
		ATF_CHECK_EQ(excepts, fetestexcept(excepts));
		if ((excepts & (FE_UNDERFLOW | FE_OVERFLOW)) != 0) {
			excepts |= FE_INEXACT;
			ATF_CHECK_EQ(excepts, (fetestexcept(ALL_STD_EXCEPT) | FE_INEXACT));
		} else {
			ATF_CHECK_EQ(excepts, fetestexcept(ALL_STD_EXCEPT));
		}
		ATF_CHECK_EQ(0, feclearexcept(excepts));
		ATF_CHECK_EQ(0, fetestexcept(ALL_STD_EXCEPT));
	}
}

/*
 * Test fegetexceptflag() and fesetexceptflag().
 *
 * Prerequisites: fetestexcept(), feclearexcept()
 */
ATF_TC_WITHOUT_HEAD(fegsetexceptflag);
ATF_TC_BODY(fegsetexceptflag, tc)
{
	fexcept_t flag;
	int excepts, i;

	CHECK_FP_EXCEPTIONS(0, FE_ALL_EXCEPT);
	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		ATF_CHECK_EQ(0, fegetexceptflag(&flag, excepts));
		raiseexcept(ALL_STD_EXCEPT);
		ATF_CHECK_EQ(0, fesetexceptflag(&flag, excepts));
		ATF_CHECK_EQ((ALL_STD_EXCEPT ^ excepts), fetestexcept(ALL_STD_EXCEPT));

		ATF_CHECK_EQ(0, fegetexceptflag(&flag, FE_ALL_EXCEPT));
		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
		ATF_CHECK_EQ(0, fesetexceptflag(&flag, excepts));
		ATF_CHECK_EQ(0, fetestexcept(ALL_STD_EXCEPT));
		ATF_CHECK_EQ(0, fesetexceptflag(&flag, ALL_STD_EXCEPT ^ excepts));
		ATF_CHECK_EQ((ALL_STD_EXCEPT ^ excepts), fetestexcept(ALL_STD_EXCEPT));

		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
	}
}

/*
 * Test feraiseexcept().
 *
 * Prerequisites: fetestexcept(), feclearexcept()
 */
ATF_TC_WITHOUT_HEAD(feraiseexcept);
ATF_TC_BODY(feraiseexcept, tc)
{
	int excepts, i;

	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
		ATF_CHECK_EQ(0, feraiseexcept(excepts));
		if ((excepts & (FE_UNDERFLOW | FE_OVERFLOW)) != 0) {
			excepts |= FE_INEXACT;
			ATF_CHECK_EQ(excepts, (fetestexcept(ALL_STD_EXCEPT) | FE_INEXACT));
		} else {
			ATF_CHECK_EQ(excepts, fetestexcept(ALL_STD_EXCEPT));
		}
		ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
	}
	ATF_CHECK_EQ(0, feraiseexcept(FE_INVALID | FE_DIVBYZERO));
	ATF_CHECK_EQ((FE_INVALID | FE_DIVBYZERO), fetestexcept(ALL_STD_EXCEPT));
	ATF_CHECK_EQ(0, feraiseexcept(FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT));
	ATF_CHECK_EQ(ALL_STD_EXCEPT, fetestexcept(ALL_STD_EXCEPT));
	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
}

/*
 * Test fegetround() and fesetround().
 */
ATF_TC_WITHOUT_HEAD(fegsetround);
ATF_TC_BODY(fegsetround, tc)
{

	ATF_CHECK_EQ(FE_TONEAREST, fegetround());
	ATF_CHECK_EQ(FE_TONEAREST, getround());
	ATF_CHECK_EQ(1, FLT_ROUNDS);

	ATF_CHECK_EQ(0, fesetround(FE_DOWNWARD));
	ATF_CHECK_EQ(FE_DOWNWARD, fegetround());
	ATF_CHECK_EQ(FE_DOWNWARD, getround());
	ATF_CHECK_EQ(3, FLT_ROUNDS);

	ATF_CHECK_EQ(0, fesetround(FE_UPWARD));
	ATF_CHECK_EQ(FE_UPWARD, getround());
	ATF_CHECK_EQ(FE_UPWARD, fegetround());
	ATF_CHECK_EQ(2, FLT_ROUNDS);

	ATF_CHECK_EQ(0, fesetround(FE_TOWARDZERO));
	ATF_CHECK_EQ(FE_TOWARDZERO, getround());
	ATF_CHECK_EQ(FE_TOWARDZERO, fegetround());
	ATF_CHECK_EQ(0, FLT_ROUNDS);

	ATF_CHECK_EQ(0, fesetround(FE_TONEAREST));
	ATF_CHECK_EQ(FE_TONEAREST, getround());
	ATF_CHECK_EQ(1, FLT_ROUNDS);

	ATF_REQUIRE_EQ(0, feclearexcept(FE_ALL_EXCEPT));
}

/*
 * Test fegetenv() and fesetenv().
 *
 * Prerequisites: fetestexcept(), feclearexcept(), fegetround(), fesetround()
 */
ATF_TC_WITHOUT_HEAD(fegsetenv);
ATF_TC_BODY(fegsetenv, tc)
{
	fenv_t env1, env2;
	int excepts, i;

	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
		ATF_CHECK_EQ(FE_TONEAREST, fegetround());
		ATF_CHECK_EQ(0, fegetenv(&env1));

		/*
		 * fe[gs]etenv() should be able to save and restore
		 * exception flags without the spurious inexact
		 * exceptions that afflict raiseexcept().
		 */
		raiseexcept(excepts);
		if ((excepts & (FE_UNDERFLOW | FE_OVERFLOW)) != 0 &&
		    (excepts & FE_INEXACT) == 0)
			ATF_CHECK_EQ(0, feclearexcept(FE_INEXACT));

		fesetround(FE_DOWNWARD);
		ATF_CHECK_EQ(0, fegetenv(&env2));
		ATF_CHECK_EQ(0, fesetenv(&env1));
		ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
		ATF_CHECK_EQ(FE_TONEAREST, fegetround());

		ATF_CHECK_EQ(0, fesetenv(&env2));

		/* 
		 * Some platforms like powerpc may set extra exception bits. Since
		 * only standard exceptions are tested, mask against ALL_STD_EXCEPT 
		 */
		ATF_CHECK_EQ(excepts, (fetestexcept(FE_ALL_EXCEPT) & ALL_STD_EXCEPT));

		ATF_CHECK_EQ(FE_DOWNWARD, fegetround());
		ATF_CHECK_EQ(0, fesetenv(&env1));
		ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
		ATF_CHECK_EQ(FE_TONEAREST, fegetround());
	}
}

/*
 * Test fegetexcept(), fedisableexcept(), and feenableexcept().
 *
 * Prerequisites: fetestexcept(), feraiseexcept()
 */
ATF_TC_WITHOUT_HEAD(masking);
ATF_TC_BODY(masking, tc)
{
#if !defined(__arm__) && !defined(__aarch64__) && !defined(__riscv)
	struct sigaction act;
	int except, pass, raise, status;
	unsigned i;

	ATF_REQUIRE_EQ(0, (fegetexcept() & ALL_STD_EXCEPT));

	/*
	 * Some CPUs, e.g. AArch64 QEMU does not support trapping on FP
	 * exceptions. In that case the trap enable bits are all RAZ/WI, so
	 * writing to those bits will be ignored and the the next read will
	 * return all zeroes for those bits. Skip the test if no floating
	 * point exceptions are supported and mark it XFAIL if some are missing.
	 */
	ATF_REQUIRE_EQ(0, (feenableexcept(FE_ALL_EXCEPT)));
	except = fegetexcept();
	if (except == 0) {
		atf_tc_skip("CPU does not support trapping on floating point "
		    "exceptions.");
	} else if ((except & ALL_STD_EXCEPT) != ALL_STD_EXCEPT) {
		atf_tc_expect_fail("Not all floating point exceptions can be "
		    "set to trap: %#x vs %#x", except, ALL_STD_EXCEPT);
	}
	fedisableexcept(FE_ALL_EXCEPT);


	ATF_CHECK_EQ(0, (feenableexcept(FE_INVALID|FE_OVERFLOW) & ALL_STD_EXCEPT));
	ATF_CHECK_EQ((FE_INVALID | FE_OVERFLOW), (feenableexcept(FE_UNDERFLOW) & ALL_STD_EXCEPT));
	ATF_CHECK_EQ((FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW), (fedisableexcept(FE_OVERFLOW) & ALL_STD_EXCEPT));
	ATF_CHECK_EQ((FE_INVALID | FE_UNDERFLOW), (fegetexcept() & ALL_STD_EXCEPT));
	ATF_CHECK_EQ((FE_INVALID | FE_UNDERFLOW), (fedisableexcept(FE_ALL_EXCEPT) & ALL_STD_EXCEPT));
	ATF_CHECK_EQ(0, (fegetexcept() & ALL_STD_EXCEPT));

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = trap_handler;
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < NEXCEPTS; i++) {
			except = std_excepts[i];
			/* over/underflow may also raise inexact */
			if (except == FE_INEXACT)
				raise = FE_DIVBYZERO | FE_INVALID;
			else
				raise = ALL_STD_EXCEPT ^ except;

			/*
			 * We need to fork a child process because
			 * there isn't a portable way to recover from
			 * a floating-point exception.
			 */
			switch(fork()) {
			case 0:		/* child */
				ATF_CHECK_EQ(0, (fegetexcept() & ALL_STD_EXCEPT));
				ATF_REQUIRE_EQ(0, (feenableexcept(except) & ALL_STD_EXCEPT));
				ATF_CHECK_EQ(except, fegetexcept());
				raiseexcept(raise);
				ATF_CHECK_EQ(0, feraiseexcept(raise));
				ATF_CHECK_EQ(raise, fetestexcept(ALL_STD_EXCEPT));
 
				ATF_CHECK_EQ(0, sigaction(SIGFPE, &act, NULL));
				switch (pass) {
				case 0:
					raiseexcept(except);
				case 1:
					feraiseexcept(except);
				default:
					ATF_REQUIRE(0);
				}
				ATF_REQUIRE(0);
			default:	/* parent */
				ATF_REQUIRE(wait(&status) > 0);
				/*
				 * Avoid assert() here so that it's possible
				 * to examine a failed child's core dump.
				 */
				if (!WIFEXITED(status))
					errx(1, "child aborted\n");
				ATF_CHECK_EQ(0, WEXITSTATUS(status));
				break;
			case -1:	/* error */
				ATF_REQUIRE(0);
			}
		}
	}
	ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
#endif
}

/*
 * Test feholdexcept() and feupdateenv().
 *
 * Prerequisites: fetestexcept(), fegetround(), fesetround(),
 *	fedisableexcept(), feenableexcept()
 */
ATF_TC_WITHOUT_HEAD(feholdupdate);
ATF_TC_BODY(feholdupdate, tc)
{
	fenv_t env;

	struct sigaction act;
	int except, pass, status, raise;
	unsigned i;

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = trap_handler;
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < NEXCEPTS; i++) {
			except = std_excepts[i];
			/* over/underflow may also raise inexact */
			if (except == FE_INEXACT)
				raise = FE_DIVBYZERO | FE_INVALID;
			else
				raise = ALL_STD_EXCEPT ^ except;

			/*
			 * We need to fork a child process because
			 * there isn't a portable way to recover from
			 * a floating-point exception.
			 */
			switch(fork()) {
			case 0:		/* child */
				/*
				 * We don't want to cause a fatal exception in
				 * the child until the second pass, so we can
				 * check other properties of feupdateenv().
				 */
				if (pass == 1)
					ATF_REQUIRE_EQ(0,
					    feenableexcept(except) &
					    ALL_STD_EXCEPT);
				raiseexcept(raise);
				ATF_CHECK_EQ(0, fesetround(FE_DOWNWARD));
				ATF_CHECK_EQ(0, feholdexcept(&env));
				ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
				raiseexcept(except);
				ATF_CHECK_EQ(0, fesetround(FE_UPWARD));

				if (pass == 1)
					ATF_CHECK_EQ(0, sigaction(SIGFPE, &act, NULL));
				ATF_CHECK_EQ(0, feupdateenv(&env));
				ATF_CHECK_EQ(FE_DOWNWARD, fegetround());
				ATF_CHECK_EQ((except | raise), fetestexcept(ALL_STD_EXCEPT));

				ATF_CHECK_EQ(0, pass);
				_exit(0);
			default:	/* parent */
				ATF_REQUIRE(wait(&status) > 0);
				/*
				 * Avoid assert() here so that it's possible
				 * to examine a failed child's core dump.
				 */
				if (!WIFEXITED(status))
					errx(1, "child aborted\n");
				ATF_CHECK_EQ(0, WEXITSTATUS(status));
				break;
			case -1:	/* error */
				ATF_REQUIRE(0);
			}
		}
#if defined(__arm__) || defined(__aarch64__) || defined(__riscv)
		break;
#endif
	}
	ATF_CHECK_EQ(0, fetestexcept(FE_ALL_EXCEPT));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, dfl_env);
	ATF_TP_ADD_TC(tp, fetestclearexcept);
	ATF_TP_ADD_TC(tp, fegsetexceptflag);
	ATF_TP_ADD_TC(tp, feraiseexcept);
	ATF_TP_ADD_TC(tp, fegsetround);
	ATF_TP_ADD_TC(tp, fegsetenv);
	ATF_TP_ADD_TC(tp, masking);
	ATF_TP_ADD_TC(tp, feholdupdate);

	return (atf_no_error());
}
