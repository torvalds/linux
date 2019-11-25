// SPDX-License-Identifier: GPL-2.0-or-later
/* Service connection management
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
				break;
			conn = NULL;
		}
	} while (need_seqretry(&peer->service_conn_lock, seq));

	done_seqretry(&peer->service_conn_lock, seq);
	_leave(" = %d", conn ? conn->debug_id : -1);
	return conn;
}

/*
 * Insert a service connection into a peer's tree, thereby making it a target
 * for incoming packets.
 */
static void rxrpc_publish_service_conn(struct rxrpc_peer *peer,
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
	return;

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
 * Preallocate a service connection.  The connection is placed on the proc and
 * reap lists so that we don't have to get the lock from BH context.
 */
struct rxrpc_connection *rxrpc_prealloc_service_connection(struct rxrpc_net *rxnet,
							   gfp_t gfp)
{
	struct rxrpc_connection *conn = rxrpc_alloc_connection(gfp);

	if (conn) {
		/* We maintain an extra ref on the connection whilst it is on
		 * the rxrpc_connections list.
		 */
		conn->state = RXRPC_CONN_SERVICE_PREALLOC;
		atomic_set(&conn->usage, 2);

		atomic_inc(&rxnet->nr_conns);
		write_lock(&rxnet->conn_lock);
		list_add_tail(&conn->link, &rxnet->service_conns);
		list_add_tail(&conn->proc_link, &rxnet->conn_proc_list);
		write_unlock(&rxnet->conn_lock);

		trace_rxrpc_conn(conn->debug_id, rxrpc_conn_new_service,
				 atomic_read(&conn->usage),
				 __builtin_return_address(0));
	}

	return conn;
}

/*
 * Set up an incoming connection.  This is called in BH context with the RCU
 * read lock held.
 */
void rxrpc_new_incoming_connection(struct rxrpc_sock *rx,
				   struct rxrpc_connection *conn,
				   struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	_enter("");

	conn->proto.epoch	= sp->hdr.epoch;
	conn->proto.cid		= sp->hdr.cid & RXRPC_CIDMASK;
	conn->params.service_id	= sp->hdr.serviceId;
	conn->service_id	= sp->hdr.serviceId;
	conn->security_ix	= sp->hdr.securityIndex;
	conn->out_clientflag	= 0;
	if (conn->security_ix)
		conn->state	= RXRPC_CONN_SERVICE_UNSECURED;
	else
		conn->state	= RXRPC_CONN_SERVICE;

	/* See if we should upgrade the service.  This can only happen on the
	 * first packet on a new connection.  Once done, it applies to all
	 * subsequent calls on that connection.
	 */
	if (sp->hdr.userStatus == RXRPC_USERSTATUS_SERVICE_UPGRADE &&
	    conn->service_id == rx->service_upgrade.from)
		conn->service_id = rx->service_upgrade.to;

	/* Make the connection a target for incoming packets. */
	rxrpc_publish_service_conn(conn->params.peer, conn);

	_net("CONNECTION new %d {%x}", conn->debug_id, conn->proto.cid);
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
