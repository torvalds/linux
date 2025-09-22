/* $OpenBSD: x509cset.c,v 1.22 2024/03/26 23:41:45 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2001.
 */
/* ====================================================================
 * Copyright (c) 2001 The OpenSSL Project.  All rights reserved.
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

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "x509_local.h"

int
X509_CRL_up_ref(X509_CRL *x)
{
	return CRYPTO_add(&x->references, 1, CRYPTO_LOCK_X509_CRL) > 1;
}
LCRYPTO_ALIAS(X509_CRL_up_ref);

int
X509_CRL_set_version(X509_CRL *x, long version)
{
	if (x == NULL)
		return 0;
	/*
	 * RFC 5280, 4.1: versions 1 - 3 are specified as follows.
	 * Version  ::=  INTEGER  {  v1(0), v2(1), v3(2) }
	 * The only specified versions for CRLs are 1 and 2.
	 */
	if (version < 0 || version > 1)
		return 0;
	if (x->crl->version == NULL) {
		if ((x->crl->version = ASN1_INTEGER_new()) == NULL)
			return 0;
	}
	return ASN1_INTEGER_set(x->crl->version, version);
}
LCRYPTO_ALIAS(X509_CRL_set_version);

int
X509_CRL_set_issuer_name(X509_CRL *x, X509_NAME *name)
{
	if (x == NULL || x->crl == NULL)
		return 0;
	return X509_NAME_set(&x->crl->issuer, name);
}
LCRYPTO_ALIAS(X509_CRL_set_issuer_name);

int
X509_CRL_set_lastUpdate(X509_CRL *x, const ASN1_TIME *tm)
{
	ASN1_TIME *in;

	if (x == NULL)
		return 0;
	in = x->crl->lastUpdate;
	if (in != tm) {
		in = ASN1_STRING_dup(tm);
		if (in != NULL) {
			ASN1_TIME_free(x->crl->lastUpdate);
			x->crl->lastUpdate = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_CRL_set_lastUpdate);

int
X509_CRL_set1_lastUpdate(X509_CRL *x, const ASN1_TIME *tm)
{
	return X509_CRL_set_lastUpdate(x, tm);
}
LCRYPTO_ALIAS(X509_CRL_set1_lastUpdate);

int
X509_CRL_set_nextUpdate(X509_CRL *x, const ASN1_TIME *tm)
{
	ASN1_TIME *in;

	if (x == NULL)
		return 0;
	in = x->crl->nextUpdate;
	if (in != tm) {
		in = ASN1_STRING_dup(tm);
		if (in != NULL) {
			ASN1_TIME_free(x->crl->nextUpdate);
			x->crl->nextUpdate = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_CRL_set_nextUpdate);

int
X509_CRL_set1_nextUpdate(X509_CRL *x, const ASN1_TIME *tm)
{
	return X509_CRL_set_nextUpdate(x, tm);
}
LCRYPTO_ALIAS(X509_CRL_set1_nextUpdate);

int
X509_CRL_sort(X509_CRL *c)
{
	X509_REVOKED *r;
	int i;

	/* Sort the data so it will be written in serial number order */
	sk_X509_REVOKED_sort(c->crl->revoked);
	for (i = 0; i < sk_X509_REVOKED_num(c->crl->revoked); i++) {
		r = sk_X509_REVOKED_value(c->crl->revoked, i);
		r->sequence = i;
	}
	c->crl->enc.modified = 1;
	return 1;
}
LCRYPTO_ALIAS(X509_CRL_sort);

const STACK_OF(X509_EXTENSION) *
X509_REVOKED_get0_extensions(const X509_REVOKED *x)
{
	return x->extensions;
}
LCRYPTO_ALIAS(X509_REVOKED_get0_extensions);

const ASN1_TIME *
X509_REVOKED_get0_revocationDate(const X509_REVOKED *x)
{
	return x->revocationDate;
}
LCRYPTO_ALIAS(X509_REVOKED_get0_revocationDate);

const ASN1_INTEGER *
X509_REVOKED_get0_serialNumber(const X509_REVOKED *x)
{
	return x->serialNumber;
}
LCRYPTO_ALIAS(X509_REVOKED_get0_serialNumber);

int
X509_REVOKED_set_revocationDate(X509_REVOKED *x, ASN1_TIME *tm)
{
	ASN1_TIME *in;

	if (x == NULL)
		return 0;
	in = x->revocationDate;
	if (in != tm) {
		in = ASN1_STRING_dup(tm);
		if (in != NULL) {
			ASN1_TIME_free(x->revocationDate);
			x->revocationDate = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_REVOKED_set_revocationDate);

int
X509_REVOKED_set_serialNumber(X509_REVOKED *x, ASN1_INTEGER *serial)
{
	ASN1_INTEGER *in;

	if (x == NULL)
		return 0;
	in = x->serialNumber;
	if (in != serial) {
		in = ASN1_INTEGER_dup(serial);
		if (in != NULL) {
			ASN1_INTEGER_free(x->serialNumber);
			x->serialNumber = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_REVOKED_set_serialNumber);

int
i2d_re_X509_CRL_tbs(X509_CRL *crl, unsigned char **pp)
{
	crl->crl->enc.modified = 1;
	return i2d_X509_CRL_INFO(crl->crl, pp);
}
LCRYPTO_ALIAS(i2d_re_X509_CRL_tbs);
