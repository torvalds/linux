/*	$OpenBSD: tls13_lib.c,v 1.78 2025/06/07 10:25:12 tb Exp $ */
/*
 * Copyright (c) 2018, 2019 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2019 Bob Beck <beck@openbsd.org>
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

#include <stddef.h>

#include <openssl/evp.h>

#include "ssl_local.h"
#include "ssl_tlsext.h"
#include "tls13_internal.h"

/*
 * RFC 8446, section 4.6.1. Servers must not indicate a lifetime longer than
 * 7 days and clients must not cache tickets for longer than 7 days.
 */

#define TLS13_MAX_TICKET_LIFETIME	(7 * 24 * 3600)

/*
 * Downgrade sentinels - RFC 8446 section 4.1.3, magic values which must be set
 * by the server in server random if it is willing to downgrade but supports
 * TLSv1.3
 */
const uint8_t tls13_downgrade_12[8] = {
	0x44, 0x4f, 0x57, 0x4e, 0x47, 0x52, 0x44, 0x01,
};
const uint8_t tls13_downgrade_11[8] = {
	0x44, 0x4f, 0x57, 0x4e, 0x47, 0x52, 0x44, 0x00,
};

/*
 * HelloRetryRequest hash - RFC 8446 section 4.1.3.
 */
const uint8_t tls13_hello_retry_request_hash[32] = {
	0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11,
	0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
	0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e,
	0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c,
};

/*
 * Certificate Verify padding - RFC 8446 section 4.4.3.
 */
const uint8_t tls13_cert_verify_pad[64] = {
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};

const uint8_t tls13_cert_client_verify_context[] =
    "TLS 1.3, client CertificateVerify";
const uint8_t tls13_cert_server_verify_context[] =
    "TLS 1.3, server CertificateVerify";

const EVP_AEAD *
tls13_cipher_aead(const SSL_CIPHER *cipher)
{
	if (cipher == NULL)
		return NULL;
	if (cipher->algorithm_ssl != SSL_TLSV1_3)
		return NULL;

	switch (cipher->algorithm_enc) {
	case SSL_AES128GCM:
		return EVP_aead_aes_128_gcm();
	case SSL_AES256GCM:
		return EVP_aead_aes_256_gcm();
	case SSL_CHACHA20POLY1305:
		return EVP_aead_chacha20_poly1305();
	}

	return NULL;
}

const EVP_MD *
tls13_cipher_hash(const SSL_CIPHER *cipher)
{
	if (cipher == NULL)
		return NULL;
	if (cipher->algorithm_ssl != SSL_TLSV1_3)
		return NULL;

	switch (cipher->algorithm2) {
	case SSL_HANDSHAKE_MAC_SHA256:
		return EVP_sha256();
	case SSL_HANDSHAKE_MAC_SHA384:
		return EVP_sha384();
	}

	return NULL;
}

static void
tls13_legacy_alert_cb(int sent, uint8_t alert_level, uint8_t alert_desc,
    void *arg)
{
	uint8_t alert[] = {alert_level, alert_desc};
	struct tls13_ctx *ctx = arg;
	SSL *s = ctx->ssl;
	CBS cbs;

	if (s->msg_callback == NULL)
		return;

	CBS_init(&cbs, alert, sizeof(alert));
	ssl_msg_callback_cbs(s, sent, SSL3_RT_ALERT, &cbs);
}

static void
tls13_legacy_alert_recv_cb(uint8_t alert_level, uint8_t alert_desc, void *arg)
{
	tls13_legacy_alert_cb(0, alert_level, alert_desc, arg);
}

static void
tls13_legacy_alert_sent_cb(uint8_t alert_level, uint8_t alert_desc, void *arg)
{
	tls13_legacy_alert_cb(1, alert_level, alert_desc, arg);
}

void
tls13_alert_received_cb(uint8_t alert_level, uint8_t alert_desc, void *arg)
{
	struct tls13_ctx *ctx = arg;

	if (ctx->alert_recv_cb != NULL)
		ctx->alert_recv_cb(alert_level, alert_desc, arg);

	if (alert_desc == TLS13_ALERT_CLOSE_NOTIFY) {
		ctx->close_notify_recv = 1;
		ctx->ssl->shutdown |= SSL_RECEIVED_SHUTDOWN;
		ctx->ssl->s3->warn_alert = alert_desc;
		return;
	}

	if (alert_desc == TLS13_ALERT_USER_CANCELED) {
		/*
		 * We treat this as advisory, since a close_notify alert
		 * SHOULD follow this alert (RFC 8446 section 6.1).
		 */
		return;
	}

	/* All other alerts are treated as fatal in TLSv1.3. */
	ctx->ssl->s3->fatal_alert = alert_desc;

	SSLerror(ctx->ssl, SSL_AD_REASON_OFFSET + alert_desc);
	ERR_asprintf_error_data("SSL alert number %d", alert_desc);

	SSL_CTX_remove_session(ctx->ssl->ctx, ctx->ssl->session);
}

void
tls13_alert_sent_cb(uint8_t alert_level, uint8_t alert_desc, void *arg)
{
	struct tls13_ctx *ctx = arg;

	if (ctx->alert_sent_cb != NULL)
		ctx->alert_sent_cb(alert_level, alert_desc, arg);

	if (alert_desc == TLS13_ALERT_CLOSE_NOTIFY) {
		ctx->close_notify_sent = 1;
		return;
	}

	if (alert_desc == TLS13_ALERT_USER_CANCELED) {
		return;
	}

	/* All other alerts are treated as fatal in TLSv1.3. */
	if (ctx->error.code == 0)
		SSLerror(ctx->ssl, SSL_AD_REASON_OFFSET + alert_desc);
}

static void
tls13_legacy_handshake_message_recv_cb(void *arg)
{
	struct tls13_ctx *ctx = arg;
	SSL *s = ctx->ssl;
	CBS cbs;

	if (s->msg_callback == NULL)
		return;

	tls13_handshake_msg_data(ctx->hs_msg, &cbs);
	ssl_msg_callback_cbs(s, 0, SSL3_RT_HANDSHAKE, &cbs);
}

static void
tls13_legacy_handshake_message_sent_cb(void *arg)
{
	struct tls13_ctx *ctx = arg;
	SSL *s = ctx->ssl;
	CBS cbs;

	if (s->msg_callback == NULL)
		return;

	tls13_handshake_msg_data(ctx->hs_msg, &cbs);
	ssl_msg_callback_cbs(s, 1, SSL3_RT_HANDSHAKE, &cbs);
}

static void
tls13_legacy_info_cb(void *arg, int state, int ret)
{
	struct tls13_ctx *ctx = arg;
	SSL *s = ctx->ssl;

	ssl_info_callback(s, state, ret);
}

static int
tls13_legacy_ocsp_status_recv_cb(void *arg)
{
	struct tls13_ctx *ctx = arg;
	SSL *s = ctx->ssl;
	int ret;

	if (s->ctx->tlsext_status_cb == NULL)
		return 1;

	ret = s->ctx->tlsext_status_cb(s,
	    s->ctx->tlsext_status_arg);
	if (ret < 0) {
		ctx->alert = TLS13_ALERT_INTERNAL_ERROR;
		SSLerror(s, ERR_R_MALLOC_FAILURE);
		return 0;
	}
	if (ret == 0) {
		ctx->alert = TLS13_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE;
		SSLerror(s, SSL_R_INVALID_STATUS_RESPONSE);
		return 0;
	}

	return 1;
}

static int
tls13_phh_update_read_traffic_secret(struct tls13_ctx *ctx)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret *secret;

	if (ctx->mode == TLS13_HS_CLIENT) {
		secret = &secrets->server_application_traffic;
		if (!tls13_update_server_traffic_secret(secrets))
			return 0;
	} else {
		secret = &secrets->client_application_traffic;
		if (!tls13_update_client_traffic_secret(secrets))
			return 0;
	}

	return tls13_record_layer_set_read_traffic_key(ctx->rl,
	    secret, ssl_encryption_application);
}

static int
tls13_phh_update_write_traffic_secret(struct tls13_ctx *ctx)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret *secret;

	if (ctx->mode == TLS13_HS_CLIENT) {
		secret = &secrets->client_application_traffic;
		if (!tls13_update_client_traffic_secret(secrets))
			return 0;
	} else {
		secret = &secrets->server_application_traffic;
		if (!tls13_update_server_traffic_secret(secrets))
			return 0;
	}

	return tls13_record_layer_set_write_traffic_key(ctx->rl,
	    secret, ssl_encryption_application);
}

/*
 * XXX arbitrarily chosen limit of 100 post handshake handshake
 * messages in an hour - to avoid a hostile peer from constantly
 * requesting certificates or key renegotiaitons, etc.
 */
static int
tls13_phh_limit_check(struct tls13_ctx *ctx)
{
	time_t now = time(NULL);

	if (ctx->phh_last_seen > now - TLS13_PHH_LIMIT_TIME) {
		if (ctx->phh_count > TLS13_PHH_LIMIT)
			return 0;
	} else
		ctx->phh_count = 0;
	ctx->phh_count++;
	ctx->phh_last_seen = now;
	return 1;
}

static ssize_t
tls13_key_update_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	struct tls13_handshake_msg *hs_msg = NULL;
	CBB cbb_hs;
	CBS cbs_hs;
	uint8_t alert = TLS13_ALERT_INTERNAL_ERROR;
	uint8_t key_update_request;
	ssize_t ret;

	if (!CBS_get_u8(cbs, &key_update_request)) {
		alert = TLS13_ALERT_DECODE_ERROR;
		goto err;
	}
	if (CBS_len(cbs) != 0) {
		alert = TLS13_ALERT_DECODE_ERROR;
		goto err;
	}
	if (key_update_request > 1) {
		alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}

	if (!tls13_phh_update_read_traffic_secret(ctx))
		goto err;

	if (key_update_request == 0)
		return TLS13_IO_SUCCESS;

	/* Our peer requested that we update our write traffic keys. */
	if ((hs_msg = tls13_handshake_msg_new()) == NULL)
		goto err;
	if (!tls13_handshake_msg_start(hs_msg, &cbb_hs, TLS13_MT_KEY_UPDATE))
		goto err;
	if (!CBB_add_u8(&cbb_hs, 0))
		goto err;
	if (!tls13_handshake_msg_finish(hs_msg))
		goto err;

	ctx->key_update_request = 1;
	tls13_handshake_msg_data(hs_msg, &cbs_hs);
	ret = tls13_record_layer_phh(ctx->rl, &cbs_hs);

	tls13_handshake_msg_free(hs_msg);
	hs_msg = NULL;

	return ret;

 err:
	tls13_handshake_msg_free(hs_msg);

	return tls13_send_alert(ctx->rl, alert);
}

/* RFC 8446 section 4.6.1 */
static ssize_t
tls13_new_session_ticket_recv(struct tls13_ctx *ctx, CBS *cbs)
{
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	struct tls13_secret nonce;
	uint32_t ticket_lifetime, ticket_age_add;
	CBS ticket_nonce, ticket;
	SSL_SESSION *sess = NULL;
	int alert, session_id_length;
	ssize_t ret = 0;

	memset(&nonce, 0, sizeof(nonce));

	if (ctx->mode != TLS13_HS_CLIENT) {
		alert = TLS13_ALERT_UNEXPECTED_MESSAGE;
		goto err;
	}

	alert = TLS13_ALERT_DECODE_ERROR;

	if (!CBS_get_u32(cbs, &ticket_lifetime))
		goto err;
	if (!CBS_get_u32(cbs, &ticket_age_add))
		goto err;
	if (!CBS_get_u8_length_prefixed(cbs, &ticket_nonce))
		goto err;
	if (!CBS_get_u16_length_prefixed(cbs, &ticket))
		goto err;
	/* Extensions can only contain early_data, which we currently ignore. */
	if (!tlsext_client_parse(ctx->ssl, SSL_TLSEXT_MSG_NST, cbs, &alert))
		goto err;

	if (CBS_len(cbs) != 0)
		goto err;

	/* Zero indicates that the ticket should be discarded immediately. */
	if (ticket_lifetime == 0) {
		ret = TLS13_IO_SUCCESS;
		goto done;
	}

	/* Servers MUST NOT use any value larger than 7 days. */
	if (ticket_lifetime > TLS13_MAX_TICKET_LIFETIME) {
		alert = TLS13_ALERT_ILLEGAL_PARAMETER;
		goto err;
	}

	alert = TLS13_ALERT_INTERNAL_ERROR;

	/*
	 * Create new session instead of modifying the current session.
	 * The current session could already be in the session cache.
	 */
	if ((sess = ssl_session_dup(ctx->ssl->session, 0)) == NULL)
		goto err;

	sess->time = time(NULL);

	sess->tlsext_tick_lifetime_hint = ticket_lifetime;
	sess->tlsext_tick_age_add = ticket_age_add;

	if (!CBS_stow(&ticket, &sess->tlsext_tick, &sess->tlsext_ticklen))
		goto err;

	/* XXX - ensure this doesn't overflow session_id if hash is changed. */
	if (!EVP_Digest(CBS_data(&ticket), CBS_len(&ticket),
	    sess->session_id, &session_id_length, EVP_sha256(), NULL))
		goto err;
	sess->session_id_length = session_id_length;

	if (!CBS_stow(&ticket_nonce, &nonce.data, &nonce.len))
		goto err;

	if (!tls13_secret_init(&sess->resumption_master_secret, 256))
		goto err;

	if (!tls13_derive_secret(&sess->resumption_master_secret,
	    secrets->digest, &secrets->resumption_master, "resumption",
	    &nonce))
		goto err;

	SSL_SESSION_free(ctx->ssl->session);
	ctx->ssl->session = sess;
	sess = NULL;

	ssl_update_cache(ctx->ssl, SSL_SESS_CACHE_CLIENT);

	ret = TLS13_IO_SUCCESS;
	goto done;

 err:
	ret = tls13_send_alert(ctx->rl, alert);

 done:
	tls13_secret_cleanup(&nonce);
	SSL_SESSION_free(sess);

	return ret;
}

ssize_t
tls13_phh_received_cb(void *cb_arg)
{
	ssize_t ret = TLS13_IO_FAILURE;
	struct tls13_ctx *ctx = cb_arg;
	CBS cbs;

	if (!tls13_phh_limit_check(ctx))
		return tls13_send_alert(ctx->rl, TLS13_ALERT_UNEXPECTED_MESSAGE);

	if ((ctx->hs_msg == NULL) &&
	    ((ctx->hs_msg = tls13_handshake_msg_new()) == NULL))
		return TLS13_IO_FAILURE;

	if ((ret = tls13_handshake_msg_recv(ctx->hs_msg, ctx->rl)) !=
	    TLS13_IO_SUCCESS)
		return ret;

	if (!tls13_handshake_msg_content(ctx->hs_msg, &cbs))
		return TLS13_IO_FAILURE;

	switch(tls13_handshake_msg_type(ctx->hs_msg)) {
	case TLS13_MT_KEY_UPDATE:
		ret = tls13_key_update_recv(ctx, &cbs);
		break;
	case TLS13_MT_NEW_SESSION_TICKET:
		ret = tls13_new_session_ticket_recv(ctx, &cbs);
		break;
	case TLS13_MT_CERTIFICATE_REQUEST:
		/* XXX add support if we choose to advertise this */
		/* FALLTHROUGH */
	default:
		ret = TLS13_IO_FAILURE; /* XXX send alert */
		break;
	}

	tls13_handshake_msg_free(ctx->hs_msg);
	ctx->hs_msg = NULL;
	return ret;
}

void
tls13_phh_done_cb(void *cb_arg)
{
	struct tls13_ctx *ctx = cb_arg;

	if (ctx->key_update_request) {
		tls13_phh_update_write_traffic_secret(ctx);
		ctx->key_update_request = 0;
	}
}

static const struct tls13_record_layer_callbacks tls13_rl_callbacks = {
	.wire_read = tls13_legacy_wire_read_cb,
	.wire_write = tls13_legacy_wire_write_cb,
	.wire_flush = tls13_legacy_wire_flush_cb,

	.alert_recv = tls13_alert_received_cb,
	.alert_sent = tls13_alert_sent_cb,
	.phh_recv = tls13_phh_received_cb,
	.phh_sent = tls13_phh_done_cb,
};

struct tls13_ctx *
tls13_ctx_new(int mode, SSL *ssl)
{
	struct tls13_ctx *ctx = NULL;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		goto err;

	ctx->hs = &ssl->s3->hs;
	ctx->mode = mode;
	ctx->ssl = ssl;

	if ((ctx->rl = tls13_record_layer_new(&tls13_rl_callbacks, ctx)) == NULL)
		goto err;

	ctx->alert_sent_cb = tls13_legacy_alert_sent_cb;
	ctx->alert_recv_cb = tls13_legacy_alert_recv_cb;
	ctx->handshake_message_sent_cb = tls13_legacy_handshake_message_sent_cb;
	ctx->handshake_message_recv_cb = tls13_legacy_handshake_message_recv_cb;
	ctx->info_cb = tls13_legacy_info_cb;
	ctx->ocsp_status_recv_cb = tls13_legacy_ocsp_status_recv_cb;

	ctx->middlebox_compat = 1;

	ssl->tls13 = ctx;

	if (SSL_is_quic(ssl)) {
		if (!tls13_quic_init(ctx))
			goto err;
	}

	return ctx;

 err:
	tls13_ctx_free(ctx);

	return NULL;
}

void
tls13_ctx_free(struct tls13_ctx *ctx)
{
	if (ctx == NULL)
		return;

	tls13_error_clear(&ctx->error);
	tls13_record_layer_free(ctx->rl);
	tls13_handshake_msg_free(ctx->hs_msg);

	freezero(ctx, sizeof(struct tls13_ctx));
}

int
tls13_cert_add(struct tls13_ctx *ctx, CBB *cbb, X509 *cert,
    int (*build_extensions)(SSL *s, uint16_t msg_type, CBB *cbb))
{
	CBB cert_data, cert_exts;
	uint8_t *data;
	int cert_len;

	if ((cert_len = i2d_X509(cert, NULL)) < 0)
		return 0;

	if (!CBB_add_u24_length_prefixed(cbb, &cert_data))
		return 0;
	if (!CBB_add_space(&cert_data, &data, cert_len))
		return 0;
	if (i2d_X509(cert, &data) != cert_len)
		return 0;
	if (build_extensions != NULL) {
		if (!build_extensions(ctx->ssl, SSL_TLSEXT_MSG_CT, cbb))
			return 0;
	} else {
		if (!CBB_add_u16_length_prefixed(cbb, &cert_exts))
			return 0;
	}
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

int
tls13_synthetic_handshake_message(struct tls13_ctx *ctx)
{
	struct tls13_handshake_msg *hm = NULL;
	unsigned char buf[EVP_MAX_MD_SIZE];
	size_t hash_len;
	CBB cbb;
	CBS cbs;
	SSL *s = ctx->ssl;
	int ret = 0;

	/*
	 * Replace ClientHello with synthetic handshake message - see
	 * RFC 8446 section 4.4.1.
	 */
	if (!tls1_transcript_hash_init(s))
		goto err;
	if (!tls1_transcript_hash_value(s, buf, sizeof(buf), &hash_len))
		goto err;

	if ((hm = tls13_handshake_msg_new()) == NULL)
		goto err;
	if (!tls13_handshake_msg_start(hm, &cbb, TLS13_MT_MESSAGE_HASH))
		goto err;
	if (!CBB_add_bytes(&cbb, buf, hash_len))
		goto err;
	if (!tls13_handshake_msg_finish(hm))
		goto err;

	tls13_handshake_msg_data(hm, &cbs);

	tls1_transcript_reset(ctx->ssl);
	if (!tls1_transcript_record(ctx->ssl, CBS_data(&cbs), CBS_len(&cbs)))
		goto err;

	ret = 1;

 err:
	tls13_handshake_msg_free(hm);

	return ret;
}

int
tls13_clienthello_hash_init(struct tls13_ctx *ctx)
{
	if (ctx->hs->tls13.clienthello_md_ctx != NULL)
		return 0;
	if ((ctx->hs->tls13.clienthello_md_ctx = EVP_MD_CTX_new()) == NULL)
		return 0;
	if (!EVP_DigestInit_ex(ctx->hs->tls13.clienthello_md_ctx,
	    EVP_sha256(), NULL))
		return 0;

	if ((ctx->hs->tls13.clienthello_hash == NULL) &&
	    (ctx->hs->tls13.clienthello_hash = calloc(1, EVP_MAX_MD_SIZE)) ==
	    NULL)
		return 0;

	return 1;
}

void
tls13_clienthello_hash_clear(struct ssl_handshake_tls13_st *hs) /* XXX */
{
	EVP_MD_CTX_free(hs->clienthello_md_ctx);
	hs->clienthello_md_ctx = NULL;
	freezero(hs->clienthello_hash, EVP_MAX_MD_SIZE);
	hs->clienthello_hash = NULL;
}

int
tls13_clienthello_hash_update_bytes(struct tls13_ctx *ctx, void *data,
    size_t len)
{
	return EVP_DigestUpdate(ctx->hs->tls13.clienthello_md_ctx, data, len);
}

int
tls13_clienthello_hash_update(struct tls13_ctx *ctx, CBS *cbs)
{
	return tls13_clienthello_hash_update_bytes(ctx, (void *)CBS_data(cbs),
	    CBS_len(cbs));
}

int
tls13_clienthello_hash_finalize(struct tls13_ctx *ctx)
{
	if (!EVP_DigestFinal_ex(ctx->hs->tls13.clienthello_md_ctx,
	    ctx->hs->tls13.clienthello_hash,
	    &ctx->hs->tls13.clienthello_hash_len))
		return 0;
	EVP_MD_CTX_free(ctx->hs->tls13.clienthello_md_ctx);
	ctx->hs->tls13.clienthello_md_ctx = NULL;
	return 1;
}

int
tls13_clienthello_hash_validate(struct tls13_ctx *ctx)
{
	unsigned char new_ch_hash[EVP_MAX_MD_SIZE];
	unsigned int new_ch_hash_len;

	if (ctx->hs->tls13.clienthello_hash == NULL)
		return 0;

	if (!EVP_DigestFinal_ex(ctx->hs->tls13.clienthello_md_ctx,
	    new_ch_hash, &new_ch_hash_len))
		return 0;
	EVP_MD_CTX_free(ctx->hs->tls13.clienthello_md_ctx);
	ctx->hs->tls13.clienthello_md_ctx = NULL;

	if (ctx->hs->tls13.clienthello_hash_len != new_ch_hash_len)
		return 0;
	if (memcmp(ctx->hs->tls13.clienthello_hash, new_ch_hash,
	    new_ch_hash_len) != 0)
		return 0;

	return 1;
}
