// SPDX-License-Identifier: GPL-2.0-or-later
/* Null security operations.
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <net/af_rxrpc.h>
#include "ar-internal.h"

static int none_init_connection_security(struct rxrpc_connection *conn,
					 struct rxrpc_key_token *token)
{
	return 0;
}

/*
 * Allocate an appropriately sized buffer for the amount of data remaining.
 */
static struct rxrpc_txbuf *none_alloc_txbuf(struct rxrpc_call *call, size_t remain, gfp_t gfp)
{
	return rxrpc_alloc_data_txbuf(call, min_t(size_t, remain, RXRPC_JUMBO_DATALEN), 1, gfp);
}

static int none_secure_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	return 0;
}

static int none_verify_packet(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	sp->flags |= RXRPC_RX_VERIFIED;
	return 0;
}

static void none_free_call_crypto(struct rxrpc_call *call)
{
}

static int none_respond_to_challenge(struct rxrpc_connection *conn,
				     struct sk_buff *skb)
{
	return rxrpc_abort_conn(conn, skb, RX_PROTOCOL_ERROR, -EPROTO,
				rxrpc_eproto_rxnull_challenge);
}

static int none_verify_response(struct rxrpc_connection *conn,
				struct sk_buff *skb)
{
	return rxrpc_abort_conn(conn, skb, RX_PROTOCOL_ERROR, -EPROTO,
				rxrpc_eproto_rxnull_response);
}

static void none_clear(struct rxrpc_connection *conn)
{
}

static int none_init(void)
{
	return 0;
}

static void none_exit(void)
{
}

/*
 * RxRPC Kerberos-based security
 */
const struct rxrpc_security rxrpc_no_security = {
	.name				= "none",
	.security_index			= RXRPC_SECURITY_NONE,
	.init				= none_init,
	.exit				= none_exit,
	.init_connection_security	= none_init_connection_security,
	.free_call_crypto		= none_free_call_crypto,
	.alloc_txbuf			= none_alloc_txbuf,
	.secure_packet			= none_secure_packet,
	.verify_packet			= none_verify_packet,
	.respond_to_challenge		= none_respond_to_challenge,
	.verify_response		= none_verify_response,
	.clear				= none_clear,
};
