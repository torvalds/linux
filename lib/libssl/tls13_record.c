/* $OpenBSD: tls13_record.c,v 1.10 2022/07/22 19:33:53 jsing Exp $ */
/*
 * Copyright (c) 2018, 2019 Joel Sing <jsing@openbsd.org>
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

#include "tls13_internal.h"
#include "tls13_record.h"

struct tls13_record {
	uint16_t version;
	uint8_t content_type;
	size_t rec_len;
	uint8_t *data;
	size_t data_len;
	CBS cbs;

	struct tls_buffer *buf;
};

struct tls13_record *
tls13_record_new(void)
{
	struct tls13_record *rec = NULL;

	if ((rec = calloc(1, sizeof(struct tls13_record))) == NULL)
		goto err;
	if ((rec->buf = tls_buffer_new(TLS13_RECORD_MAX_LEN)) == NULL)
		goto err;

	return rec;

 err:
	tls13_record_free(rec);

	return NULL;
}

void
tls13_record_free(struct tls13_record *rec)
{
	if (rec == NULL)
		return;

	tls_buffer_free(rec->buf);

	freezero(rec->data, rec->data_len);
	freezero(rec, sizeof(struct tls13_record));
}

uint16_t
tls13_record_version(struct tls13_record *rec)
{
	return rec->version;
}

uint8_t
tls13_record_content_type(struct tls13_record *rec)
{
	return rec->content_type;
}

int
tls13_record_header(struct tls13_record *rec, CBS *cbs)
{
	if (rec->data_len < TLS13_RECORD_HEADER_LEN)
		return 0;

	CBS_init(cbs, rec->data, TLS13_RECORD_HEADER_LEN);

	return 1;
}

int
tls13_record_content(struct tls13_record *rec, CBS *cbs)
{
	CBS content;

	tls13_record_data(rec, &content);

	if (!CBS_skip(&content, TLS13_RECORD_HEADER_LEN))
		return 0;

	CBS_dup(&content, cbs);

	return 1;
}

void
tls13_record_data(struct tls13_record *rec, CBS *cbs)
{
	CBS_init(cbs, rec->data, rec->data_len);
}

int
tls13_record_set_data(struct tls13_record *rec, uint8_t *data, size_t data_len)
{
	if (data_len > TLS13_RECORD_MAX_LEN)
		return 0;

	freezero(rec->data, rec->data_len);
	rec->data = data;
	rec->data_len = data_len;
	CBS_init(&rec->cbs, rec->data, rec->data_len);

	return 1;
}

ssize_t
tls13_record_recv(struct tls13_record *rec, tls_read_cb wire_read,
    void *wire_arg)
{
	uint16_t rec_len, rec_version;
	uint8_t content_type;
	ssize_t ret;
	CBS cbs;

	if (rec->data != NULL)
		return TLS13_IO_FAILURE;

	if (rec->content_type == 0) {
		if ((ret = tls_buffer_extend(rec->buf,
		    TLS13_RECORD_HEADER_LEN, wire_read, wire_arg)) <= 0)
			return ret;

		if (!tls_buffer_data(rec->buf, &cbs))
			return TLS13_IO_FAILURE;

		if (!CBS_get_u8(&cbs, &content_type))
			return TLS13_IO_FAILURE;
		if (!CBS_get_u16(&cbs, &rec_version))
			return TLS13_IO_FAILURE;
		if (!CBS_get_u16(&cbs, &rec_len))
			return TLS13_IO_FAILURE;

		if ((rec_version >> 8) != SSL3_VERSION_MAJOR)
			return TLS13_IO_RECORD_VERSION;
		if (rec_len > TLS13_RECORD_MAX_CIPHERTEXT_LEN)
			return TLS13_IO_RECORD_OVERFLOW;

		rec->content_type = content_type;
		rec->version = rec_version;
		rec->rec_len = rec_len;
	}

	if ((ret = tls_buffer_extend(rec->buf,
	    TLS13_RECORD_HEADER_LEN + rec->rec_len, wire_read, wire_arg)) <= 0)
		return ret;

	if (!tls_buffer_finish(rec->buf, &rec->data, &rec->data_len))
		return TLS13_IO_FAILURE;

	return rec->data_len;
}

ssize_t
tls13_record_send(struct tls13_record *rec, tls_write_cb wire_write,
    void *wire_arg)
{
	ssize_t ret;

	if (rec->data == NULL)
		return TLS13_IO_FAILURE;

	while (CBS_len(&rec->cbs) > 0) {
		if ((ret = wire_write(CBS_data(&rec->cbs),
		    CBS_len(&rec->cbs), wire_arg)) <= 0)
			return ret;

		if (!CBS_skip(&rec->cbs, ret))
			return TLS13_IO_FAILURE;
	}

	return rec->data_len;
}
