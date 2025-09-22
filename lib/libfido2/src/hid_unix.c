/*
 * Copyright (c) 2020 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "fido.h"

#ifdef __NetBSD__
#define	ppoll	pollts
#endif

int
fido_hid_unix_open(const char *path)
{
	int fd;
	struct stat st;

	if ((fd = open(path, O_RDWR)) == -1) {
		if (errno != ENOENT && errno != ENXIO)
			fido_log_error(errno, "%s: open %s", __func__, path);
		return (-1);
	}

	if (fstat(fd, &st) == -1) {
		fido_log_error(errno, "%s: fstat %s", __func__, path);
		if (close(fd) == -1)
			fido_log_error(errno, "%s: close", __func__);
		return (-1);
	}

	if (S_ISCHR(st.st_mode) == 0) {
		fido_log_debug("%s: S_ISCHR %s", __func__, path);
		if (close(fd) == -1)
			fido_log_error(errno, "%s: close", __func__);
		return (-1);
	}

	return (fd);
}

int
fido_hid_unix_wait(int fd, int ms, const fido_sigset_t *sigmask)
{
	struct timespec ts;
	struct pollfd pfd;
	int r;

	memset(&pfd, 0, sizeof(pfd));
	pfd.events = POLLIN;
	pfd.fd = fd;

#ifdef FIDO_FUZZ
	return (0);
#endif
	if (ms > -1) {
		ts.tv_sec = ms / 1000;
		ts.tv_nsec = (ms % 1000) * 1000000;
	}

	if ((r = ppoll(&pfd, 1, ms > -1 ? &ts : NULL, sigmask)) < 1) {
		if (r == -1)
			fido_log_error(errno, "%s: ppoll", __func__);
		return (-1);
	}

	return (0);
}
