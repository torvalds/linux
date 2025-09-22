/*	$OpenBSD: gettimeofday.c,v 1.2 2003/07/31 21:48:08 deraadt Exp $	*/
/*
 *	Written by Thomas Nordin <nordin@openbsd.org> 2002 Public Domain.
 */
#include <err.h>
#include <stdio.h>

#include <sys/time.h>

int
main(int argc, char *argv[])
{
	struct timeval s;
	struct timeval t1;
	struct timeval t2;

	if (gettimeofday(&s, NULL) == -1)
		err(1, "gettimeofday");

	do {
		if (gettimeofday(&t1, NULL) == -1)
			err(1, "gettimeofday");
		if (gettimeofday(&t2, NULL) == -1)
			err(1, "gettimeofday");

		if (timercmp(&t2, &t1, <))
			errx(1, "time of day decreased");
        } while (t1.tv_sec - s.tv_sec < 7);

        return 0;
}
