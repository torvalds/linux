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
#ifdef CONFIG_RXGK
	[RXRPC_SECURITY_YFS_RXGK] = &rxgk_yfs,
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
const struct rxrpc_security *rxrpc_security_lookup(u8 security_index)
{
	if (security_index >= ARRAY_SIZE(rxrpc_security_types))
		return NULL;
	return rxrpc_security_types[security_index];
}

/*
 * Initialise the security on a client call.
 */
int rxrpc_init_client_call_security(struct rxrpc_call *call)
{
	const struct rxrpc_security *sec = &rxrpc_no_security;
	struct rxrpc_key_token *token;
	struct key *key = call->key;
	int ret;

	if (!key)
		goto found;

	ret = key_validate(key);
	if (ret < 0)
		return ret;

	for (token = key->payload.data[0]; token; token = token->next) {
		sec = rxrpc_security_lookup(token->security_index);
		if (sec)
			goto found;
	}
	return -EKEYREJECTED;

found:
	call->security = sec;
	call->security_ix = sec->security_index;
	return 0;
}

/*
 * initialise the security on a client connection
 */
int rxrpc_init_client_conn_security(struct rxrpc_connection *conn)
{
	struct rxrpc_key_token *token;
	struct key *key = conn->key;
	int ret = 0;

	_enter("{%d},{%x}", conn->debug_id, key_serial(key));

	for (token = key->payload.data[0]; token; token = token->next) {
		if (token->security_index == conn->security->security_index)
			goto found;
	}
	return -EKEYREJECTED;

found:
	mutex_lock(&conn->security_lock);
	if (conn->state == RXRPC_CONN_CLIENT_UNSECURED) {
		ret = conn->security->init_connection_security(conn, token);
		if (ret == 0) {
			spin_lock_irq(&conn->state_lock);
			if (conn->state == RXRPC_CONN_CLIENT_UNSECURED)
				conn->state = RXRPC_CONN_CLIENT;
			spin_unlock_irq(&conn->state_lock);
		}
	}
	mutex_unlock(&conn->security_lock);
	return ret;
}

/*
 * Set the ops a server connection.
 */
const struct rxrpc_security *rxrpc_get_incoming_security(struct rxrpc_sock *rx,
							 struct sk_buff *skb)
{
	const struct rxrpc_security *sec;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	_enter("");

	sec = rxrpc_security_lookup(sp->hdr.securityIndex);
	if (!sec) {
		rxrpc_direct_conn_abort(skb, rxrpc_abort_unsupported_security,
					RX_INVALID_OPERATION, -EKEYREJECTED);
		return NULL;
	}

	if (sp->hdr.securityIndex != RXRPC_SECURITY_NONE &&
	    !rx->securities) {
		rxrpc_direct_conn_abort(skb, rxrpc_abort_no_service_key,
					sec->no_key_abort, -EKEYREJECTED);
		return NULL;
	}

	return sec;
}

/*
 * Find the security key for a server connection.
 */
struct key *rxrpc_look_up_server_security(struct rxrpc_connection *conn,
					  struct sk_buff *skb,
					  u32 kvno, u32 enctype)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_sock *rx;
	struct key *key = ERR_PTR(-EKEYREJECTED);
	key_ref_t kref = NULL;
	char kdesc[5 + 1 + 3 + 1 + 12 + 1 + 12 + 1];
	int ret;

	_enter("");

	if (enctype)
		sprintf(kdesc, "%u:%u:%u:%u",
			sp->hdr.serviceId, sp->hdr.securityIndex, kvno, enctype);
	else if (kvno)
		sprintf(kdesc, "%u:%u:%u",
			sp->hdr.serviceId, sp->hdr.securityIndex, kvno);
	else
		sprintf(kdesc, "%u:%u",
			sp->hdr.serviceId, sp->hdr.securityIndex);

	read_lock(&conn->local->services_lock);

	rx = conn->local->service;
	if (!rx)
		goto out;

	/* look through the service's keyring */
	kref = keyring_search(make_key_ref(rx->securities, 1UL),
			      &key_type_rxrpc_s, kdesc, true);
	if (IS_ERR(kref)) {
		key = ERR_CAST(kref);
		goto out;
	}

	key = key_ref_to_ptr(kref);

	ret = key_validate(key);
	if (ret < 0) {
		key_put(key);
		key = ERR_PTR(ret);
		goto out;
	}

out:
	read_unlock(&conn->local->services_lock);
	return key;
}
