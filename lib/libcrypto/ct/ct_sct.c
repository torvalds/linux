/*	$OpenBSD: ct_sct.c,v 1.11 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Rob Stradling (rob@comodo.com), Stephen Henson (steve@openssl.org)
 * and Adam Eijdenberg (adam.eijdenberg@gmail.com) for the OpenSSL project 2016.
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
# error "CT disabled"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/ct.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "ct_local.h"
#include "err_local.h"

SCT *
SCT_new(void)
{
	SCT *sct = calloc(1, sizeof(*sct));

	if (sct == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	sct->entry_type = CT_LOG_ENTRY_TYPE_NOT_SET;
	sct->version = SCT_VERSION_NOT_SET;
	return sct;
}
LCRYPTO_ALIAS(SCT_new);

void
SCT_free(SCT *sct)
{
	if (sct == NULL)
		return;

	free(sct->log_id);
	free(sct->ext);
	free(sct->sig);
	free(sct->sct);
	free(sct);
}
LCRYPTO_ALIAS(SCT_free);

void
SCT_LIST_free(STACK_OF(SCT) *scts)
{
	sk_SCT_pop_free(scts, SCT_free);
}
LCRYPTO_ALIAS(SCT_LIST_free);

int
SCT_set_version(SCT *sct, sct_version_t version)
{
	if (version != SCT_VERSION_V1) {
		CTerror(CT_R_UNSUPPORTED_VERSION);
		return 0;
	}
	sct->version = version;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
	return 1;
}
LCRYPTO_ALIAS(SCT_set_version);

int
SCT_set_log_entry_type(SCT *sct, ct_log_entry_type_t entry_type)
{
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

	switch (entry_type) {
	case CT_LOG_ENTRY_TYPE_X509:
	case CT_LOG_ENTRY_TYPE_PRECERT:
		sct->entry_type = entry_type;
		return 1;
	case CT_LOG_ENTRY_TYPE_NOT_SET:
		break;
	}
	CTerror(CT_R_UNSUPPORTED_ENTRY_TYPE);
	return 0;
}
LCRYPTO_ALIAS(SCT_set_log_entry_type);

int
SCT_set0_log_id(SCT *sct, unsigned char *log_id, size_t log_id_len)
{
	if (sct->version == SCT_VERSION_V1 && log_id_len != CT_V1_HASHLEN) {
		CTerror(CT_R_INVALID_LOG_ID_LENGTH);
		return 0;
	}

	free(sct->log_id);
	sct->log_id = log_id;
	sct->log_id_len = log_id_len;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
	return 1;
}
LCRYPTO_ALIAS(SCT_set0_log_id);

int
SCT_set1_log_id(SCT *sct, const unsigned char *log_id, size_t log_id_len)
{
	if (sct->version == SCT_VERSION_V1 && log_id_len != CT_V1_HASHLEN) {
		CTerror(CT_R_INVALID_LOG_ID_LENGTH);
		return 0;
	}

	free(sct->log_id);
	sct->log_id = NULL;
	sct->log_id_len = 0;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

	if (log_id != NULL && log_id_len > 0) {
		sct->log_id = malloc(log_id_len);
		if (sct->log_id == NULL) {
			CTerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		memcpy(sct->log_id, log_id, log_id_len);
		sct->log_id_len = log_id_len;
	}
	return 1;
}
LCRYPTO_ALIAS(SCT_set1_log_id);


void
SCT_set_timestamp(SCT *sct, uint64_t timestamp)
{
	sct->timestamp = timestamp;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
}
LCRYPTO_ALIAS(SCT_set_timestamp);

int
SCT_set_signature_nid(SCT *sct, int nid)
{
	switch (nid) {
	case NID_sha256WithRSAEncryption:
		sct->hash_alg = 4; /* XXX */
		sct->sig_alg = 1; /* XXX */
		sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
		return 1;
	case NID_ecdsa_with_SHA256:
		sct->hash_alg = 4; /* XXX */
		sct->sig_alg = 3; /* XXX */
		sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
		return 1;
	default:
		CTerror(CT_R_UNRECOGNIZED_SIGNATURE_NID);
		return 0;
	}
}
LCRYPTO_ALIAS(SCT_set_signature_nid);

void
SCT_set0_extensions(SCT *sct, unsigned char *ext, size_t ext_len)
{
	free(sct->ext);
	sct->ext = ext;
	sct->ext_len = ext_len;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
}
LCRYPTO_ALIAS(SCT_set0_extensions);

int
SCT_set1_extensions(SCT *sct, const unsigned char *ext, size_t ext_len)
{
	free(sct->ext);
	sct->ext = NULL;
	sct->ext_len = 0;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

	if (ext != NULL && ext_len > 0) {
		sct->ext = malloc(ext_len);
		if (sct->ext == NULL) {
			CTerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		memcpy(sct->ext, ext, ext_len);
		sct->ext_len = ext_len;
	}
	return 1;
}
LCRYPTO_ALIAS(SCT_set1_extensions);

void
SCT_set0_signature(SCT *sct, unsigned char *sig, size_t sig_len)
{
	free(sct->sig);
	sct->sig = sig;
	sct->sig_len = sig_len;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
}
LCRYPTO_ALIAS(SCT_set0_signature);

int
SCT_set1_signature(SCT *sct, const unsigned char *sig, size_t sig_len)
{
	free(sct->sig);
	sct->sig = NULL;
	sct->sig_len = 0;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

	if (sig != NULL && sig_len > 0) {
		sct->sig = malloc(sig_len);
		if (sct->sig == NULL) {
			CTerror(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		memcpy(sct->sig, sig, sig_len);
		sct->sig_len = sig_len;
	}
	return 1;
}
LCRYPTO_ALIAS(SCT_set1_signature);

sct_version_t
SCT_get_version(const SCT *sct)
{
	return sct->version;
}
LCRYPTO_ALIAS(SCT_get_version);

ct_log_entry_type_t
SCT_get_log_entry_type(const SCT *sct)
{
	return sct->entry_type;
}
LCRYPTO_ALIAS(SCT_get_log_entry_type);

size_t
SCT_get0_log_id(const SCT *sct, unsigned char **log_id)
{
	*log_id = sct->log_id;
	return sct->log_id_len;
}
LCRYPTO_ALIAS(SCT_get0_log_id);

uint64_t
SCT_get_timestamp(const SCT *sct)
{
	return sct->timestamp;
}
LCRYPTO_ALIAS(SCT_get_timestamp);

int
SCT_get_signature_nid(const SCT *sct)
{
	if (sct->version == SCT_VERSION_V1) {
		/* XXX sigalg numbers */
		if (sct->hash_alg == 4) {
			switch (sct->sig_alg) {
			case 3:
				return NID_ecdsa_with_SHA256;
			case 1:
				return NID_sha256WithRSAEncryption;
			default:
				return NID_undef;
			}
		}
	}
	return NID_undef;
}
LCRYPTO_ALIAS(SCT_get_signature_nid);

size_t
SCT_get0_extensions(const SCT *sct, unsigned char **ext)
{
	*ext = sct->ext;
	return sct->ext_len;
}
LCRYPTO_ALIAS(SCT_get0_extensions);

size_t
SCT_get0_signature(const SCT *sct, unsigned char **sig)
{
	*sig = sct->sig;
	return sct->sig_len;
}
LCRYPTO_ALIAS(SCT_get0_signature);

int
SCT_is_complete(const SCT *sct)
{
	switch (sct->version) {
	case SCT_VERSION_NOT_SET:
		return 0;
	case SCT_VERSION_V1:
		return sct->log_id != NULL && SCT_signature_is_complete(sct);
	default:
		return sct->sct != NULL; /* Just need cached encoding */
	}
}

int
SCT_signature_is_complete(const SCT *sct)
{
	return SCT_get_signature_nid(sct) != NID_undef &&
	    sct->sig != NULL && sct->sig_len > 0;
}

sct_source_t
SCT_get_source(const SCT *sct)
{
	return sct->source;
}
LCRYPTO_ALIAS(SCT_get_source);

int
SCT_set_source(SCT *sct, sct_source_t source)
{
	sct->source = source;
	sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
	switch (source) {
	case SCT_SOURCE_TLS_EXTENSION:
	case SCT_SOURCE_OCSP_STAPLED_RESPONSE:
		return SCT_set_log_entry_type(sct, CT_LOG_ENTRY_TYPE_X509);
	case SCT_SOURCE_X509V3_EXTENSION:
		return SCT_set_log_entry_type(sct, CT_LOG_ENTRY_TYPE_PRECERT);
	case SCT_SOURCE_UNKNOWN:
		break;
	}
	/* if we aren't sure, leave the log entry type alone */
	return 1;
}
LCRYPTO_ALIAS(SCT_set_source);

sct_validation_status_t
SCT_get_validation_status(const SCT *sct)
{
	return sct->validation_status;
}
LCRYPTO_ALIAS(SCT_get_validation_status);

int
SCT_validate(SCT *sct, const CT_POLICY_EVAL_CTX *ctx)
{
	int is_sct_valid = -1;
	SCT_CTX *sctx = NULL;
	X509_PUBKEY *pub = NULL, *log_pkey = NULL;
	const CTLOG *log;

	/*
	 * With an unrecognized SCT version we don't know what such an SCT means,
	 * let alone validate one.  So we return validation failure (0).
	 */
	if (sct->version != SCT_VERSION_V1) {
		sct->validation_status = SCT_VALIDATION_STATUS_UNKNOWN_VERSION;
		return 0;
	}

	log = CTLOG_STORE_get0_log_by_id(ctx->log_store, sct->log_id,
	    sct->log_id_len);

	/* Similarly, an SCT from an unknown log also cannot be validated. */
	if (log == NULL) {
		sct->validation_status = SCT_VALIDATION_STATUS_UNKNOWN_LOG;
		return 0;
	}

	sctx = SCT_CTX_new();
	if (sctx == NULL)
		goto err;

	if (X509_PUBKEY_set(&log_pkey, CTLOG_get0_public_key(log)) != 1)
		goto err;
	if (SCT_CTX_set1_pubkey(sctx, log_pkey) != 1)
		goto err;

	if (SCT_get_log_entry_type(sct) == CT_LOG_ENTRY_TYPE_PRECERT) {
		EVP_PKEY *issuer_pkey;

		if (ctx->issuer == NULL) {
			sct->validation_status = SCT_VALIDATION_STATUS_UNVERIFIED;
			goto end;
		}

		if ((issuer_pkey = X509_get0_pubkey(ctx->issuer)) == NULL)
			goto err;

		if (X509_PUBKEY_set(&pub, issuer_pkey) != 1)
			goto err;
		if (SCT_CTX_set1_issuer_pubkey(sctx, pub) != 1)
			goto err;
	}

	SCT_CTX_set_time(sctx, ctx->epoch_time_in_ms);

	/*
	 * XXX: Potential for optimization.  This repeats some idempotent heavy
	 * lifting on the certificate for each candidate SCT, and appears to not
	 * use any information in the SCT itself, only the certificate is
	 * processed.  So it may make more sense to to do this just once, perhaps
	 * associated with the shared (by all SCTs) policy eval ctx.
	 *
	 * XXX: Failure here is global (SCT independent) and represents either an
	 * issue with the certificate (e.g. duplicate extensions) or an out of
	 * memory condition.  When the certificate is incompatible with CT, we just
	 * mark the SCTs invalid, rather than report a failure to determine the
	 * validation status.  That way, callbacks that want to do "soft" SCT
	 * processing will not abort handshakes with false positive internal
	 * errors.  Since the function does not distinguish between certificate
	 * issues (peer's fault) and internal problems (out fault) the safe thing
	 * to do is to report a validation failure and let the callback or
	 * application decide what to do.
	 */
	if (SCT_CTX_set1_cert(sctx, ctx->cert, NULL) != 1)
		sct->validation_status = SCT_VALIDATION_STATUS_UNVERIFIED;
	else
		sct->validation_status = SCT_CTX_verify(sctx, sct) == 1 ?
		    SCT_VALIDATION_STATUS_VALID : SCT_VALIDATION_STATUS_INVALID;

 end:
	is_sct_valid = sct->validation_status == SCT_VALIDATION_STATUS_VALID;
 err:
	X509_PUBKEY_free(pub);
	X509_PUBKEY_free(log_pkey);
	SCT_CTX_free(sctx);

	return is_sct_valid;
}
LCRYPTO_ALIAS(SCT_validate);

int
SCT_LIST_validate(const STACK_OF(SCT) *scts, CT_POLICY_EVAL_CTX *ctx)
{
	int are_scts_valid = 1;
	int sct_count = scts != NULL ? sk_SCT_num(scts) : 0;
	int i;

	for (i = 0; i < sct_count; ++i) {
		int is_sct_valid = -1;
		SCT *sct = sk_SCT_value(scts, i);

		if (sct == NULL)
			continue;

		is_sct_valid = SCT_validate(sct, ctx);
		if (is_sct_valid < 0)
			return is_sct_valid;
		are_scts_valid &= is_sct_valid;
	}

	return are_scts_valid;
}
LCRYPTO_ALIAS(SCT_LIST_validate);
