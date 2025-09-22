/* $OpenBSD: dh_asn1.c,v 1.13 2024/04/15 15:47:37 tb Exp $ */
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
#include <openssl/dh.h>
#include <openssl/objects.h>

#include "dh_local.h"

/* Override the default free and new methods */
static int
dh_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_NEW_PRE) {
		*pval = (ASN1_VALUE *)DH_new();
		if (*pval)
			return 2;
		return 0;
	} else if (operation == ASN1_OP_FREE_PRE) {
		DH_free((DH *)*pval);
		*pval = NULL;
		return 2;
	}
	return 1;
}

static const ASN1_AUX DHparams_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = dh_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE DHparams_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DH, p),
		.field_name = "p",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(DH, g),
		.field_name = "g",
		.item = &BIGNUM_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(DH, length),
		.field_name = "length",
		.item = &ZLONG_it,
	},
};

static const ASN1_ITEM DHparams_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = DHparams_seq_tt,
	.tcount = sizeof(DHparams_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &DHparams_aux,
	.size = sizeof(DH),
	.sname = "DH",
};


DH *
d2i_DHparams(DH **a, const unsigned char **in, long len)
{
	return (DH *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DHparams_it);
}
LCRYPTO_ALIAS(d2i_DHparams);

int
i2d_DHparams(const DH *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DHparams_it);
}
LCRYPTO_ALIAS(i2d_DHparams);

DH *
d2i_DHparams_bio(BIO *bp, DH **a)
{
	return ASN1_item_d2i_bio(&DHparams_it, bp, a);
}
LCRYPTO_ALIAS(d2i_DHparams_bio);

int
i2d_DHparams_bio(BIO *bp, DH *a)
{
	return ASN1_item_i2d_bio(&DHparams_it, bp, a);
}
LCRYPTO_ALIAS(i2d_DHparams_bio);

DH *
d2i_DHparams_fp(FILE *fp, DH **a)
{
	return ASN1_item_d2i_fp(&DHparams_it, fp, a);
}
LCRYPTO_ALIAS(d2i_DHparams_fp);

int
i2d_DHparams_fp(FILE *fp, DH *a)
{
	return ASN1_item_i2d_fp(&DHparams_it, fp, a);
}
LCRYPTO_ALIAS(i2d_DHparams_fp);

DH *
DHparams_dup(DH *dh)
{
	return ASN1_item_dup(&DHparams_it, dh);
}
LCRYPTO_ALIAS(DHparams_dup);
