/* $OpenBSD: x_long.c,v 1.22 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <string.h>

#include <openssl/asn1t.h>
#include <openssl/bn.h>

#include "asn1_local.h"
#include "err_local.h"

/*
 * Custom primitive type for long handling. This converts between an
 * ASN1_INTEGER and a long directly.
 */

static int long_new(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void long_free(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void long_clear(ASN1_VALUE **pval, const ASN1_ITEM *it);

static int long_i2c(ASN1_VALUE **pval, unsigned char *content, int *putype,
    const ASN1_ITEM *it);
static int long_c2i(ASN1_VALUE **pval, const unsigned char *content, int len,
    int utype, char *free_content, const ASN1_ITEM *it);
static int long_print(BIO *out, ASN1_VALUE **pval, const ASN1_ITEM *it,
    int indent, const ASN1_PCTX *pctx);

static const ASN1_PRIMITIVE_FUNCS long_pf = {
	.app_data = NULL,
	.flags = 0,
	.prim_new = long_new,
	.prim_free = long_free,
	.prim_clear = long_clear,
	.prim_c2i = long_c2i,
	.prim_i2c = long_i2c,
	.prim_print = long_print,
};

const ASN1_ITEM LONG_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_INTEGER,
	.templates = NULL,
	.tcount = 0,
	.funcs = &long_pf,
	.size = ASN1_LONG_UNDEF,
	.sname = "LONG",
};
LCRYPTO_ALIAS(LONG_it);

const ASN1_ITEM ZLONG_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_INTEGER,
	.templates = NULL,
	.tcount = 0,
	.funcs = &long_pf,
	.size = 0,
	.sname = "ZLONG",
};
LCRYPTO_ALIAS(ZLONG_it);

static void
long_get(ASN1_VALUE **pval, long *out_val)
{
	memcpy(out_val, pval, sizeof(long));
}

static void
long_set(ASN1_VALUE **pval, long val)
{
	memcpy(pval, &val, sizeof(long));
}

static int
long_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	long_clear(pval, it);

	return 1;
}

static void
long_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	long_clear(pval, it);
}

static void
long_clear(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
	/* Zero value. */
	long_set(pval, it->size);
}

static int
long_i2c(ASN1_VALUE **pval, unsigned char *content, int *putype,
    const ASN1_ITEM *it)
{
	ASN1_INTEGER *aint;
	uint8_t **pp = NULL;
	long val;
	int ret = 0;

	long_get(pval, &val);

	/*
	 * The zero value for this type (stored in the overloaded it->size
	 * field) is considered to be invalid.
	 */
	if (val == it->size)
		return -1;

	if ((aint = ASN1_INTEGER_new()) == NULL)
		goto err;
	if (!ASN1_INTEGER_set_int64(aint, (int64_t)val))
		goto err;
	if (content != NULL)
		pp = &content;
	ret = i2c_ASN1_INTEGER(aint, pp);

 err:
	ASN1_INTEGER_free(aint);

	return ret;
}

static int
long_c2i(ASN1_VALUE **pval, const unsigned char *content, int len, int utype,
    char *free_content, const ASN1_ITEM *it)
{
	ASN1_INTEGER *aint = NULL;
	const uint8_t **pp = NULL;
	int64_t val = 0;
	int ret = 0;

	/*
	 * The original long_i2c() mishandled 0 values and encoded them as
	 * content with zero length, rather than a single zero byte. Permit
	 * zero length content here for backwards compatibility.
	 */
	if (len != 0) {
		if (content != NULL)
			pp = &content;
		if (!c2i_ASN1_INTEGER(&aint, pp, len))
			goto err;
		if (!ASN1_INTEGER_get_int64(&val, aint))
			goto err;
	}

	if (val < LONG_MIN || val > LONG_MAX) {
		ASN1error(ASN1_R_INTEGER_TOO_LARGE_FOR_LONG);
		goto err;
	}

	/*
	 * The zero value for this type (stored in the overloaded it->size
	 * field) is considered to be invalid.
	 */
	if (val == (int64_t)it->size) {
		ASN1error(ASN1_R_INTEGER_TOO_LARGE_FOR_LONG);
		goto err;
	}

	long_set(pval, (long)val);

	ret = 1;

 err:
	ASN1_INTEGER_free(aint);

	return ret;
}

static int
long_print(BIO *out, ASN1_VALUE **pval, const ASN1_ITEM *it, int indent,
    const ASN1_PCTX *pctx)
{
	long val;

	long_get(pval, &val);

	if (BIO_printf(out, "%ld\n", val) <= 0)
		return 0;

	return 1;
}
