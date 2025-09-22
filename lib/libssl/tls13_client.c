/* $OpenBSD: tls13_client.c,v 1.104 2024/07/22 14:47:15 jsing Exp $ */
/*
 * Copyright (c) 2018, 2019 Joel Sing <jsing@openbsd.org>
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

#include <openssl/ssl3.h>

#include "bytestring.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"
#include "tls13_handshake.h"
#include "tls13_internal.h"

int
tls13_client_init(struct tls13_ctx *ctx)
{
	const uint16_t *groups;
	size_t groups_len;
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

	if (!tls1_transcript_init(s))
		return 0;

	/* Generate a key share using our preferred group. */
	tls1_get_group_list(s, 0, &groups, &groups_len);
	if (groups_len < 1)
		return 0;
	if ((ctx->hs->key_share = tls_key_share_new(groups[0])) == NULL)
		return 0;
	if (!tls_key_share_generate(ctx->hs->key_share))
		return 0;

	arc4random_buf(s->s3->client_random, SSL3_RANDOM_SIZE);

	/*
	 * The legacy session identifier should either be set to an
	 * unpredictable 32-byte value or zero length... a non-zero length
	 * legacy session identifier triggers compatibility mode (see RFC 8446
	 * Appendix D.4). In the pre-TLSv1.3 case a zero length value is used.
	 */
	if (ctx->middlebox_compat &&
	    ctx->hs->our_max_tls_version >= TLS1_3_VERSION) {
		arc4random_buf(ctx->hs->tls13.legacy_session_id,
		    sizeof(ctx->hs->tls13.legacy_session_id));
		ctx->hs->tls13.legacy_session_id_len =
		    sizeof(ctx->hs->tls13.legacy_session_id);
	}

	return 1;
}

int
tls13_client_connect(struct tls13_ctx *ctx)
{
	if (ctx->mode != TLS13_HS_CLIENT)
		return TLS13_IO_FAILURE;

	return tls13_handshake_perform(ctx);
}

static int
tls13_client_hello_build(struct tls13_ctx *ctx, CBB *cbb)
{
	CBB cipher_suites, compression_methods, session_id;
	uint16_t client_version;
	SSL *s = ctx->ssl;

	/* Legacy client version is capped at TLS 1.2. */
	if (!ssl_max_legacy_version(s, &client_version))
		goto err;

	if (!CBB_add_u16(cbb, client_version))
		goto err;
	if (!CBB_add_bytes(cbb, s->s3->client_random, SSL3_RANDOM_SIZE))
		goto err;

	if (!CBB_add_u8_length_prefixed(cbb, &session_id))
		goto err;
	if (!CBB_add_bytes(&session_id, ctx->hs->tls13.legacy_session_id,
	    ctx->hs->tls13.legacy_session_id_len))
		goto err;

	if (!CBB_add_u16_length_prefixed(cbb, &cipher_suites))
		goto err;
	if (!ssl_cipher_list_to_bytes(s, SSL_get_ciphers(s), &cipher_suites)) {
		SSLerror(s, SSL_R_NO_CIPHERS_AVAILABLE);
		goto err;
	}

	if (!CBB_add_u8_length_prefixed(cbb, &compression_methods))
		goto err;
	if (!CBB_add_u8(&compression_methods, 0))
		goto err;

	if (!tlsext_client_build(s, SSL_TLSEXT_MSG_CH, cbb))
		goto err;

	if (!CBB_flush(cbb))
		goto err;

	return 1;

 err:
	return 0;
}

int
tls13_client_hello_send(struct tls13_ctx *ctx, CBB *cbb)
{
	if (ctx->hs->our_min_tls_version < TLS1_2_VERSION)
		tls13_record_layer_set_legacy_version(ctx->rl, TLS1_VERSION);

	/* We may receive a pre-TLSv1.3 alert in response to the client hello. */
	tls13_record_layer_allow_legacy_alerts(ctx->rl, 1);

	if (!tls13_client_hello_build(ctx, cbb))
		return 0;

	return 1;
}

int
tls13_client_hello_sent(struct tls13_ctx *ctx)
{
	tls1_transcript_freeze(ctx->ssl);

	if (ctx->middlebox_compat) {
		tls13_record_layer_allow_ccs(ctx->rl, 1);
		ctx->send_dummy_ccs = 1;
	}

	return 1;
}

static int
tls13_server_hello_is_legacy(CBS *cbs)
{
	CBS extensions_block, extensions, extension_data;
	uint16_t selected_version = 0;
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
		if (!CBS_get_u16(&extension_data, &selected_version))
			return 1;
		if (CBS_len(&extension_data) != 0)
			return 1;
	}

	return (selected_version < TLS1_3_VERSION);
}

static int
tls13_server_hello_is_retry(CBS *cbs)
{
	CBS server_hello, server_random;
	uint16_t legacy_version;

	CBS_dup(cbs, &server_hello);

	if (!CBS_get_u16(&server_hello, &legacy_version))
		return 0;
	if (!CBS_get_bytes(&server_hello, &server_random, SSL3_RANDOM_SIZE))
		return 0;

	/* See if this is a HelloRetryRequest. */
	return CBS_mem_equal(&server_random, tls13_hello_retry_request_hash,
	    sizeof(tls13_hello_retry_request_hash));
}

static int
tls13_server_hello_process(struct tls13_ctx *ctx, CBS *cbs)
{
	CBS server_random, session_id;
	uint16_t tlsext_msg_type = SSL_TLSEXT_MSG_SH;
	uint16_t cipher_suite, legacy_version;
	uint8_t compression_method;
	const SSL_CIPHER *cipher;
	int alert_desc;
	SSL *s = ctx->ssl;

	if (!CBS_get_u16(cbs, &legacy_version))
		goto err;
	if (!CBS_get_bytes(cbs, &server_random, SSL3_RANDOM_SIZE))
		goto err;
	if (!CBS_get_u8_length_prefixed(cbs, &session_id))
		goto err;
	if (!CBS_get_u16(cbs, &cipher_suite))
		goto err;
	if (!CBS_get_u8(cbs, &compression_method))
		goto err;

	if (tls13_server_hello_is_legacy(cbs)) {
		if (ctx->hs->our_max_tls_version >= TLS1_3_VERSION) {
			/*
			 * RFC 8446 section 4.1.3: we must not downgrade if
			 * the server random value contains the TLS 1.2 or 1.1
			 * magical value.
			 */
			if (!CBS_skip(&server_random, CBS_len(&server_random) -
			    sizeof(tls13_downgrade_12)))
				goto err;
			if (CBS_mem_equal(&server_random, tls13_downgrade_12,
			    sizeof(tls13_downgrade_12)) ||
			    CBS_mem_equal(&server_random, tls13_downgrade_11,
			    sizeof(tls13_downgrade_11))) {
				ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
				goto err;
			}
		}

		if (!CBS_skip(cbs, CBS_len(cbs)))
			goto err;

		ctx->hs->tls13.use_legacy = 1;
		return 1;
	}

	/* From here on in we know we are doing TLSv1.3. */
	tls13_record_layer_set_legacy_version(ctx->rl, TLS1_2_VERSION);
	tls13_record_layer_allow_legacy_alerts(ctx->rl, 0);

	/* See if this is a HelloRetryRequest. */
	/* XXX - see if we can avoid doing this twice. */
	if (CBS_mem_equal(&server_random, tls13_hello_retry_request_hash,
	    sizeof(tls13_hello_retry_request_hash))) {
		tlsext_msg_type = SSL_TLSEXT_MSG_HRR;
		ctx->hs->tls13.hrr = 1;
	}

	if (!tlsext_client_parse(s, tlsext_msg_type, cbs, &alert_desc)) {
		ctx->alert = alert_desc;
		goto err;
	}

	/*
	 * The supported versions extension indicated 0x0304 or greater.
	 * Ensure that it was 0x0304 and that legacy version is set to 0x0303
	 * (RFC 8446 section 4.2.1).
	 */
	if (ctx->hs->tls13.server_version != TLS1_3_VERSION ||
	    legacy_version != TLS1_2_VERSION) {
		ctx->alert = TLS13_ALERT_PROTOCOL_VERSION;
		goto err;
	}
	ctx->hs->negotiated_tls_version = ctx->hs->tls13.server_version;
	ctx->hs->peer_legacy_version = legacy_version;

	/* The session_id must match. */
	if (!CBS_mem_equal(&session_id, ctx->hs->tls13.legacy_session_id,
	    ctx->hs->tls13.legacy_session_id_len)) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}

	/*
	 * Ensure that the cipher suite is one that we offered in the client
	 * hello and that it is a TLSv1.3 cipher suite.
	 */
	cipher = ssl3_get_cipher_by_value(cipher_suite);
	if (cipher == NULL || !ssl_cipher_in_list(SSL_get_ciphers(s), cipher)) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}
	if (cipher->algorithm_ssl != SSL_TLSV1_3) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}
	if (!(ctx->handshake_stage.hs_type & WITHOUT_HRR) && !ctx->hs->tls13.hrr) {
		/*
		 * A ServerHello following a HelloRetryRequest MUST use the same
		 * cipher suite (RFC 8446 section 4.1.4).
		 */
		if (ctx->hs->cipher != cipher) {
			ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
			goto err;
		}
	}
	ctx->hs->cipher = cipher;

	if (compression_method != 0) {
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}

	return 1;

 err:
	if (ctx->alert == 0)
		ctx->alert = TLS13_ALERT_DECODE_ERROR;

	return 0;
}

static int
tls13_client_engage_record_protection(struct tls13_ctx *ctx)
{
	struct tls13_secrets *secrets;
	struct tls13_secret context;
	unsigned char buf[EVP_MAX_MD_SIZE];
	uint8_t *shared_key = NULL;
	size_t shared_key_len = 0;
	size_t hash_len;
	SSL *s = ctx->ssl;
	int ret = 0;

	/* Derive the shared key and engage record protection. */

	if (!tls_key_share_derive(ctx->hs->key_share, &shared_key,
	    &shared_key_len))
		goto err;

	s->session->cipher_value = ctx->hs->cipher->value;
	s->session->ssl_version = ctx->hs->tls13.server_version;

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
	    &secrets->server_handshake_traffic, ssl_encryption_handshake))
		goto err;
	if (!tls13_record_layer_set_write_traffic_key(ctx->rl,
	    &secrets->client_handshake_traffic, ssl_encryption_handshake))
		goto err;

	ret = 1;

 err:
	freezero(shared_key, shared_key_len);

	return ret;
}

int
tls13_server_hello_retry_request_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	/*
	 * The state machine has no way of knowing if we're going to receive a
	 * HelloRetryRequest or a ServerHello. As such, we have to handle
	 * this case here and hand off to the appropriate function.
	 */
	if (!tls13_server_hello_is_retry(cbs)) {
		ctx->handshake_stage.hs_type |= WITHOUT_HRR;
		return tls13_server_hello_recv(ctx, cbs);
	}

	if (!tls13_server_hello_process(ctx, cbs))
		return 0;

	/*
	 * This may have been a TLSv1.2 or earlier ServerHello that just
	 * happened to have matching server random...
	 */
	if (ctx->hs->tls13.use_legacy)
		return tls13_use_legacy_client(ctx);

	if (!ctx->hs->tls13.hrr)
		return 0;

	if (!tls13_synthetic_handshake_message(ctx))
		return 0;
	if (!tls13_handshake_msg_record(ctx))
		return 0;

	ctx->hs->tls13.hrr = 0;

	return 1;
}

int
tls13_client_hello_retry_send(struct tls13_ctx *ctx, CBB *cbb)
{
	/*
	 * Ensure that the server supported group is one that we listed in our
	 * supported groups and is not the same as the key share we previously
	 * offered.
	 */
	if (!tls1_check_group(ctx->ssl, ctx->hs->tls13.server_group))
		return 0; /* XXX alert */
	if (ctx->hs->tls13.server_group == tls_key_share_group(ctx->hs->key_share))
		return 0; /* XXX alert */

	/* Switch to new key share. */
	tls_key_share_free(ctx->hs->key_share);
	if ((ctx->hs->key_share =
	    tls_key_share_new(ctx->hs->tls13.server_group)) == NULL)
		return 0;
	if (!tls_key_share_generate(ctx->hs->key_share))
		return 0;

	if (!tls13_client_hello_build(ctx, cbb))
		return 0;

	return 1;
}

int
tls13_server_hello_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	SSL *s = ctx->ssl;

	/*
	 * We may have received a legacy (pre-TLSv1.3) ServerHello or a TLSv1.3
	 * ServerHello. HelloRetryRequests have already been handled.
	 */
	if (!tls13_server_hello_process(ctx, cbs))
		return 0;

	if (ctx->handshake_stage.hs_type & WITHOUT_HRR) {
		tls1_transcript_unfreeze(s);
		if (!tls13_handshake_msg_record(ctx))
			return 0;
	}

	if (ctx->hs->tls13.use_legacy) {
		if (!(ctx->handshake_stage.hs_type & WITHOUT_HRR))
			return 0;
		return tls13_use_legacy_client(ctx);
	}

	if (ctx->hs->tls13.hrr) {
		/* The server has sent two HelloRetryRequests. */
		ctx->alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		return 0;
	}

	if (!tls13_client_engage_record_protection(ctx))
		return 0;

	ctx->handshake_stage.hs_type |= NEGOTIATED;

	return 1;
}

int
tls13_server_encrypted_extensions_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	int alert_desc;

	if (!tlsext_client_parse(ctx->ssl, SSL_TLSEXT_MSG_EE, cbs, &alert_desc)) {
		ctx->alert = alert_desc;
		return 0;
	}

	return 1;
}

int
tls13_server_certificate_request_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	CBS cert_request_context;
	int alert_desc;

	/*
	 * Thanks to poor state design in the RFC, this function can be called
	 * when we actually have a certificate message instead of a certificate
	 * request... in that case we call the certificate handler after
	 * switching state, to avoid advancing state.
	 */
	if (tls13_handshake_msg_type(ctx->hs_msg) == TLS13_MT_CERTIFICATE) {
		ctx->handshake_stage.hs_type |= WITHOUT_CR;
		return tls13_server_certificate_recv(ctx, cbs);
	}

	if (!CBS_get_u8_length_prefixed(cbs, &cert_request_context))
		goto err;
	if (CBS_len(&cert_request_context) != 0)
		goto err;

	if (!tlsext_client_parse(ctx->ssl, SSL_TLSEXT_MSG_CR, cbs, &alert_desc)) {
		ctx->alert = alert_desc;
		goto err;
	}

	return 1;

 err:
	if (ctx->alert == 0)
		ctx->alert = TLS13_ALERT_DECODE_ERROR;

	return 0;
}

int
tls13_server_certificate_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	CBS cert_request_context, cert_list, cert_data;
	struct stack_st_X509 *certs = NULL;
	SSL *s = ctx->ssl;
	X509 *cert = NULL;
	const uint8_t *p;
	int alert_desc;
	int ret = 0;

	if ((certs = sk_X509_new_null()) == NULL)
		goto err;

	if (!CBS_get_u8_length_prefixed(cbs, &cert_request_context))
		goto err;
	if (CBS_len(&cert_request_context) != 0)
		goto err;
	if (!CBS_get_u24_length_prefixed(cbs, &cert_list))
		goto err;

	while (CBS_len(&cert_list) > 0) {
		if (!CBS_get_u24_length_prefixed(&cert_list, &cert_data))
			goto err;

		if (!tlsext_client_parse(ctx->ssl, SSL_TLSEXT_MSG_CT,
		    &cert_list, &alert_desc)) {
			ctx->alert = alert_desc;
			goto err;
		}

		p = CBS_data(&cert_data);
		if ((cert = d2i_X509(NULL, &p, CBS_len(&cert_data))) == NULL)
			goto err;
		if (p != CBS_data(&cert_data) + CBS_len(&cert_data))
			goto err;

		if (!sk_X509_push(certs, cert))
			goto err;

		cert = NULL;
	}

	/* A server must always provide a non-empty certificate list. */
	if (sk_X509_num(certs) < 1) {
		ctx->alert = TLS13_ALERT_DECODE_ERROR;
		tls13_set_errorx(ctx, TLS13_ERR_NO_PEER_CERTIFICATE, 0,
		    "peer failed to provide a certificate", NULL);
		goto err;
	}

	/*
	 * At this stage we still have no proof of possession. As such, it would
	 * be preferable to keep the chain and verify once we have successfully
	 * processed the CertificateVerify message.
	 */
	if (ssl_verify_cert_chain(s, certs) <= 0 &&
	    s->verify_mode != SSL_VERIFY_NONE) {
		ctx->alert = ssl_verify_alarm_type(s->verify_result);
		tls13_set_errorx(ctx, TLS13_ERR_VERIFY_FAILED, 0,
		    "failed to verify peer certificate", NULL);
		goto err;
	}
	s->session->verify_result = s->verify_result;
	ERR_clear_error();

	if (!tls_process_peer_certs(s, certs))
		goto err;

	if (ctx->ocsp_status_recv_cb != NULL &&
	    !ctx->ocsp_status_recv_cb(ctx))
		goto err;

	ret = 1;

 err:
	sk_X509_pop_free(certs, X509_free);
	X509_free(cert);

	return ret;
}

int
tls13_server_certificate_verify_recv(struct tls13_ctx *ctx, CBS *cbs)
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
	if (!CBB_add_bytes(&cbb, tls13_cert_server_verify_context,
	    strlen(tls13_cert_server_verify_context)))
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
tls13_server_finished_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret context = { .data = "", .len = 0 };
	struct tls13_secret finished_key;
	uint8_t transcript_hash[EVP_MAX_MD_SIZE];
	size_t transcript_hash_len;
	uint8_t *verify_data = NULL;
	size_t verify_data_len;
	uint8_t key[EVP_MAX_MD_SIZE];
	HMAC_CTX *hmac_ctx = NULL;
	unsigned int hlen;
	int ret = 0;

	/*
	 * Verify server finished.
	 */
	finished_key.data = key;
	finished_key.len = EVP_MD_size(ctx->hash);

	if (!tls13_hkdf_expand_label(&finished_key, ctx->hash,
	    &secrets->server_handshake_traffic, "finished",
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
	 * Derive application traffic keys.
	 */
	if (!tls1_transcript_hash_value(ctx->ssl, transcript_hash,
	    sizeof(transcript_hash), &transcript_hash_len))
		goto err;

	context.data = transcript_hash;
	context.len = transcript_hash_len;

	if (!tls13_derive_application_secrets(secrets, &context))
		goto err;

	/*
	 * Any records following the server finished message must be encrypted
	 * using the server application traffic keys.
	 */
	if (!tls13_record_layer_set_read_traffic_key(ctx->rl,
	    &secrets->server_application_traffic, ssl_encryption_application))
		goto err;

	tls13_record_layer_allow_ccs(ctx->rl, 0);

	ret = 1;

 err:
	HMAC_CTX_free(hmac_ctx);
	free(verify_data);

	return ret;
}

static int
tls13_client_check_certificate(struct tls13_ctx *ctx, SSL_CERT_PKEY *cpk,
    int *ok, const struct ssl_sigalg **out_sigalg)
{
	const struct ssl_sigalg *sigalg;
	SSL *s = ctx->ssl;

	*ok = 0;
	*out_sigalg = NULL;

	if (cpk->x509 == NULL || cpk->privatekey == NULL)
		goto done;

	if ((sigalg = ssl_sigalg_select(s, cpk->privatekey)) == NULL)
		goto done;

	*ok = 1;
	*out_sigalg = sigalg;

 done:
	return 1;
}

static int
tls13_client_select_certificate(struct tls13_ctx *ctx, SSL_CERT_PKEY **out_cpk,
    const struct ssl_sigalg **out_sigalg)
{
	SSL *s = ctx->ssl;
	const struct ssl_sigalg *sigalg;
	SSL_CERT_PKEY *cpk;
	int cert_ok;

	*out_cpk = NULL;
	*out_sigalg = NULL;

	/*
	 * XXX - RFC 8446, 4.4.2.3: the server can communicate preferences
	 * with the certificate_authorities (4.2.4) and oid_filters (4.2.5)
	 * extensions. We should honor the former and must apply the latter.
	 */

	cpk = &s->cert->pkeys[SSL_PKEY_ECC];
	if (!tls13_client_check_certificate(ctx, cpk, &cert_ok, &sigalg))
		return 0;
	if (cert_ok)
		goto done;

	cpk = &s->cert->pkeys[SSL_PKEY_RSA];
	if (!tls13_client_check_certificate(ctx, cpk, &cert_ok, &sigalg))
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
tls13_client_certificate_send(struct tls13_ctx *ctx, CBB *cbb)
{
	SSL *s = ctx->ssl;
	CBB cert_request_context, cert_list;
	const struct ssl_sigalg *sigalg;
	STACK_OF(X509) *chain;
	SSL_CERT_PKEY *cpk;
	X509 *cert;
	int i, ret = 0;

	if (!tls13_client_select_certificate(ctx, &cpk, &sigalg))
		goto err;

	ctx->hs->tls13.cpk = cpk;
	ctx->hs->our_sigalg = sigalg;

	if (!CBB_add_u8_length_prefixed(cbb, &cert_request_context))
		goto err;
	if (!CBB_add_u24_length_prefixed(cbb, &cert_list))
		goto err;

	/* No certificate selected. */
	if (cpk == NULL)
		goto done;

	if ((chain = cpk->chain) == NULL)
	       chain = s->ctx->extra_certs;

	if (!tls13_cert_add(ctx, &cert_list, cpk->x509, tlsext_client_build))
		goto err;

	for (i = 0; i < sk_X509_num(chain); i++) {
		cert = sk_X509_value(chain, i);
		if (!tls13_cert_add(ctx, &cert_list, cert, tlsext_client_build))
			goto err;
	}

	ctx->handshake_stage.hs_type |= WITH_CCV;
 done:
	if (!CBB_flush(cbb))
		goto err;

	ret = 1;

 err:
	return ret;
}

int
tls13_client_certificate_verify_send(struct tls13_ctx *ctx, CBB *cbb)
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
	if (!CBB_add_bytes(&sig_cbb, tls13_cert_client_verify_context,
	    strlen(tls13_cert_client_verify_context)))
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
tls13_client_end_of_early_data_send(struct tls13_ctx *ctx, CBB *cbb)
{
	return 0;
}

int
tls13_client_finished_send(struct tls13_ctx *ctx, CBB *cbb)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret context = { .data = "", .len = 0 };
	struct tls13_secret finished_key = { .data = NULL, .len = 0 };
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
	    &secrets->client_handshake_traffic, "finished",
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
tls13_client_finished_sent(struct tls13_ctx *ctx)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;

	/*
	 * Any records following the client finished message must be encrypted
	 * using the client application traffic keys.
	 */
	return tls13_record_layer_set_write_traffic_key(ctx->rl,
	    &secrets->client_application_traffic, ssl_encryption_application);
}
