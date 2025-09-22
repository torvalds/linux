/* $OpenBSD: evp_cipher.c,v 1.28 2025/07/02 06:19:46 tb Exp $ */
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
/* ====================================================================
 * Copyright (c) 2015 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/evp.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"

int
EVP_CipherInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv, int enc)
{
	return EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, enc);
}
LCRYPTO_ALIAS(EVP_CipherInit);

int
EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *engine,
    const unsigned char *key, const unsigned char *iv, int enc)
{
	if (enc == -1)
		enc = ctx->encrypt;
	if (enc != 0)
		enc = 1;
	ctx->encrypt = enc;

	if (cipher == NULL && ctx->cipher == NULL) {
		EVPerror(EVP_R_NO_CIPHER_SET);
		return 0;
	}

	/*
	 * Set up cipher and context. Allocate cipher data and initialize ctx.
	 * On ctx reuse only retain encryption direction and key wrap flag.
	 */
	if (cipher != NULL) {
		unsigned long flags = ctx->flags;

		EVP_CIPHER_CTX_cleanup(ctx);
		ctx->encrypt = enc;
		ctx->flags = flags & EVP_CIPHER_CTX_FLAG_WRAP_ALLOW;

		ctx->cipher = cipher;
		ctx->key_len = cipher->key_len;

		if (ctx->cipher->ctx_size != 0) {
			ctx->cipher_data = calloc(1, ctx->cipher->ctx_size);
			if (ctx->cipher_data == NULL) {
				EVPerror(ERR_R_MALLOC_FAILURE);
				return 0;
			}
		}

		if ((ctx->cipher->flags & EVP_CIPH_CTRL_INIT) != 0) {
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_INIT, 0, NULL) <= 0) {
				EVPerror(EVP_R_INITIALIZATION_ERROR);
				return 0;
			}
		}
	}

	/* Block sizes must be a power of 2 due to the use of block_mask. */
	if (ctx->cipher->block_size != 1 &&
	    ctx->cipher->block_size != 8 &&
	    ctx->cipher->block_size != 16) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}

	if ((ctx->flags & EVP_CIPHER_CTX_FLAG_WRAP_ALLOW) == 0 &&
	    EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_WRAP_MODE) {
		EVPerror(EVP_R_WRAP_MODE_NOT_ALLOWED);
		return 0;
	}

	if ((EVP_CIPHER_CTX_flags(ctx) & EVP_CIPH_CUSTOM_IV) == 0) {
		int iv_len;

		switch (EVP_CIPHER_CTX_mode(ctx)) {

		case EVP_CIPH_STREAM_CIPHER:
		case EVP_CIPH_ECB_MODE:
			break;

		case EVP_CIPH_CFB_MODE:
		case EVP_CIPH_OFB_MODE:

			ctx->num = 0;
			/* fall-through */

		case EVP_CIPH_CBC_MODE:
			iv_len = EVP_CIPHER_CTX_iv_length(ctx);
			if (iv_len < 0 || iv_len > sizeof(ctx->oiv) ||
			    iv_len > sizeof(ctx->iv)) {
				EVPerror(EVP_R_IV_TOO_LARGE);
				return 0;
			}
			if (iv != NULL)
				memcpy(ctx->oiv, iv, iv_len);
			memcpy(ctx->iv, ctx->oiv, iv_len);
			break;

		case EVP_CIPH_CTR_MODE:
			ctx->num = 0;
			iv_len = EVP_CIPHER_CTX_iv_length(ctx);
			if (iv_len < 0 || iv_len > sizeof(ctx->iv)) {
				EVPerror(EVP_R_IV_TOO_LARGE);
				return 0;
			}
			/* Don't reuse IV for CTR mode */
			if (iv != NULL)
				memcpy(ctx->iv, iv, iv_len);
			break;

		default:
			return 0;
			break;
		}
	}

	if (key != NULL || (ctx->cipher->flags & EVP_CIPH_ALWAYS_CALL_INIT) != 0) {
		if (!ctx->cipher->init(ctx, key, iv, enc))
			return 0;
	}

	ctx->partial_len = 0;
	ctx->final_used = 0;

	return 1;
}
LCRYPTO_ALIAS(EVP_CipherInit_ex);

int
EVP_CipherUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len,
    const unsigned char *in, int in_len)
{
	if (ctx->encrypt)
		return EVP_EncryptUpdate(ctx, out, out_len, in, in_len);

	return EVP_DecryptUpdate(ctx, out, out_len, in, in_len);
}
LCRYPTO_ALIAS(EVP_CipherUpdate);

int
EVP_CipherFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len)
{
	if (ctx->encrypt)
		return EVP_EncryptFinal_ex(ctx, out, out_len);

	return EVP_DecryptFinal_ex(ctx, out, out_len);
}
LCRYPTO_ALIAS(EVP_CipherFinal);

int
EVP_CipherFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len)
{
	if (ctx->encrypt)
		return EVP_EncryptFinal_ex(ctx, out, out_len);

	return EVP_DecryptFinal_ex(ctx, out, out_len);
}
LCRYPTO_ALIAS(EVP_CipherFinal_ex);

int
EVP_EncryptInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit(ctx, cipher, key, iv, 1);
}
LCRYPTO_ALIAS(EVP_EncryptInit);

int
EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *engine,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, 1);
}
LCRYPTO_ALIAS(EVP_EncryptInit_ex);

/*
 * EVP_Cipher() is an implementation detail of EVP_Cipher{Update,Final}().
 * Behavior depends on EVP_CIPH_FLAG_CUSTOM_CIPHER being set on ctx->cipher.
 *
 * If the flag is set, do_cipher() operates in update mode if in != NULL and
 * in final mode if in == NULL. It returns the number of bytes written to out
 * (which may be 0) or -1 on error.
 *
 * If the flag is not set, do_cipher() assumes properly aligned data and that
 * padding is handled correctly by the caller. Most do_cipher() methods will
 * silently produce garbage and succeed. Returns 1 on success, 0 on error.
 */
int
EVP_Cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, const unsigned char *in,
    unsigned int in_len)
{
	return ctx->cipher->do_cipher(ctx, out, in, in_len);
}
LCRYPTO_ALIAS(EVP_Cipher);

static int
evp_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len,
    const unsigned char *in, int in_len)
{
	int len;

	*out_len = 0;

	if (in_len < 0)
		return 0;

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) != 0) {
		if ((len = ctx->cipher->do_cipher(ctx, out, in, in_len)) < 0)
			return 0;

		*out_len = len;
		return 1;
	}

	if (!ctx->cipher->do_cipher(ctx, out, in, in_len))
		return 0;

	*out_len = in_len;

	return 1;
}

int
EVP_EncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len,
    const unsigned char *in, int in_len)
{
	const int block_size = ctx->cipher->block_size;
	const int block_mask = block_size - 1;
	int partial_len = ctx->partial_len;
	int len = 0, total_len = 0;

	*out_len = 0;

	if ((block_size & block_mask) != 0)
		return 0;

	if (in_len < 0)
		return 0;

	if (in_len == 0 && EVP_CIPHER_mode(ctx->cipher) != EVP_CIPH_CCM_MODE)
		return 1;

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) != 0)
		return evp_cipher(ctx, out, out_len, in, in_len);

	if (partial_len == 0 && (in_len & block_mask) == 0)
		return evp_cipher(ctx, out, out_len, in, in_len);

	if (partial_len < 0 || partial_len >= block_size ||
	    block_size > sizeof(ctx->buf)) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}

	if (partial_len > 0) {
		int partial_needed;

		if ((partial_needed = block_size - partial_len) > in_len) {
			memcpy(&ctx->buf[partial_len], in, in_len);
			ctx->partial_len += in_len;
			return 1;
		}

		/*
		 * Once the first partial_needed bytes from in are processed,
		 * the number of multiples of block_size of data remaining is
		 * (in_len - partial_needed) & ~block_mask.  Ensure that this
		 * plus the block processed from ctx->buf doesn't overflow.
		 */
		if (((in_len - partial_needed) & ~block_mask) > INT_MAX - block_size) {
			EVPerror(EVP_R_TOO_LARGE);
			return 0;
		}
		memcpy(&ctx->buf[partial_len], in, partial_needed);

		len = 0;
		if (!evp_cipher(ctx, out, &len, ctx->buf, block_size))
			return 0;
		total_len = len;

		in_len -= partial_needed;
		in += partial_needed;
		out += len;
	}

	partial_len = in_len & block_mask;
	if ((in_len -= partial_len) > 0) {
		if (INT_MAX - in_len < total_len)
			return 0;
		len = 0;
		if (!evp_cipher(ctx, out, &len, in, in_len))
			return 0;
		if (INT_MAX - len < total_len)
			return 0;
		total_len += len;
	}

	if ((ctx->partial_len = partial_len) > 0)
		memcpy(ctx->buf, &in[in_len], partial_len);

	*out_len = total_len;

	return 1;
}
LCRYPTO_ALIAS(EVP_EncryptUpdate);

int
EVP_EncryptFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len)
{
	return EVP_EncryptFinal_ex(ctx, out, out_len);
}
LCRYPTO_ALIAS(EVP_EncryptFinal);

int
EVP_EncryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len)
{
	const int block_size = ctx->cipher->block_size;
	int partial_len = ctx->partial_len;
	int pad;

	*out_len = 0;

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) != 0)
		return evp_cipher(ctx, out, out_len, NULL, 0);

	if (partial_len < 0 || partial_len >= block_size ||
	    block_size > sizeof(ctx->buf)) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}
	if (block_size == 1)
		return 1;

	if ((ctx->flags & EVP_CIPH_NO_PADDING) != 0) {
		if (partial_len != 0) {
			EVPerror(EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH);
			return 0;
		}
		return 1;
	}

	pad = block_size - partial_len;
	memset(&ctx->buf[partial_len], pad, pad);

	return evp_cipher(ctx, out, out_len, ctx->buf, block_size);
}
LCRYPTO_ALIAS(EVP_EncryptFinal_ex);

int
EVP_DecryptInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit(ctx, cipher, key, iv, 0);
}
LCRYPTO_ALIAS(EVP_DecryptInit);

int
EVP_DecryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *engine,
    const unsigned char *key, const unsigned char *iv)
{
	return EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, 0);
}
LCRYPTO_ALIAS(EVP_DecryptInit_ex);

int
EVP_DecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len,
    const unsigned char *in, int in_len)
{
	const int block_size = ctx->cipher->block_size;
	const int block_mask = block_size - 1;
	int len = 0, total_len = 0;

	*out_len = 0;

	if ((block_size & block_mask) != 0)
		return 0;

	if (in_len < 0)
		return 0;

	if (in_len == 0 && EVP_CIPHER_mode(ctx->cipher) != EVP_CIPH_CCM_MODE)
		return 1;

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) != 0)
		return evp_cipher(ctx, out, out_len, in, in_len);

	if ((ctx->flags & EVP_CIPH_NO_PADDING) != 0)
		return EVP_EncryptUpdate(ctx, out, out_len, in, in_len);

	if (block_size > sizeof(ctx->final)) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}

	if (ctx->final_used) {
		/*
		 * final_used is only set if partial_len is 0. Therefore the
		 * output from EVP_EncryptUpdate() is in_len & ~block_mask.
		 * Ensure (in_len & ~block_mask) + block_size doesn't overflow.
		 */
		if ((in_len & ~block_mask) > INT_MAX - block_size) {
			EVPerror(EVP_R_TOO_LARGE);
			return 0;
		}
		memcpy(out, ctx->final, block_size);
		out += block_size;
		total_len = block_size;
	}

	ctx->final_used = 0;

	len = 0;
	if (!EVP_EncryptUpdate(ctx, out, &len, in, in_len))
		return 0;

	/* Keep copy of last block if a multiple of block_size was decrypted. */
	if (block_size > 1 && ctx->partial_len == 0) {
		if (len < block_size)
			return 0;
		len -= block_size;
		memcpy(ctx->final, &out[len], block_size);
		ctx->final_used = 1;
	}

	if (len > INT_MAX - total_len)
		return 0;
	total_len += len;

	*out_len = total_len;

	return 1;
}
LCRYPTO_ALIAS(EVP_DecryptUpdate);

int
EVP_DecryptFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len)
{
	return EVP_DecryptFinal_ex(ctx, out, out_len);
}
LCRYPTO_ALIAS(EVP_DecryptFinal);

int
EVP_DecryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *out_len)
{
	const int block_size = ctx->cipher->block_size;
	int partial_len = ctx->partial_len;
	int i, pad, plain_len;

	*out_len = 0;

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_CIPHER) != 0)
		return evp_cipher(ctx, out, out_len, NULL, 0);

	if ((ctx->flags & EVP_CIPH_NO_PADDING) != 0) {
		if (partial_len != 0) {
			EVPerror(EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH);
			return 0;
		}
		return 1;
	}

	if (block_size == 1)
		return 1;

	if (partial_len != 0 || !ctx->final_used) {
		EVPerror(EVP_R_WRONG_FINAL_BLOCK_LENGTH);
		return 0;
	}

	if (block_size > sizeof(ctx->final)) {
		EVPerror(EVP_R_BAD_BLOCK_LENGTH);
		return 0;
	}

	pad = ctx->final[block_size - 1];
	if (pad <= 0 || pad > block_size) {
		EVPerror(EVP_R_BAD_DECRYPT);
		return 0;
	}
	plain_len = block_size - pad;
	for (i = plain_len; i < block_size; i++) {
		if (ctx->final[i] != pad) {
			EVPerror(EVP_R_BAD_DECRYPT);
			return 0;
		}
	}

	memcpy(out, ctx->final, plain_len);
	*out_len = plain_len;

	return 1;
}
LCRYPTO_ALIAS(EVP_DecryptFinal_ex);

EVP_CIPHER_CTX *
EVP_CIPHER_CTX_new(void)
{
	return calloc(1, sizeof(EVP_CIPHER_CTX));
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_new);

void
EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx)
{
	if (ctx == NULL)
		return;

	EVP_CIPHER_CTX_cleanup(ctx);

	free(ctx);
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_free);

void
EVP_CIPHER_CTX_legacy_clear(EVP_CIPHER_CTX *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

int
EVP_CIPHER_CTX_init(EVP_CIPHER_CTX *ctx)
{
	return EVP_CIPHER_CTX_cleanup(ctx);
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_init);

int
EVP_CIPHER_CTX_reset(EVP_CIPHER_CTX *ctx)
{
	return EVP_CIPHER_CTX_cleanup(ctx);
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_reset);

int
EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *ctx)
{
	if (ctx == NULL)
		return 1;

	if (ctx->cipher != NULL) {
		/* XXX - Avoid leaks, so ignore return value of cleanup()... */
		if (ctx->cipher->cleanup != NULL)
			ctx->cipher->cleanup(ctx);
		if (ctx->cipher_data != NULL)
			explicit_bzero(ctx->cipher_data, ctx->cipher->ctx_size);
	}

	/* XXX - store size of cipher_data so we can always freezero(). */
	free(ctx->cipher_data);

	explicit_bzero(ctx, sizeof(EVP_CIPHER_CTX));

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_cleanup);

int
EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
{
	int ret;

	if (!ctx->cipher) {
		EVPerror(EVP_R_NO_CIPHER_SET);
		return 0;
	}

	if (!ctx->cipher->ctrl) {
		EVPerror(EVP_R_CTRL_NOT_IMPLEMENTED);
		return 0;
	}

	ret = ctx->cipher->ctrl(ctx, type, arg, ptr);
	if (ret == -1) {
		EVPerror(EVP_R_CTRL_OPERATION_NOT_IMPLEMENTED);
		return 0;
	}
	return ret;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_ctrl);

int
EVP_CIPHER_CTX_rand_key(EVP_CIPHER_CTX *ctx, unsigned char *key)
{
	if (ctx->cipher->flags & EVP_CIPH_RAND_KEY)
		return EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_RAND_KEY, 0, key);
	arc4random_buf(key, ctx->key_len);
	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_rand_key);

int
EVP_CIPHER_CTX_copy(EVP_CIPHER_CTX *out, const EVP_CIPHER_CTX *in)
{
	if (in == NULL || in->cipher == NULL) {
		EVPerror(EVP_R_INPUT_NOT_INITIALIZED);
		return 0;
	}

	EVP_CIPHER_CTX_cleanup(out);
	memcpy(out, in, sizeof *out);

	if (in->cipher_data && in->cipher->ctx_size) {
		out->cipher_data = calloc(1, in->cipher->ctx_size);
		if (out->cipher_data == NULL) {
			EVPerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		memcpy(out->cipher_data, in->cipher_data, in->cipher->ctx_size);
	}

	if (in->cipher->flags & EVP_CIPH_CUSTOM_COPY) {
		if (!in->cipher->ctrl((EVP_CIPHER_CTX *)in, EVP_CTRL_COPY,
		    0, out)) {
			/*
			 * If the custom copy control failed, assume that there
			 * may still be pointers copied in the cipher_data that
			 * we do not own. This may result in a leak from a bad
			 * custom copy control, but that's preferable to a
			 * double free...
			 */
			freezero(out->cipher_data, in->cipher->ctx_size);
			out->cipher_data = NULL;
			return 0;
		}
	}

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_copy);

/*
 * EVP_CIPHER_CTX accessors.
 */

const EVP_CIPHER *
EVP_CIPHER_CTX_cipher(const EVP_CIPHER_CTX *ctx)
{
	return ctx->cipher;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_cipher);

int
EVP_CIPHER_CTX_encrypting(const EVP_CIPHER_CTX *ctx)
{
	return ctx->encrypt;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_encrypting);

int
EVP_CIPHER_CTX_get_iv(const EVP_CIPHER_CTX *ctx, unsigned char *iv, size_t len)
{
	if (ctx == NULL || len != EVP_CIPHER_CTX_iv_length(ctx))
		return 0;
	if (len > EVP_MAX_IV_LENGTH)
		return 0; /* sanity check; shouldn't happen */
	/*
	 * Skip the memcpy entirely when the requested IV length is zero,
	 * since the iv pointer may be NULL or invalid.
	 */
	if (len != 0) {
		if (iv == NULL)
			return 0;
		memcpy(iv, ctx->iv, len);
	}
	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_get_iv);

int
EVP_CIPHER_CTX_set_iv(EVP_CIPHER_CTX *ctx, const unsigned char *iv, size_t len)
{
	if (ctx == NULL || len != EVP_CIPHER_CTX_iv_length(ctx))
		return 0;
	if (len > EVP_MAX_IV_LENGTH)
		return 0; /* sanity check; shouldn't happen */
	/*
	 * Skip the memcpy entirely when the requested IV length is zero,
	 * since the iv pointer may be NULL or invalid.
	 */
	if (len != 0) {
		if (iv == NULL)
			return 0;
		memcpy(ctx->iv, iv, len);
	}
	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_set_iv);

unsigned char *
EVP_CIPHER_CTX_buf_noconst(EVP_CIPHER_CTX *ctx)
{
	return ctx->buf;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_buf_noconst);

void *
EVP_CIPHER_CTX_get_app_data(const EVP_CIPHER_CTX *ctx)
{
	return ctx->app_data;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_get_app_data);

void
EVP_CIPHER_CTX_set_app_data(EVP_CIPHER_CTX *ctx, void *data)
{
	ctx->app_data = data;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_set_app_data);

int
EVP_CIPHER_CTX_key_length(const EVP_CIPHER_CTX *ctx)
{
	return ctx->key_len;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_key_length);

int
EVP_CIPHER_CTX_set_key_length(EVP_CIPHER_CTX *ctx, int key_len)
{
	if (ctx->key_len == key_len)
		return 1;
	if (key_len > 0 && (ctx->cipher->flags & EVP_CIPH_VARIABLE_LENGTH)) {
		ctx->key_len = key_len;
		return 1;
	}
	EVPerror(EVP_R_INVALID_KEY_LENGTH);
	return 0;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_set_key_length);

int
EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *ctx, int pad)
{
	if (pad)
		ctx->flags &= ~EVP_CIPH_NO_PADDING;
	else
		ctx->flags |= EVP_CIPH_NO_PADDING;
	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_set_padding);

void
EVP_CIPHER_CTX_set_flags(EVP_CIPHER_CTX *ctx, int flags)
{
	ctx->flags |= flags;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_set_flags);

void
EVP_CIPHER_CTX_clear_flags(EVP_CIPHER_CTX *ctx, int flags)
{
	ctx->flags &= ~flags;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_clear_flags);

int
EVP_CIPHER_CTX_test_flags(const EVP_CIPHER_CTX *ctx, int flags)
{
	return (ctx->flags & flags);
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_test_flags);

void *
EVP_CIPHER_CTX_get_cipher_data(const EVP_CIPHER_CTX *ctx)
{
	return ctx->cipher_data;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_get_cipher_data);

void *
EVP_CIPHER_CTX_set_cipher_data(EVP_CIPHER_CTX *ctx, void *cipher_data)
{
	void *old_cipher_data;

	old_cipher_data = ctx->cipher_data;
	ctx->cipher_data = cipher_data;

	return old_cipher_data;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_set_cipher_data);

/*
 * EVP_CIPHER_CTX getters that reach into the cipher attached to the context.
 */

int
EVP_CIPHER_CTX_nid(const EVP_CIPHER_CTX *ctx)
{
	return ctx->cipher->nid;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_nid);

int
EVP_CIPHER_CTX_block_size(const EVP_CIPHER_CTX *ctx)
{
	return ctx->cipher->block_size;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_block_size);

int
EVP_CIPHER_CTX_iv_length(const EVP_CIPHER_CTX *ctx)
{
	int iv_length = 0;

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_CUSTOM_IV_LENGTH) == 0)
		return ctx->cipher->iv_len;

	/*
	 * XXX - sanity would suggest to pass the size of the pointer along,
	 * but unfortunately we have to match the other crowd.
	 */
	if (EVP_CIPHER_CTX_ctrl((EVP_CIPHER_CTX *)ctx, EVP_CTRL_GET_IVLEN, 0,
	    &iv_length) != 1)
		return -1;

	return iv_length;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_iv_length);

unsigned long
EVP_CIPHER_CTX_flags(const EVP_CIPHER_CTX *ctx)
{
	return ctx->cipher->flags;
}
LCRYPTO_ALIAS(EVP_CIPHER_CTX_flags);

/*
 * Used by CMS and its predecessors. Only RC2 has a custom method.
 */

int
EVP_CIPHER_asn1_to_param(EVP_CIPHER_CTX *ctx, ASN1_TYPE *type)
{
	int iv_len;

	if (ctx->cipher->get_asn1_parameters != NULL)
		return ctx->cipher->get_asn1_parameters(ctx, type);

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_DEFAULT_ASN1) == 0)
		return -1;

	if (type == NULL)
		return 0;

	iv_len = EVP_CIPHER_CTX_iv_length(ctx);
	if (iv_len < 0 || iv_len > sizeof(ctx->oiv) || iv_len > sizeof(ctx->iv)) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0; /* XXX */
	}
	if (ASN1_TYPE_get_octetstring(type, ctx->oiv, iv_len) != iv_len)
		return -1;

	if (iv_len > 0)
		memcpy(ctx->iv, ctx->oiv, iv_len);

	return iv_len;
}

int
EVP_CIPHER_param_to_asn1(EVP_CIPHER_CTX *ctx, ASN1_TYPE *type)
{
	int iv_len;

	if (ctx->cipher->set_asn1_parameters != NULL)
		return ctx->cipher->set_asn1_parameters(ctx, type);

	if ((ctx->cipher->flags & EVP_CIPH_FLAG_DEFAULT_ASN1) == 0)
		return -1;

	if (type == NULL)
		return 0;

	iv_len = EVP_CIPHER_CTX_iv_length(ctx);
	if (iv_len < 0 || iv_len > sizeof(ctx->oiv)) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0;
	}

	return ASN1_TYPE_set_octetstring(type, ctx->oiv, iv_len);
}

/* Convert the various cipher NIDs and dummies to a proper OID NID */
int
EVP_CIPHER_type(const EVP_CIPHER *cipher)
{
	ASN1_OBJECT *aobj;
	int nid;

	nid = EVP_CIPHER_nid(cipher);
	switch (nid) {
	case NID_rc2_cbc:
	case NID_rc2_64_cbc:
	case NID_rc2_40_cbc:
		return NID_rc2_cbc;

	case NID_rc4:
	case NID_rc4_40:
		return NID_rc4;

	case NID_aes_128_cfb128:
	case NID_aes_128_cfb8:
	case NID_aes_128_cfb1:
		return NID_aes_128_cfb128;

	case NID_aes_192_cfb128:
	case NID_aes_192_cfb8:
	case NID_aes_192_cfb1:
		return NID_aes_192_cfb128;

	case NID_aes_256_cfb128:
	case NID_aes_256_cfb8:
	case NID_aes_256_cfb1:
		return NID_aes_256_cfb128;

	case NID_des_cfb64:
	case NID_des_cfb8:
	case NID_des_cfb1:
		return NID_des_cfb64;

	case NID_des_ede3_cfb64:
	case NID_des_ede3_cfb8:
	case NID_des_ede3_cfb1:
		return NID_des_cfb64;

	default:
		/* Check it has an OID and it is valid */
		if (((aobj = OBJ_nid2obj(nid)) == NULL) || aobj->data == NULL)
			nid = NID_undef;

		ASN1_OBJECT_free(aobj);

		return nid;
	}
}
LCRYPTO_ALIAS(EVP_CIPHER_type);

/*
 * Accessors. First the trivial getters, then the setters for the method API.
 */

int
EVP_CIPHER_nid(const EVP_CIPHER *cipher)
{
	return cipher->nid;
}
LCRYPTO_ALIAS(EVP_CIPHER_nid);

int
EVP_CIPHER_block_size(const EVP_CIPHER *cipher)
{
	return cipher->block_size;
}
LCRYPTO_ALIAS(EVP_CIPHER_block_size);

int
EVP_CIPHER_key_length(const EVP_CIPHER *cipher)
{
	return cipher->key_len;
}
LCRYPTO_ALIAS(EVP_CIPHER_key_length);

int
EVP_CIPHER_iv_length(const EVP_CIPHER *cipher)
{
	return cipher->iv_len;
}
LCRYPTO_ALIAS(EVP_CIPHER_iv_length);

unsigned long
EVP_CIPHER_flags(const EVP_CIPHER *cipher)
{
	return cipher->flags;
}
LCRYPTO_ALIAS(EVP_CIPHER_flags);

EVP_CIPHER *
EVP_CIPHER_meth_new(int cipher_type, int block_size, int key_len)
{
	EVP_CIPHER *cipher;

	if (cipher_type < 0 || key_len < 0)
		return NULL;

	/* EVP_CipherInit() will fail for any other value. */
	if (block_size != 1 && block_size != 8 && block_size != 16)
		return NULL;

	if ((cipher = calloc(1, sizeof(*cipher))) == NULL)
		return NULL;

	cipher->nid = cipher_type;
	cipher->block_size = block_size;
	cipher->key_len = key_len;

	return cipher;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_new);

EVP_CIPHER *
EVP_CIPHER_meth_dup(const EVP_CIPHER *cipher)
{
	EVP_CIPHER *copy;

	if ((copy = calloc(1, sizeof(*copy))) == NULL)
		return NULL;

	*copy = *cipher;

	return copy;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_dup);

void
EVP_CIPHER_meth_free(EVP_CIPHER *cipher)
{
	free(cipher);
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_free);

int
EVP_CIPHER_meth_set_iv_length(EVP_CIPHER *cipher, int iv_len)
{
	cipher->iv_len = iv_len;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_iv_length);

int
EVP_CIPHER_meth_set_flags(EVP_CIPHER *cipher, unsigned long flags)
{
	cipher->flags = flags;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_flags);

int
EVP_CIPHER_meth_set_impl_ctx_size(EVP_CIPHER *cipher, int ctx_size)
{
	cipher->ctx_size = ctx_size;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_impl_ctx_size);

int
EVP_CIPHER_meth_set_init(EVP_CIPHER *cipher,
    int (*init)(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc))
{
	cipher->init = init;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_init);

int
EVP_CIPHER_meth_set_do_cipher(EVP_CIPHER *cipher,
    int (*do_cipher)(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl))
{
	cipher->do_cipher = do_cipher;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_do_cipher);

int
EVP_CIPHER_meth_set_cleanup(EVP_CIPHER *cipher,
    int (*cleanup)(EVP_CIPHER_CTX *))
{
	cipher->cleanup = cleanup;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_cleanup);

int
EVP_CIPHER_meth_set_set_asn1_params(EVP_CIPHER *cipher,
    int (*set_asn1_parameters)(EVP_CIPHER_CTX *, ASN1_TYPE *))
{
	cipher->set_asn1_parameters = set_asn1_parameters;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_set_asn1_params);

int
EVP_CIPHER_meth_set_get_asn1_params(EVP_CIPHER *cipher,
    int (*get_asn1_parameters)(EVP_CIPHER_CTX *, ASN1_TYPE *))
{
	cipher->get_asn1_parameters = get_asn1_parameters;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_get_asn1_params);

int
EVP_CIPHER_meth_set_ctrl(EVP_CIPHER *cipher,
    int (*ctrl)(EVP_CIPHER_CTX *, int type, int arg, void *ptr))
{
	cipher->ctrl = ctrl;

	return 1;
}
LCRYPTO_ALIAS(EVP_CIPHER_meth_set_ctrl);
