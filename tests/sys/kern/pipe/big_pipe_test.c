#include <sys/select.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BIG_PIPE_SIZE  64*1024 /* From sys/pipe.h */

/*
 * Test for the non-blocking big pipe bug (write(2) returning
 * EAGAIN while select(2) returns the descriptor as ready for write).
 *
 * $FreeBSD$
 */

static void
write_frame(int fd, char *buf, unsigned long buflen)
{
	fd_set wfd;
	int i;

	while (buflen) {
		FD_ZERO(&wfd);
		FD_SET(fd, &wfd);
		i = select(fd+1, NULL, &wfd, NULL, NULL);
		if (i < 0)
			err(1, "select failed");
		if (i != 1) {
			errx(1, "select returned unexpected value %d\n", i);
			exit(1);
		}
		i = write(fd, buf, buflen);
		if (i < 0) {
			if (errno != EAGAIN)
				warn("write failed");
			exit(1);
		}
		buf += i;
		buflen -= i;
	}
}

int
main(void)
{
	/* any value over PIPE_SIZE should do */
	char buf[BIG_PIPE_SIZE];
	int i, flags, fd[2];

	if (pipe(fd) < 0)
		errx(1, "pipe failed");

	flags = fcntl(fd[1], F_GETFL);
	if (flags == -1 || fcntl(fd[1], F_SETFL, flags|O_NONBLOCK) == -1) {
		printf("fcntl failed: %s\n", strerror(errno));
		exit(1);
	}

	switch (fork()) {
	case -1:
		err(1, "fork failed: %s\n", strerror(errno));
		break;
	case 0:
		close(fd[1]);
		for (;;) {
			/* Any small size should do */
			i = read(fd[0], buf, 256);
			if (i == 0)
				break;
			if (i < 0)
				err(1, "read");
		}
		exit(0);
	default:
		break;
	}

	close(fd[0]);
	memset(buf, 0, sizeof buf);
	for (i = 0; i < 1000; i++)
		write_frame(fd[1], buf, sizeof buf);

	printf("ok\n");
	exit(0);
}
