/*	$OpenBSD: except.c,v 1.12 2010/05/08 19:16:33 naddy Exp $	*/

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <ieeefp.h>
#include <float.h>
#include <err.h>

volatile sig_atomic_t signal_status;

volatile const double one  = 1.0;
volatile const double zero = 0.0;
volatile const double huge = DBL_MAX;
volatile const double tiny = DBL_MIN;

void
sigfpe(int sig, siginfo_t *si, void *v)
{
	char buf[132];

	if (si) {
		snprintf(buf, sizeof(buf), "sigfpe: addr=%p, code=%d\n",
		    si->si_addr, si->si_code);
		write(1, buf, strlen(buf));
	}
	_exit(signal_status);
}


int
main(int argc, char *argv[])
{
	struct sigaction sa;
	volatile double x;

	if (argc != 2) {
		fprintf(stderr, "usage: %s condition\n", argv[0]);
		exit(1);
	}

	/*
	 * check to make sure that all exceptions are masked and 
	 * that the accumulated exception status is clear.
 	 */
	assert(fpgetmask() == 0);
	assert(fpgetsticky() == 0);

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sigfpe;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGFPE, &sa, NULL);
	signal_status = 1;

	if (strcmp(argv[1], "fltdiv") == 0) {
		/* trip divide by zero */
		x = one / zero;
		assert(fpgetsticky() & FP_X_DZ);
		fpsetsticky(0);

		/* and now unmask to get a signal */
		signal_status = 0;
		fpsetmask(FP_X_DZ);
		x = one / zero;
	} else if (strcmp(argv[1], "fltinv") == 0) {
		/* trip invalid operation */
		x = zero / zero;
		assert(fpgetsticky() & FP_X_INV);
		fpsetsticky(0);

		/* and now unmask to get a signal */
		signal_status = 0;
		fpsetmask(FP_X_INV);
		x = zero / zero;
	} else if (strcmp(argv[1], "fltovf") == 0) {
		/* trip overflow */
		x = huge * huge;
		assert(fpgetsticky() & FP_X_OFL);
		fpsetsticky(0);

		/* and now unmask to get a signal */
		signal_status = 0;
		fpsetmask(FP_X_OFL);
		x = huge * huge;
	} else if (strcmp(argv[1], "fltund") == 0) {
		/* trip underflow */
		x = tiny * tiny;
		assert(fpgetsticky() & FP_X_UFL);
		fpsetsticky(0);

		/* and now unmask to get a signal */
		signal_status = 0;
		fpsetmask(FP_X_UFL);
		x = tiny * tiny;
	} else {
		errx(1, "unrecognized condition %s", argv[1]);
	}

	/*
	 * attempt to trigger the exception on machines where
	 * floating-point exceptions are deferred.
	 */
	x = one * one;

	errx(1, "signal wasn't caught");
}
