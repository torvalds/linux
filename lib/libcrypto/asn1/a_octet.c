/* $OpenBSD: a_octet.c,v 1.13 2024/07/08 14:52:31 beck Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/asn1t.h>

const ASN1_ITEM ASN1_OCTET_STRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_OCTET_STRING,
	.sname = "ASN1_OCTET_STRING",
};
LCRYPTO_ALIAS(ASN1_OCTET_STRING_it);

ASN1_OCTET_STRING *
ASN1_OCTET_STRING_new(void)
{
	return (ASN1_OCTET_STRING *)ASN1_item_new(&ASN1_OCTET_STRING_it);
}
LCRYPTO_ALIAS(ASN1_OCTET_STRING_new);

void
ASN1_OCTET_STRING_free(ASN1_OCTET_STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_OCTET_STRING_it);
}
LCRYPTO_ALIAS(ASN1_OCTET_STRING_free);


ASN1_OCTET_STRING *
ASN1_OCTET_STRING_dup(const ASN1_OCTET_STRING *x)
{
	return ASN1_STRING_dup(x);
}
LCRYPTO_ALIAS(ASN1_OCTET_STRING_dup);

int
ASN1_OCTET_STRING_cmp(const ASN1_OCTET_STRING *a, const ASN1_OCTET_STRING *b)
{
	return ASN1_STRING_cmp(a, b);
}
LCRYPTO_ALIAS(ASN1_OCTET_STRING_cmp);

int
ASN1_OCTET_STRING_set(ASN1_OCTET_STRING *x, const unsigned char *d, int len)
{
	return ASN1_STRING_set(x, d, len);
}
LCRYPTO_ALIAS(ASN1_OCTET_STRING_set);

int
i2d_ASN1_OCTET_STRING(ASN1_OCTET_STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_OCTET_STRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_OCTET_STRING);

ASN1_OCTET_STRING *
d2i_ASN1_OCTET_STRING(ASN1_OCTET_STRING **a, const unsigned char **in, long len)
{
	return (ASN1_OCTET_STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_OCTET_STRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_OCTET_STRING);
