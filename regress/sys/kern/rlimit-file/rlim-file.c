/*	$OpenBSD: rlim-file.c,v 1.4 2014/05/20 01:25:24 guenther Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> (2002) Public Domain.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	int lim, fd, fds[2];
	struct rlimit rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
		err(1, "getrlimit");

	lim = rl.rlim_cur;

	fd = -1;
	while (fd < lim - 2)
		if ((fd = open("/dev/null", O_RDONLY)) < 0)
			err(1, "open");

	if (pipe(fds) == 0)
		errx(1, "pipe was allowed");

	if (errno == ENFILE)
		errx(1, "try to do the test on a less loaded system");

	if (errno != EMFILE)
		err(1, "bad errno (%d)", errno);

	return 0;
}

