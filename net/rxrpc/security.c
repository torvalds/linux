// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC security handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/crypto.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <keys/rxrpc-type.h>
#include "ar-internal.h"

static const struct rxrpc_security *rxrpc_security_types[] = {
	[RXRPC_SECURITY_NONE]	= &rxrpc_no_security,
#ifdef CONFIG_RXKAD
	[RXRPC_SECURITY_RXKAD]	= &rxkad,
#endif
};

int __init rxrpc_init_security(void)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(rxrpc_security_types); i++) {
		if (rxrpc_security_types[i]) {
			ret = rxrpc_security_types[i]->init();
			if (ret < 0)
				goto failed;
		}
	}

	return 0;

failed:
	for (i--; i >= 0; i--)
		if (rxrpc_security_types[i])
			rxrpc_security_types[i]->exit();
	return ret;
}

void rxrpc_exit_security(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rxrpc_security_types); i++)
		if (rxrpc_security_types[i])
			rxrpc_security_types[i]->exit();
}

/*
 * look up an rxrpc security module
 */
static const struct rxrpc_security *rxrpc_security_lookup(u8 security_index)
{
	if (security_index >= ARRAY_SIZE(rxrpc_security_types))
		return NULL;
	return rxrpc_security_types[security_index];
}

/*
 * initialise the security on a client connection
 */
int rxrpc_init_client_conn_security(struct rxrpc_connection *conn)
{
	const struct rxrpc_security *sec;
	struct rxrpc_key_token *token;
	struct key *key = conn->params.key;
	int ret;

	_enter("{%d},{%x}", conn->debug_id, key_serial(key));

	if (!key)
		return 0;

	ret = key_validate(key);
	if (ret < 0)
		return ret;

	token = key->payload.data[0];
	if (!token)
		return -EKEYREJECTED;

	sec = rxrpc_security_lookup(token->security_index);
	if (!sec)
		return -EKEYREJECTED;
	conn->security = sec;

	ret = conn->security->init_connection_security(conn);
	if (ret < 0) {
		conn->security = &rxrpc_no_security;
		return ret;
	}

	_leave(" = 0");
	return 0;
}

/*
 * Find the security key for a server connection.
 */
bool rxrpc_look_up_server_security(struct rxrpc_local *local, struct rxrpc_sock *rx,
				   const struct rxrpc_security **_sec,
				   struct key **_key,
				   struct sk_buff *skb)
{
	const struct rxrpc_security *sec;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	key_ref_t kref = NULL;
	char kdesc[5 + 1 + 3 + 1];

	_enter("");

	sprintf(kdesc, "%u:%u", sp->hdr.serviceId, sp->hdr.securityIndex);

	sec = rxrpc_security_lookup(sp->hdr.securityIndex);
	if (!sec) {
		trace_rxrpc_abort(0, "SVS",
				  sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
				  RX_INVALID_OPERATION, EKEYREJECTED);
		skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
		skb->priority = RX_INVALID_OPERATION;
		return false;
	}

	if (sp->hdr.securityIndex == RXRPC_SECURITY_NONE)
		goto out;

	if (!rx->securities) {
		trace_rxrpc_abort(0, "SVR",
				  sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
				  RX_INVALID_OPERATION, EKEYREJECTED);
		skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
		skb->priority = RX_INVALID_OPERATION;
		return false;
	}

	/* look through the service's keyring */
	kref = keyring_search(make_key_ref(rx->securities, 1UL),
			      &key_type_rxrpc_s, kdesc, true);
	if (IS_ERR(kref)) {
		trace_rxrpc_abort(0, "SVK",
				  sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
				  sec->no_key_abort, EKEYREJECTED);
		skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
		skb->priority = sec->no_key_abort;
		return false;
	}

out:
	*_sec = sec;
	*_key = key_ref_to_ptr(kref);
	return true;
}
