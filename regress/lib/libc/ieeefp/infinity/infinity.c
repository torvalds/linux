/*	$OpenBSD: infinity.c,v 1.2 2004/01/16 19:34:37 miod Exp $	*/
/*
 * Written by Miodrag Vallat, 2004 - Public Domain
 * Inspired from Perl's t/op/arith test #134
 */

#include <math.h>
#include <signal.h>
#include <unistd.h>

void
sigfpe(int signum)
{
	/* looks like we don't handle fp overflow correctly... */
	_exit(1);
}

int
main(int argc, char *argv[])
{
	int opt;
	double d, two;
	int i;
	char method = 'a';

	while ((opt = getopt(argc, argv, "amnp")) != -1)
		method = (char)opt;

	signal(SIGFPE, sigfpe);

	switch (method) {
	case 'a':
		/* try to produce +Inf through addition */
		d = 1.0;
		for (i = 2000; i != 0; i--) {
			d = d + d;
		}
		/* result should be _positive_ infinity */
		if (!isinf(d) || copysign(1.0, d) < 0.0)
			return (1);
		break;
	case 'm':
		/* try to produce +Inf through multiplication */
		d = 1.0;
		two = 2.0;
		for (i = 2000; i != 0; i--) {
			d = d * two;
		}
		/* result should be _positive_ infinity */
		if (!isinf(d) || copysign(1.0, d) < 0.0)
			return (1);
		break;
	case 'n':
		/* try to produce -Inf through subtraction */
		d = -1.0;
		for (i = 2000; i != 0; i--) {
			d = d + d;
		}
		/* result should be _negative_ infinity */
		if (!isinf(d) || copysign(1.0, d) > 0.0)
			return (1);
		break;
	case 'p':
		/* try to produce -Inf through multiplication */
		d = -1.0;
		two = 2.0;
		for (i = 2000; i != 0; i--) {
			d = d * two;
		}
		/* result should be _negative_ infinity */
		if (!isinf(d) || copysign(1.0, d) > 0.0)
			return (1);
		break;
	}

	return (0);
}
