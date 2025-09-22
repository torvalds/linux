/*	$OpenBSD: pty.c,v 1.1 2024/06/03 08:02:22 anton Exp $	*/

#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "pty.h"

int
pty_open(struct pty *pty)
{
	int master, slave;

	master = posix_openpt(O_RDWR);
	if (master == -1) {
		warn("posix_openpt");
		return 1;
	}
	if (grantpt(master) == -1) {
		warn("grantpt");
		return 1;
	}
	if (unlockpt(master) == -1) {
		warn("unlockpt");
		return 1;
	}

	slave = open(ptsname(master), O_RDWR);
	if (slave == -1) {
		warn("%s", ptsname(master));
		return 1;
	}

	pty->master = master;
	pty->slave = slave;
	return 0;
}

void
pty_close(struct pty *pty)
{
	close(pty->slave);
	close(pty->master);
}

/*
 * Disconnect the controlling tty, if present.
 */
int
pty_detach(struct pty *pty)
{
	int fd;

	fd = open("/dev/tty", O_RDWR | O_NOCTTY);
	if (fd >= 0) {
		(void)ioctl(fd, TIOCNOTTY, NULL);
		close(fd);
	}
	return 0;
}

/*
 * Connect the slave as the controlling tty.
 */
int
pty_attach(struct pty *pty)
{
	if (ioctl(pty->slave, TIOCSCTTY, NULL) == -1) {
		warn("TIOCSCTTY");
		return 1;
	}
	return 0;
}

int
pty_drain(struct pty *pty)
{
	for (;;) {
		char *buf = &pty->buf.storage[pty->buf.len];
		size_t bufsize = sizeof(pty->buf.storage) - pty->buf.len;
		ssize_t n;

		n = read(pty->master, buf, bufsize);
		if (n == -1) {
			warn("read");
			return 1;
		}
		if (n == 0)
			break;

		/* Ensure space for NUL-terminator. */
		if ((size_t)n >= bufsize) {
			warnx("pty buffer too small");
			return 1;
		}
		pty->buf.len += n;
	}

	return 0;
}
