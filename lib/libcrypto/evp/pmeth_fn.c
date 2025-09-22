/* $OpenBSD: pmeth_fn.c,v 1.12 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>

#include <openssl/evp.h>
#include <openssl/objects.h>

#include "err_local.h"
#include "evp_local.h"

#define M_check_autoarg(ctx, arg, arglen, err) \
	if (ctx->pmeth->flags & EVP_PKEY_FLAG_AUTOARGLEN) \
		{ \
		size_t pksize = (size_t)EVP_PKEY_size(ctx->pkey); \
		if (!arg) \
			{ \
			*arglen = pksize; \
			return 1; \
			} \
		else if (*arglen < pksize) \
			{ \
			EVPerror(EVP_R_BUFFER_TOO_SMALL); /*ckerr_ignore*/\
			return 0; \
			} \
		}

int
EVP_PKEY_sign_init(EVP_PKEY_CTX *ctx)
{
	int ret;

	if (!ctx || !ctx->pmeth || !ctx->pmeth->sign) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	ctx->operation = EVP_PKEY_OP_SIGN;
	if (!ctx->pmeth->sign_init)
		return 1;
	ret = ctx->pmeth->sign_init(ctx);
	if (ret <= 0)
		ctx->operation = EVP_PKEY_OP_UNDEFINED;
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_sign_init);

int
EVP_PKEY_sign(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
    const unsigned char *tbs, size_t tbslen)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->sign) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_SIGN) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}
	M_check_autoarg(ctx, sig, siglen, EVP_F_EVP_PKEY_SIGN)
	return ctx->pmeth->sign(ctx, sig, siglen, tbs, tbslen);
}
LCRYPTO_ALIAS(EVP_PKEY_sign);

int
EVP_PKEY_verify_init(EVP_PKEY_CTX *ctx)
{
	int ret;

	if (!ctx || !ctx->pmeth || !ctx->pmeth->verify) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	ctx->operation = EVP_PKEY_OP_VERIFY;
	if (!ctx->pmeth->verify_init)
		return 1;
	ret = ctx->pmeth->verify_init(ctx);
	if (ret <= 0)
		ctx->operation = EVP_PKEY_OP_UNDEFINED;
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_verify_init);

int
EVP_PKEY_verify(EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->verify) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_VERIFY) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}
	return ctx->pmeth->verify(ctx, sig, siglen, tbs, tbslen);
}
LCRYPTO_ALIAS(EVP_PKEY_verify);

int
EVP_PKEY_verify_recover_init(EVP_PKEY_CTX *ctx)
{
	if (ctx == NULL || ctx->pmeth == NULL ||
	    ctx->pmeth->verify_recover == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}

	ctx->operation = EVP_PKEY_OP_VERIFYRECOVER;

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_verify_recover_init);

int
EVP_PKEY_verify_recover(EVP_PKEY_CTX *ctx, unsigned char *rout, size_t *routlen,
    const unsigned char *sig, size_t siglen)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->verify_recover) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_VERIFYRECOVER) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}
	M_check_autoarg(ctx, rout, routlen, EVP_F_EVP_PKEY_VERIFY_RECOVER)
	return ctx->pmeth->verify_recover(ctx, rout, routlen, sig, siglen);
}
LCRYPTO_ALIAS(EVP_PKEY_verify_recover);

int
EVP_PKEY_encrypt_init(EVP_PKEY_CTX *ctx)
{
	if (ctx == NULL || ctx->pmeth == NULL || ctx->pmeth->encrypt == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}

	ctx->operation = EVP_PKEY_OP_ENCRYPT;

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_encrypt_init);

int
EVP_PKEY_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->encrypt) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_ENCRYPT) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}
	M_check_autoarg(ctx, out, outlen, EVP_F_EVP_PKEY_ENCRYPT)
	return ctx->pmeth->encrypt(ctx, out, outlen, in, inlen);
}
LCRYPTO_ALIAS(EVP_PKEY_encrypt);

int
EVP_PKEY_decrypt_init(EVP_PKEY_CTX *ctx)
{
	if (ctx == NULL || ctx->pmeth == NULL || ctx->pmeth->decrypt == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}

	ctx->operation = EVP_PKEY_OP_DECRYPT;

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_decrypt_init);

int
EVP_PKEY_decrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->decrypt) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_DECRYPT) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}
	M_check_autoarg(ctx, out, outlen, EVP_F_EVP_PKEY_DECRYPT)
	return ctx->pmeth->decrypt(ctx, out, outlen, in, inlen);
}
LCRYPTO_ALIAS(EVP_PKEY_decrypt);

int
EVP_PKEY_derive_init(EVP_PKEY_CTX *ctx)
{
	int ret;

	if (!ctx || !ctx->pmeth || !ctx->pmeth->derive) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	ctx->operation = EVP_PKEY_OP_DERIVE;
	if (!ctx->pmeth->derive_init)
		return 1;
	ret = ctx->pmeth->derive_init(ctx);
	if (ret <= 0)
		ctx->operation = EVP_PKEY_OP_UNDEFINED;
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_derive_init);

int
EVP_PKEY_derive_set_peer(EVP_PKEY_CTX *ctx, EVP_PKEY *peer)
{
	int ret;

	if (!ctx || !ctx->pmeth || !(ctx->pmeth->derive ||
	    ctx->pmeth->encrypt || ctx->pmeth->decrypt) ||
	    !ctx->pmeth->ctrl) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_DERIVE &&
	    ctx->operation != EVP_PKEY_OP_ENCRYPT &&
	    ctx->operation != EVP_PKEY_OP_DECRYPT) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}

	ret = ctx->pmeth->ctrl(ctx, EVP_PKEY_CTRL_PEER_KEY, 0, peer);

	if (ret <= 0)
		return ret;

	if (ret == 2)
		return 1;

	if (!ctx->pkey) {
		EVPerror(EVP_R_NO_KEY_SET);
		return -1;
	}

	if (ctx->pkey->type != peer->type) {
		EVPerror(EVP_R_DIFFERENT_KEY_TYPES);
		return -1;
	}

	/* ran@cryptocom.ru: For clarity.  The error is if parameters in peer are
	 * present (!missing) but don't match.  EVP_PKEY_cmp_parameters may return
	 * 1 (match), 0 (don't match) and -2 (comparison is not defined).  -1
	 * (different key types) is impossible here because it is checked earlier.
	 * -2 is OK for us here, as well as 1, so we can check for 0 only. */
	if (!EVP_PKEY_missing_parameters(peer) &&
	    !EVP_PKEY_cmp_parameters(ctx->pkey, peer)) {
		EVPerror(EVP_R_DIFFERENT_PARAMETERS);
		return -1;
	}

	EVP_PKEY_free(ctx->peerkey);
	ctx->peerkey = peer;

	ret = ctx->pmeth->ctrl(ctx, EVP_PKEY_CTRL_PEER_KEY, 1, peer);

	if (ret <= 0) {
		ctx->peerkey = NULL;
		return ret;
	}

	CRYPTO_add(&peer->references, 1, CRYPTO_LOCK_EVP_PKEY);
	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_derive_set_peer);

int
EVP_PKEY_derive(EVP_PKEY_CTX *ctx, unsigned char *key, size_t *pkeylen)
{
	if (!ctx || !ctx->pmeth || !ctx->pmeth->derive) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	}
	if (ctx->operation != EVP_PKEY_OP_DERIVE) {
		EVPerror(EVP_R_OPERATON_NOT_INITIALIZED);
		return -1;
	}
	M_check_autoarg(ctx, key, pkeylen, EVP_F_EVP_PKEY_DERIVE)
	return ctx->pmeth->derive(ctx, key, pkeylen);
}
LCRYPTO_ALIAS(EVP_PKEY_derive);
