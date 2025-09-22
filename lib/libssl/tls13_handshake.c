/*	$OpenBSD: tls13_handshake.c,v 1.73 2024/02/03 19:57:14 tb Exp $	*/
/*
 * Copyright (c) 2018-2021 Theo Buehler <tb@openbsd.org>
 * Copyright (c) 2019 Joel Sing <jsing@openbsd.org>
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

#include "ssl_local.h"
#include "tls13_handshake.h"
#include "tls13_internal.h"

/* Based on RFC 8446 and inspired by s2n's TLS 1.2 state machine. */

struct tls13_handshake_action {
	uint8_t	handshake_type;
	uint8_t	sender;
	uint8_t	handshake_complete;
	uint8_t	send_preserve_transcript_hash;
	uint8_t	recv_preserve_transcript_hash;

	int (*send)(struct tls13_ctx *ctx, CBB *cbb);
	int (*sent)(struct tls13_ctx *ctx);
	int (*recv)(struct tls13_ctx *ctx, CBS *cbs);
};

static enum tls13_message_type
    tls13_handshake_active_state(struct tls13_ctx *ctx);

static const struct tls13_handshake_action *
    tls13_handshake_active_action(struct tls13_ctx *ctx);
static int tls13_handshake_advance_state_machine(struct tls13_ctx *ctx);

static int tls13_handshake_send_action(struct tls13_ctx *ctx,
    const struct tls13_handshake_action *action);
static int tls13_handshake_recv_action(struct tls13_ctx *ctx,
    const struct tls13_handshake_action *action);

static int tls13_handshake_set_legacy_state(struct tls13_ctx *ctx);
static int tls13_handshake_legacy_info_callback(struct tls13_ctx *ctx);

static const struct tls13_handshake_action state_machine[] = {
	[CLIENT_HELLO] = {
		.handshake_type = TLS13_MT_CLIENT_HELLO,
		.sender = TLS13_HS_CLIENT,
		.send = tls13_client_hello_send,
		.sent = tls13_client_hello_sent,
		.recv = tls13_client_hello_recv,
	},
	[CLIENT_HELLO_RETRY] = {
		.handshake_type = TLS13_MT_CLIENT_HELLO,
		.sender = TLS13_HS_CLIENT,
		.send = tls13_client_hello_retry_send,
		.recv = tls13_client_hello_retry_recv,
	},
	[CLIENT_END_OF_EARLY_DATA] = {
		.handshake_type = TLS13_MT_END_OF_EARLY_DATA,
		.sender = TLS13_HS_CLIENT,
		.send = tls13_client_end_of_early_data_send,
		.recv = tls13_client_end_of_early_data_recv,
	},
	[CLIENT_CERTIFICATE] = {
		.handshake_type = TLS13_MT_CERTIFICATE,
		.sender = TLS13_HS_CLIENT,
		.send_preserve_transcript_hash = 1,
		.send = tls13_client_certificate_send,
		.recv = tls13_client_certificate_recv,
	},
	[CLIENT_CERTIFICATE_VERIFY] = {
		.handshake_type = TLS13_MT_CERTIFICATE_VERIFY,
		.sender = TLS13_HS_CLIENT,
		.recv_preserve_transcript_hash = 1,
		.send = tls13_client_certificate_verify_send,
		.recv = tls13_client_certificate_verify_recv,
	},
	[CLIENT_FINISHED] = {
		.handshake_type = TLS13_MT_FINISHED,
		.sender = TLS13_HS_CLIENT,
		.recv_preserve_transcript_hash = 1,
		.send = tls13_client_finished_send,
		.sent = tls13_client_finished_sent,
		.recv = tls13_client_finished_recv,
	},
	[SERVER_HELLO] = {
		.handshake_type = TLS13_MT_SERVER_HELLO,
		.sender = TLS13_HS_SERVER,
		.send = tls13_server_hello_send,
		.sent = tls13_server_hello_sent,
		.recv = tls13_server_hello_recv,
	},
	[SERVER_HELLO_RETRY_REQUEST] = {
		.handshake_type = TLS13_MT_SERVER_HELLO,
		.sender = TLS13_HS_SERVER,
		.send = tls13_server_hello_retry_request_send,
		.recv = tls13_server_hello_retry_request_recv,
		.sent = tls13_server_hello_retry_request_sent,
	},
	[SERVER_ENCRYPTED_EXTENSIONS] = {
		.handshake_type = TLS13_MT_ENCRYPTED_EXTENSIONS,
		.sender = TLS13_HS_SERVER,
		.send = tls13_server_encrypted_extensions_send,
		.recv = tls13_server_encrypted_extensions_recv,
	},
	[SERVER_CERTIFICATE] = {
		.handshake_type = TLS13_MT_CERTIFICATE,
		.sender = TLS13_HS_SERVER,
		.send_preserve_transcript_hash = 1,
		.send = tls13_server_certificate_send,
		.recv = tls13_server_certificate_recv,
	},
	[SERVER_CERTIFICATE_REQUEST] = {
		.handshake_type = TLS13_MT_CERTIFICATE_REQUEST,
		.sender = TLS13_HS_SERVER,
		.send = tls13_server_certificate_request_send,
		.recv = tls13_server_certificate_request_recv,
	},
	[SERVER_CERTIFICATE_VERIFY] = {
		.handshake_type = TLS13_MT_CERTIFICATE_VERIFY,
		.sender = TLS13_HS_SERVER,
		.recv_preserve_transcript_hash = 1,
		.send = tls13_server_certificate_verify_send,
		.recv = tls13_server_certificate_verify_recv,
	},
	[SERVER_FINISHED] = {
		.handshake_type = TLS13_MT_FINISHED,
		.sender = TLS13_HS_SERVER,
		.recv_preserve_transcript_hash = 1,
		.send_preserve_transcript_hash = 1,
		.send = tls13_server_finished_send,
		.sent = tls13_server_finished_sent,
		.recv = tls13_server_finished_recv,
	},
	[APPLICATION_DATA] = {
		.handshake_complete = 1,
	},
};

const enum tls13_message_type handshakes[][TLS13_NUM_MESSAGE_TYPES] = {
	[INITIAL] = {
		CLIENT_HELLO,
		SERVER_HELLO_RETRY_REQUEST,
		CLIENT_HELLO_RETRY,
		SERVER_HELLO,
	},
	[NEGOTIATED] = {
		CLIENT_HELLO,
		SERVER_HELLO_RETRY_REQUEST,
		CLIENT_HELLO_RETRY,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_CERTIFICATE_REQUEST,
		SERVER_CERTIFICATE,
		SERVER_CERTIFICATE_VERIFY,
		SERVER_FINISHED,
		CLIENT_CERTIFICATE,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITHOUT_HRR] = {
		CLIENT_HELLO,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_CERTIFICATE_REQUEST,
		SERVER_CERTIFICATE,
		SERVER_CERTIFICATE_VERIFY,
		SERVER_FINISHED,
		CLIENT_CERTIFICATE,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITHOUT_CR] = {
		CLIENT_HELLO,
		SERVER_HELLO_RETRY_REQUEST,
		CLIENT_HELLO_RETRY,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_CERTIFICATE,
		SERVER_CERTIFICATE_VERIFY,
		SERVER_FINISHED,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITHOUT_HRR | WITHOUT_CR] = {
		CLIENT_HELLO,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_CERTIFICATE,
		SERVER_CERTIFICATE_VERIFY,
		SERVER_FINISHED,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITH_PSK] = {
		CLIENT_HELLO,
		SERVER_HELLO_RETRY_REQUEST,
		CLIENT_HELLO_RETRY,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_FINISHED,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITHOUT_HRR | WITH_PSK] = {
		CLIENT_HELLO,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_FINISHED,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITH_CCV] = {
		CLIENT_HELLO,
		SERVER_HELLO_RETRY_REQUEST,
		CLIENT_HELLO_RETRY,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_CERTIFICATE_REQUEST,
		SERVER_CERTIFICATE,
		SERVER_CERTIFICATE_VERIFY,
		SERVER_FINISHED,
		CLIENT_CERTIFICATE,
		CLIENT_CERTIFICATE_VERIFY,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
	[NEGOTIATED | WITHOUT_HRR | WITH_CCV] = {
		CLIENT_HELLO,
		SERVER_HELLO,
		SERVER_ENCRYPTED_EXTENSIONS,
		SERVER_CERTIFICATE_REQUEST,
		SERVER_CERTIFICATE,
		SERVER_CERTIFICATE_VERIFY,
		SERVER_FINISHED,
		CLIENT_CERTIFICATE,
		CLIENT_CERTIFICATE_VERIFY,
		CLIENT_FINISHED,
		APPLICATION_DATA,
	},
};

const size_t handshake_count = sizeof(handshakes) / sizeof(handshakes[0]);

#ifndef TLS13_DEBUG
#define DEBUGF(...)
#else
#define DEBUGF(...) fprintf(stderr, __VA_ARGS__)

static const char *
tls13_handshake_mode_name(uint8_t mode)
{
	switch (mode) {
	case TLS13_HS_CLIENT:
		return "Client";
	case TLS13_HS_SERVER:
		return "Server";
	}
	return "Unknown";
}

static const char *
tls13_handshake_message_name(uint8_t msg_type)
{
	switch (msg_type) {
	case TLS13_MT_CLIENT_HELLO:
		return "ClientHello";
	case TLS13_MT_SERVER_HELLO:
		return "ServerHello";
	case TLS13_MT_NEW_SESSION_TICKET:
		return "NewSessionTicket";
	case TLS13_MT_END_OF_EARLY_DATA:
		return "EndOfEarlyData";
	case TLS13_MT_ENCRYPTED_EXTENSIONS:
		return "EncryptedExtensions";
	case TLS13_MT_CERTIFICATE:
		return "Certificate";
	case TLS13_MT_CERTIFICATE_REQUEST:
		return "CertificateRequest";
	case TLS13_MT_CERTIFICATE_VERIFY:
		return "CertificateVerify";
	case TLS13_MT_FINISHED:
		return "Finished";
	}
	return "Unknown";
}
#endif

static enum tls13_message_type
tls13_handshake_active_state(struct tls13_ctx *ctx)
{
	struct tls13_handshake_stage hs = ctx->handshake_stage;

	if (hs.hs_type >= handshake_count)
		return INVALID;
	if (hs.message_number >= TLS13_NUM_MESSAGE_TYPES)
		return INVALID;

	return handshakes[hs.hs_type][hs.message_number];
}

static const struct tls13_handshake_action *
tls13_handshake_active_action(struct tls13_ctx *ctx)
{
	enum tls13_message_type mt = tls13_handshake_active_state(ctx);

	if (mt == INVALID)
		return NULL;

	return &state_machine[mt];
}

static int
tls13_handshake_advance_state_machine(struct tls13_ctx *ctx)
{
	if (++ctx->handshake_stage.message_number >= TLS13_NUM_MESSAGE_TYPES)
		return 0;

	return 1;
}

static int
tls13_handshake_end_of_flight(struct tls13_ctx *ctx,
    const struct tls13_handshake_action *previous)
{
	const struct tls13_handshake_action *current;

	if ((current = tls13_handshake_active_action(ctx)) == NULL)
		return 1;

	return current->sender != previous->sender;
}

int
tls13_handshake_msg_record(struct tls13_ctx *ctx)
{
	CBS cbs;

	tls13_handshake_msg_data(ctx->hs_msg, &cbs);
	return tls1_transcript_record(ctx->ssl, CBS_data(&cbs), CBS_len(&cbs));
}

int
tls13_handshake_perform(struct tls13_ctx *ctx)
{
	const struct tls13_handshake_action *action;
	int sending;
	int ret;

	if (!ctx->handshake_started) {
		/*
		 * Set legacy state to connect/accept and call info callback
		 * to signal that the handshake started.
		 */
		if (!tls13_handshake_set_legacy_state(ctx))
			return TLS13_IO_FAILURE;
		if (!tls13_handshake_legacy_info_callback(ctx))
			return TLS13_IO_FAILURE;

		ctx->handshake_started = 1;

		/* Set legacy state for initial ClientHello read or write. */
		if (!tls13_handshake_set_legacy_state(ctx))
			return TLS13_IO_FAILURE;
	}

	for (;;) {
		if ((action = tls13_handshake_active_action(ctx)) == NULL)
			return TLS13_IO_FAILURE;

		if (ctx->need_flush) {
			if ((ret = tls13_record_layer_flush(ctx->rl)) !=
			    TLS13_IO_SUCCESS)
				return ret;
			ctx->need_flush = 0;
		}

		if (action->handshake_complete) {
			ctx->handshake_completed = 1;
			tls13_record_layer_handshake_completed(ctx->rl);

			if (!tls13_handshake_set_legacy_state(ctx))
				return TLS13_IO_FAILURE;
			if (!tls13_handshake_legacy_info_callback(ctx))
				return TLS13_IO_FAILURE;

			return TLS13_IO_SUCCESS;
		}

		sending = action->sender == ctx->mode;

		DEBUGF("%s %s %s\n", tls13_handshake_mode_name(ctx->mode),
		    sending ? "sending" : "receiving",
		    tls13_handshake_message_name(action->handshake_type));

		if (ctx->alert != 0)
			return tls13_send_alert(ctx->rl, ctx->alert);

		if (sending)
			ret = tls13_handshake_send_action(ctx, action);
		else
			ret = tls13_handshake_recv_action(ctx, action);

		if (ctx->alert != 0)
			return tls13_send_alert(ctx->rl, ctx->alert);

		if (ret <= 0) {
			DEBUGF("%s %s returned %d\n",
			    tls13_handshake_mode_name(ctx->mode),
			    (action->sender == ctx->mode) ? "send" : "recv",
			    ret);
			return ret;
		}

		if (!tls13_handshake_legacy_info_callback(ctx))
			return TLS13_IO_FAILURE;

		if (!tls13_handshake_advance_state_machine(ctx))
			return TLS13_IO_FAILURE;

		if (sending)
			ctx->need_flush = tls13_handshake_end_of_flight(ctx,
			    action);

		if (!tls13_handshake_set_legacy_state(ctx))
			return TLS13_IO_FAILURE;
	}
}

static int
tls13_handshake_send_action(struct tls13_ctx *ctx,
    const struct tls13_handshake_action *action)
{
	ssize_t ret;
	CBB cbb;

	if (ctx->send_dummy_ccs) {
		if ((ret = tls13_send_dummy_ccs(ctx->rl)) != TLS13_IO_SUCCESS)
			return ret;
		ctx->send_dummy_ccs = 0;
		if (ctx->send_dummy_ccs_after) {
			ctx->send_dummy_ccs_after = 0;
			return TLS13_IO_SUCCESS;
		}
	}

	/* If we have no handshake message, we need to build one. */
	if (ctx->hs_msg == NULL) {
		if ((ctx->hs_msg = tls13_handshake_msg_new()) == NULL)
			return TLS13_IO_FAILURE;
		if (!tls13_handshake_msg_start(ctx->hs_msg, &cbb,
		    action->handshake_type))
			return TLS13_IO_FAILURE;
		if (!action->send(ctx, &cbb))
			return TLS13_IO_FAILURE;
		if (!tls13_handshake_msg_finish(ctx->hs_msg))
			return TLS13_IO_FAILURE;
	}

	if ((ret = tls13_handshake_msg_send(ctx->hs_msg, ctx->rl)) <= 0)
		return ret;

	if (!tls13_handshake_msg_record(ctx))
		return TLS13_IO_FAILURE;

	if (action->send_preserve_transcript_hash) {
		if (!tls1_transcript_hash_value(ctx->ssl,
		    ctx->hs->tls13.transcript_hash,
		    sizeof(ctx->hs->tls13.transcript_hash),
		    &ctx->hs->tls13.transcript_hash_len))
			return TLS13_IO_FAILURE;
	}

	if (ctx->handshake_message_sent_cb != NULL)
		ctx->handshake_message_sent_cb(ctx);

	tls13_handshake_msg_free(ctx->hs_msg);
	ctx->hs_msg = NULL;

	if (action->sent != NULL && !action->sent(ctx))
		return TLS13_IO_FAILURE;

	if (ctx->send_dummy_ccs_after) {
		ctx->send_dummy_ccs = 1;
		if ((ret = tls13_send_dummy_ccs(ctx->rl)) != TLS13_IO_SUCCESS)
			return ret;
		ctx->send_dummy_ccs = 0;
		ctx->send_dummy_ccs_after = 0;
	}

	return TLS13_IO_SUCCESS;
}

static int
tls13_handshake_recv_action(struct tls13_ctx *ctx,
    const struct tls13_handshake_action *action)
{
	uint8_t msg_type;
	ssize_t ret;
	CBS cbs;

	if (ctx->hs_msg == NULL) {
		if ((ctx->hs_msg = tls13_handshake_msg_new()) == NULL)
			return TLS13_IO_FAILURE;
	}

	if ((ret = tls13_handshake_msg_recv(ctx->hs_msg, ctx->rl)) <= 0)
		return ret;

	if (action->recv_preserve_transcript_hash) {
		if (!tls1_transcript_hash_value(ctx->ssl,
		    ctx->hs->tls13.transcript_hash,
		    sizeof(ctx->hs->tls13.transcript_hash),
		    &ctx->hs->tls13.transcript_hash_len))
			return TLS13_IO_FAILURE;
	}

	if (!tls13_handshake_msg_record(ctx))
		return TLS13_IO_FAILURE;

	if (ctx->handshake_message_recv_cb != NULL)
		ctx->handshake_message_recv_cb(ctx);

	/*
	 * In TLSv1.3 there is no way to know if you're going to receive a
	 * certificate request message or not, hence we have to special case it
	 * here. The receive handler also knows how to deal with this situation.
	 */
	msg_type = tls13_handshake_msg_type(ctx->hs_msg);
	if (msg_type != action->handshake_type &&
	    (msg_type != TLS13_MT_CERTIFICATE ||
	     action->handshake_type != TLS13_MT_CERTIFICATE_REQUEST))
		return tls13_send_alert(ctx->rl, TLS13_ALERT_UNEXPECTED_MESSAGE);

	if (!tls13_handshake_msg_content(ctx->hs_msg, &cbs))
		return TLS13_IO_FAILURE;

	ret = TLS13_IO_FAILURE;
	if (!action->recv(ctx, &cbs))
		goto err;

	if (CBS_len(&cbs) != 0) {
		tls13_set_errorx(ctx, TLS13_ERR_TRAILING_DATA, 0,
		    "trailing data in handshake message", NULL);
		ctx->alert = TLS13_ALERT_DECODE_ERROR;
		goto err;
	}

	ret = TLS13_IO_SUCCESS;
	if (ctx->ssl->method->version < TLS1_3_VERSION)
		ret = TLS13_IO_USE_LEGACY;

 err:
	tls13_handshake_msg_free(ctx->hs_msg);
	ctx->hs_msg = NULL;

	return ret;
}

struct tls13_handshake_legacy_state {
	int recv;
	int send;
};

static const struct tls13_handshake_legacy_state legacy_states[] = {
	[CLIENT_HELLO] = {
		.recv = SSL3_ST_SR_CLNT_HELLO_A,
		.send = SSL3_ST_CW_CLNT_HELLO_A,
	},
	[SERVER_HELLO_RETRY_REQUEST] = {
		.recv = SSL3_ST_CR_SRVR_HELLO_A,
		.send = SSL3_ST_SW_SRVR_HELLO_A,
	},
	[CLIENT_HELLO_RETRY] = {
		.recv = SSL3_ST_SR_CLNT_HELLO_A,
		.send = SSL3_ST_CW_CLNT_HELLO_A,
	},
	[SERVER_HELLO] = {
		.recv = SSL3_ST_CR_SRVR_HELLO_A,
		.send = SSL3_ST_SW_SRVR_HELLO_A,
	},
	[SERVER_ENCRYPTED_EXTENSIONS] = {
		.send = 0,
		.recv = 0,
	},
	[SERVER_CERTIFICATE_REQUEST] = {
		.recv = SSL3_ST_CR_CERT_REQ_A,
		.send = SSL3_ST_SW_CERT_REQ_A,
	},
	[SERVER_CERTIFICATE] = {
		.recv = SSL3_ST_CR_CERT_A,
		.send = SSL3_ST_SW_CERT_A,
	},
	[SERVER_CERTIFICATE_VERIFY] = {
		.send = 0,
		.recv = 0,
	},
	[SERVER_FINISHED] = {
		.recv = SSL3_ST_CR_FINISHED_A,
		.send = SSL3_ST_SW_FINISHED_A,
	},
	[CLIENT_END_OF_EARLY_DATA] = {
		.send = 0,
		.recv = 0,
	},
	[CLIENT_CERTIFICATE] = {
		.recv = SSL3_ST_SR_CERT_VRFY_A,
		.send = SSL3_ST_CW_CERT_VRFY_B,
	},
	[CLIENT_CERTIFICATE_VERIFY] = {
		.send = 0,
		.recv = 0,
	},
	[CLIENT_FINISHED] = {
		.recv = SSL3_ST_SR_FINISHED_A,
		.send = SSL3_ST_CW_FINISHED_A,
	},
	[APPLICATION_DATA] = {
		.recv = 0,
		.send = 0,
	},
};

CTASSERT(sizeof(state_machine) / sizeof(state_machine[0]) ==
    sizeof(legacy_states) / sizeof(legacy_states[0]));

static int
tls13_handshake_legacy_state(struct tls13_ctx *ctx, int *out_state)
{
	const struct tls13_handshake_action *action;
	enum tls13_message_type mt;

	*out_state = 0;

	if (!ctx->handshake_started) {
		if (ctx->mode == TLS13_HS_CLIENT)
			*out_state = SSL_ST_CONNECT;
		else
			*out_state = SSL_ST_ACCEPT;

		return 1;
	}

	if (ctx->handshake_completed) {
		*out_state = SSL_ST_OK;
		return 1;
	}

	if ((mt = tls13_handshake_active_state(ctx)) == INVALID)
		return 0;

	if ((action = tls13_handshake_active_action(ctx)) == NULL)
		return 0;

	if (action->sender == ctx->mode)
		*out_state = legacy_states[mt].send;
	else
		*out_state = legacy_states[mt].recv;

	return 1;
}

static int
tls13_handshake_info_position(struct tls13_ctx *ctx)
{
	if (!ctx->handshake_started)
		return TLS13_INFO_HANDSHAKE_STARTED;

	if (ctx->handshake_completed)
		return TLS13_INFO_HANDSHAKE_COMPLETED;

	if (ctx->mode == TLS13_HS_CLIENT)
		return TLS13_INFO_CONNECT_LOOP;
	else
		return TLS13_INFO_ACCEPT_LOOP;
}

static int
tls13_handshake_legacy_info_callback(struct tls13_ctx *ctx)
{
	int state, where;

	if (!tls13_handshake_legacy_state(ctx, &state))
		return 0;

	/* Do nothing if there's no corresponding legacy state. */
	if (state == 0)
		return 1;

	if (ctx->info_cb != NULL) {
		where = tls13_handshake_info_position(ctx);
		ctx->info_cb(ctx, where, 1);
	}

	return 1;
}

static int
tls13_handshake_set_legacy_state(struct tls13_ctx *ctx)
{
	int state;

	if (!tls13_handshake_legacy_state(ctx, &state))
		return 0;

	/* Do nothing if there's no corresponding legacy state. */
	if (state == 0)
		return 1;

	ctx->hs->state = state;

	return 1;
}
