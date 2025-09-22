/*	$OpenBSD: imsg.c,v 1.42 2025/06/16 13:56:11 claudio Exp $	*/

/*
 * Copyright (c) 2023 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imsg.h"

#define IMSG_ALLOW_FDPASS	0x01
#define IMSG_FD_MARK		0x80000000U

static struct ibuf	*imsg_parse_hdr(struct ibuf *, void *, int *);

int
imsgbuf_init(struct imsgbuf *imsgbuf, int fd)
{
	imsgbuf->w = msgbuf_new_reader(IMSG_HEADER_SIZE, imsg_parse_hdr,
	    imsgbuf);
	if (imsgbuf->w == NULL)
		return (-1);
	imsgbuf->pid = getpid();
	imsgbuf->maxsize = MAX_IMSGSIZE;
	imsgbuf->fd = fd;
	imsgbuf->flags = 0;
	return (0);
}

void
imsgbuf_allow_fdpass(struct imsgbuf *imsgbuf)
{
	imsgbuf->flags |= IMSG_ALLOW_FDPASS;
}

int
imsgbuf_set_maxsize(struct imsgbuf *imsgbuf, uint32_t max)
{
	if (max > UINT32_MAX - IMSG_HEADER_SIZE) {
		errno = ERANGE;
		return (-1);
	}
	max += IMSG_HEADER_SIZE;
	if (max & IMSG_FD_MARK) {
		errno = EINVAL;
		return (-1);
	}
	imsgbuf->maxsize = max;
	return (0);
}

int
imsgbuf_read(struct imsgbuf *imsgbuf)
{
	if (imsgbuf->flags & IMSG_ALLOW_FDPASS)
		return msgbuf_read(imsgbuf->fd, imsgbuf->w);
	else
		return ibuf_read(imsgbuf->fd, imsgbuf->w);
}

int
imsgbuf_write(struct imsgbuf *imsgbuf)
{
	if (imsgbuf->flags & IMSG_ALLOW_FDPASS)
		return msgbuf_write(imsgbuf->fd, imsgbuf->w);
	else
		return ibuf_write(imsgbuf->fd, imsgbuf->w);
}

int
imsgbuf_flush(struct imsgbuf *imsgbuf)
{
	while (imsgbuf_queuelen(imsgbuf) > 0) {
		if (imsgbuf_write(imsgbuf) == -1)
			return (-1);
	}
	return (0);
}

void
imsgbuf_clear(struct imsgbuf *imsgbuf)
{
	msgbuf_free(imsgbuf->w);
	imsgbuf->w = NULL;
}

uint32_t
imsgbuf_queuelen(struct imsgbuf *imsgbuf)
{
	return msgbuf_queuelen(imsgbuf->w);
}

int
imsgbuf_get(struct imsgbuf *imsgbuf, struct imsg *imsg)
{
	struct imsg	 m;
	struct ibuf	*buf;

	if ((buf = msgbuf_get(imsgbuf->w)) == NULL)
		return (0);

	if (ibuf_get(buf, &m.hdr, sizeof(m.hdr)) == -1)
		return (-1);

	if (ibuf_size(buf))
		m.data = ibuf_data(buf);
	else
		m.data = NULL;
	m.buf = buf;
	m.hdr.len &= ~IMSG_FD_MARK;

	*imsg = m;
	return (1);
}

ssize_t
imsg_get(struct imsgbuf *imsgbuf, struct imsg *imsg)
{
	int rv;

	if ((rv = imsgbuf_get(imsgbuf, imsg)) != 1)
		return rv;
	return (imsg_get_len(imsg) + IMSG_HEADER_SIZE);
}

int
imsg_ibufq_pop(struct ibufqueue *bufq, struct imsg *imsg)
{
	struct imsg	 m;
	struct ibuf	*buf;

	if ((buf = ibufq_pop(bufq)) == NULL)
		return (0);

	if (ibuf_get(buf, &m.hdr, sizeof(m.hdr)) == -1)
		return (-1);

	if (ibuf_size(buf))
		m.data = ibuf_data(buf);
	else
		m.data = NULL;
	m.buf = buf;
	m.hdr.len &= ~IMSG_FD_MARK;

	*imsg = m;
	return (1);
}

void
imsg_ibufq_push(struct ibufqueue *bufq, struct imsg *imsg)
{
	ibuf_rewind(imsg->buf);
	ibufq_push(bufq, imsg->buf);
	memset(imsg, 0, sizeof(*imsg));
}

int
imsg_get_ibuf(struct imsg *imsg, struct ibuf *ibuf)
{
	if (ibuf_size(imsg->buf) == 0) {
		errno = EBADMSG;
		return (-1);
	}
	return ibuf_get_ibuf(imsg->buf, ibuf_size(imsg->buf), ibuf);
}

int
imsg_get_data(struct imsg *imsg, void *data, size_t len)
{
	if (len == 0) {
		errno = EINVAL;
		return (-1);
	}
	if (ibuf_size(imsg->buf) != len) {
		errno = EBADMSG;
		return (-1);
	}
	return ibuf_get(imsg->buf, data, len);
}

int
imsg_get_buf(struct imsg *imsg, void *data, size_t len)
{
	return ibuf_get(imsg->buf, data, len);
}

int
imsg_get_strbuf(struct imsg *imsg, char *str, size_t len)
{
	return ibuf_get_strbuf(imsg->buf, str, len);
}

int
imsg_get_fd(struct imsg *imsg)
{
	return ibuf_fd_get(imsg->buf);
}

uint32_t
imsg_get_id(struct imsg *imsg)
{
	return (imsg->hdr.peerid);
}

size_t
imsg_get_len(struct imsg *imsg)
{
	return ibuf_size(imsg->buf);
}

pid_t
imsg_get_pid(struct imsg *imsg)
{
	return (imsg->hdr.pid);
}

uint32_t
imsg_get_type(struct imsg *imsg)
{
	return (imsg->hdr.type);
}

int
imsg_compose(struct imsgbuf *imsgbuf, uint32_t type, uint32_t id, pid_t pid,
    int fd, const void *data, size_t datalen)
{
	struct ibuf	*wbuf;

	if ((wbuf = imsg_create(imsgbuf, type, id, pid, datalen)) == NULL)
		goto fail;

	if (ibuf_add(wbuf, data, datalen) == -1)
		goto fail;

	ibuf_fd_set(wbuf, fd);
	imsg_close(imsgbuf, wbuf);

	return (1);

 fail:
	ibuf_free(wbuf);
	return (-1);
}

int
imsg_composev(struct imsgbuf *imsgbuf, uint32_t type, uint32_t id, pid_t pid,
    int fd, const struct iovec *iov, int iovcnt)
{
	struct ibuf	*wbuf;
	int		 i;
	size_t		 datalen = 0;

	for (i = 0; i < iovcnt; i++)
		datalen += iov[i].iov_len;

	if ((wbuf = imsg_create(imsgbuf, type, id, pid, datalen)) == NULL)
		goto fail;

	for (i = 0; i < iovcnt; i++)
		if (ibuf_add(wbuf, iov[i].iov_base, iov[i].iov_len) == -1)
			goto fail;

	ibuf_fd_set(wbuf, fd);
	imsg_close(imsgbuf, wbuf);

	return (1);

 fail:
	ibuf_free(wbuf);
	return (-1);
}

/*
 * Enqueue imsg with payload from ibuf buf. fd passing is not possible 
 * with this function.
 */
int
imsg_compose_ibuf(struct imsgbuf *imsgbuf, uint32_t type, uint32_t id,
    pid_t pid, struct ibuf *buf)
{
	struct ibuf	*hdrbuf = NULL;
	struct imsg_hdr	 hdr;

	if (ibuf_size(buf) + IMSG_HEADER_SIZE > imsgbuf->maxsize) {
		errno = ERANGE;
		goto fail;
	}

	hdr.type = type;
	hdr.len = ibuf_size(buf) + IMSG_HEADER_SIZE;
	hdr.peerid = id;
	if ((hdr.pid = pid) == 0)
		hdr.pid = imsgbuf->pid;

	if ((hdrbuf = ibuf_open(IMSG_HEADER_SIZE)) == NULL)
		goto fail;
	if (ibuf_add(hdrbuf, &hdr, sizeof(hdr)) == -1)
		goto fail;

	ibuf_close(imsgbuf->w, hdrbuf);
	ibuf_close(imsgbuf->w, buf);
	return (1);

 fail:
	ibuf_free(buf);
	ibuf_free(hdrbuf);
	return (-1);
}

/*
 * Forward imsg to another channel. Any attached fd is closed.
 */
int
imsg_forward(struct imsgbuf *imsgbuf, struct imsg *msg)
{
	struct ibuf	*wbuf;
	size_t		 len;

	ibuf_rewind(msg->buf);
	ibuf_skip(msg->buf, sizeof(msg->hdr));
	len = ibuf_size(msg->buf);

	if ((wbuf = imsg_create(imsgbuf, msg->hdr.type, msg->hdr.peerid,
	    msg->hdr.pid, len)) == NULL)
		return (-1);

	if (len != 0) {
		if (ibuf_add_ibuf(wbuf, msg->buf) == -1) {
			ibuf_free(wbuf);
			return (-1);
		}
	}

	imsg_close(imsgbuf, wbuf);
	return (1);
}

struct ibuf *
imsg_create(struct imsgbuf *imsgbuf, uint32_t type, uint32_t id, pid_t pid,
    size_t datalen)
{
	struct ibuf	*wbuf;
	struct imsg_hdr	 hdr;

	datalen += IMSG_HEADER_SIZE;
	if (datalen > imsgbuf->maxsize) {
		errno = ERANGE;
		return (NULL);
	}

	hdr.len = 0;
	hdr.type = type;
	hdr.peerid = id;
	if ((hdr.pid = pid) == 0)
		hdr.pid = imsgbuf->pid;
	if ((wbuf = ibuf_dynamic(datalen, imsgbuf->maxsize)) == NULL)
		goto fail;
	if (ibuf_add(wbuf, &hdr, sizeof(hdr)) == -1)
		goto fail;

	return (wbuf);

 fail:
	ibuf_free(wbuf);
	return (NULL);
}

int
imsg_add(struct ibuf *msg, const void *data, size_t datalen)
{
	if (datalen)
		if (ibuf_add(msg, data, datalen) == -1) {
			ibuf_free(msg);
			return (-1);
		}
	return (datalen);
}

void
imsg_close(struct imsgbuf *imsgbuf, struct ibuf *msg)
{
	uint32_t len;

	len = ibuf_size(msg);
	if (ibuf_fd_avail(msg))
		len |= IMSG_FD_MARK;
	(void)ibuf_set_h32(msg, offsetof(struct imsg_hdr, len), len);
	ibuf_close(imsgbuf->w, msg);
}

void
imsg_free(struct imsg *imsg)
{
	ibuf_free(imsg->buf);
}

int
imsg_set_maxsize(struct ibuf *msg, size_t max)
{
	if (max > UINT32_MAX - IMSG_HEADER_SIZE) {
		errno = ERANGE;
		return (-1);
	}
	return ibuf_set_maxsize(msg, max + IMSG_HEADER_SIZE);
}

static struct ibuf *
imsg_parse_hdr(struct ibuf *buf, void *arg, int *fd)
{
	struct imsgbuf *imsgbuf = arg;
	struct imsg_hdr hdr;
	struct ibuf *b;
	uint32_t len;

	if (ibuf_get(buf, &hdr, sizeof(hdr)) == -1)
		return (NULL);

	len = hdr.len & ~IMSG_FD_MARK;

	if (len < IMSG_HEADER_SIZE || len > imsgbuf->maxsize) {
		errno = ERANGE;
		return (NULL);
	}
	if ((b = ibuf_open(len)) == NULL)
		return (NULL);
	if (hdr.len & IMSG_FD_MARK) {
		ibuf_fd_set(b, *fd);
		*fd = -1;
	}

	return b;
}
