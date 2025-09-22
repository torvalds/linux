/* $OpenBSD: x_pubkey.c,v 1.38 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1t.h>
#include <openssl/x509.h>

#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

/* Minor tweak to operation: free up EVP_PKEY */
static int
pubkey_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_FREE_POST) {
		X509_PUBKEY *pubkey = (X509_PUBKEY *)*pval;
		EVP_PKEY_free(pubkey->pkey);
	}
	return 1;
}

static const ASN1_AUX X509_PUBKEY_aux = {
	.asn1_cb = pubkey_cb,
};
static const ASN1_TEMPLATE X509_PUBKEY_seq_tt[] = {
	{
		.offset = offsetof(X509_PUBKEY, algor),
		.field_name = "algor",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(X509_PUBKEY, public_key),
		.field_name = "public_key",
		.item = &ASN1_BIT_STRING_it,
	},
};

const ASN1_ITEM X509_PUBKEY_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_PUBKEY_seq_tt,
	.tcount = sizeof(X509_PUBKEY_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_PUBKEY_aux,
	.size = sizeof(X509_PUBKEY),
	.sname = "X509_PUBKEY",
};
LCRYPTO_ALIAS(X509_PUBKEY_it);

X509_PUBKEY *
d2i_X509_PUBKEY(X509_PUBKEY **a, const unsigned char **in, long len)
{
	return (X509_PUBKEY *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_PUBKEY_it);
}
LCRYPTO_ALIAS(d2i_X509_PUBKEY);

int
i2d_X509_PUBKEY(X509_PUBKEY *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_PUBKEY_it);
}
LCRYPTO_ALIAS(i2d_X509_PUBKEY);

X509_PUBKEY *
X509_PUBKEY_new(void)
{
	return (X509_PUBKEY *)ASN1_item_new(&X509_PUBKEY_it);
}
LCRYPTO_ALIAS(X509_PUBKEY_new);

void
X509_PUBKEY_free(X509_PUBKEY *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_PUBKEY_it);
}
LCRYPTO_ALIAS(X509_PUBKEY_free);

int
X509_PUBKEY_set(X509_PUBKEY **x, EVP_PKEY *pkey)
{
	X509_PUBKEY *pk = NULL;

	if (x == NULL)
		return (0);
	if ((pk = X509_PUBKEY_new()) == NULL)
		goto error;

	if (pkey->ameth) {
		if (pkey->ameth->pub_encode) {
			if (!pkey->ameth->pub_encode(pk, pkey)) {
				X509error(X509_R_PUBLIC_KEY_ENCODE_ERROR);
				goto error;
			}
		} else {
			X509error(X509_R_METHOD_NOT_SUPPORTED);
			goto error;
		}
	} else {
		X509error(X509_R_UNSUPPORTED_ALGORITHM);
		goto error;
	}

	if (*x != NULL)
		X509_PUBKEY_free(*x);

	*x = pk;

	return 1;

 error:
	if (pk != NULL)
		X509_PUBKEY_free(pk);
	return 0;
}
LCRYPTO_ALIAS(X509_PUBKEY_set);

EVP_PKEY *
X509_PUBKEY_get0(X509_PUBKEY *key)
{
	EVP_PKEY *ret = NULL;

	if (key == NULL)
		goto error;

	if (key->pkey != NULL)
		return key->pkey;

	if (key->public_key == NULL)
		goto error;

	if ((ret = EVP_PKEY_new()) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto error;
	}

	if (!EVP_PKEY_set_type(ret, OBJ_obj2nid(key->algor->algorithm))) {
		X509error(X509_R_UNSUPPORTED_ALGORITHM);
		goto error;
	}

	if (ret->ameth->pub_decode) {
		if (!ret->ameth->pub_decode(ret, key)) {
			X509error(X509_R_PUBLIC_KEY_DECODE_ERROR);
			goto error;
		}
	} else {
		X509error(X509_R_METHOD_NOT_SUPPORTED);
		goto error;
	}

	/* Check to see if another thread set key->pkey first */
	CRYPTO_w_lock(CRYPTO_LOCK_EVP_PKEY);
	if (key->pkey) {
		CRYPTO_w_unlock(CRYPTO_LOCK_EVP_PKEY);
		EVP_PKEY_free(ret);
		ret = key->pkey;
	} else {
		key->pkey = ret;
		CRYPTO_w_unlock(CRYPTO_LOCK_EVP_PKEY);
	}

	return ret;

 error:
	EVP_PKEY_free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(X509_PUBKEY_get0);

EVP_PKEY *
X509_PUBKEY_get(X509_PUBKEY *key)
{
	EVP_PKEY *pkey;

	if ((pkey = X509_PUBKEY_get0(key)) == NULL)
		return (NULL);

	CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY);

	return pkey;
}
LCRYPTO_ALIAS(X509_PUBKEY_get);

/*
 * Decode an X509_PUBKEY into the specified key type.
 */
static int
pubkey_ex_d2i(int pkey_type, ASN1_VALUE **pval, const unsigned char **in,
    long len, const ASN1_ITEM *it)
{
	const ASN1_EXTERN_FUNCS *ef = it->funcs;
	const unsigned char *p = *in;
	X509_PUBKEY *xpk = NULL;
	ASN1_VALUE *key = NULL;
	EVP_PKEY *pkey = NULL;
	int ret = 0;

	if ((xpk = d2i_X509_PUBKEY(NULL, &p, len)) == NULL)
		goto err;
	if ((pkey = X509_PUBKEY_get(xpk)) == NULL)
		goto err;

	switch (pkey_type) {
	case EVP_PKEY_NONE:
		key = (ASN1_VALUE *)pkey;
		pkey = NULL;
		break;

	case EVP_PKEY_DSA:
		key = (ASN1_VALUE *)EVP_PKEY_get1_DSA(pkey);
		break;

	case EVP_PKEY_RSA:
		key = (ASN1_VALUE *)EVP_PKEY_get1_RSA(pkey);
		break;

	case EVP_PKEY_EC:
		key = (ASN1_VALUE *)EVP_PKEY_get1_EC_KEY(pkey);
		break;

	default:
		goto err;
	}

	if (key == NULL)
		goto err;

	ef->asn1_ex_free(pval, it);

	*pval = key;
	*in = p;
	ret = 1;

 err:
	EVP_PKEY_free(pkey);
	X509_PUBKEY_free(xpk);

	return ret;
}

/*
 * Encode the specified key type into an X509_PUBKEY.
 */
static int
pubkey_ex_i2d(int pkey_type, ASN1_VALUE **pval, unsigned char **out,
    const ASN1_ITEM *it)
{
	X509_PUBKEY *xpk = NULL;
	EVP_PKEY *pkey, *pktmp;
	int ret = -1;

	if ((pkey = pktmp = EVP_PKEY_new()) == NULL)
		goto err;

	switch (pkey_type) {
	case EVP_PKEY_NONE:
		pkey = (EVP_PKEY *)*pval;
		break;

	case EVP_PKEY_DSA:
		if (!EVP_PKEY_set1_DSA(pkey, (DSA *)*pval))
			goto err;
		break;

	case EVP_PKEY_RSA:
		if (!EVP_PKEY_set1_RSA(pkey, (RSA *)*pval))
			goto err;
		break;

	case EVP_PKEY_EC:
		if (!EVP_PKEY_set1_EC_KEY(pkey, (EC_KEY*)*pval))
			goto err;
		break;

	default:
		goto err;
	}

	if (!X509_PUBKEY_set(&xpk, pkey))
		goto err;

	ret = i2d_X509_PUBKEY(xpk, out);

 err:
	EVP_PKEY_free(pktmp);
	X509_PUBKEY_free(xpk);

	return ret;
}

static int
pkey_pubkey_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	if ((*pval = (ASN1_VALUE *)EVP_PKEY_new()) == NULL)
		return 0;

	return 1;
}

static void
pkey_pubkey_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	EVP_PKEY_free((EVP_PKEY *)*pval);
	*pval = NULL;
}

static int
pkey_pubkey_ex_d2i(ASN1_VALUE **pval, const unsigned char **in, long len,
    const ASN1_ITEM *it, int tag, int aclass, char opt, ASN1_TLC *ctx)
{
	return pubkey_ex_d2i(EVP_PKEY_NONE, pval, in, len, it);
}

static int
pkey_pubkey_ex_i2d(ASN1_VALUE **pval, unsigned char **out, const ASN1_ITEM *it,
    int tag, int aclass)
{
	return pubkey_ex_i2d(EVP_PKEY_NONE, pval, out, it);
}

const ASN1_EXTERN_FUNCS pkey_pubkey_asn1_ff = {
	.app_data = NULL,
	.asn1_ex_new = pkey_pubkey_ex_new,
	.asn1_ex_free = pkey_pubkey_ex_free,
	.asn1_ex_clear = NULL,
	.asn1_ex_d2i = pkey_pubkey_ex_d2i,
	.asn1_ex_i2d = pkey_pubkey_ex_i2d,
	.asn1_ex_print = NULL,
};

const ASN1_ITEM EVP_PKEY_PUBKEY_it = {
	.itype = ASN1_ITYPE_EXTERN,
	.utype = 0,
	.templates = NULL,
	.tcount = 0,
	.funcs = &pkey_pubkey_asn1_ff,
	.size = 0,
	.sname = NULL,
};

EVP_PKEY *
d2i_PUBKEY(EVP_PKEY **pkey, const unsigned char **in, long len)
{
	return (EVP_PKEY *)ASN1_item_d2i((ASN1_VALUE **)pkey, in, len,
	    &EVP_PKEY_PUBKEY_it);
}
LCRYPTO_ALIAS(d2i_PUBKEY);

int
i2d_PUBKEY(EVP_PKEY *pkey, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)pkey, out, &EVP_PKEY_PUBKEY_it);
}
LCRYPTO_ALIAS(i2d_PUBKEY);

EVP_PKEY *
d2i_PUBKEY_bio(BIO *bp, EVP_PKEY **pkey)
{
	return (EVP_PKEY *)ASN1_item_d2i_bio(&EVP_PKEY_PUBKEY_it, bp,
	    (ASN1_VALUE **)pkey);
}
LCRYPTO_ALIAS(d2i_PUBKEY_bio);

int
i2d_PUBKEY_bio(BIO *bp, EVP_PKEY *pkey)
{
	return ASN1_item_i2d_bio(&EVP_PKEY_PUBKEY_it, bp, (ASN1_VALUE *)pkey);
}
LCRYPTO_ALIAS(i2d_PUBKEY_bio);

EVP_PKEY *
d2i_PUBKEY_fp(FILE *fp, EVP_PKEY **pkey)
{
	return (EVP_PKEY *)ASN1_item_d2i_fp(&EVP_PKEY_PUBKEY_it, fp,
	    (ASN1_VALUE **)pkey);
}
LCRYPTO_ALIAS(d2i_PUBKEY_fp);

int
i2d_PUBKEY_fp(FILE *fp, EVP_PKEY *pkey)
{
	return ASN1_item_i2d_fp(&EVP_PKEY_PUBKEY_it, fp, (ASN1_VALUE *)pkey);
}
LCRYPTO_ALIAS(i2d_PUBKEY_fp);

/*
 * The following are equivalents but which return RSA and DSA keys.
 */
#ifndef OPENSSL_NO_RSA

static int
rsa_pubkey_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	if ((*pval = (ASN1_VALUE *)RSA_new()) == NULL)
		return 0;

	return 1;
}

static void
rsa_pubkey_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	RSA_free((RSA *)*pval);
	*pval = NULL;
}

static int
rsa_pubkey_ex_d2i(ASN1_VALUE **pval, const unsigned char **in, long len,
    const ASN1_ITEM *it, int tag, int aclass, char opt, ASN1_TLC *ctx)
{
	return pubkey_ex_d2i(EVP_PKEY_RSA, pval, in, len, it);
}

static int
rsa_pubkey_ex_i2d(ASN1_VALUE **pval, unsigned char **out, const ASN1_ITEM *it,
    int tag, int aclass)
{
	return pubkey_ex_i2d(EVP_PKEY_RSA, pval, out, it);
}

const ASN1_EXTERN_FUNCS rsa_pubkey_asn1_ff = {
	.app_data = NULL,
	.asn1_ex_new = rsa_pubkey_ex_new,
	.asn1_ex_free = rsa_pubkey_ex_free,
	.asn1_ex_clear = NULL,
	.asn1_ex_d2i = rsa_pubkey_ex_d2i,
	.asn1_ex_i2d = rsa_pubkey_ex_i2d,
	.asn1_ex_print = NULL,
};

const ASN1_ITEM RSA_PUBKEY_it = {
	.itype = ASN1_ITYPE_EXTERN,
	.utype = 0,
	.templates = NULL,
	.tcount = 0,
	.funcs = &rsa_pubkey_asn1_ff,
	.size = 0,
	.sname = NULL,
};

RSA *
d2i_RSA_PUBKEY(RSA **rsa, const unsigned char **in, long len)
{
	return (RSA *)ASN1_item_d2i((ASN1_VALUE **)rsa, in, len,
	    &RSA_PUBKEY_it);
}
LCRYPTO_ALIAS(d2i_RSA_PUBKEY);

int
i2d_RSA_PUBKEY(RSA *rsa, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)rsa, out, &RSA_PUBKEY_it);
}
LCRYPTO_ALIAS(i2d_RSA_PUBKEY);

RSA *
d2i_RSA_PUBKEY_bio(BIO *bp, RSA **rsa)
{
	return (RSA *)ASN1_item_d2i_bio(&RSA_PUBKEY_it, bp, (ASN1_VALUE **)rsa);
}
LCRYPTO_ALIAS(d2i_RSA_PUBKEY_bio);

int
i2d_RSA_PUBKEY_bio(BIO *bp, RSA *rsa)
{
	return ASN1_item_i2d_bio(&RSA_PUBKEY_it, bp, (ASN1_VALUE *)rsa);
}
LCRYPTO_ALIAS(i2d_RSA_PUBKEY_bio);

RSA *
d2i_RSA_PUBKEY_fp(FILE *fp, RSA **rsa)
{
	return (RSA *)ASN1_item_d2i_fp(&RSA_PUBKEY_it, fp, (ASN1_VALUE **)rsa);
}
LCRYPTO_ALIAS(d2i_RSA_PUBKEY_fp);

int
i2d_RSA_PUBKEY_fp(FILE *fp, RSA *rsa)
{
	return ASN1_item_i2d_fp(&RSA_PUBKEY_it, fp, (ASN1_VALUE *)rsa);
}
LCRYPTO_ALIAS(i2d_RSA_PUBKEY_fp);
#endif

#ifndef OPENSSL_NO_DSA

static int
dsa_pubkey_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	if ((*pval = (ASN1_VALUE *)DSA_new()) == NULL)
		return 0;

	return 1;
}

static void
dsa_pubkey_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	DSA_free((DSA *)*pval);
	*pval = NULL;
}

static int
dsa_pubkey_ex_d2i(ASN1_VALUE **pval, const unsigned char **in, long len,
    const ASN1_ITEM *it, int tag, int aclass, char opt, ASN1_TLC *ctx)
{
	return pubkey_ex_d2i(EVP_PKEY_DSA, pval, in, len, it);
}

static int
dsa_pubkey_ex_i2d(ASN1_VALUE **pval, unsigned char **out, const ASN1_ITEM *it,
    int tag, int aclass)
{
	return pubkey_ex_i2d(EVP_PKEY_DSA, pval, out, it);
}

const ASN1_EXTERN_FUNCS dsa_pubkey_asn1_ff = {
	.app_data = NULL,
	.asn1_ex_new = dsa_pubkey_ex_new,
	.asn1_ex_free = dsa_pubkey_ex_free,
	.asn1_ex_clear = NULL,
	.asn1_ex_d2i = dsa_pubkey_ex_d2i,
	.asn1_ex_i2d = dsa_pubkey_ex_i2d,
	.asn1_ex_print = NULL,
};

const ASN1_ITEM DSA_PUBKEY_it = {
	.itype = ASN1_ITYPE_EXTERN,
	.utype = 0,
	.templates = NULL,
	.tcount = 0,
	.funcs = &dsa_pubkey_asn1_ff,
	.size = 0,
	.sname = NULL,
};

DSA *
d2i_DSA_PUBKEY(DSA **dsa, const unsigned char **in, long len)
{
	return (DSA *)ASN1_item_d2i((ASN1_VALUE **)dsa, in, len,
	    &DSA_PUBKEY_it);
}
LCRYPTO_ALIAS(d2i_DSA_PUBKEY);

int
i2d_DSA_PUBKEY(DSA *dsa, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)dsa, out, &DSA_PUBKEY_it);
}
LCRYPTO_ALIAS(i2d_DSA_PUBKEY);

DSA *
d2i_DSA_PUBKEY_bio(BIO *bp, DSA **dsa)
{
	return (DSA *)ASN1_item_d2i_bio(&DSA_PUBKEY_it, bp, (ASN1_VALUE **)dsa);
}
LCRYPTO_ALIAS(d2i_DSA_PUBKEY_bio);

int
i2d_DSA_PUBKEY_bio(BIO *bp, DSA *dsa)
{
	return ASN1_item_i2d_bio(&DSA_PUBKEY_it, bp, (ASN1_VALUE *)dsa);
}
LCRYPTO_ALIAS(i2d_DSA_PUBKEY_bio);

DSA *
d2i_DSA_PUBKEY_fp(FILE *fp, DSA **dsa)
{
	return (DSA *)ASN1_item_d2i_fp(&DSA_PUBKEY_it, fp, (ASN1_VALUE **)dsa);
}
LCRYPTO_ALIAS(d2i_DSA_PUBKEY_fp);

int
i2d_DSA_PUBKEY_fp(FILE *fp, DSA *dsa)
{
	return ASN1_item_i2d_fp(&DSA_PUBKEY_it, fp, (ASN1_VALUE *)dsa);
}
LCRYPTO_ALIAS(i2d_DSA_PUBKEY_fp);

#endif

#ifndef OPENSSL_NO_EC

static int
ec_pubkey_ex_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	if ((*pval = (ASN1_VALUE *)EC_KEY_new()) == NULL)
		return 0;

	return 1;
}

static void
ec_pubkey_ex_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	EC_KEY_free((EC_KEY *)*pval);
	*pval = NULL;
}

static int
ec_pubkey_ex_d2i(ASN1_VALUE **pval, const unsigned char **in, long len,
    const ASN1_ITEM *it, int tag, int aclass, char opt, ASN1_TLC *ctx)
{
	return pubkey_ex_d2i(EVP_PKEY_EC, pval, in, len, it);
}

static int
ec_pubkey_ex_i2d(ASN1_VALUE **pval, unsigned char **out, const ASN1_ITEM *it,
    int tag, int aclass)
{
	return pubkey_ex_i2d(EVP_PKEY_EC, pval, out, it);
}

const ASN1_EXTERN_FUNCS ec_pubkey_asn1_ff = {
	.app_data = NULL,
	.asn1_ex_new = ec_pubkey_ex_new,
	.asn1_ex_free = ec_pubkey_ex_free,
	.asn1_ex_clear = NULL,
	.asn1_ex_d2i = ec_pubkey_ex_d2i,
	.asn1_ex_i2d = ec_pubkey_ex_i2d,
	.asn1_ex_print = NULL,
};

const ASN1_ITEM EC_PUBKEY_it = {
	.itype = ASN1_ITYPE_EXTERN,
	.utype = 0,
	.templates = NULL,
	.tcount = 0,
	.funcs = &ec_pubkey_asn1_ff,
	.size = 0,
	.sname = NULL,
};

EC_KEY *
d2i_EC_PUBKEY(EC_KEY **ec, const unsigned char **in, long len)
{
	return (EC_KEY *)ASN1_item_d2i((ASN1_VALUE **)ec, in, len,
	    &EC_PUBKEY_it);
}
LCRYPTO_ALIAS(d2i_EC_PUBKEY);

int
i2d_EC_PUBKEY(EC_KEY *ec, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)ec, out, &EC_PUBKEY_it);
}
LCRYPTO_ALIAS(i2d_EC_PUBKEY);

EC_KEY *
d2i_EC_PUBKEY_bio(BIO *bp, EC_KEY **ec)
{
	return (EC_KEY *)ASN1_item_d2i_bio(&EC_PUBKEY_it, bp, (ASN1_VALUE **)ec);
}
LCRYPTO_ALIAS(d2i_EC_PUBKEY_bio);

int
i2d_EC_PUBKEY_bio(BIO *bp, EC_KEY *ec)
{
	return ASN1_item_i2d_bio(&EC_PUBKEY_it, bp, (ASN1_VALUE *)ec);
}
LCRYPTO_ALIAS(i2d_EC_PUBKEY_bio);

EC_KEY *
d2i_EC_PUBKEY_fp(FILE *fp, EC_KEY **ec)
{
	return (EC_KEY *)ASN1_item_d2i_fp(&EC_PUBKEY_it, fp, (ASN1_VALUE **)ec);
}
LCRYPTO_ALIAS(d2i_EC_PUBKEY_fp);

int
i2d_EC_PUBKEY_fp(FILE *fp, EC_KEY *ec)
{
	return ASN1_item_i2d_fp(&EC_PUBKEY_it, fp, (ASN1_VALUE *)ec);
}
LCRYPTO_ALIAS(i2d_EC_PUBKEY_fp);
#endif

int
X509_PUBKEY_set0_param(X509_PUBKEY *pub, ASN1_OBJECT *aobj, int ptype,
    void *pval, unsigned char *penc, int penclen)
{
	if (!X509_ALGOR_set0(pub->algor, aobj, ptype, pval))
		return 0;

	if (penc == NULL)
		return 1;

	ASN1_STRING_set0(pub->public_key, penc, penclen);

	return asn1_abs_set_unused_bits(pub->public_key, 0);
}
LCRYPTO_ALIAS(X509_PUBKEY_set0_param);

int
X509_PUBKEY_get0_param(ASN1_OBJECT **ppkalg, const unsigned char **pk,
    int *ppklen, X509_ALGOR **pa, X509_PUBKEY *pub)
{
	if (ppkalg)
		*ppkalg = pub->algor->algorithm;
	if (pk) {
		*pk = pub->public_key->data;
		*ppklen = pub->public_key->length;
	}
	if (pa)
		*pa = pub->algor;
	return 1;
}
LCRYPTO_ALIAS(X509_PUBKEY_get0_param);
