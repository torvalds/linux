/*	$OpenBSD: pwritev.c,v 1.5 2011/11/06 15:00:34 guenther Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */
#include <sys/types.h>
#include <sys/uio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{	
	char temp[] = "/tmp/pwritevXXXXXXXXX";
	char magic[10] = "0123456789";
	const char zeroes[10] = "0000000000";
	char buf[10];
	struct iovec iov[2];
	char c;
	int fd, ret;

	if ((fd = mkstemp(temp)) < 0)
		err(1, "mkstemp");
	remove(temp);

	if (write(fd, zeroes, sizeof(zeroes)) != sizeof(zeroes))
		err(1, "write");

	if (lseek(fd, 5, SEEK_SET) != 5)
		err(1, "lseek");

	iov[0].iov_base = &magic[8];
	iov[0].iov_len = 2;
	iov[1].iov_base = &magic[7];
	iov[1].iov_len = 2;

	if (pwritev(fd, iov, 2, 4) != 4)
		err(1, "pwritev");

	if (read(fd, &c, 1) != 1)
		err(1, "read");

	if (c != '9')
		errx(1, "read %c != %c", c, '9');

	c = '5';
	if (write(fd, &c, 1) != 1)
		err(1, "write");

	if (pread(fd, buf, 10, 0) != 10)
		err(1, "pread");

	iov[1].iov_base = &magic[1];
	iov[1].iov_len = 2;
	if ((ret = pwritev(fd, iov, 2, -1)) != -1)
		errx(1, "pwritev with negative offset succeeded,\
				returning %d", ret);
	if (errno != EINVAL)
		err(1, "pwritev with negative offset");

	if ((ret = pwritev(fd, iov, 2, LLONG_MAX)) != -1)
		errx(1, "pwritev with wrapping offset succeeded,\
				returning %d", ret);
	if (errno != EFBIG && errno != EINVAL)
		err(1, "pwritev with wrapping offset");

	if (memcmp(buf, "0000895800", 10) != 0)
		errx(1, "data mismatch: %s != %s", buf, "0000895800");

	/* pwrite should be unaffected by O_APPEND */
	if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_APPEND))
		err(1, "fcntl");
	if (pwritev(fd, iov, 2, 2) != 4)
		err(1, "pwritev");
	if (pread(fd, buf, 10, 0) != 10)
		err(1, "pread");
	if (memcmp(buf, "0089125800", 10) != 0)
		errx(1, "data mismatch: %s != %s", buf, "0089125800");

	close(fd);

	/* also, verify that pwritev fails on ttys */
	fd = open("/dev/tty", O_RDWR);
	if (fd < 0)
		printf("skipping tty test\n");
	else if ((ret = pwritev(fd, iov, 2, 7)) != -1)
		errx(1, "pwritev succeeded on tty, returning %d", ret);
	else if (errno != ESPIPE)
		err(1, "pwritev");

	return 0;
}
