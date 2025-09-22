/* $OpenBSD: p12_key.c,v 1.37 2025/05/10 05:54:38 tb Exp $ */
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
#include <string.h>

#include <openssl/bn.h>
#include <openssl/pkcs12.h>

#include "err_local.h"
#include "evp_local.h"
#include "pkcs12_local.h"

/* PKCS12 compatible key/IV generation */
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

int
PKCS12_key_gen_asc(const char *pass, int passlen, unsigned char *salt,
    int saltlen, int id, int iter, int n, unsigned char *out,
    const EVP_MD *md_type)
{
	int ret;
	unsigned char *unipass;
	int uniplen;

	if (!pass) {
		unipass = NULL;
		uniplen = 0;
	} else if (!OPENSSL_asc2uni(pass, passlen, &unipass, &uniplen)) {
		PKCS12error(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	ret = PKCS12_key_gen_uni(unipass, uniplen, salt, saltlen,
	    id, iter, n, out, md_type);
	if (ret <= 0)
		return 0;
	freezero(unipass, uniplen);
	return ret;
}

int
PKCS12_key_gen_uni(unsigned char *pass, int passlen, unsigned char *salt,
    int saltlen, int id, int iter, int n, unsigned char *out,
    const EVP_MD *md_type)
{
	EVP_MD_CTX *ctx = NULL;
	unsigned char *B = NULL, *D = NULL, *I = NULL, *Ai = NULL;
	unsigned char *p;
	int Slen, Plen, Ilen;
	int i, j, u, v;
	int ret = 0;

	if ((ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if ((v = EVP_MD_block_size(md_type)) <= 0)
		goto err;
	if ((u = EVP_MD_size(md_type)) <= 0)
		goto err;

	if ((D = malloc(v)) == NULL)
		goto err;
	if ((Ai = malloc(u)) == NULL)
		goto err;
	if ((B = malloc(v + 1)) == NULL)
		goto err;

	Slen = v * ((saltlen + v - 1) / v);

	Plen = 0;
	if (passlen)
		Plen = v * ((passlen + v - 1) / v);

	Ilen = Slen + Plen;

	if ((I = malloc(Ilen)) == NULL)
		goto err;

	for (i = 0; i < v; i++)
		D[i] = id;

	p = I;
	for (i = 0; i < Slen; i++)
		*p++ = salt[i % saltlen];
	for (i = 0; i < Plen; i++)
		*p++ = pass[i % passlen];

	for (;;) {
		if (!EVP_DigestInit_ex(ctx, md_type, NULL))
			goto err;
		if (!EVP_DigestUpdate(ctx, D, v))
			goto err;
		if (!EVP_DigestUpdate(ctx, I, Ilen))
			goto err;
		if (!EVP_DigestFinal_ex(ctx, Ai, NULL))
			goto err;
		for (j = 1; j < iter; j++) {
			if (!EVP_DigestInit_ex(ctx, md_type, NULL))
				goto err;
			if (!EVP_DigestUpdate(ctx, Ai, u))
				goto err;
			if (!EVP_DigestFinal_ex(ctx, Ai, NULL))
				goto err;
		}
		memcpy(out, Ai, min(n, u));
		if (u >= n) {
			ret = 1;
			goto end;
		}
		n -= u;
		out += u;
		for (j = 0; j < v; j++)
			B[j] = Ai[j % u];

		for (j = 0; j < Ilen; j += v) {
			uint16_t c = 1;
			int k;

			/* Work out I[j] = I[j] + B + 1. */
			for (k = v - 1; k >= 0; k--) {
				c += I[j + k] + B[k];
				I[j + k] = (unsigned char)c;
				c >>= 8;
			}
		}
	}

 err:
	PKCS12error(ERR_R_MALLOC_FAILURE);

 end:
	free(Ai);
	free(B);
	free(D);
	free(I);
	EVP_MD_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(PKCS12_key_gen_uni);
