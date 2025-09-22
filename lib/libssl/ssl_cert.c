/* $OpenBSD: ssl_cert.c,v 1.108 2024/02/03 15:58:33 beck Exp $ */
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
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#include <sys/types.h>

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/objects.h>
#include <openssl/opensslconf.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include "ssl_local.h"

int
SSL_get_ex_data_X509_STORE_CTX_idx(void)
{
	static volatile int ssl_x509_store_ctx_idx = -1;
	int got_write_lock = 0;

	CRYPTO_r_lock(CRYPTO_LOCK_SSL_CTX);

	if (ssl_x509_store_ctx_idx < 0) {
		CRYPTO_r_unlock(CRYPTO_LOCK_SSL_CTX);
		CRYPTO_w_lock(CRYPTO_LOCK_SSL_CTX);
		got_write_lock = 1;

		if (ssl_x509_store_ctx_idx < 0) {
			ssl_x509_store_ctx_idx =
			    X509_STORE_CTX_get_ex_new_index(
				0, "SSL for verify callback", NULL, NULL, NULL);
		}
	}

	if (got_write_lock)
		CRYPTO_w_unlock(CRYPTO_LOCK_SSL_CTX);
	else
		CRYPTO_r_unlock(CRYPTO_LOCK_SSL_CTX);

	return ssl_x509_store_ctx_idx;
}
LSSL_ALIAS(SSL_get_ex_data_X509_STORE_CTX_idx);

SSL_CERT *
ssl_cert_new(void)
{
	SSL_CERT *ret;

	ret = calloc(1, sizeof(SSL_CERT));
	if (ret == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}
	ret->key = &(ret->pkeys[SSL_PKEY_RSA]);
	ret->references = 1;
	ret->security_cb = ssl_security_default_cb;
	ret->security_level = OPENSSL_TLS_SECURITY_LEVEL;
	ret->security_ex_data = NULL;
	return (ret);
}

SSL_CERT *
ssl_cert_dup(SSL_CERT *cert)
{
	SSL_CERT *ret;
	int i;

	ret = calloc(1, sizeof(SSL_CERT));
	if (ret == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}

	/*
	 * same as ret->key = ret->pkeys + (cert->key - cert->pkeys),
	 * if you find that more readable
	 */
	ret->key = &ret->pkeys[cert->key - &cert->pkeys[0]];

	ret->valid = cert->valid;
	ret->mask_k = cert->mask_k;
	ret->mask_a = cert->mask_a;

	if (cert->dhe_params != NULL) {
		ret->dhe_params = DHparams_dup(cert->dhe_params);
		if (ret->dhe_params == NULL) {
			SSLerrorx(ERR_R_DH_LIB);
			goto err;
		}
	}
	ret->dhe_params_cb = cert->dhe_params_cb;
	ret->dhe_params_auto = cert->dhe_params_auto;

	for (i = 0; i < SSL_PKEY_NUM; i++) {
		if (cert->pkeys[i].x509 != NULL) {
			ret->pkeys[i].x509 = cert->pkeys[i].x509;
			X509_up_ref(ret->pkeys[i].x509);
		}

		if (cert->pkeys[i].privatekey != NULL) {
			ret->pkeys[i].privatekey = cert->pkeys[i].privatekey;
			EVP_PKEY_up_ref(ret->pkeys[i].privatekey);
			switch (i) {
				/*
				 * If there was anything special to do for
				 * certain types of keys, we'd do it here.
				 * (Nothing at the moment, I think.)
				 */

			case SSL_PKEY_RSA:
				/* We have an RSA key. */
				break;

			case SSL_PKEY_ECC:
				/* We have an ECC key */
				break;

			default:
				/* Can't happen. */
				SSLerrorx(SSL_R_LIBRARY_BUG);
			}
		}

		if (cert->pkeys[i].chain != NULL) {
			if ((ret->pkeys[i].chain =
			    X509_chain_up_ref(cert->pkeys[i].chain)) == NULL)
				goto err;
		}
	}

	ret->security_cb = cert->security_cb;
	ret->security_level = cert->security_level;
	ret->security_ex_data = cert->security_ex_data;

	/*
	 * ret->extra_certs *should* exist, but currently the own certificate
	 * chain is held inside SSL_CTX
	 */

	ret->references = 1;

	return (ret);

 err:
	DH_free(ret->dhe_params);

	for (i = 0; i < SSL_PKEY_NUM; i++) {
		X509_free(ret->pkeys[i].x509);
		EVP_PKEY_free(ret->pkeys[i].privatekey);
		sk_X509_pop_free(ret->pkeys[i].chain, X509_free);
	}
	free (ret);
	return NULL;
}


void
ssl_cert_free(SSL_CERT *c)
{
	int i;

	if (c == NULL)
		return;

	i = CRYPTO_add(&c->references, -1, CRYPTO_LOCK_SSL_CERT);
	if (i > 0)
		return;

	DH_free(c->dhe_params);

	for (i = 0; i < SSL_PKEY_NUM; i++) {
		X509_free(c->pkeys[i].x509);
		EVP_PKEY_free(c->pkeys[i].privatekey);
		sk_X509_pop_free(c->pkeys[i].chain, X509_free);
	}

	free(c);
}

SSL_CERT *
ssl_get0_cert(SSL_CTX *ctx, SSL *ssl)
{
	if (ssl != NULL)
		return ssl->cert;

	return ctx->cert;
}

int
ssl_cert_set0_chain(SSL_CTX *ctx, SSL *ssl, STACK_OF(X509) *chain)
{
	SSL_CERT *ssl_cert;
	SSL_CERT_PKEY *cpk;
	X509 *x509;
	int ssl_err;
	int i;

	if ((ssl_cert = ssl_get0_cert(ctx, ssl)) == NULL)
		return 0;

	if ((cpk = ssl_cert->key) == NULL)
		return 0;

	for (i = 0; i < sk_X509_num(chain); i++) {
		x509 = sk_X509_value(chain, i);
		if (!ssl_security_cert(ctx, ssl, x509, 0, &ssl_err)) {
			SSLerrorx(ssl_err);
			return 0;
		}
	}

	sk_X509_pop_free(cpk->chain, X509_free);
	cpk->chain = chain;

	return 1;
}

int
ssl_cert_set1_chain(SSL_CTX *ctx, SSL *ssl, STACK_OF(X509) *chain)
{
	STACK_OF(X509) *new_chain = NULL;

	if (chain != NULL) {
		if ((new_chain = X509_chain_up_ref(chain)) == NULL)
			return 0;
	}
	if (!ssl_cert_set0_chain(ctx, ssl, new_chain)) {
		sk_X509_pop_free(new_chain, X509_free);
		return 0;
	}

	return 1;
}

int
ssl_cert_add0_chain_cert(SSL_CTX *ctx, SSL *ssl, X509 *cert)
{
	SSL_CERT *ssl_cert;
	SSL_CERT_PKEY *cpk;
	int ssl_err;

	if ((ssl_cert = ssl_get0_cert(ctx, ssl)) == NULL)
		return 0;

	if ((cpk = ssl_cert->key) == NULL)
		return 0;

	if (!ssl_security_cert(ctx, ssl, cert, 0, &ssl_err)) {
		SSLerrorx(ssl_err);
		return 0;
	}

	if (cpk->chain == NULL) {
		if ((cpk->chain = sk_X509_new_null()) == NULL)
			return 0;
	}
	if (!sk_X509_push(cpk->chain, cert))
		return 0;

	return 1;
}

int
ssl_cert_add1_chain_cert(SSL_CTX *ctx, SSL *ssl, X509 *cert)
{
	if (!ssl_cert_add0_chain_cert(ctx, ssl, cert))
		return 0;

	X509_up_ref(cert);

	return 1;
}

int
ssl_verify_cert_chain(SSL *s, STACK_OF(X509) *certs)
{
	X509_STORE_CTX *ctx = NULL;
	X509_VERIFY_PARAM *param;
	X509 *cert;
	int ret = 0;

	if (sk_X509_num(certs) < 1)
		goto err;

	if ((ctx = X509_STORE_CTX_new()) == NULL)
		goto err;

	cert = sk_X509_value(certs, 0);
	if (!X509_STORE_CTX_init(ctx, s->ctx->cert_store, cert, certs)) {
		SSLerror(s, ERR_R_X509_LIB);
		goto err;
	}
	X509_STORE_CTX_set_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx(), s);

	/*
	 * We need to inherit the verify parameters. These can be
	 * determined by the context: if its a server it will verify
	 * SSL client certificates or vice versa.
	 */
	X509_STORE_CTX_set_default(ctx, s->server ? "ssl_client" : "ssl_server");

	param = X509_STORE_CTX_get0_param(ctx);

	X509_VERIFY_PARAM_set_auth_level(param, SSL_get_security_level(s));

	/*
	 * Anything non-default in "param" should overwrite anything
	 * in the ctx.
	 */
	X509_VERIFY_PARAM_set1(param, s->param);

	if (s->verify_callback)
		X509_STORE_CTX_set_verify_cb(ctx, s->verify_callback);

	if (s->ctx->app_verify_callback != NULL)
		ret = s->ctx->app_verify_callback(ctx,
		    s->ctx->app_verify_arg);
	else
		ret = X509_verify_cert(ctx);

	s->verify_result = X509_STORE_CTX_get_error(ctx);
	sk_X509_pop_free(s->s3->hs.verified_chain, X509_free);
	s->s3->hs.verified_chain = NULL;
	if (X509_STORE_CTX_get0_chain(ctx) != NULL) {
		s->s3->hs.verified_chain = X509_STORE_CTX_get1_chain(ctx);
		if (s->s3->hs.verified_chain == NULL) {
			SSLerrorx(ERR_R_MALLOC_FAILURE);
			ret = 0;
		}
	}

 err:
	X509_STORE_CTX_free(ctx);

	return (ret);
}

static void
set_client_CA_list(STACK_OF(X509_NAME) **ca_list,
    STACK_OF(X509_NAME) *name_list)
{
	sk_X509_NAME_pop_free(*ca_list, X509_NAME_free);
	*ca_list = name_list;
}

STACK_OF(X509_NAME) *
SSL_dup_CA_list(const STACK_OF(X509_NAME) *sk)
{
	int i;
	STACK_OF(X509_NAME) *ret;
	X509_NAME *name = NULL;

	if ((ret = sk_X509_NAME_new_null()) == NULL)
		goto err;

	for (i = 0; i < sk_X509_NAME_num(sk); i++) {
		if ((name = X509_NAME_dup(sk_X509_NAME_value(sk, i))) == NULL)
			goto err;
		if (!sk_X509_NAME_push(ret, name))
			goto err;
	}
	return (ret);

 err:
	X509_NAME_free(name);
	sk_X509_NAME_pop_free(ret, X509_NAME_free);
	return NULL;
}
LSSL_ALIAS(SSL_dup_CA_list);

void
SSL_set_client_CA_list(SSL *s, STACK_OF(X509_NAME) *name_list)
{
	set_client_CA_list(&(s->client_CA), name_list);
}
LSSL_ALIAS(SSL_set_client_CA_list);

void
SSL_CTX_set_client_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list)
{
	set_client_CA_list(&(ctx->client_CA), name_list);
}
LSSL_ALIAS(SSL_CTX_set_client_CA_list);

STACK_OF(X509_NAME) *
SSL_CTX_get_client_CA_list(const SSL_CTX *ctx)
{
	return (ctx->client_CA);
}
LSSL_ALIAS(SSL_CTX_get_client_CA_list);

STACK_OF(X509_NAME) *
SSL_get_client_CA_list(const SSL *s)
{
	if (!s->server) {
		/* We are in the client. */
		if ((s->version >> 8) == SSL3_VERSION_MAJOR)
			return (s->s3->hs.tls12.ca_names);
		else
			return (NULL);
	} else {
		if (s->client_CA != NULL)
			return (s->client_CA);
		else
			return (s->ctx->client_CA);
	}
}
LSSL_ALIAS(SSL_get_client_CA_list);

static int
add_client_CA(STACK_OF(X509_NAME) **sk, X509 *x)
{
	X509_NAME *name;

	if (x == NULL)
		return (0);
	if ((*sk == NULL) && ((*sk = sk_X509_NAME_new_null()) == NULL))
		return (0);

	if ((name = X509_NAME_dup(X509_get_subject_name(x))) == NULL)
		return (0);

	if (!sk_X509_NAME_push(*sk, name)) {
		X509_NAME_free(name);
		return (0);
	}
	return (1);
}

int
SSL_add_client_CA(SSL *ssl, X509 *x)
{
	return (add_client_CA(&(ssl->client_CA), x));
}
LSSL_ALIAS(SSL_add_client_CA);

int
SSL_CTX_add_client_CA(SSL_CTX *ctx, X509 *x)
{
	return (add_client_CA(&(ctx->client_CA), x));
}
LSSL_ALIAS(SSL_CTX_add_client_CA);

static int
xname_cmp(const X509_NAME * const *a, const X509_NAME * const *b)
{
	return (X509_NAME_cmp(*a, *b));
}

/*!
 * Load CA certs from a file into a ::STACK. Note that it is somewhat misnamed;
 * it doesn't really have anything to do with clients (except that a common use
 * for a stack of CAs is to send it to the client). Actually, it doesn't have
 * much to do with CAs, either, since it will load any old cert.
 * \param file the file containing one or more certs.
 * \return a ::STACK containing the certs.
 */
STACK_OF(X509_NAME) *
SSL_load_client_CA_file(const char *file)
{
	BIO *in;
	X509 *x = NULL;
	X509_NAME *xn = NULL;
	STACK_OF(X509_NAME) *ret = NULL, *sk;

	sk = sk_X509_NAME_new(xname_cmp);

	in = BIO_new(BIO_s_file());

	if ((sk == NULL) || (in == NULL)) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!BIO_read_filename(in, file))
		goto err;

	for (;;) {
		if (PEM_read_bio_X509(in, &x, NULL, NULL) == NULL)
			break;
		if (ret == NULL) {
			ret = sk_X509_NAME_new_null();
			if (ret == NULL) {
				SSLerrorx(ERR_R_MALLOC_FAILURE);
				goto err;
			}
		}
		if ((xn = X509_get_subject_name(x)) == NULL)
			goto err;
		/* check for duplicates */
		xn = X509_NAME_dup(xn);
		if (xn == NULL)
			goto err;
		if (sk_X509_NAME_find(sk, xn) >= 0)
			X509_NAME_free(xn);
		else {
			if (!sk_X509_NAME_push(sk, xn))
				goto err;
			if (!sk_X509_NAME_push(ret, xn))
				goto err;
		}
	}

	if (0) {
 err:
		sk_X509_NAME_pop_free(ret, X509_NAME_free);
		ret = NULL;
	}
	sk_X509_NAME_free(sk);
	BIO_free(in);
	X509_free(x);
	if (ret != NULL)
		ERR_clear_error();

	return (ret);
}
LSSL_ALIAS(SSL_load_client_CA_file);

/*!
 * Add a file of certs to a stack.
 * \param stack the stack to add to.
 * \param file the file to add from. All certs in this file that are not
 * already in the stack will be added.
 * \return 1 for success, 0 for failure. Note that in the case of failure some
 * certs may have been added to \c stack.
 */

int
SSL_add_file_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack,
    const char *file)
{
	BIO *in;
	X509 *x = NULL;
	X509_NAME *xn = NULL;
	int ret = 1;
	int (*oldcmp)(const X509_NAME * const *a, const X509_NAME * const *b);

	oldcmp = sk_X509_NAME_set_cmp_func(stack, xname_cmp);

	in = BIO_new(BIO_s_file());

	if (in == NULL) {
		SSLerrorx(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!BIO_read_filename(in, file))
		goto err;

	for (;;) {
		if (PEM_read_bio_X509(in, &x, NULL, NULL) == NULL)
			break;
		if ((xn = X509_get_subject_name(x)) == NULL)
			goto err;
		xn = X509_NAME_dup(xn);
		if (xn == NULL)
			goto err;
		if (sk_X509_NAME_find(stack, xn) >= 0)
			X509_NAME_free(xn);
		else
			if (!sk_X509_NAME_push(stack, xn))
				goto err;
	}

	ERR_clear_error();

	if (0) {
 err:
		ret = 0;
	}
	BIO_free(in);
	X509_free(x);

	(void)sk_X509_NAME_set_cmp_func(stack, oldcmp);

	return ret;
}
LSSL_ALIAS(SSL_add_file_cert_subjects_to_stack);

/*!
 * Add a directory of certs to a stack.
 * \param stack the stack to append to.
 * \param dir the directory to append from. All files in this directory will be
 * examined as potential certs. Any that are acceptable to
 * SSL_add_dir_cert_subjects_to_stack() that are not already in the stack will
 * be included.
 * \return 1 for success, 0 for failure. Note that in the case of failure some
 * certs may have been added to \c stack.
 */

int
SSL_add_dir_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack, const char *dir)
{
	DIR *dirp = NULL;
	char *path = NULL;
	int ret = 0;

	dirp = opendir(dir);
	if (dirp) {
		struct dirent *dp;
		while ((dp = readdir(dirp)) != NULL) {
			if (asprintf(&path, "%s/%s", dir, dp->d_name) != -1) {
				ret = SSL_add_file_cert_subjects_to_stack(
				    stack, path);
				free(path);
			}
			if (!ret)
				break;
		}
		(void) closedir(dirp);
	}
	if (!ret) {
 		SYSerror(errno);
		ERR_asprintf_error_data("opendir ('%s')", dir);
		SSLerrorx(ERR_R_SYS_LIB);
	}
	return ret;
}
LSSL_ALIAS(SSL_add_dir_cert_subjects_to_stack);
