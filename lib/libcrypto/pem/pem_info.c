/* $OpenBSD: pem_info.c,v 1.33 2025/07/16 15:59:26 tb Exp $ */
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
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/dsa.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "evp_local.h"

X509_PKEY *
X509_PKEY_new(void)
{
	X509_PKEY *x_pkey;

	if ((x_pkey = calloc(1, sizeof(*x_pkey))) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	return x_pkey;
}

void
X509_PKEY_free(X509_PKEY *x_pkey)
{
	if (x_pkey == NULL)
		return;

	EVP_PKEY_free(x_pkey->dec_pkey);
	free(x_pkey);
}

X509_INFO *
X509_INFO_new(void)
{
	X509_INFO *ret;

	if ((ret = calloc(1, sizeof(X509_INFO))) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	ret->references = 1;

	return ret;
}
LCRYPTO_ALIAS(X509_INFO_new);

void
X509_INFO_free(X509_INFO *x)
{
	if (x == NULL)
		return;

	if (CRYPTO_add(&x->references, -1, CRYPTO_LOCK_X509_INFO) > 0)
		return;

	X509_free(x->x509);
	X509_CRL_free(x->crl);
	X509_PKEY_free(x->x_pkey);
	free(x->enc_data);

	free(x);
}
LCRYPTO_ALIAS(X509_INFO_free);

STACK_OF(X509_INFO) *
PEM_X509_INFO_read_bio(BIO *bp, STACK_OF(X509_INFO) *sk, pem_password_cb *cb,
    void *u)
{
	X509_INFO *xi = NULL;
	char *name = NULL, *header = NULL;
	void *pp;
	unsigned char *data = NULL;
	const unsigned char *p;
	long len;
	int ok = 0;
	int num_in, ptype, raw;
	STACK_OF(X509_INFO) *ret = sk;
	d2i_of_void *d2i = NULL;

	if (ret == NULL) {
		if ((ret = sk_X509_INFO_new_null()) == NULL) {
			PEMerror(ERR_R_MALLOC_FAILURE);
			return NULL;
		}
	}
	num_in = sk_X509_INFO_num(ret);

	if ((xi = X509_INFO_new()) == NULL)
		goto err;
	for (;;) {
		raw = 0;
		ptype = 0;
		if (!PEM_read_bio(bp, &name, &header, &data, &len)) {
			if (ERR_GET_REASON(ERR_peek_last_error()) ==
			    PEM_R_NO_START_LINE) {
				ERR_clear_error();
				break;
			}
			goto err;
		}
		if ((strcmp(name, PEM_STRING_X509) == 0) ||
		    (strcmp(name, PEM_STRING_X509_OLD) == 0)) {
			d2i = (D2I_OF(void))d2i_X509;
			if (xi->x509 != NULL) {
				if (!sk_X509_INFO_push(ret, xi))
					goto err;
				if ((xi = X509_INFO_new()) == NULL)
					goto err;
			}
			pp = &(xi->x509);
		} else if ((strcmp(name, PEM_STRING_X509_TRUSTED) == 0)) {
			d2i = (D2I_OF(void))d2i_X509_AUX;
			if (xi->x509 != NULL) {
				if (!sk_X509_INFO_push(ret, xi))
					goto err;
				if ((xi = X509_INFO_new()) == NULL)
					goto err;
			}
			pp = &(xi->x509);
		} else if (strcmp(name, PEM_STRING_X509_CRL) == 0) {
			d2i = (D2I_OF(void))d2i_X509_CRL;
			if (xi->crl != NULL) {
				if (!sk_X509_INFO_push(ret, xi))
					goto err;
				if ((xi = X509_INFO_new()) == NULL)
					goto err;
			}
			pp = &(xi->crl);
		} else
#ifndef OPENSSL_NO_RSA
		if (strcmp(name, PEM_STRING_RSA) == 0) {
			d2i = (D2I_OF(void))d2i_RSAPrivateKey;
			if (xi->x_pkey != NULL) {
				if (!sk_X509_INFO_push(ret, xi))
					goto err;
				if ((xi = X509_INFO_new()) == NULL)
					goto err;
			}
			xi->enc_data = NULL;
			xi->enc_len = 0;
			xi->x_pkey = X509_PKEY_new();
			if (xi->x_pkey == NULL)
				goto err;
			ptype = EVP_PKEY_RSA;
			pp = &xi->x_pkey->dec_pkey;
			if (strlen(header) > 10) /* assume encrypted */
				raw = 1;
		} else
#endif
#ifndef OPENSSL_NO_DSA
		if (strcmp(name, PEM_STRING_DSA) == 0) {
			d2i = (D2I_OF(void))d2i_DSAPrivateKey;
			if (xi->x_pkey != NULL) {
				if (!sk_X509_INFO_push(ret, xi))
					goto err;
				if ((xi = X509_INFO_new()) == NULL)
					goto err;
			}
			xi->enc_data = NULL;
			xi->enc_len = 0;
			xi->x_pkey = X509_PKEY_new();
			if (xi->x_pkey == NULL)
				goto err;
			ptype = EVP_PKEY_DSA;
			pp = &xi->x_pkey->dec_pkey;
			if (strlen(header) > 10) /* assume encrypted */
				raw = 1;
		} else
#endif
#ifndef OPENSSL_NO_EC
		if (strcmp(name, PEM_STRING_ECPRIVATEKEY) == 0) {
			d2i = (D2I_OF(void))d2i_ECPrivateKey;
			if (xi->x_pkey != NULL) {
				if (!sk_X509_INFO_push(ret, xi))
					goto err;
				if ((xi = X509_INFO_new()) == NULL)
					goto err;
			}
			xi->enc_data = NULL;
			xi->enc_len = 0;
			xi->x_pkey = X509_PKEY_new();
			if (xi->x_pkey == NULL)
				goto err;
			ptype = EVP_PKEY_EC;
			pp = &xi->x_pkey->dec_pkey;
			if (strlen(header) > 10) /* assume encrypted */
				raw = 1;
		} else
#endif
		{
			d2i = NULL;
			pp = NULL;
		}

		if (d2i != NULL) {
			if (!raw) {
				EVP_CIPHER_INFO cipher;

				if (!PEM_get_EVP_CIPHER_INFO(header, &cipher))
					goto err;
				if (!PEM_do_header(&cipher, data, &len, cb, u))
					goto err;
				p = data;
				if (ptype) {
					if (!d2i_PrivateKey(ptype, pp, &p,
					    len)) {
						PEMerror(ERR_R_ASN1_LIB);
						goto err;
					}
				} else if (d2i(pp, &p, len) == NULL) {
					PEMerror(ERR_R_ASN1_LIB);
					goto err;
				}
			} else { /* encrypted RSA data */
				if (!PEM_get_EVP_CIPHER_INFO(header,
				    &xi->enc_cipher))
					goto err;
				xi->enc_data = (char *)data;
				xi->enc_len = (int)len;
				data = NULL;
			}
		} else {
			/* unknown */
		}
		free(name);
		free(header);
		free(data);
		name = NULL;
		header = NULL;
		data = NULL;
	}

	/* if the last one hasn't been pushed yet and there is anything
	 * in it then add it to the stack ...
	 */
	if ((xi->x509 != NULL) || (xi->crl != NULL) ||
	    (xi->x_pkey != NULL) || (xi->enc_data != NULL)) {
		if (!sk_X509_INFO_push(ret, xi))
			goto err;
		xi = NULL;
	}
	ok = 1;

err:
	if (!ok) {
		while (sk_X509_INFO_num(ret) > num_in)
			X509_INFO_free(sk_X509_INFO_pop(ret));
		if (ret != sk)
			sk_X509_INFO_free(ret);
		ret = NULL;
	}
	X509_INFO_free(xi);
	free(name);
	free(header);
	free(data);

	return ret;
}
LCRYPTO_ALIAS(PEM_X509_INFO_read_bio);
