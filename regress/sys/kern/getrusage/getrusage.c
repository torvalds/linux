/*	$OpenBSD: getrusage.c,v 1.3 2003/07/31 21:48:08 deraadt Exp $	*/
/*
 *	Written by Thomas Nordin <nordin@openbsd.org> 2002 Public Domain.
 */
#include <err.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

int
main(int argc, char *argv[])
{
	struct timeval utime;
	struct timeval stime;
        struct rusage r;
        int i;

	timerclear(&utime);
	timerclear(&stime);
	do {
		if (getrusage(RUSAGE_SELF, &r) == -1)
			err(1, "getrusage");

		if (timercmp(&(r.ru_utime), &utime, <))
			errx(1, "user time decreased");
		utime = r.ru_utime;

		if (timercmp(&(r.ru_stime), &stime, <))
			errx(1, "system time decreased");
		stime = r.ru_stime;
        } while (utime.tv_sec < 1 && stime.tv_sec < 1);

        return 0;
}
