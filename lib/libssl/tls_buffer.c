/* $OpenBSD: tls_buffer.c,v 1.4 2022/11/10 18:06:37 jsing Exp $ */
/*
 * Copyright (c) 2018, 2019, 2022 Joel Sing <jsing@openbsd.org>
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

#include <stdlib.h>
#include <string.h>

#include "bytestring.h"
#include "tls_internal.h"

#define TLS_BUFFER_CAPACITY_LIMIT	(1024 * 1024)

struct tls_buffer {
	size_t capacity;
	size_t capacity_limit;
	uint8_t *data;
	size_t len;
	size_t offset;
};

static int tls_buffer_resize(struct tls_buffer *buf, size_t capacity);

struct tls_buffer *
tls_buffer_new(size_t init_size)
{
	struct tls_buffer *buf = NULL;

	if ((buf = calloc(1, sizeof(struct tls_buffer))) == NULL)
		goto err;

	buf->capacity_limit = TLS_BUFFER_CAPACITY_LIMIT;

	if (!tls_buffer_resize(buf, init_size))
		goto err;

	return buf;

 err:
	tls_buffer_free(buf);

	return NULL;
}

void
tls_buffer_clear(struct tls_buffer *buf)
{
	freezero(buf->data, buf->capacity);

	buf->data = NULL;
	buf->capacity = 0;
	buf->len = 0;
	buf->offset = 0;
}

void
tls_buffer_free(struct tls_buffer *buf)
{
	if (buf == NULL)
		return;

	tls_buffer_clear(buf);

	freezero(buf, sizeof(struct tls_buffer));
}

static int
tls_buffer_grow(struct tls_buffer *buf, size_t capacity)
{
	if (buf->capacity >= capacity)
		return 1;

	return tls_buffer_resize(buf, capacity);
}

static int
tls_buffer_resize(struct tls_buffer *buf, size_t capacity)
{
	uint8_t *data;

	/*
	 * XXX - Consider maintaining a minimum size and growing more
	 * intelligently (rather than exactly).
	 */
	if (buf->capacity == capacity)
		return 1;

	if (capacity > buf->capacity_limit)
		return 0;

	if ((data = recallocarray(buf->data, buf->capacity, capacity, 1)) == NULL)
		return 0;

	buf->data = data;
	buf->capacity = capacity;

	/* Ensure that len and offset are valid if capacity decreased. */
	if (buf->len > buf->capacity)
		buf->len = buf->capacity;
	if (buf->offset > buf->len)
		buf->offset = buf->len;

	return 1;
}

void
tls_buffer_set_capacity_limit(struct tls_buffer *buf, size_t limit)
{
	/*
	 * XXX - do we want to force a resize if this limit is less than current
	 * capacity... and what do we do with existing data? Force a clear?
	 */
	buf->capacity_limit = limit;
}

ssize_t
tls_buffer_extend(struct tls_buffer *buf, size_t len,
    tls_read_cb read_cb, void *cb_arg)
{
	ssize_t ret;

	if (len == buf->len)
		return buf->len;

	if (len < buf->len)
		return TLS_IO_FAILURE;

	if (!tls_buffer_resize(buf, len))
		return TLS_IO_FAILURE;

	for (;;) {
		if ((ret = read_cb(&buf->data[buf->len],
		    buf->capacity - buf->len, cb_arg)) <= 0)
			return ret;

		if (ret > buf->capacity - buf->len)
			return TLS_IO_FAILURE;

		buf->len += ret;

		if (buf->len == buf->capacity)
			return buf->len;
	}
}

size_t
tls_buffer_remaining(struct tls_buffer *buf)
{
	if (buf->offset > buf->len)
		return 0;

	return buf->len - buf->offset;
}

ssize_t
tls_buffer_read(struct tls_buffer *buf, uint8_t *rbuf, size_t n)
{
	if (buf->offset > buf->len)
		return TLS_IO_FAILURE;

	if (buf->offset == buf->len)
		return TLS_IO_WANT_POLLIN;

	if (n > buf->len - buf->offset)
		n = buf->len - buf->offset;

	memcpy(rbuf, &buf->data[buf->offset], n);

	buf->offset += n;

	return n;
}

ssize_t
tls_buffer_write(struct tls_buffer *buf, const uint8_t *wbuf, size_t n)
{
	if (buf->offset > buf->len)
		return TLS_IO_FAILURE;

	/*
	 * To avoid continually growing the buffer, pull data up to the
	 * start of the buffer. If all data has been read then we can simply
	 * reset, otherwise wait until we're going to save at least 4KB of
	 * memory to reduce overhead.
	 */
	if (buf->offset == buf->len) {
		buf->len = 0;
		buf->offset = 0;
	}
	if (buf->offset >= 4096) {
		memmove(buf->data, &buf->data[buf->offset],
		    buf->len - buf->offset);
		buf->len -= buf->offset;
		buf->offset = 0;
	}

	if (buf->len > SIZE_MAX - n)
		return TLS_IO_FAILURE;
	if (!tls_buffer_grow(buf, buf->len + n))
		return TLS_IO_FAILURE;

	memcpy(&buf->data[buf->len], wbuf, n);

	buf->len += n;

	return n;
}

int
tls_buffer_append(struct tls_buffer *buf, const uint8_t *wbuf, size_t n)
{
	return tls_buffer_write(buf, wbuf, n) == n;
}

int
tls_buffer_data(struct tls_buffer *buf, CBS *out_cbs)
{
	CBS cbs;

	CBS_init(&cbs, buf->data, buf->len);

	if (!CBS_skip(&cbs, buf->offset))
		return 0;

	CBS_dup(&cbs, out_cbs);

	return 1;
}

int
tls_buffer_finish(struct tls_buffer *buf, uint8_t **out, size_t *out_len)
{
	if (out == NULL || out_len == NULL)
		return 0;

	*out = buf->data;
	*out_len = buf->len;

	buf->data = NULL;
	buf->capacity = 0;
	buf->len = 0;
	buf->offset = 0;

	return 1;
}
