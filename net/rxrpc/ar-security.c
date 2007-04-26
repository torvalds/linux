/* RxRPC security handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/crypto.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static LIST_HEAD(rxrpc_security_methods);
static DECLARE_RWSEM(rxrpc_security_sem);

/*
 * get an RxRPC security module
 */
static struct rxrpc_security *rxrpc_security_get(struct rxrpc_security *sec)
{
	return try_module_get(sec->owner) ? sec : NULL;
}

/*
 * release an RxRPC security module
 */
static void rxrpc_security_put(struct rxrpc_security *sec)
{
	module_put(sec->owner);
}

/*
 * look up an rxrpc security module
 */
struct rxrpc_security *rxrpc_security_lookup(u8 security_index)
{
	struct rxrpc_security *sec = NULL;

	_enter("");

	down_read(&rxrpc_security_sem);

	list_for_each_entry(sec, &rxrpc_security_methods, link) {
		if (sec->security_index == security_index) {
			if (unlikely(!rxrpc_security_get(sec)))
				break;
			goto out;
		}
	}

	sec = NULL;
out:
	up_read(&rxrpc_security_sem);
	_leave(" = %p [%s]", sec, sec ? sec->name : "");
	return sec;
}

/**
 * rxrpc_register_security - register an RxRPC security handler
 * @sec: security module
 *
 * register an RxRPC security handler for use by RxRPC
 */
int rxrpc_register_security(struct rxrpc_security *sec)
{
	struct rxrpc_security *psec;
	int ret;

	_enter("");
	down_write(&rxrpc_security_sem);

	ret = -EEXIST;
	list_for_each_entry(psec, &rxrpc_security_methods, link) {
		if (psec->security_index == sec->security_index)
			goto out;
	}

	list_add(&sec->link, &rxrpc_security_methods);

	printk(KERN_NOTICE "RxRPC: Registered security type %d '%s'\n",
	       sec->security_index, sec->name);
	ret = 0;

out:
	up_write(&rxrpc_security_sem);
	_leave(" = %d", ret);
	return ret;
}

EXPORT_SYMBOL_GPL(rxrpc_register_security);

/**
 * rxrpc_unregister_security - unregister an RxRPC security handler
 * @sec: security module
 *
 * unregister an RxRPC security handler
 */
void rxrpc_unregister_security(struct rxrpc_security *sec)
{

	_enter("");
	down_write(&rxrpc_security_sem);
	list_del_init(&sec->link);
	up_write(&rxrpc_security_sem);

	printk(KERN_NOTICE "RxRPC: Unregistered security type %d '%s'\n",
	       sec->security_index, sec->name);
}

EXPORT_SYMBOL_GPL(rxrpc_unregister_security);

/*
 * initialise the security on a client connection
 */
int rxrpc_init_client_conn_security(struct rxrpc_connection *conn)
{
	struct rxrpc_security *sec;
	struct key *key = conn->key;
	int ret;

	_enter("{%d},{%x}", conn->debug_id, key_serial(key));

	if (!key)
		return 0;

	ret = key_validate(key);
	if (ret < 0)
		return ret;

	sec = rxrpc_security_lookup(key->type_data.x[0]);
	if (!sec)
		return -EKEYREJECTED;
	conn->security = sec;

	ret = conn->security->init_connection_security(conn);
	if (ret < 0) {
		rxrpc_security_put(conn->security);
		conn->security = NULL;
		return ret;
	}

	_leave(" = 0");
	return 0;
}

/*
 * initialise the security on a server connection
 */
int rxrpc_init_server_conn_security(struct rxrpc_connection *conn)
{
	struct rxrpc_security *sec;
	struct rxrpc_local *local = conn->trans->local;
	struct rxrpc_sock *rx;
	struct key *key;
	key_ref_t kref;
	char kdesc[5+1+3+1];

	_enter("");

	sprintf(kdesc, "%u:%u", ntohs(conn->service_id), conn->security_ix);

	sec = rxrpc_security_lookup(conn->security_ix);
	if (!sec) {
		_leave(" = -ENOKEY [lookup]");
		return -ENOKEY;
	}

	/* find the service */
	read_lock_bh(&local->services_lock);
	list_for_each_entry(rx, &local->services, listen_link) {
		if (rx->service_id == conn->service_id)
			goto found_service;
	}

	/* the service appears to have died */
	read_unlock_bh(&local->services_lock);
	rxrpc_security_put(sec);
	_leave(" = -ENOENT");
	return -ENOENT;

found_service:
	if (!rx->securities) {
		read_unlock_bh(&local->services_lock);
		rxrpc_security_put(sec);
		_leave(" = -ENOKEY");
		return -ENOKEY;
	}

	/* look through the service's keyring */
	kref = keyring_search(make_key_ref(rx->securities, 1UL),
			      &key_type_rxrpc_s, kdesc);
	if (IS_ERR(kref)) {
		read_unlock_bh(&local->services_lock);
		rxrpc_security_put(sec);
		_leave(" = %ld [search]", PTR_ERR(kref));
		return PTR_ERR(kref);
	}

	key = key_ref_to_ptr(kref);
	read_unlock_bh(&local->services_lock);

	conn->server_key = key;
	conn->security = sec;

	_leave(" = 0");
	return 0;
}

/*
 * secure a packet prior to transmission
 */
int rxrpc_secure_packet(const struct rxrpc_call *call,
			struct sk_buff *skb,
			size_t data_size,
			void *sechdr)
{
	if (call->conn->security)
		return call->conn->security->secure_packet(
			call, skb, data_size, sechdr);
	return 0;
}

/*
 * secure a packet prior to transmission
 */
int rxrpc_verify_packet(const struct rxrpc_call *call, struct sk_buff *skb,
			u32 *_abort_code)
{
	if (call->conn->security)
		return call->conn->security->verify_packet(
			call, skb, _abort_code);
	return 0;
}

/*
 * clear connection security
 */
void rxrpc_clear_conn_security(struct rxrpc_connection *conn)
{
	_enter("{%d}", conn->debug_id);

	if (conn->security) {
		conn->security->clear(conn);
		rxrpc_security_put(conn->security);
		conn->security = NULL;
	}

	key_put(conn->key);
	key_put(conn->server_key);
}
