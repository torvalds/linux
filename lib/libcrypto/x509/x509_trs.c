/* $OpenBSD: x509_trs.c,v 1.58 2024/08/31 10:12:23 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
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

#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "x509_internal.h"
#include "x509_local.h"

static int
trust_if_self_signed(const X509 *x)
{
	/* Extensions already cached in X509_check_trust(). */
	if ((x->ex_flags & EXFLAG_SS) != 0)
		return X509_TRUST_TRUSTED;

	return X509_TRUST_UNTRUSTED;
}

static int
trust_was_set(const X509 *x)
{
	return x->aux != NULL && (x->aux->trust != NULL ||
	    x->aux->reject != NULL);
}

static int
obj_trust(int id, const X509 *x)
{
	const X509_CERT_AUX *aux;
	ASN1_OBJECT *obj;
	int i, nid;

	if ((aux = x->aux) == NULL)
		return X509_TRUST_UNTRUSTED;

	for (i = 0; i < sk_ASN1_OBJECT_num(aux->reject); i++) {
		obj = sk_ASN1_OBJECT_value(aux->reject, i);
		nid = OBJ_obj2nid(obj);
		if (nid == id || nid == NID_anyExtendedKeyUsage)
			return X509_TRUST_REJECTED;
	}

	for (i = 0; i < sk_ASN1_OBJECT_num(aux->trust); i++) {
		obj = sk_ASN1_OBJECT_value(aux->trust, i);
		nid = OBJ_obj2nid(obj);
		if (nid == id || nid == NID_anyExtendedKeyUsage)
			return X509_TRUST_TRUSTED;
	}

	return X509_TRUST_UNTRUSTED;
}

static int
nid_from_trust_id(int trust_id)
{
	OPENSSL_assert(trust_id == 0 ||
	    (trust_id >= X509_TRUST_MIN && trust_id <= X509_TRUST_MAX));

	switch (trust_id) {
	case X509_TRUST_COMPAT:
		return NID_undef;
	case X509_TRUST_SSL_CLIENT:
		return NID_client_auth;
	case X509_TRUST_SSL_SERVER:
		return NID_server_auth;
	case X509_TRUST_EMAIL:
		return NID_email_protect;
	case X509_TRUST_OBJECT_SIGN:
		return NID_code_sign;
	case X509_TRUST_OCSP_SIGN:
		return NID_OCSP_sign;
	case X509_TRUST_OCSP_REQUEST:
		return NID_ad_OCSP;
	case X509_TRUST_TSA:
		return NID_time_stamp;
	default:
		return NID_undef;
	}
}

int
X509_check_trust(X509 *x, int trust_id, int flags)
{
	int rv;

	/* Call early so the trust handlers don't need to modify the certs. */
	if (!x509v3_cache_extensions(x))
		return X509_TRUST_UNTRUSTED;

	if (trust_id == X509_TRUST_ACCEPT_ALL)
		return 1;

	switch (trust_id) {
	case X509_TRUST_COMPAT:
		return trust_if_self_signed(x);
	case X509_TRUST_EMAIL:
	case X509_TRUST_OBJECT_SIGN:
	case X509_TRUST_SSL_SERVER:
	case X509_TRUST_SSL_CLIENT:
	case X509_TRUST_TSA:
		if (trust_was_set(x))
			return obj_trust(nid_from_trust_id(trust_id), x);
		return trust_if_self_signed(x);
	case X509_TRUST_OCSP_SIGN:
	case X509_TRUST_OCSP_REQUEST:
		return obj_trust(nid_from_trust_id(trust_id), x);
	default:
		rv = obj_trust(NID_anyExtendedKeyUsage, x);
		if (rv != X509_TRUST_UNTRUSTED)
			return rv;
		return trust_if_self_signed(x);
	}
}
