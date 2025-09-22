/*	$OpenBSD: imsg-buffer.c,v 1.36 2025/08/25 08:29:49 claudio Exp $	*/

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

#include <limits.h>
#include <errno.h>
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imsg.h"

struct ibufqueue {
	TAILQ_HEAD(, ibuf)	bufs;
	uint32_t		queued;
};

struct msgbuf {
	struct ibufqueue	 bufs;
	struct ibufqueue	 rbufs;
	char			*rbuf;
	struct ibuf		*rpmsg;
	struct ibuf		*(*readhdr)(struct ibuf *, void *, int *);
	void			*rarg;
	size_t			 roff;
	size_t			 hdrsize;
};

static void	msgbuf_drain(struct msgbuf *, size_t);
static void	ibufq_init(struct ibufqueue *);

#define	IBUF_FD_MARK_ON_STACK	-2

struct ibuf *
ibuf_open(size_t len)
{
	struct ibuf	*buf;

	if ((buf = calloc(1, sizeof(struct ibuf))) == NULL)
		return (NULL);
	if (len > 0) {
		if ((buf->buf = calloc(len, 1)) == NULL) {
			free(buf);
			return (NULL);
		}
	}
	buf->size = buf->max = len;
	buf->fd = -1;

	return (buf);
}

struct ibuf *
ibuf_dynamic(size_t len, size_t max)
{
	struct ibuf	*buf;

	if (max == 0 || max < len) {
		errno = EINVAL;
		return (NULL);
	}

	if ((buf = calloc(1, sizeof(struct ibuf))) == NULL)
		return (NULL);
	if (len > 0) {
		if ((buf->buf = calloc(len, 1)) == NULL) {
			free(buf);
			return (NULL);
		}
	}
	buf->size = len;
	buf->max = max;
	buf->fd = -1;

	return (buf);
}

void *
ibuf_reserve(struct ibuf *buf, size_t len)
{
	void	*b;

	if (len > SIZE_MAX - buf->wpos) {
		errno = ERANGE;
		return (NULL);
	}
	if (buf->fd == IBUF_FD_MARK_ON_STACK) {
		/* can not grow stack buffers */
		errno = EINVAL;
		return (NULL);
	}

	if (buf->wpos + len > buf->size) {
		unsigned char	*nb;

		/* check if buffer is allowed to grow */
		if (buf->wpos + len > buf->max) {
			errno = ERANGE;
			return (NULL);
		}
		nb = realloc(buf->buf, buf->wpos + len);
		if (nb == NULL)
			return (NULL);
		memset(nb + buf->size, 0, buf->wpos + len - buf->size);
		buf->buf = nb;
		buf->size = buf->wpos + len;
	}

	b = buf->buf + buf->wpos;
	buf->wpos += len;
	return (b);
}

int
ibuf_add(struct ibuf *buf, const void *data, size_t len)
{
	void *b;

	if (len == 0)
		return (0);

	if ((b = ibuf_reserve(buf, len)) == NULL)
		return (-1);

	memcpy(b, data, len);
	return (0);
}

int
ibuf_add_ibuf(struct ibuf *buf, const struct ibuf *from)
{
	return ibuf_add(buf, ibuf_data(from), ibuf_size(from));
}

int
ibuf_add_n8(struct ibuf *buf, uint64_t value)
{
	uint8_t v;

	if (value > UINT8_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = value;
	return ibuf_add(buf, &v, sizeof(v));
}

int
ibuf_add_n16(struct ibuf *buf, uint64_t value)
{
	uint16_t v;

	if (value > UINT16_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = htobe16(value);
	return ibuf_add(buf, &v, sizeof(v));
}

int
ibuf_add_n32(struct ibuf *buf, uint64_t value)
{
	uint32_t v;

	if (value > UINT32_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = htobe32(value);
	return ibuf_add(buf, &v, sizeof(v));
}

int
ibuf_add_n64(struct ibuf *buf, uint64_t value)
{
	value = htobe64(value);
	return ibuf_add(buf, &value, sizeof(value));
}

int
ibuf_add_h16(struct ibuf *buf, uint64_t value)
{
	uint16_t v;

	if (value > UINT16_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = value;
	return ibuf_add(buf, &v, sizeof(v));
}

int
ibuf_add_h32(struct ibuf *buf, uint64_t value)
{
	uint32_t v;

	if (value > UINT32_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = value;
	return ibuf_add(buf, &v, sizeof(v));
}

int
ibuf_add_h64(struct ibuf *buf, uint64_t value)
{
	return ibuf_add(buf, &value, sizeof(value));
}

int
ibuf_add_zero(struct ibuf *buf, size_t len)
{
	void *b;

	if (len == 0)
		return (0);

	if ((b = ibuf_reserve(buf, len)) == NULL)
		return (-1);
	memset(b, 0, len);
	return (0);
}

int
ibuf_add_strbuf(struct ibuf *buf, const char *str, size_t len)
{
	char *b;
	size_t n;

	if ((b = ibuf_reserve(buf, len)) == NULL)
		return (-1);

	n = strlcpy(b, str, len);
	if (n >= len) {
		/* also covers the case where len == 0 */
		errno = EOVERFLOW;
		return (-1);
	}
	memset(b + n, 0, len - n);
	return (0);
}

void *
ibuf_seek(struct ibuf *buf, size_t pos, size_t len)
{
	/* only allow seeking between rpos and wpos */
	if (ibuf_size(buf) < pos || SIZE_MAX - pos < len ||
	    ibuf_size(buf) < pos + len) {
		errno = ERANGE;
		return (NULL);
	}

	return (buf->buf + buf->rpos + pos);
}

int
ibuf_set(struct ibuf *buf, size_t pos, const void *data, size_t len)
{
	void *b;

	if ((b = ibuf_seek(buf, pos, len)) == NULL)
		return (-1);

	if (len == 0)
		return (0);
	memcpy(b, data, len);
	return (0);
}

int
ibuf_set_n8(struct ibuf *buf, size_t pos, uint64_t value)
{
	uint8_t v;

	if (value > UINT8_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = value;
	return (ibuf_set(buf, pos, &v, sizeof(v)));
}

int
ibuf_set_n16(struct ibuf *buf, size_t pos, uint64_t value)
{
	uint16_t v;

	if (value > UINT16_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = htobe16(value);
	return (ibuf_set(buf, pos, &v, sizeof(v)));
}

int
ibuf_set_n32(struct ibuf *buf, size_t pos, uint64_t value)
{
	uint32_t v;

	if (value > UINT32_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = htobe32(value);
	return (ibuf_set(buf, pos, &v, sizeof(v)));
}

int
ibuf_set_n64(struct ibuf *buf, size_t pos, uint64_t value)
{
	value = htobe64(value);
	return (ibuf_set(buf, pos, &value, sizeof(value)));
}

int
ibuf_set_h16(struct ibuf *buf, size_t pos, uint64_t value)
{
	uint16_t v;

	if (value > UINT16_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = value;
	return (ibuf_set(buf, pos, &v, sizeof(v)));
}

int
ibuf_set_h32(struct ibuf *buf, size_t pos, uint64_t value)
{
	uint32_t v;

	if (value > UINT32_MAX) {
		errno = EINVAL;
		return (-1);
	}
	v = value;
	return (ibuf_set(buf, pos, &v, sizeof(v)));
}

int
ibuf_set_h64(struct ibuf *buf, size_t pos, uint64_t value)
{
	return (ibuf_set(buf, pos, &value, sizeof(value)));
}

int
ibuf_set_maxsize(struct ibuf *buf, size_t max)
{
	if (buf->fd == IBUF_FD_MARK_ON_STACK) {
		/* can't fiddle with stack buffers */
		errno = EINVAL;
		return (-1);
	}
	if (max > buf->max) {
		errno = ERANGE;
		return (-1);
	}
	buf->max = max;
	return (0);
}

void *
ibuf_data(const struct ibuf *buf)
{
	return (buf->buf + buf->rpos);
}

size_t
ibuf_size(const struct ibuf *buf)
{
	return (buf->wpos - buf->rpos);
}

size_t
ibuf_left(const struct ibuf *buf)
{
	/* on stack buffers have no space left */
	if (buf->fd == IBUF_FD_MARK_ON_STACK)
		return (0);
	return (buf->max - buf->wpos);
}

int
ibuf_truncate(struct ibuf *buf, size_t len)
{
	if (ibuf_size(buf) >= len) {
		buf->wpos = buf->rpos + len;
		return (0);
	}
	if (buf->fd == IBUF_FD_MARK_ON_STACK) {
		/* only allow to truncate down for stack buffers */
		errno = ERANGE;
		return (-1);
	}
	return ibuf_add_zero(buf, len - ibuf_size(buf));
}

void
ibuf_rewind(struct ibuf *buf)
{
	buf->rpos = 0;
}

void
ibuf_close(struct msgbuf *msgbuf, struct ibuf *buf)
{
	ibufq_push(&msgbuf->bufs, buf);
}

void
ibuf_from_buffer(struct ibuf *buf, void *data, size_t len)
{
	memset(buf, 0, sizeof(*buf));
	buf->buf = data;
	buf->size = buf->wpos = len;
	buf->fd = IBUF_FD_MARK_ON_STACK;
}

void
ibuf_from_ibuf(struct ibuf *buf, const struct ibuf *from)
{
	ibuf_from_buffer(buf, ibuf_data(from), ibuf_size(from));
}

int
ibuf_get(struct ibuf *buf, void *data, size_t len)
{
	if (ibuf_size(buf) < len) {
		errno = EBADMSG;
		return (-1);
	}

	memcpy(data, ibuf_data(buf), len);
	buf->rpos += len;
	return (0);
}

int
ibuf_get_ibuf(struct ibuf *buf, size_t len, struct ibuf *new)
{
	if (ibuf_size(buf) < len) {
		errno = EBADMSG;
		return (-1);
	}

	ibuf_from_buffer(new, ibuf_data(buf), len);
	buf->rpos += len;
	return (0);
}

int
ibuf_get_h16(struct ibuf *buf, uint16_t *value)
{
	return ibuf_get(buf, value, sizeof(*value));
}

int
ibuf_get_h32(struct ibuf *buf, uint32_t *value)
{
	return ibuf_get(buf, value, sizeof(*value));
}

int
ibuf_get_h64(struct ibuf *buf, uint64_t *value)
{
	return ibuf_get(buf, value, sizeof(*value));
}

int
ibuf_get_n8(struct ibuf *buf, uint8_t *value)
{
	return ibuf_get(buf, value, sizeof(*value));
}

int
ibuf_get_n16(struct ibuf *buf, uint16_t *value)
{
	int rv;

	rv = ibuf_get(buf, value, sizeof(*value));
	*value = be16toh(*value);
	return (rv);
}

int
ibuf_get_n32(struct ibuf *buf, uint32_t *value)
{
	int rv;

	rv = ibuf_get(buf, value, sizeof(*value));
	*value = be32toh(*value);
	return (rv);
}

int
ibuf_get_n64(struct ibuf *buf, uint64_t *value)
{
	int rv;

	rv = ibuf_get(buf, value, sizeof(*value));
	*value = be64toh(*value);
	return (rv);
}

char *
ibuf_get_string(struct ibuf *buf, size_t len)
{
	char *str;

	if (ibuf_size(buf) < len) {
		errno = EBADMSG;
		return (NULL);
	}

	str = strndup(ibuf_data(buf), len);
	if (str == NULL)
		return (NULL);
	buf->rpos += len;
	return (str);
}

int
ibuf_get_strbuf(struct ibuf *buf, char *str, size_t len)
{
	if (len == 0) {
		errno = EINVAL;
		return (-1);
	}

	if (ibuf_get(buf, str, len) == -1)
		return -1;
	if (str[len - 1] != '\0') {
		str[len - 1] = '\0';
		errno = EOVERFLOW;
		return -1;
	}
	return 0;
}

int
ibuf_skip(struct ibuf *buf, size_t len)
{
	if (ibuf_size(buf) < len) {
		errno = EBADMSG;
		return (-1);
	}

	buf->rpos += len;
	return (0);
}

void
ibuf_free(struct ibuf *buf)
{
	int save_errno = errno;

	if (buf == NULL)
		return;
	/* if buf lives on the stack abort before causing more harm */
	if (buf->fd == IBUF_FD_MARK_ON_STACK)
		abort();
	if (buf->fd >= 0)
		close(buf->fd);
	freezero(buf->buf, buf->size);
	free(buf);
	errno = save_errno;
}

int
ibuf_fd_avail(struct ibuf *buf)
{
	return (buf->fd >= 0);
}

int
ibuf_fd_get(struct ibuf *buf)
{
	int fd;

	/* negative fds are internal use and equivalent to -1 */
	if (buf->fd < 0)
		return (-1);
	fd = buf->fd;
	buf->fd = -1;
	return (fd);
}

void
ibuf_fd_set(struct ibuf *buf, int fd)
{
	/* if buf lives on the stack abort before causing more harm */
	if (buf->fd == IBUF_FD_MARK_ON_STACK)
		abort();
	if (buf->fd >= 0)
		close(buf->fd);
	buf->fd = -1;
	if (fd >= 0)
		buf->fd = fd;
}

struct msgbuf *
msgbuf_new(void)
{
	struct msgbuf *msgbuf;

	if ((msgbuf = calloc(1, sizeof(*msgbuf))) == NULL)
		return (NULL);
	ibufq_init(&msgbuf->bufs);
	ibufq_init(&msgbuf->rbufs);

	return msgbuf;
}

struct msgbuf *
msgbuf_new_reader(size_t hdrsz,
    struct ibuf *(*readhdr)(struct ibuf *, void *, int *), void *arg)
{
	struct msgbuf *msgbuf;
	char *buf;

	if (hdrsz == 0 || hdrsz > IBUF_READ_SIZE / 2) {
		errno = EINVAL;
		return (NULL);
	}

	if ((buf = malloc(IBUF_READ_SIZE)) == NULL)
		return (NULL);

	msgbuf = msgbuf_new();
	if (msgbuf == NULL) {
		free(buf);
		return (NULL);
	}

	msgbuf->rbuf = buf;
	msgbuf->hdrsize = hdrsz;
	msgbuf->readhdr = readhdr;
	msgbuf->rarg = arg;

	return (msgbuf);
}

void
msgbuf_free(struct msgbuf *msgbuf)
{
	if (msgbuf == NULL)
		return;
	msgbuf_clear(msgbuf);
	free(msgbuf->rbuf);
	free(msgbuf);
}

uint32_t
msgbuf_queuelen(struct msgbuf *msgbuf)
{
	return ibufq_queuelen(&msgbuf->bufs);
}

void
msgbuf_clear(struct msgbuf *msgbuf)
{
	/* write side */
	ibufq_flush(&msgbuf->bufs);

	/* read side */
	ibufq_flush(&msgbuf->rbufs);
	msgbuf->roff = 0;
	ibuf_free(msgbuf->rpmsg);
	msgbuf->rpmsg = NULL;
}

struct ibuf *
msgbuf_get(struct msgbuf *msgbuf)
{
	return ibufq_pop(&msgbuf->rbufs);
}

void
msgbuf_concat(struct msgbuf *msgbuf, struct ibufqueue *from)
{
	ibufq_concat(&msgbuf->bufs, from);
}

int
ibuf_write(int fd, struct msgbuf *msgbuf)
{
	struct iovec	 iov[IOV_MAX];
	struct ibuf	*buf;
	unsigned int	 i = 0;
	ssize_t	n;

	memset(&iov, 0, sizeof(iov));
	TAILQ_FOREACH(buf, &msgbuf->bufs.bufs, entry) {
		if (i >= IOV_MAX)
			break;
		iov[i].iov_base = ibuf_data(buf);
		iov[i].iov_len = ibuf_size(buf);
		i++;
	}
	if (i == 0)
		return (0);	/* nothing queued */

 again:
	if ((n = writev(fd, iov, i)) == -1) {
		if (errno == EINTR)
			goto again;
		if (errno == EAGAIN || errno == ENOBUFS)
			/* lets retry later again */
			return (0);
		return (-1);
	}

	msgbuf_drain(msgbuf, n);
	return (0);
}

int
msgbuf_write(int fd, struct msgbuf *msgbuf)
{
	struct iovec	 iov[IOV_MAX];
	struct ibuf	*buf, *buf0 = NULL;
	unsigned int	 i = 0;
	ssize_t		 n;
	struct msghdr	 msg;
	struct cmsghdr	*cmsg;
	union {
		struct cmsghdr	hdr;
		char		buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	memset(&iov, 0, sizeof(iov));
	memset(&msg, 0, sizeof(msg));
	memset(&cmsgbuf, 0, sizeof(cmsgbuf));
	TAILQ_FOREACH(buf, &msgbuf->bufs.bufs, entry) {
		if (i >= IOV_MAX)
			break;
		if (i > 0 && buf->fd != -1)
			break;
		iov[i].iov_base = ibuf_data(buf);
		iov[i].iov_len = ibuf_size(buf);
		i++;
		if (buf->fd != -1)
			buf0 = buf;
	}

	if (i == 0)
		return (0);	/* nothing queued */

	msg.msg_iov = iov;
	msg.msg_iovlen = i;

	if (buf0 != NULL) {
		msg.msg_control = (caddr_t)&cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*(int *)CMSG_DATA(cmsg) = buf0->fd;
	}

 again:
	if ((n = sendmsg(fd, &msg, 0)) == -1) {
		if (errno == EINTR)
			goto again;
		if (errno == EAGAIN || errno == ENOBUFS)
			/* lets retry later again */
			return (0);
		return (-1);
	}

	/*
	 * assumption: fd got sent if sendmsg sent anything
	 * this works because fds are passed one at a time
	 */
	if (buf0 != NULL) {
		close(buf0->fd);
		buf0->fd = -1;
	}

	msgbuf_drain(msgbuf, n);

	return (0);
}

static int
ibuf_read_process(struct msgbuf *msgbuf, int fd)
{
	struct ibuf rbuf, msg;
	ssize_t sz;

	ibuf_from_buffer(&rbuf, msgbuf->rbuf, msgbuf->roff);

	do {
		if (msgbuf->rpmsg == NULL) {
			if (ibuf_size(&rbuf) < msgbuf->hdrsize)
				break;
			/* get size from header */
			ibuf_from_buffer(&msg, ibuf_data(&rbuf),
			    msgbuf->hdrsize);
			if ((msgbuf->rpmsg = msgbuf->readhdr(&msg,
			    msgbuf->rarg, &fd)) == NULL)
				goto fail;
		}

		if (ibuf_left(msgbuf->rpmsg) <= ibuf_size(&rbuf))
			sz = ibuf_left(msgbuf->rpmsg);
		else
			sz = ibuf_size(&rbuf);

		/* neither call below can fail */
		if (ibuf_get_ibuf(&rbuf, sz, &msg) == -1 ||
		    ibuf_add_ibuf(msgbuf->rpmsg, &msg) == -1)
			goto fail;

		if (ibuf_left(msgbuf->rpmsg) == 0) {
			ibufq_push(&msgbuf->rbufs, msgbuf->rpmsg);
			msgbuf->rpmsg = NULL;
		}
	} while (ibuf_size(&rbuf) > 0);

	if (ibuf_size(&rbuf) > 0)
		memmove(msgbuf->rbuf, ibuf_data(&rbuf), ibuf_size(&rbuf));
	msgbuf->roff = ibuf_size(&rbuf);

	if (fd != -1)
		close(fd);
	return (1);

 fail:
	/* XXX how to properly clean up is unclear */
	if (fd != -1)
		close(fd);
	return (-1);
}

int
ibuf_read(int fd, struct msgbuf *msgbuf)
{
	struct iovec	iov;
	ssize_t		n;

	if (msgbuf->rbuf == NULL) {
		errno = EINVAL;
		return (-1);
	}

	iov.iov_base = msgbuf->rbuf + msgbuf->roff;
	iov.iov_len = IBUF_READ_SIZE - msgbuf->roff;

 again:
	if ((n = readv(fd, &iov, 1)) == -1) {
		if (errno == EINTR)
			goto again;
		if (errno == EAGAIN)
			/* lets retry later again */
			return (1);
		return (-1);
	}
	if (n == 0)	/* connection closed */
		return (0);

	msgbuf->roff += n;
	/* new data arrived, try to process it */
	return (ibuf_read_process(msgbuf, -1));
}

int
msgbuf_read(int fd, struct msgbuf *msgbuf)
{
	struct msghdr		 msg;
	struct cmsghdr		*cmsg;
	union {
		struct cmsghdr hdr;
		char	buf[CMSG_SPACE(sizeof(int) * 1)];
	} cmsgbuf;
	struct iovec		 iov;
	ssize_t			 n;
	int			 fdpass = -1;

	if (msgbuf->rbuf == NULL) {
		errno = EINVAL;
		return (-1);
	}

	memset(&msg, 0, sizeof(msg));
	memset(&cmsgbuf, 0, sizeof(cmsgbuf));

	iov.iov_base = msgbuf->rbuf + msgbuf->roff;
	iov.iov_len = IBUF_READ_SIZE - msgbuf->roff;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

again:
	if ((n = recvmsg(fd, &msg, 0)) == -1) {
		if (errno == EINTR)
			goto again;
		if (errno == EMSGSIZE)
			/*
			 * Not enough fd slots: fd passing failed, retry
			 * to receive the message without fd.
			 * imsg_get_fd() will return -1 in that case.
			 */
			goto again;
		if (errno == EAGAIN)
			/* lets retry later again */
			return (1);
		return (-1);
	}
	if (n == 0)	/* connection closed */
		return (0);

	msgbuf->roff += n;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS) {
			int i, j, f;

			/*
			 * We only accept one file descriptor.  Due to C
			 * padding rules, our control buffer might contain
			 * more than one fd, and we must close them.
			 */
			j = ((char *)cmsg + cmsg->cmsg_len -
			    (char *)CMSG_DATA(cmsg)) / sizeof(int);
			for (i = 0; i < j; i++) {
				f = ((int *)CMSG_DATA(cmsg))[i];
				if (i == 0)
					fdpass = f;
				else
					close(f);
			}
		}
		/* we do not handle other ctl data level */
	}

	/* new data arrived, try to process it */
	return (ibuf_read_process(msgbuf, fdpass));
}

static void
msgbuf_drain(struct msgbuf *msgbuf, size_t n)
{
	struct ibuf	*buf;

	while ((buf = TAILQ_FIRST(&msgbuf->bufs.bufs)) != NULL) {
		if (n >= ibuf_size(buf)) {
			n -= ibuf_size(buf);
			TAILQ_REMOVE(&msgbuf->bufs.bufs, buf, entry);
			msgbuf->bufs.queued--;
			ibuf_free(buf);
		} else {
			buf->rpos += n;
			return;
		}
	}
}

static void
ibufq_init(struct ibufqueue *bufq)
{
	TAILQ_INIT(&bufq->bufs);
	bufq->queued = 0;
}

struct ibufqueue *
ibufq_new(void)
{
	struct ibufqueue *bufq;

	if ((bufq = calloc(1, sizeof(*bufq))) == NULL)
		return NULL;
	ibufq_init(bufq);
	return bufq;
}

void
ibufq_free(struct ibufqueue *bufq)
{
	if (bufq == NULL)
		return;
	ibufq_flush(bufq);
	free(bufq);
}

struct ibuf *
ibufq_pop(struct ibufqueue *bufq)
{
	struct ibuf *buf;

	if ((buf = TAILQ_FIRST(&bufq->bufs)) == NULL)
		return NULL;
	TAILQ_REMOVE(&bufq->bufs, buf, entry);
	bufq->queued--;
	return buf;
}

void
ibufq_push(struct ibufqueue *bufq, struct ibuf *buf)
{
	/* if buf lives on the stack abort before causing more harm */
	if (buf->fd == IBUF_FD_MARK_ON_STACK)
		abort();
	TAILQ_INSERT_TAIL(&bufq->bufs, buf, entry);
	bufq->queued++;
}

uint32_t
ibufq_queuelen(struct ibufqueue *bufq)
{
	return (bufq->queued);
}

void
ibufq_concat(struct ibufqueue *to, struct ibufqueue *from)
{
	to->queued += from->queued;
	TAILQ_CONCAT(&to->bufs, &from->bufs, entry);
	from->queued = 0;
}

void
ibufq_flush(struct ibufqueue *bufq)
{
	struct ibuf *buf;

	while ((buf = TAILQ_FIRST(&bufq->bufs)) != NULL) {
		TAILQ_REMOVE(&bufq->bufs, buf, entry);
		ibuf_free(buf);
	}
	bufq->queued = 0;
}
