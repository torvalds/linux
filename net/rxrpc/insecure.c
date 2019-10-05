// SPDX-License-Identifier: GPL-2.0-or-later
/* Null security operations.
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <net/af_rxrpc.h>
#include "ar-internal.h"

static int none_init_connection_security(struct rxrpc_connection *conn)
{
	return 0;
}

static int none_prime_packet_security(struct rxrpc_connection *conn)
{
	return 0;
}

static int none_secure_packet(struct rxrpc_call *call,
			      struct sk_buff *skb,
			      size_t data_size,
			      void *sechdr)
{
	return 0;
}

static int none_verify_packet(struct rxrpc_call *call, struct sk_buff *skb,
			      unsigned int offset, unsigned int len,
			      rxrpc_seq_t seq, u16 expected_cksum)
{
	return 0;
}

static void none_free_call_crypto(struct rxrpc_call *call)
{
}

static void none_locate_data(struct rxrpc_call *call, struct sk_buff *skb,
			     unsigned int *_offset, unsigned int *_len)
{
}

static int none_respond_to_challenge(struct rxrpc_connection *conn,
				     struct sk_buff *skb,
				     u32 *_abort_code)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
			      tracepoint_string("chall_none"));
	return -EPROTO;
}

static int none_verify_response(struct rxrpc_connection *conn,
				struct sk_buff *skb,
				u32 *_abort_code)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
			      tracepoint_string("resp_none"));
	return -EPROTO;
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
	.prime_packet_security		= none_prime_packet_security,
	.free_call_crypto		= none_free_call_crypto,
	.secure_packet			= none_secure_packet,
	.verify_packet			= none_verify_packet,
	.locate_data			= none_locate_data,
	.respond_to_challenge		= none_respond_to_challenge,
	.verify_response		= none_verify_response,
	.clear				= none_clear,
};
