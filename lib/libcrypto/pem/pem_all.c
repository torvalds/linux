/* $OpenBSD: pem_all.c,v 1.21 2023/07/07 13:40:44 beck Exp $ */
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
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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

#include <openssl/opensslconf.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>

#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif
#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#ifndef OPENSSL_NO_RSA
static RSA *pkey_get_rsa(EVP_PKEY *key, RSA **rsa);
#endif
#ifndef OPENSSL_NO_DSA
static DSA *pkey_get_dsa(EVP_PKEY *key, DSA **dsa);
#endif

#ifndef OPENSSL_NO_EC
static EC_KEY *pkey_get_eckey(EVP_PKEY *key, EC_KEY **eckey);
#endif


X509_REQ *
PEM_read_X509_REQ(FILE *fp, X509_REQ **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_X509_REQ, PEM_STRING_X509_REQ, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_X509_REQ);

int
PEM_write_X509_REQ(FILE *fp, X509_REQ *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_X509_REQ, PEM_STRING_X509_REQ, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_X509_REQ);

X509_REQ *
PEM_read_bio_X509_REQ(BIO *bp, X509_REQ **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_X509_REQ, PEM_STRING_X509_REQ, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_X509_REQ);

int
PEM_write_bio_X509_REQ(BIO *bp, X509_REQ *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_X509_REQ, PEM_STRING_X509_REQ, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_X509_REQ);

int
PEM_write_X509_REQ_NEW(FILE *fp, X509_REQ *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_X509_REQ, PEM_STRING_X509_REQ_OLD, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_X509_REQ_NEW);

int
PEM_write_bio_X509_REQ_NEW(BIO *bp, X509_REQ *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_X509_REQ, PEM_STRING_X509_REQ_OLD, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_X509_REQ_NEW);

X509_CRL *
PEM_read_X509_CRL(FILE *fp, X509_CRL **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_X509_CRL, PEM_STRING_X509_CRL, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_X509_CRL);

int
PEM_write_X509_CRL(FILE *fp, X509_CRL *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_X509_CRL, PEM_STRING_X509_CRL, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_X509_CRL);

X509_CRL *
PEM_read_bio_X509_CRL(BIO *bp, X509_CRL **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_X509_CRL, PEM_STRING_X509_CRL, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_X509_CRL);

int
PEM_write_bio_X509_CRL(BIO *bp, X509_CRL *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_X509_CRL, PEM_STRING_X509_CRL, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_X509_CRL);

PKCS7 *
PEM_read_PKCS7(FILE *fp, PKCS7 **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_PKCS7, PEM_STRING_PKCS7, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_PKCS7);

int
PEM_write_PKCS7(FILE *fp, PKCS7 *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_PKCS7, PEM_STRING_PKCS7, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_PKCS7);

PKCS7 *
PEM_read_bio_PKCS7(BIO *bp, PKCS7 **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_PKCS7, PEM_STRING_PKCS7, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_PKCS7);

int
PEM_write_bio_PKCS7(BIO *bp, PKCS7 *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_PKCS7, PEM_STRING_PKCS7, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_PKCS7);

#ifndef OPENSSL_NO_RSA

/* We treat RSA or DSA private keys as a special case.
 *
 * For private keys we read in an EVP_PKEY structure with
 * PEM_read_bio_PrivateKey() and extract the relevant private
 * key: this means can handle "traditional" and PKCS#8 formats
 * transparently.
 */

static RSA *
pkey_get_rsa(EVP_PKEY *key, RSA **rsa)
{
	RSA *rtmp;

	if (!key)
		return NULL;
	rtmp = EVP_PKEY_get1_RSA(key);
	EVP_PKEY_free(key);
	if (!rtmp)
		return NULL;
	if (rsa) {
		RSA_free(*rsa);
		*rsa = rtmp;
	}
	return rtmp;
}

RSA *
PEM_read_RSAPrivateKey(FILE *fp, RSA **rsa, pem_password_cb *cb, void *u)
{
	EVP_PKEY *pktmp;

	pktmp = PEM_read_PrivateKey(fp, NULL, cb, u);
	return pkey_get_rsa(pktmp, rsa);
}
LCRYPTO_ALIAS(PEM_read_RSAPrivateKey);

int
PEM_write_RSAPrivateKey(FILE *fp, RSA *x, const EVP_CIPHER *enc,
    unsigned char *kstr, int klen, pem_password_cb *cb, void *u)
{
        return PEM_ASN1_write((i2d_of_void *)i2d_RSAPrivateKey, PEM_STRING_RSA, fp,
	    x, enc, kstr, klen, cb, u);
}
LCRYPTO_ALIAS(PEM_write_RSAPrivateKey);

RSA *
PEM_read_bio_RSAPrivateKey(BIO *bp, RSA **rsa, pem_password_cb *cb, void *u)
{
	EVP_PKEY *pktmp;

	pktmp = PEM_read_bio_PrivateKey(bp, NULL, cb, u);
	return pkey_get_rsa(pktmp, rsa);
}
LCRYPTO_ALIAS(PEM_read_bio_RSAPrivateKey);

int
PEM_write_bio_RSAPrivateKey(BIO *bp, RSA *x,
    const EVP_CIPHER *enc, unsigned char *kstr, int klen, pem_password_cb *cb,
    void *u)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_RSAPrivateKey, PEM_STRING_RSA, bp,
	    x, enc, kstr, klen, cb, u);
}
LCRYPTO_ALIAS(PEM_write_bio_RSAPrivateKey);

RSA *
PEM_read_RSAPublicKey(FILE *fp, RSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_RSAPublicKey, PEM_STRING_RSA_PUBLIC, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_RSAPublicKey);

int
PEM_write_RSAPublicKey(FILE *fp, const RSA *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_RSAPublicKey, PEM_STRING_RSA_PUBLIC, fp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_RSAPublicKey);

RSA *
PEM_read_bio_RSAPublicKey(BIO *bp, RSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_RSAPublicKey, PEM_STRING_RSA_PUBLIC, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_RSAPublicKey);

int
PEM_write_bio_RSAPublicKey(BIO *bp, const RSA *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_RSAPublicKey, PEM_STRING_RSA_PUBLIC, bp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_RSAPublicKey);

RSA *
PEM_read_RSA_PUBKEY(FILE *fp, RSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_RSA_PUBKEY, PEM_STRING_PUBLIC, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_RSA_PUBKEY);

int
PEM_write_RSA_PUBKEY(FILE *fp, RSA *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_RSA_PUBKEY, PEM_STRING_PUBLIC, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_RSA_PUBKEY);

RSA *
PEM_read_bio_RSA_PUBKEY(BIO *bp, RSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_RSA_PUBKEY, PEM_STRING_PUBLIC, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_RSA_PUBKEY);

int
PEM_write_bio_RSA_PUBKEY(BIO *bp, RSA *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_RSA_PUBKEY, PEM_STRING_PUBLIC, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_RSA_PUBKEY);

#endif

#ifndef OPENSSL_NO_DSA

static DSA *
pkey_get_dsa(EVP_PKEY *key, DSA **dsa)
{
	DSA *dtmp;

	if (!key)
		return NULL;
	dtmp = EVP_PKEY_get1_DSA(key);
	EVP_PKEY_free(key);
	if (!dtmp)
		return NULL;
	if (dsa) {
		DSA_free(*dsa);
		*dsa = dtmp;
	}
	return dtmp;
}

DSA *
PEM_read_DSAPrivateKey(FILE *fp, DSA **dsa, pem_password_cb *cb, void *u)
{
	EVP_PKEY *pktmp;

	pktmp = PEM_read_PrivateKey(fp, NULL, cb, u);
	return pkey_get_dsa(pktmp, dsa);	/* will free pktmp */
}
LCRYPTO_ALIAS(PEM_read_DSAPrivateKey);

int
PEM_write_DSAPrivateKey(FILE *fp, DSA *x, const EVP_CIPHER *enc,
    unsigned char *kstr, int klen, pem_password_cb *cb, void *u)
{
        return PEM_ASN1_write((i2d_of_void *)i2d_DSAPrivateKey, PEM_STRING_DSA, fp,
	    x, enc, kstr, klen, cb, u);
}
LCRYPTO_ALIAS(PEM_write_DSAPrivateKey);

DSA *
PEM_read_bio_DSAPrivateKey(BIO *bp, DSA **dsa, pem_password_cb *cb, void *u)
{
	EVP_PKEY *pktmp;

	pktmp = PEM_read_bio_PrivateKey(bp, NULL, cb, u);
	return pkey_get_dsa(pktmp, dsa);	/* will free pktmp */
}
LCRYPTO_ALIAS(PEM_read_bio_DSAPrivateKey);

int
PEM_write_bio_DSAPrivateKey(BIO *bp, DSA *x,
    const EVP_CIPHER *enc, unsigned char *kstr, int klen, pem_password_cb *cb,
    void *u)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_DSAPrivateKey, PEM_STRING_DSA, bp,
	    x, enc, kstr, klen, cb, u);
}
LCRYPTO_ALIAS(PEM_write_bio_DSAPrivateKey);

DSA *
PEM_read_DSA_PUBKEY(FILE *fp, DSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_DSA_PUBKEY, PEM_STRING_PUBLIC, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_DSA_PUBKEY);

int
PEM_write_DSA_PUBKEY(FILE *fp, DSA *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_DSA_PUBKEY, PEM_STRING_PUBLIC, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_DSA_PUBKEY);

int
PEM_write_bio_DSA_PUBKEY(BIO *bp, DSA *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_DSA_PUBKEY, PEM_STRING_PUBLIC, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_DSA_PUBKEY);

DSA *
PEM_read_bio_DSA_PUBKEY(BIO *bp, DSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_DSA_PUBKEY, PEM_STRING_PUBLIC, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_DSA_PUBKEY);

DSA *
PEM_read_DSAparams(FILE *fp, DSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_DSAparams, PEM_STRING_DSAPARAMS, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_DSAparams);

int
PEM_write_DSAparams(FILE *fp, const DSA *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_DSAparams, PEM_STRING_DSAPARAMS, fp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_DSAparams);

DSA *
PEM_read_bio_DSAparams(BIO *bp, DSA **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_DSAparams, PEM_STRING_DSAPARAMS, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_DSAparams);

int
PEM_write_bio_DSAparams(BIO *bp, const DSA *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_DSAparams, PEM_STRING_DSAPARAMS, bp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_DSAparams);

#endif


#ifndef OPENSSL_NO_EC
static EC_KEY *
pkey_get_eckey(EVP_PKEY *key, EC_KEY **eckey)
{
	EC_KEY *dtmp;

	if (!key)
		return NULL;
	dtmp = EVP_PKEY_get1_EC_KEY(key);
	EVP_PKEY_free(key);
	if (!dtmp)
		return NULL;
	if (eckey) {
		EC_KEY_free(*eckey);
		*eckey = dtmp;
	}
	return dtmp;
}

EC_GROUP *
PEM_read_ECPKParameters(FILE *fp, EC_GROUP **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_ECPKParameters, PEM_STRING_ECPARAMETERS, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_ECPKParameters);

int
PEM_write_ECPKParameters(FILE *fp, const EC_GROUP *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_ECPKParameters, PEM_STRING_ECPARAMETERS, fp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_ECPKParameters);

EC_GROUP *
PEM_read_bio_ECPKParameters(BIO *bp, EC_GROUP **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_ECPKParameters, PEM_STRING_ECPARAMETERS, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_ECPKParameters);

int
PEM_write_bio_ECPKParameters(BIO *bp, const EC_GROUP *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_ECPKParameters, PEM_STRING_ECPARAMETERS, bp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_ECPKParameters);

EC_KEY *
PEM_read_ECPrivateKey(FILE *fp, EC_KEY **eckey, pem_password_cb *cb, void *u)
{
	EVP_PKEY *pktmp;

	pktmp = PEM_read_PrivateKey(fp, NULL, cb, u);
	return pkey_get_eckey(pktmp, eckey);	/* will free pktmp */
}
LCRYPTO_ALIAS(PEM_read_ECPrivateKey);

int
PEM_write_ECPrivateKey(FILE *fp, EC_KEY *x, const EVP_CIPHER *enc,
    unsigned char *kstr, int klen, pem_password_cb *cb, void *u)
{
        return PEM_ASN1_write((i2d_of_void *)i2d_ECPrivateKey, PEM_STRING_ECPRIVATEKEY, fp,
	    x, enc, kstr, klen, cb, u);
}
LCRYPTO_ALIAS(PEM_write_ECPrivateKey);

EC_KEY *
PEM_read_bio_ECPrivateKey(BIO *bp, EC_KEY **key, pem_password_cb *cb, void *u)
{
	EVP_PKEY *pktmp;
	pktmp = PEM_read_bio_PrivateKey(bp, NULL, cb, u);
	return pkey_get_eckey(pktmp, key);	/* will free pktmp */
}
LCRYPTO_ALIAS(PEM_read_bio_ECPrivateKey);

int
PEM_write_bio_ECPrivateKey(BIO *bp, EC_KEY *x,
    const EVP_CIPHER *enc, unsigned char *kstr, int klen, pem_password_cb *cb,
    void *u)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_ECPrivateKey, PEM_STRING_ECPRIVATEKEY, bp,
	    x, enc, kstr, klen, cb, u);
}
LCRYPTO_ALIAS(PEM_write_bio_ECPrivateKey);

EC_KEY *
PEM_read_EC_PUBKEY(FILE *fp, EC_KEY **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_EC_PUBKEY, PEM_STRING_PUBLIC, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_EC_PUBKEY);

int
PEM_write_EC_PUBKEY(FILE *fp, EC_KEY *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_EC_PUBKEY, PEM_STRING_PUBLIC, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_EC_PUBKEY);

EC_KEY *
PEM_read_bio_EC_PUBKEY(BIO *bp, EC_KEY **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_EC_PUBKEY, PEM_STRING_PUBLIC, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_EC_PUBKEY);

int
PEM_write_bio_EC_PUBKEY(BIO *bp, EC_KEY *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_EC_PUBKEY, PEM_STRING_PUBLIC, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_EC_PUBKEY);

#endif

#ifndef OPENSSL_NO_DH

DH *
PEM_read_DHparams(FILE *fp, DH **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_DHparams, PEM_STRING_DHPARAMS, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_DHparams);

int
PEM_write_DHparams(FILE *fp, const DH *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_DHparams, PEM_STRING_DHPARAMS, fp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_DHparams);

DH *
PEM_read_bio_DHparams(BIO *bp, DH **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_DHparams, PEM_STRING_DHPARAMS, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_DHparams);

int
PEM_write_bio_DHparams(BIO *bp, const DH *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_DHparams, PEM_STRING_DHPARAMS, bp,
	    (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_DHparams);

#endif

EVP_PKEY *
PEM_read_PUBKEY(FILE *fp, EVP_PKEY **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_PUBKEY, PEM_STRING_PUBLIC, fp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_PUBKEY);

int
PEM_write_PUBKEY(FILE *fp, EVP_PKEY *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_PUBKEY, PEM_STRING_PUBLIC, fp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_PUBKEY);

EVP_PKEY *
PEM_read_bio_PUBKEY(BIO *bp, EVP_PKEY **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_PUBKEY, PEM_STRING_PUBLIC, bp,
	    (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_PUBKEY);

int
PEM_write_bio_PUBKEY(BIO *bp, EVP_PKEY *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_PUBKEY, PEM_STRING_PUBLIC, bp,
	    x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_PUBKEY);
