/*	$OpenBSD: fchdir.c,v 1.2 2017/03/08 18:42:28 deraadt Exp $ */

/*
 *	Written by Philip Guenther <guenther@openbsd.org> 2011 Public Domain.
 *
 *	Verify errno returns from fchdir()
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int fds[2];
	int fd;

	if ((fd = open("/etc/passwd", O_RDONLY)) == -1)
		err(1, "open");
	if (fchdir(fd) == 0)
		errx(1, "fchdir file succeeded");
	if (errno != ENOTDIR)
		err(1, "fchdir file: wrong errno");
	close(fd);

	if (pipe(fds))
		err(1, "pipe");
	if (fchdir(fds[0]) == 0)
		errx(1, "fchdir pipe succeeded");
	if (errno != ENOTDIR)
		err(1, "fchdir pipe: wrong errno");
	close(fds[0]);
	close(fds[1]);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");
	if (fchdir(fd) == 0)
		errx(1, "fchdir socket succeeded");
	if (errno != ENOTDIR)
		err(1, "fchdir socket: wrong errno");
	close(fd);

	if (fchdir(fd) == 0)
		errx(1, "fchdir bad fd succeeded");
	if (errno != EBADF)
		err(1, "fchdir bad fd: wrong errno");

	return 0;
}
