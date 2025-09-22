/*	$OpenBSD: pread.c,v 1.4 2011/11/06 15:00:34 guenther Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{	
	char temp[] = "/tmp/dup2XXXXXXXXX";
	const char magic[10] = "0123456789";
	char c;
	int fd, ret;

	if ((fd = mkstemp(temp)) < 0)
		err(1, "mkstemp");
	remove(temp);

	if (write(fd, magic, sizeof(magic)) != sizeof(magic))
		err(1, "write");

	if (lseek(fd, 0, SEEK_SET) != 0)
		err(1, "lseek");

	if (read(fd, &c, 1) != 1)
		err(1, "read1");

	if (c != magic[0])
		errx(1, "read1 %c != %c", c, magic[0]);

	if (pread(fd, &c, 1, 7) != 1)
		err(1, "pread");

	if (c != magic[7])
		errx(1, "pread %c != %c", c, magic[7]);

	if (read(fd, &c, 1) != 1)
		err(1, "read2");

	if (c != magic[1])
		errx(1, "read2 %c != %c", c, magic[1]);

	if ((ret = pread(fd, &c, 1, -1)) != -1)
		errx(1, "pread with negative offset succeeded,\
				returning %d", ret);
	if (errno != EINVAL)
		err(1, "pread with negative offset");

	if ((ret = pread(fd, &c, 3, LLONG_MAX)) != -1)
		errx(1, "pread with wrapping offset succeeded,\
				returning %d", ret);
	if (errno != EINVAL)
		err(1, "pread with wrapping offset");

	if (read(fd, &c, 1) != 1)
		err(1, "read3");

	if (c != magic[2])
		errx(1, "read3 %c != %c", c, magic[2]);

	close(fd);

	/* also, verify that pread fails on ttys */
	fd = open("/dev/tty", O_RDWR);
	if (fd < 0)
		printf("skipping tty test\n");
	else if ((ret = pread(fd, &c, 1, 7)) != -1)
		errx(1, "pread succeeded on tty, returning %d", ret);
	else if (errno != ESPIPE)
		err(1, "pread");

	return 0;
}
