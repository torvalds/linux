/* RxRPC virtual connection handler
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/crypto.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * Time till a connection expires after last use (in seconds).
 */
unsigned int rxrpc_connection_expiry = 10 * 60;

static void rxrpc_connection_reaper(struct work_struct *work);

LIST_HEAD(rxrpc_connections);
DEFINE_RWLOCK(rxrpc_connection_lock);
static DECLARE_DELAYED_WORK(rxrpc_connection_reap, rxrpc_connection_reaper);

/*
 * allocate a new connection
 */
struct rxrpc_connection *rxrpc_alloc_connection(gfp_t gfp)
{
	struct rxrpc_connection *conn;

	_enter("");

	conn = kzalloc(sizeof(struct rxrpc_connection), gfp);
	if (conn) {
		spin_lock_init(&conn->channel_lock);
		init_waitqueue_head(&conn->channel_wq);
		INIT_WORK(&conn->processor, &rxrpc_process_connection);
		INIT_LIST_HEAD(&conn->link);
		skb_queue_head_init(&conn->rx_queue);
		conn->security = &rxrpc_no_security;
		spin_lock_init(&conn->state_lock);
		atomic_set(&conn->usage, 1);
		conn->debug_id = atomic_inc_return(&rxrpc_debug_id);
		atomic_set(&conn->avail_chans, RXRPC_MAXCALLS);
		conn->size_align = 4;
		conn->header_size = sizeof(struct rxrpc_wire_header);
	}

	_leave(" = %p{%d}", conn, conn ? conn->debug_id : 0);
	return conn;
}

/*
 * find a connection based on transport and RxRPC connection ID for an incoming
 * packet
 */
struct rxrpc_connection *rxrpc_find_connection(struct rxrpc_local *local,
					       struct rxrpc_peer *peer,
					       struct sk_buff *skb)
{
	struct rxrpc_connection *conn;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rb_node *p;
	u32 epoch, cid;

	_enter(",{%x,%x}", sp->hdr.cid, sp->hdr.flags);

	read_lock_bh(&peer->conn_lock);

	cid	= sp->hdr.cid & RXRPC_CIDMASK;
	epoch	= sp->hdr.epoch;

	if (sp->hdr.flags & RXRPC_CLIENT_INITIATED) {
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
				goto found;
		}
	} else {
		conn = idr_find(&rxrpc_client_conn_ids, cid >> RXRPC_CIDSHIFT);
		if (conn &&
		    conn->proto.epoch == epoch &&
		    conn->params.peer == peer)
			goto found;
	}

	read_unlock_bh(&peer->conn_lock);
	_leave(" = NULL");
	return NULL;

found:
	rxrpc_get_connection(conn);
	read_unlock_bh(&peer->conn_lock);
	_leave(" = %p", conn);
	return conn;
}

/*
 * Disconnect a call and clear any channel it occupies when that call
 * terminates.  The caller must hold the channel_lock and must release the
 * call's ref on the connection.
 */
void __rxrpc_disconnect_call(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_channel *chan = &conn->channels[call->channel];

	_enter("%d,%d", conn->debug_id, call->channel);

	if (rcu_access_pointer(chan->call) == call) {
		/* Save the result of the call so that we can repeat it if necessary
		 * through the channel, whilst disposing of the actual call record.
		 */
		chan->last_result = call->local_abort;
		smp_wmb();
		chan->last_call = chan->call_id;
		chan->call_id = chan->call_counter;

		rcu_assign_pointer(chan->call, NULL);
		atomic_inc(&conn->avail_chans);
		wake_up(&conn->channel_wq);
	}

	_leave("");
}

/*
 * Disconnect a call and clear any channel it occupies when that call
 * terminates.
 */
void rxrpc_disconnect_call(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;

	spin_lock(&conn->channel_lock);
	__rxrpc_disconnect_call(call);
	spin_unlock(&conn->channel_lock);

	call->conn = NULL;
	rxrpc_put_connection(conn);
}

/*
 * release a virtual connection
 */
void rxrpc_put_connection(struct rxrpc_connection *conn)
{
	if (!conn)
		return;

	_enter("%p{u=%d,d=%d}",
	       conn, atomic_read(&conn->usage), conn->debug_id);

	ASSERTCMP(atomic_read(&conn->usage), >, 0);

	conn->put_time = ktime_get_seconds();
	if (atomic_dec_and_test(&conn->usage)) {
		_debug("zombie");
		rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);
	}

	_leave("");
}

/*
 * destroy a virtual connection
 */
static void rxrpc_destroy_connection(struct rcu_head *rcu)
{
	struct rxrpc_connection *conn =
		container_of(rcu, struct rxrpc_connection, rcu);

	_enter("{%d,u=%d}", conn->debug_id, atomic_read(&conn->usage));

	ASSERTCMP(atomic_read(&conn->usage), ==, 0);

	_net("DESTROY CONN %d", conn->debug_id);

	rxrpc_purge_queue(&conn->rx_queue);

	conn->security->clear(conn);
	key_put(conn->params.key);
	key_put(conn->server_key);
	rxrpc_put_peer(conn->params.peer);
	rxrpc_put_local(conn->params.local);

	kfree(conn);
	_leave("");
}

/*
 * reap dead connections
 */
static void rxrpc_connection_reaper(struct work_struct *work)
{
	struct rxrpc_connection *conn, *_p;
	struct rxrpc_peer *peer;
	unsigned long now, earliest, reap_time;

	LIST_HEAD(graveyard);

	_enter("");

	now = ktime_get_seconds();
	earliest = ULONG_MAX;

	write_lock(&rxrpc_connection_lock);
	list_for_each_entry_safe(conn, _p, &rxrpc_connections, link) {
		_debug("reap CONN %d { u=%d,t=%ld }",
		       conn->debug_id, atomic_read(&conn->usage),
		       (long) now - (long) conn->put_time);

		if (likely(atomic_read(&conn->usage) > 0))
			continue;

		if (rxrpc_conn_is_client(conn)) {
			struct rxrpc_local *local = conn->params.local;
			spin_lock(&local->client_conns_lock);
			reap_time = conn->put_time + rxrpc_connection_expiry;

			if (atomic_read(&conn->usage) > 0) {
				;
			} else if (reap_time <= now) {
				list_move_tail(&conn->link, &graveyard);
				rxrpc_put_client_connection_id(conn);
				rb_erase(&conn->client_node,
					 &local->client_conns);
			} else if (reap_time < earliest) {
				earliest = reap_time;
			}

			spin_unlock(&local->client_conns_lock);
		} else {
			peer = conn->params.peer;
			write_lock_bh(&peer->conn_lock);
			reap_time = conn->put_time + rxrpc_connection_expiry;

			if (atomic_read(&conn->usage) > 0) {
				;
			} else if (reap_time <= now) {
				list_move_tail(&conn->link, &graveyard);
				rb_erase(&conn->service_node,
					 &peer->service_conns);
			} else if (reap_time < earliest) {
				earliest = reap_time;
			}

			write_unlock_bh(&peer->conn_lock);
		}
	}
	write_unlock(&rxrpc_connection_lock);

	if (earliest != ULONG_MAX) {
		_debug("reschedule reaper %ld", (long) earliest - now);
		ASSERTCMP(earliest, >, now);
		rxrpc_queue_delayed_work(&rxrpc_connection_reap,
					 (earliest - now) * HZ);
	}

	/* then destroy all those pulled out */
	while (!list_empty(&graveyard)) {
		conn = list_entry(graveyard.next, struct rxrpc_connection,
				  link);
		list_del_init(&conn->link);

		ASSERTCMP(atomic_read(&conn->usage), ==, 0);
		skb_queue_purge(&conn->rx_queue);
		call_rcu(&conn->rcu, rxrpc_destroy_connection);
	}

	_leave("");
}

/*
 * preemptively destroy all the connection records rather than waiting for them
 * to time out
 */
void __exit rxrpc_destroy_all_connections(void)
{
	struct rxrpc_connection *conn, *_p;
	bool leak = false;

	_enter("");

	rxrpc_connection_expiry = 0;
	cancel_delayed_work(&rxrpc_connection_reap);
	rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);
	flush_workqueue(rxrpc_workqueue);

	write_lock(&rxrpc_connection_lock);
	list_for_each_entry_safe(conn, _p, &rxrpc_connections, link) {
		pr_err("AF_RXRPC: Leaked conn %p {%d}\n",
		       conn, atomic_read(&conn->usage));
		leak = true;
	}
	write_unlock(&rxrpc_connection_lock);
	BUG_ON(leak);

	/* Make sure the local and peer records pinned by any dying connections
	 * are released.
	 */
	rcu_barrier();
	rxrpc_destroy_client_conn_ids();

	_leave("");
}
