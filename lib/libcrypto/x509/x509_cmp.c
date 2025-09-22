/* $OpenBSD: x509_cmp.c,v 1.45 2025/05/10 05:54:39 tb Exp $ */
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

int
X509_issuer_and_serial_cmp(const X509 *a, const X509 *b)
{
	int i;
	X509_CINF *ai, *bi;

	ai = a->cert_info;
	bi = b->cert_info;
	i = ASN1_INTEGER_cmp(ai->serialNumber, bi->serialNumber);
	if (i)
		return (i);
	return (X509_NAME_cmp(ai->issuer, bi->issuer));
}
LCRYPTO_ALIAS(X509_issuer_and_serial_cmp);

#ifndef OPENSSL_NO_MD5
unsigned long
X509_issuer_and_serial_hash(X509 *a)
{
	unsigned long ret = 0;
	EVP_MD_CTX *md_ctx;
	unsigned char md[16];
	char *f = NULL;

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if ((f = X509_NAME_oneline(a->cert_info->issuer, NULL, 0)) == NULL)
		goto err;
	if (!EVP_DigestInit_ex(md_ctx, EVP_md5(), NULL))
		goto err;
	if (!EVP_DigestUpdate(md_ctx, (unsigned char *)f, strlen(f)))
		goto err;
	if (!EVP_DigestUpdate(md_ctx,
	    (unsigned char *)a->cert_info->serialNumber->data,
	    (unsigned long)a->cert_info->serialNumber->length))
		goto err;
	if (!EVP_DigestFinal_ex(md_ctx, &(md[0]), NULL))
		goto err;

	ret = (((unsigned long)md[0]) | ((unsigned long)md[1] << 8L) |
	    ((unsigned long)md[2] << 16L) | ((unsigned long)md[3] << 24L)) &
	    0xffffffffL;

err:
	EVP_MD_CTX_free(md_ctx);
	free(f);

	return ret;
}
LCRYPTO_ALIAS(X509_issuer_and_serial_hash);
#endif

int
X509_issuer_name_cmp(const X509 *a, const X509 *b)
{
	return (X509_NAME_cmp(a->cert_info->issuer, b->cert_info->issuer));
}
LCRYPTO_ALIAS(X509_issuer_name_cmp);

int
X509_subject_name_cmp(const X509 *a, const X509 *b)
{
	return (X509_NAME_cmp(a->cert_info->subject, b->cert_info->subject));
}
LCRYPTO_ALIAS(X509_subject_name_cmp);

int
X509_CRL_cmp(const X509_CRL *a, const X509_CRL *b)
{
	return (X509_NAME_cmp(a->crl->issuer, b->crl->issuer));
}
LCRYPTO_ALIAS(X509_CRL_cmp);

#ifndef OPENSSL_NO_SHA
int
X509_CRL_match(const X509_CRL *a, const X509_CRL *b)
{
	return memcmp(a->hash, b->hash, X509_CRL_HASH_LEN);
}
LCRYPTO_ALIAS(X509_CRL_match);
#endif

X509_NAME *
X509_get_issuer_name(const X509 *a)
{
	return (a->cert_info->issuer);
}
LCRYPTO_ALIAS(X509_get_issuer_name);

unsigned long
X509_issuer_name_hash(X509 *x)
{
	return (X509_NAME_hash(x->cert_info->issuer));
}
LCRYPTO_ALIAS(X509_issuer_name_hash);

#ifndef OPENSSL_NO_MD5
unsigned long
X509_issuer_name_hash_old(X509 *x)
{
	return (X509_NAME_hash_old(x->cert_info->issuer));
}
LCRYPTO_ALIAS(X509_issuer_name_hash_old);
#endif

X509_NAME *
X509_get_subject_name(const X509 *a)
{
	return (a->cert_info->subject);
}
LCRYPTO_ALIAS(X509_get_subject_name);

ASN1_INTEGER *
X509_get_serialNumber(X509 *a)
{
	return (a->cert_info->serialNumber);
}
LCRYPTO_ALIAS(X509_get_serialNumber);

const ASN1_INTEGER *
X509_get0_serialNumber(const X509 *a)
{
	return (a->cert_info->serialNumber);
}
LCRYPTO_ALIAS(X509_get0_serialNumber);

unsigned long
X509_subject_name_hash(X509 *x)
{
	return (X509_NAME_hash(x->cert_info->subject));
}
LCRYPTO_ALIAS(X509_subject_name_hash);

#ifndef OPENSSL_NO_MD5
unsigned long
X509_subject_name_hash_old(X509 *x)
{
	return (X509_NAME_hash_old(x->cert_info->subject));
}
LCRYPTO_ALIAS(X509_subject_name_hash_old);
#endif

#ifndef OPENSSL_NO_SHA
/* Compare two certificates: they must be identical for
 * this to work. NB: Although "cmp" operations are generally
 * prototyped to take "const" arguments (eg. for use in
 * STACKs), the way X509 handling is - these operations may
 * involve ensuring the hashes are up-to-date and ensuring
 * certain cert information is cached. So this is the point
 * where the "depth-first" constification tree has to halt
 * with an evil cast.
 */
int
X509_cmp(const X509 *a, const X509 *b)
{
	/* ensure hash is valid */
	X509_check_purpose((X509 *)a, -1, 0);
	X509_check_purpose((X509 *)b, -1, 0);

	return memcmp(a->hash, b->hash, X509_CERT_HASH_LEN);
}
LCRYPTO_ALIAS(X509_cmp);
#endif

int
X509_NAME_cmp(const X509_NAME *a, const X509_NAME *b)
{
	int ret;

	/* Ensure canonical encoding is present and up to date */
	if (!a->canon_enc || a->modified) {
		ret = i2d_X509_NAME((X509_NAME *)a, NULL);
		if (ret < 0)
			return -2;
	}
	if (!b->canon_enc || b->modified) {
		ret = i2d_X509_NAME((X509_NAME *)b, NULL);
		if (ret < 0)
			return -2;
	}
	ret = a->canon_enclen - b->canon_enclen;
	if (ret)
		return ret;
	return memcmp(a->canon_enc, b->canon_enc, a->canon_enclen);
}
LCRYPTO_ALIAS(X509_NAME_cmp);

unsigned long
X509_NAME_hash(X509_NAME *x)
{
	unsigned long ret = 0;
	unsigned char md[SHA_DIGEST_LENGTH];

	/* Make sure X509_NAME structure contains valid cached encoding */
	i2d_X509_NAME(x, NULL);
	if (!EVP_Digest(x->canon_enc, x->canon_enclen, md, NULL, EVP_sha1(),
	    NULL))
		return 0;

	ret = (((unsigned long)md[0]) | ((unsigned long)md[1] << 8L) |
	    ((unsigned long)md[2] << 16L) | ((unsigned long)md[3] << 24L)) &
	    0xffffffffL;
	return (ret);
}
LCRYPTO_ALIAS(X509_NAME_hash);


#ifndef OPENSSL_NO_MD5
/* I now DER encode the name and hash it.  Since I cache the DER encoding,
 * this is reasonably efficient. */

unsigned long
X509_NAME_hash_old(X509_NAME *x)
{
	EVP_MD_CTX *md_ctx;
	unsigned long ret = 0;
	unsigned char md[16];

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		return ret;

	/* Make sure X509_NAME structure contains valid cached encoding */
	i2d_X509_NAME(x, NULL);
	if (EVP_DigestInit_ex(md_ctx, EVP_md5(), NULL) &&
	    EVP_DigestUpdate(md_ctx, x->bytes->data, x->bytes->length) &&
	    EVP_DigestFinal_ex(md_ctx, md, NULL))
		ret = (((unsigned long)md[0]) |
		    ((unsigned long)md[1] << 8L) |
		    ((unsigned long)md[2] << 16L) |
		    ((unsigned long)md[3] << 24L)) &
		    0xffffffffL;

	EVP_MD_CTX_free(md_ctx);

	return ret;
}
LCRYPTO_ALIAS(X509_NAME_hash_old);
#endif

/* Search a stack of X509 for a match */
X509 *
X509_find_by_issuer_and_serial(STACK_OF(X509) *sk, X509_NAME *name,
    ASN1_INTEGER *serial)
{
	int i;
	X509_CINF cinf;
	X509 x, *x509 = NULL;

	if (!sk)
		return NULL;

	x.cert_info = &cinf;
	cinf.serialNumber = serial;
	cinf.issuer = name;

	for (i = 0; i < sk_X509_num(sk); i++) {
		x509 = sk_X509_value(sk, i);
		if (X509_issuer_and_serial_cmp(x509, &x) == 0)
			return (x509);
	}
	return (NULL);
}
LCRYPTO_ALIAS(X509_find_by_issuer_and_serial);

X509 *
X509_find_by_subject(STACK_OF(X509) *sk, X509_NAME *name)
{
	X509 *x509;
	int i;

	for (i = 0; i < sk_X509_num(sk); i++) {
		x509 = sk_X509_value(sk, i);
		if (X509_NAME_cmp(X509_get_subject_name(x509), name) == 0)
			return (x509);
	}
	return (NULL);
}
LCRYPTO_ALIAS(X509_find_by_subject);

EVP_PKEY *
X509_get_pubkey(X509 *x)
{
	if (x == NULL || x->cert_info == NULL)
		return (NULL);
	return (X509_PUBKEY_get(x->cert_info->key));
}
LCRYPTO_ALIAS(X509_get_pubkey);

EVP_PKEY *
X509_get0_pubkey(const X509 *x)
{
	if (x == NULL || x->cert_info == NULL)
		return (NULL);
	return (X509_PUBKEY_get0(x->cert_info->key));
}
LCRYPTO_ALIAS(X509_get0_pubkey);

ASN1_BIT_STRING *
X509_get0_pubkey_bitstr(const X509 *x)
{
	if (!x)
		return NULL;
	return x->cert_info->key->public_key;
}
LCRYPTO_ALIAS(X509_get0_pubkey_bitstr);

int
X509_check_private_key(const X509 *x, const EVP_PKEY *k)
{
	const EVP_PKEY *xk;
	int ret;

	xk = X509_get0_pubkey(x);

	if (xk)
		ret = EVP_PKEY_cmp(xk, k);
	else
		ret = -2;

	switch (ret) {
	case 1:
		break;
	case 0:
		X509error(X509_R_KEY_VALUES_MISMATCH);
		break;
	case -1:
		X509error(X509_R_KEY_TYPE_MISMATCH);
		break;
	case -2:
		X509error(X509_R_UNKNOWN_KEY_TYPE);
	}
	if (ret > 0)
		return 1;
	return 0;
}
LCRYPTO_ALIAS(X509_check_private_key);

/*
 * Not strictly speaking an "up_ref" as a STACK doesn't have a reference
 * count but it has the same effect by duping the STACK and upping the ref of
 * each X509 structure.
 */
STACK_OF(X509) *
X509_chain_up_ref(STACK_OF(X509) *chain)
{
	STACK_OF(X509) *ret;
	size_t i;

	ret = sk_X509_dup(chain);
	for (i = 0; i < sk_X509_num(ret); i++)
		X509_up_ref(sk_X509_value(ret, i));

	return ret;
}
LCRYPTO_ALIAS(X509_chain_up_ref);
