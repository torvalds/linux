/*	$OpenBSD: ct_sct_ctx.c,v 1.7 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Rob Stradling (rob@comodo.com) and Stephen Henson
 * (steve@openssl.org) for the OpenSSL project 2014.
 */
/* ====================================================================
 * Copyright (c) 2014 The OpenSSL Project.  All rights reserved.
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

#ifdef OPENSSL_NO_CT
# error "CT is disabled"
#endif

#include <stddef.h>
#include <string.h>

#include <openssl/objects.h>
#include <openssl/x509.h>

#include "ct_local.h"
#include "err_local.h"

SCT_CTX *
SCT_CTX_new(void)
{
	SCT_CTX *sctx = calloc(1, sizeof(*sctx));

	if (sctx == NULL)
		CTerror(ERR_R_MALLOC_FAILURE);

	return sctx;
}

void
SCT_CTX_free(SCT_CTX *sctx)
{
	if (sctx == NULL)
		return;
	EVP_PKEY_free(sctx->pkey);
	free(sctx->pkeyhash);
	free(sctx->ihash);
	free(sctx->certder);
	free(sctx->preder);
	free(sctx);
}

/*
 * Finds the index of the first extension with the given NID in cert.
 * If there is more than one extension with that NID, *is_duplicated is set to
 * 1, otherwise 0 (unless it is NULL).
 */
static int
ct_x509_get_ext(X509 *cert, int nid, int *is_duplicated)
{
	int ret = X509_get_ext_by_NID(cert, nid, -1);

	if (is_duplicated != NULL)
		*is_duplicated = ret >= 0 &&
		    X509_get_ext_by_NID(cert, nid, ret) >= 0;

	return ret;
}

/*
 * Modifies a certificate by deleting extensions and copying the issuer and
 * AKID from the presigner certificate, if necessary.
 * Returns 1 on success, 0 otherwise.
 */
static int
ct_x509_cert_fixup(X509 *cert, X509 *presigner)
{
	int preidx, certidx;
	int pre_akid_ext_is_dup, cert_akid_ext_is_dup;

	if (presigner == NULL)
		return 1;

	preidx = ct_x509_get_ext(presigner, NID_authority_key_identifier,
	    &pre_akid_ext_is_dup);
	certidx = ct_x509_get_ext(cert, NID_authority_key_identifier,
	    &cert_akid_ext_is_dup);

	/* An error occurred whilst searching for the extension */
	if (preidx < -1 || certidx < -1)
		return 0;
	/* Invalid certificate if they contain duplicate extensions */
	if (pre_akid_ext_is_dup || cert_akid_ext_is_dup)
		return 0;
	/* AKID must be present in both certificate or absent in both */
	if (preidx >= 0 && certidx == -1)
		return 0;
	if (preidx == -1 && certidx >= 0)
		return 0;
	/* Copy issuer name */
	if (!X509_set_issuer_name(cert, X509_get_issuer_name(presigner)))
		return 0;
	if (preidx != -1) {
		/* Retrieve and copy AKID encoding */
		X509_EXTENSION *preext = X509_get_ext(presigner, preidx);
		X509_EXTENSION *certext = X509_get_ext(cert, certidx);
		ASN1_OCTET_STRING *preextdata;

		/* Should never happen */
		if (preext == NULL || certext == NULL)
			return 0;
		preextdata = X509_EXTENSION_get_data(preext);
		if (preextdata == NULL ||
		    !X509_EXTENSION_set_data(certext, preextdata))
			return 0;
	}
	return 1;
}

int
SCT_CTX_set1_cert(SCT_CTX *sctx, X509 *cert, X509 *presigner)
{
	unsigned char *certder = NULL, *preder = NULL;
	X509 *pretmp = NULL;
	int certderlen = 0, prederlen = 0;
	int idx = -1;
	int poison_ext_is_dup, sct_ext_is_dup;
	int poison_idx = ct_x509_get_ext(cert, NID_ct_precert_poison, &poison_ext_is_dup);

	/* Duplicate poison extensions are present - error */
	if (poison_ext_is_dup)
		goto err;

	/* If *cert doesn't have a poison extension, it isn't a precert */
	if (poison_idx == -1) {
		/* cert isn't a precert, so we shouldn't have a presigner */
		if (presigner != NULL)
			goto err;

		certderlen = i2d_X509(cert, &certder);
		if (certderlen < 0)
			goto err;
	}

	/* See if cert has a precert SCTs extension */
	idx = ct_x509_get_ext(cert, NID_ct_precert_scts, &sct_ext_is_dup);
	/* Duplicate SCT extensions are present - error */
	if (sct_ext_is_dup)
		goto err;

	if (idx >= 0 && poison_idx >= 0) {
		/*
		 * cert can't both contain SCTs (i.e. have an SCT extension) and be a
		 * precert (i.e. have a poison extension).
		 */
		goto err;
	}

	if (idx == -1) {
		idx = poison_idx;
	}

	/*
	 * If either a poison or SCT extension is present, remove it before encoding
	 * cert. This, along with ct_x509_cert_fixup(), gets a TBSCertificate (see
	 * RFC5280) from cert, which is what the CT log signed when it produced the
	 * SCT.
	 */
	if (idx >= 0) {
		X509_EXTENSION *ext;

		/* Take a copy of certificate so we don't modify passed version */
		pretmp = X509_dup(cert);
		if (pretmp == NULL)
			goto err;

		ext = X509_delete_ext(pretmp, idx);
		X509_EXTENSION_free(ext);

		if (!ct_x509_cert_fixup(pretmp, presigner))
			goto err;

		prederlen = i2d_re_X509_tbs(pretmp, &preder);
		if (prederlen <= 0)
			goto err;
	}

	X509_free(pretmp);

	free(sctx->certder);
	sctx->certder = certder;
	sctx->certderlen = certderlen;

	free(sctx->preder);
	sctx->preder = preder;
	sctx->prederlen = prederlen;

	return 1;
 err:
	free(certder);
	free(preder);
	X509_free(pretmp);
	return 0;
}

static int
ct_public_key_hash(X509_PUBKEY *pkey, unsigned char **hash, size_t *hash_len)
{
	int ret = 0;
	unsigned char *md = NULL, *der = NULL;
	int der_len;
	unsigned int md_len;

	/* Reuse buffer if possible */
	if (*hash != NULL && *hash_len >= SHA256_DIGEST_LENGTH) {
		md = *hash;
	} else {
		md = malloc(SHA256_DIGEST_LENGTH);
		if (md == NULL)
			goto err;
	}

	/* Calculate key hash */
	der_len = i2d_X509_PUBKEY(pkey, &der);
	if (der_len <= 0)
		goto err;

	if (!EVP_Digest(der, der_len, md, &md_len, EVP_sha256(), NULL))
		goto err;

	if (md != *hash) {
		free(*hash);
		*hash = md;
		*hash_len = SHA256_DIGEST_LENGTH;
	}

	md = NULL;
	ret = 1;
 err:
	free(md);
	free(der);
	return ret;
}

int
SCT_CTX_set1_issuer(SCT_CTX *sctx, const X509 *issuer)
{
	return SCT_CTX_set1_issuer_pubkey(sctx, X509_get_X509_PUBKEY(issuer));
}

int
SCT_CTX_set1_issuer_pubkey(SCT_CTX *sctx, X509_PUBKEY *pubkey)
{
	return ct_public_key_hash(pubkey, &sctx->ihash, &sctx->ihashlen);
}

int
SCT_CTX_set1_pubkey(SCT_CTX *sctx, X509_PUBKEY *pubkey)
{
	EVP_PKEY *pkey = X509_PUBKEY_get(pubkey);

	if (pkey == NULL)
		return 0;

	if (!ct_public_key_hash(pubkey, &sctx->pkeyhash, &sctx->pkeyhashlen)) {
		EVP_PKEY_free(pkey);
		return 0;
	}

	EVP_PKEY_free(sctx->pkey);
	sctx->pkey = pkey;
	return 1;
}

void
SCT_CTX_set_time(SCT_CTX *sctx, uint64_t time_in_ms)
{
	sctx->epoch_time_in_ms = time_in_ms;
}
