/*	$OpenBSD: round.c,v 1.3 2023/01/27 16:39:58 miod Exp $	*/

/*	Written by Michael Shalayeff, 2003,  Public domain.	*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <ieeefp.h>

static void
sigfpe(int sig, siginfo_t *si, void *v)
{
	char buf[132];

	if (si) {
		snprintf(buf, sizeof(buf), "sigfpe: addr=%p, code=%d\n",
		    si->si_addr, si->si_code);
		write(1, buf, strlen(buf));
	}
	_exit(1);
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sigfpe;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGFPE, &sa, NULL);

	assert(round(8.6) == 9.);
	assert(roundf(8.6F) == 9.);
	assert(roundl(8.6L) == 9.);
 	assert(lround(8.6) == 9L);
 	assert(lroundf(8.6F) == 9L);
 	assert(lroundl(8.6L) == 9L);
 	assert(llround(8.6) == 9LL);
 	assert(llroundf(8.6F) == 9LL);
 	assert(llroundl(8.6L) == 9LL);

	assert(lround(0.0) == 0L);
	assert(lroundf(0.0F) == 0L);
	assert(lroundl(0.0L) == 0L);
	assert(lround(-0.0) == 0L);
	assert(lroundf(-0.0F) == 0L);
	assert(lroundl(-0.0L) == 0L);

	/* Test proper rounding direction of halfway values. */
	assert(round(2.5) == 3.);
	assert(roundf(2.5F) == 3.);
	assert(roundl(2.5L) == 3.);
	assert(round(-2.5) == -3.);
	assert(roundf(-2.5F) == -3.);
	assert(roundl(-2.5L) == -3.);

	assert(llround(4503599627370496.0) == 4503599627370496LL);
	assert(llroundf(4503599627370496.0F) == 4503599627370496LL);
	assert(llroundl(4503599627370496.0L) == 4503599627370496LL);
	assert(llround(-4503599627370496.0) == -4503599627370496LL);
	assert(llroundf(-4503599627370496.0F) == -4503599627370496LL);
	assert(llroundl(-4503599627370496.0L) == -4503599627370496LL);

	assert(llround(0x7ffffffffffffc00.0p0) == 0x7ffffffffffffc00LL);
	assert(llroundf(0x7fffff8000000000.0p0F) == 0x7fffff8000000000LL);
	assert(llroundl(0x7fffff8000000000.0p0L) == 0x7fffff8000000000LL);
	assert(llround(-0x8000000000000000.0p0) == -0x8000000000000000LL);
	assert(llroundf(-0x8000000000000000.0p0F) == -0x8000000000000000LL);
	assert(llroundl(-0x8000000000000000.0p0L) == -0x8000000000000000LL);

	exit(0);
}
