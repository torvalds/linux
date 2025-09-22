/*	$OpenBSD: rsa_meth.c,v 1.8 2025/05/10 05:54:38 tb Exp $	*/
/*
 * Copyright (c) 2018 Theo Buehler <tb@openbsd.org>
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

#include <openssl/rsa.h>

#include "rsa_local.h"

RSA_METHOD *
RSA_meth_new(const char *name, int flags)
{
	RSA_METHOD *meth;

	if ((meth = calloc(1, sizeof(*meth))) == NULL)
		return NULL;
	if ((meth->name = strdup(name)) == NULL) {
		free(meth);
		return NULL;
	}
	meth->flags = flags;

	return meth;
}
LCRYPTO_ALIAS(RSA_meth_new);

void
RSA_meth_free(RSA_METHOD *meth)
{
	if (meth == NULL)
		return;

	free(meth->name);
	free(meth);
}
LCRYPTO_ALIAS(RSA_meth_free);

RSA_METHOD *
RSA_meth_dup(const RSA_METHOD *meth)
{
	RSA_METHOD *copy;

	if ((copy = calloc(1, sizeof(*copy))) == NULL)
		return NULL;
	memcpy(copy, meth, sizeof(*copy));
	if ((copy->name = strdup(meth->name)) == NULL) {
		free(copy);
		return NULL;
	}

	return copy;
}
LCRYPTO_ALIAS(RSA_meth_dup);

int
RSA_meth_set1_name(RSA_METHOD *meth, const char *name)
{
	char *new_name;

	if ((new_name = strdup(name)) == NULL)
		return 0;
	free(meth->name);
	meth->name = new_name;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set1_name);

int
(*RSA_meth_get_finish(const RSA_METHOD *meth))(RSA *rsa)
{
	return meth->finish;
}
LCRYPTO_ALIAS(RSA_meth_get_finish);

int
RSA_meth_set_priv_enc(RSA_METHOD *meth, int (*priv_enc)(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding))
{
	meth->rsa_priv_enc = priv_enc;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_priv_enc);

int
RSA_meth_set_priv_dec(RSA_METHOD *meth, int (*priv_dec)(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding))
{
	meth->rsa_priv_dec = priv_dec;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_priv_dec);

int
RSA_meth_set_finish(RSA_METHOD *meth, int (*finish)(RSA *rsa))
{
	meth->finish = finish;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_finish);

int
RSA_meth_set_pub_enc(RSA_METHOD *meth, int (*pub_enc)(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding))
{
	meth->rsa_pub_enc = pub_enc;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_pub_enc);

int
RSA_meth_set_pub_dec(RSA_METHOD *meth, int (*pub_dec)(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding))
{
	meth->rsa_pub_dec = pub_dec;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_pub_dec);

int
RSA_meth_set_mod_exp(RSA_METHOD *meth, int (*mod_exp)(BIGNUM *r0,
    const BIGNUM *i, RSA *rsa, BN_CTX *ctx))
{
	meth->rsa_mod_exp = mod_exp;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_mod_exp);

int
RSA_meth_set_bn_mod_exp(RSA_METHOD *meth, int (*bn_mod_exp)(BIGNUM *r,
    const BIGNUM *a, const BIGNUM *p, const BIGNUM *m, BN_CTX *ctx,
	BN_MONT_CTX *m_ctx))
{
	meth->bn_mod_exp = bn_mod_exp;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_bn_mod_exp);

int
RSA_meth_set_init(RSA_METHOD *meth, int (*init)(RSA *rsa))
{
	meth->init = init;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_init);

int
RSA_meth_set_keygen(RSA_METHOD *meth, int (*keygen)(RSA *rsa, int bits,
    BIGNUM *e, BN_GENCB *cb))
{
	meth->rsa_keygen = keygen;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_keygen);

int
RSA_meth_set_flags(RSA_METHOD *meth, int flags)
{
	meth->flags = flags;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_flags);

int
RSA_meth_set0_app_data(RSA_METHOD *meth, void *app_data)
{
	meth->app_data = app_data;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set0_app_data);

const char *
RSA_meth_get0_name(const RSA_METHOD *meth)
{
	return meth->name;
}
LCRYPTO_ALIAS(RSA_meth_get0_name);

int
(*RSA_meth_get_pub_enc(const RSA_METHOD *meth))(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
	return meth->rsa_pub_enc;
}
LCRYPTO_ALIAS(RSA_meth_get_pub_enc);

int
(*RSA_meth_get_pub_dec(const RSA_METHOD *meth))(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
	return meth->rsa_pub_dec;
}
LCRYPTO_ALIAS(RSA_meth_get_pub_dec);

int
(*RSA_meth_get_priv_enc(const RSA_METHOD *meth))(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
	return meth->rsa_priv_enc;
}
LCRYPTO_ALIAS(RSA_meth_get_priv_enc);

int
(*RSA_meth_get_priv_dec(const RSA_METHOD *meth))(int flen,
    const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
{
	return meth->rsa_priv_dec;
}
LCRYPTO_ALIAS(RSA_meth_get_priv_dec);

int
(*RSA_meth_get_mod_exp(const RSA_METHOD *meth))(BIGNUM *r0, const BIGNUM *i,
    RSA *rsa, BN_CTX *ctx)
{
	return meth->rsa_mod_exp;
}
LCRYPTO_ALIAS(RSA_meth_get_mod_exp);

int
(*RSA_meth_get_bn_mod_exp(const RSA_METHOD *meth))(BIGNUM *r,
    const BIGNUM *a, const BIGNUM *p, const BIGNUM *m, BN_CTX *ctx,
    BN_MONT_CTX *m_ctx)
{
	return meth->bn_mod_exp;
}
LCRYPTO_ALIAS(RSA_meth_get_bn_mod_exp);

int
(*RSA_meth_get_init(const RSA_METHOD *meth))(RSA *rsa)
{
	return meth->init;
}
LCRYPTO_ALIAS(RSA_meth_get_init);

int
(*RSA_meth_get_keygen(const RSA_METHOD *meth))(RSA *rsa, int bits, BIGNUM *e,
    BN_GENCB *cb)
{
	return meth->rsa_keygen;
}
LCRYPTO_ALIAS(RSA_meth_get_keygen);

int
RSA_meth_get_flags(const RSA_METHOD *meth)
{
	return meth->flags;
}
LCRYPTO_ALIAS(RSA_meth_get_flags);

void *
RSA_meth_get0_app_data(const RSA_METHOD *meth)
{
	return meth->app_data;
}
LCRYPTO_ALIAS(RSA_meth_get0_app_data);

int
(*RSA_meth_get_sign(const RSA_METHOD *meth))(int type,
    const unsigned char *m, unsigned int m_length,
    unsigned char *sigret, unsigned int *siglen,
    const RSA *rsa)
{
	return meth->rsa_sign;
}
LCRYPTO_ALIAS(RSA_meth_get_sign);

int
RSA_meth_set_sign(RSA_METHOD *meth, int (*sign)(int type,
    const unsigned char *m, unsigned int m_length, unsigned char *sigret,
    unsigned int *siglen, const RSA *rsa))
{
	meth->rsa_sign = sign;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_sign);

int
(*RSA_meth_get_verify(const RSA_METHOD *meth))(int dtype,
    const unsigned char *m, unsigned int m_length, const unsigned char *sigbuf,
    unsigned int siglen, const RSA *rsa)
{
	return meth->rsa_verify;
}
LCRYPTO_ALIAS(RSA_meth_get_verify);

int
RSA_meth_set_verify(RSA_METHOD *meth, int (*verify)(int dtype,
    const unsigned char *m, unsigned int m_length, const unsigned char *sigbuf,
    unsigned int siglen, const RSA *rsa))
{
	meth->rsa_verify = verify;
	return 1;
}
LCRYPTO_ALIAS(RSA_meth_set_verify);
