/*	$OpenBSD: tls13_legacy.c,v 1.44 2024/01/30 14:50:50 jsing Exp $ */
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

#include <limits.h>

#include "ssl_local.h"
#include "tls13_internal.h"

static ssize_t
tls13_legacy_wire_read(SSL *ssl, uint8_t *buf, size_t len)
{
	int n;

	if (ssl->rbio == NULL) {
		SSLerror(ssl, SSL_R_BIO_NOT_SET);
		return TLS13_IO_FAILURE;
	}

	ssl->rwstate = SSL_READING;
	errno = 0;

	if ((n = BIO_read(ssl->rbio, buf, len)) <= 0) {
		if (BIO_should_read(ssl->rbio))
			return TLS13_IO_WANT_POLLIN;
		if (n == 0)
			return TLS13_IO_EOF;

		if (ERR_peek_error() == 0 && errno != 0)
			SYSerror(errno);

		return TLS13_IO_FAILURE;
	}

	if (n == len)
		ssl->rwstate = SSL_NOTHING;

	return n;
}

ssize_t
tls13_legacy_wire_read_cb(void *buf, size_t n, void *arg)
{
	struct tls13_ctx *ctx = arg;

	return tls13_legacy_wire_read(ctx->ssl, buf, n);
}

static ssize_t
tls13_legacy_wire_write(SSL *ssl, const uint8_t *buf, size_t len)
{
	int n;

	if (ssl->wbio == NULL) {
		SSLerror(ssl, SSL_R_BIO_NOT_SET);
		return TLS13_IO_FAILURE;
	}

	ssl->rwstate = SSL_WRITING;
	errno = 0;

	if ((n = BIO_write(ssl->wbio, buf, len)) <= 0) {
		if (BIO_should_write(ssl->wbio))
			return TLS13_IO_WANT_POLLOUT;

		if (ERR_peek_error() == 0 && errno != 0)
			SYSerror(errno);

		return TLS13_IO_FAILURE;
	}

	if (n == len)
		ssl->rwstate = SSL_NOTHING;

	return n;
}

ssize_t
tls13_legacy_wire_write_cb(const void *buf, size_t n, void *arg)
{
	struct tls13_ctx *ctx = arg;

	return tls13_legacy_wire_write(ctx->ssl, buf, n);
}

static ssize_t
tls13_legacy_wire_flush(SSL *ssl)
{
	if (BIO_flush(ssl->wbio) <= 0) {
		if (BIO_should_write(ssl->wbio))
			return TLS13_IO_WANT_POLLOUT;

		if (ERR_peek_error() == 0 && errno != 0)
			SYSerror(errno);

		return TLS13_IO_FAILURE;
	}

	return TLS13_IO_SUCCESS;
}

ssize_t
tls13_legacy_wire_flush_cb(void *arg)
{
	struct tls13_ctx *ctx = arg;

	return tls13_legacy_wire_flush(ctx->ssl);
}

static void
tls13_legacy_error(SSL *ssl)
{
	struct tls13_ctx *ctx = ssl->tls13;
	int reason = SSL_R_UNKNOWN;

	/* If we received a fatal alert we already put an error on the stack. */
	if (ssl->s3->fatal_alert != 0)
		return;

	switch (ctx->error.code) {
	case TLS13_ERR_VERIFY_FAILED:
		reason = SSL_R_CERTIFICATE_VERIFY_FAILED;
		break;
	case TLS13_ERR_HRR_FAILED:
		reason = SSL_R_NO_CIPHERS_AVAILABLE;
		break;
	case TLS13_ERR_TRAILING_DATA:
		reason = SSL_R_EXTRA_DATA_IN_MESSAGE;
		break;
	case TLS13_ERR_NO_SHARED_CIPHER:
		reason = SSL_R_NO_SHARED_CIPHER;
		break;
	case TLS13_ERR_NO_CERTIFICATE:
		reason = SSL_R_MISSING_RSA_CERTIFICATE; /* XXX */
		break;
	case TLS13_ERR_NO_PEER_CERTIFICATE:
		reason = SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE;
		break;
	}

	/* Something (probably libcrypto) already pushed an error on the stack. */
	if (reason == SSL_R_UNKNOWN && ERR_peek_error() != 0)
		return;

	ERR_put_error(ERR_LIB_SSL, (0xfff), reason, ctx->error.file,
	    ctx->error.line);
}

static int
tls13_legacy_return_code(SSL *ssl, ssize_t ret)
{
	if (ret > INT_MAX) {
		SSLerror(ssl, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	/* A successful read, write or other operation. */
	if (ret > 0)
		return ret;

	ssl->rwstate = SSL_NOTHING;

	switch (ret) {
	case TLS13_IO_EOF:
		return 0;

	case TLS13_IO_FAILURE:
		tls13_legacy_error(ssl);
		return -1;

	case TLS13_IO_ALERT:
		tls13_legacy_error(ssl);
		return -1;

	case TLS13_IO_WANT_POLLIN:
		BIO_set_retry_read(ssl->rbio);
		ssl->rwstate = SSL_READING;
		return -1;

	case TLS13_IO_WANT_POLLOUT:
		BIO_set_retry_write(ssl->wbio);
		ssl->rwstate = SSL_WRITING;
		return -1;

	case TLS13_IO_WANT_RETRY:
		SSLerror(ssl, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	SSLerror(ssl, ERR_R_INTERNAL_ERROR);
	return -1;
}

int
tls13_legacy_pending(const SSL *ssl)
{
	struct tls13_ctx *ctx = ssl->tls13;
	ssize_t ret;

	if (ctx == NULL)
		return 0;

	ret = tls13_pending_application_data(ctx->rl);
	if (ret < 0 || ret > INT_MAX)
		return 0;

	return ret;
}

int
tls13_legacy_read_bytes(SSL *ssl, int type, unsigned char *buf, int len, int peek)
{
	struct tls13_ctx *ctx = ssl->tls13;
	ssize_t ret;

	if (ctx == NULL || !ctx->handshake_completed) {
		if ((ret = ssl->handshake_func(ssl)) <= 0)
			return ret;
		if (len == 0)
			return 0;
		return tls13_legacy_return_code(ssl, TLS13_IO_WANT_POLLIN);
	}

	tls13_record_layer_set_retry_after_phh(ctx->rl,
	    (ctx->ssl->mode & SSL_MODE_AUTO_RETRY) != 0);

	if (type != SSL3_RT_APPLICATION_DATA) {
		SSLerror(ssl, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return -1;
	}
	if (len < 0) {
		SSLerror(ssl, SSL_R_BAD_LENGTH);
		return -1;
	}

	if (peek)
		ret = tls13_peek_application_data(ctx->rl, buf, len);
	else
		ret = tls13_read_application_data(ctx->rl, buf, len);

	return tls13_legacy_return_code(ssl, ret);
}

int
tls13_legacy_write_bytes(SSL *ssl, int type, const void *vbuf, int len)
{
	struct tls13_ctx *ctx = ssl->tls13;
	const uint8_t *buf = vbuf;
	size_t n, sent;
	ssize_t ret;

	if (ctx == NULL || !ctx->handshake_completed) {
		if ((ret = ssl->handshake_func(ssl)) <= 0)
			return ret;
		if (len == 0)
			return 0;
		return tls13_legacy_return_code(ssl, TLS13_IO_WANT_POLLOUT);
	}

	if (type != SSL3_RT_APPLICATION_DATA) {
		SSLerror(ssl, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		return -1;
	}
	if (len < 0) {
		SSLerror(ssl, SSL_R_BAD_LENGTH);
		return -1;
	}

	/*
	 * The TLSv1.3 record layer write behaviour is the same as
	 * SSL_MODE_ENABLE_PARTIAL_WRITE.
	 */
	if (ssl->mode & SSL_MODE_ENABLE_PARTIAL_WRITE) {
		ret = tls13_write_application_data(ctx->rl, buf, len);
		return tls13_legacy_return_code(ssl, ret);
	}

	/*
	 * In the non-SSL_MODE_ENABLE_PARTIAL_WRITE case we have to loop until
	 * we have written out all of the requested data.
	 */
	sent = ssl->s3->wnum;
	if (len < sent) {
		SSLerror(ssl, SSL_R_BAD_LENGTH);
		return -1;
	}
	n = len - sent;
	for (;;) {
		if (n == 0) {
			ssl->s3->wnum = 0;
			return sent;
		}
		if ((ret = tls13_write_application_data(ctx->rl,
		    &buf[sent], n)) <= 0) {
			ssl->s3->wnum = sent;
			return tls13_legacy_return_code(ssl, ret);
		}
		sent += ret;
		n -= ret;
	}
}

static int
tls13_use_legacy_stack(struct tls13_ctx *ctx)
{
	SSL *s = ctx->ssl;
	CBB cbb, fragment;
	CBS cbs;

	memset(&cbb, 0, sizeof(cbb));

	if (!ssl3_setup_init_buffer(s))
		goto err;
	if (!ssl3_setup_buffers(s))
		goto err;
	if (!ssl_init_wbio_buffer(s, 1))
		goto err;

	/* Stash any unprocessed data from the last record. */
	tls13_record_layer_rcontent(ctx->rl, &cbs);
	if (CBS_len(&cbs) > 0) {
		if (!CBB_init_fixed(&cbb, s->s3->rbuf.buf,
		    s->s3->rbuf.len))
			goto err;
		if (!CBB_add_u8(&cbb, SSL3_RT_HANDSHAKE))
			goto err;
		if (!CBB_add_u16(&cbb, TLS1_2_VERSION))
			goto err;
		if (!CBB_add_u16_length_prefixed(&cbb, &fragment))
			goto err;
		if (!CBB_add_bytes(&fragment, CBS_data(&cbs), CBS_len(&cbs)))
			goto err;
		if (!CBB_finish(&cbb, NULL, NULL))
			goto err;

		s->s3->rbuf.offset = SSL3_RT_HEADER_LENGTH;
		s->s3->rbuf.left = CBS_len(&cbs);
		s->s3->rrec.type = SSL3_RT_HANDSHAKE;
		s->s3->rrec.length = CBS_len(&cbs);
		s->rstate = SSL_ST_READ_BODY;
		s->packet = s->s3->rbuf.buf;
		s->packet_length = SSL3_RT_HEADER_LENGTH;
		s->mac_packet = 1;
	}

	/* Stash the current handshake message. */
	tls13_handshake_msg_data(ctx->hs_msg, &cbs);
	if (!BUF_MEM_grow_clean(s->init_buf, CBS_len(&cbs)))
		goto err;
	if (!CBS_write_bytes(&cbs, s->init_buf->data,
	    s->init_buf->length, NULL))
		goto err;

	s->s3->hs.tls12.reuse_message = 1;
	s->s3->hs.tls12.message_type = tls13_handshake_msg_type(ctx->hs_msg);
	s->s3->hs.tls12.message_size = CBS_len(&cbs) - SSL3_HM_HEADER_LENGTH;

	/*
	 * Only switch the method after initialization is complete
	 * as we start part way into the legacy state machine.
	 */
	s->method = tls_legacy_method();

	return 1;

 err:
	CBB_cleanup(&cbb);

	return 0;
}

int
tls13_use_legacy_client(struct tls13_ctx *ctx)
{
	SSL *s = ctx->ssl;

	if (!tls13_use_legacy_stack(ctx))
		return 0;

	s->handshake_func = s->method->ssl_connect;
	s->version = s->method->max_tls_version;

	return 1;
}

int
tls13_use_legacy_server(struct tls13_ctx *ctx)
{
	SSL *s = ctx->ssl;

	if (!tls13_use_legacy_stack(ctx))
		return 0;

	s->handshake_func = s->method->ssl_accept;
	s->version = s->method->max_tls_version;
	s->server = 1;

	return 1;
}

int
tls13_legacy_accept(SSL *ssl)
{
	struct tls13_ctx *ctx = ssl->tls13;
	int ret;

	if (ctx == NULL) {
		if ((ctx = tls13_ctx_new(TLS13_HS_SERVER, ssl)) == NULL) {
			SSLerror(ssl, ERR_R_INTERNAL_ERROR); /* XXX */
			return -1;
		}
		if (!tls13_server_init(ctx)) {
			if (ERR_peek_error() == 0)
				SSLerror(ssl, ERR_R_INTERNAL_ERROR); /* XXX */
			return -1;
		}
	}

	ERR_clear_error();

	ret = tls13_server_accept(ctx);
	if (ret == TLS13_IO_USE_LEGACY)
		return ssl->method->ssl_accept(ssl);

	ret = tls13_legacy_return_code(ssl, ret);

	if (ctx->info_cb != NULL)
		ctx->info_cb(ctx, TLS13_INFO_ACCEPT_EXIT, ret);

	return ret;
}

int
tls13_legacy_connect(SSL *ssl)
{
	struct tls13_ctx *ctx = ssl->tls13;
	int ret;

	if (ctx == NULL) {
		if ((ctx = tls13_ctx_new(TLS13_HS_CLIENT, ssl)) == NULL) {
			SSLerror(ssl, ERR_R_INTERNAL_ERROR); /* XXX */
			return -1;
		}
		if (!tls13_client_init(ctx)) {
			if (ERR_peek_error() == 0)
				SSLerror(ssl, ERR_R_INTERNAL_ERROR); /* XXX */
			return -1;
		}
	}

	ERR_clear_error();

	ret = tls13_client_connect(ctx);
	if (ret == TLS13_IO_USE_LEGACY)
		return ssl->method->ssl_connect(ssl);

	ret = tls13_legacy_return_code(ssl, ret);

	if (ctx->info_cb != NULL)
		ctx->info_cb(ctx, TLS13_INFO_CONNECT_EXIT, ret);

	return ret;
}

int
tls13_legacy_shutdown(SSL *ssl)
{
	struct tls13_ctx *ctx = ssl->tls13;
	uint8_t buf[512]; /* XXX */
	ssize_t ret;

	/*
	 * We need to return 0 at the point that we have completed sending a
	 * close-notify. We return 1 when we have sent and received close-notify
	 * alerts. All other cases, including EOF, return -1 and set internal
	 * state appropriately. Note that all of this insanity can also be
	 * externally controlled by manipulating the shutdown flags.
	 */
	if (ctx == NULL || ssl->quiet_shutdown) {
		ssl->shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN;
		return 1;
	}

	if ((ssl->shutdown & SSL_SENT_SHUTDOWN) == 0) {
		ssl->shutdown |= SSL_SENT_SHUTDOWN;
		ret = tls13_send_alert(ctx->rl, TLS13_ALERT_CLOSE_NOTIFY);
		if (ret == TLS13_IO_EOF)
			return -1;
		if (ret != TLS13_IO_SUCCESS)
			return tls13_legacy_return_code(ssl, ret);
		goto done;
	}

	ret = tls13_record_layer_send_pending(ctx->rl);
	if (ret == TLS13_IO_EOF)
		return -1;
	if (ret != TLS13_IO_SUCCESS)
		return tls13_legacy_return_code(ssl, ret);

	if ((ssl->shutdown & SSL_RECEIVED_SHUTDOWN) == 0) {
		/*
		 * If there is no application data pending, attempt to read more
		 * data in order to receive a close-notify. This should trigger
		 * a record to be read from the wire, which may be application
		 * handshake or alert data. Only one attempt is made with no
		 * error handling, in order to match previous semantics.
		 */
		if (tls13_pending_application_data(ctx->rl) == 0) {
			(void)tls13_read_application_data(ctx->rl, buf, sizeof(buf));
			if (!ctx->close_notify_recv)
				return -1;
		}
	}

 done:
	if (ssl->shutdown == (SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN))
		return 1;

	return 0;
}

int
tls13_legacy_servername_process(struct tls13_ctx *ctx, uint8_t *alert)
{
	int legacy_alert = SSL_AD_UNRECOGNIZED_NAME;
	int ret = SSL_TLSEXT_ERR_NOACK;
	SSL_CTX *ssl_ctx = ctx->ssl->ctx;
	SSL *s = ctx->ssl;

	if (ssl_ctx->tlsext_servername_callback == NULL)
		ssl_ctx = s->initial_ctx;
	if (ssl_ctx->tlsext_servername_callback == NULL)
		return 1;

	ret = ssl_ctx->tlsext_servername_callback(s, &legacy_alert,
	    ssl_ctx->tlsext_servername_arg);

	/*
	 * Ignore SSL_TLSEXT_ERR_ALERT_WARNING returns to match OpenSSL's
	 * behavior: the only warning alerts in TLSv1.3 are close_notify and
	 * user_canceled, neither of which should be returned by the callback.
	 */
	if (ret == SSL_TLSEXT_ERR_ALERT_FATAL) {
		if (legacy_alert >= 0 && legacy_alert <= 255)
			*alert = legacy_alert;
		return 0;
	}

	return 1;
}
