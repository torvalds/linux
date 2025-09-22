/* $OpenBSD: ocsp_vfy.c,v 1.25 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000-2004 The OpenSSL Project.  All rights reserved.
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

#include <openssl/ocsp.h>
#include <string.h>

#include "err_local.h"
#include "ocsp_local.h"
#include "x509_local.h"

static int ocsp_find_signer(X509 **psigner, OCSP_BASICRESP *bs,
    STACK_OF(X509) *certs, X509_STORE *st, unsigned long flags);
static X509 *ocsp_find_signer_sk(STACK_OF(X509) *certs, OCSP_RESPID *id);
static int ocsp_check_issuer(OCSP_BASICRESP *bs, STACK_OF(X509) *chain,
    unsigned long flags);
static int ocsp_check_ids(STACK_OF(OCSP_SINGLERESP) *sresp, OCSP_CERTID **ret);
static int ocsp_match_issuerid(X509 *cert, OCSP_CERTID *cid,
    STACK_OF(OCSP_SINGLERESP) *sresp);
static int ocsp_check_delegated(X509 *x, int flags);
static int ocsp_req_find_signer(X509 **psigner, OCSP_REQUEST *req,
    X509_NAME *nm, STACK_OF(X509) *certs, X509_STORE *st,
    unsigned long flags);

/* Verify a basic response message */
int
OCSP_basic_verify(OCSP_BASICRESP *bs, STACK_OF(X509) *certs, X509_STORE *st,
    unsigned long flags)
{
	X509 *signer, *x;
	STACK_OF(X509) *chain = NULL;
	STACK_OF(X509) *untrusted = NULL;
	X509_STORE_CTX ctx;
	int i, ret = 0;

	ret = ocsp_find_signer(&signer, bs, certs, st, flags);
	if (!ret) {
		OCSPerror(OCSP_R_SIGNER_CERTIFICATE_NOT_FOUND);
		goto end;
	}
	if ((ret == 2) && (flags & OCSP_TRUSTOTHER))
		flags |= OCSP_NOVERIFY;
	if (!(flags & OCSP_NOSIGS)) {
		EVP_PKEY *skey;

		skey = X509_get0_pubkey(signer);
		if (skey) {
			ret = OCSP_BASICRESP_verify(bs, skey, 0);
		}
		if (!skey || ret <= 0) {
			OCSPerror(OCSP_R_SIGNATURE_FAILURE);
			goto end;
		}
	}
	if (!(flags & OCSP_NOVERIFY)) {
		int init_res;

		if (flags & OCSP_NOCHAIN) {
			untrusted = NULL;
		} else if (bs->certs && certs) {
			untrusted = sk_X509_dup(bs->certs);
			for (i = 0; i < sk_X509_num(certs); i++) {
				if (!sk_X509_push(untrusted,
					sk_X509_value(certs, i))) {
					OCSPerror(ERR_R_MALLOC_FAILURE);
					goto end;
				}
			}
		} else if (certs != NULL) {
			untrusted = certs;
		} else {
			untrusted = bs->certs;
		}
		init_res = X509_STORE_CTX_init(&ctx, st, signer, untrusted);
		if (!init_res) {
			ret = -1;
			OCSPerror(ERR_R_X509_LIB);
			goto end;
		}

		if (X509_STORE_CTX_set_purpose(&ctx,
		    X509_PURPOSE_OCSP_HELPER) == 0) {
			X509_STORE_CTX_cleanup(&ctx);
			ret = -1;
			goto end;
		}
		ret = X509_verify_cert(&ctx);
		chain = X509_STORE_CTX_get1_chain(&ctx);
		X509_STORE_CTX_cleanup(&ctx);
		if (ret <= 0) {
			i = X509_STORE_CTX_get_error(&ctx);
			OCSPerror(OCSP_R_CERTIFICATE_VERIFY_ERROR);
			ERR_asprintf_error_data("Verify error:%s",
			    X509_verify_cert_error_string(i));
			goto end;
		}
		if (flags & OCSP_NOCHECKS) {
			ret = 1;
			goto end;
		}
		/* At this point we have a valid certificate chain
		 * need to verify it against the OCSP issuer criteria.
		 */
		ret = ocsp_check_issuer(bs, chain, flags);

		/* If fatal error or valid match then finish */
		if (ret != 0)
			goto end;

		/* Easy case: explicitly trusted. Get root CA and
		 * check for explicit trust
		 */
		if (flags & OCSP_NOEXPLICIT)
			goto end;

		x = sk_X509_value(chain, sk_X509_num(chain) - 1);
		if (X509_check_trust(x, X509_TRUST_OCSP_SIGN, 0) !=
		    X509_TRUST_TRUSTED) {
			OCSPerror(OCSP_R_ROOT_CA_NOT_TRUSTED);
			goto end;
		}
		ret = 1;
	}

end:
	if (chain)
		sk_X509_pop_free(chain, X509_free);
	if (bs->certs && certs)
		sk_X509_free(untrusted);
	return ret;
}
LCRYPTO_ALIAS(OCSP_basic_verify);

int
OCSP_resp_get0_signer(OCSP_BASICRESP *bs, X509 **signer,
    STACK_OF(X509) *extra_certs)
{
	return ocsp_find_signer(signer, bs, extra_certs, NULL, 0) > 0;
}
LCRYPTO_ALIAS(OCSP_resp_get0_signer);

static int
ocsp_find_signer(X509 **psigner, OCSP_BASICRESP *bs, STACK_OF(X509) *certs,
    X509_STORE *st, unsigned long flags)
{
	X509 *signer;
	OCSP_RESPID *rid = bs->tbsResponseData->responderId;

	if ((signer = ocsp_find_signer_sk(certs, rid))) {
		*psigner = signer;
		return 2;
	}
	if (!(flags & OCSP_NOINTERN) &&
	    (signer = ocsp_find_signer_sk(bs->certs, rid))) {
		*psigner = signer;
		return 1;
	}
	/* Maybe lookup from store if by subject name */

	*psigner = NULL;
	return 0;
}

static X509 *
ocsp_find_signer_sk(STACK_OF(X509) *certs, OCSP_RESPID *id)
{
	int i;
	unsigned char tmphash[SHA_DIGEST_LENGTH], *keyhash;
	X509 *x;

	/* Easy if lookup by name */
	if (id->type == V_OCSP_RESPID_NAME)
		return X509_find_by_subject(certs, id->value.byName);

	/* Lookup by key hash */

	/* If key hash isn't SHA1 length then forget it */
	if (id->value.byKey->length != SHA_DIGEST_LENGTH)
		return NULL;
	keyhash = id->value.byKey->data;
	/* Calculate hash of each key and compare */
	for (i = 0; i < sk_X509_num(certs); i++) {
		x = sk_X509_value(certs, i);
		X509_pubkey_digest(x, EVP_sha1(), tmphash, NULL);
		if (!memcmp(keyhash, tmphash, SHA_DIGEST_LENGTH))
			return x;
	}
	return NULL;
}

static int
ocsp_check_issuer(OCSP_BASICRESP *bs, STACK_OF(X509) *chain,
    unsigned long flags)
{
	STACK_OF(OCSP_SINGLERESP) *sresp;
	X509 *signer, *sca;
	OCSP_CERTID *caid = NULL;
	int i;

	sresp = bs->tbsResponseData->responses;

	if (sk_X509_num(chain) <= 0) {
		OCSPerror(OCSP_R_NO_CERTIFICATES_IN_CHAIN);
		return -1;
	}

	/* See if the issuer IDs match. */
	i = ocsp_check_ids(sresp, &caid);

	/* If ID mismatch or other error then return */
	if (i <= 0)
		return i;

	signer = sk_X509_value(chain, 0);
	/* Check to see if OCSP responder CA matches request CA */
	if (sk_X509_num(chain) > 1) {
		sca = sk_X509_value(chain, 1);
		i = ocsp_match_issuerid(sca, caid, sresp);
		if (i < 0)
			return i;
		if (i) {
			/* We have a match, if extensions OK then success */
			if (ocsp_check_delegated(signer, flags))
				return 1;
			return 0;
		}
	}

	/* Otherwise check if OCSP request signed directly by request CA */
	return ocsp_match_issuerid(signer, caid, sresp);
}

/* Check the issuer certificate IDs for equality. If there is a mismatch with the same
 * algorithm then there's no point trying to match any certificates against the issuer.
 * If the issuer IDs all match then we just need to check equality against one of them.
 */
static int
ocsp_check_ids(STACK_OF(OCSP_SINGLERESP) *sresp, OCSP_CERTID **ret)
{
	OCSP_CERTID *tmpid, *cid;
	int i, idcount;

	idcount = sk_OCSP_SINGLERESP_num(sresp);
	if (idcount <= 0) {
		OCSPerror(OCSP_R_RESPONSE_CONTAINS_NO_REVOCATION_DATA);
		return -1;
	}

	cid = sk_OCSP_SINGLERESP_value(sresp, 0)->certId;

	*ret = NULL;

	for (i = 1; i < idcount; i++) {
		tmpid = sk_OCSP_SINGLERESP_value(sresp, i)->certId;
		/* Check to see if IDs match */
		if (OCSP_id_issuer_cmp(cid, tmpid)) {
			return 0;
		}
	}

	/* All IDs match: only need to check one ID */
	*ret = cid;
	return 1;
}

static int
ocsp_match_issuerid(X509 *cert, OCSP_CERTID *cid,
    STACK_OF(OCSP_SINGLERESP) *sresp)
{
	/* If only one ID to match then do it */
	if (cid) {
		const EVP_MD *dgst;
		X509_NAME *iname;
		int mdlen;
		unsigned char md[EVP_MAX_MD_SIZE];

		if (!(dgst =
		    EVP_get_digestbyobj(cid->hashAlgorithm->algorithm))) {
			OCSPerror(OCSP_R_UNKNOWN_MESSAGE_DIGEST);
			return -1;
		}

		mdlen = EVP_MD_size(dgst);
		if (mdlen < 0)
			return -1;
		if (cid->issuerNameHash->length != mdlen ||
		    cid->issuerKeyHash->length != mdlen)
			return 0;
		iname = X509_get_subject_name(cert);
		if (!X509_NAME_digest(iname, dgst, md, NULL))
			return -1;
		if (memcmp(md, cid->issuerNameHash->data, mdlen))
			return 0;
		X509_pubkey_digest(cert, dgst, md, NULL);
		if (memcmp(md, cid->issuerKeyHash->data, mdlen))
			return 0;

		return 1;
	} else {
		/* We have to match the whole lot */
		int i, ret;
		OCSP_CERTID *tmpid;

		for (i = 0; i < sk_OCSP_SINGLERESP_num(sresp); i++) {
			tmpid = sk_OCSP_SINGLERESP_value(sresp, i)->certId;
			ret = ocsp_match_issuerid(cert, tmpid, NULL);
			if (ret <= 0)
				return ret;
		}
		return 1;
	}
}

static int
ocsp_check_delegated(X509 *x, int flags)
{
	X509_check_purpose(x, -1, 0);
	if ((x->ex_flags & EXFLAG_XKUSAGE) && (x->ex_xkusage & XKU_OCSP_SIGN))
		return 1;
	OCSPerror(OCSP_R_MISSING_OCSPSIGNING_USAGE);
	return 0;
}

/* Verify an OCSP request. This is fortunately much easier than OCSP
 * response verify. Just find the signers certificate and verify it
 * against a given trust value.
 */
int
OCSP_request_verify(OCSP_REQUEST *req, STACK_OF(X509) *certs, X509_STORE *store,
    unsigned long flags)
{
	X509 *signer;
	X509_NAME *nm;
	GENERAL_NAME *gen;
	int ret;
	X509_STORE_CTX ctx;

	if (!req->optionalSignature) {
		OCSPerror(OCSP_R_REQUEST_NOT_SIGNED);
		return 0;
	}
	gen = req->tbsRequest->requestorName;
	if (!gen || gen->type != GEN_DIRNAME) {
		OCSPerror(OCSP_R_UNSUPPORTED_REQUESTORNAME_TYPE);
		return 0;
	}
	nm = gen->d.directoryName;
	ret = ocsp_req_find_signer(&signer, req, nm, certs, store, flags);
	if (ret <= 0) {
		OCSPerror(OCSP_R_SIGNER_CERTIFICATE_NOT_FOUND);
		return 0;
	}
	if ((ret == 2) && (flags & OCSP_TRUSTOTHER))
		flags |= OCSP_NOVERIFY;
	if (!(flags & OCSP_NOSIGS)) {
		EVP_PKEY *skey;

		if ((skey = X509_get0_pubkey(signer)) == NULL)
			return 0;
		ret = OCSP_REQUEST_verify(req, skey);
		if (ret <= 0) {
			OCSPerror(OCSP_R_SIGNATURE_FAILURE);
			return 0;
		}
	}
	if (!(flags & OCSP_NOVERIFY)) {
		int init_res;

		if (flags & OCSP_NOCHAIN)
			init_res = X509_STORE_CTX_init(&ctx, store, signer,
			    NULL);
		else
			init_res = X509_STORE_CTX_init(&ctx, store, signer,
			    req->optionalSignature->certs);
		if (!init_res) {
			OCSPerror(ERR_R_X509_LIB);
			return 0;
		}

		if (X509_STORE_CTX_set_purpose(&ctx,
		      X509_PURPOSE_OCSP_HELPER) == 0 ||
		    X509_STORE_CTX_set_trust(&ctx,
		      X509_TRUST_OCSP_REQUEST) == 0) {
			X509_STORE_CTX_cleanup(&ctx);
			return 0;
		}
		ret = X509_verify_cert(&ctx);
		X509_STORE_CTX_cleanup(&ctx);
		if (ret <= 0) {
			ret = X509_STORE_CTX_get_error(&ctx);
			OCSPerror(OCSP_R_CERTIFICATE_VERIFY_ERROR);
			ERR_asprintf_error_data("Verify error:%s",
			    X509_verify_cert_error_string(ret));
			return 0;
		}
	}
	return 1;
}
LCRYPTO_ALIAS(OCSP_request_verify);

static int
ocsp_req_find_signer(X509 **psigner, OCSP_REQUEST *req, X509_NAME *nm,
    STACK_OF(X509) *certs, X509_STORE *st, unsigned long flags)
{
	X509 *signer;

	if (!(flags & OCSP_NOINTERN)) {
		signer = X509_find_by_subject(req->optionalSignature->certs, nm);
		if (signer) {
			*psigner = signer;
			return 1;
		}
	}

	signer = X509_find_by_subject(certs, nm);
	if (signer) {
		*psigner = signer;
		return 2;
	}
	return 0;
}
