/*	$OpenBSD: io.c,v 1.29 2025/08/01 13:46:06 claudio Exp $ */
/*
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/queue.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <imsg.h>

#include "extern.h"

#define IO_FD_MARK	0x80000000U

/*
 * Create new io buffer, call io_close() when done with it.
 * Function always returns a new buffer.
 */
struct ibuf *
io_new_buffer(void)
{
	struct ibuf *b;

	if ((b = ibuf_dynamic(64, MAX_MSG_SIZE)) == NULL)
		err(1, NULL);
	ibuf_add_zero(b, sizeof(size_t));	/* can not fail */
	return b;
}

/*
 * Add a simple object of static size to the io buffer.
 */
void
io_simple_buffer(struct ibuf *b, const void *res, size_t sz)
{
	if (ibuf_add(b, res, sz) == -1)
		err(1, NULL);
}

/*
 * Add a sz sized buffer into the io buffer.
 */
void
io_buf_buffer(struct ibuf *b, const void *p, size_t sz)
{
	if (ibuf_add(b, &sz, sizeof(size_t)) == -1)
		err(1, NULL);
	if (sz > 0)
		if (ibuf_add(b, p, sz) == -1)
			err(1, NULL);
}

/*
 * Add a string into the io buffer.
 */
void
io_str_buffer(struct ibuf *b, const char *p)
{
	io_buf_buffer(b, p, strlen(p));
}

/*
 * Add an optional string into the io buffer.
 */
void
io_opt_str_buffer(struct ibuf *b, const char *p)
{
	size_t sz = (p == NULL) ? 0 : strlen(p);

	io_buf_buffer(b, p, sz);
}

/*
 * Finish and enqueue a io buffer.
 */
void
io_close_buffer(struct msgbuf *msgbuf, struct ibuf *b)
{
	size_t len;

	len = ibuf_size(b);
	if (ibuf_fd_avail(b))
		len |= IO_FD_MARK;
	ibuf_set(b, 0, &len, sizeof(len));
	ibuf_close(msgbuf, b);
}

/*
 * Finish and enqueue a io buffer.
 */
void
io_close_queue(struct ibufqueue *bufq, struct ibuf *b)
{
	size_t len;

	len = ibuf_size(b);
	if (ibuf_fd_avail(b))
		len |= IO_FD_MARK;
	ibuf_set(b, 0, &len, sizeof(len));
	ibufq_push(bufq, b);
}

/*
 * Read of an ibuf and extract sz byte from there.
 * Does nothing if "sz" is zero.
 * Return 1 on success or 0 if there was not enough data.
 */
void
io_read_buf(struct ibuf *b, void *res, size_t sz)
{
	if (sz == 0)
		errx(1, "io_read_buf: zero size");
	if (ibuf_get(b, res, sz) == -1)
		err(1, "bad internal framing");
}

/*
 * Read a string, allocating space for it. String can not be empty.
 */
void
io_read_str(struct ibuf *b, char **res)
{
	size_t	 sz;

	io_read_buf(b, &sz, sizeof(sz));
	if (sz == 0)
		errx(1, "bad internal framing: empty string");
	if ((*res = calloc(sz + 1, 1)) == NULL)
		err(1, NULL);
	io_read_buf(b, *res, sz);
}

/*
 * Read a string (returns NULL for zero-length strings), allocating
 * space for it.
 * Return 1 on success or 0 if there was not enough data.
 */
void
io_read_opt_str(struct ibuf *b, char **res)
{
	size_t	 sz;

	io_read_buf(b, &sz, sizeof(sz));
	if (sz == 0) {
		*res = NULL;
		return;
	}
	if ((*res = calloc(sz + 1, 1)) == NULL)
		err(1, NULL);
	io_read_buf(b, *res, sz);
}

/*
 * Read a binary buffer, allocating space for it.
 * If the buffer is zero-sized, this won't allocate "res", but
 * will still initialise it to NULL.
 * Return 1 on success or 0 if there was not enough data.
 */
void
io_read_buf_alloc(struct ibuf *b, void **res, size_t *sz)
{
	*res = NULL;
	io_read_buf(b, sz, sizeof(*sz));
	if (*sz == 0)
		return;
	if ((*res = malloc(*sz)) == NULL)
		err(1, NULL);
	io_read_buf(b, *res, *sz);
}

struct ibuf *
io_parse_hdr(struct ibuf *buf, void *arg, int *fd)
{
	struct ibuf *b;
	size_t len;
	int hasfd = 0;

	if (ibuf_get(buf, &len, sizeof(len)) == -1)
		return NULL;

	if (len & IO_FD_MARK) {
		hasfd = 1;
		len &= ~IO_FD_MARK;
	}
	if (len <= sizeof(len) || len > MAX_MSG_SIZE) {
		errno = ERANGE;
		return NULL;
	}
	if ((b = ibuf_open(len)) == NULL)
		return NULL;
	if (hasfd) {
		ibuf_fd_set(b, *fd);
		*fd = -1;
	}
	return b;
}

struct ibuf *
io_buf_get(struct msgbuf *msgq)
{
	struct ibuf *b;

	if ((b = msgbuf_get(msgq)) == NULL)
		return NULL;

	ibuf_skip(b, sizeof(size_t));
	return b;
}
