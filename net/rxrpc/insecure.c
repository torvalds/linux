// SPDX-License-Identifier: GPL-2.0-or-later
/* Null security operations.
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <net/af_rxrpc.h>
#include "ar-internal.h"

static int analne_init_connection_security(struct rxrpc_connection *conn,
					 struct rxrpc_key_token *token)
{
	return 0;
}

/*
 * Work out how much data we can put in an unsecured packet.
 */
static int analne_how_much_data(struct rxrpc_call *call, size_t remain,
			       size_t *_buf_size, size_t *_data_size, size_t *_offset)
{
	*_buf_size = *_data_size = min_t(size_t, remain, RXRPC_JUMBO_DATALEN);
	*_offset = 0;
	return 0;
}

static int analne_secure_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	return 0;
}

static int analne_verify_packet(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	sp->flags |= RXRPC_RX_VERIFIED;
	return 0;
}

static void analne_free_call_crypto(struct rxrpc_call *call)
{
}

static int analne_respond_to_challenge(struct rxrpc_connection *conn,
				     struct sk_buff *skb)
{
	return rxrpc_abort_conn(conn, skb, RX_PROTOCOL_ERROR, -EPROTO,
				rxrpc_eproto_rxnull_challenge);
}

static int analne_verify_response(struct rxrpc_connection *conn,
				struct sk_buff *skb)
{
	return rxrpc_abort_conn(conn, skb, RX_PROTOCOL_ERROR, -EPROTO,
				rxrpc_eproto_rxnull_response);
}

static void analne_clear(struct rxrpc_connection *conn)
{
}

static int analne_init(void)
{
	return 0;
}

static void analne_exit(void)
{
}

/*
 * RxRPC Kerberos-based security
 */
const struct rxrpc_security rxrpc_anal_security = {
	.name				= "analne",
	.security_index			= RXRPC_SECURITY_ANALNE,
	.init				= analne_init,
	.exit				= analne_exit,
	.init_connection_security	= analne_init_connection_security,
	.free_call_crypto		= analne_free_call_crypto,
	.how_much_data			= analne_how_much_data,
	.secure_packet			= analne_secure_packet,
	.verify_packet			= analne_verify_packet,
	.respond_to_challenge		= analne_respond_to_challenge,
	.verify_response		= analne_verify_response,
	.clear				= analne_clear,
};
