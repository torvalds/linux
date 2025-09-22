/* $OpenBSD: tls_bio_cb.c,v 1.22 2024/03/26 06:24:52 joshua Exp $ */
/*
 * Copyright (c) 2016 Tobias Pape <tobias@netshed.de>
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

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>

#include <tls.h>
#include "tls_internal.h"

static int bio_cb_write(BIO *bio, const char *buf, int num);
static int bio_cb_read(BIO *bio, char *buf, int size);
static int bio_cb_puts(BIO *bio, const char *str);
static long bio_cb_ctrl(BIO *bio, int cmd, long num, void *ptr);

static BIO_METHOD *bio_cb_method;

static pthread_mutex_t bio_cb_method_lock = PTHREAD_MUTEX_INITIALIZER;

static void
bio_cb_method_init(void)
{
	BIO_METHOD *bio_method;

	if (bio_cb_method != NULL)
		return;

	bio_method = BIO_meth_new(BIO_TYPE_MEM, "libtls_callbacks");
	if (bio_method == NULL)
		return;

	BIO_meth_set_write(bio_method, bio_cb_write);
	BIO_meth_set_read(bio_method, bio_cb_read);
	BIO_meth_set_puts(bio_method, bio_cb_puts);
	BIO_meth_set_ctrl(bio_method, bio_cb_ctrl);

	bio_cb_method = bio_method;
}

static BIO_METHOD *
bio_s_cb(void)
{
	if (bio_cb_method != NULL)
		return (bio_cb_method);

	pthread_mutex_lock(&bio_cb_method_lock);
	bio_cb_method_init();
	pthread_mutex_unlock(&bio_cb_method_lock);

	return (bio_cb_method);
}

static int
bio_cb_puts(BIO *bio, const char *str)
{
	return (bio_cb_write(bio, str, strlen(str)));
}

static long
bio_cb_ctrl(BIO *bio, int cmd, long num, void *ptr)
{
	long ret = 1;

	switch (cmd) {
	case BIO_CTRL_GET_CLOSE:
		ret = (long)BIO_get_shutdown(bio);
		break;
	case BIO_CTRL_SET_CLOSE:
		BIO_set_shutdown(bio, (int)num);
		break;
	case BIO_CTRL_DUP:
	case BIO_CTRL_FLUSH:
		break;
	case BIO_CTRL_INFO:
	case BIO_CTRL_GET:
	case BIO_CTRL_SET:
	default:
		ret = BIO_ctrl(BIO_next(bio), cmd, num, ptr);
	}

	return (ret);
}

static int
bio_cb_write(BIO *bio, const char *buf, int num)
{
	struct tls *ctx = BIO_get_data(bio);
	int rv;

	BIO_clear_retry_flags(bio);
	rv = (ctx->write_cb)(ctx, buf, num, ctx->cb_arg);
	if (rv == TLS_WANT_POLLIN) {
		BIO_set_retry_read(bio);
		rv = -1;
	} else if (rv == TLS_WANT_POLLOUT) {
		BIO_set_retry_write(bio);
		rv = -1;
	}
	return (rv);
}

static int
bio_cb_read(BIO *bio, char *buf, int size)
{
	struct tls *ctx = BIO_get_data(bio);
	int rv;

	BIO_clear_retry_flags(bio);
	rv = (ctx->read_cb)(ctx, buf, size, ctx->cb_arg);
	if (rv == TLS_WANT_POLLIN) {
		BIO_set_retry_read(bio);
		rv = -1;
	} else if (rv == TLS_WANT_POLLOUT) {
		BIO_set_retry_write(bio);
		rv = -1;
	}
	return (rv);
}

int
tls_set_cbs(struct tls *ctx, tls_read_cb read_cb, tls_write_cb write_cb,
    void *cb_arg)
{
	const BIO_METHOD *bio_cb;
	BIO *bio;
	int rv = -1;

	if (read_cb == NULL || write_cb == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "no callbacks provided");
		goto err;
	}

	ctx->read_cb = read_cb;
	ctx->write_cb = write_cb;
	ctx->cb_arg = cb_arg;

	if ((bio_cb = bio_s_cb()) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "failed to create callback method");
		goto err;
	}
	if ((bio = BIO_new(bio_cb)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "failed to create callback i/o");
		goto err;
	}
	BIO_set_data(bio, ctx);
	BIO_set_init(bio, 1);

	SSL_set_bio(ctx->ssl_conn, bio, bio);

	rv = 0;

 err:
	return (rv);
}
