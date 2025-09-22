/* $OpenBSD: ts_rsp_sign.c,v 1.37 2025/07/31 02:02:35 tb Exp $ */
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

#include <sys/time.h>

#include <string.h>

#include <openssl/objects.h>
#include <openssl/pkcs7.h>
#include <openssl/ts.h>

#include "err_local.h"
#include "evp_local.h"
#include "ts_local.h"
#include "x509_local.h"

/* Private function declarations. */

static ASN1_INTEGER *def_serial_cb(struct TS_resp_ctx *, void *);
static int def_time_cb(struct TS_resp_ctx *, void *, time_t *sec, long *usec);
static int def_extension_cb(struct TS_resp_ctx *, X509_EXTENSION *, void *);

static void TS_RESP_CTX_init(TS_RESP_CTX *ctx);
static void TS_RESP_CTX_cleanup(TS_RESP_CTX *ctx);
static int TS_RESP_check_request(TS_RESP_CTX *ctx);
static ASN1_OBJECT *TS_RESP_get_policy(TS_RESP_CTX *ctx);
static TS_TST_INFO *TS_RESP_create_tst_info(TS_RESP_CTX *ctx,
    ASN1_OBJECT *policy);
static int TS_RESP_process_extensions(TS_RESP_CTX *ctx);
static int TS_RESP_sign(TS_RESP_CTX *ctx);

static ESS_SIGNING_CERT *ESS_SIGNING_CERT_new_init(X509 *signcert,
    STACK_OF(X509) *certs);
static ESS_CERT_ID *ESS_CERT_ID_new_init(X509 *cert, int issuer_needed);
static int TS_TST_INFO_content_new(PKCS7 *p7);
static int ESS_add_signing_cert(PKCS7_SIGNER_INFO *si, ESS_SIGNING_CERT *sc);

/* Default callbacks for response generation. */

static ASN1_INTEGER *
def_serial_cb(struct TS_resp_ctx *ctx, void *data)
{
	ASN1_INTEGER *serial;

	if ((serial = ASN1_INTEGER_new()) == NULL)
		goto err;
	if (!ASN1_INTEGER_set(serial, 1))
		goto err;

	return serial;

 err:
	ASN1_INTEGER_free(serial);
	TSerror(ERR_R_MALLOC_FAILURE);
	TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
	    "Error during serial number generation.");

	return NULL;
}

/* Use the gettimeofday function call. */
static int
def_time_cb(struct TS_resp_ctx *ctx, void *data, time_t *sec, long *usec)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0) {
		TSerror(TS_R_TIME_SYSCALL_ERROR);
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Time is not available.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_TIME_NOT_AVAILABLE);
		return 0;
	}
	/* Return time to caller. */
	*sec = tv.tv_sec;
	*usec = tv.tv_usec;

	return 1;
}

static int
def_extension_cb(struct TS_resp_ctx *ctx, X509_EXTENSION *ext, void *data)
{
	/* No extensions are processed here. */
	TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
	    "Unsupported extension.");
	TS_RESP_CTX_add_failure_info(ctx, TS_INFO_UNACCEPTED_EXTENSION);
	return 0;
}

void
TS_RESP_CTX_set_time_cb(TS_RESP_CTX *ctx, TS_time_cb cb, void *data)
{
	ctx->time_cb = cb;
	ctx->time_cb_data = data;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_time_cb);

/* TS_RESP_CTX management functions. */

TS_RESP_CTX *
TS_RESP_CTX_new(void)
{
	TS_RESP_CTX *ctx;

	if (!(ctx = calloc(1, sizeof(TS_RESP_CTX)))) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	/* Setting default callbacks. */
	ctx->serial_cb = def_serial_cb;
	ctx->time_cb = def_time_cb;
	ctx->extension_cb = def_extension_cb;

	return ctx;
}
LCRYPTO_ALIAS(TS_RESP_CTX_new);

void
TS_RESP_CTX_free(TS_RESP_CTX *ctx)
{
	if (!ctx)
		return;

	X509_free(ctx->signer_cert);
	EVP_PKEY_free(ctx->signer_key);
	sk_X509_pop_free(ctx->certs, X509_free);
	sk_ASN1_OBJECT_pop_free(ctx->policies, ASN1_OBJECT_free);
	ASN1_OBJECT_free(ctx->default_policy);
	sk_EVP_MD_free(ctx->mds);	/* No EVP_MD_free method exists. */
	ASN1_INTEGER_free(ctx->seconds);
	ASN1_INTEGER_free(ctx->millis);
	ASN1_INTEGER_free(ctx->micros);
	free(ctx);
}
LCRYPTO_ALIAS(TS_RESP_CTX_free);

int
TS_RESP_CTX_set_signer_cert(TS_RESP_CTX *ctx, X509 *signer)
{
	if (X509_check_purpose(signer, X509_PURPOSE_TIMESTAMP_SIGN, 0) != 1) {
		TSerror(TS_R_INVALID_SIGNER_CERTIFICATE_PURPOSE);
		return 0;
	}
	X509_free(ctx->signer_cert);
	ctx->signer_cert = signer;
	CRYPTO_add(&ctx->signer_cert->references, +1, CRYPTO_LOCK_X509);
	return 1;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_signer_cert);

int
TS_RESP_CTX_set_signer_key(TS_RESP_CTX *ctx, EVP_PKEY *key)
{
	EVP_PKEY_free(ctx->signer_key);
	ctx->signer_key = key;
	CRYPTO_add(&ctx->signer_key->references, +1, CRYPTO_LOCK_EVP_PKEY);

	return 1;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_signer_key);

int
TS_RESP_CTX_set_def_policy(TS_RESP_CTX *ctx, const ASN1_OBJECT *def_policy)
{
	if (ctx->default_policy)
		ASN1_OBJECT_free(ctx->default_policy);
	if (!(ctx->default_policy = OBJ_dup(def_policy)))
		goto err;
	return 1;

err:
	TSerror(ERR_R_MALLOC_FAILURE);
	return 0;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_def_policy);

int
TS_RESP_CTX_set_certs(TS_RESP_CTX *ctx, STACK_OF(X509) *certs)
{
	int i;

	if (ctx->certs) {
		sk_X509_pop_free(ctx->certs, X509_free);
		ctx->certs = NULL;
	}
	if (!certs)
		return 1;
	if (!(ctx->certs = sk_X509_dup(certs))) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	for (i = 0; i < sk_X509_num(ctx->certs); ++i) {
		X509 *cert = sk_X509_value(ctx->certs, i);
		CRYPTO_add(&cert->references, +1, CRYPTO_LOCK_X509);
	}

	return 1;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_certs);

int
TS_RESP_CTX_add_policy(TS_RESP_CTX *ctx, const ASN1_OBJECT *policy)
{
	ASN1_OBJECT *copy = NULL;

	/* Create new policy stack if necessary. */
	if (!ctx->policies && !(ctx->policies = sk_ASN1_OBJECT_new_null()))
		goto err;
	if (!(copy = OBJ_dup(policy)))
		goto err;
	if (!sk_ASN1_OBJECT_push(ctx->policies, copy))
		goto err;

	return 1;

err:
	TSerror(ERR_R_MALLOC_FAILURE);
	ASN1_OBJECT_free(copy);
	return 0;
}
LCRYPTO_ALIAS(TS_RESP_CTX_add_policy);

int
TS_RESP_CTX_add_md(TS_RESP_CTX *ctx, const EVP_MD *md)
{
	/* Create new md stack if necessary. */
	if (!ctx->mds && !(ctx->mds = sk_EVP_MD_new_null()))
		goto err;
	/* Add the shared md, no copy needed. */
	if (!sk_EVP_MD_push(ctx->mds, (EVP_MD *)md))
		goto err;

	return 1;

err:
	TSerror(ERR_R_MALLOC_FAILURE);
	return 0;
}
LCRYPTO_ALIAS(TS_RESP_CTX_add_md);

#define TS_RESP_CTX_accuracy_free(ctx)		\
	ASN1_INTEGER_free(ctx->seconds);	\
	ctx->seconds = NULL;			\
	ASN1_INTEGER_free(ctx->millis);		\
	ctx->millis = NULL;			\
	ASN1_INTEGER_free(ctx->micros);		\
	ctx->micros = NULL;

int
TS_RESP_CTX_set_accuracy(TS_RESP_CTX *ctx, int secs, int millis, int micros)
{
	TS_RESP_CTX_accuracy_free(ctx);
	if (secs && (!(ctx->seconds = ASN1_INTEGER_new()) ||
	    !ASN1_INTEGER_set(ctx->seconds, secs)))
		goto err;
	if (millis && (!(ctx->millis = ASN1_INTEGER_new()) ||
	    !ASN1_INTEGER_set(ctx->millis, millis)))
		goto err;
	if (micros && (!(ctx->micros = ASN1_INTEGER_new()) ||
	    !ASN1_INTEGER_set(ctx->micros, micros)))
		goto err;

	return 1;

err:
	TS_RESP_CTX_accuracy_free(ctx);
	TSerror(ERR_R_MALLOC_FAILURE);
	return 0;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_accuracy);

void
TS_RESP_CTX_add_flags(TS_RESP_CTX *ctx, int flags)
{
	ctx->flags |= flags;
}
LCRYPTO_ALIAS(TS_RESP_CTX_add_flags);

void
TS_RESP_CTX_set_serial_cb(TS_RESP_CTX *ctx, TS_serial_cb cb, void *data)
{
	ctx->serial_cb = cb;
	ctx->serial_cb_data = data;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_serial_cb);

void
TS_RESP_CTX_set_extension_cb(TS_RESP_CTX *ctx, TS_extension_cb cb, void *data)
{
	ctx->extension_cb = cb;
	ctx->extension_cb_data = data;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_extension_cb);

int
TS_RESP_CTX_set_status_info(TS_RESP_CTX *ctx, int status, const char *text)
{
	TS_STATUS_INFO *si = NULL;
	ASN1_UTF8STRING *utf8_text = NULL;
	int ret = 0;

	if (!(si = TS_STATUS_INFO_new()))
		goto err;
	if (!ASN1_INTEGER_set(si->status, status))
		goto err;
	if (text) {
		if (!(utf8_text = ASN1_UTF8STRING_new()) ||
		    !ASN1_STRING_set(utf8_text, text, strlen(text)))
			goto err;
		if (!si->text && !(si->text = sk_ASN1_UTF8STRING_new_null()))
			goto err;
		if (!sk_ASN1_UTF8STRING_push(si->text, utf8_text))
			goto err;
		utf8_text = NULL;	/* Ownership is lost. */
	}
	if (!TS_RESP_set_status_info(ctx->response, si))
		goto err;
	ret = 1;

err:
	if (!ret)
		TSerror(ERR_R_MALLOC_FAILURE);
	TS_STATUS_INFO_free(si);
	ASN1_UTF8STRING_free(utf8_text);
	return ret;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_status_info);

int
TS_RESP_CTX_set_status_info_cond(TS_RESP_CTX *ctx, int status, const char *text)
{
	int ret = 1;
	TS_STATUS_INFO *si = TS_RESP_get_status_info(ctx->response);

	if (ASN1_INTEGER_get(si->status) == TS_STATUS_GRANTED) {
		/* Status has not been set, set it now. */
		ret = TS_RESP_CTX_set_status_info(ctx, status, text);
	}
	return ret;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_status_info_cond);

int
TS_RESP_CTX_add_failure_info(TS_RESP_CTX *ctx, int failure)
{
	TS_STATUS_INFO *si = TS_RESP_get_status_info(ctx->response);

	if (!si->failure_info && !(si->failure_info = ASN1_BIT_STRING_new()))
		goto err;
	if (!ASN1_BIT_STRING_set_bit(si->failure_info, failure, 1))
		goto err;
	return 1;

err:
	TSerror(ERR_R_MALLOC_FAILURE);
	return 0;
}
LCRYPTO_ALIAS(TS_RESP_CTX_add_failure_info);

TS_REQ *
TS_RESP_CTX_get_request(TS_RESP_CTX *ctx)
{
	return ctx->request;
}
LCRYPTO_ALIAS(TS_RESP_CTX_get_request);

TS_TST_INFO *
TS_RESP_CTX_get_tst_info(TS_RESP_CTX *ctx)
{
	return ctx->tst_info;
}
LCRYPTO_ALIAS(TS_RESP_CTX_get_tst_info);

int
TS_RESP_CTX_set_clock_precision_digits(TS_RESP_CTX *ctx, unsigned precision)
{
	if (precision > 0)
		return 0;
	ctx->clock_precision_digits = precision;
	return 1;
}
LCRYPTO_ALIAS(TS_RESP_CTX_set_clock_precision_digits);

/* Main entry method of the response generation. */
TS_RESP *
TS_RESP_create_response(TS_RESP_CTX *ctx, BIO *req_bio)
{
	ASN1_OBJECT *policy;
	TS_RESP *response;
	int result = 0;

	TS_RESP_CTX_init(ctx);

	/* Creating the response object. */
	if (!(ctx->response = TS_RESP_new())) {
		TSerror(ERR_R_MALLOC_FAILURE);
		goto end;
	}

	/* Parsing DER request. */
	if (!(ctx->request = d2i_TS_REQ_bio(req_bio, NULL))) {
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Bad request format or "
		    "system error.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_DATA_FORMAT);
		goto end;
	}

	/* Setting default status info. */
	if (!TS_RESP_CTX_set_status_info(ctx, TS_STATUS_GRANTED, NULL))
		goto end;

	/* Checking the request format. */
	if (!TS_RESP_check_request(ctx))
		goto end;

	/* Checking acceptable policies. */
	if (!(policy = TS_RESP_get_policy(ctx)))
		goto end;

	/* Creating the TS_TST_INFO object. */
	if (!(ctx->tst_info = TS_RESP_create_tst_info(ctx, policy)))
		goto end;

	/* Processing extensions. */
	if (!TS_RESP_process_extensions(ctx))
		goto end;

	/* Generating the signature. */
	if (!TS_RESP_sign(ctx))
		goto end;

	/* Everything was successful. */
	result = 1;

end:
	if (!result) {
		TSerror(TS_R_RESPONSE_SETUP_ERROR);
		if (ctx->response != NULL) {
			if (TS_RESP_CTX_set_status_info_cond(ctx,
			    TS_STATUS_REJECTION, "Error during response "
			    "generation.") == 0) {
				TS_RESP_free(ctx->response);
				ctx->response = NULL;
			}
		}
	}
	response = ctx->response;
	ctx->response = NULL;	/* Ownership will be returned to caller. */
	TS_RESP_CTX_cleanup(ctx);
	return response;
}
LCRYPTO_ALIAS(TS_RESP_create_response);

/* Initializes the variable part of the context. */
static void
TS_RESP_CTX_init(TS_RESP_CTX *ctx)
{
	ctx->request = NULL;
	ctx->response = NULL;
	ctx->tst_info = NULL;
}

/* Cleans up the variable part of the context. */
static void
TS_RESP_CTX_cleanup(TS_RESP_CTX *ctx)
{
	TS_REQ_free(ctx->request);
	ctx->request = NULL;
	TS_RESP_free(ctx->response);
	ctx->response = NULL;
	TS_TST_INFO_free(ctx->tst_info);
	ctx->tst_info = NULL;
}

/* Checks the format and content of the request. */
static int
TS_RESP_check_request(TS_RESP_CTX *ctx)
{
	TS_REQ *request = ctx->request;
	TS_MSG_IMPRINT *msg_imprint;
	X509_ALGOR *md_alg;
	int md_alg_id;
	const ASN1_OCTET_STRING *digest;
	EVP_MD *md = NULL;
	int i;

	/* Checking request version. */
	if (TS_REQ_get_version(request) != 1) {
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Bad request version.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_REQUEST);
		return 0;
	}

	/* Checking message digest algorithm. */
	msg_imprint = TS_REQ_get_msg_imprint(request);
	md_alg = TS_MSG_IMPRINT_get_algo(msg_imprint);
	md_alg_id = OBJ_obj2nid(md_alg->algorithm);
	for (i = 0; !md && i < sk_EVP_MD_num(ctx->mds); ++i) {
		EVP_MD *current_md = sk_EVP_MD_value(ctx->mds, i);
		if (md_alg_id == EVP_MD_type(current_md))
			md = current_md;
	}
	if (!md) {
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Message digest algorithm is "
		    "not supported.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_ALG);
		return 0;
	}

	/* No message digest takes parameter. */
	if (md_alg->parameter &&
	    ASN1_TYPE_get(md_alg->parameter) != V_ASN1_NULL) {
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Superfluous message digest "
		    "parameter.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_ALG);
		return 0;
	}
	/* Checking message digest size. */
	digest = TS_MSG_IMPRINT_get_msg(msg_imprint);
	if (digest->length != EVP_MD_size(md)) {
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Bad message digest.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_DATA_FORMAT);
		return 0;
	}

	return 1;
}

/* Returns the TSA policy based on the requested and acceptable policies. */
static ASN1_OBJECT *
TS_RESP_get_policy(TS_RESP_CTX *ctx)
{
	ASN1_OBJECT *requested = TS_REQ_get_policy_id(ctx->request);
	ASN1_OBJECT *policy = NULL;
	int i;

	if (ctx->default_policy == NULL) {
		TSerror(TS_R_INVALID_NULL_POINTER);
		return NULL;
	}
	/* Return the default policy if none is requested or the default is
	   requested. */
	if (!requested || !OBJ_cmp(requested, ctx->default_policy))
		policy = ctx->default_policy;

	/* Check if the policy is acceptable. */
	for (i = 0; !policy && i < sk_ASN1_OBJECT_num(ctx->policies); ++i) {
		ASN1_OBJECT *current = sk_ASN1_OBJECT_value(ctx->policies, i);
		if (!OBJ_cmp(requested, current))
			policy = current;
	}
	if (!policy) {
		TSerror(TS_R_UNACCEPTABLE_POLICY);
		TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
		    "Requested policy is not "
		    "supported.");
		TS_RESP_CTX_add_failure_info(ctx, TS_INFO_UNACCEPTED_POLICY);
	}
	return policy;
}

/* Creates the TS_TST_INFO object based on the settings of the context. */
static TS_TST_INFO *
TS_RESP_create_tst_info(TS_RESP_CTX *ctx, ASN1_OBJECT *policy)
{
	int result = 0;
	TS_TST_INFO *tst_info = NULL;
	ASN1_INTEGER *serial = NULL;
	ASN1_GENERALIZEDTIME *asn1_time = NULL;
	time_t sec;
	long usec;
	TS_ACCURACY *accuracy = NULL;
	const ASN1_INTEGER *nonce;
	GENERAL_NAME *tsa_name = NULL;

	if (!(tst_info = TS_TST_INFO_new()))
		goto end;
	if (!TS_TST_INFO_set_version(tst_info, 1))
		goto end;
	if (!TS_TST_INFO_set_policy_id(tst_info, policy))
		goto end;
	if (!TS_TST_INFO_set_msg_imprint(tst_info, ctx->request->msg_imprint))
		goto end;
	if (!(serial = (*ctx->serial_cb)(ctx, ctx->serial_cb_data)) ||
	    !TS_TST_INFO_set_serial(tst_info, serial))
		goto end;
	if (!(*ctx->time_cb)(ctx, ctx->time_cb_data, &sec, &usec) ||
	    ((asn1_time = ASN1_GENERALIZEDTIME_set(NULL, sec)) == NULL) ||
	    !TS_TST_INFO_set_time(tst_info, asn1_time))
		goto end;

	/* Setting accuracy if needed. */
	if ((ctx->seconds || ctx->millis || ctx->micros) &&
	    !(accuracy = TS_ACCURACY_new()))
		goto end;

	if (ctx->seconds && !TS_ACCURACY_set_seconds(accuracy, ctx->seconds))
		goto end;
	if (ctx->millis && !TS_ACCURACY_set_millis(accuracy, ctx->millis))
		goto end;
	if (ctx->micros && !TS_ACCURACY_set_micros(accuracy, ctx->micros))
		goto end;
	if (accuracy && !TS_TST_INFO_set_accuracy(tst_info, accuracy))
		goto end;

	/* Setting ordering. */
	if ((ctx->flags & TS_ORDERING) &&
	    !TS_TST_INFO_set_ordering(tst_info, 1))
		goto end;

	/* Setting nonce if needed. */
	if ((nonce = TS_REQ_get_nonce(ctx->request)) != NULL &&
	    !TS_TST_INFO_set_nonce(tst_info, nonce))
		goto end;

	/* Setting TSA name to subject of signer certificate. */
	if (ctx->flags & TS_TSA_NAME) {
		if (!(tsa_name = GENERAL_NAME_new()))
			goto end;
		tsa_name->type = GEN_DIRNAME;
		tsa_name->d.dirn =
		    X509_NAME_dup(X509_get_subject_name(ctx->signer_cert));
		if (!tsa_name->d.dirn)
			goto end;
		if (!TS_TST_INFO_set_tsa(tst_info, tsa_name))
			goto end;
	}

	result = 1;

end:
	if (!result) {
		TS_TST_INFO_free(tst_info);
		tst_info = NULL;
		TSerror(TS_R_TST_INFO_SETUP_ERROR);
		TS_RESP_CTX_set_status_info_cond(ctx, TS_STATUS_REJECTION,
		    "Error during TSTInfo "
		    "generation.");
	}
	GENERAL_NAME_free(tsa_name);
	TS_ACCURACY_free(accuracy);
	ASN1_GENERALIZEDTIME_free(asn1_time);
	ASN1_INTEGER_free(serial);

	return tst_info;
}

/* Processing the extensions of the request. */
static int
TS_RESP_process_extensions(TS_RESP_CTX *ctx)
{
	STACK_OF(X509_EXTENSION) *exts = TS_REQ_get_exts(ctx->request);
	int i;
	int ok = 1;

	for (i = 0; ok && i < sk_X509_EXTENSION_num(exts); ++i) {
		X509_EXTENSION *ext = sk_X509_EXTENSION_value(exts, i);
		/* XXXXX The last argument was previously
		   (void *)ctx->extension_cb, but ISO C doesn't permit
		   converting a function pointer to void *.  For lack of
		   better information, I'm placing a NULL there instead.
		   The callback can pick its own address out from the ctx
		   anyway...
		*/
		ok = (*ctx->extension_cb)(ctx, ext, NULL);
	}

	return ok;
}

/* Functions for signing the TS_TST_INFO structure of the context. */
static int
TS_RESP_sign(TS_RESP_CTX *ctx)
{
	int ret = 0;
	PKCS7 *p7 = NULL;
	PKCS7_SIGNER_INFO *si;
	STACK_OF(X509) *certs;	/* Certificates to include in sc. */
	ESS_SIGNING_CERT *sc = NULL;
	ASN1_OBJECT *oid;
	BIO *p7bio = NULL;
	int i;

	/* Check if signcert and pkey match. */
	if (!X509_check_private_key(ctx->signer_cert, ctx->signer_key)) {
		TSerror(TS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
		goto err;
	}

	/* Create a new PKCS7 signed object. */
	if (!(p7 = PKCS7_new())) {
		TSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!PKCS7_set_type(p7, NID_pkcs7_signed))
		goto err;

	/* Force SignedData version to be 3 instead of the default 1. */
	if (!ASN1_INTEGER_set(p7->d.sign->version, 3))
		goto err;

	/* Add signer certificate and optional certificate chain. */
	if (TS_REQ_get_cert_req(ctx->request)) {
		PKCS7_add_certificate(p7, ctx->signer_cert);
		if (ctx->certs) {
			for (i = 0; i < sk_X509_num(ctx->certs); ++i) {
				X509 *cert = sk_X509_value(ctx->certs, i);
				PKCS7_add_certificate(p7, cert);
			}
		}
	}

	/* Add a new signer info. */
	if (!(si = PKCS7_add_signature(p7, ctx->signer_cert,
	    ctx->signer_key, EVP_sha1()))) {
		TSerror(TS_R_PKCS7_ADD_SIGNATURE_ERROR);
		goto err;
	}

	/* Add content type signed attribute to the signer info. */
	oid = OBJ_nid2obj(NID_id_smime_ct_TSTInfo);
	if (!PKCS7_add_signed_attribute(si, NID_pkcs9_contentType,
	    V_ASN1_OBJECT, oid)) {
		TSerror(TS_R_PKCS7_ADD_SIGNED_ATTR_ERROR);
		goto err;
	}

	/* Create the ESS SigningCertificate attribute which contains
	   the signer certificate id and optionally the certificate chain. */
	certs = ctx->flags & TS_ESS_CERT_ID_CHAIN ? ctx->certs : NULL;
	if (!(sc = ESS_SIGNING_CERT_new_init(ctx->signer_cert, certs)))
		goto err;

	/* Add SigningCertificate signed attribute to the signer info. */
	if (!ESS_add_signing_cert(si, sc)) {
		TSerror(TS_R_ESS_ADD_SIGNING_CERT_ERROR);
		goto err;
	}

	/* Add a new empty NID_id_smime_ct_TSTInfo encapsulated content. */
	if (!TS_TST_INFO_content_new(p7))
		goto err;

	/* Add the DER encoded tst_info to the PKCS7 structure. */
	if (!(p7bio = PKCS7_dataInit(p7, NULL))) {
		TSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/* Convert tst_info to DER. */
	if (!i2d_TS_TST_INFO_bio(p7bio, ctx->tst_info)) {
		TSerror(TS_R_TS_DATASIGN);
		goto err;
	}

	/* Create the signature and add it to the signer info. */
	if (!PKCS7_dataFinal(p7, p7bio)) {
		TSerror(TS_R_TS_DATASIGN);
		goto err;
	}

	/* Set new PKCS7 and TST_INFO objects. */
	TS_RESP_set_tst_info(ctx->response, p7, ctx->tst_info);
	p7 = NULL;		/* Ownership is lost. */
	ctx->tst_info = NULL;	/* Ownership is lost. */

	ret = 1;

err:
	if (!ret)
		TS_RESP_CTX_set_status_info_cond(ctx, TS_STATUS_REJECTION,
	    "Error during signature "
	    "generation.");
	BIO_free_all(p7bio);
	ESS_SIGNING_CERT_free(sc);
	PKCS7_free(p7);
	return ret;
}

static ESS_SIGNING_CERT *
ESS_SIGNING_CERT_new_init(X509 *signcert, STACK_OF(X509) *certs)
{
	ESS_CERT_ID *cid;
	ESS_SIGNING_CERT *sc = NULL;
	int i;

	/* Creating the ESS_CERT_ID stack. */
	if (!(sc = ESS_SIGNING_CERT_new()))
		goto err;
	if (!sc->cert_ids && !(sc->cert_ids = sk_ESS_CERT_ID_new_null()))
		goto err;

	/* Adding the signing certificate id. */
	if (!(cid = ESS_CERT_ID_new_init(signcert, 0)) ||
	    !sk_ESS_CERT_ID_push(sc->cert_ids, cid))
		goto err;
	/* Adding the certificate chain ids. */
	for (i = 0; i < sk_X509_num(certs); ++i) {
		X509 *cert = sk_X509_value(certs, i);
		if (!(cid = ESS_CERT_ID_new_init(cert, 1)) ||
		    !sk_ESS_CERT_ID_push(sc->cert_ids, cid))
			goto err;
	}

	return sc;

err:
	ESS_SIGNING_CERT_free(sc);
	TSerror(ERR_R_MALLOC_FAILURE);
	return NULL;
}

static ESS_CERT_ID *
ESS_CERT_ID_new_init(X509 *cert, int issuer_needed)
{
	ESS_CERT_ID *cid = NULL;
	GENERAL_NAME *name = NULL;
	unsigned char cert_hash[TS_HASH_LEN];

	/* Recompute SHA1 hash of certificate if necessary (side effect). */
	X509_check_purpose(cert, -1, 0);

	if (!(cid = ESS_CERT_ID_new()))
		goto err;

	if (!X509_digest(cert, TS_HASH_EVP, cert_hash, NULL))
		goto err;

	if (!ASN1_OCTET_STRING_set(cid->hash, cert_hash, sizeof(cert_hash)))
		goto err;

	/* Setting the issuer/serial if requested. */
	if (issuer_needed) {
		/* Creating issuer/serial structure. */
		if (!cid->issuer_serial &&
		    !(cid->issuer_serial = ESS_ISSUER_SERIAL_new()))
			goto err;
		/* Creating general name from the certificate issuer. */
		if (!(name = GENERAL_NAME_new()))
			goto err;
		name->type = GEN_DIRNAME;
		if ((name->d.dirn = X509_NAME_dup(X509_get_issuer_name(cert))) == NULL)
			goto err;
		if (!sk_GENERAL_NAME_push(cid->issuer_serial->issuer, name))
			goto err;
		name = NULL;	/* Ownership is lost. */
		/* Setting the serial number. */
		ASN1_INTEGER_free(cid->issuer_serial->serial);
		if (!(cid->issuer_serial->serial =
		    ASN1_INTEGER_dup(X509_get_serialNumber(cert))))
			goto err;
	}

	return cid;

err:
	GENERAL_NAME_free(name);
	ESS_CERT_ID_free(cid);
	TSerror(ERR_R_MALLOC_FAILURE);
	return NULL;
}

static int
TS_TST_INFO_content_new(PKCS7 *p7)
{
	PKCS7 *ret = NULL;
	ASN1_OCTET_STRING *octet_string = NULL;

	/* Create new encapsulated NID_id_smime_ct_TSTInfo content. */
	if (!(ret = PKCS7_new()))
		goto err;
	if (!(ret->d.other = ASN1_TYPE_new()))
		goto err;
	ret->type = OBJ_nid2obj(NID_id_smime_ct_TSTInfo);
	if (!(octet_string = ASN1_OCTET_STRING_new()))
		goto err;
	ASN1_TYPE_set(ret->d.other, V_ASN1_OCTET_STRING, octet_string);
	octet_string = NULL;

	/* Add encapsulated content to signed PKCS7 structure. */
	if (!PKCS7_set_content(p7, ret))
		goto err;

	return 1;

err:
	ASN1_OCTET_STRING_free(octet_string);
	PKCS7_free(ret);
	return 0;
}

static int
ESS_add_signing_cert(PKCS7_SIGNER_INFO *si, ESS_SIGNING_CERT *sc)
{
	ASN1_STRING *seq = NULL;
	unsigned char *data = NULL;
	int len = 0;
	int ret = 0;

	if ((len = i2d_ESS_SIGNING_CERT(sc, &data)) <= 0) {
		len = 0;
		goto err;
	}

	if ((seq = ASN1_STRING_new()) == NULL)
		goto err;

	ASN1_STRING_set0(seq, data, len);
	data = NULL;
	len = 0;

	if (!PKCS7_add_signed_attribute(si, NID_id_smime_aa_signingCertificate,
	    V_ASN1_SEQUENCE, seq))
		goto err;
	seq = NULL;

	ret = 1;

 err:
	ASN1_STRING_free(seq);
	freezero(data, len);

	return ret;
}
