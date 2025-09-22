/* $OpenBSD: ts_rsp_verify.c,v 1.31 2025/05/10 05:54:39 tb Exp $ */
/* Written by Zoltan Glozik (zglozik@stones.com) for the OpenSSL
 * project 2002.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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

#include <openssl/objects.h>
#include <openssl/pkcs7.h>
#include <openssl/ts.h>

#include "err_local.h"
#include "evp_local.h"
#include "ts_local.h"
#include "x509_local.h"

/* Private function declarations. */

static int TS_verify_cert(X509_STORE *store, STACK_OF(X509) *untrusted,
    X509 *signer, STACK_OF(X509) **chain);
static int TS_check_signing_certs(PKCS7_SIGNER_INFO *si, STACK_OF(X509) *chain);
static ESS_SIGNING_CERT *ESS_get_signing_cert(PKCS7_SIGNER_INFO *si);
static int TS_find_cert(STACK_OF(ESS_CERT_ID) *cert_ids, X509 *cert);
static ESS_SIGNING_CERT_V2 *ESS_get_signing_cert_v2(PKCS7_SIGNER_INFO *si);
static int TS_find_cert_v2(STACK_OF(ESS_CERT_ID_V2) *cert_ids, X509 *cert);
static int TS_issuer_serial_cmp(ESS_ISSUER_SERIAL *is, X509 *cert);
static int int_TS_RESP_verify_token(TS_VERIFY_CTX *ctx,
    PKCS7 *token, TS_TST_INFO *tst_info);
static int TS_check_status_info(TS_RESP *response);
static char *TS_get_status_text(STACK_OF(ASN1_UTF8STRING) *text);
static int TS_check_policy(ASN1_OBJECT *req_oid, TS_TST_INFO *tst_info);
static int TS_compute_imprint(BIO *data, TS_TST_INFO *tst_info,
    X509_ALGOR **md_alg,
    unsigned char **imprint, unsigned *imprint_len);
static int TS_check_imprints(X509_ALGOR *algor_a,
    unsigned char *imprint_a, unsigned len_a,
    TS_TST_INFO *tst_info);
static int TS_check_nonces(const ASN1_INTEGER *a, TS_TST_INFO *tst_info);
static int TS_check_signer_name(GENERAL_NAME *tsa_name, X509 *signer);
static int TS_find_name(STACK_OF(GENERAL_NAME) *gen_names, GENERAL_NAME *name);

/*
 * Local mapping between response codes and descriptions.
 * Don't forget to change TS_STATUS_BUF_SIZE when modifying
 * the elements of this array.
 */
static const char *TS_status_text[] = {
	"granted",
	"grantedWithMods",
	"rejection",
	"waiting",
	"revocationWarning",
	"revocationNotification"
};

#define TS_STATUS_TEXT_SIZE	(sizeof(TS_status_text)/sizeof(*TS_status_text))

/*
 * This must be greater or equal to the sum of the strings in TS_status_text
 * plus the number of its elements.
 */
#define TS_STATUS_BUF_SIZE	256

static struct {
	int code;
	const char *text;
} TS_failure_info[] = {
	{ TS_INFO_BAD_ALG, "badAlg" },
	{ TS_INFO_BAD_REQUEST, "badRequest" },
	{ TS_INFO_BAD_DATA_FORMAT, "badDataFormat" },
	{ TS_INFO_TIME_NOT_AVAILABLE, "timeNotAvailable" },
	{ TS_INFO_UNACCEPTED_POLICY, "unacceptedPolicy" },
	{ TS_INFO_UNACCEPTED_EXTENSION, "unacceptedExtension" },
	{ TS_INFO_ADD_INFO_NOT_AVAILABLE, "addInfoNotAvailable" },
	{ TS_INFO_SYSTEM_FAILURE, "systemFailure" }
};

#define TS_FAILURE_INFO_SIZE	(sizeof(TS_failure_info) / \
				sizeof(*TS_failure_info))

/* Functions for verifying a signed TS_TST_INFO structure. */

/*
 * This function carries out the following tasks:
 *	- Checks if there is one and only one signer.
 *	- Search for the signing certificate in 'certs' and in the response.
 *	- Check the extended key usage and key usage fields of the signer
 *	certificate (done by the path validation).
 *	- Build and validate the certificate path.
 *	- Check if the certificate path meets the requirements of the
 *	SigningCertificate ESS signed attribute.
 *	- Verify the signature value.
 *	- Returns the signer certificate in 'signer', if 'signer' is not NULL.
 */
int
TS_RESP_verify_signature(PKCS7 *token, STACK_OF(X509) *certs,
    X509_STORE *store, X509 **signer_out)
{
	STACK_OF(PKCS7_SIGNER_INFO) *sinfos = NULL;
	PKCS7_SIGNER_INFO *si;
	STACK_OF(X509) *signers = NULL;
	X509	*signer;
	STACK_OF(X509) *chain = NULL;
	char	buf[4096];
	int	i, j = 0, ret = 0;
	BIO	*p7bio = NULL;

	/* Some sanity checks first. */
	if (!token) {
		TSerror(TS_R_INVALID_NULL_POINTER);
		goto err;
	}

	/* Check for the correct content type */
	if (!PKCS7_type_is_signed(token)) {
		TSerror(TS_R_WRONG_CONTENT_TYPE);
		goto err;
	}

	/* Check if there is one and only one signer. */
	sinfos = PKCS7_get_signer_info(token);
	if (!sinfos || sk_PKCS7_SIGNER_INFO_num(sinfos) != 1) {
		TSerror(TS_R_THERE_MUST_BE_ONE_SIGNER);
		goto err;
	}
	si = sk_PKCS7_SIGNER_INFO_value(sinfos, 0);

	/* Check for no content: no data to verify signature. */
	if (PKCS7_get_detached(token)) {
		TSerror(TS_R_NO_CONTENT);
		goto err;
	}

	/* Get hold of the signer certificate, search only internal
	   certificates if it was requested. */
	signers = PKCS7_get0_signers(token, certs, 0);
	if (!signers || sk_X509_num(signers) != 1)
		goto err;
	signer = sk_X509_value(signers, 0);

	/* Now verify the certificate. */
	if (!TS_verify_cert(store, certs, signer, &chain))
		goto err;

	/* Check if the signer certificate is consistent with the
	   ESS extension. */
	if (!TS_check_signing_certs(si, chain))
		goto err;

	/* Creating the message digest. */
	p7bio = PKCS7_dataInit(token, NULL);

	/* We now have to 'read' from p7bio to calculate digests etc. */
	while ((i = BIO_read(p7bio, buf, sizeof(buf))) > 0)
		;

	/* Verifying the signature. */
	j = PKCS7_signatureVerify(p7bio, token, si, signer);
	if (j <= 0) {
		TSerror(TS_R_SIGNATURE_FAILURE);
		goto err;
	}

	/* Return the signer certificate if needed. */
	if (signer_out) {
		*signer_out = signer;
		CRYPTO_add(&signer->references, 1, CRYPTO_LOCK_X509);
	}

	ret = 1;

err:
	BIO_free_all(p7bio);
	sk_X509_pop_free(chain, X509_free);
	sk_X509_free(signers);

	return ret;
}
LCRYPTO_ALIAS(TS_RESP_verify_signature);

/*
 * The certificate chain is returned in chain. Caller is responsible for
 * freeing the vector.
 */
static int
TS_verify_cert(X509_STORE *store, STACK_OF(X509) *untrusted, X509 *signer,
    STACK_OF(X509) **chain)
{
	X509_STORE_CTX cert_ctx;
	int i;
	int ret = 0;

	/* chain is an out argument. */
	*chain = NULL;
	if (X509_STORE_CTX_init(&cert_ctx, store, signer, untrusted) == 0) {
		TSerror(ERR_R_X509_LIB);
		goto err;
	}
	if (X509_STORE_CTX_set_purpose(&cert_ctx,
	    X509_PURPOSE_TIMESTAMP_SIGN) == 0)
		goto err;
	i = X509_verify_cert(&cert_ctx);
	if (i <= 0) {
		int j = X509_STORE_CTX_get_error(&cert_ctx);

		TSerror(TS_R_CERTIFICATE_VERIFY_ERROR);
		ERR_asprintf_error_data("Verify error:%s",
		    X509_verify_cert_error_string(j));
		goto err;
	} else {
		/* Get a copy of the certificate chain. */
		*chain = X509_STORE_CTX_get1_chain(&cert_ctx);
		ret = 1;
	}

err:
	X509_STORE_CTX_cleanup(&cert_ctx);

	return ret;
}

static int
TS_check_signing_certs(PKCS7_SIGNER_INFO *si, STACK_OF(X509) *chain)
{
	ESS_SIGNING_CERT *ss = NULL;
	STACK_OF(ESS_CERT_ID) *cert_ids;
	ESS_SIGNING_CERT_V2 *ssv2 = NULL;
	STACK_OF(ESS_CERT_ID_V2) *cert_ids_v2;
	X509 *cert;
	int i = 0;
	int ret = 0;

	if ((ss = ESS_get_signing_cert(si)) != NULL) {
		cert_ids = ss->cert_ids;
		/* The signer certificate must be the first in cert_ids. */
		cert = sk_X509_value(chain, 0);

		if (TS_find_cert(cert_ids, cert) != 0)
			goto err;

		/*
		 * Check the other certificates of the chain if there are more
		 * than one certificate ids in cert_ids.
		 */
		if (sk_ESS_CERT_ID_num(cert_ids) > 1) {
			/* All the certificates of the chain must be in cert_ids. */
			for (i = 1; i < sk_X509_num(chain); i++) {
				cert = sk_X509_value(chain, i);

				if (TS_find_cert(cert_ids, cert) < 0)
					goto err;
			}
		}
	}

	if ((ssv2 = ESS_get_signing_cert_v2(si)) != NULL) {
		cert_ids_v2 = ssv2->cert_ids;
		/* The signer certificate must be the first in cert_ids_v2. */
		cert = sk_X509_value(chain, 0);

		if (TS_find_cert_v2(cert_ids_v2, cert) != 0)
			goto err;

		/*
		 * Check the other certificates of the chain if there are more
		 * than one certificate ids in cert_ids_v2.
		 */
		if (sk_ESS_CERT_ID_V2_num(cert_ids_v2) > 1) {
			/* All the certificates of the chain must be in cert_ids_v2. */
			for (i = 1; i < sk_X509_num(chain); i++) {
				cert = sk_X509_value(chain, i);

				if (TS_find_cert_v2(cert_ids_v2, cert) < 0)
					goto err;
			}
		}
	}

	ret = 1;

err:
	if (!ret)
		TSerror(TS_R_ESS_SIGNING_CERTIFICATE_ERROR);
	ESS_SIGNING_CERT_free(ss);
	ESS_SIGNING_CERT_V2_free(ssv2);
	return ret;
}

static ESS_SIGNING_CERT *
ESS_get_signing_cert(PKCS7_SIGNER_INFO *si)
{
	ASN1_TYPE *attr;
	const unsigned char *p;

	attr = PKCS7_get_signed_attribute(si,
	    NID_id_smime_aa_signingCertificate);
	if (!attr)
		return NULL;
	if (attr->type != V_ASN1_SEQUENCE)
		return NULL;
	p = attr->value.sequence->data;
	return d2i_ESS_SIGNING_CERT(NULL, &p, attr->value.sequence->length);
}

static ESS_SIGNING_CERT_V2 *
ESS_get_signing_cert_v2(PKCS7_SIGNER_INFO *si)
{
	ASN1_TYPE *attr;
	const unsigned char *p;

	attr = PKCS7_get_signed_attribute(si, NID_id_smime_aa_signingCertificateV2);
	if (attr == NULL)
		return NULL;
	p = attr->value.sequence->data;
	return d2i_ESS_SIGNING_CERT_V2(NULL, &p, attr->value.sequence->length);
}

/* Returns < 0 if certificate is not found, certificate index otherwise. */
static int
TS_find_cert(STACK_OF(ESS_CERT_ID) *cert_ids, X509 *cert)
{
	int i;
	unsigned char cert_hash[TS_HASH_LEN];

	if (!cert_ids || !cert)
		return -1;

	if (!X509_digest(cert, TS_HASH_EVP, cert_hash, NULL))
		return -1;

	/* Recompute SHA1 hash of certificate if necessary (side effect). */
	if (X509_check_purpose(cert, -1, 0) == -1)
		return -1;

	/* Look for cert in the cert_ids vector. */
	for (i = 0; i < sk_ESS_CERT_ID_num(cert_ids); ++i) {
		ESS_CERT_ID *cid = sk_ESS_CERT_ID_value(cert_ids, i);

		/* Check the SHA-1 hash first. */
		if (cid->hash->length == TS_HASH_LEN && !memcmp(cid->hash->data,
		    cert_hash, TS_HASH_LEN)) {
			/* Check the issuer/serial as well if specified. */
			ESS_ISSUER_SERIAL *is = cid->issuer_serial;

			if (is == NULL || TS_issuer_serial_cmp(is, cert) == 0)
				return i;
		}
	}

	return -1;
}

/* Returns < 0 if certificate is not found, certificate index otherwise. */
static int
TS_find_cert_v2(STACK_OF(ESS_CERT_ID_V2) *cert_ids, X509 *cert)
{
	int i;
	unsigned char cert_digest[EVP_MAX_MD_SIZE];
	unsigned int len;

	/* Look for cert in the cert_ids vector. */
	for (i = 0; i < sk_ESS_CERT_ID_V2_num(cert_ids); ++i) {
		ESS_CERT_ID_V2 *cid = sk_ESS_CERT_ID_V2_value(cert_ids, i);
		const EVP_MD *md = EVP_sha256();

		if (cid->hash_alg != NULL)
			md = EVP_get_digestbyobj(cid->hash_alg->algorithm);
		if (md == NULL)
			return -1;

		if (!X509_digest(cert, md, cert_digest, &len))
			return -1;

		if ((unsigned int)cid->hash->length != len)
			return -1;

		if (memcmp(cid->hash->data, cert_digest, cid->hash->length) == 0) {
			ESS_ISSUER_SERIAL *is = cid->issuer_serial;

			if (is == NULL || TS_issuer_serial_cmp(is, cert) == 0)
				return i;
		}
	}

	return -1;
}

static int
TS_issuer_serial_cmp(ESS_ISSUER_SERIAL *is, X509 *cert)
{
	GENERAL_NAME *issuer;

	if (is == NULL || cert == NULL || sk_GENERAL_NAME_num(is->issuer) != 1)
		return -1;

	/* Check the issuer first. It must be a directory name. */
	issuer = sk_GENERAL_NAME_value(is->issuer, 0);
	if (issuer->type != GEN_DIRNAME ||
	    X509_NAME_cmp(issuer->d.dirn, X509_get_issuer_name(cert)))
		return -1;

	/* Check the serial number, too. */
	if (ASN1_INTEGER_cmp(is->serial, X509_get_serialNumber(cert)))
		return -1;

	return 0;
}

/*
 * Verifies whether 'response' contains a valid response with regards
 * to the settings of the context:
 *	- Gives an error message if the TS_TST_INFO is not present.
 *	- Calls _TS_RESP_verify_token to verify the token content.
 */
int
TS_RESP_verify_response(TS_VERIFY_CTX *ctx, TS_RESP *response)
{
	PKCS7 *token = TS_RESP_get_token(response);
	TS_TST_INFO *tst_info = TS_RESP_get_tst_info(response);
	int ret = 0;

	/* Check if we have a successful TS_TST_INFO object in place. */
	if (!TS_check_status_info(response))
		goto err;

	/* Check the contents of the time stamp token. */
	if (!int_TS_RESP_verify_token(ctx, token, tst_info))
		goto err;

	ret = 1;

err:
	return ret;
}
LCRYPTO_ALIAS(TS_RESP_verify_response);

/*
 * Tries to extract a TS_TST_INFO structure from the PKCS7 token and
 * calls the internal int_TS_RESP_verify_token function for verifying it.
 */
int
TS_RESP_verify_token(TS_VERIFY_CTX *ctx, PKCS7 *token)
{
	TS_TST_INFO *tst_info = PKCS7_to_TS_TST_INFO(token);
	int ret = 0;

	if (tst_info) {
		ret = int_TS_RESP_verify_token(ctx, token, tst_info);
		TS_TST_INFO_free(tst_info);
	}
	return ret;
}
LCRYPTO_ALIAS(TS_RESP_verify_token);

/*
 * Verifies whether the 'token' contains a valid time stamp token
 * with regards to the settings of the context. Only those checks are
 * carried out that are specified in the context:
 *	- Verifies the signature of the TS_TST_INFO.
 *	- Checks the version number of the response.
 *	- Check if the requested and returned policies math.
 *	- Check if the message imprints are the same.
 *	- Check if the nonces are the same.
 *	- Check if the TSA name matches the signer.
 *	- Check if the TSA name is the expected TSA.
 */
static int
int_TS_RESP_verify_token(TS_VERIFY_CTX *ctx, PKCS7 *token,
    TS_TST_INFO *tst_info)
{
	X509 *signer = NULL;
	GENERAL_NAME *tsa_name = TS_TST_INFO_get_tsa(tst_info);
	X509_ALGOR *md_alg = NULL;
	unsigned char *imprint = NULL;
	unsigned imprint_len = 0;
	int ret = 0;

	/* Verify the signature. */
	if ((ctx->flags & TS_VFY_SIGNATURE) &&
	    !TS_RESP_verify_signature(token, ctx->certs, ctx->store, &signer))
		goto err;

	/* Check version number of response. */
	if ((ctx->flags & TS_VFY_VERSION) &&
	    TS_TST_INFO_get_version(tst_info) != 1) {
		TSerror(TS_R_UNSUPPORTED_VERSION);
		goto err;
	}

	/* Check policies. */
	if ((ctx->flags & TS_VFY_POLICY) &&
	    !TS_check_policy(ctx->policy, tst_info))
		goto err;

	/* Check message imprints. */
	if ((ctx->flags & TS_VFY_IMPRINT) &&
	    !TS_check_imprints(ctx->md_alg, ctx->imprint, ctx->imprint_len,
		tst_info))
		goto err;

	/* Compute and check message imprints. */
	if ((ctx->flags & TS_VFY_DATA) &&
	    (!TS_compute_imprint(ctx->data, tst_info,
	    &md_alg, &imprint, &imprint_len) ||
	    !TS_check_imprints(md_alg, imprint, imprint_len, tst_info)))
		goto err;

	/* Check nonces. */
	if ((ctx->flags & TS_VFY_NONCE) &&
	    !TS_check_nonces(ctx->nonce, tst_info))
		goto err;

	/* Check whether TSA name and signer certificate match. */
	if ((ctx->flags & TS_VFY_SIGNER) &&
	    tsa_name && !TS_check_signer_name(tsa_name, signer)) {
		TSerror(TS_R_TSA_NAME_MISMATCH);
		goto err;
	}

	/* Check whether the TSA is the expected one. */
	if ((ctx->flags & TS_VFY_TSA_NAME) &&
	    !TS_check_signer_name(ctx->tsa_name, signer)) {
		TSerror(TS_R_TSA_UNTRUSTED);
		goto err;
	}

	ret = 1;

err:
	X509_free(signer);
	X509_ALGOR_free(md_alg);
	free(imprint);
	return ret;
}

static int
TS_check_status_info(TS_RESP *response)
{
	TS_STATUS_INFO *info = TS_RESP_get_status_info(response);
	long status = ASN1_INTEGER_get(info->status);
	const char *status_text = NULL;
	char *embedded_status_text = NULL;
	char failure_text[TS_STATUS_BUF_SIZE] = "";

	/* Check if everything went fine. */
	if (status == 0 || status == 1)
		return 1;

	/* There was an error, get the description in status_text. */
	if (0 <= status && status < (long)TS_STATUS_TEXT_SIZE)
		status_text = TS_status_text[status];
	else
		status_text = "unknown code";

	/* Set the embedded_status_text to the returned description. */
	if (sk_ASN1_UTF8STRING_num(info->text) > 0 &&
	    !(embedded_status_text = TS_get_status_text(info->text)))
		return 0;

	/* Filling in failure_text with the failure information. */
	if (info->failure_info) {
		int i;
		int first = 1;
		for (i = 0; i < (int)TS_FAILURE_INFO_SIZE; ++i) {
			if (ASN1_BIT_STRING_get_bit(info->failure_info,
			    TS_failure_info[i].code)) {
				if (!first)
					strlcat(failure_text, ",",
					    TS_STATUS_BUF_SIZE);
				else
					first = 0;
				strlcat(failure_text, TS_failure_info[i].text,
				    TS_STATUS_BUF_SIZE);
			}
		}
	}
	if (failure_text[0] == '\0')
		strlcpy(failure_text, "unspecified", TS_STATUS_BUF_SIZE);

	/* Making up the error string. */
	TSerror(TS_R_NO_TIME_STAMP_TOKEN);
	ERR_asprintf_error_data
	    ("status code: %s, status text: %s, failure codes: %s",
	    status_text,
	    embedded_status_text ? embedded_status_text : "unspecified",
	    failure_text);
	free(embedded_status_text);

	return 0;
}

static char *
TS_get_status_text(STACK_OF(ASN1_UTF8STRING) *text)
{
	int i;
	unsigned int length = 0;
	char *result = NULL;

	/* Determine length first. */
	for (i = 0; i < sk_ASN1_UTF8STRING_num(text); ++i) {
		ASN1_UTF8STRING *current = sk_ASN1_UTF8STRING_value(text, i);
		length += ASN1_STRING_length(current);
		length += 1;	/* separator character */
	}
	/* Allocate memory (closing '\0' included). */
	if (!(result = malloc(length))) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	/* Concatenate the descriptions. */
	result[0] = '\0';
	for (i = 0; i < sk_ASN1_UTF8STRING_num(text); ++i) {
		ASN1_UTF8STRING *current = sk_ASN1_UTF8STRING_value(text, i);
		if (i > 0)
			strlcat(result, "/", length);
		strlcat(result, (const char *)ASN1_STRING_data(current), length);
	}
	return result;
}

static int
TS_check_policy(ASN1_OBJECT *req_oid, TS_TST_INFO *tst_info)
{
	ASN1_OBJECT *resp_oid = TS_TST_INFO_get_policy_id(tst_info);

	if (OBJ_cmp(req_oid, resp_oid) != 0) {
		TSerror(TS_R_POLICY_MISMATCH);
		return 0;
	}

	return 1;
}

static int
TS_compute_imprint(BIO *data, TS_TST_INFO *tst_info, X509_ALGOR **out_md_alg,
    unsigned char **out_imprint, unsigned int *out_imprint_len)
{
	TS_MSG_IMPRINT *msg_imprint;
	X509_ALGOR *md_alg_resp;
	X509_ALGOR *md_alg = NULL;
	unsigned char *imprint = NULL;
	unsigned int imprint_len = 0;
	const EVP_MD *md;
	EVP_MD_CTX md_ctx;
	unsigned char buffer[4096];
	int length;

	*out_md_alg = NULL;
	*out_imprint = NULL;
	*out_imprint_len = 0;

	/* Retrieve the MD algorithm of the response. */
	msg_imprint = TS_TST_INFO_get_msg_imprint(tst_info);
	md_alg_resp = TS_MSG_IMPRINT_get_algo(msg_imprint);
	if ((md_alg = X509_ALGOR_dup(md_alg_resp)) == NULL)
		goto err;

	/* Getting the MD object. */
	if ((md = EVP_get_digestbyobj((md_alg)->algorithm)) == NULL) {
		TSerror(TS_R_UNSUPPORTED_MD_ALGORITHM);
		goto err;
	}

	/* Compute message digest. */
	if ((length = EVP_MD_size(md)) < 0)
		goto err;
	imprint_len = length;
	if ((imprint = malloc(imprint_len)) == NULL) {
		TSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EVP_DigestInit(&md_ctx, md))
		goto err;
	while ((length = BIO_read(data, buffer, sizeof(buffer))) > 0) {
		if (!EVP_DigestUpdate(&md_ctx, buffer, length))
			goto err;
	}
	if (!EVP_DigestFinal(&md_ctx, imprint, NULL))
		goto err;

	*out_md_alg = md_alg;
	md_alg = NULL;
	*out_imprint = imprint;
	imprint = NULL;
	*out_imprint_len = imprint_len;

	return 1;

err:
	X509_ALGOR_free(md_alg);
	free(imprint);
	return 0;
}

static int
TS_check_imprints(X509_ALGOR *algor_a, unsigned char *imprint_a, unsigned len_a,
    TS_TST_INFO *tst_info)
{
	TS_MSG_IMPRINT *b = TS_TST_INFO_get_msg_imprint(tst_info);
	X509_ALGOR *algor_b = TS_MSG_IMPRINT_get_algo(b);
	int ret = 0;

	/* algor_a is optional. */
	if (algor_a) {
		/* Compare algorithm OIDs. */
		if (OBJ_cmp(algor_a->algorithm, algor_b->algorithm))
			goto err;

		/* The parameter must be NULL in both. */
		if ((algor_a->parameter &&
		    ASN1_TYPE_get(algor_a->parameter) != V_ASN1_NULL) ||
		    (algor_b->parameter &&
		    ASN1_TYPE_get(algor_b->parameter) != V_ASN1_NULL))
			goto err;
	}

	/* Compare octet strings. */
	ret = len_a == (unsigned) ASN1_STRING_length(b->hashed_msg) &&
	    memcmp(imprint_a, ASN1_STRING_data(b->hashed_msg), len_a) == 0;

err:
	if (!ret)
		TSerror(TS_R_MESSAGE_IMPRINT_MISMATCH);
	return ret;
}

static int
TS_check_nonces(const ASN1_INTEGER *a, TS_TST_INFO *tst_info)
{
	const ASN1_INTEGER *b = TS_TST_INFO_get_nonce(tst_info);

	/* Error if nonce is missing. */
	if (!b) {
		TSerror(TS_R_NONCE_NOT_RETURNED);
		return 0;
	}

	/* No error if a nonce is returned without being requested. */
	if (ASN1_INTEGER_cmp(a, b) != 0) {
		TSerror(TS_R_NONCE_MISMATCH);
		return 0;
	}

	return 1;
}

/* Check if the specified TSA name matches either the subject
   or one of the subject alternative names of the TSA certificate. */
static int
TS_check_signer_name(GENERAL_NAME *tsa_name, X509 *signer)
{
	STACK_OF(GENERAL_NAME) *gen_names = NULL;
	int idx = -1;
	int found = 0;

	if (signer == NULL)
		return 0;

	/* Check the subject name first. */
	if (tsa_name->type == GEN_DIRNAME &&
	    X509_name_cmp(tsa_name->d.dirn, X509_get_subject_name(signer)) == 0)
		return 1;

	/* Check all the alternative names. */
	gen_names = X509_get_ext_d2i(signer, NID_subject_alt_name,
	    NULL, &idx);
	while (gen_names != NULL &&
	    !(found = (TS_find_name(gen_names, tsa_name) >= 0))) {
		/* Get the next subject alternative name,
		   although there should be no more than one. */
		GENERAL_NAMES_free(gen_names);
		gen_names = X509_get_ext_d2i(signer, NID_subject_alt_name,
		    NULL, &idx);
	}
	if (gen_names)
		GENERAL_NAMES_free(gen_names);

	return found;
}

/* Returns 1 if name is in gen_names, 0 otherwise. */
static int
TS_find_name(STACK_OF(GENERAL_NAME) *gen_names, GENERAL_NAME *name)
{
	int i, found;
	for (i = 0, found = 0; !found && i < sk_GENERAL_NAME_num(gen_names);
	    ++i) {
		GENERAL_NAME *current = sk_GENERAL_NAME_value(gen_names, i);
		found = GENERAL_NAME_cmp(current, name) == 0;
	}
	return found ? i - 1 : -1;
}
