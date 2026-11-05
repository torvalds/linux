/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SELFTESTS_FILESYSTEMS_FREEZE_LOOP_UTIL_H
#define __SELFTESTS_FILESYSTEMS_FREEZE_LOOP_UTIL_H

#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* How often to retry when somebody else binds the free device first. */
#define LOOP_BIND_TRIES 64

/* Bind a free loop device to @fd; its number or -errno. */
static inline int loop_bind(int fd)
{
	for (int i = 0; i < LOOP_BIND_TRIES; i++) {
		char dev[32];
		int cfd, lfd, n, err;

		cfd = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
		if (cfd < 0)
			return -errno;
		n = ioctl(cfd, LOOP_CTL_GET_FREE);
		err = errno;
		close(cfd);
		if (n < 0)
			return -err;
		snprintf(dev, sizeof(dev), "/dev/loop%d", n);
		lfd = open(dev, O_RDWR | O_CLOEXEC);
		if (lfd < 0)
			return -errno;
		err = ioctl(lfd, LOOP_SET_FD, fd) ? errno : 0;
		close(lfd);
		if (!err)
			return n;
		if (err != EBUSY)
			return -err;
	}
	return -EBUSY;
}

/* Did loop_bind() fail because there are no loop device nodes or no driver? */
static inline bool loop_missing(int ret)
{
	return ret == -ENOENT || ret == -ENODEV;
}

static inline void loop_clear(int n)
{
	char dev[32];
	int fd;

	snprintf(dev, sizeof(dev), "/dev/loop%d", n);
	fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return;
	ioctl(fd, LOOP_CLR_FD, 0);
	close(fd);
}

#endif /* __SELFTESTS_FILESYSTEMS_FREEZE_LOOP_UTIL_H */
