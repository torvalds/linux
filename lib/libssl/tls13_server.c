/* $OpenBSD: tls13_server.c,v 1.109 2024/07/22 14:47:15 jsing Exp $ */
/*
 * Copyright (c) 2019, 2020 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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

#include <openssl/x509v3.h>

#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"
#include "tls13_handshake.h"
#include "tls13_internal.h"

int
tls13_server_init(struct tls13_ctx *ctx)
{
	SSL *s = ctx->ssl;

	if (!ssl_supported_tls_version_range(s, &ctx->hs->our_min_tls_version,
	    &ctx->hs->our_max_tls_version)) {
		SSLerror(s, SSL_R_NO_PROTOCOLS_AVAILABLE);
		return 0;
	}
	s->version = ctx->hs->our_max_tls_version;

	tls13_record_layer_set_retry_after_phh(ctx->rl,
	    (s->mode & SSL_MODE_AUTO_RETRY) != 0);

	if (!ssl_get_new_session(s, 0)) /* XXX */
		return 0;

	tls13_record_layer_set_legacy_version(ctx->rl, TLS1_VERSION);

	if (!tls1_transcript_init(s))
		return 0;

	arc4random_buf(s->s3->server_random, SSL3_RANDOM_SIZE);

	return 1;
}

int
tls13_server_accept(struct tls13_ctx *ctx)
{
	if (ctx->mode != TLS13_HS_SERVER)
		return TLS13_IO_FAILURE;

	return tls13_handshake_perform(ctx);
}

static int
tls13_client_hello_is_legacy(CBS *cbs)
{
	CBS extensions_block, extensions, extension_data, versions;
	uint16_t version, max_version = 0;
	uint16_t type;

	CBS_dup(cbs, &extensions_block);

	if (!CBS_get_u16_length_prefixed(&extensions_block, &extensions))
		return 1;

	while (CBS_len(&extensions) > 0) {
		if (!CBS_get_u16(&extensions, &type))
			return 1;
		if (!CBS_get_u16_length_prefixed(&extensions, &extension_data))
			return 1;

		if (type != TLSEXT_TYPE_supported_versions)
			continue;
		if (!CBS_get_u8_length_prefixed(&extension_data, &versions))
			return 1;
		while (CBS_len(&versions) > 0) {
			if (!CBS_get_u16(&versions, &version))
				return 1;
			if (version >= max_version)
				max_version = version;
		}
		if (CBS_len(&extension_data) != 0)
			return 1;
	}

	return (max_version < TLS1_3_VERSION);
}

int
tls13_client_hello_required_extensions(struct tls13_ctx *ctx)
{
	SSL *s = ctx->ssl;

	/*
	 * RFC 8446, section 9.2. If the ClientHello has supported_versions
	 * containing TLSv1.3, presence or absence of some extensions requires
	 * presence or absence of others.
	 */

	/*
	 * RFC 8446 section 4.2.9 - if we received a pre_shared_key, then we
	 * also need psk_key_exchange_modes. Otherwise, section 9.2 specifies
	 * that we need both signature_algorithms and supported_groups.
	 */
	if (tlsext_extension_seen(s, TLSEXT_TYPE_pre_shared_key)) {
		if (!tlsext_extension_seen(s,
		    TLSEXT_TYPE_psk_key_exchange_modes))
			return 0;
	} else {
		if (!tlsext_extension_seen(s, TLSEXT_TYPE_signature_algorithms))
			return 0;
		if (!tlsext_extension_seen(s, TLSEXT_TYPE_supported_groups))
			return 0;
	}

	/*
	 * supported_groups and key_share must either both be present or
	 * both be absent.
	 */
	if (tlsext_extension_seen(s, TLSEXT_TYPE_supported_groups) !=
	    tlsext_extension_seen(s, TLSEXT_TYPE_key_share))
		return 0;

	/*
	 * XXX - Require server_name from client? If so, we SHOULD enforce
	 * this here - RFC 8446, 9.2.
	 */

	return 1;
}

static const uint8_t tls13_compression_null_only[] = { 0 };

static int
tls13_client_hello_process(struct tls13_ctx *ctx, CBS *cbs)
{
	CBS cipher_suites, client_random, compression_methods, session_id;
	STACK_OF(SSL_CIPHER) *ciphers = NULL;
	const SSL_CIPHER *cipher;
	uint16_t legacy_version;
	int alert_desc;
	SSL *s = ctx->ssl;
	int ret = 0;

	if (!CBS_get_u16(cbs, &legacy_version))
		goto err;
	if (!CBS_get_bytes(cbs, &client_random, SSL3_RANDOM_SIZE))
		goto err;
	if (!CBS_get_u8_length_prefixed(cbs, &session_id))
		goto err;
	if (!CBS_get_u16_length_prefixed(cbs, &cipher_suites))
		goto err;
	if (!CBS_get_u8_length_prefixed(cbs, &compression_methods))
		goto err;

	if (tls13_client_hello_is_legacy(cbs) || s->version < TLS1_3_VERSION) {
		if (!CBS_skip(cbs, CBS_len(cbs)))
			goto err;
		return tls13_use_legacy_server(ctx);
	}
	ctx->hs->negotiated_tls_version = TLS1_3_VERSION;
	ctx->hs->peer_legacy_version = legacy_version;

	/* Ensure we send subsequent alerts with the correct record version. */
	tls13_record_layer_set_legacy_version(ctx->rl, TLS1_2_VERSION);

	/*
	 * Ensure that the client has not requested middlebox compatibility mode
	 * if it is prohibited from doing so.
	 */
	if (!ctx->middlebox_compat && CBS_len(&session_id) != 0) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}

	/* Add decoded values to the current ClientHello hash */
	if (!tls13_clienthello_hash_init(ctx)) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}
	if (!tls13_clienthello_hash_update_bytes(ctx, (void *)&legacy_version,
	    sizeof(legacy_version))) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}
	if (!tls13_clienthello_hash_update(ctx, &client_random)) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}
	if (!tls13_clienthello_hash_update(ctx, &session_id)) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}
	if (!tls13_clienthello_hash_update(ctx, &cipher_suites)) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}
	if (!tls13_clienthello_hash_update(ctx, &compression_methods)) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}

	if (!tlsext_server_parse(s, SSL_TLSEXT_MSG_CH, cbs, &alert_desc)) {
		ctx->alert = alert_desc;
		goto err;
	}

	/* Finalize first ClientHello hash, or validate against it */
	if (!ctx->hs->tls13.hrr) {
		if (!tls13_clienthello_hash_finalize(ctx)) {
			ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
			goto err;
		}
	} else {
		if (!tls13_clienthello_hash_validate(ctx)) {
			ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
			goto err;
		}
		tls13_clienthello_hash_clear(&ctx->hs->tls13);
	}

	if (!tls13_client_hello_required_extensions(ctx)) {
		ctx->alert = TLS13_ALERT_MISSING_EXTENSION;
		goto err;
	}

	/*
	 * If we got this far we have a supported versions extension that offers
	 * TLS 1.3 or later. This requires the legacy version be set to 0x0303.
	 */
	if (legacy_version != TLS1_2_VERSION) {
		ctx->alert = TLS13_ALERT_PROTOCOL_VERSION;
		goto err;
	}

	/*
	 * The legacy session identifier must either be zero length or a 32 byte
	 * value (in which case the client is requesting middlebox compatibility
	 * mode), as per RFC 8446 section 4.1.2. If it is valid, store the value
	 * so that we can echo it back to the client.
	 */
	if (CBS_len(&session_id) != 0 &&
	    CBS_len(&session_id) != sizeof(ctx->hs->tls13.legacy_session_id)) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}
	if (!CBS_write_bytes(&session_id, ctx->hs->tls13.legacy_session_id,
	    sizeof(ctx->hs->tls13.legacy_session_id),
	    &ctx->hs->tls13.legacy_session_id_len)) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		goto err;
	}

	/* Parse cipher suites list and select preferred cipher. */
	if ((ciphers = ssl_bytes_to_cipher_list(s, &cipher_suites)) == NULL) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}
	cipher = ssl3_choose_cipher(s, ciphers, SSL_get_ciphers(s));
	if (cipher == NULL) {
		tls13_set_errorx(ctx, TLS13_ERR_NO_SHARED_CIPHER, 0,
		    "no shared cipher found", NULL);
		ctx->alert = TLS13_ALERT_HANDSHAKE_FAILURE;
		goto err;
	}
	ctx->hs->cipher = cipher;

	sk_SSL_CIPHER_free(s->s3->hs.client_ciphers);
	s->s3->hs.client_ciphers = ciphers;
	ciphers = NULL;

	/* Ensure only the NULL compression method is advertised. */
	if (!CBS_mem_equal(&compression_methods, tls13_compression_null_only,
	    sizeof(tls13_compression_null_only))) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}

	ret = 1;

 err:
	sk_SSL_CIPHER_free(ciphers);

	return ret;
}

int
tls13_client_hello_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	SSL *s = ctx->ssl;

	if (!tls13_client_hello_process(ctx, cbs))
		goto err;

	/* See if we switched back to the legacy client method. */
	if (s->method->version < TLS1_3_VERSION)
		return 1;

	/*
	 * If a matching key share was provided, we do not need to send a
	 * HelloRetryRequest.
	 */
	/*
	 * XXX - ideally NEGOTIATED would only be added after record protection
	 * has been enabled. This would probably mean using either an
	 * INITIAL | WITHOUT_HRR state, or another intermediate state.
	 */
	if (ctx->hs->key_share != NULL)
		ctx->handshake_stage.hs_type |= NEGOTIATED | WITHOUT_HRR;

	tls13_record_layer_allow_ccs(ctx->rl, 1);

	return 1;

 err:
	return 0;
}

static int
tls13_server_hello_build(struct tls13_ctx *ctx, CBB *cbb, int hrr)
{
	uint16_t tlsext_msg_type = SSL_TLSEXT_MSG_SH;
	const uint8_t *server_random;
	CBB session_id;
	SSL *s = ctx->ssl;
	uint16_t cipher;

	cipher = SSL_CIPHER_get_value(ctx->hs->cipher);
	server_random = s->s3->server_random;

	if (hrr) {
		server_random = tls13_hello_retry_request_hash;
		tlsext_msg_type = SSL_TLSEXT_MSG_HRR;
	}

	if (!CBB_add_u16(cbb, TLS1_2_VERSION))
		goto err;
	if (!CBB_add_bytes(cbb, server_random, SSL3_RANDOM_SIZE))
		goto err;
	if (!CBB_add_u8_length_prefixed(cbb, &session_id))
		goto err;
	if (!CBB_add_bytes(&session_id, ctx->hs->tls13.legacy_session_id,
	    ctx->hs->tls13.legacy_session_id_len))
		goto err;
	if (!CBB_add_u16(cbb, cipher))
		goto err;
	if (!CBB_add_u8(cbb, 0))
		goto err;
	if (!tlsext_server_build(s, tlsext_msg_type, cbb))
		goto err;

	if (!CBB_flush(cbb))
		goto err;

	return 1;
 err:
	return 0;
}

static int
tls13_server_engage_record_protection(struct tls13_ctx *ctx)
{
	struct tls13_secrets *secrets;
	struct tls13_secret context;
	unsigned char buf[EVP_MAX_MD_SIZE];
	uint8_t *shared_key = NULL;
	size_t shared_key_len = 0;
	size_t hash_len;
	SSL *s = ctx->ssl;
	int ret = 0;

	if (!tls_key_share_derive(ctx->hs->key_share, &shared_key,
	    &shared_key_len))
		goto err;

	s->session->cipher_value = ctx->hs->cipher->value;

	if ((ctx->aead = tls13_cipher_aead(ctx->hs->cipher)) == NULL)
		goto err;
	if ((ctx->hash = tls13_cipher_hash(ctx->hs->cipher)) == NULL)
		goto err;

	if ((secrets = tls13_secrets_create(ctx->hash, 0)) == NULL)
		goto err;
	ctx->hs->tls13.secrets = secrets;

	/* XXX - pass in hash. */
	if (!tls1_transcript_hash_init(s))
		goto err;
	tls1_transcript_free(s);
	if (!tls1_transcript_hash_value(s, buf, sizeof(buf), &hash_len))
		goto err;
	context.data = buf;
	context.len = hash_len;

	/* Early secrets. */
	if (!tls13_derive_early_secrets(secrets, secrets->zeros.data,
	    secrets->zeros.len, &context))
		goto err;

	/* Handshake secrets. */
	if (!tls13_derive_handshake_secrets(ctx->hs->tls13.secrets, shared_key,
	    shared_key_len, &context))
		goto err;

	tls13_record_layer_set_aead(ctx->rl, ctx->aead);
	tls13_record_layer_set_hash(ctx->rl, ctx->hash);

	if (!tls13_record_layer_set_read_traffic_key(ctx->rl,
	    &secrets->client_handshake_traffic, ssl_encryption_handshake))
		goto err;
	if (!tls13_record_layer_set_write_traffic_key(ctx->rl,
	    &secrets->server_handshake_traffic, ssl_encryption_handshake))
		goto err;

	ctx->handshake_stage.hs_type |= NEGOTIATED;
	if (!(SSL_get_verify_mode(s) & SSL_VERIFY_PEER))
		ctx->handshake_stage.hs_type |= WITHOUT_CR;

	ret = 1;

 err:
	freezero(shared_key, shared_key_len);
	return ret;
}

int
tls13_server_hello_retry_request_send(struct tls13_ctx *ctx, CBB *cbb)
{
	int nid;

	ctx->hs->tls13.hrr = 1;

	if (!tls13_synthetic_handshake_message(ctx))
		return 0;

	if (ctx->hs->key_share != NULL)
		return 0;
	if (!tls1_get_supported_group(ctx->ssl, &nid))
		return 0;
	if (!tls1_ec_nid2group_id(nid, &ctx->hs->tls13.server_group))
		return 0;

	if (!tls13_server_hello_build(ctx, cbb, 1))
		return 0;

	return 1;
}

int
tls13_server_hello_retry_request_sent(struct tls13_ctx *ctx)
{
	/*
	 * If the client has requested middlebox compatibility mode,
	 * we MUST send a dummy CCS following our first handshake message.
	 * See RFC 8446 Appendix D.4.
	 */
	if (ctx->hs->tls13.legacy_session_id_len > 0)
		ctx->send_dummy_ccs_after = 1;

	return 1;
}

int
tls13_client_hello_retry_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	SSL *s = ctx->ssl;

	if (!tls13_client_hello_process(ctx, cbs))
		return 0;

	/* XXX - need further checks. */
	if (s->method->version < TLS1_3_VERSION)
		return 0;

	ctx->hs->tls13.hrr = 0;

	return 1;
}

static int
tls13_servername_process(struct tls13_ctx *ctx)
{
	uint8_t alert = TLS13_ALERT_INTERNAL_ERROR;

	if (!tls13_legacy_servername_process(ctx, &alert)) {
		ctx->alert = alert;
		return 0;
	}

	return 1;
}

int
tls13_server_hello_send(struct tls13_ctx *ctx, CBB *cbb)
{
	if (ctx->hs->key_share == NULL)
		return 0;
	if (!tls_key_share_generate(ctx->hs->key_share))
		return 0;
	if (!tls13_servername_process(ctx))
		return 0;

	ctx->hs->tls13.server_group = 0;

	if (!tls13_server_hello_build(ctx, cbb, 0))
		return 0;

	return 1;
}

int
tls13_server_hello_sent(struct tls13_ctx *ctx)
{
	/*
	 * If the client has requested middlebox compatibility mode,
	 * we MUST send a dummy CCS following our first handshake message.
	 * See RFC 8446 Appendix D.4.
	 */
	if ((ctx->handshake_stage.hs_type & WITHOUT_HRR) &&
	    ctx->hs->tls13.legacy_session_id_len > 0)
		ctx->send_dummy_ccs_after = 1;

	return tls13_server_engage_record_protection(ctx);
}

int
tls13_server_encrypted_extensions_send(struct tls13_ctx *ctx, CBB *cbb)
{
	if (!tlsext_server_build(ctx->ssl, SSL_TLSEXT_MSG_EE, cbb))
		goto err;

	return 1;
 err:
	return 0;
}

int
tls13_server_certificate_request_send(struct tls13_ctx *ctx, CBB *cbb)
{
	CBB certificate_request_context;

	if (!CBB_add_u8_length_prefixed(cbb, &certificate_request_context))
		goto err;
	if (!tlsext_server_build(ctx->ssl, SSL_TLSEXT_MSG_CR, cbb))
		goto err;

	if (!CBB_flush(cbb))
		goto err;

	return 1;
 err:
	return 0;
}

static int
tls13_server_check_certificate(struct tls13_ctx *ctx, SSL_CERT_PKEY *cpk,
    int *ok, const struct ssl_sigalg **out_sigalg)
{
	const struct ssl_sigalg *sigalg;
	SSL *s = ctx->ssl;

	*ok = 0;
	*out_sigalg = NULL;

	if (cpk->x509 == NULL || cpk->privatekey == NULL)
		goto done;

	/*
	 * The digitalSignature bit MUST be set if the Key Usage extension is
	 * present as per RFC 8446 section 4.4.2.2.
	 */
	if (!(X509_get_key_usage(cpk->x509) & X509v3_KU_DIGITAL_SIGNATURE))
		goto done;

	if ((sigalg = ssl_sigalg_select(s, cpk->privatekey)) == NULL)
		goto done;

	*ok = 1;
	*out_sigalg = sigalg;

 done:
	return 1;
}

static int
tls13_server_select_certificate(struct tls13_ctx *ctx, SSL_CERT_PKEY **out_cpk,
    const struct ssl_sigalg **out_sigalg)
{
	SSL *s = ctx->ssl;
	const struct ssl_sigalg *sigalg;
	SSL_CERT_PKEY *cpk;
	int cert_ok;

	*out_cpk = NULL;
	*out_sigalg = NULL;

	cpk = &s->cert->pkeys[SSL_PKEY_ECC];
	if (!tls13_server_check_certificate(ctx, cpk, &cert_ok, &sigalg))
		return 0;
	if (cert_ok)
		goto done;

	cpk = &s->cert->pkeys[SSL_PKEY_RSA];
	if (!tls13_server_check_certificate(ctx, cpk, &cert_ok, &sigalg))
		return 0;
	if (cert_ok)
		goto done;

	cpk = NULL;
	sigalg = NULL;

 done:
	*out_cpk = cpk;
	*out_sigalg = sigalg;

	return 1;
}

int
tls13_server_certificate_send(struct tls13_ctx *ctx, CBB *cbb)
{
	SSL *s = ctx->ssl;
	CBB cert_request_context, cert_list;
	const struct ssl_sigalg *sigalg;
	X509_STORE_CTX *xsc = NULL;
	STACK_OF(X509) *chain;
	SSL_CERT_PKEY *cpk;
	X509 *cert;
	int i, ret = 0;

	if (!tls13_server_select_certificate(ctx, &cpk, &sigalg))
		goto err;

	if (cpk == NULL) {
		/* A server must always provide a certificate. */
		ctx->alert = TLS13_ALERT_HANDSHAKE_FAILURE;
		tls13_set_errorx(ctx, TLS13_ERR_NO_CERTIFICATE, 0,
		    "no server certificate", NULL);
		goto err;
	}

	ctx->hs->tls13.cpk = cpk;
	ctx->hs->our_sigalg = sigalg;

	if ((chain = cpk->chain) == NULL)
		chain = s->ctx->extra_certs;

	if (chain == NULL && !(s->mode & SSL_MODE_NO_AUTO_CHAIN)) {
		if ((xsc = X509_STORE_CTX_new()) == NULL)
			goto err;
		if (!X509_STORE_CTX_init(xsc, s->ctx->cert_store, cpk->x509, NULL))
			goto err;
		X509_VERIFY_PARAM_set_flags(X509_STORE_CTX_get0_param(xsc),
		    X509_V_FLAG_LEGACY_VERIFY);
		X509_verify_cert(xsc);
		ERR_clear_error();
		chain = X509_STORE_CTX_get0_chain(xsc);
	}

	if (!CBB_add_u8_length_prefixed(cbb, &cert_request_context))
		goto err;
	if (!CBB_add_u24_length_prefixed(cbb, &cert_list))
		goto err;

	if (!tls13_cert_add(ctx, &cert_list, cpk->x509, tlsext_server_build))
		goto err;

	for (i = 0; i < sk_X509_num(chain); i++) {
		cert = sk_X509_value(chain, i);

		/*
		 * In the case of auto chain, the leaf certificate will be at
		 * the top of the chain - skip over it as we've already added
		 * it earlier.
		 */
		if (i == 0 && cert == cpk->x509)
			continue;

		/*
		 * XXX we don't send extensions with chain certs to avoid sending
		 * a leaf ocsp staple with the chain certs.  This needs to get
		 * fixed.
		 */
		if (!tls13_cert_add(ctx, &cert_list, cert, NULL))
			goto err;
	}

	if (!CBB_flush(cbb))
		goto err;

	ret = 1;

 err:
	X509_STORE_CTX_free(xsc);

	return ret;
}

int
tls13_server_certificate_verify_send(struct tls13_ctx *ctx, CBB *cbb)
{
	const struct ssl_sigalg *sigalg;
	uint8_t *sig = NULL, *sig_content = NULL;
	size_t sig_len, sig_content_len;
	EVP_MD_CTX *mdctx = NULL;
	EVP_PKEY_CTX *pctx;
	EVP_PKEY *pkey;
	const SSL_CERT_PKEY *cpk;
	CBB sig_cbb;
	int ret = 0;

	memset(&sig_cbb, 0, sizeof(sig_cbb));

	if ((cpk = ctx->hs->tls13.cpk) == NULL)
		goto err;
	if ((sigalg = ctx->hs->our_sigalg) == NULL)
		goto err;
	pkey = cpk->privatekey;

	if (!CBB_init(&sig_cbb, 0))
		goto err;
	if (!CBB_add_bytes(&sig_cbb, tls13_cert_verify_pad,
	    sizeof(tls13_cert_verify_pad)))
		goto err;
	if (!CBB_add_bytes(&sig_cbb, tls13_cert_server_verify_context,
	    strlen(tls13_cert_server_verify_context)))
		goto err;
	if (!CBB_add_u8(&sig_cbb, 0))
		goto err;
	if (!CBB_add_bytes(&sig_cbb, ctx->hs->tls13.transcript_hash,
	    ctx->hs->tls13.transcript_hash_len))
		goto err;
	if (!CBB_finish(&sig_cbb, &sig_content, &sig_content_len))
		goto err;

	if ((mdctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_DigestSignInit(mdctx, &pctx, sigalg->md(), NULL, pkey))
		goto err;
	if (sigalg->flags & SIGALG_FLAG_RSA_PSS) {
		if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING))
			goto err;
		if (!EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1))
			goto err;
	}
	if (!EVP_DigestSign(mdctx, NULL, &sig_len, sig_content, sig_content_len))
		goto err;
	if ((sig = calloc(1, sig_len)) == NULL)
		goto err;
	if (!EVP_DigestSign(mdctx, sig, &sig_len, sig_content, sig_content_len))
		goto err;

	if (!CBB_add_u16(cbb, sigalg->value))
		goto err;
	if (!CBB_add_u16_length_prefixed(cbb, &sig_cbb))
		goto err;
	if (!CBB_add_bytes(&sig_cbb, sig, sig_len))
		goto err;

	if (!CBB_flush(cbb))
		goto err;

	ret = 1;

 err:
	if (!ret && ctx->alert == 0)
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;

	CBB_cleanup(&sig_cbb);
	EVP_MD_CTX_free(mdctx);
	free(sig_content);
	free(sig);

	return ret;
}

int
tls13_server_finished_send(struct tls13_ctx *ctx, CBB *cbb)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret context = { .data = "", .len = 0 };
	struct tls13_secret finished_key = { .data = NULL, .len = 0 } ;
	uint8_t transcript_hash[EVP_MAX_MD_SIZE];
	size_t transcript_hash_len;
	uint8_t *verify_data;
	size_t verify_data_len;
	unsigned int hlen;
	HMAC_CTX *hmac_ctx = NULL;
	CBS cbs;
	int ret = 0;

	if (!tls13_secret_init(&finished_key, EVP_MD_size(ctx->hash)))
		goto err;

	if (!tls13_hkdf_expand_label(&finished_key, ctx->hash,
	    &secrets->server_handshake_traffic, "finished",
	    &context))
		goto err;

	if (!tls1_transcript_hash_value(ctx->ssl, transcript_hash,
	    sizeof(transcript_hash), &transcript_hash_len))
		goto err;

	if ((hmac_ctx = HMAC_CTX_new()) == NULL)
		goto err;
	if (!HMAC_Init_ex(hmac_ctx, finished_key.data, finished_key.len,
	    ctx->hash, NULL))
		goto err;
	if (!HMAC_Update(hmac_ctx, transcript_hash, transcript_hash_len))
		goto err;

	verify_data_len = HMAC_size(hmac_ctx);
	if (!CBB_add_space(cbb, &verify_data, verify_data_len))
		goto err;
	if (!HMAC_Final(hmac_ctx, verify_data, &hlen))
		goto err;
	if (hlen != verify_data_len)
		goto err;

	CBS_init(&cbs, verify_data, verify_data_len);
	if (!CBS_write_bytes(&cbs, ctx->hs->finished,
	    sizeof(ctx->hs->finished), &ctx->hs->finished_len))
		goto err;

	ret = 1;

 err:
	tls13_secret_cleanup(&finished_key);
	HMAC_CTX_free(hmac_ctx);

	return ret;
}

int
tls13_server_finished_sent(struct tls13_ctx *ctx)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret context = { .data = "", .len = 0 };

	/*
	 * Derive application traffic keys.
	 */
	context.data = ctx->hs->tls13.transcript_hash;
	context.len = ctx->hs->tls13.transcript_hash_len;

	if (!tls13_derive_application_secrets(secrets, &context))
		return 0;

	/*
	 * Any records following the server finished message must be encrypted
	 * using the server application traffic keys.
	 */
	return tls13_record_layer_set_write_traffic_key(ctx->rl,
	    &secrets->server_application_traffic, ssl_encryption_application);
}

int
tls13_client_certificate_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	CBS cert_request_context, cert_list, cert_data, cert_exts;
	struct stack_st_X509 *certs = NULL;
	SSL *s = ctx->ssl;
	X509 *cert = NULL;
	const uint8_t *p;
	int ret = 0;

	if (!CBS_get_u8_length_prefixed(cbs, &cert_request_context))
		goto err;
	if (CBS_len(&cert_request_context) != 0)
		goto err;
	if (!CBS_get_u24_length_prefixed(cbs, &cert_list))
		goto err;
	if (CBS_len(&cert_list) == 0) {
		if (!(s->verify_mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT))
			return 1;
		ctx->alert = TLS13_ALERT_CERTIFICATE_REQUIRED;
		tls13_set_errorx(ctx, TLS13_ERR_NO_PEER_CERTIFICATE, 0,
		    "peer did not provide a certificate", NULL);
		goto err;
	}

	if ((certs = sk_X509_new_null()) == NULL)
		goto err;
	while (CBS_len(&cert_list) > 0) {
		if (!CBS_get_u24_length_prefixed(&cert_list, &cert_data))
			goto err;
		if (!CBS_get_u16_length_prefixed(&cert_list, &cert_exts))
			goto err;

		p = CBS_data(&cert_data);
		if ((cert = d2i_X509(NULL, &p, CBS_len(&cert_data))) == NULL)
			goto err;
		if (p != CBS_data(&cert_data) + CBS_len(&cert_data))
			goto err;

		if (!sk_X509_push(certs, cert))
			goto err;

		cert = NULL;
	}

	/*
	 * At this stage we still have no proof of possession. As such, it would
	 * be preferable to keep the chain and verify once we have successfully
	 * processed the CertificateVerify message.
	 */
	if (ssl_verify_cert_chain(s, certs) <= 0) {
		ctx->alert = ssl_verify_alarm_type(s->verify_result);
		tls13_set_errorx(ctx, TLS13_ERR_VERIFY_FAILED, 0,
		    "failed to verify peer certificate", NULL);
		goto err;
	}
	s->session->verify_result = s->verify_result;
	ERR_clear_error();

	if (!tls_process_peer_certs(s, certs))
		goto err;

	ctx->handshake_stage.hs_type |= WITH_CCV;
	ret = 1;

 err:
	sk_X509_pop_free(certs, X509_free);
	X509_free(cert);

	return ret;
}

int
tls13_client_certificate_verify_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	const struct ssl_sigalg *sigalg;
	uint16_t signature_scheme;
	uint8_t *sig_content = NULL;
	size_t sig_content_len;
	EVP_MD_CTX *mdctx = NULL;
	EVP_PKEY_CTX *pctx;
	EVP_PKEY *pkey;
	X509 *cert;
	CBS signature;
	CBB cbb;
	int ret = 0;

	memset(&cbb, 0, sizeof(cbb));

	if (!CBS_get_u16(cbs, &signature_scheme))
		goto err;
	if (!CBS_get_u16_length_prefixed(cbs, &signature))
		goto err;

	if (!CBB_init(&cbb, 0))
		goto err;
	if (!CBB_add_bytes(&cbb, tls13_cert_verify_pad,
	    sizeof(tls13_cert_verify_pad)))
		goto err;
	if (!CBB_add_bytes(&cbb, tls13_cert_client_verify_context,
	    strlen(tls13_cert_client_verify_context)))
		goto err;
	if (!CBB_add_u8(&cbb, 0))
		goto err;
	if (!CBB_add_bytes(&cbb, ctx->hs->tls13.transcript_hash,
	    ctx->hs->tls13.transcript_hash_len))
		goto err;
	if (!CBB_finish(&cbb, &sig_content, &sig_content_len))
		goto err;

	if ((cert = ctx->ssl->session->peer_cert) == NULL)
		goto err;
	if ((pkey = X509_get0_pubkey(cert)) == NULL)
		goto err;
	if ((sigalg = ssl_sigalg_for_peer(ctx->ssl, pkey,
	    signature_scheme)) == NULL)
		goto err;
	ctx->hs->peer_sigalg = sigalg;

	if (CBS_len(&signature) > EVP_PKEY_size(pkey))
		goto err;

	if ((mdctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_DigestVerifyInit(mdctx, &pctx, sigalg->md(), NULL, pkey))
		goto err;
	if (sigalg->flags & SIGALG_FLAG_RSA_PSS) {
		if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING))
			goto err;
		if (!EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1))
			goto err;
	}
	if (EVP_DigestVerify(mdctx, CBS_data(&signature), CBS_len(&signature),
	    sig_content, sig_content_len) <= 0) {
		ctx->alert = TLS13_ALERT_DECRYPT_ERROR;
		goto err;
	}

	ret = 1;

 err:
	if (!ret && ctx->alert == 0)
		ctx->alert = TLS13_ALERT_DECODE_ERROR;

	CBB_cleanup(&cbb);
	EVP_MD_CTX_free(mdctx);
	free(sig_content);

	return ret;
}

int
tls13_client_end_of_early_data_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	return 0;
}

int
tls13_client_finished_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret context = { .data = "", .len = 0 };
	struct tls13_secret finished_key;
	uint8_t *verify_data = NULL;
	size_t verify_data_len;
	uint8_t key[EVP_MAX_MD_SIZE];
	HMAC_CTX *hmac_ctx = NULL;
	unsigned int hlen;
	int ret = 0;

	/*
	 * Verify client finished.
	 */
	finished_key.data = key;
	finished_key.len = EVP_MD_size(ctx->hash);

	if (!tls13_hkdf_expand_label(&finished_key, ctx->hash,
	    &secrets->client_handshake_traffic, "finished",
	    &context))
		goto err;

	if ((hmac_ctx = HMAC_CTX_new()) == NULL)
		goto err;
	if (!HMAC_Init_ex(hmac_ctx, finished_key.data, finished_key.len,
	    ctx->hash, NULL))
		goto err;
	if (!HMAC_Update(hmac_ctx, ctx->hs->tls13.transcript_hash,
	    ctx->hs->tls13.transcript_hash_len))
		goto err;
	verify_data_len = HMAC_size(hmac_ctx);
	if ((verify_data = calloc(1, verify_data_len)) == NULL)
		goto err;
	if (!HMAC_Final(hmac_ctx, verify_data, &hlen))
		goto err;
	if (hlen != verify_data_len)
		goto err;

	if (!CBS_mem_equal(cbs, verify_data, verify_data_len)) {
		ctx->alert = TLS13_ALERT_DECRYPT_ERROR;
		goto err;
	}

	if (!CBS_write_bytes(cbs, ctx->hs->peer_finished,
	    sizeof(ctx->hs->peer_finished),
	    &ctx->hs->peer_finished_len))
		goto err;

	if (!CBS_skip(cbs, verify_data_len))
		goto err;

	/*
	 * Any records following the client finished message must be encrypted
	 * using the client application traffic keys.
	 */
	if (!tls13_record_layer_set_read_traffic_key(ctx->rl,
	    &secrets->client_application_traffic, ssl_encryption_application))
		goto err;

	tls13_record_layer_allow_ccs(ctx->rl, 0);

	ret = 1;

 err:
	HMAC_CTX_free(hmac_ctx);
	free(verify_data);

	return ret;
}
