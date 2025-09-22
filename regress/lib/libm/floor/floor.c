/*	$OpenBSD: floor.c,v 1.2 2011/07/09 03:33:07 martynas Exp $	*/

/*	Written by Michael Shalayeff, 2003,  Public domain.	*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>

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

	if (floor(4294967295.7) != 4294967295.)
		exit(1);
	if (floorl(4294967295.7L) != 4294967295.L)
		exit(1);

	exit(0);
}
