/* $OpenBSD: bio_ndef.c,v 1.25 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2008 The OpenSSL Project.  All rights reserved.
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
 */

#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bio.h>

#include "asn1_local.h"
#include "err_local.h"

int BIO_asn1_set_prefix(BIO *b, asn1_ps_func *prefix, asn1_ps_func *prefix_free);
int BIO_asn1_set_suffix(BIO *b, asn1_ps_func *suffix, asn1_ps_func *suffix_free);

/* Experimental NDEF ASN1 BIO support routines */

/* The usage is quite simple, initialize an ASN1 structure,
 * get a BIO from it then any data written through the BIO
 * will end up translated to approptiate format on the fly.
 * The data is streamed out and does *not* need to be
 * all held in memory at once.
 *
 * When the BIO is flushed the output is finalized and any
 * signatures etc written out.
 *
 * The BIO is a 'proper' BIO and can handle non blocking I/O
 * correctly.
 *
 * The usage is simple. The implementation is *not*...
 */

/* BIO support data stored in the ASN1 BIO ex_arg */

typedef struct ndef_aux_st {
	/* ASN1 structure this BIO refers to */
	ASN1_VALUE *val;
	const ASN1_ITEM *it;
	/* Top of the BIO chain */
	BIO *ndef_bio;
	/* Output BIO */
	BIO *out;
	/* Boundary where content is inserted */
	unsigned char **boundary;
	/* DER buffer start */
	unsigned char *derbuf;
} NDEF_SUPPORT;

static int ndef_prefix(BIO *b, unsigned char **pbuf, int *plen, void *parg);
static int ndef_prefix_free(BIO *b, unsigned char **pbuf, int *plen, void *parg);
static int ndef_suffix(BIO *b, unsigned char **pbuf, int *plen, void *parg);
static int ndef_suffix_free(BIO *b, unsigned char **pbuf, int *plen, void *parg);

BIO *
BIO_new_NDEF(BIO *out, ASN1_VALUE *val, const ASN1_ITEM *it)
{
	NDEF_SUPPORT *ndef_aux = NULL;
	BIO *asn_bio = NULL, *pop_bio = NULL;
	const ASN1_AUX *aux = it->funcs;
	ASN1_STREAM_ARG sarg;

	if (aux == NULL || aux->asn1_cb == NULL) {
		ASN1error(ASN1_R_STREAMING_NOT_SUPPORTED);
		goto err;
	}

	if ((asn_bio = BIO_new(BIO_f_asn1())) == NULL)
		goto err;

	if (BIO_push(asn_bio, out) == NULL)
		goto err;
	pop_bio = asn_bio;

	/*
	 * Set up prefix and suffix handlers first. This ensures that ndef_aux
	 * is freed as part of asn_bio once it is the asn_bio's ex_arg.
	 */
	if (BIO_asn1_set_prefix(asn_bio, ndef_prefix, ndef_prefix_free) <= 0)
		goto err;
	if (BIO_asn1_set_suffix(asn_bio, ndef_suffix, ndef_suffix_free) <= 0)
		goto err;

	/*
	 * Allocate early to avoid the tricky cleanup after the asn1_cb().
	 * Ownership of ndef_aux is transferred to asn_bio in BIO_ctrl().
	 * Keep a reference to populate it after callback success.
	 */
	if ((ndef_aux = calloc(1, sizeof(*ndef_aux))) == NULL)
		goto err;
	if (BIO_ctrl(asn_bio, BIO_C_SET_EX_ARG, 0, ndef_aux) <= 0) {
		free(ndef_aux);
		goto err;
	}

	/*
	 * The callback prepends BIOs to the chain starting at asn_bio for
	 * digest, cipher, etc. The resulting chain starts at sarg.ndef_bio.
	 */

	sarg.out = asn_bio;
	sarg.ndef_bio = NULL;
	sarg.boundary = NULL;

	if (aux->asn1_cb(ASN1_OP_STREAM_PRE, &val, it, &sarg) <= 0)
		goto err;

	ndef_aux->val = val;
	ndef_aux->it = it;
	ndef_aux->ndef_bio = sarg.ndef_bio;
	ndef_aux->boundary = sarg.boundary;
	ndef_aux->out = asn_bio;

	return sarg.ndef_bio;

 err:
	BIO_pop(pop_bio);
	BIO_free(asn_bio);

	return NULL;
}

static int
ndef_prefix(BIO *b, unsigned char **pbuf, int *plen, void *parg)
{
	NDEF_SUPPORT *ndef_aux;
	unsigned char *p = NULL;
	int derlen;

	if (!parg)
		return 0;

	ndef_aux = *(NDEF_SUPPORT **)parg;

	if ((derlen = ASN1_item_ndef_i2d(ndef_aux->val, &p, ndef_aux->it)) <= 0)
		return 0;

	ndef_aux->derbuf = p;
	*pbuf = p;

	if (*ndef_aux->boundary == NULL)
		return 0;

	*plen = *ndef_aux->boundary - *pbuf;

	return 1;
}

static int
ndef_prefix_free(BIO *b, unsigned char **pbuf, int *plen, void *parg)
{
	NDEF_SUPPORT **pndef_aux = parg;

	if (pndef_aux == NULL || *pndef_aux == NULL)
		return 0;

	free((*pndef_aux)->derbuf);
	(*pndef_aux)->derbuf = NULL;

	*pbuf = NULL;
	*plen = 0;

	return 1;
}

static int
ndef_suffix_free(BIO *b, unsigned char **pbuf, int *plen, void *parg)
{
	NDEF_SUPPORT **pndef_aux = parg;

	/* Ensure ndef_prefix_free() won't fail, so we won't leak *pndef_aux. */
	if (pndef_aux == NULL || *pndef_aux == NULL)
		return 0;
	if (!ndef_prefix_free(b, pbuf, plen, parg))
		return 0;

	free(*pndef_aux);
	*pndef_aux = NULL;

	return 1;
}

static int
ndef_suffix(BIO *b, unsigned char **pbuf, int *plen, void *parg)
{
	NDEF_SUPPORT *ndef_aux;
	unsigned char *p = NULL;
	int derlen;
	const ASN1_AUX *aux;
	ASN1_STREAM_ARG sarg;

	if (!parg)
		return 0;

	ndef_aux = *(NDEF_SUPPORT **)parg;

	aux = ndef_aux->it->funcs;

	/* Finalize structures */
	sarg.ndef_bio = ndef_aux->ndef_bio;
	sarg.out = ndef_aux->out;
	sarg.boundary = ndef_aux->boundary;
	if (aux->asn1_cb(ASN1_OP_STREAM_POST,
	    &ndef_aux->val, ndef_aux->it, &sarg) <= 0)
		return 0;

	if ((derlen = ASN1_item_ndef_i2d(ndef_aux->val, &p, ndef_aux->it)) <= 0)
		return 0;

	ndef_aux->derbuf = p;
	*pbuf = p;

	if (*ndef_aux->boundary == NULL)
		return 0;

	*pbuf = *ndef_aux->boundary;
	*plen = derlen - (*ndef_aux->boundary - ndef_aux->derbuf);

	return 1;
}
