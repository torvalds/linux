/* $OpenBSD: x_bignum.c,v 1.15 2024/07/08 16:24:22 beck Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000 The OpenSSL Project.  All rights reserved.
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

#include "asn1_local.h"
#include "bytestring.h"

/*
 * Custom primitive type for that reads an ASN.1 INTEGER into a BIGNUM.
 */

static int bn_new(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void bn_free(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void bn_clear(ASN1_VALUE **pval, const ASN1_ITEM *it);

static int bn_i2c(ASN1_VALUE **pval, unsigned char *cont, int *putype,
    const ASN1_ITEM *it);
static int bn_c2i(ASN1_VALUE **pval, const unsigned char *cont, int len,
    int utype, char *free_cont, const ASN1_ITEM *it);
static int bn_print(BIO *out, ASN1_VALUE **pval, const ASN1_ITEM *it,
    int indent, const ASN1_PCTX *pctx);

static const ASN1_PRIMITIVE_FUNCS bignum_pf = {
	.app_data = NULL,
	.flags = 0,
	.prim_new = bn_new,
	.prim_free = bn_free,
	.prim_clear = bn_clear,
	.prim_c2i = bn_c2i,
	.prim_i2c = bn_i2c,
	.prim_print = bn_print,
};

const ASN1_ITEM BIGNUM_it = {
        .itype = ASN1_ITYPE_PRIMITIVE,
        .utype = V_ASN1_INTEGER,
        .templates = NULL,
        .tcount = 0,
        .funcs = &bignum_pf,
        .size = 0,
        .sname = "BIGNUM",
};
LCRYPTO_ALIAS(BIGNUM_it);

const ASN1_ITEM CBIGNUM_it = {
        .itype = ASN1_ITYPE_PRIMITIVE,
        .utype = V_ASN1_INTEGER,
        .templates = NULL,
        .tcount = 0,
        .funcs = &bignum_pf,
        .size = 0,
        .sname = "BIGNUM",
};
LCRYPTO_ALIAS(CBIGNUM_it);

static int
bn_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	if ((*pval = (ASN1_VALUE *)BN_new()) == NULL)
		return 0;

	return 1;
}

static void
bn_clear(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	BN_free((BIGNUM *)*pval);
	*pval = NULL;
}

static void
bn_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	if (*pval == NULL)
		return;

	bn_clear(pval, it);
}

static int
bn_i2c(ASN1_VALUE **pval, unsigned char *content, int *putype, const ASN1_ITEM *it)
{
	ASN1_INTEGER *aint = NULL;
	unsigned char **pp = NULL;
	const BIGNUM *bn;
	int ret;

	if (*pval == NULL)
		return -1;

	bn = (const BIGNUM *)*pval;

	if ((aint = BN_to_ASN1_INTEGER(bn, NULL)) == NULL)
		return -1;

	if (content != NULL)
		pp = &content;

	ret = i2c_ASN1_INTEGER(aint, pp);

	ASN1_INTEGER_free(aint);

	return ret;
}

static int
bn_c2i(ASN1_VALUE **pval, const unsigned char *content, int len, int utype,
    char *free_content, const ASN1_ITEM *it)
{
	ASN1_INTEGER *aint = NULL;
	BIGNUM *bn;
	CBS cbs;
	int ret = 0;

	bn_clear(pval, it);

	if (len < 0)
		goto err;
	CBS_init(&cbs, content, len);
	if (!c2i_ASN1_INTEGER_cbs(&aint, &cbs))
		goto err;

	if ((bn = ASN1_INTEGER_to_BN(aint, NULL)) == NULL)
		goto err;
	*pval = (ASN1_VALUE *)bn;

	ret = 1;

 err:
	ASN1_INTEGER_free(aint);

	return ret;
}

static int
bn_print(BIO *out, ASN1_VALUE **pval, const ASN1_ITEM *it, int indent,
    const ASN1_PCTX *pctx)
{
	const BIGNUM *bn = (BIGNUM *)*pval;

	if (!BN_print(out, bn))
		return 0;
	if (BIO_printf(out, "\n") <= 0)
		return 0;

	return 1;
}
