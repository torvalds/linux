// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC virtual connection handler, common bits.
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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

static void rxrpc_clean_up_connection(struct work_struct *work);
static void rxrpc_set_service_reap_timer(struct rxrpc_net *rxnet,
					 unsigned long reap_at);

void rxrpc_poke_conn(struct rxrpc_connection *conn, enum rxrpc_conn_trace why)
{
	struct rxrpc_local *local = conn->local;
	bool busy;

	if (WARN_ON_ONCE(!local))
		return;

	spin_lock_irq(&local->lock);
	busy = !list_empty(&conn->attend_link);
	if (!busy) {
		rxrpc_get_connection(conn, why);
		list_add_tail(&conn->attend_link, &local->conn_attend_q);
	}
	spin_unlock_irq(&local->lock);
	rxrpc_wake_up_io_thread(local);
}

static void rxrpc_connection_timer(struct timer_list *timer)
{
	struct rxrpc_connection *conn =
		container_of(timer, struct rxrpc_connection, timer);

	rxrpc_poke_conn(conn, rxrpc_conn_get_poke_timer);
}

/*
 * allocate a new connection
 */
struct rxrpc_connection *rxrpc_alloc_connection(struct rxrpc_net *rxnet,
						gfp_t gfp)
{
	struct rxrpc_connection *conn;

	_enter("");

	conn = kzalloc(sizeof(struct rxrpc_connection), gfp);
	if (conn) {
		INIT_LIST_HEAD(&conn->cache_link);
		timer_setup(&conn->timer, &rxrpc_connection_timer, 0);
		INIT_WORK(&conn->processor, rxrpc_process_connection);
		INIT_WORK(&conn->destructor, rxrpc_clean_up_connection);
		INIT_LIST_HEAD(&conn->proc_link);
		INIT_LIST_HEAD(&conn->link);
		INIT_LIST_HEAD(&conn->attend_link);
		mutex_init(&conn->security_lock);
		mutex_init(&conn->tx_data_alloc_lock);
		skb_queue_head_init(&conn->rx_queue);
		conn->rxnet = rxnet;
		conn->security = &rxrpc_no_security;
		spin_lock_init(&conn->state_lock);
		conn->debug_id = atomic_inc_return(&rxrpc_debug_id);
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
struct rxrpc_connection *rxrpc_find_client_connection_rcu(struct rxrpc_local *local,
							  struct sockaddr_rxrpc *srx,
							  struct sk_buff *skb)
{
	struct rxrpc_connection *conn;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_peer *peer;

	_enter(",%x", sp->hdr.cid & RXRPC_CIDMASK);

	/* Look up client connections by connection ID alone as their
	 * IDs are unique for this machine.
	 */
	conn = idr_find(&local->conn_ids, sp->hdr.cid >> RXRPC_CIDSHIFT);
	if (!conn || refcount_read(&conn->ref) == 0) {
		_debug("no conn");
		goto not_found;
	}

	if (conn->proto.epoch != sp->hdr.epoch ||
	    conn->local != local)
		goto not_found;

	peer = conn->peer;
	switch (srx->transport.family) {
	case AF_INET:
		if (peer->srx.transport.sin.sin_port !=
		    srx->transport.sin.sin_port)
			goto not_found;
		break;
#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		if (peer->srx.transport.sin6.sin6_port !=
		    srx->transport.sin6.sin6_port)
			goto not_found;
		break;
#endif
	default:
		BUG();
	}

	_leave(" = %p", conn);
	return conn;

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

	if (chan->call == call) {
		/* Save the result of the call so that we can repeat it if necessary
		 * through the channel, whilst disposing of the actual call record.
		 */
		trace_rxrpc_disconnect_call(call);
		switch (call->completion) {
		case RXRPC_CALL_SUCCEEDED:
			chan->last_seq = call->rx_highest_seq;
			chan->last_type = RXRPC_PACKET_TYPE_ACK;
			break;
		case RXRPC_CALL_LOCALLY_ABORTED:
			chan->last_abort = call->abort_code;
			chan->last_type = RXRPC_PACKET_TYPE_ABORT;
			break;
		default:
			chan->last_abort = RX_CALL_DEAD;
			chan->last_type = RXRPC_PACKET_TYPE_ABORT;
			break;
		}

		chan->last_call = chan->call_id;
		chan->call_id = chan->call_counter;
		chan->call = NULL;
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

	set_bit(RXRPC_CALL_DISCONNECTED, &call->flags);
	rxrpc_see_call(call, rxrpc_call_see_disconnected);

	call->peer->cong_ssthresh = call->cong_ssthresh;

	if (!hlist_unhashed(&call->error_link)) {
		spin_lock_irq(&call->peer->lock);
		hlist_del_init(&call->error_link);
		spin_unlock_irq(&call->peer->lock);
	}

	if (rxrpc_is_client_call(call)) {
		rxrpc_disconnect_client_call(call->bundle, call);
	} else {
		__rxrpc_disconnect_call(conn, call);
		conn->idle_timestamp = jiffies;
		if (atomic_dec_and_test(&conn->active))
			rxrpc_set_service_reap_timer(conn->rxnet,
						     jiffies + rxrpc_connection_expiry * HZ);
	}

	rxrpc_put_call(call, rxrpc_call_put_io_thread);
}

/*
 * Queue a connection's work processor, getting a ref to pass to the work
 * queue.
 */
void rxrpc_queue_conn(struct rxrpc_connection *conn, enum rxrpc_conn_trace why)
{
	if (atomic_read(&conn->active) >= 0 &&
	    rxrpc_queue_work(&conn->processor))
		rxrpc_see_connection(conn, why);
}

/*
 * Note the re-emergence of a connection.
 */
void rxrpc_see_connection(struct rxrpc_connection *conn,
			  enum rxrpc_conn_trace why)
{
	if (conn) {
		int r = refcount_read(&conn->ref);

		trace_rxrpc_conn(conn->debug_id, r, why);
	}
}

/*
 * Get a ref on a connection.
 */
struct rxrpc_connection *rxrpc_get_connection(struct rxrpc_connection *conn,
					      enum rxrpc_conn_trace why)
{
	int r;

	__refcount_inc(&conn->ref, &r);
	trace_rxrpc_conn(conn->debug_id, r + 1, why);
	return conn;
}

/*
 * Try to get a ref on a connection.
 */
struct rxrpc_connection *
rxrpc_get_connection_maybe(struct rxrpc_connection *conn,
			   enum rxrpc_conn_trace why)
{
	int r;

	if (conn) {
		if (__refcount_inc_not_zero(&conn->ref, &r))
			trace_rxrpc_conn(conn->debug_id, r + 1, why);
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
 * destroy a virtual connection
 */
static void rxrpc_rcu_free_connection(struct rcu_head *rcu)
{
	struct rxrpc_connection *conn =
		container_of(rcu, struct rxrpc_connection, rcu);
	struct rxrpc_net *rxnet = conn->rxnet;

	_enter("{%d,u=%d}", conn->debug_id, refcount_read(&conn->ref));

	trace_rxrpc_conn(conn->debug_id, refcount_read(&conn->ref),
			 rxrpc_conn_free);
	kfree(conn);

	if (atomic_dec_and_test(&rxnet->nr_conns))
		wake_up_var(&rxnet->nr_conns);
}

/*
 * Clean up a dead connection.
 */
static void rxrpc_clean_up_connection(struct work_struct *work)
{
	struct rxrpc_connection *conn =
		container_of(work, struct rxrpc_connection, destructor);
	struct rxrpc_net *rxnet = conn->rxnet;

	ASSERT(!conn->channels[0].call &&
	       !conn->channels[1].call &&
	       !conn->channels[2].call &&
	       !conn->channels[3].call);
	ASSERT(list_empty(&conn->cache_link));

	timer_delete_sync(&conn->timer);
	cancel_work_sync(&conn->processor); /* Processing may restart the timer */
	timer_delete_sync(&conn->timer);

	write_lock(&rxnet->conn_lock);
	list_del_init(&conn->proc_link);
	write_unlock(&rxnet->conn_lock);

	if (conn->pmtud_probe) {
		trace_rxrpc_pmtud_lost(conn, 0);
		conn->peer->pmtud_probing = false;
		conn->peer->pmtud_pending = true;
	}

	rxrpc_purge_queue(&conn->rx_queue);

	rxrpc_kill_client_conn(conn);

	conn->security->clear(conn);
	key_put(conn->key);
	rxrpc_put_bundle(conn->bundle, rxrpc_bundle_put_conn);
	rxrpc_put_peer(conn->peer, rxrpc_peer_put_conn);
	rxrpc_put_local(conn->local, rxrpc_local_put_kill_conn);

	/* Drain the Rx queue.  Note that even though we've unpublished, an
	 * incoming packet could still be being added to our Rx queue, so we
	 * will need to drain it again in the RCU cleanup handler.
	 */
	rxrpc_purge_queue(&conn->rx_queue);

	page_frag_cache_drain(&conn->tx_data_alloc);
	call_rcu(&conn->rcu, rxrpc_rcu_free_connection);
}

/*
 * Drop a ref on a connection.
 */
void rxrpc_put_connection(struct rxrpc_connection *conn,
			  enum rxrpc_conn_trace why)
{
	unsigned int debug_id;
	bool dead;
	int r;

	if (!conn)
		return;

	debug_id = conn->debug_id;
	dead = __refcount_dec_and_test(&conn->ref, &r);
	trace_rxrpc_conn(debug_id, r - 1, why);
	if (dead) {
		timer_delete(&conn->timer);
		cancel_work(&conn->processor);

		if (in_softirq() || work_busy(&conn->processor) ||
		    timer_pending(&conn->timer))
			/* Can't use the rxrpc workqueue as we need to cancel/flush
			 * something that may be running/waiting there.
			 */
			schedule_work(&conn->destructor);
		else
			rxrpc_clean_up_connection(&conn->destructor);
	}
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
	int active;

	LIST_HEAD(graveyard);

	_enter("");

	now = jiffies;
	earliest = now + MAX_JIFFY_OFFSET;

	write_lock(&rxnet->conn_lock);
	list_for_each_entry_safe(conn, _p, &rxnet->service_conns, link) {
		ASSERTCMP(atomic_read(&conn->active), >=, 0);
		if (likely(atomic_read(&conn->active) > 0))
			continue;
		if (conn->state == RXRPC_CONN_SERVICE_PREALLOC)
			continue;

		if (rxnet->live && !conn->local->dead) {
			idle_timestamp = READ_ONCE(conn->idle_timestamp);
			expire_at = idle_timestamp + rxrpc_connection_expiry * HZ;
			if (conn->local->service_closed)
				expire_at = idle_timestamp + rxrpc_closed_conn_expiry * HZ;

			_debug("reap CONN %d { a=%d,t=%ld }",
			       conn->debug_id, atomic_read(&conn->active),
			       (long)expire_at - (long)now);

			if (time_before(now, expire_at)) {
				if (time_before(expire_at, earliest))
					earliest = expire_at;
				continue;
			}
		}

		/* The activity count sits at 0 whilst the conn is unused on
		 * the list; we reduce that to -1 to make the conn unavailable.
		 */
		active = 0;
		if (!atomic_try_cmpxchg(&conn->active, &active, -1))
			continue;
		rxrpc_see_connection(conn, rxrpc_conn_see_reap_service);

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

		ASSERTCMP(atomic_read(&conn->active), ==, -1);
		rxrpc_put_connection(conn, rxrpc_conn_put_service_reaped);
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

	timer_delete_sync(&rxnet->service_conn_reap_timer);
	rxrpc_queue_work(&rxnet->service_conn_reaper);
	flush_workqueue(rxrpc_workqueue);

	write_lock(&rxnet->conn_lock);
	list_for_each_entry_safe(conn, _p, &rxnet->service_conns, link) {
		pr_err("AF_RXRPC: Leaked conn %p {%d}\n",
		       conn, refcount_read(&conn->ref));
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
