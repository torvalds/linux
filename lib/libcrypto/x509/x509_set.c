/* $OpenBSD: x509_set.c,v 1.29 2024/03/26 23:21:36 tb Exp $ */
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
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "x509_local.h"

const STACK_OF(X509_EXTENSION) *
X509_get0_extensions(const X509 *x)
{
	return x->cert_info->extensions;
}
LCRYPTO_ALIAS(X509_get0_extensions);

const X509_ALGOR *
X509_get0_tbs_sigalg(const X509 *x)
{
	return x->cert_info->signature;
}
LCRYPTO_ALIAS(X509_get0_tbs_sigalg);

int
X509_set_version(X509 *x, long version)
{
	if (x == NULL)
		return 0;
	/*
	 * RFC 5280, 4.1: versions 1 - 3 are specified as follows.
	 * Version  ::=  INTEGER  {  v1(0), v2(1), v3(2) }
	 */
	if (version < 0 || version > 2)
		return 0;
	if (x->cert_info->version == NULL) {
		if ((x->cert_info->version = ASN1_INTEGER_new()) == NULL)
			return 0;
	}
	x->cert_info->enc.modified = 1;
	return ASN1_INTEGER_set(x->cert_info->version, version);
}
LCRYPTO_ALIAS(X509_set_version);

long
X509_get_version(const X509 *x)
{
	return ASN1_INTEGER_get(x->cert_info->version);
}
LCRYPTO_ALIAS(X509_get_version);

int
X509_set_serialNumber(X509 *x, ASN1_INTEGER *serial)
{
	ASN1_INTEGER *in;

	if (x == NULL)
		return 0;
	in = x->cert_info->serialNumber;
	if (in != serial) {
		in = ASN1_INTEGER_dup(serial);
		if (in != NULL) {
			x->cert_info->enc.modified = 1;
			ASN1_INTEGER_free(x->cert_info->serialNumber);
			x->cert_info->serialNumber = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_set_serialNumber);

int
X509_set_issuer_name(X509 *x, X509_NAME *name)
{
	if (x == NULL || x->cert_info == NULL)
		return 0;
	x->cert_info->enc.modified = 1;
	return X509_NAME_set(&x->cert_info->issuer, name);
}
LCRYPTO_ALIAS(X509_set_issuer_name);

int
X509_set_subject_name(X509 *x, X509_NAME *name)
{
	if (x == NULL || x->cert_info == NULL)
		return 0;
	x->cert_info->enc.modified = 1;
	return X509_NAME_set(&x->cert_info->subject, name);
}
LCRYPTO_ALIAS(X509_set_subject_name);

const ASN1_TIME *
X509_get0_notBefore(const X509 *x)
{
	return X509_getm_notBefore(x);
}
LCRYPTO_ALIAS(X509_get0_notBefore);

ASN1_TIME *
X509_getm_notBefore(const X509 *x)
{
	if (x == NULL || x->cert_info == NULL || x->cert_info->validity == NULL)
		return NULL;
	return x->cert_info->validity->notBefore;
}
LCRYPTO_ALIAS(X509_getm_notBefore);

int
X509_set_notBefore(X509 *x, const ASN1_TIME *tm)
{
	ASN1_TIME *in;

	if (x == NULL || x->cert_info->validity == NULL)
		return 0;
	in = x->cert_info->validity->notBefore;
	if (in != tm) {
		in = ASN1_STRING_dup(tm);
		if (in != NULL) {
			x->cert_info->enc.modified = 1;
			ASN1_TIME_free(x->cert_info->validity->notBefore);
			x->cert_info->validity->notBefore = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_set_notBefore);

int
X509_set1_notBefore(X509 *x, const ASN1_TIME *tm)
{
	return X509_set_notBefore(x, tm);
}
LCRYPTO_ALIAS(X509_set1_notBefore);

const ASN1_TIME *
X509_get0_notAfter(const X509 *x)
{
	return X509_getm_notAfter(x);
}
LCRYPTO_ALIAS(X509_get0_notAfter);

ASN1_TIME *
X509_getm_notAfter(const X509 *x)
{
	if (x == NULL || x->cert_info == NULL || x->cert_info->validity == NULL)
		return NULL;
	return x->cert_info->validity->notAfter;
}
LCRYPTO_ALIAS(X509_getm_notAfter);

int
X509_set_notAfter(X509 *x, const ASN1_TIME *tm)
{
	ASN1_TIME *in;

	if (x == NULL || x->cert_info->validity == NULL)
		return 0;
	in = x->cert_info->validity->notAfter;
	if (in != tm) {
		in = ASN1_STRING_dup(tm);
		if (in != NULL) {
			x->cert_info->enc.modified = 1;
			ASN1_TIME_free(x->cert_info->validity->notAfter);
			x->cert_info->validity->notAfter = in;
		}
	}
	return in != NULL;
}
LCRYPTO_ALIAS(X509_set_notAfter);

int
X509_set1_notAfter(X509 *x, const ASN1_TIME *tm)
{
	return X509_set_notAfter(x, tm);
}
LCRYPTO_ALIAS(X509_set1_notAfter);

int
X509_set_pubkey(X509 *x, EVP_PKEY *pkey)
{
	if (x == NULL || x->cert_info == NULL)
		return 0;
	x->cert_info->enc.modified = 1;
	return X509_PUBKEY_set(&x->cert_info->key, pkey);
}
LCRYPTO_ALIAS(X509_set_pubkey);

int
X509_get_signature_type(const X509 *x)
{
	return EVP_PKEY_type(OBJ_obj2nid(x->sig_alg->algorithm));
}
LCRYPTO_ALIAS(X509_get_signature_type);

X509_PUBKEY *
X509_get_X509_PUBKEY(const X509 *x)
{
	return x->cert_info->key;
}
LCRYPTO_ALIAS(X509_get_X509_PUBKEY);

void
X509_get0_uids(const X509 *x, const ASN1_BIT_STRING **issuerUID,
    const ASN1_BIT_STRING **subjectUID)
{
	if (issuerUID != NULL)
		*issuerUID = x->cert_info->issuerUID;
	if (subjectUID != NULL)
		*subjectUID = x->cert_info->subjectUID;
}
LCRYPTO_ALIAS(X509_get0_uids);
