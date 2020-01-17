// SPDX-License-Identifier: GPL-2.0-or-later
/* Null security operations.
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <net/af_rxrpc.h>
#include "ar-internal.h"

static int yesne_init_connection_security(struct rxrpc_connection *conn)
{
	return 0;
}

static int yesne_prime_packet_security(struct rxrpc_connection *conn)
{
	return 0;
}

static int yesne_secure_packet(struct rxrpc_call *call,
			      struct sk_buff *skb,
			      size_t data_size,
			      void *sechdr)
{
	return 0;
}

static int yesne_verify_packet(struct rxrpc_call *call, struct sk_buff *skb,
			      unsigned int offset, unsigned int len,
			      rxrpc_seq_t seq, u16 expected_cksum)
{
	return 0;
}

static void yesne_free_call_crypto(struct rxrpc_call *call)
{
}

static void yesne_locate_data(struct rxrpc_call *call, struct sk_buff *skb,
			     unsigned int *_offset, unsigned int *_len)
{
}

static int yesne_respond_to_challenge(struct rxrpc_connection *conn,
				     struct sk_buff *skb,
				     u32 *_abort_code)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
			      tracepoint_string("chall_yesne"));
	return -EPROTO;
}

static int yesne_verify_response(struct rxrpc_connection *conn,
				struct sk_buff *skb,
				u32 *_abort_code)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
			      tracepoint_string("resp_yesne"));
	return -EPROTO;
}

static void yesne_clear(struct rxrpc_connection *conn)
{
}

static int yesne_init(void)
{
	return 0;
}

static void yesne_exit(void)
{
}

/*
 * RxRPC Kerberos-based security
 */
const struct rxrpc_security rxrpc_yes_security = {
	.name				= "yesne",
	.security_index			= RXRPC_SECURITY_NONE,
	.init				= yesne_init,
	.exit				= yesne_exit,
	.init_connection_security	= yesne_init_connection_security,
	.prime_packet_security		= yesne_prime_packet_security,
	.free_call_crypto		= yesne_free_call_crypto,
	.secure_packet			= yesne_secure_packet,
	.verify_packet			= yesne_verify_packet,
	.locate_data			= yesne_locate_data,
	.respond_to_challenge		= yesne_respond_to_challenge,
	.verify_response		= yesne_verify_response,
	.clear				= yesne_clear,
};
