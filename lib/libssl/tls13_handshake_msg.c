/* $OpenBSD: tls13_handshake_msg.c,v 1.7 2024/02/04 20:50:23 tb Exp $ */
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

#include "bytestring.h"
#include "tls13_internal.h"

#define TLS13_HANDSHAKE_MSG_HEADER_LEN	4
#define TLS13_HANDSHAKE_MSG_INITIAL_LEN	256
#define TLS13_HANDSHAKE_MSG_MAX_LEN	(256 * 1024)

struct tls13_handshake_msg {
	uint8_t msg_type;
	uint32_t msg_len;
	uint8_t *data;
	size_t data_len;

	struct tls_buffer *buf;
	CBS cbs;
	CBB cbb;
};

struct tls13_handshake_msg *
tls13_handshake_msg_new(void)
{
	struct tls13_handshake_msg *msg = NULL;

	if ((msg = calloc(1, sizeof(struct tls13_handshake_msg))) == NULL)
		goto err;
	if ((msg->buf = tls_buffer_new(0)) == NULL)
		goto err;

	return msg;

 err:
	tls13_handshake_msg_free(msg);

	return NULL;
}

void
tls13_handshake_msg_free(struct tls13_handshake_msg *msg)
{
	if (msg == NULL)
		return;

	tls_buffer_free(msg->buf);

	CBB_cleanup(&msg->cbb);

	freezero(msg->data, msg->data_len);
	freezero(msg, sizeof(struct tls13_handshake_msg));
}

void
tls13_handshake_msg_data(struct tls13_handshake_msg *msg, CBS *cbs)
{
	CBS_init(cbs, msg->data, msg->data_len);
}

uint8_t
tls13_handshake_msg_type(struct tls13_handshake_msg *msg)
{
	return msg->msg_type;
}

int
tls13_handshake_msg_content(struct tls13_handshake_msg *msg, CBS *cbs)
{
	tls13_handshake_msg_data(msg, cbs);

	return CBS_skip(cbs, TLS13_HANDSHAKE_MSG_HEADER_LEN);
}

int
tls13_handshake_msg_start(struct tls13_handshake_msg *msg, CBB *body,
    uint8_t msg_type)
{
	if (!CBB_init(&msg->cbb, TLS13_HANDSHAKE_MSG_INITIAL_LEN))
		return 0;
	if (!CBB_add_u8(&msg->cbb, msg_type))
		return 0;
	if (!CBB_add_u24_length_prefixed(&msg->cbb, body))
		return 0;

	return 1;
}

int
tls13_handshake_msg_finish(struct tls13_handshake_msg *msg)
{
	if (!CBB_finish(&msg->cbb, &msg->data, &msg->data_len))
		return 0;

	CBS_init(&msg->cbs, msg->data, msg->data_len);

	return 1;
}

static ssize_t
tls13_handshake_msg_read_cb(void *buf, size_t n, void *cb_arg)
{
	struct tls13_record_layer *rl = cb_arg;

	return tls13_read_handshake_data(rl, buf, n);
}

int
tls13_handshake_msg_recv(struct tls13_handshake_msg *msg,
    struct tls13_record_layer *rl)
{
	uint8_t msg_type;
	uint32_t msg_len;
	CBS cbs;
	int ret;

	if (msg->data != NULL)
		return TLS13_IO_FAILURE;

	if (msg->msg_type == 0) {
		if ((ret = tls_buffer_extend(msg->buf,
		    TLS13_HANDSHAKE_MSG_HEADER_LEN,
		    tls13_handshake_msg_read_cb, rl)) <= 0)
			return ret;

		if (!tls_buffer_data(msg->buf, &cbs))
			return TLS13_IO_FAILURE;

		if (!CBS_get_u8(&cbs, &msg_type))
			return TLS13_IO_FAILURE;
		if (!CBS_get_u24(&cbs, &msg_len))
			return TLS13_IO_FAILURE;

		/* XXX - do we want to make this variable on message type? */
		if (msg_len > TLS13_HANDSHAKE_MSG_MAX_LEN)
			return TLS13_IO_FAILURE;

		msg->msg_type = msg_type;
		msg->msg_len = msg_len;
	}

	if ((ret = tls_buffer_extend(msg->buf,
	    TLS13_HANDSHAKE_MSG_HEADER_LEN + msg->msg_len,
	    tls13_handshake_msg_read_cb, rl)) <= 0)
		return ret;

	if (!tls_buffer_finish(msg->buf, &msg->data, &msg->data_len))
		return TLS13_IO_FAILURE;

	return TLS13_IO_SUCCESS;
}

int
tls13_handshake_msg_send(struct tls13_handshake_msg *msg,
    struct tls13_record_layer *rl)
{
	ssize_t ret;

	if (msg->data == NULL)
		return TLS13_IO_FAILURE;

	if (CBS_len(&msg->cbs) == 0)
		return TLS13_IO_FAILURE;

	while (CBS_len(&msg->cbs) > 0) {
		if ((ret = tls13_write_handshake_data(rl, CBS_data(&msg->cbs),
		    CBS_len(&msg->cbs))) <= 0)
			return ret;

		if (!CBS_skip(&msg->cbs, ret))
			return TLS13_IO_FAILURE;
	}

	return TLS13_IO_SUCCESS;
}
