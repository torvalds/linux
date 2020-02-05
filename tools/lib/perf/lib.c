// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/kernel.h>
#include <internal/lib.h>

unsigned int page_size;

static ssize_t ion(bool is_read, int fd, void *buf, size_t n)
{
	void *buf_start = buf;
	size_t left = n;

	while (left) {
		/* buf must be treated as const if !is_read. */
		ssize_t ret = is_read ? read(fd, buf, left) :
					write(fd, buf, left);

		if (ret < 0 && errno == EINTR)
			continue;
		if (ret <= 0)
			return ret;

		left -= ret;
		buf  += ret;
	}

	BUG_ON((size_t)(buf - buf_start) != n);
	return n;
}

/*
 * Read exactly 'n' bytes or return an error.
 */
ssize_t readn(int fd, void *buf, size_t n)
{
	return ion(true, fd, buf, n);
}

/*
 * Write exactly 'n' bytes or return an error.
 */
ssize_t writen(int fd, const void *buf, size_t n)
{
	/* ion does not modify buf. */
	return ion(false, fd, (void *)buf, n);
}
