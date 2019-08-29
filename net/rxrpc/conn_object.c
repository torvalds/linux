/* RxRPC virtual connection handler, common bits.
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
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
#include "ar-internal.h"

/*
 * Time till a connection expires after last use (in seconds).
 */
unsigned int __read_mostly rxrpc_connection_expiry = 10 * 60;
unsigned int __read_mostly rxrpc_closed_conn_expiry = 10;

static void rxrpc_destroy_connection(struct rcu_head *);

static void rxrpc_connection_timer(struct timer_list *timer)
{
	struct rxrpc_connection *conn =
		container_of(timer, struct rxrpc_connection, timer);

	rxrpc_queue_conn(conn);
}

/*
 * allocate a new connection
 */
struct rxrpc_connection *rxrpc_alloc_connection(gfp_t gfp)
{
	struct rxrpc_connection *conn;

	_enter("");

	conn = kzalloc(sizeof(struct rxrpc_connection), gfp);
	if (conn) {
		INIT_LIST_HEAD(&conn->cache_link);
		spin_lock_init(&conn->channel_lock);
		INIT_LIST_HEAD(&conn->waiting_calls);
		timer_setup(&conn->timer, &rxrpc_connection_timer, 0);
		INIT_WORK(&conn->processor, &rxrpc_process_connection);
		INIT_LIST_HEAD(&conn->proc_link);
		INIT_LIST_HEAD(&conn->link);
		skb_queue_head_init(&conn->rx_queue);
		conn->security = &rxrpc_no_security;
		spin_lock_init(&conn->state_lock);
		conn->debug_id = atomic_inc_return(&rxrpc_debug_id);
		conn->size_align = 4;
		conn->idle_timestamp = jiffies;
	}

	_leave(" = %p{%d}", conn, conn ? conn->debug_id : 0);
	return conn;
}

/*
 * Look up a connection in the cache by protocol parameters.
 *
 * If successful, a pointer to the connection is returned, but no ref is taken.
 * NULL is returned if there is no match.
 *
 * When searching for a service call, if we find a peer but no connection, we
 * return that through *_peer in case we need to create a new service call.
 *
 * The caller must be holding the RCU read lock.
 */
struct rxrpc_connection *rxrpc_find_connection_rcu(struct rxrpc_local *local,
						   struct sk_buff *skb,
						   struct rxrpc_peer **_peer)
{
	struct rxrpc_connection *conn;
	struct rxrpc_conn_proto k;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct sockaddr_rxrpc srx;
	struct rxrpc_peer *peer;

	_enter(",%x", sp->hdr.cid & RXRPC_CIDMASK);

	if (rxrpc_extract_addr_from_skb(local, &srx, skb) < 0)
		goto not_found;

	/* We may have to handle mixing IPv4 and IPv6 */
	if (srx.transport.family != local->srx.transport.family) {
		pr_warn_ratelimited("AF_RXRPC: Protocol mismatch %u not %u\n",
				    srx.transport.family,
				    local->srx.transport.family);
		goto not_found;
	}

	k.epoch	= sp->hdr.epoch;
	k.cid	= sp->hdr.cid & RXRPC_CIDMASK;

	if (rxrpc_to_server(sp)) {
		/* We need to look up service connections by the full protocol
		 * parameter set.  We look up the peer first as an intermediate
		 * step and then the connection from the peer's tree.
		 */
		peer = rxrpc_lookup_peer_rcu(local, &srx);
		if (!peer)
			goto not_found;
		*_peer = peer;
		conn = rxrpc_find_service_conn_rcu(peer, skb);
		if (!conn || atomic_read(&conn->usage) == 0)
			goto not_found;
		_leave(" = %p", conn);
		return conn;
	} else {
		/* Look up client connections by connection ID alone as their
		 * IDs are unique for this machine.
		 */
		conn = idr_find(&rxrpc_client_conn_ids,
				sp->hdr.cid >> RXRPC_CIDSHIFT);
		if (!conn || atomic_read(&conn->usage) == 0) {
			_debug("no conn");
			goto not_found;
		}

		if (conn->proto.epoch != k.epoch ||
		    conn->params.local != local)
			goto not_found;

		peer = conn->params.peer;
		switch (srx.transport.family) {
		case AF_INET:
			if (peer->srx.transport.sin.sin_port !=
			    srx.transport.sin.sin_port ||
			    peer->srx.transport.sin.sin_addr.s_addr !=
			    srx.transport.sin.sin_addr.s_addr)
				goto not_found;
			break;
#ifdef CONFIG_AF_RXRPC_IPV6
		case AF_INET6:
			if (peer->srx.transport.sin6.sin6_port !=
			    srx.transport.sin6.sin6_port ||
			    memcmp(&peer->srx.transport.sin6.sin6_addr,
				   &srx.transport.sin6.sin6_addr,
				   sizeof(struct in6_addr)) != 0)
				goto not_found;
			break;
#endif
		default:
			BUG();
		}

		_leave(" = %p", conn);
		return conn;
	}

not_found:
	_leave(" = NULL");
	return NULL;
}

/*
 * Disconnect a call and clear any channel it occupies when that call
 * terminates.  The caller must hold the channel_lock and must release the
 * call's ref on the connection.
 */
void __rxrpc_disconnect_call(struct rxrpc_connection *conn,
			     struct rxrpc_call *call)
{
	struct rxrpc_channel *chan =
		&conn->channels[call->cid & RXRPC_CHANNELMASK];

	_enter("%d,%x", conn->debug_id, call->cid);

	if (rcu_access_pointer(chan->call) == call) {
		/* Save the result of the call so that we can repeat it if necessary
		 * through the channel, whilst disposing of the actual call record.
		 */
		trace_rxrpc_disconnect_call(call);
		switch (call->completion) {
		case RXRPC_CALL_SUCCEEDED:
			chan->last_seq = call->rx_hard_ack;
			chan->last_type = RXRPC_PACKET_TYPE_ACK;
			break;
		case RXRPC_CALL_LOCALLY_ABORTED:
			chan->last_abort = call->abort_code;
			chan->last_type = RXRPC_PACKET_TYPE_ABORT;
			break;
		default:
			chan->last_abort = RX_USER_ABORT;
			chan->last_type = RXRPC_PACKET_TYPE_ABORT;
			break;
		}

		/* Sync with rxrpc_conn_retransmit(). */
		smp_wmb();
		chan->last_call = chan->call_id;
		chan->call_id = chan->call_counter;

		rcu_assign_pointer(chan->call, NULL);
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

	call->peer->cong_cwnd = call->cong_cwnd;

	spin_lock_bh(&conn->params.peer->lock);
	hlist_del_rcu(&call->error_link);
	spin_unlock_bh(&conn->params.peer->lock);

	if (rxrpc_is_client_call(call))
		return rxrpc_disconnect_client_call(call);

	spin_lock(&conn->channel_lock);
	__rxrpc_disconnect_call(conn, call);
	spin_unlock(&conn->channel_lock);

	call->conn = NULL;
	conn->idle_timestamp = jiffies;
	rxrpc_put_connection(conn);
}

/*
 * Kill off a connection.
 */
void rxrpc_kill_connection(struct rxrpc_connection *conn)
{
	struct rxrpc_net *rxnet = conn->params.local->rxnet;

	ASSERT(!rcu_access_pointer(conn->channels[0].call) &&
	       !rcu_access_pointer(conn->channels[1].call) &&
	       !rcu_access_pointer(conn->channels[2].call) &&
	       !rcu_access_pointer(conn->channels[3].call));
	ASSERT(list_empty(&conn->cache_link));

	write_lock(&rxnet->conn_lock);
	list_del_init(&conn->proc_link);
	write_unlock(&rxnet->conn_lock);

	/* Drain the Rx queue.  Note that even though we've unpublished, an
	 * incoming packet could still be being added to our Rx queue, so we
	 * will need to drain it again in the RCU cleanup handler.
	 */
	rxrpc_purge_queue(&conn->rx_queue);

	/* Leave final destruction to RCU.  The connection processor work item
	 * must carry a ref on the connection to prevent us getting here whilst
	 * it is queued or running.
	 */
	call_rcu(&conn->rcu, rxrpc_destroy_connection);
}

/*
 * Queue a connection's work processor, getting a ref to pass to the work
 * queue.
 */
bool rxrpc_queue_conn(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_fetch_add_unless(&conn->usage, 1, 0);
	if (n == 0)
		return false;
	if (rxrpc_queue_work(&conn->processor))
		trace_rxrpc_conn(conn, rxrpc_conn_queued, n + 1, here);
	else
		rxrpc_put_connection(conn);
	return true;
}

/*
 * Note the re-emergence of a connection.
 */
void rxrpc_see_connection(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	if (conn) {
		int n = atomic_read(&conn->usage);

		trace_rxrpc_conn(conn, rxrpc_conn_seen, n, here);
	}
}

/*
 * Get a ref on a connection.
 */
void rxrpc_get_connection(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(&conn->usage);

	trace_rxrpc_conn(conn, rxrpc_conn_got, n, here);
}

/*
 * Try to get a ref on a connection.
 */
struct rxrpc_connection *
rxrpc_get_connection_maybe(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);

	if (conn) {
		int n = atomic_fetch_add_unless(&conn->usage, 1, 0);
		if (n > 0)
			trace_rxrpc_conn(conn, rxrpc_conn_got, n + 1, here);
		else
			conn = NULL;
	}
	return conn;
}

/*
 * Set the service connection reap timer.
 */
static void rxrpc_set_service_reap_timer(struct rxrpc_net *rxnet,
					 unsigned long reap_at)
{
	if (rxnet->live)
		timer_reduce(&rxnet->service_conn_reap_timer, reap_at);
}

/*
 * Release a service connection
 */
void rxrpc_put_service_conn(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	int n;

	n = atomic_dec_return(&conn->usage);
	trace_rxrpc_conn(conn, rxrpc_conn_put_service, n, here);
	ASSERTCMP(n, >=, 0);
	if (n == 1)
		rxrpc_set_service_reap_timer(conn->params.local->rxnet,
					     jiffies + rxrpc_connection_expiry);
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

	del_timer_sync(&conn->timer);
	rxrpc_purge_queue(&conn->rx_queue);

	conn->security->clear(conn);
	key_put(conn->params.key);
	key_put(conn->server_key);
	rxrpc_put_peer(conn->params.peer);

	if (atomic_dec_and_test(&conn->params.local->rxnet->nr_conns))
		wake_up_var(&conn->params.local->rxnet->nr_conns);
	rxrpc_put_local(conn->params.local);

	kfree(conn);
	_leave("");
}

/*
 * reap dead service connections
 */
void rxrpc_service_connection_reaper(struct work_struct *work)
{
	struct rxrpc_connection *conn, *_p;
	struct rxrpc_net *rxnet =
		container_of(work, struct rxrpc_net, service_conn_reaper);
	unsigned long expire_at, earliest, idle_timestamp, now;

	LIST_HEAD(graveyard);

	_enter("");

	now = jiffies;
	earliest = now + MAX_JIFFY_OFFSET;

	write_lock(&rxnet->conn_lock);
	list_for_each_entry_safe(conn, _p, &rxnet->service_conns, link) {
		ASSERTCMP(atomic_read(&conn->usage), >, 0);
		if (likely(atomic_read(&conn->usage) > 1))
			continue;
		if (conn->state == RXRPC_CONN_SERVICE_PREALLOC)
			continue;

		if (rxnet->live && !conn->params.local->dead) {
			idle_timestamp = READ_ONCE(conn->idle_timestamp);
			expire_at = idle_timestamp + rxrpc_connection_expiry * HZ;
			if (conn->params.local->service_closed)
				expire_at = idle_timestamp + rxrpc_closed_conn_expiry * HZ;

			_debug("reap CONN %d { u=%d,t=%ld }",
			       conn->debug_id, atomic_read(&conn->usage),
			       (long)expire_at - (long)now);

			if (time_before(now, expire_at)) {
				if (time_before(expire_at, earliest))
					earliest = expire_at;
				continue;
			}
		}

		/* The usage count sits at 1 whilst the object is unused on the
		 * list; we reduce that to 0 to make the object unavailable.
		 */
		if (atomic_cmpxchg(&conn->usage, 1, 0) != 1)
			continue;
		trace_rxrpc_conn(conn, rxrpc_conn_reap_service, 0, NULL);

		if (rxrpc_conn_is_client(conn))
			BUG();
		else
			rxrpc_unpublish_service_conn(conn);

		list_move_tail(&conn->link, &graveyard);
	}
	write_unlock(&rxnet->conn_lock);

	if (earliest != now + MAX_JIFFY_OFFSET) {
		_debug("reschedule reaper %ld", (long)earliest - (long)now);
		ASSERT(time_after(earliest, now));
		rxrpc_set_service_reap_timer(rxnet, earliest);
	}

	while (!list_empty(&graveyard)) {
		conn = list_entry(graveyard.next, struct rxrpc_connection,
				  link);
		list_del_init(&conn->link);

		ASSERTCMP(atomic_read(&conn->usage), ==, 0);
		rxrpc_kill_connection(conn);
	}

	_leave("");
}

/*
 * preemptively destroy all the service connection records rather than
 * waiting for them to time out
 */
void rxrpc_destroy_all_connections(struct rxrpc_net *rxnet)
{
	struct rxrpc_connection *conn, *_p;
	bool leak = false;

	_enter("");

	atomic_dec(&rxnet->nr_conns);
	rxrpc_destroy_all_client_connections(rxnet);

	del_timer_sync(&rxnet->service_conn_reap_timer);
	rxrpc_queue_work(&rxnet->service_conn_reaper);
	flush_workqueue(rxrpc_workqueue);

	write_lock(&rxnet->conn_lock);
	list_for_each_entry_safe(conn, _p, &rxnet->service_conns, link) {
		pr_err("AF_RXRPC: Leaked conn %p {%d}\n",
		       conn, atomic_read(&conn->usage));
		leak = true;
	}
	write_unlock(&rxnet->conn_lock);
	BUG_ON(leak);

	ASSERT(list_empty(&rxnet->conn_proc_list));

	/* We need to wait for the connections to be destroyed by RCU as they
	 * pin things that we still need to get rid of.
	 */
	wait_var_event(&rxnet->nr_conns, !atomic_read(&rxnet->nr_conns));
	_leave("");
}
