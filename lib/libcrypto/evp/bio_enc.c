/* $OpenBSD: bio_enc.c,v 1.33 2024/04/12 11:10:34 tb Exp $ */
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

#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "bio_local.h"
#include "evp_local.h"

static int enc_write(BIO *h, const char *buf, int num);
static int enc_read(BIO *h, char *buf, int size);
/*static int enc_puts(BIO *h, const char *str); */
/*static int enc_gets(BIO *h, char *str, int size); */
static long enc_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int enc_new(BIO *h);
static int enc_free(BIO *data);
static long enc_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fps);
#define ENC_BLOCK_SIZE	(1024*4)
#define BUF_OFFSET	(EVP_MAX_BLOCK_LENGTH*2)

typedef struct enc_struct {
	int buf_len;
	int buf_off;
	int cont;		/* <= 0 when finished */
	int finished;
	int ok;			/* bad decrypt */
	EVP_CIPHER_CTX *cipher_ctx;
	/* buf is larger than ENC_BLOCK_SIZE because EVP_DecryptUpdate
	 * can return up to a block more data than is presented to it
	 */
	char buf[ENC_BLOCK_SIZE + BUF_OFFSET + 2];
} BIO_ENC_CTX;

static const BIO_METHOD methods_enc = {
	.type = BIO_TYPE_CIPHER,
	.name = "cipher",
	.bwrite = enc_write,
	.bread = enc_read,
	.ctrl = enc_ctrl,
	.create = enc_new,
	.destroy = enc_free,
	.callback_ctrl = enc_callback_ctrl
};

const BIO_METHOD *
BIO_f_cipher(void)
{
	return &methods_enc;
}
LCRYPTO_ALIAS(BIO_f_cipher);

static void
bio_enc_ctx_free(BIO_ENC_CTX *ctx)
{
	if (ctx == NULL)
		return;

	EVP_CIPHER_CTX_free(ctx->cipher_ctx);
	freezero(ctx, sizeof(*ctx));
}

static int
enc_new(BIO *bio)
{
	BIO_ENC_CTX *ctx;
	int ret = 0;

	if ((ctx = calloc(1, sizeof(BIO_ENC_CTX))) == NULL)
		goto err;
	if ((ctx->cipher_ctx = EVP_CIPHER_CTX_new()) == NULL)
		goto err;

	ctx->cont = 1;
	ctx->ok = 1;

	bio->ptr = ctx;
	ctx = NULL;

	ret = 1;

 err:
	bio_enc_ctx_free(ctx);

	return ret;
}

static int
enc_free(BIO *bio)
{
	if (bio == NULL)
		return 0;

	bio_enc_ctx_free(bio->ptr);
	explicit_bzero(bio, sizeof(*bio));

	return 1;
}

static int
enc_read(BIO *bio, char *out, int outl)
{
	BIO_ENC_CTX *ctx;
	int ret = 0, i;

	if (out == NULL)
		return 0;
	ctx = bio->ptr;

	if (ctx == NULL || bio->next_bio == NULL)
		return 0;

	/* First check if there are bytes decoded/encoded */
	if (ctx->buf_len > 0) {
		i = ctx->buf_len - ctx->buf_off;
		if (i > outl)
			i = outl;
		memcpy(out, &(ctx->buf[ctx->buf_off]), i);
		ret = i;
		out += i;
		outl -= i;
		ctx->buf_off += i;
		if (ctx->buf_len == ctx->buf_off) {
			ctx->buf_len = 0;
			ctx->buf_off = 0;
		}
	}

	/* At this point, we have room of outl bytes and an empty
	 * buffer, so we should read in some more. */

	while (outl > 0) {
		if (ctx->cont <= 0)
			break;

		/* read in at IV offset, read the EVP_Cipher
		 * documentation about why */
		i = BIO_read(bio->next_bio, &ctx->buf[BUF_OFFSET],
		    ENC_BLOCK_SIZE);

		if (i <= 0) {
			/* Should be continue next time we are called? */
			if (!BIO_should_retry(bio->next_bio)) {
				ctx->cont = i;
				i = EVP_CipherFinal_ex(ctx->cipher_ctx,
				    (unsigned char *)ctx->buf,
				    &(ctx->buf_len));
				ctx->ok = i;
				ctx->buf_off = 0;
			} else {
				ret = (ret == 0) ? i : ret;
				break;
			}
		} else {
			EVP_CipherUpdate(ctx->cipher_ctx,
			    (unsigned char *)ctx->buf, &ctx->buf_len,
			    (unsigned char *)&ctx->buf[BUF_OFFSET], i);
			ctx->cont = 1;
			/* Note: it is possible for EVP_CipherUpdate to
			 * decrypt zero bytes because this is or looks like
			 * the final block: if this happens we should retry
			 * and either read more data or decrypt the final
			 * block
			 */
			if (ctx->buf_len == 0)
				continue;
		}

		if (ctx->buf_len <= outl)
			i = ctx->buf_len;
		else
			i = outl;
		if (i <= 0)
			break;
		memcpy(out, ctx->buf, i);
		ret += i;
		ctx->buf_off = i;
		outl -= i;
		out += i;
	}

	BIO_clear_retry_flags(bio);
	BIO_copy_next_retry(bio);
	return ret == 0 ? ctx->cont : ret;
}

static int
enc_write(BIO *bio, const char *in, int inl)
{
	BIO_ENC_CTX *ctx;
	int ret = 0, n, i;

	ctx = bio->ptr;
	ret = inl;

	BIO_clear_retry_flags(bio);
	n = ctx->buf_len - ctx->buf_off;
	while (n > 0) {
		i = BIO_write(bio->next_bio, &(ctx->buf[ctx->buf_off]), n);
		if (i <= 0) {
			BIO_copy_next_retry(bio);
			return i;
		}
		ctx->buf_off += i;
		n -= i;
	}
	/* at this point all pending data has been written */

	if (in == NULL || inl <= 0)
		return 0;

	ctx->buf_off = 0;
	while (inl > 0) {
		n = inl > ENC_BLOCK_SIZE ? ENC_BLOCK_SIZE : inl;
		EVP_CipherUpdate(ctx->cipher_ctx,
		    (unsigned char *)ctx->buf, &ctx->buf_len,
		    (unsigned char *)in, n);
		inl -= n;
		in += n;

		ctx->buf_off = 0;
		n = ctx->buf_len;
		while (n > 0) {
			i = BIO_write(bio->next_bio, &ctx->buf[ctx->buf_off], n);
			if (i <= 0) {
				BIO_copy_next_retry(bio);
				return ret == inl ? i : ret - inl;
			}
			n -= i;
			ctx->buf_off += i;
		}
		ctx->buf_len = 0;
		ctx->buf_off = 0;
	}
	BIO_copy_next_retry(bio);

	return ret;
}

static long
enc_ctrl(BIO *bio, int cmd, long num, void *ptr)
{
	BIO *dbio;
	BIO_ENC_CTX *ctx, *dctx;
	EVP_CIPHER_CTX **c_ctx;
	int i;
	long ret = 1;

	ctx = bio->ptr;

	switch (cmd) {
	case BIO_CTRL_RESET:
		ctx->ok = 1;
		ctx->finished = 0;
		EVP_CipherInit_ex(ctx->cipher_ctx, NULL, NULL, NULL, NULL,
		    ctx->cipher_ctx->encrypt);
		ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		break;
	case BIO_CTRL_EOF:	/* More to read */
		if (ctx->cont <= 0)
			ret = 1;
		else
			ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		break;
	case BIO_CTRL_WPENDING:
		ret = ctx->buf_len - ctx->buf_off;
		if (ret <= 0)
			ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		break;
	case BIO_CTRL_PENDING: /* More to read in buffer */
		ret = ctx->buf_len - ctx->buf_off;
		if (ret <= 0)
			ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		break;
	case BIO_CTRL_FLUSH:
		/* do a final write */
 again:
		while (ctx->buf_len != ctx->buf_off) {
			i = enc_write(bio, NULL, 0);
			if (i < 0)
				return i;
		}

		if (!ctx->finished) {
			ctx->finished = 1;
			ctx->buf_off = 0;
			ret = EVP_CipherFinal_ex(ctx->cipher_ctx,
			    (unsigned char *)ctx->buf,
			    &ctx->buf_len);
			ctx->ok = (int)ret;
			if (ret <= 0)
				break;

			/* push out the bytes */
			goto again;
		}

		/* Finally flush the underlying BIO */
		ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		break;
	case BIO_C_GET_CIPHER_STATUS:
		ret = (long)ctx->ok;
		break;
	case BIO_C_DO_STATE_MACHINE:
		BIO_clear_retry_flags(bio);
		ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		BIO_copy_next_retry(bio);
		break;
	case BIO_C_GET_CIPHER_CTX:
		c_ctx = ptr;
		*c_ctx = ctx->cipher_ctx;
		bio->init = 1;
		break;
	case BIO_CTRL_DUP:
		dbio = ptr;
		dctx = dbio->ptr;
		ret = EVP_CIPHER_CTX_copy(dctx->cipher_ctx, ctx->cipher_ctx);
		if (ret)
			dbio->init = 1;
		break;
	default:
		ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
		break;
	}

	return ret;
}

static long
enc_callback_ctrl(BIO *bio, int cmd, BIO_info_cb *fp)
{
	long ret = 1;

	if (bio->next_bio == NULL)
		return 0;

	switch (cmd) {
	default:
		ret = BIO_callback_ctrl(bio->next_bio, cmd, fp);
		break;
	}

	return ret;
}

int
BIO_set_cipher(BIO *bio, const EVP_CIPHER *c, const unsigned char *k,
    const unsigned char *i, int e)
{
	BIO_ENC_CTX *ctx;
	long (*cb)(BIO *, int, const char *, int, long, long);

	if (bio == NULL)
		return 0;

	if ((ctx = BIO_get_data(bio)) == NULL)
		return 0;

	if ((cb = BIO_get_callback(bio)) != NULL) {
		if (cb(bio, BIO_CB_CTRL, (const char *)c, BIO_CTRL_SET, e, 0L) <= 0)
			return 0;
	}

	BIO_set_init(bio, 1);

	if (!EVP_CipherInit_ex(ctx->cipher_ctx, c, NULL, k, i, e))
		return 0;

	if (cb != NULL)
		return cb(bio, BIO_CB_CTRL, (const char *)c, BIO_CTRL_SET, e, 1L);

	return 1;
}
LCRYPTO_ALIAS(BIO_set_cipher);
