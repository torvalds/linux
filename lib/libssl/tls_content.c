/* $OpenBSD: tls_content.c,v 1.2 2022/11/11 17:15:27 jsing Exp $ */
/*
 * Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
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

#include "tls_content.h"

/* Content from a TLS record. */
struct tls_content {
	uint8_t type;
	uint16_t epoch;

	const uint8_t *data;
	size_t data_len;
	CBS cbs;
};

struct tls_content *
tls_content_new(void)
{
	return calloc(1, sizeof(struct tls_content));
}

void
tls_content_clear(struct tls_content *content)
{
	freezero((void *)content->data, content->data_len);
	memset(content, 0, sizeof(*content));
}

void
tls_content_free(struct tls_content *content)
{
	if (content == NULL)
		return;

	tls_content_clear(content);

	freezero(content, sizeof(struct tls_content));
}

CBS *
tls_content_cbs(struct tls_content *content)
{
	return &content->cbs;
}

int
tls_content_equal(struct tls_content *content, const uint8_t *buf, size_t n)
{
	return CBS_mem_equal(&content->cbs, buf, n);
}

size_t
tls_content_remaining(struct tls_content *content)
{
	return CBS_len(&content->cbs);
}

uint8_t
tls_content_type(struct tls_content *content)
{
	return content->type;
}

int
tls_content_dup_data(struct tls_content *content, uint8_t type,
    const uint8_t *data, size_t data_len)
{
	uint8_t *dup;

	if ((dup = calloc(1, data_len)) == NULL)
		return 0;
	memcpy(dup, data, data_len);

	tls_content_set_data(content, type, dup, data_len);

	return 1;
}

uint16_t
tls_content_epoch(struct tls_content *content)
{
	return content->epoch;
}

void
tls_content_set_epoch(struct tls_content *content, uint16_t epoch)
{
	content->epoch = epoch;
}

void
tls_content_set_data(struct tls_content *content, uint8_t type,
    const uint8_t *data, size_t data_len)
{
	tls_content_clear(content);

	content->type = type;
	content->data = data;
	content->data_len = data_len;

	CBS_init(&content->cbs, content->data, content->data_len);
}

int
tls_content_set_bounds(struct tls_content *content, size_t offset, size_t len)
{
	size_t content_len;

	content_len = offset + len;
	if (content_len < len)
		return 0;
	if (content_len > content->data_len)
		return 0;

	CBS_init(&content->cbs, content->data, content_len);
	return CBS_skip(&content->cbs, offset);
}

static ssize_t
tls_content_read_internal(struct tls_content *content, uint8_t *buf, size_t n,
    int peek)
{
	if (n > CBS_len(&content->cbs))
		n = CBS_len(&content->cbs);

	/* XXX - CBS_memcpy? CBS_copy_bytes? */
	memcpy(buf, CBS_data(&content->cbs), n);

	if (!peek) {
		if (!CBS_skip(&content->cbs, n))
			return -1;
	}

	return n;
}

ssize_t
tls_content_peek(struct tls_content *content, uint8_t *buf, size_t n)
{
	return tls_content_read_internal(content, buf, n, 1);
}

ssize_t
tls_content_read(struct tls_content *content, uint8_t *buf, size_t n)
{
	return tls_content_read_internal(content, buf, n, 0);
}
