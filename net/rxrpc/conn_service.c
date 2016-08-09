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
 * Find a service connection under RCU conditions.
 *
 * We could use a hash table, but that is subject to bucket stuffing by an
 * attacker as the client gets to pick the epoch and cid values and would know
 * the hash function.  So, instead, we use a hash table for the peer and from
 * that an rbtree to find the service connection.  Under ordinary circumstances
 * it might be slower than a large hash table, but it is at least limited in
 * depth.
 */
struct rxrpc_connection *rxrpc_find_service_conn_rcu(struct rxrpc_peer *peer,
						     struct sk_buff *skb)
{
	struct rxrpc_connection *conn = NULL;
	struct rxrpc_conn_proto k;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rb_node *p;
	unsigned int seq = 0;

	k.epoch	= sp->hdr.epoch;
	k.cid	= sp->hdr.cid & RXRPC_CIDMASK;

	do {
		/* Unfortunately, rbtree walking doesn't give reliable results
		 * under just the RCU read lock, so we have to check for
		 * changes.
		 */
		read_seqbegin_or_lock(&peer->service_conn_lock, &seq);

		p = rcu_dereference_raw(peer->service_conns.rb_node);
		while (p) {
			conn = rb_entry(p, struct rxrpc_connection, service_node);

			if (conn->proto.index_key < k.index_key)
				p = rcu_dereference_raw(p->rb_left);
			else if (conn->proto.index_key > k.index_key)
				p = rcu_dereference_raw(p->rb_right);
			else
				goto done;
			conn = NULL;
		}
	} while (need_seqretry(&peer->service_conn_lock, seq));

done:
	done_seqretry(&peer->service_conn_lock, seq);
	_leave(" = %d", conn ? conn->debug_id : -1);
	return conn;
}

/*
 * Insert a service connection into a peer's tree, thereby making it a target
 * for incoming packets.
 */
static struct rxrpc_connection *
rxrpc_publish_service_conn(struct rxrpc_peer *peer,
			   struct rxrpc_connection *conn)
{
	struct rxrpc_connection *cursor = NULL;
	struct rxrpc_conn_proto k = conn->proto;
	struct rb_node **pp, *parent;

	write_seqlock_bh(&peer->service_conn_lock);

	pp = &peer->service_conns.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		cursor = rb_entry(parent,
				  struct rxrpc_connection, service_node);

		if (cursor->proto.index_key < k.index_key)
			pp = &(*pp)->rb_left;
		else if (cursor->proto.index_key > k.index_key)
			pp = &(*pp)->rb_right;
		else
			goto found_extant_conn;
	}

	rb_link_node_rcu(&conn->service_node, parent, pp);
	rb_insert_color(&conn->service_node, &peer->service_conns);
conn_published:
	set_bit(RXRPC_CONN_IN_SERVICE_CONNS, &conn->flags);
	write_sequnlock_bh(&peer->service_conn_lock);
	_leave(" = %d [new]", conn->debug_id);
	return conn;

found_extant_conn:
	if (atomic_read(&cursor->usage) == 0)
		goto replace_old_connection;
	write_sequnlock_bh(&peer->service_conn_lock);
	/* We should not be able to get here.  rxrpc_incoming_connection() is
	 * called in a non-reentrant context, so there can't be a race to
	 * insert a new connection.
	 */
	BUG();

replace_old_connection:
	/* The old connection is from an outdated epoch. */
	_debug("replace conn");
	rb_replace_node_rcu(&cursor->service_node,
			    &conn->service_node,
			    &peer->service_conns);
	clear_bit(RXRPC_CONN_IN_SERVICE_CONNS, &cursor->flags);
	goto conn_published;
}

/*
 * get a record of an incoming connection
 */
struct rxrpc_connection *rxrpc_incoming_connection(struct rxrpc_local *local,
						   struct sockaddr_rxrpc *srx,
						   struct sk_buff *skb)
{
	struct rxrpc_connection *conn;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_peer *peer;
	const char *new = "old";

	_enter("");

	peer = rxrpc_lookup_peer(local, srx, GFP_NOIO);
	if (!peer) {
		_debug("no peer");
		return ERR_PTR(-EBUSY);
	}

	ASSERT(sp->hdr.flags & RXRPC_CLIENT_INITIATED);

	rcu_read_lock();
	peer = rxrpc_lookup_peer_rcu(local, srx);
	if (peer) {
		conn = rxrpc_find_service_conn_rcu(peer, skb);
		if (conn) {
			if (sp->hdr.securityIndex != conn->security_ix)
				goto security_mismatch_rcu;
			if (rxrpc_get_connection_maybe(conn))
				goto found_extant_connection_rcu;

			/* The conn has expired but we can't remove it without
			 * the appropriate lock, so we attempt to replace it
			 * when we have a new candidate.
			 */
		}

		if (!rxrpc_get_peer_maybe(peer))
			peer = NULL;
	}
	rcu_read_unlock();

	if (!peer) {
		peer = rxrpc_lookup_peer(local, srx, GFP_NOIO);
		if (!peer)
			goto enomem;
	}

	/* We don't have a matching record yet. */
	conn = rxrpc_alloc_connection(GFP_NOIO);
	if (!conn)
		goto enomem_peer;

	conn->proto.epoch	= sp->hdr.epoch;
	conn->proto.cid		= sp->hdr.cid & RXRPC_CIDMASK;
	conn->params.local	= local;
	conn->params.peer	= peer;
	conn->params.service_id	= sp->hdr.serviceId;
	conn->security_ix	= sp->hdr.securityIndex;
	conn->out_clientflag	= 0;
	conn->state		= RXRPC_CONN_SERVICE;
	if (conn->params.service_id)
		conn->state	= RXRPC_CONN_SERVICE_UNSECURED;

	rxrpc_get_local(local);

	write_lock(&rxrpc_connection_lock);
	list_add_tail(&conn->link, &rxrpc_connections);
	write_unlock(&rxrpc_connection_lock);

	/* Make the connection a target for incoming packets. */
	rxrpc_publish_service_conn(peer, conn);

	new = "new";

success:
	_net("CONNECTION %s %d {%x}", new, conn->debug_id, conn->proto.cid);
	_leave(" = %p {u=%d}", conn, atomic_read(&conn->usage));
	return conn;

found_extant_connection_rcu:
	rcu_read_unlock();
	goto success;

security_mismatch_rcu:
	rcu_read_unlock();
	_leave(" = -EKEYREJECTED");
	return ERR_PTR(-EKEYREJECTED);

enomem_peer:
	rxrpc_put_peer(peer);
enomem:
	_leave(" = -ENOMEM");
	return ERR_PTR(-ENOMEM);
}

/*
 * Remove the service connection from the peer's tree, thereby removing it as a
 * target for incoming packets.
 */
void rxrpc_unpublish_service_conn(struct rxrpc_connection *conn)
{
	struct rxrpc_peer *peer = conn->params.peer;

	write_seqlock_bh(&peer->service_conn_lock);
	if (test_and_clear_bit(RXRPC_CONN_IN_SERVICE_CONNS, &conn->flags))
		rb_erase(&conn->service_node, &peer->service_conns);
	write_sequnlock_bh(&peer->service_conn_lock);
}
