/* $OpenBSD: a_time.c,v 1.39 2025/05/10 05:54:38 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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

/* This is an implementation of the ASN1 Time structure which is:
 *    Time ::= CHOICE {
 *      utcTime        UTCTime,
 *      generalTime    GeneralizedTime }
 * written by Steve Henson.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <openssl/asn1t.h>

#include "asn1_local.h"

const ASN1_ITEM ASN1_TIME_it = {
	.itype = ASN1_ITYPE_MSTRING,
	.utype = B_ASN1_TIME,
	.templates = NULL,
	.tcount = 0,
	.funcs = NULL,
	.size = sizeof(ASN1_STRING),
	.sname = "ASN1_TIME",
};
LCRYPTO_ALIAS(ASN1_TIME_it);

ASN1_TIME *
ASN1_TIME_new(void)
{
	return (ASN1_TIME *)ASN1_item_new(&ASN1_TIME_it);
}
LCRYPTO_ALIAS(ASN1_TIME_new);

void
ASN1_TIME_free(ASN1_TIME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_TIME_it);
}
LCRYPTO_ALIAS(ASN1_TIME_free);

int
ASN1_TIME_to_tm(const ASN1_TIME *s, struct tm *tm)
{
	time_t now;

	if (s != NULL)
		return ASN1_time_parse(s->data, s->length, tm, 0) != -1;

	time(&now);
	memset(tm, 0, sizeof(*tm));

	return asn1_time_time_t_to_tm(&now, tm);
}
LCRYPTO_ALIAS(ASN1_TIME_to_tm);

int
ASN1_TIME_diff(int *pday, int *psec, const ASN1_TIME *from, const ASN1_TIME *to)
{
	struct tm tm_from, tm_to;

	if (!ASN1_TIME_to_tm(from, &tm_from))
		return 0;
	if (!ASN1_TIME_to_tm(to, &tm_to))
		return 0;

	return OPENSSL_gmtime_diff(pday, psec, &tm_from, &tm_to);
}
LCRYPTO_ALIAS(ASN1_TIME_diff);

ASN1_TIME *
d2i_ASN1_TIME(ASN1_TIME **a, const unsigned char **in, long len)
{
	return (ASN1_TIME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_TIME_it);
}
LCRYPTO_ALIAS(d2i_ASN1_TIME);

int
i2d_ASN1_TIME(ASN1_TIME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_TIME_it);
}
LCRYPTO_ALIAS(i2d_ASN1_TIME);
