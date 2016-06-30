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
static struct rxrpc_connection *rxrpc_alloc_connection(gfp_t gfp)
{
	struct rxrpc_connection *conn;

	_enter("");

	conn = kzalloc(sizeof(struct rxrpc_connection), gfp);
	if (conn) {
		spin_lock_init(&conn->channel_lock);
		init_waitqueue_head(&conn->channel_wq);
		INIT_WORK(&conn->processor, &rxrpc_process_connection);
		INIT_LIST_HEAD(&conn->link);
		conn->calls = RB_ROOT;
		skb_queue_head_init(&conn->rx_queue);
		conn->security = &rxrpc_no_security;
		rwlock_init(&conn->lock);
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
 * add a call to a connection's call-by-ID tree
 */
static void rxrpc_add_call_ID_to_conn(struct rxrpc_connection *conn,
				      struct rxrpc_call *call)
{
	struct rxrpc_call *xcall;
	struct rb_node *parent, **p;
	u32 call_id;

	write_lock_bh(&conn->lock);

	call_id = call->call_id;
	p = &conn->calls.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		xcall = rb_entry(parent, struct rxrpc_call, conn_node);

		if (call_id < xcall->call_id)
			p = &(*p)->rb_left;
		else if (call_id > xcall->call_id)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&call->conn_node, parent, p);
	rb_insert_color(&call->conn_node, &conn->calls);

	write_unlock_bh(&conn->lock);
}

/*
 * Allocate a client connection.  The caller must take care to clear any
 * padding bytes in *cp.
 */
static struct rxrpc_connection *
rxrpc_alloc_client_connection(struct rxrpc_conn_parameters *cp, gfp_t gfp)
{
	struct rxrpc_connection *conn;
	int ret;

	_enter("");

	conn = rxrpc_alloc_connection(gfp);
	if (!conn) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	conn->params		= *cp;
	conn->proto.local	= cp->local;
	conn->proto.epoch	= rxrpc_epoch;
	conn->proto.cid		= 0;
	conn->proto.in_clientflag = 0;
	conn->proto.family	= cp->peer->srx.transport.family;
	conn->out_clientflag	= RXRPC_CLIENT_INITIATED;
	conn->state		= RXRPC_CONN_CLIENT;

	switch (conn->proto.family) {
	case AF_INET:
		conn->proto.addr_size = sizeof(conn->proto.ipv4_addr);
		conn->proto.ipv4_addr = cp->peer->srx.transport.sin.sin_addr;
		conn->proto.port = cp->peer->srx.transport.sin.sin_port;
		break;
	}

	ret = rxrpc_get_client_connection_id(conn, gfp);
	if (ret < 0)
		goto error_0;

	ret = rxrpc_init_client_conn_security(conn);
	if (ret < 0)
		goto error_1;

	conn->security->prime_packet_security(conn);

	write_lock(&rxrpc_connection_lock);
	list_add_tail(&conn->link, &rxrpc_connections);
	write_unlock(&rxrpc_connection_lock);

	/* We steal the caller's peer ref. */
	cp->peer = NULL;
	rxrpc_get_local(conn->params.local);
	key_get(conn->params.key);

	_leave(" = %p", conn);
	return conn;

error_1:
	rxrpc_put_client_connection_id(conn);
error_0:
	kfree(conn);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * find a connection for a call
 * - called in process context with IRQs enabled
 */
int rxrpc_connect_call(struct rxrpc_call *call,
		       struct rxrpc_conn_parameters *cp,
		       struct sockaddr_rxrpc *srx,
		       gfp_t gfp)
{
	struct rxrpc_connection *conn, *candidate = NULL;
	struct rxrpc_local *local = cp->local;
	struct rb_node *p, **pp, *parent;
	long diff;
	int chan;

	DECLARE_WAITQUEUE(myself, current);

	_enter("{%d,%lx},", call->debug_id, call->user_call_ID);

	cp->peer = rxrpc_lookup_peer(cp->local, srx, gfp);
	if (!cp->peer)
		return -ENOMEM;

	if (!cp->exclusive) {
		/* Search for a existing client connection unless this is going
		 * to be a connection that's used exclusively for a single call.
		 */
		_debug("search 1");
		spin_lock(&local->client_conns_lock);
		p = local->client_conns.rb_node;
		while (p) {
			conn = rb_entry(p, struct rxrpc_connection, client_node);

#define cmp(X) ((long)conn->params.X - (long)cp->X)
			diff = (cmp(peer) ?:
				cmp(key) ?:
				cmp(security_level));
			if (diff < 0)
				p = p->rb_left;
			else if (diff > 0)
				p = p->rb_right;
			else
				goto found_extant_conn;
		}
		spin_unlock(&local->client_conns_lock);
	}

	/* We didn't find a connection or we want an exclusive one. */
	_debug("get new conn");
	candidate = rxrpc_alloc_client_connection(cp, gfp);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	if (cp->exclusive) {
		/* Assign the call on an exclusive connection to channel 0 and
		 * don't add the connection to the endpoint's shareable conn
		 * lookup tree.
		 */
		_debug("exclusive chan 0");
		conn = candidate;
		atomic_set(&conn->avail_chans, RXRPC_MAXCALLS - 1);
		spin_lock(&conn->channel_lock);
		chan = 0;
		goto found_channel;
	}

	/* We need to redo the search before attempting to add a new connection
	 * lest we race with someone else adding a conflicting instance.
	 */
	_debug("search 2");
	spin_lock(&local->client_conns_lock);

	pp = &local->client_conns.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		conn = rb_entry(parent, struct rxrpc_connection, client_node);

		diff = (cmp(peer) ?:
			cmp(key) ?:
			cmp(security_level));
		if (diff < 0)
			pp = &(*pp)->rb_left;
		else if (diff > 0)
			pp = &(*pp)->rb_right;
		else
			goto found_extant_conn;
	}

	/* The second search also failed; simply add the new connection with
	 * the new call in channel 0.  Note that we need to take the channel
	 * lock before dropping the client conn lock.
	 */
	_debug("new conn");
	conn = candidate;
	candidate = NULL;

	rb_link_node(&conn->client_node, parent, pp);
	rb_insert_color(&conn->client_node, &local->client_conns);

	atomic_set(&conn->avail_chans, RXRPC_MAXCALLS - 1);
	spin_lock(&conn->channel_lock);
	spin_unlock(&local->client_conns_lock);
	chan = 0;

found_channel:
	_debug("found chan");
	call->conn	= conn;
	call->channel	= chan;
	call->epoch	= conn->proto.epoch;
	call->cid	= conn->proto.cid | chan;
	call->call_id	= ++conn->call_counter;
	rcu_assign_pointer(conn->channels[chan], call);

	_net("CONNECT call %d on conn %d", call->debug_id, conn->debug_id);

	rxrpc_add_call_ID_to_conn(conn, call);
	spin_unlock(&conn->channel_lock);
	rxrpc_put_peer(cp->peer);
	cp->peer = NULL;
	_leave(" = %p {u=%d}", conn, atomic_read(&conn->usage));
	return 0;

	/* We found a suitable connection already in existence.  Discard any
	 * candidate we may have allocated, and try to get a channel on this
	 * one.
	 */
found_extant_conn:
	_debug("found conn");
	rxrpc_get_connection(conn);
	spin_unlock(&local->client_conns_lock);

	rxrpc_put_connection(candidate);

	if (!atomic_add_unless(&conn->avail_chans, -1, 0)) {
		if (!gfpflags_allow_blocking(gfp)) {
			rxrpc_put_connection(conn);
			_leave(" = -EAGAIN");
			return -EAGAIN;
		}

		add_wait_queue(&conn->channel_wq, &myself);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (atomic_add_unless(&conn->avail_chans, -1, 0))
				break;
			if (signal_pending(current))
				goto interrupted;
			schedule();
		}
		remove_wait_queue(&conn->channel_wq, &myself);
		__set_current_state(TASK_RUNNING);
	}

	/* The connection allegedly now has a free channel and we can now
	 * attach the call to it.
	 */
	spin_lock(&conn->channel_lock);

	for (chan = 0; chan < RXRPC_MAXCALLS; chan++)
		if (!conn->channels[chan])
			goto found_channel;
	BUG();

interrupted:
	remove_wait_queue(&conn->channel_wq, &myself);
	__set_current_state(TASK_RUNNING);
	rxrpc_put_connection(conn);
	rxrpc_put_peer(cp->peer);
	cp->peer = NULL;
	_leave(" = -ERESTARTSYS");
	return -ERESTARTSYS;
}

/*
 * get a record of an incoming connection
 */
struct rxrpc_connection *rxrpc_incoming_connection(struct rxrpc_local *local,
						   struct rxrpc_peer *peer,
						   struct sk_buff *skb)
{
	struct rxrpc_connection *conn, *candidate = NULL;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rb_node *p, **pp;
	const char *new = "old";
	u32 epoch, cid;

	_enter("");

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
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	candidate->proto.local		= local;
	candidate->proto.epoch		= sp->hdr.epoch;
	candidate->proto.cid		= sp->hdr.cid & RXRPC_CIDMASK;
	candidate->proto.in_clientflag	= RXRPC_CLIENT_INITIATED;
	candidate->params.local		= local;
	candidate->params.peer		= peer;
	candidate->params.service_id	= sp->hdr.serviceId;
	candidate->security_ix		= sp->hdr.securityIndex;
	candidate->out_clientflag	= 0;
	candidate->state		= RXRPC_CONN_SERVER;
	if (candidate->params.service_id)
		candidate->state	= RXRPC_CONN_SERVER_UNSECURED;

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
	conn = candidate;
	candidate = NULL;
	rb_link_node(&conn->service_node, p, pp);
	rb_insert_color(&conn->service_node, &peer->service_conns);
	rxrpc_get_peer(peer);
	rxrpc_get_local(local);

	write_unlock_bh(&peer->conn_lock);

	write_lock(&rxrpc_connection_lock);
	list_add_tail(&conn->link, &rxrpc_connections);
	write_unlock(&rxrpc_connection_lock);

	new = "new";

success:
	_net("CONNECTION %s %d {%x}", new, conn->debug_id, conn->proto.cid);

	_leave(" = %p {u=%d}", conn, atomic_read(&conn->usage));
	return conn;

	/* we found the connection in the list immediately */
found_extant_connection:
	if (sp->hdr.securityIndex != conn->security_ix) {
		read_unlock_bh(&peer->conn_lock);
		goto security_mismatch;
	}
	rxrpc_get_connection(conn);
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

security_mismatch:
	kfree(candidate);
	_leave(" = -EKEYREJECTED");
	return ERR_PTR(-EKEYREJECTED);
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
 * terminates.
 */
void rxrpc_disconnect_call(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;
	unsigned chan = call->channel;

	_enter("%d,%d", conn->debug_id, call->channel);

	if (conn->channels[chan] == call) {
		rcu_assign_pointer(conn->channels[chan], NULL);
		atomic_inc(&conn->avail_chans);
		wake_up(&conn->channel_wq);
	}
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
static void rxrpc_destroy_connection(struct rxrpc_connection *conn)
{
	_enter("%p{%d}", conn, atomic_read(&conn->usage));

	ASSERTCMP(atomic_read(&conn->usage), ==, 0);

	_net("DESTROY CONN %d", conn->debug_id);

	ASSERT(RB_EMPTY_ROOT(&conn->calls));
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
		rxrpc_destroy_connection(conn);
	}

	_leave("");
}

/*
 * preemptively destroy all the connection records rather than waiting for them
 * to time out
 */
void __exit rxrpc_destroy_all_connections(void)
{
	_enter("");

	rxrpc_connection_expiry = 0;
	cancel_delayed_work(&rxrpc_connection_reap);
	rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);

	_leave("");
}
