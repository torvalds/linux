/*	$OpenBSD: fcntl_dup.c,v 1.3 2017/07/27 10:42:24 bluhm Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>

int
main(int argc, char *argv[])
{
	int orgfd, fd1, fd2;
	char temp[] = "/tmp/dup2XXXXXXXXX";

	if ((orgfd = mkstemp(temp)) < 0)
		err(1, "mkstemp");
	remove(temp);

	if (ftruncate(orgfd, 1024) != 0)
		err(1, "ftruncate");

	if ((fd1 = dup(orgfd)) < 0)
		err(1, "dup");

	/* Set close-on-exec */
	if (fcntl(fd1, F_SETFD, 1) != 0)
		err(1, "fcntl(F_SETFD)");

	if ((fd2 = fcntl(fd1, F_DUPFD, 0)) < 0)
		err(1, "fcntl(F_DUPFD)");

	/* Test 2: Was close-on-exec cleared? */
	if (fcntl(fd2, F_GETFD) != 0)
		errx(1, "fcntl(F_DUPFD) didn't clear close-on-exec");

	return 0;
}
