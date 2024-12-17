// SPDX-License-Identifier: GPL-2.0
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>

int read_with_timeout(int fd, char *buf, size_t count, long usec)
{
	const long M = 1000 * 1000;
	struct timeval tv = { usec / M, usec % M };
	fd_set fds;
	int err;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	err = select(fd + 1, &fds, NULL, NULL, &tv);
	if (err < 0)
		return err;
	if (FD_ISSET(fd, &fds))
		return read(fd, buf, count);
	return -EAGAIN;
}
