/*	$OpenBSD: mio_rmidi.c,v 1.28 2019/06/29 06:05:26 ratchov Exp $	*/
/*
 * Copyright (c) 2008 Alexandre Ratchov <alex@caoua.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "mio_priv.h"

#define DEVPATH_PREFIX	"/dev/rmidi"
#define DEVPATH_MAX 	(1 +		\
	sizeof(DEVPATH_PREFIX) - 1 +	\
	sizeof(int) * 3)

struct mio_rmidi_hdl {
	struct mio_hdl mio;
	int fd;
};

static void mio_rmidi_close(struct mio_hdl *);
static size_t mio_rmidi_read(struct mio_hdl *, void *, size_t);
static size_t mio_rmidi_write(struct mio_hdl *, const void *, size_t);
static int mio_rmidi_nfds(struct mio_hdl *);
static int mio_rmidi_pollfd(struct mio_hdl *, struct pollfd *, int);
static int mio_rmidi_revents(struct mio_hdl *, struct pollfd *);

static struct mio_ops mio_rmidi_ops = {
	mio_rmidi_close,
	mio_rmidi_write,
	mio_rmidi_read,
	mio_rmidi_nfds,
	mio_rmidi_pollfd,
	mio_rmidi_revents
};

int
mio_rmidi_getfd(const char *str, unsigned int mode, int nbio)
{
	const char *p;
	char path[DEVPATH_MAX];
	unsigned int devnum;
	int fd, flags;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	p = _sndio_parsetype(str, "rmidi");
	if (p == NULL) {
		DPRINTF("mio_rmidi_getfd: %s: \"rmidi\" expected\n", str);
		return -1;
	}
	switch (*p) {
	case '/':
		p++;
		break;
	default:
		DPRINTF("mio_rmidi_getfd: %s: '/' expected\n", str);
		return -1;
	}
	p = _sndio_parsenum(p, &devnum, 255);
	if (p == NULL) {
		DPRINTF("mio_rmidi_getfd: %s: number expected after '/'\n", str);
		return -1;
	}
	if (*p != '\0') {
		DPRINTF("mio_rmidi_getfd: junk at end of string: %s\n", p);
		return -1;
	}
	snprintf(path, sizeof(path), DEVPATH_PREFIX "%u", devnum);
	if (mode == (MIO_IN | MIO_OUT))
		flags = O_RDWR;
	else
		flags = (mode & MIO_OUT) ? O_WRONLY : O_RDONLY;
	while ((fd = open(path, flags | O_NONBLOCK | O_CLOEXEC)) == -1) {
		if (errno == EINTR)
			continue;
		DPERROR(path);
		return -1;
	}
	return fd;
}

struct mio_hdl *
mio_rmidi_fdopen(int fd, unsigned int mode, int nbio)
{
	struct mio_rmidi_hdl *hdl;

#ifdef DEBUG
	_sndio_debug_init();
#endif
	hdl = malloc(sizeof(struct mio_rmidi_hdl));
	if (hdl == NULL)
		return NULL;
	_mio_create(&hdl->mio, &mio_rmidi_ops, mode, nbio);
	hdl->fd = fd;
	return (struct mio_hdl *)hdl;
}

struct mio_hdl *
_mio_rmidi_open(const char *str, unsigned int mode, int nbio)
{
	struct mio_hdl *hdl;
	int fd;

	fd = mio_rmidi_getfd(str, mode, nbio);
	if (fd == -1)
		return NULL;
	hdl = mio_rmidi_fdopen(fd, mode, nbio);
	if (hdl != NULL)
		return hdl;
	while (close(fd) == -1 && errno == EINTR)
		; /* retry */
	return NULL;
}

static void
mio_rmidi_close(struct mio_hdl *sh)
{
	struct mio_rmidi_hdl *hdl = (struct mio_rmidi_hdl *)sh;
	int rc;

	do {
		rc = close(hdl->fd);
	} while (rc == -1 && errno == EINTR);
	free(hdl);
}

static size_t
mio_rmidi_read(struct mio_hdl *sh, void *buf, size_t len)
{
	struct mio_rmidi_hdl *hdl = (struct mio_rmidi_hdl *)sh;
	ssize_t n;

	while ((n = read(hdl->fd, buf, len)) == -1) {
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN) {
			DPERROR("mio_rmidi_read: read");
			hdl->mio.eof = 1;
		}
		return 0;
	}
	if (n == 0) {
		DPRINTF("mio_rmidi_read: eof\n");
		hdl->mio.eof = 1;
		return 0;
	}
	return n;
}

static size_t
mio_rmidi_write(struct mio_hdl *sh, const void *buf, size_t len)
{
	struct mio_rmidi_hdl *hdl = (struct mio_rmidi_hdl *)sh;
	ssize_t n;

	while ((n = write(hdl->fd, buf, len)) == -1) {
		if (errno == EINTR)
			continue;
		if (errno != EAGAIN) {
			DPERROR("mio_rmidi_write: write");
			hdl->mio.eof = 1;
		}
		return 0;
	}
	return n;
}

static int
mio_rmidi_nfds(struct mio_hdl *sh)
{
	return 1;
}

static int
mio_rmidi_pollfd(struct mio_hdl *sh, struct pollfd *pfd, int events)
{
	struct mio_rmidi_hdl *hdl = (struct mio_rmidi_hdl *)sh;

	pfd->fd = hdl->fd;
	pfd->events = events;
	return 1;
}

static int
mio_rmidi_revents(struct mio_hdl *sh, struct pollfd *pfd)
{
	return pfd->revents;
}
