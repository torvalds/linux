/* $OpenBSD: dsa_asn1.c,v 1.34 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000-2005 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/dsa.h>

#include "dsa_local.h"
#include "err_local.h"

/* Override the default new methods */
static int
sig_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_NEW_PRE) {
		DSA_SIG *sig;

		if ((sig = DSA_SIG_new()) == NULL) {
			DSAerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		*pval = (ASN1_VALUE *)sig;
		return 2;
	}
	return 1;
}

static const ASN1_AUX DSA_SIG_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = sig_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE DSA_SIG_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA_SIG, r),
		.field_name = "r",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA_SIG, s),
		.field_name = "s",
		.item = &BIGNUM_it,
	},
};

static const ASN1_ITEM DSA_SIG_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = DSA_SIG_seq_tt,
	.tcount = sizeof(DSA_SIG_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &DSA_SIG_aux,
	.size = sizeof(DSA_SIG),
	.sname = "DSA_SIG",
};


DSA_SIG *
d2i_DSA_SIG(DSA_SIG **a, const unsigned char **in, long len)
{
	return (DSA_SIG *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DSA_SIG_it);
}
LCRYPTO_ALIAS(d2i_DSA_SIG);

int
i2d_DSA_SIG(const DSA_SIG *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DSA_SIG_it);
}
LCRYPTO_ALIAS(i2d_DSA_SIG);

void
DSA_SIG_get0(const DSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps)
{
	if (pr != NULL)
		*pr = sig->r;
	if (ps != NULL)
		*ps = sig->s;
}
LCRYPTO_ALIAS(DSA_SIG_get0);

int
DSA_SIG_set0(DSA_SIG *sig, BIGNUM *r, BIGNUM *s)
{
	if (r == NULL || s == NULL)
		return 0;

	BN_free(sig->r);
	sig->r = r;
	BN_free(sig->s);
	sig->s = s;

	return 1;
}
LCRYPTO_ALIAS(DSA_SIG_set0);

/* Override the default free and new methods */
static int
dsa_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_NEW_PRE) {
		*pval = (ASN1_VALUE *)DSA_new();
		if (*pval)
			return 2;
		return 0;
	} else if (operation == ASN1_OP_FREE_PRE) {
		DSA_free((DSA *)*pval);
		*pval = NULL;
		return 2;
	}
	return 1;
}

static const ASN1_AUX DSAPrivateKey_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = dsa_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE DSAPrivateKey_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, p),
		.field_name = "p",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, q),
		.field_name = "q",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, g),
		.field_name = "g",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, pub_key),
		.field_name = "pub_key",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, priv_key),
		.field_name = "priv_key",
		.item = &BIGNUM_it,
	},
};

const ASN1_ITEM DSAPrivateKey_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = DSAPrivateKey_seq_tt,
	.tcount = sizeof(DSAPrivateKey_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &DSAPrivateKey_aux,
	.size = sizeof(DSA),
	.sname = "DSA",
};
LCRYPTO_ALIAS(DSAPrivateKey_it);


DSA *
d2i_DSAPrivateKey(DSA **a, const unsigned char **in, long len)
{
	return (DSA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DSAPrivateKey_it);
}
LCRYPTO_ALIAS(d2i_DSAPrivateKey);

int
i2d_DSAPrivateKey(const DSA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DSAPrivateKey_it);
}
LCRYPTO_ALIAS(i2d_DSAPrivateKey);

static const ASN1_AUX DSAparams_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = dsa_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE DSAparams_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, p),
		.field_name = "p",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, q),
		.field_name = "q",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, g),
		.field_name = "g",
		.item = &BIGNUM_it,
	},
};

const ASN1_ITEM DSAparams_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = DSAparams_seq_tt,
	.tcount = sizeof(DSAparams_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &DSAparams_aux,
	.size = sizeof(DSA),
	.sname = "DSA",
};
LCRYPTO_ALIAS(DSAparams_it);


DSA *
d2i_DSAparams(DSA **a, const unsigned char **in, long len)
{
	return (DSA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DSAparams_it);
}
LCRYPTO_ALIAS(d2i_DSAparams);

int
i2d_DSAparams(const DSA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DSAparams_it);
}
LCRYPTO_ALIAS(i2d_DSAparams);

DSA *
d2i_DSAparams_bio(BIO *bp, DSA **a)
{
	return ASN1_item_d2i_bio(&DSAparams_it, bp, a);
}
LCRYPTO_ALIAS(d2i_DSAparams_bio);

int
i2d_DSAparams_bio(BIO *bp, DSA *a)
{
	return ASN1_item_i2d_bio(&DSAparams_it, bp, a);
}
LCRYPTO_ALIAS(i2d_DSAparams_bio);

DSA *
d2i_DSAparams_fp(FILE *fp, DSA **a)
{
	return ASN1_item_d2i_fp(&DSAparams_it, fp, a);
}
LCRYPTO_ALIAS(d2i_DSAparams_fp);

int
i2d_DSAparams_fp(FILE *fp, DSA *a)
{
	return ASN1_item_i2d_fp(&DSAparams_it, fp, a);
}
LCRYPTO_ALIAS(i2d_DSAparams_fp);

static const ASN1_AUX DSAPublicKey_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = dsa_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE DSAPublicKey_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, pub_key),
		.field_name = "pub_key",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, p),
		.field_name = "p",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, q),
		.field_name = "q",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DSA, g),
		.field_name = "g",
		.item = &BIGNUM_it,
	},
};

const ASN1_ITEM DSAPublicKey_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = DSAPublicKey_seq_tt,
	.tcount = sizeof(DSAPublicKey_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &DSAPublicKey_aux,
	.size = sizeof(DSA),
	.sname = "DSA",
};
LCRYPTO_ALIAS(DSAPublicKey_it);

DSA *
d2i_DSAPublicKey(DSA **a, const unsigned char **in, long len)
{
	return (DSA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DSAPublicKey_it);
}
LCRYPTO_ALIAS(d2i_DSAPublicKey);

int
i2d_DSAPublicKey(const DSA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DSAPublicKey_it);
}
LCRYPTO_ALIAS(i2d_DSAPublicKey);

DSA *
DSAparams_dup(DSA *dsa)
{
	return ASN1_item_dup(&DSAparams_it, dsa);
}
LCRYPTO_ALIAS(DSAparams_dup);

int
DSA_sign(int type, const unsigned char *dgst, int dlen, unsigned char *sig,
    unsigned int *out_siglen, DSA *dsa)
{
	DSA_SIG *s;
	int siglen;
	int ret = 0;

	*out_siglen = 0;

	if ((s = DSA_do_sign(dgst, dlen, dsa)) == NULL)
		goto err;

	if ((siglen = i2d_DSA_SIG(s, &sig)) < 0)
		goto err;

	*out_siglen = siglen;

	ret = 1;
 err:
	DSA_SIG_free(s);

	return ret;
}
LCRYPTO_ALIAS(DSA_sign);

/*
 * data has already been hashed (probably with SHA or SHA-1).
 * returns
 *      1: correct signature
 *      0: incorrect signature
 *     -1: error
 */
int
DSA_verify(int type, const unsigned char *dgst, int dgst_len,
    const unsigned char *sigbuf, int siglen, DSA *dsa)
{
	DSA_SIG *s = NULL;
	unsigned char *der = NULL;
	const unsigned char *p;
	int ret = -1;

	p = sigbuf;
	if ((s = d2i_DSA_SIG(NULL, &p, siglen)) == NULL)
		goto err;

	/* Ensure signature uses DER and doesn't have trailing garbage */
	if (i2d_DSA_SIG(s, &der) != siglen)
		goto err;

	if (memcmp(der, sigbuf, siglen) != 0)
		goto err;

	ret = DSA_do_verify(dgst, dgst_len, s, dsa);
 err:
	free(der);
	DSA_SIG_free(s);

	return ret;
}
LCRYPTO_ALIAS(DSA_verify);
