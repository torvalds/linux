/* $OpenBSD: ssl_rsa.c,v 1.53 2025/08/14 15:55:54 tb Exp $ */
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

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "ssl_local.h"

static int ssl_get_password_cb_and_arg(SSL_CTX *ctx, SSL *ssl,
    pem_password_cb **passwd_cb, void **passwd_arg);
static int ssl_set_cert(SSL_CTX *ctx, SSL *ssl, X509 *x509);
static int ssl_set_pkey(SSL_CTX *ctx, SSL *ssl, EVP_PKEY *pkey);
static int ssl_use_certificate_chain_bio(SSL_CTX *ctx, SSL *ssl, BIO *in);
static int ssl_use_certificate_chain_file(SSL_CTX *ctx, SSL *ssl,
    const char *file);

int
SSL_use_certificate(SSL *ssl, X509 *x)
{
	if (x == NULL) {
		SSLerror(ssl, ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}
	return ssl_set_cert(NULL, ssl, x);
}
LSSL_ALIAS(SSL_use_certificate);

int
SSL_use_certificate_file(SSL *ssl, const char *file, int type)
{
	int j;
	BIO *in;
	int ret = 0;
	X509 *x = NULL;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerror(ssl, ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerror(ssl, ERR_R_SYS_LIB);
		goto end;
	}
	if (type == SSL_FILETYPE_ASN1) {
		j = ERR_R_ASN1_LIB;
		x = d2i_X509_bio(in, NULL);
	} else if (type == SSL_FILETYPE_PEM) {
		j = ERR_R_PEM_LIB;
		x = PEM_read_bio_X509(in, NULL,
		    ssl->ctx->default_passwd_callback,
		    ssl->ctx->default_passwd_callback_userdata);
	} else {
		SSLerror(ssl, SSL_R_BAD_SSL_FILETYPE);
		goto end;
	}

	if (x == NULL) {
		SSLerror(ssl, j);
		goto end;
	}

	ret = SSL_use_certificate(ssl, x);
 end:
	X509_free(x);
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_use_certificate_file);

int
SSL_use_certificate_ASN1(SSL *ssl, const unsigned char *d, int len)
{
	X509 *x;
	int ret;

	x = d2i_X509(NULL, &d, (long)len);
	if (x == NULL) {
		SSLerror(ssl, ERR_R_ASN1_LIB);
		return (0);
	}

	ret = SSL_use_certificate(ssl, x);
	X509_free(x);
	return (ret);
}
LSSL_ALIAS(SSL_use_certificate_ASN1);

int
SSL_use_RSAPrivateKey(SSL *ssl, RSA *rsa)
{
	EVP_PKEY *pkey = NULL;
	int ret = 0;

	if (rsa == NULL) {
		SSLerror(ssl, ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	if ((pkey = EVP_PKEY_new()) == NULL) {
		SSLerror(ssl, ERR_R_EVP_LIB);
		goto err;
	}
	if (!EVP_PKEY_set1_RSA(pkey, rsa))
		goto err;
	if (!ssl_set_pkey(NULL, ssl, pkey))
		goto err;

	ret = 1;

 err:
	EVP_PKEY_free(pkey);

	return ret;
}
LSSL_ALIAS(SSL_use_RSAPrivateKey);

static int
ssl_set_pkey(SSL_CTX *ctx, SSL *ssl, EVP_PKEY *pkey)
{
	SSL_CERT *c;
	int i;

	i = ssl_cert_type(pkey);
	if (i < 0) {
		SSLerrorx(SSL_R_UNKNOWN_CERTIFICATE_TYPE);
		return (0);
	}

	if ((c = ssl_get0_cert(ctx, ssl)) == NULL)
		return (0);

	if (c->pkeys[i].x509 != NULL) {
		EVP_PKEY *pktmp;

		if ((pktmp = X509_get0_pubkey(c->pkeys[i].x509)) == NULL)
			return 0;

		/*
		 * Callers of EVP_PKEY_copy_parameters() can't distinguish
		 * errors from the absence of a param_copy() method. So
		 * pretend it can never fail.
		 */
		EVP_PKEY_copy_parameters(pktmp, pkey);

		ERR_clear_error();

		/*
		 * Don't check the public/private key, this is mostly
		 * for smart cards.
		 */
		if (EVP_PKEY_id(pkey) != EVP_PKEY_RSA ||
		    !(RSA_flags(EVP_PKEY_get0_RSA(pkey)) & RSA_METHOD_FLAG_NO_CHECK)) {
			if (!X509_check_private_key(c->pkeys[i].x509, pkey)) {
				X509_free(c->pkeys[i].x509);
				c->pkeys[i].x509 = NULL;
				return 0;
			}
		}
	}

	EVP_PKEY_free(c->pkeys[i].privatekey);
	EVP_PKEY_up_ref(pkey);
	c->pkeys[i].privatekey = pkey;
	c->key = &(c->pkeys[i]);

	c->valid = 0;
	return 1;
}

int
SSL_use_RSAPrivateKey_file(SSL *ssl, const char *file, int type)
{
	int j, ret = 0;
	BIO *in;
	RSA *rsa = NULL;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerror(ssl, ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerror(ssl, ERR_R_SYS_LIB);
		goto end;
	}
	if (type == SSL_FILETYPE_ASN1) {
		j = ERR_R_ASN1_LIB;
		rsa = d2i_RSAPrivateKey_bio(in, NULL);
	} else if (type == SSL_FILETYPE_PEM) {
		j = ERR_R_PEM_LIB;
		rsa = PEM_read_bio_RSAPrivateKey(in, NULL,
		    ssl->ctx->default_passwd_callback,
		    ssl->ctx->default_passwd_callback_userdata);
	} else {
		SSLerror(ssl, SSL_R_BAD_SSL_FILETYPE);
		goto end;
	}
	if (rsa == NULL) {
		SSLerror(ssl, j);
		goto end;
	}
	ret = SSL_use_RSAPrivateKey(ssl, rsa);
	RSA_free(rsa);
 end:
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_use_RSAPrivateKey_file);

int
SSL_use_RSAPrivateKey_ASN1(SSL *ssl, const unsigned char *d, long len)
{
	int ret;
	RSA *rsa;

	if ((rsa = d2i_RSAPrivateKey(NULL, &d, (long)len)) == NULL) {
		SSLerror(ssl, ERR_R_ASN1_LIB);
		return (0);
	}

	ret = SSL_use_RSAPrivateKey(ssl, rsa);
	RSA_free(rsa);
	return (ret);
}
LSSL_ALIAS(SSL_use_RSAPrivateKey_ASN1);

int
SSL_use_PrivateKey(SSL *ssl, EVP_PKEY *pkey)
{
	int ret;

	if (pkey == NULL) {
		SSLerror(ssl, ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}
	ret = ssl_set_pkey(NULL, ssl, pkey);
	return (ret);
}
LSSL_ALIAS(SSL_use_PrivateKey);

int
SSL_use_PrivateKey_file(SSL *ssl, const char *file, int type)
{
	int j, ret = 0;
	BIO *in;
	EVP_PKEY *pkey = NULL;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerror(ssl, ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerror(ssl, ERR_R_SYS_LIB);
		goto end;
	}
	if (type == SSL_FILETYPE_PEM) {
		j = ERR_R_PEM_LIB;
		pkey = PEM_read_bio_PrivateKey(in, NULL,
		    ssl->ctx->default_passwd_callback,
		    ssl->ctx->default_passwd_callback_userdata);
	} else if (type == SSL_FILETYPE_ASN1) {
		j = ERR_R_ASN1_LIB;
		pkey = d2i_PrivateKey_bio(in, NULL);
	} else {
		SSLerror(ssl, SSL_R_BAD_SSL_FILETYPE);
		goto end;
	}
	if (pkey == NULL) {
		SSLerror(ssl, j);
		goto end;
	}
	ret = SSL_use_PrivateKey(ssl, pkey);
	EVP_PKEY_free(pkey);
 end:
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_use_PrivateKey_file);

int
SSL_use_PrivateKey_ASN1(int type, SSL *ssl, const unsigned char *d, long len)
{
	int ret;
	EVP_PKEY *pkey;

	if ((pkey = d2i_PrivateKey(type, NULL, &d, (long)len)) == NULL) {
		SSLerror(ssl, ERR_R_ASN1_LIB);
		return (0);
	}

	ret = SSL_use_PrivateKey(ssl, pkey);
	EVP_PKEY_free(pkey);
	return (ret);
}
LSSL_ALIAS(SSL_use_PrivateKey_ASN1);

int
SSL_CTX_use_certificate(SSL_CTX *ctx, X509 *x)
{
	if (x == NULL) {
		SSLerrorx(ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}
	return ssl_set_cert(ctx, NULL, x);
}
LSSL_ALIAS(SSL_CTX_use_certificate);

static int
ssl_get_password_cb_and_arg(SSL_CTX *ctx, SSL *ssl,
    pem_password_cb **passwd_cb, void **passwd_arg)
{
	if (ssl != NULL)
		ctx = ssl->ctx;

	*passwd_cb = ctx->default_passwd_callback;
	*passwd_arg = ctx->default_passwd_callback_userdata;

	return 1;
}

static int
ssl_set_cert(SSL_CTX *ctx, SSL *ssl, X509 *x)
{
	SSL_CERT *c;
	EVP_PKEY *pkey;
	int ssl_err;
	int i;

	if (!ssl_security_cert(ctx, ssl, x, 1, &ssl_err)) {
		SSLerrorx(ssl_err);
		return (0);
	}

	if ((c = ssl_get0_cert(ctx, ssl)) == NULL)
		return (0);

	pkey = X509_get_pubkey(x);
	if (pkey == NULL) {
		SSLerrorx(SSL_R_X509_LIB);
		return (0);
	}

	i = ssl_cert_type(pkey);
	if (i < 0) {
		SSLerrorx(SSL_R_UNKNOWN_CERTIFICATE_TYPE);
		EVP_PKEY_free(pkey);
		return (0);
	}

	if (c->pkeys[i].privatekey != NULL) {
		EVP_PKEY *priv_key = c->pkeys[i].privatekey;

		EVP_PKEY_copy_parameters(pkey, priv_key);
		ERR_clear_error();

		/*
		 * Don't check the public/private key, this is mostly
		 * for smart cards.
		 */
		if (EVP_PKEY_id(priv_key) != EVP_PKEY_RSA ||
		    !(RSA_flags(EVP_PKEY_get0_RSA(priv_key)) & RSA_METHOD_FLAG_NO_CHECK)) {
			if (!X509_check_private_key(x, priv_key)) {
				/*
				 * don't fail for a cert/key mismatch, just free
				 * current private key (when switching to a
				 * different cert & key, first this function
				 * should be used, then ssl_set_pkey.
				 */
				EVP_PKEY_free(c->pkeys[i].privatekey);
				c->pkeys[i].privatekey = NULL;
				ERR_clear_error();
			}
		}
	}

	EVP_PKEY_free(pkey);

	X509_free(c->pkeys[i].x509);
	X509_up_ref(x);
	c->pkeys[i].x509 = x;
	c->key = &(c->pkeys[i]);

	c->valid = 0;
	return (1);
}

int
SSL_CTX_use_certificate_file(SSL_CTX *ctx, const char *file, int type)
{
	int j;
	BIO *in;
	int ret = 0;
	X509 *x = NULL;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerrorx(ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerrorx(ERR_R_SYS_LIB);
		goto end;
	}
	if (type == SSL_FILETYPE_ASN1) {
		j = ERR_R_ASN1_LIB;
		x = d2i_X509_bio(in, NULL);
	} else if (type == SSL_FILETYPE_PEM) {
		j = ERR_R_PEM_LIB;
		x = PEM_read_bio_X509(in, NULL, ctx->default_passwd_callback,
		    ctx->default_passwd_callback_userdata);
	} else {
		SSLerrorx(SSL_R_BAD_SSL_FILETYPE);
		goto end;
	}

	if (x == NULL) {
		SSLerrorx(j);
		goto end;
	}

	ret = SSL_CTX_use_certificate(ctx, x);
 end:
	X509_free(x);
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_certificate_file);

int
SSL_CTX_use_certificate_ASN1(SSL_CTX *ctx, int len, const unsigned char *d)
{
	X509 *x;
	int ret;

	x = d2i_X509(NULL, &d, (long)len);
	if (x == NULL) {
		SSLerrorx(ERR_R_ASN1_LIB);
		return (0);
	}

	ret = SSL_CTX_use_certificate(ctx, x);
	X509_free(x);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_certificate_ASN1);

int
SSL_CTX_use_RSAPrivateKey(SSL_CTX *ctx, RSA *rsa)
{
	EVP_PKEY *pkey = NULL;
	int ret = 0;

	if (rsa == NULL) {
		SSLerrorx(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	if ((pkey = EVP_PKEY_new()) == NULL) {
		SSLerrorx(ERR_R_EVP_LIB);
		goto err;
	}
	if (!EVP_PKEY_set1_RSA(pkey, rsa))
		goto err;
	if (!ssl_set_pkey(ctx, NULL, pkey))
		goto err;

	ret = 1;

 err:
	EVP_PKEY_free(pkey);

	return ret;
}
LSSL_ALIAS(SSL_CTX_use_RSAPrivateKey);

int
SSL_CTX_use_RSAPrivateKey_file(SSL_CTX *ctx, const char *file, int type)
{
	int j, ret = 0;
	BIO *in;
	RSA *rsa = NULL;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerrorx(ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerrorx(ERR_R_SYS_LIB);
		goto end;
	}
	if (type == SSL_FILETYPE_ASN1) {
		j = ERR_R_ASN1_LIB;
		rsa = d2i_RSAPrivateKey_bio(in, NULL);
	} else if (type == SSL_FILETYPE_PEM) {
		j = ERR_R_PEM_LIB;
		rsa = PEM_read_bio_RSAPrivateKey(in, NULL,
		    ctx->default_passwd_callback,
		    ctx->default_passwd_callback_userdata);
	} else {
		SSLerrorx(SSL_R_BAD_SSL_FILETYPE);
		goto end;
	}
	if (rsa == NULL) {
		SSLerrorx(j);
		goto end;
	}
	ret = SSL_CTX_use_RSAPrivateKey(ctx, rsa);
	RSA_free(rsa);
 end:
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_RSAPrivateKey_file);

int
SSL_CTX_use_RSAPrivateKey_ASN1(SSL_CTX *ctx, const unsigned char *d, long len)
{
	int ret;
	RSA *rsa;

	if ((rsa = d2i_RSAPrivateKey(NULL, &d, (long)len)) == NULL) {
		SSLerrorx(ERR_R_ASN1_LIB);
		return (0);
	}

	ret = SSL_CTX_use_RSAPrivateKey(ctx, rsa);
	RSA_free(rsa);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_RSAPrivateKey_ASN1);

int
SSL_CTX_use_PrivateKey(SSL_CTX *ctx, EVP_PKEY *pkey)
{
	if (pkey == NULL) {
		SSLerrorx(ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}
	return ssl_set_pkey(ctx, NULL, pkey);
}
LSSL_ALIAS(SSL_CTX_use_PrivateKey);

int
SSL_CTX_use_PrivateKey_file(SSL_CTX *ctx, const char *file, int type)
{
	int j, ret = 0;
	BIO *in;
	EVP_PKEY *pkey = NULL;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerrorx(ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerrorx(ERR_R_SYS_LIB);
		goto end;
	}
	if (type == SSL_FILETYPE_PEM) {
		j = ERR_R_PEM_LIB;
		pkey = PEM_read_bio_PrivateKey(in, NULL,
		    ctx->default_passwd_callback,
		    ctx->default_passwd_callback_userdata);
	} else if (type == SSL_FILETYPE_ASN1) {
		j = ERR_R_ASN1_LIB;
		pkey = d2i_PrivateKey_bio(in, NULL);
	} else {
		SSLerrorx(SSL_R_BAD_SSL_FILETYPE);
		goto end;
	}
	if (pkey == NULL) {
		SSLerrorx(j);
		goto end;
	}
	ret = SSL_CTX_use_PrivateKey(ctx, pkey);
	EVP_PKEY_free(pkey);
 end:
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_PrivateKey_file);

int
SSL_CTX_use_PrivateKey_ASN1(int type, SSL_CTX *ctx, const unsigned char *d,
    long len)
{
	int ret;
	EVP_PKEY *pkey;

	if ((pkey = d2i_PrivateKey(type, NULL, &d, (long)len)) == NULL) {
		SSLerrorx(ERR_R_ASN1_LIB);
		return (0);
	}

	ret = SSL_CTX_use_PrivateKey(ctx, pkey);
	EVP_PKEY_free(pkey);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_PrivateKey_ASN1);


/*
 * Read a bio that contains our certificate in "PEM" format,
 * possibly followed by a sequence of CA certificates that should be
 * sent to the peer in the Certificate message.
 */
static int
ssl_use_certificate_chain_bio(SSL_CTX *ctx, SSL *ssl, BIO *in)
{
	pem_password_cb *passwd_cb;
	void *passwd_arg;
	X509 *ca, *x = NULL;
	unsigned long err;
	int ret = 0;

	if (!ssl_get_password_cb_and_arg(ctx, ssl, &passwd_cb, &passwd_arg))
		goto err;

	if ((x = PEM_read_bio_X509_AUX(in, NULL, passwd_cb, passwd_arg)) ==
	    NULL) {
		SSLerrorx(ERR_R_PEM_LIB);
		goto err;
	}

	if (!ssl_set_cert(ctx, ssl, x))
		goto err;

	if (!ssl_cert_set0_chain(ctx, ssl, NULL))
		goto err;

	/* Process any additional CA certificates. */
	while ((ca = PEM_read_bio_X509(in, NULL, passwd_cb, passwd_arg)) !=
	    NULL) {
		if (!ssl_cert_add0_chain_cert(ctx, ssl, ca)) {
			X509_free(ca);
			goto err;
		}
	}

	/* When the while loop ends, it's usually just EOF. */
	err = ERR_peek_last_error();
	if (ERR_GET_LIB(err) == ERR_LIB_PEM &&
	    ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
		ERR_clear_error();
		ret = 1;
	}

 err:
	X509_free(x);

	return (ret);
}

int
ssl_use_certificate_chain_file(SSL_CTX *ctx, SSL *ssl, const char *file)
{
	BIO *in;
	int ret = 0;

	in = BIO_new(BIO_s_file());
	if (in == NULL) {
		SSLerrorx(ERR_R_BUF_LIB);
		goto end;
	}

	if (BIO_read_filename(in, file) <= 0) {
		SSLerrorx(ERR_R_SYS_LIB);
		goto end;
	}

	ret = ssl_use_certificate_chain_bio(ctx, ssl, in);

 end:
	BIO_free(in);
	return (ret);
}

int
SSL_CTX_use_certificate_chain_file(SSL_CTX *ctx, const char *file)
{
	return ssl_use_certificate_chain_file(ctx, NULL, file);
}
LSSL_ALIAS(SSL_CTX_use_certificate_chain_file);

int
SSL_use_certificate_chain_file(SSL *ssl, const char *file)
{
	return ssl_use_certificate_chain_file(NULL, ssl, file);
}
LSSL_ALIAS(SSL_use_certificate_chain_file);

int
SSL_CTX_use_certificate_chain_mem(SSL_CTX *ctx, void *buf, int len)
{
	BIO *in;
	int ret = 0;

	in = BIO_new_mem_buf(buf, len);
	if (in == NULL) {
		SSLerrorx(ERR_R_BUF_LIB);
		goto end;
	}

	ret = ssl_use_certificate_chain_bio(ctx, NULL, in);

 end:
	BIO_free(in);
	return (ret);
}
LSSL_ALIAS(SSL_CTX_use_certificate_chain_mem);
