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

/*
 * Test the correctness and C99-compliance of various fenv.h features.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <err.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Implementations are permitted to define additional exception flags
 * not specified in the standard, so it is not necessarily true that
 * FE_ALL_EXCEPT == ALL_STD_EXCEPT.
 */
#define	ALL_STD_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | FE_INVALID | \
			 FE_OVERFLOW | FE_UNDERFLOW)

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
static void
init_exceptsets(void)
{
	unsigned i, j, sr;

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
	volatile double d;

	/*
	 * This test works just as well with 0.0 - 0.0, except on ia64
	 * where 0.0 - 0.0 gives the wrong sign when rounding downwards.
	 */
	d = 1.0;
	d -= 1.0;
	if (copysign(1.0, d) < 0.0)
		return (FE_DOWNWARD);

	d = 1.0;
	if (d + (DBL_EPSILON * 3.0 / 4.0) == 1.0)
		return (FE_TOWARDZERO);
	if (d + (DBL_EPSILON * 1.0 / 4.0) > 1.0)
		return (FE_UPWARD);

	return (FE_TONEAREST);
}

static void
trap_handler(int sig)
{

	assert(sig == SIGFPE);
	_exit(0);
}

/*
 * This tests checks the default FP environment, so it must be first.
 * The memcmp() test below may be too much to ask for, since there
 * could be multiple machine-specific default environments.
 */
static void
test_dfl_env(void)
{
#ifndef NO_STRICT_DFL_ENV
	fenv_t env;

	fegetenv(&env);

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
	assert(memcmp(&env.__mxcsr, &FE_DFL_ENV->__mxcsr,
	    sizeof(env.__mxcsr)) == 0);
	assert(memcmp(&env.__x87.__control, &FE_DFL_ENV->__x87.__control,
	    sizeof(env.__x87.__control)) == 0);
	assert(memcmp(&env.__x87.__status, &FE_DFL_ENV->__x87.__status,
	    sizeof(env.__x87.__status)) == 0);
	assert(memcmp(&env.__x87.__tag, &FE_DFL_ENV->__x87.__tag,
	    sizeof(env.__x87.__tag)) == 0);
#else
	assert(memcmp(&env, FE_DFL_ENV, sizeof(env)) == 0);
#endif

#endif
	assert(fetestexcept(FE_ALL_EXCEPT) == 0);
}

/*
 * Test fetestexcept() and feclearexcept().
 */
static void
test_fetestclearexcept(void)
{
	int excepts, i;

	for (i = 0; i < 1 << NEXCEPTS; i++)
		assert(fetestexcept(std_except_sets[i]) == 0);
	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		/* FE_ALL_EXCEPT might be special-cased, as on i386. */
		raiseexcept(excepts);
		assert(fetestexcept(excepts) == excepts);
		assert(feclearexcept(FE_ALL_EXCEPT) == 0);
		assert(fetestexcept(FE_ALL_EXCEPT) == 0);

		raiseexcept(excepts);
		assert(fetestexcept(excepts) == excepts);
		if ((excepts & (FE_UNDERFLOW | FE_OVERFLOW)) != 0) {
			excepts |= FE_INEXACT;
			assert((fetestexcept(ALL_STD_EXCEPT) | FE_INEXACT) ==
			    excepts);
		} else {
			assert(fetestexcept(ALL_STD_EXCEPT) == excepts);
		}
		assert(feclearexcept(excepts) == 0);
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
	}
}

/*
 * Test fegetexceptflag() and fesetexceptflag().
 *
 * Prerequisites: fetestexcept(), feclearexcept()
 */
static void
test_fegsetexceptflag(void)
{
	fexcept_t flag;
	int excepts, i;

	assert(fetestexcept(FE_ALL_EXCEPT) == 0);
	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		assert(fegetexceptflag(&flag, excepts) == 0);
		raiseexcept(ALL_STD_EXCEPT);
		assert(fesetexceptflag(&flag, excepts) == 0);
		assert(fetestexcept(ALL_STD_EXCEPT) ==
		    (ALL_STD_EXCEPT ^ excepts));

		assert(fegetexceptflag(&flag, FE_ALL_EXCEPT) == 0);
		assert(feclearexcept(FE_ALL_EXCEPT) == 0);
		assert(fesetexceptflag(&flag, excepts) == 0);
		assert(fetestexcept(ALL_STD_EXCEPT) == 0);
		assert(fesetexceptflag(&flag, ALL_STD_EXCEPT ^ excepts) == 0);
		assert(fetestexcept(ALL_STD_EXCEPT) ==
		    (ALL_STD_EXCEPT ^ excepts));

		assert(feclearexcept(FE_ALL_EXCEPT) == 0);
	}
}

/*
 * Test feraiseexcept().
 *
 * Prerequisites: fetestexcept(), feclearexcept()
 */
static void
test_feraiseexcept(void)
{
	int excepts, i;

	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		assert(fetestexcept(FE_ALL_EXCEPT) == 0);
		assert(feraiseexcept(excepts) == 0);
		if ((excepts & (FE_UNDERFLOW | FE_OVERFLOW)) != 0) {
			excepts |= FE_INEXACT;
			assert((fetestexcept(ALL_STD_EXCEPT) | FE_INEXACT) ==
			    excepts);
		} else {
			assert(fetestexcept(ALL_STD_EXCEPT) == excepts);
		}
		assert(feclearexcept(FE_ALL_EXCEPT) == 0);
	}
	assert(feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
	assert(fetestexcept(ALL_STD_EXCEPT) == (FE_INVALID | FE_DIVBYZERO));
	assert(feraiseexcept(FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT) == 0);
	assert(fetestexcept(ALL_STD_EXCEPT) == ALL_STD_EXCEPT);
	assert(feclearexcept(FE_ALL_EXCEPT) == 0);
}

/*
 * Test fegetround() and fesetround().
 */
static void
test_fegsetround(void)
{

	assert(fegetround() == FE_TONEAREST);
	assert(getround() == FE_TONEAREST);
	assert(FLT_ROUNDS == 1);

	assert(fesetround(FE_DOWNWARD) == 0);
	assert(fegetround() == FE_DOWNWARD);
	assert(getround() == FE_DOWNWARD);
	assert(FLT_ROUNDS == 3);

	assert(fesetround(FE_UPWARD) == 0);
	assert(getround() == FE_UPWARD);
	assert(fegetround() == FE_UPWARD);
	assert(FLT_ROUNDS == 2);

	assert(fesetround(FE_TOWARDZERO) == 0);
	assert(getround() == FE_TOWARDZERO);
	assert(fegetround() == FE_TOWARDZERO);
	assert(FLT_ROUNDS == 0);

	assert(fesetround(FE_TONEAREST) == 0);
	assert(getround() == FE_TONEAREST);
	assert(FLT_ROUNDS == 1);

	assert(feclearexcept(FE_ALL_EXCEPT) == 0);
}

/*
 * Test fegetenv() and fesetenv().
 *
 * Prerequisites: fetestexcept(), feclearexcept(), fegetround(), fesetround()
 */
static void
test_fegsetenv(void)
{
	fenv_t env1, env2;
	int excepts, i;

	for (i = 0; i < 1 << NEXCEPTS; i++) {
		excepts = std_except_sets[i];

		assert(fetestexcept(FE_ALL_EXCEPT) == 0);
		assert(fegetround() == FE_TONEAREST);
		assert(fegetenv(&env1) == 0);

		/*
		 * fe[gs]etenv() should be able to save and restore
		 * exception flags without the spurious inexact
		 * exceptions that afflict raiseexcept().
		 */
		raiseexcept(excepts);
		if ((excepts & (FE_UNDERFLOW | FE_OVERFLOW)) != 0 &&
		    (excepts & FE_INEXACT) == 0)
			assert(feclearexcept(FE_INEXACT) == 0);

		fesetround(FE_DOWNWARD);
		assert(fegetenv(&env2) == 0);
		assert(fesetenv(&env1) == 0);
		assert(fetestexcept(FE_ALL_EXCEPT) == 0);
		assert(fegetround() == FE_TONEAREST);

		assert(fesetenv(&env2) == 0);
		assert(fetestexcept(FE_ALL_EXCEPT) == excepts);
		assert(fegetround() == FE_DOWNWARD);
		assert(fesetenv(&env1) == 0);
		assert(fetestexcept(FE_ALL_EXCEPT) == 0);
		assert(fegetround() == FE_TONEAREST);
	}
}

/*
 * Test fegetexcept(), fedisableexcept(), and feenableexcept().
 *
 * Prerequisites: fetestexcept(), feraiseexcept()
 */
static void
test_masking(void)
{
	struct sigaction act;
	int except, pass, raise, status;
	unsigned i;

	assert((fegetexcept() & ALL_STD_EXCEPT) == 0);
	assert((feenableexcept(FE_INVALID|FE_OVERFLOW) & ALL_STD_EXCEPT) == 0);
	assert((feenableexcept(FE_UNDERFLOW) & ALL_STD_EXCEPT) ==
	    (FE_INVALID | FE_OVERFLOW));
	assert((fedisableexcept(FE_OVERFLOW) & ALL_STD_EXCEPT) ==
	    (FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW));
	assert((fegetexcept() & ALL_STD_EXCEPT) == (FE_INVALID | FE_UNDERFLOW));
	assert((fedisableexcept(FE_ALL_EXCEPT) & ALL_STD_EXCEPT) ==
	    (FE_INVALID | FE_UNDERFLOW));
	assert((fegetexcept() & ALL_STD_EXCEPT) == 0);

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
				assert((fegetexcept() & ALL_STD_EXCEPT) == 0);
				assert((feenableexcept(except)
					   & ALL_STD_EXCEPT) == 0);
				assert(fegetexcept() == except);
				raiseexcept(raise);
				assert(feraiseexcept(raise) == 0);
				assert(fetestexcept(ALL_STD_EXCEPT) == raise);

				assert(sigaction(SIGFPE, &act, NULL) == 0);
				switch (pass) {
				case 0:
					raiseexcept(except);
				case 1:
					feraiseexcept(except);
				default:
					assert(0);
				}
				assert(0);
			default:	/* parent */
				assert(wait(&status) > 0);
				/*
				 * Avoid assert() here so that it's possible
				 * to examine a failed child's core dump.
				 */
				if (!WIFEXITED(status))
					errx(1, "child aborted\n");
				assert(WEXITSTATUS(status) == 0);
				break;
			case -1:	/* error */
				assert(0);
			}
		}
	}
	assert(fetestexcept(FE_ALL_EXCEPT) == 0);
}

/*
 * Test feholdexcept() and feupdateenv().
 *
 * Prerequisites: fetestexcept(), fegetround(), fesetround(),
 *	fedisableexcept(), feenableexcept()
 */
static void
test_feholdupdate(void)
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
					assert((feenableexcept(except) &
						   ALL_STD_EXCEPT) == 0);
				raiseexcept(raise);
				assert(fesetround(FE_DOWNWARD) == 0);
				assert(feholdexcept(&env) == 0);
				assert(fetestexcept(FE_ALL_EXCEPT) == 0);
				raiseexcept(except);
				assert(fesetround(FE_UPWARD) == 0);

				if (pass == 1)
					assert(sigaction(SIGFPE, &act, NULL) ==
					    0);
				assert(feupdateenv(&env) == 0);
				assert(fegetround() == FE_DOWNWARD);
				assert(fetestexcept(ALL_STD_EXCEPT) ==
				    (except | raise));

				assert(pass == 0);
				_exit(0);
			default:	/* parent */
				assert(wait(&status) > 0);
				/*
				 * Avoid assert() here so that it's possible
				 * to examine a failed child's core dump.
				 */
				if (!WIFEXITED(status))
					errx(1, "child aborted\n");
				assert(WEXITSTATUS(status) == 0);
				break;
			case -1:	/* error */
				assert(0);
			}
		}
	}
	assert(fetestexcept(FE_ALL_EXCEPT) == 0);
}

int
main(void)
{

	printf("1..8\n");
	init_exceptsets();
	test_dfl_env();
	printf("ok 1 - fenv\n");
	test_fetestclearexcept();
	printf("ok 2 - fenv\n");
	test_fegsetexceptflag();
	printf("ok 3 - fenv\n");
	test_feraiseexcept();
	printf("ok 4 - fenv\n");
	test_fegsetround();
	printf("ok 5 - fenv\n");
	test_fegsetenv();
	printf("ok 6 - fenv\n");
	test_masking();
	printf("ok 7 - fenv\n");
	test_feholdupdate();
	printf("ok 8 - fenv\n");

	return (0);
}
