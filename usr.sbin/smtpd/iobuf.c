/*	$OpenBSD: iobuf.c,v 1.16 2021/06/14 17:58:15 eric Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <sys/uio.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IO_TLS
#include <tls.h>
#endif
#include <unistd.h>

#include "iobuf.h"

#define IOBUF_MAX	65536
#define IOBUFQ_MIN	4096

struct ioqbuf	*ioqbuf_alloc(struct iobuf *, size_t);
void		 iobuf_drain(struct iobuf *, size_t);

int
iobuf_init(struct iobuf *io, size_t size, size_t max)
{
	memset(io, 0, sizeof *io);

	if (max == 0)
		max = IOBUF_MAX;

	if (size == 0)
		size = max;

	if (size > max)
		return (-1);

	if ((io->buf = calloc(size, 1)) == NULL)
		return (-1);

	io->size = size;
	io->max = max;

	return (0);
}

void
iobuf_clear(struct iobuf *io)
{
	struct ioqbuf	*q;

	free(io->buf);

	while ((q = io->outq)) {
		io->outq = q->next;
		free(q);
	}

	memset(io, 0, sizeof (*io));
}

void
iobuf_drain(struct iobuf *io, size_t n)
{
	struct	ioqbuf	*q;
	size_t		 left = n;

	while ((q = io->outq) && left) {
		if ((q->wpos - q->rpos) > left) {
			q->rpos += left;
			left = 0;
		} else {
			left -= q->wpos - q->rpos;
			io->outq = q->next;
			free(q);
		}
	}

	io->queued -= (n - left);
	if (io->outq == NULL)
		io->outqlast = NULL;
}

int
iobuf_extend(struct iobuf *io, size_t n)
{
	char	*t;

	if (n > io->max)
		return (-1);

	if (io->max - io->size < n)
		return (-1);

	t = recallocarray(io->buf, io->size, io->size + n, 1);
	if (t == NULL)
		return (-1);

	io->size += n;
	io->buf = t;

	return (0);
}

size_t
iobuf_left(struct iobuf *io)
{
	return io->size - io->wpos;
}

size_t
iobuf_space(struct iobuf *io)
{
	return io->size - (io->wpos - io->rpos);
}

size_t
iobuf_len(struct iobuf *io)
{
	return io->wpos - io->rpos;
}

char *
iobuf_data(struct iobuf *io)
{
	return io->buf + io->rpos;
}

void
iobuf_drop(struct iobuf *io, size_t n)
{
	if (n >= iobuf_len(io)) {
		io->rpos = io->wpos = 0;
		return;
	}

	io->rpos += n;
}

char *
iobuf_getline(struct iobuf *iobuf, size_t *rlen)
{
	char	*buf;
	size_t	 len, i;

	buf = iobuf_data(iobuf);
	len = iobuf_len(iobuf);

	for (i = 0; i + 1 <= len; i++)
		if (buf[i] == '\n') {
			/* Note: the returned address points into the iobuf
			 * buffer.  We NUL-end it for convenience, and discard
			 * the data from the iobuf, so that the caller doesn't
			 * have to do it.  The data remains "valid" as long
			 * as the iobuf does not overwrite it, that is until
			 * the next call to iobuf_normalize() or iobuf_extend().
			 */
			iobuf_drop(iobuf, i + 1);
			buf[i] = '\0';
			if (rlen)
				*rlen = i;
			return (buf);
		}

	return (NULL);
}

void
iobuf_normalize(struct iobuf *io)
{
	if (io->rpos == 0)
		return;

	if (io->rpos == io->wpos) {
		io->rpos = io->wpos = 0;
		return;
	}

	memmove(io->buf, io->buf + io->rpos, io->wpos - io->rpos);
	io->wpos -= io->rpos;
	io->rpos = 0;
}

ssize_t
iobuf_read(struct iobuf *io, int fd)
{
	ssize_t	n;

	n = read(fd, io->buf + io->wpos, iobuf_left(io));
	if (n == -1) {
		/* XXX is this really what we want? */
		if (errno == EAGAIN || errno == EINTR)
			return (IOBUF_WANT_READ);
		return (IOBUF_ERROR);
	}
	if (n == 0)
		return (IOBUF_CLOSED);

	io->wpos += n;

	return (n);
}

struct ioqbuf *
ioqbuf_alloc(struct iobuf *io, size_t len)
{
	struct ioqbuf   *q;

	if (len < IOBUFQ_MIN)
		len = IOBUFQ_MIN;

	if ((q = malloc(sizeof(*q) + len)) == NULL)
		return (NULL);

	q->rpos = 0;
	q->wpos = 0;
	q->size = len;
	q->next = NULL;
	q->buf = (char *)(q) + sizeof(*q);

	if (io->outqlast == NULL)
		io->outq = q;
	else
		io->outqlast->next = q;
	io->outqlast = q;

	return (q);
}

size_t
iobuf_queued(struct iobuf *io)
{
	return io->queued;
}

void *
iobuf_reserve(struct iobuf *io, size_t len)
{
	struct ioqbuf	*q;
	void		*r;

	if (len == 0)
		return (NULL);

	if (((q = io->outqlast) == NULL) || q->size - q->wpos <= len) {
		if ((q = ioqbuf_alloc(io, len)) == NULL)
			return (NULL);
	}

	r = q->buf + q->wpos;
	q->wpos += len;
	io->queued += len;

	return (r);
}

int
iobuf_queue(struct iobuf *io, const void *data, size_t len)
{
	void	*buf;

	if (len == 0)
		return (0);

	if ((buf = iobuf_reserve(io, len)) == NULL)
		return (-1);

	memmove(buf, data, len);

	return (len);
}

int
iobuf_queuev(struct iobuf *io, const struct iovec *iov, int iovcnt)
{
	int	 i;
	size_t	 len = 0;
	char	*buf;

	for (i = 0; i < iovcnt; i++)
		len += iov[i].iov_len;

	if ((buf = iobuf_reserve(io, len)) == NULL)
		return (-1);

	for (i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0)
			continue;
		memmove(buf, iov[i].iov_base, iov[i].iov_len);
		buf += iov[i].iov_len;
	}

	return (0);

}

int
iobuf_fqueue(struct iobuf *io, const char *fmt, ...)
{
	va_list	ap;
	int	len;

	va_start(ap, fmt);
	len = iobuf_vfqueue(io, fmt, ap);
	va_end(ap);

	return (len);
}

int
iobuf_vfqueue(struct iobuf *io, const char *fmt, va_list ap)
{
	char	*buf;
	int	 len;

	len = vasprintf(&buf, fmt, ap);

	if (len == -1)
		return (-1);

	len = iobuf_queue(io, buf, len);
	free(buf);

	return (len);
}

ssize_t
iobuf_write(struct iobuf *io, int fd)
{
	struct iovec	 iov[IOV_MAX];
	struct ioqbuf	*q;
	int		 i;
	ssize_t		 n;

	i = 0;
	for (q = io->outq; q ; q = q->next) {
		if (i >= IOV_MAX)
			break;
		iov[i].iov_base = q->buf + q->rpos;
		iov[i].iov_len = q->wpos - q->rpos;
		i++;
	}

	n = writev(fd, iov, i);
	if (n == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return (IOBUF_WANT_WRITE);
		if (errno == EPIPE)
			return (IOBUF_CLOSED);
		return (IOBUF_ERROR);
	}

	iobuf_drain(io, n);

	return (n);
}

int
iobuf_flush(struct iobuf *io, int fd)
{
	ssize_t	s;

	while (io->queued)
		if ((s = iobuf_write(io, fd)) < 0)
			return (s);

	return (0);
}

#ifdef IO_TLS

int
iobuf_flush_tls(struct iobuf *io, struct tls *tls)
{
	ssize_t	s;

	while (io->queued)
		if ((s = iobuf_write_tls(io, tls)) < 0)
			return (s);

	return (0);
}

ssize_t
iobuf_write_tls(struct iobuf *io, struct tls *tls)
{
	struct ioqbuf	*q;
	ssize_t		 n;

	q = io->outq;

	n = tls_write(tls, q->buf + q->rpos, q->wpos - q->rpos);
	if (n == TLS_WANT_POLLIN)
		return (IOBUF_WANT_READ);
	else if (n == TLS_WANT_POLLOUT)
		return (IOBUF_WANT_WRITE);
	else if (n == 0)
		return (IOBUF_CLOSED);
	else if (n == -1)
		return (IOBUF_ERROR);

	iobuf_drain(io, n);

	return (n);
}

ssize_t
iobuf_read_tls(struct iobuf *io, struct tls *tls)
{
	ssize_t	n;

	n = tls_read(tls, io->buf + io->wpos, iobuf_left(io));
	if (n == TLS_WANT_POLLIN)
		return (IOBUF_WANT_READ);
	else if (n == TLS_WANT_POLLOUT)
		return (IOBUF_WANT_WRITE);
	else if (n == 0)
		return (IOBUF_CLOSED);
	else if (n == -1)
		return (IOBUF_ERROR);

	io->wpos += n;

	return (n);
}

#endif /* IO_TLS */
