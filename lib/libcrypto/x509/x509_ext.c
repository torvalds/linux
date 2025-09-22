/* $OpenBSD: x509_ext.c,v 1.18 2024/05/14 07:39:43 tb Exp $ */
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

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "x509_local.h"

int
X509_CRL_get_ext_count(const X509_CRL *x)
{
	return X509v3_get_ext_count(x->crl->extensions);
}
LCRYPTO_ALIAS(X509_CRL_get_ext_count);

int
X509_CRL_get_ext_by_NID(const X509_CRL *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->crl->extensions, nid, lastpos);
}
LCRYPTO_ALIAS(X509_CRL_get_ext_by_NID);

int
X509_CRL_get_ext_by_OBJ(const X509_CRL *x, const ASN1_OBJECT *obj, int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->crl->extensions, obj, lastpos);
}
LCRYPTO_ALIAS(X509_CRL_get_ext_by_OBJ);

int
X509_CRL_get_ext_by_critical(const X509_CRL *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(x->crl->extensions, crit, lastpos);
}
LCRYPTO_ALIAS(X509_CRL_get_ext_by_critical);

X509_EXTENSION *
X509_CRL_get_ext(const X509_CRL *x, int loc)
{
	return X509v3_get_ext(x->crl->extensions, loc);
}
LCRYPTO_ALIAS(X509_CRL_get_ext);

X509_EXTENSION *
X509_CRL_delete_ext(X509_CRL *x, int loc)
{
	return X509v3_delete_ext(x->crl->extensions, loc);
}
LCRYPTO_ALIAS(X509_CRL_delete_ext);

void *
X509_CRL_get_ext_d2i(const X509_CRL *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->crl->extensions, nid, crit, idx);
}
LCRYPTO_ALIAS(X509_CRL_get_ext_d2i);

int
X509_CRL_add1_ext_i2d(X509_CRL *x, int nid, void *value, int crit,
    unsigned long flags)
{
	return X509V3_add1_i2d(&x->crl->extensions, nid, value, crit, flags);
}
LCRYPTO_ALIAS(X509_CRL_add1_ext_i2d);

int
X509_CRL_add_ext(X509_CRL *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&x->crl->extensions, ex, loc) != NULL;
}
LCRYPTO_ALIAS(X509_CRL_add_ext);

int
X509_get_ext_count(const X509 *x)
{
	return X509v3_get_ext_count(x->cert_info->extensions);
}
LCRYPTO_ALIAS(X509_get_ext_count);

int
X509_get_ext_by_NID(const X509 *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->cert_info->extensions, nid, lastpos);
}
LCRYPTO_ALIAS(X509_get_ext_by_NID);

int
X509_get_ext_by_OBJ(const X509 *x, const ASN1_OBJECT *obj, int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->cert_info->extensions, obj, lastpos);
}
LCRYPTO_ALIAS(X509_get_ext_by_OBJ);

int
X509_get_ext_by_critical(const X509 *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(x->cert_info->extensions, crit,
	    lastpos);
}
LCRYPTO_ALIAS(X509_get_ext_by_critical);

X509_EXTENSION *
X509_get_ext(const X509 *x, int loc)
{
	return X509v3_get_ext(x->cert_info->extensions, loc);
}
LCRYPTO_ALIAS(X509_get_ext);

X509_EXTENSION *
X509_delete_ext(X509 *x, int loc)
{
	return X509v3_delete_ext(x->cert_info->extensions, loc);
}
LCRYPTO_ALIAS(X509_delete_ext);

int
X509_add_ext(X509 *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&x->cert_info->extensions, ex, loc) != NULL;
}
LCRYPTO_ALIAS(X509_add_ext);

void *
X509_get_ext_d2i(const X509 *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->cert_info->extensions, nid, crit, idx);
}
LCRYPTO_ALIAS(X509_get_ext_d2i);

int
X509_add1_ext_i2d(X509 *x, int nid, void *value, int crit, unsigned long flags)
{
	return X509V3_add1_i2d(&x->cert_info->extensions, nid, value, crit,
	    flags);
}
LCRYPTO_ALIAS(X509_add1_ext_i2d);

int
X509_REVOKED_get_ext_count(const X509_REVOKED *x)
{
	return X509v3_get_ext_count(x->extensions);
}
LCRYPTO_ALIAS(X509_REVOKED_get_ext_count);

int
X509_REVOKED_get_ext_by_NID(const X509_REVOKED *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->extensions, nid, lastpos);
}
LCRYPTO_ALIAS(X509_REVOKED_get_ext_by_NID);

int
X509_REVOKED_get_ext_by_OBJ(const X509_REVOKED *x, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->extensions, obj, lastpos);
}
LCRYPTO_ALIAS(X509_REVOKED_get_ext_by_OBJ);

int
X509_REVOKED_get_ext_by_critical(const X509_REVOKED *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(x->extensions, crit, lastpos);
}
LCRYPTO_ALIAS(X509_REVOKED_get_ext_by_critical);

X509_EXTENSION *
X509_REVOKED_get_ext(const X509_REVOKED *x, int loc)
{
	return X509v3_get_ext(x->extensions, loc);
}
LCRYPTO_ALIAS(X509_REVOKED_get_ext);

X509_EXTENSION *
X509_REVOKED_delete_ext(X509_REVOKED *x, int loc)
{
	return X509v3_delete_ext(x->extensions, loc);
}
LCRYPTO_ALIAS(X509_REVOKED_delete_ext);

int
X509_REVOKED_add_ext(X509_REVOKED *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&x->extensions, ex, loc) != NULL;
}
LCRYPTO_ALIAS(X509_REVOKED_add_ext);

void *
X509_REVOKED_get_ext_d2i(const X509_REVOKED *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->extensions, nid, crit, idx);
}
LCRYPTO_ALIAS(X509_REVOKED_get_ext_d2i);

int
X509_REVOKED_add1_ext_i2d(X509_REVOKED *x, int nid, void *value, int crit,
    unsigned long flags)
{
	return X509V3_add1_i2d(&x->extensions, nid, value, crit, flags);
}
LCRYPTO_ALIAS(X509_REVOKED_add1_ext_i2d);
