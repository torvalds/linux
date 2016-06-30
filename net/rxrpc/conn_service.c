/* Service connection management
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/slab.h>
#include "ar-internal.h"

/*
 * get a record of an incoming connection
 */
struct rxrpc_connection *rxrpc_incoming_connection(struct rxrpc_local *local,
						   struct sockaddr_rxrpc *srx,
						   struct sk_buff *skb)
{
	struct rxrpc_connection *conn, *candidate = NULL;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_peer *peer;
	struct rb_node *p, **pp;
	const char *new = "old";
	u32 epoch, cid;

	_enter("");

	peer = rxrpc_lookup_peer(local, srx, GFP_NOIO);
	if (!peer) {
		_debug("no peer");
		return ERR_PTR(-EBUSY);
	}

	ASSERT(sp->hdr.flags & RXRPC_CLIENT_INITIATED);

	epoch = sp->hdr.epoch;
	cid = sp->hdr.cid & RXRPC_CIDMASK;

	/* search the connection list first */
	read_lock_bh(&peer->conn_lock);

	p = peer->service_conns.rb_node;
	while (p) {
		conn = rb_entry(p, struct rxrpc_connection, service_node);

		_debug("maybe %x", conn->proto.cid);

		if (epoch < conn->proto.epoch)
			p = p->rb_left;
		else if (epoch > conn->proto.epoch)
			p = p->rb_right;
		else if (cid < conn->proto.cid)
			p = p->rb_left;
		else if (cid > conn->proto.cid)
			p = p->rb_right;
		else
			goto found_extant_connection;
	}
	read_unlock_bh(&peer->conn_lock);

	/* not yet present - create a candidate for a new record and then
	 * redo the search */
	candidate = rxrpc_alloc_connection(GFP_NOIO);
	if (!candidate) {
		rxrpc_put_peer(peer);
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	candidate->proto.epoch		= sp->hdr.epoch;
	candidate->proto.cid		= sp->hdr.cid & RXRPC_CIDMASK;
	candidate->params.local		= local;
	candidate->params.peer		= peer;
	candidate->params.service_id	= sp->hdr.serviceId;
	candidate->security_ix		= sp->hdr.securityIndex;
	candidate->out_clientflag	= 0;
	candidate->state		= RXRPC_CONN_SERVICE;
	if (candidate->params.service_id)
		candidate->state	= RXRPC_CONN_SERVICE_UNSECURED;

	write_lock_bh(&peer->conn_lock);

	pp = &peer->service_conns.rb_node;
	p = NULL;
	while (*pp) {
		p = *pp;
		conn = rb_entry(p, struct rxrpc_connection, service_node);

		if (epoch < conn->proto.epoch)
			pp = &(*pp)->rb_left;
		else if (epoch > conn->proto.epoch)
			pp = &(*pp)->rb_right;
		else if (cid < conn->proto.cid)
			pp = &(*pp)->rb_left;
		else if (cid > conn->proto.cid)
			pp = &(*pp)->rb_right;
		else
			goto found_extant_second;
	}

	/* we can now add the new candidate to the list */
	set_bit(RXRPC_CONN_IN_SERVICE_CONNS, &candidate->flags);
	rb_link_node(&candidate->service_node, p, pp);
	rb_insert_color(&candidate->service_node, &peer->service_conns);
attached:
	conn = candidate;
	candidate = NULL;
	rxrpc_get_peer(peer);
	rxrpc_get_local(local);

	write_unlock_bh(&peer->conn_lock);

	write_lock(&rxrpc_connection_lock);
	list_add_tail(&conn->link, &rxrpc_connections);
	write_unlock(&rxrpc_connection_lock);

	new = "new";

success:
	_net("CONNECTION %s %d {%x}", new, conn->debug_id, conn->proto.cid);

	rxrpc_put_peer(peer);
	_leave(" = %p {u=%d}", conn, atomic_read(&conn->usage));
	return conn;

	/* we found the connection in the list immediately */
found_extant_connection:
	if (!rxrpc_get_connection_maybe(conn)) {
		set_bit(RXRPC_CONN_IN_SERVICE_CONNS, &candidate->flags);
		rb_replace_node(&conn->service_node,
				&candidate->service_node,
				&peer->service_conns);
		clear_bit(RXRPC_CONN_IN_SERVICE_CONNS, &conn->flags);
		goto attached;
	}

	if (sp->hdr.securityIndex != conn->security_ix) {
		read_unlock_bh(&peer->conn_lock);
		goto security_mismatch_put;
	}
	read_unlock_bh(&peer->conn_lock);
	goto success;

	/* we found the connection on the second time through the list */
found_extant_second:
	if (sp->hdr.securityIndex != conn->security_ix) {
		write_unlock_bh(&peer->conn_lock);
		goto security_mismatch;
	}
	rxrpc_get_connection(conn);
	write_unlock_bh(&peer->conn_lock);
	kfree(candidate);
	goto success;

security_mismatch_put:
	rxrpc_put_connection(conn);
security_mismatch:
	kfree(candidate);
	_leave(" = -EKEYREJECTED");
	return ERR_PTR(-EKEYREJECTED);
}

/*
 * Remove the service connection from the peer's tree, thereby removing it as a
 * target for incoming packets.
 */
void rxrpc_unpublish_service_conn(struct rxrpc_connection *conn)
{
	struct rxrpc_peer *peer = conn->params.peer;

	write_lock_bh(&peer->conn_lock);
	if (test_and_clear_bit(RXRPC_CONN_IN_SERVICE_CONNS, &conn->flags))
		rb_erase(&conn->service_node, &peer->service_conns);
	write_unlock_bh(&peer->conn_lock);
}
