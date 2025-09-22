/* $OpenBSD: bio_ssl.c,v 1.41 2025/06/02 12:18:22 jsg Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "bio_local.h"
#include "ssl_local.h"

static int ssl_write(BIO *h, const char *buf, int num);
static int ssl_read(BIO *h, char *buf, int size);
static int ssl_puts(BIO *h, const char *str);
static long ssl_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int ssl_new(BIO *h);
static int ssl_free(BIO *data);
static long ssl_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);
typedef struct bio_ssl_st {
	SSL *ssl; /* The ssl handle :-) */
	/* re-negotiate every time the total number of bytes is this size */
	int num_renegotiates;
	unsigned long renegotiate_count;
	unsigned long byte_count;
	unsigned long renegotiate_timeout;
	time_t last_time;
} BIO_SSL;

static const BIO_METHOD methods_sslp = {
	.type = BIO_TYPE_SSL,
	.name = "ssl",
	.bwrite = ssl_write,
	.bread = ssl_read,
	.bputs = ssl_puts,
	.ctrl = ssl_ctrl,
	.create = ssl_new,
	.destroy = ssl_free,
	.callback_ctrl = ssl_callback_ctrl,
};

const BIO_METHOD *
BIO_f_ssl(void)
{
	return (&methods_sslp);
}
LSSL_ALIAS(BIO_f_ssl);

static int
ssl_new(BIO *bi)
{
	BIO_SSL *bs;

	bs = calloc(1, sizeof(BIO_SSL));
	if (bs == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		return (0);
	}
	bi->init = 0;
	bi->ptr = (char *)bs;
	bi->flags = 0;
	return (1);
}
LSSL_ALIAS(BIO_f_ssl);

static int
ssl_free(BIO *a)
{
	BIO_SSL *bs;

	if (a == NULL)
		return (0);
	bs = (BIO_SSL *)a->ptr;
	if (bs->ssl != NULL)
		SSL_shutdown(bs->ssl);
	if (a->shutdown) {
		if (a->init && (bs->ssl != NULL))
			SSL_free(bs->ssl);
		a->init = 0;
		a->flags = 0;
	}
	free(a->ptr);
	return (1);
}

static int
ssl_read(BIO *b, char *out, int outl)
{
	int ret = 1;
	BIO_SSL *sb;
	SSL *ssl;
	int retry_reason = 0;
	int r = 0;

	if (out == NULL)
		return (0);
	sb = (BIO_SSL *)b->ptr;
	ssl = sb->ssl;

	BIO_clear_retry_flags(b);

	ret = SSL_read(ssl, out, outl);

	switch (SSL_get_error(ssl, ret)) {
	case SSL_ERROR_NONE:
		if (ret <= 0)
			break;
		if (sb->renegotiate_count > 0) {
			sb->byte_count += ret;
			if (sb->byte_count > sb->renegotiate_count) {
				sb->byte_count = 0;
				sb->num_renegotiates++;
				SSL_renegotiate(ssl);
				r = 1;
			}
		}
		if ((sb->renegotiate_timeout > 0) && (!r)) {
			time_t tm;

			tm = time(NULL);
			if (tm > sb->last_time + sb->renegotiate_timeout) {
				sb->last_time = tm;
				sb->num_renegotiates++;
				SSL_renegotiate(ssl);
			}
		}

		break;
	case SSL_ERROR_WANT_READ:
		BIO_set_retry_read(b);
		break;
	case SSL_ERROR_WANT_WRITE:
		BIO_set_retry_write(b);
		break;
	case SSL_ERROR_WANT_X509_LOOKUP:
		BIO_set_retry_special(b);
		retry_reason = BIO_RR_SSL_X509_LOOKUP;
		break;
	case SSL_ERROR_WANT_ACCEPT:
		BIO_set_retry_special(b);
		retry_reason = BIO_RR_ACCEPT;
		break;
	case SSL_ERROR_WANT_CONNECT:
		BIO_set_retry_special(b);
		retry_reason = BIO_RR_CONNECT;
		break;
	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_SSL:
	case SSL_ERROR_ZERO_RETURN:
	default:
		break;
	}

	b->retry_reason = retry_reason;
	return (ret);
}

static int
ssl_write(BIO *b, const char *out, int outl)
{
	int ret, r = 0;
	int retry_reason = 0;
	SSL *ssl;
	BIO_SSL *bs;

	if (out == NULL)
		return (0);
	bs = (BIO_SSL *)b->ptr;
	ssl = bs->ssl;

	BIO_clear_retry_flags(b);

	ret = SSL_write(ssl, out, outl);

	switch (SSL_get_error(ssl, ret)) {
	case SSL_ERROR_NONE:
		if (ret <= 0)
			break;
		if (bs->renegotiate_count > 0) {
			bs->byte_count += ret;
			if (bs->byte_count > bs->renegotiate_count) {
				bs->byte_count = 0;
				bs->num_renegotiates++;
				SSL_renegotiate(ssl);
				r = 1;
			}
		}
		if ((bs->renegotiate_timeout > 0) && (!r)) {
			time_t tm;

			tm = time(NULL);
			if (tm > bs->last_time + bs->renegotiate_timeout) {
				bs->last_time = tm;
				bs->num_renegotiates++;
				SSL_renegotiate(ssl);
			}
		}
		break;
	case SSL_ERROR_WANT_WRITE:
		BIO_set_retry_write(b);
		break;
	case SSL_ERROR_WANT_READ:
		BIO_set_retry_read(b);
		break;
	case SSL_ERROR_WANT_X509_LOOKUP:
		BIO_set_retry_special(b);
		retry_reason = BIO_RR_SSL_X509_LOOKUP;
		break;
	case SSL_ERROR_WANT_CONNECT:
		BIO_set_retry_special(b);
		retry_reason = BIO_RR_CONNECT;
	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_SSL:
	default:
		break;
	}

	b->retry_reason = retry_reason;
	return (ret);
}

static long
ssl_ctrl(BIO *b, int cmd, long num, void *ptr)
{
	SSL **sslp, *ssl;
	BIO_SSL *bs;
	BIO *dbio, *bio;
	long ret = 1;

	bs = (BIO_SSL *)b->ptr;
	ssl = bs->ssl;
	if ((ssl == NULL)  && (cmd != BIO_C_SET_SSL))
		return (0);
	switch (cmd) {
	case BIO_CTRL_RESET:
		SSL_shutdown(ssl);

		if (ssl->handshake_func == ssl->method->ssl_connect)
			SSL_set_connect_state(ssl);
		else if (ssl->handshake_func == ssl->method->ssl_accept)
			SSL_set_accept_state(ssl);

		SSL_clear(ssl);

		if (b->next_bio != NULL)
			ret = BIO_ctrl(b->next_bio, cmd, num, ptr);
		else if (ssl->rbio != NULL)
			ret = BIO_ctrl(ssl->rbio, cmd, num, ptr);
		else
			ret = 1;
		break;
	case BIO_CTRL_INFO:
		ret = 0;
		break;
	case BIO_C_SSL_MODE:
		if (num) /* client mode */
			SSL_set_connect_state(ssl);
		else
			SSL_set_accept_state(ssl);
		break;
	case BIO_C_SET_SSL_RENEGOTIATE_TIMEOUT:
		ret = bs->renegotiate_timeout;
		if (num < 60)
			num = 5;
		bs->renegotiate_timeout = (unsigned long)num;
		bs->last_time = time(NULL);
		break;
	case BIO_C_SET_SSL_RENEGOTIATE_BYTES:
		ret = bs->renegotiate_count;
		if ((long)num >=512)
			bs->renegotiate_count = (unsigned long)num;
		break;
	case BIO_C_GET_SSL_NUM_RENEGOTIATES:
		ret = bs->num_renegotiates;
		break;
	case BIO_C_SET_SSL:
		if (ssl != NULL) {
			ssl_free(b);
			if (!ssl_new(b))
				return 0;
		}
		b->shutdown = (int)num;
		ssl = (SSL *)ptr;
		((BIO_SSL *)b->ptr)->ssl = ssl;
		bio = SSL_get_rbio(ssl);
		if (bio != NULL) {
			if (b->next_bio != NULL)
				BIO_push(bio, b->next_bio);
			b->next_bio = bio;
			CRYPTO_add(&bio->references, 1, CRYPTO_LOCK_BIO);
		}
		b->init = 1;
		break;
	case BIO_C_GET_SSL:
		if (ptr != NULL) {
			sslp = (SSL **)ptr;
			*sslp = ssl;
		} else
			ret = 0;
		break;
	case BIO_CTRL_GET_CLOSE:
		ret = b->shutdown;
		break;
	case BIO_CTRL_SET_CLOSE:
		b->shutdown = (int)num;
		break;
	case BIO_CTRL_WPENDING:
		ret = BIO_ctrl(ssl->wbio, cmd, num, ptr);
		break;
	case BIO_CTRL_PENDING:
		ret = SSL_pending(ssl);
		if (ret == 0)
			ret = BIO_pending(ssl->rbio);
		break;
	case BIO_CTRL_FLUSH:
		BIO_clear_retry_flags(b);
		ret = BIO_ctrl(ssl->wbio, cmd, num, ptr);
		BIO_copy_next_retry(b);
		break;
	case BIO_CTRL_PUSH:
		if ((b->next_bio != NULL) && (b->next_bio != ssl->rbio)) {
			SSL_set_bio(ssl, b->next_bio, b->next_bio);
			CRYPTO_add(&b->next_bio->references, 1,
			    CRYPTO_LOCK_BIO);
		}
		break;
	case BIO_CTRL_POP:
		/* Only detach if we are the BIO explicitly being popped */
		if (b == ptr) {
			/* Shouldn't happen in practice because the
			 * rbio and wbio are the same when pushed.
			 */
			if (ssl->rbio != ssl->wbio)
				BIO_free_all(ssl->wbio);
			if (b->next_bio != NULL)
				CRYPTO_add(&b->next_bio->references, -1, CRYPTO_LOCK_BIO);
			ssl->wbio = NULL;
			ssl->rbio = NULL;
		}
		break;
	case BIO_C_DO_STATE_MACHINE:
		BIO_clear_retry_flags(b);

		b->retry_reason = 0;
		ret = (int)SSL_do_handshake(ssl);

		switch (SSL_get_error(ssl, (int)ret)) {
		case SSL_ERROR_WANT_READ:
			BIO_set_flags(b,
			    BIO_FLAGS_READ|BIO_FLAGS_SHOULD_RETRY);
			break;
		case SSL_ERROR_WANT_WRITE:
			BIO_set_flags(b,
			    BIO_FLAGS_WRITE|BIO_FLAGS_SHOULD_RETRY);
			break;
		case SSL_ERROR_WANT_CONNECT:
			BIO_set_flags(b,
			    BIO_FLAGS_IO_SPECIAL|BIO_FLAGS_SHOULD_RETRY);
			b->retry_reason = b->next_bio->retry_reason;
			break;
		default:
			break;
		}
		break;
	case BIO_CTRL_DUP:
		dbio = (BIO *)ptr;
		if (((BIO_SSL *)dbio->ptr)->ssl != NULL)
			SSL_free(((BIO_SSL *)dbio->ptr)->ssl);
		((BIO_SSL *)dbio->ptr)->ssl = SSL_dup(ssl);
		((BIO_SSL *)dbio->ptr)->renegotiate_count =
		    ((BIO_SSL *)b->ptr)->renegotiate_count;
		((BIO_SSL *)dbio->ptr)->byte_count =
		    ((BIO_SSL *)b->ptr)->byte_count;
		((BIO_SSL *)dbio->ptr)->renegotiate_timeout =
		    ((BIO_SSL *)b->ptr)->renegotiate_timeout;
		((BIO_SSL *)dbio->ptr)->last_time =
		    ((BIO_SSL *)b->ptr)->last_time;
		ret = (((BIO_SSL *)dbio->ptr)->ssl != NULL);
		break;
	case BIO_C_GET_FD:
		ret = BIO_ctrl(ssl->rbio, cmd, num, ptr);
		break;
	case BIO_CTRL_SET_CALLBACK:
		{
			ret = 0;
		}
		break;
	case BIO_CTRL_GET_CALLBACK:
		{
			void (**fptr)(const SSL *xssl, int type, int val);

			fptr = (void (**)(const SSL *xssl, int type, int val))
			    ptr;
			*fptr = SSL_get_info_callback(ssl);
		}
		break;
	default:
		ret = BIO_ctrl(ssl->rbio, cmd, num, ptr);
		break;
	}
	return (ret);
}

static long
ssl_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
	SSL *ssl;
	BIO_SSL *bs;
	long ret = 1;

	bs = (BIO_SSL *)b->ptr;
	ssl = bs->ssl;
	switch (cmd) {
	case BIO_CTRL_SET_CALLBACK:
		{
		/* FIXME: setting this via a completely different prototype
		   seems like a crap idea */
			SSL_set_info_callback(ssl,
			    (void (*)(const SSL *, int, int))fp);
		}
		break;
	default:
		ret = BIO_callback_ctrl(ssl->rbio, cmd, fp);
		break;
	}
	return (ret);
}

static int
ssl_puts(BIO *bp, const char *str)
{
	int n, ret;

	n = strlen(str);
	ret = BIO_write(bp, str, n);
	return (ret);
}

BIO *
BIO_new_buffer_ssl_connect(SSL_CTX *ctx)
{
	BIO *ret = NULL, *buf = NULL, *ssl = NULL;

	if ((buf = BIO_new(BIO_f_buffer())) == NULL)
		goto err;
	if ((ssl = BIO_new_ssl_connect(ctx)) == NULL)
		goto err;
	if ((ret = BIO_push(buf, ssl)) == NULL)
		goto err;
	return (ret);

 err:
	BIO_free(buf);
	BIO_free(ssl);
	return (NULL);
}
LSSL_ALIAS(BIO_new_buffer_ssl_connect);

BIO *
BIO_new_ssl_connect(SSL_CTX *ctx)
{
	BIO *ret = NULL, *con = NULL, *ssl = NULL;

	if ((con = BIO_new(BIO_s_connect())) == NULL)
		goto err;
	if ((ssl = BIO_new_ssl(ctx, 1)) == NULL)
		goto err;
	if ((ret = BIO_push(ssl, con)) == NULL)
		goto err;
	return (ret);

 err:
	BIO_free(con);
	BIO_free(ssl);
	return (NULL);
}
LSSL_ALIAS(BIO_new_ssl_connect);

BIO *
BIO_new_ssl(SSL_CTX *ctx, int client)
{
	BIO *ret;
	SSL *ssl;

	if ((ret = BIO_new(BIO_f_ssl())) == NULL)
		goto err;
	if ((ssl = SSL_new(ctx)) == NULL)
		goto err;

	if (client)
		SSL_set_connect_state(ssl);
	else
		SSL_set_accept_state(ssl);

	BIO_set_ssl(ret, ssl, BIO_CLOSE);
	return (ret);

 err:
	BIO_free(ret);
	return (NULL);
}
LSSL_ALIAS(BIO_new_ssl);

int
BIO_ssl_copy_session_id(BIO *t, BIO *f)
{
	t = BIO_find_type(t, BIO_TYPE_SSL);
	f = BIO_find_type(f, BIO_TYPE_SSL);
	if ((t == NULL) || (f == NULL))
		return (0);
	if ((((BIO_SSL *)t->ptr)->ssl == NULL) ||
	    (((BIO_SSL *)f->ptr)->ssl == NULL))
		return (0);
	if (!SSL_copy_session_id(((BIO_SSL *)t->ptr)->ssl,
	    ((BIO_SSL *)f->ptr)->ssl))
		return (0);
	return (1);
}
LSSL_ALIAS(BIO_ssl_copy_session_id);

void
BIO_ssl_shutdown(BIO *b)
{
	SSL *s;

	while (b != NULL) {
		if (b->method->type == BIO_TYPE_SSL) {
			s = ((BIO_SSL *)b->ptr)->ssl;
			SSL_shutdown(s);
			break;
		}
		b = b->next_bio;
	}
}
LSSL_ALIAS(BIO_ssl_shutdown);
