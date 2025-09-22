/*	$OpenBSD: itimer.c,v 1.2 2013/09/12 23:06:44 krw Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2004 Public Domain.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <err.h>
#include <signal.h>

void sighand(int);

volatile sig_atomic_t ticks;

#define TIMEOUT	2

int
main(int argc, char **argv)
{
	struct timeval stv, tv;
	struct itimerval itv;
	struct rusage ru;
	int which, sig;
	int ch;

	while ((ch = getopt(argc, argv, "rvp")) != -1) {
		switch (ch) {
		case 'r':
			which = ITIMER_REAL;
			sig = SIGALRM;
			break;
		case 'v':
			which = ITIMER_VIRTUAL;
			sig = SIGVTALRM;
			break;
		case 'p':
			which = ITIMER_PROF;
			sig = SIGPROF;
			break;
		default:
			fprintf(stderr, "Usage: itimer [-rvp]\n");
			exit(1);
		}
	}

	signal(sig, sighand);

	itv.it_value.tv_sec = 0;
	itv.it_value.tv_usec = 100000;
	itv.it_interval = itv.it_value;

	if (setitimer(which, &itv, NULL) != 0)
		err(1, "setitimer");

	gettimeofday(&stv, NULL);
	while (ticks != TIMEOUT * 10)
		;

	switch (which) {
	case ITIMER_REAL:
		gettimeofday(&tv, NULL);
		timersub(&tv, &stv, &tv);
		break;
	case ITIMER_VIRTUAL:
	case ITIMER_PROF:
		if (getrusage(RUSAGE_SELF, &ru) != 0)
			err(1, "getrusage");
		tv = ru.ru_utime;
		break;
	}
	stv.tv_sec = TIMEOUT;
	stv.tv_usec = 0;

	if (timercmp(&stv, &tv, <))
		timersub(&tv, &stv, &tv);
	else
		timersub(&stv, &tv, &tv);

	if (tv.tv_sec != 0 || tv.tv_usec > 100000)
		errx(1, "timer difference too big: %lld.%ld",
		    (long long)tv.tv_sec, tv.tv_usec);

	return (0);
}

void
sighand(int signum)
{
	ticks++;
}
