/*	$OpenBSD: tls_ocsp.c,v 1.26 2024/03/26 06:24:52 joshua Exp $ */
/*
 * Copyright (c) 2015 Marko Kreen <markokr@gmail.com>
 * Copyright (c) 2016 Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string.h>

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/posix_time.h>
#include <openssl/x509.h>

#include <tls.h>
#include "tls_internal.h"

#define MAXAGE_SEC (14*24*60*60)
#define JITTER_SEC (60)

/*
 * State for request.
 */

static struct tls_ocsp *
tls_ocsp_new(void)
{
	return (calloc(1, sizeof(struct tls_ocsp)));
}

void
tls_ocsp_free(struct tls_ocsp *ocsp)
{
	if (ocsp == NULL)
		return;

	X509_free(ocsp->main_cert);
	free(ocsp->ocsp_result);
	free(ocsp->ocsp_url);

	free(ocsp);
}

static int
tls_ocsp_asn1_parse_time(struct tls *ctx, ASN1_GENERALIZEDTIME *gt, time_t *gt_time)
{
	struct tm tm;

	if (gt == NULL)
		return -1;
	/* RFC 6960 specifies that all times in OCSP must be GENERALIZEDTIME */
	if (!ASN1_GENERALIZEDTIME_check(gt))
		return -1;
	if (!ASN1_TIME_to_tm(gt, &tm))
		return -1;
	if (!OPENSSL_timegm(&tm, gt_time))
		return -1;
	return 0;
}

static int
tls_ocsp_fill_info(struct tls *ctx, int response_status, int cert_status,
    int crl_reason, ASN1_GENERALIZEDTIME *revtime,
    ASN1_GENERALIZEDTIME *thisupd, ASN1_GENERALIZEDTIME *nextupd)
{
	struct tls_ocsp_result *info = NULL;

	free(ctx->ocsp->ocsp_result);
	ctx->ocsp->ocsp_result = NULL;

	if ((info = calloc(1, sizeof (struct tls_ocsp_result))) == NULL) {
		tls_set_error(ctx, TLS_ERROR_OUT_OF_MEMORY, "out of memory");
		return -1;
	}
	info->response_status = response_status;
	info->cert_status = cert_status;
	info->crl_reason = crl_reason;
	if (info->response_status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		info->result_msg =
		    OCSP_response_status_str(info->response_status);
	} else if (info->cert_status != V_OCSP_CERTSTATUS_REVOKED) {
		info->result_msg = OCSP_cert_status_str(info->cert_status);
	} else {
		info->result_msg = OCSP_crl_reason_str(info->crl_reason);
	}
	info->revocation_time = info->this_update = info->next_update = -1;
	if (revtime != NULL &&
	    tls_ocsp_asn1_parse_time(ctx, revtime, &info->revocation_time) != 0) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "unable to parse revocation time in OCSP reply");
		goto err;
	}
	if (thisupd != NULL &&
	    tls_ocsp_asn1_parse_time(ctx, thisupd, &info->this_update) != 0) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "unable to parse this update time in OCSP reply");
		goto err;
	}
	if (nextupd != NULL &&
	    tls_ocsp_asn1_parse_time(ctx, nextupd, &info->next_update) != 0) {
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "unable to parse next update time in OCSP reply");
		goto err;
	}
	ctx->ocsp->ocsp_result = info;
	return 0;

 err:
	free(info);
	return -1;
}

static OCSP_CERTID *
tls_ocsp_get_certid(X509 *main_cert, STACK_OF(X509) *extra_certs,
    SSL_CTX *ssl_ctx)
{
	X509_NAME *issuer_name;
	X509 *issuer;
	X509_STORE_CTX *storectx = NULL;
	X509_OBJECT *obj = NULL;
	OCSP_CERTID *cid = NULL;
	X509_STORE *store;

	if ((issuer_name = X509_get_issuer_name(main_cert)) == NULL)
		goto out;

	if (extra_certs != NULL) {
		issuer = X509_find_by_subject(extra_certs, issuer_name);
		if (issuer != NULL) {
			cid = OCSP_cert_to_id(NULL, main_cert, issuer);
			goto out;
		}
	}

	if ((store = SSL_CTX_get_cert_store(ssl_ctx)) == NULL)
		goto out;
	if ((storectx = X509_STORE_CTX_new()) == NULL)
		goto out;
	if (X509_STORE_CTX_init(storectx, store, main_cert, extra_certs) != 1)
		goto out;
	if ((obj = X509_STORE_CTX_get_obj_by_subject(storectx, X509_LU_X509,
	    issuer_name)) == NULL)
		goto out;

	cid = OCSP_cert_to_id(NULL, main_cert, X509_OBJECT_get0_X509(obj));

 out:
	X509_STORE_CTX_free(storectx);
	X509_OBJECT_free(obj);

	return cid;
}

struct tls_ocsp *
tls_ocsp_setup_from_peer(struct tls *ctx)
{
	struct tls_ocsp *ocsp = NULL;
	STACK_OF(OPENSSL_STRING) *ocsp_urls = NULL;

	if ((ocsp = tls_ocsp_new()) == NULL)
		goto err;

	/* steal state from ctx struct */
	ocsp->main_cert = SSL_get_peer_certificate(ctx->ssl_conn);
	ocsp->extra_certs = SSL_get_peer_cert_chain(ctx->ssl_conn);
	if (ocsp->main_cert == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "no peer certificate for OCSP");
		goto err;
	}

	ocsp_urls = X509_get1_ocsp(ocsp->main_cert);
	if (ocsp_urls == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "no OCSP URLs in peer certificate");
		goto err;
	}

	ocsp->ocsp_url = strdup(sk_OPENSSL_STRING_value(ocsp_urls, 0));
	if (ocsp->ocsp_url == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_OUT_OF_MEMORY, "out of memory");
		goto err;
	}

	X509_email_free(ocsp_urls);
	return ocsp;

 err:
	tls_ocsp_free(ocsp);
	X509_email_free(ocsp_urls);
	return NULL;
}

static int
tls_ocsp_verify_response(struct tls *ctx, OCSP_RESPONSE *resp)
{
	OCSP_BASICRESP *br = NULL;
	ASN1_GENERALIZEDTIME *revtime = NULL, *thisupd = NULL, *nextupd = NULL;
	OCSP_CERTID *cid = NULL;
	STACK_OF(X509) *combined = NULL;
	int response_status=0, cert_status=0, crl_reason=0;
	int ret = -1;
	unsigned long flags;

	if ((br = OCSP_response_get1_basic(resp)) == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "cannot load ocsp reply");
		goto err;
	}

	/*
	 * Skip validation of 'extra_certs' as this should be done
	 * already as part of main handshake.
	 */
	flags = OCSP_TRUSTOTHER;

	/* now verify */
	if (OCSP_basic_verify(br, ctx->ocsp->extra_certs,
		SSL_CTX_get_cert_store(ctx->ssl_ctx), flags) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN, "ocsp verify failed");
		goto err;
	}

	/* signature OK, look inside */
	response_status = OCSP_response_status(resp);
	if (response_status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ocsp verify failed: response - %s",
		    OCSP_response_status_str(response_status));
		goto err;
	}

	cid = tls_ocsp_get_certid(ctx->ocsp->main_cert,
	    ctx->ocsp->extra_certs, ctx->ssl_ctx);
	if (cid == NULL) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ocsp verify failed: no issuer cert");
		goto err;
	}

	if (OCSP_resp_find_status(br, cid, &cert_status, &crl_reason,
	    &revtime, &thisupd, &nextupd) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ocsp verify failed: no result for cert");
		goto err;
	}

	if (OCSP_check_validity(thisupd, nextupd, JITTER_SEC,
	    MAXAGE_SEC) != 1) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ocsp verify failed: ocsp response not current");
		goto err;
	}

	if (tls_ocsp_fill_info(ctx, response_status, cert_status,
	    crl_reason, revtime, thisupd, nextupd) != 0)
		goto err;

	/* finally can look at status */
	if (cert_status != V_OCSP_CERTSTATUS_GOOD && cert_status !=
	    V_OCSP_CERTSTATUS_UNKNOWN) {
		tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
		    "ocsp verify failed: revoked cert - %s",
		    OCSP_crl_reason_str(crl_reason));
		goto err;
	}
	ret = 0;

 err:
	sk_X509_free(combined);
	OCSP_CERTID_free(cid);
	OCSP_BASICRESP_free(br);
	return ret;
}

/*
 * Process a raw OCSP response from an OCSP server request.
 * OCSP details can then be retrieved with tls_peer_ocsp_* functions.
 * returns 0 if certificate ok, -1 otherwise.
 */
static int
tls_ocsp_process_response_internal(struct tls *ctx, const unsigned char *response,
    size_t size)
{
	int ret;
	OCSP_RESPONSE *resp;

	resp = d2i_OCSP_RESPONSE(NULL, &response, size);
	if (resp == NULL) {
		tls_ocsp_free(ctx->ocsp);
		ctx->ocsp = NULL;
		tls_set_error(ctx, TLS_ERROR_UNKNOWN,
		    "unable to parse OCSP response");
		return -1;
	}
	ret = tls_ocsp_verify_response(ctx, resp);
	OCSP_RESPONSE_free(resp);
	return ret;
}

/* TLS handshake verification callback for stapled requests */
int
tls_ocsp_verify_cb(SSL *ssl, void *arg)
{
	const unsigned char *raw = NULL;
	int size, res = -1;
	struct tls *ctx;

	if ((ctx = SSL_get_app_data(ssl)) == NULL)
		return -1;

	size = SSL_get_tlsext_status_ocsp_resp(ssl, &raw);
	if (size <= 0) {
		if (ctx->config->ocsp_require_stapling) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "no stapled OCSP response provided");
			return 0;
		}
		return 1;
	}

	tls_ocsp_free(ctx->ocsp);
	if ((ctx->ocsp = tls_ocsp_setup_from_peer(ctx)) == NULL)
		return 0;

	if (ctx->config->verify_cert == 0 || ctx->config->verify_time == 0)
		return 1;

	res = tls_ocsp_process_response_internal(ctx, raw, size);

	return (res == 0) ? 1 : 0;
}


/* Staple the OCSP information in ctx->ocsp to the server handshake. */
int
tls_ocsp_stapling_cb(SSL *ssl, void *arg)
{
	int ret = SSL_TLSEXT_ERR_ALERT_FATAL;
	unsigned char *ocsp_staple = NULL;
	struct tls *ctx;

	if ((ctx = SSL_get_app_data(ssl)) == NULL)
		goto err;

	if (ctx->keypair == NULL || ctx->keypair->ocsp_staple == NULL ||
	    ctx->keypair->ocsp_staple_len == 0)
		return SSL_TLSEXT_ERR_NOACK;

	if ((ocsp_staple = malloc(ctx->keypair->ocsp_staple_len)) == NULL)
		goto err;

	memcpy(ocsp_staple, ctx->keypair->ocsp_staple,
	    ctx->keypair->ocsp_staple_len);

	if (SSL_set_tlsext_status_ocsp_resp(ctx->ssl_conn, ocsp_staple,
	    ctx->keypair->ocsp_staple_len) != 1)
		goto err;

	ret = SSL_TLSEXT_ERR_OK;
 err:
	if (ret != SSL_TLSEXT_ERR_OK)
		free(ocsp_staple);

	return ret;
}

/*
 * Public API
 */

/* Retrieve OCSP URL from peer certificate, if present. */
const char *
tls_peer_ocsp_url(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return NULL;
	return ctx->ocsp->ocsp_url;
}

const char *
tls_peer_ocsp_result(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return NULL;
	if (ctx->ocsp->ocsp_result == NULL)
		return NULL;
	return ctx->ocsp->ocsp_result->result_msg;
}

int
tls_peer_ocsp_response_status(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return -1;
	if (ctx->ocsp->ocsp_result == NULL)
		return -1;
	return ctx->ocsp->ocsp_result->response_status;
}

int
tls_peer_ocsp_cert_status(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return -1;
	if (ctx->ocsp->ocsp_result == NULL)
		return -1;
	return ctx->ocsp->ocsp_result->cert_status;
}

int
tls_peer_ocsp_crl_reason(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return -1;
	if (ctx->ocsp->ocsp_result == NULL)
		return -1;
	return ctx->ocsp->ocsp_result->crl_reason;
}

time_t
tls_peer_ocsp_this_update(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return -1;
	if (ctx->ocsp->ocsp_result == NULL)
		return -1;
	return ctx->ocsp->ocsp_result->this_update;
}

time_t
tls_peer_ocsp_next_update(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return -1;
	if (ctx->ocsp->ocsp_result == NULL)
		return -1;
	return ctx->ocsp->ocsp_result->next_update;
}

time_t
tls_peer_ocsp_revocation_time(struct tls *ctx)
{
	if (ctx->ocsp == NULL)
		return -1;
	if (ctx->ocsp->ocsp_result == NULL)
		return -1;
	return ctx->ocsp->ocsp_result->revocation_time;
}

int
tls_ocsp_process_response(struct tls *ctx, const unsigned char *response,
    size_t size)
{
	if ((ctx->state & TLS_HANDSHAKE_COMPLETE) == 0)
		return -1;
	return tls_ocsp_process_response_internal(ctx, response, size);
}
