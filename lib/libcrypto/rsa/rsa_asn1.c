/* $OpenBSD: rsa_asn1.c,v 1.18 2024/07/08 17:10:18 beck Exp $ */
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

#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "rsa_local.h"

/* Override the default free and new methods */
static int
rsa_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_NEW_PRE) {
		*pval = (ASN1_VALUE *)RSA_new();
		if (*pval)
			return 2;
		return 0;
	} else if (operation == ASN1_OP_FREE_PRE) {
		RSA_free((RSA *)*pval);
		*pval = NULL;
		return 2;
	}
	return 1;
}

static const ASN1_AUX RSAPrivateKey_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = rsa_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE RSAPrivateKey_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, n),
		.field_name = "n",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, e),
		.field_name = "e",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, d),
		.field_name = "d",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, p),
		.field_name = "p",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, q),
		.field_name = "q",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, dmp1),
		.field_name = "dmp1",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, dmq1),
		.field_name = "dmq1",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, iqmp),
		.field_name = "iqmp",
		.item = &BIGNUM_it,
	},
};

const ASN1_ITEM RSAPrivateKey_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = RSAPrivateKey_seq_tt,
	.tcount = sizeof(RSAPrivateKey_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &RSAPrivateKey_aux,
	.size = sizeof(RSA),
	.sname = "RSA",
};
LCRYPTO_ALIAS(RSAPrivateKey_it);


static const ASN1_AUX RSAPublicKey_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = rsa_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE RSAPublicKey_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, n),
		.field_name = "n",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(RSA, e),
		.field_name = "e",
		.item = &BIGNUM_it,
	},
};

const ASN1_ITEM RSAPublicKey_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = RSAPublicKey_seq_tt,
	.tcount = sizeof(RSAPublicKey_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &RSAPublicKey_aux,
	.size = sizeof(RSA),
	.sname = "RSA",
};
LCRYPTO_ALIAS(RSAPublicKey_it);

static int
rsa_pss_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	/* Free up maskHash */
	if (operation == ASN1_OP_FREE_PRE) {
		RSA_PSS_PARAMS *pss = (RSA_PSS_PARAMS *)*pval;
		X509_ALGOR_free(pss->maskHash);
	}
	return 1;
}

static const ASN1_AUX RSA_PSS_PARAMS_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = rsa_pss_cb,
	.enc_offset = 0,
};

static const ASN1_TEMPLATE RSA_PSS_PARAMS_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(RSA_PSS_PARAMS, hashAlgorithm),
		.field_name = "hashAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(RSA_PSS_PARAMS, maskGenAlgorithm),
		.field_name = "maskGenAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(RSA_PSS_PARAMS, saltLength),
		.field_name = "saltLength",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 3,
		.offset = offsetof(RSA_PSS_PARAMS, trailerField),
		.field_name = "trailerField",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM RSA_PSS_PARAMS_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = RSA_PSS_PARAMS_seq_tt,
	.tcount = sizeof(RSA_PSS_PARAMS_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &RSA_PSS_PARAMS_aux,
	.size = sizeof(RSA_PSS_PARAMS),
	.sname = "RSA_PSS_PARAMS",
};
LCRYPTO_ALIAS(RSA_PSS_PARAMS_it);

RSA_PSS_PARAMS *
d2i_RSA_PSS_PARAMS(RSA_PSS_PARAMS **a, const unsigned char **in, long len)
{
	return (RSA_PSS_PARAMS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &RSA_PSS_PARAMS_it);
}
LCRYPTO_ALIAS(d2i_RSA_PSS_PARAMS);

int
i2d_RSA_PSS_PARAMS(RSA_PSS_PARAMS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &RSA_PSS_PARAMS_it);
}
LCRYPTO_ALIAS(i2d_RSA_PSS_PARAMS);

RSA_PSS_PARAMS *
RSA_PSS_PARAMS_new(void)
{
	return (RSA_PSS_PARAMS *)ASN1_item_new(&RSA_PSS_PARAMS_it);
}
LCRYPTO_ALIAS(RSA_PSS_PARAMS_new);

void
RSA_PSS_PARAMS_free(RSA_PSS_PARAMS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &RSA_PSS_PARAMS_it);
}
LCRYPTO_ALIAS(RSA_PSS_PARAMS_free);

static int
rsa_oaep_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	/* Free up maskHash */
	if (operation == ASN1_OP_FREE_PRE) {
		RSA_OAEP_PARAMS *oaep = (RSA_OAEP_PARAMS *)*pval;
		X509_ALGOR_free(oaep->maskHash);
	}
	return 1;
}

static const ASN1_AUX RSA_OAEP_PARAMS_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = rsa_oaep_cb,
	.enc_offset = 0,
};

static const ASN1_TEMPLATE RSA_OAEP_PARAMS_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(RSA_OAEP_PARAMS, hashFunc),
		.field_name = "hashFunc",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(RSA_OAEP_PARAMS, maskGenFunc),
		.field_name = "maskGenFunc",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(RSA_OAEP_PARAMS, pSourceFunc),
		.field_name = "pSourceFunc",
		.item = &X509_ALGOR_it,
	},
};

const ASN1_ITEM RSA_OAEP_PARAMS_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = RSA_OAEP_PARAMS_seq_tt,
	.tcount = sizeof(RSA_OAEP_PARAMS_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &RSA_OAEP_PARAMS_aux,
	.size = sizeof(RSA_OAEP_PARAMS),
	.sname = "RSA_OAEP_PARAMS",
};
LCRYPTO_ALIAS(RSA_OAEP_PARAMS_it);


RSA_OAEP_PARAMS *
d2i_RSA_OAEP_PARAMS(RSA_OAEP_PARAMS **a, const unsigned char **in, long len)
{
	return (RSA_OAEP_PARAMS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &RSA_OAEP_PARAMS_it);
}
LCRYPTO_ALIAS(d2i_RSA_OAEP_PARAMS);

int
i2d_RSA_OAEP_PARAMS(RSA_OAEP_PARAMS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &RSA_OAEP_PARAMS_it);
}
LCRYPTO_ALIAS(i2d_RSA_OAEP_PARAMS);

RSA_OAEP_PARAMS *
RSA_OAEP_PARAMS_new(void)
{
	return (RSA_OAEP_PARAMS *)ASN1_item_new(&RSA_OAEP_PARAMS_it);
}
LCRYPTO_ALIAS(RSA_OAEP_PARAMS_new);

void
RSA_OAEP_PARAMS_free(RSA_OAEP_PARAMS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &RSA_OAEP_PARAMS_it);
}
LCRYPTO_ALIAS(RSA_OAEP_PARAMS_free);

RSA *
d2i_RSAPrivateKey(RSA **a, const unsigned char **in, long len)
{
	return (RSA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &RSAPrivateKey_it);
}
LCRYPTO_ALIAS(d2i_RSAPrivateKey);

int
i2d_RSAPrivateKey(const RSA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &RSAPrivateKey_it);
}
LCRYPTO_ALIAS(i2d_RSAPrivateKey);


RSA *
d2i_RSAPublicKey(RSA **a, const unsigned char **in, long len)
{
	return (RSA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &RSAPublicKey_it);
}
LCRYPTO_ALIAS(d2i_RSAPublicKey);

int
i2d_RSAPublicKey(const RSA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &RSAPublicKey_it);
}
LCRYPTO_ALIAS(i2d_RSAPublicKey);

RSA *
RSAPublicKey_dup(RSA *rsa)
{
	return ASN1_item_dup(&RSAPublicKey_it, rsa);
}
LCRYPTO_ALIAS(RSAPublicKey_dup);

RSA *
RSAPrivateKey_dup(RSA *rsa)
{
	return ASN1_item_dup(&RSAPrivateKey_it, rsa);
}
LCRYPTO_ALIAS(RSAPrivateKey_dup);
