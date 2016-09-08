/* RxRPC individual remote procedure call handling
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/circ_buf.h>
#include <linux/spinlock_types.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * Maximum lifetime of a call (in jiffies).
 */
unsigned int rxrpc_max_call_lifetime = 60 * HZ;

const char *const rxrpc_call_states[NR__RXRPC_CALL_STATES] = {
	[RXRPC_CALL_UNINITIALISED]		= "Uninit  ",
	[RXRPC_CALL_CLIENT_AWAIT_CONN]		= "ClWtConn",
	[RXRPC_CALL_CLIENT_SEND_REQUEST]	= "ClSndReq",
	[RXRPC_CALL_CLIENT_AWAIT_REPLY]		= "ClAwtRpl",
	[RXRPC_CALL_CLIENT_RECV_REPLY]		= "ClRcvRpl",
	[RXRPC_CALL_CLIENT_FINAL_ACK]		= "ClFnlACK",
	[RXRPC_CALL_SERVER_PREALLOC]		= "SvPrealc",
	[RXRPC_CALL_SERVER_SECURING]		= "SvSecure",
	[RXRPC_CALL_SERVER_ACCEPTING]		= "SvAccept",
	[RXRPC_CALL_SERVER_RECV_REQUEST]	= "SvRcvReq",
	[RXRPC_CALL_SERVER_ACK_REQUEST]		= "SvAckReq",
	[RXRPC_CALL_SERVER_SEND_REPLY]		= "SvSndRpl",
	[RXRPC_CALL_SERVER_AWAIT_ACK]		= "SvAwtACK",
	[RXRPC_CALL_COMPLETE]			= "Complete",
};

const char *const rxrpc_call_completions[NR__RXRPC_CALL_COMPLETIONS] = {
	[RXRPC_CALL_SUCCEEDED]			= "Complete",
	[RXRPC_CALL_SERVER_BUSY]		= "SvBusy  ",
	[RXRPC_CALL_REMOTELY_ABORTED]		= "RmtAbort",
	[RXRPC_CALL_LOCALLY_ABORTED]		= "LocAbort",
	[RXRPC_CALL_LOCAL_ERROR]		= "LocError",
	[RXRPC_CALL_NETWORK_ERROR]		= "NetError",
};

const char rxrpc_call_traces[rxrpc_call__nr_trace][4] = {
	[rxrpc_call_new_client]		= "NWc",
	[rxrpc_call_new_service]	= "NWs",
	[rxrpc_call_queued]		= "QUE",
	[rxrpc_call_queued_ref]		= "QUR",
	[rxrpc_call_seen]		= "SEE",
	[rxrpc_call_got]		= "GOT",
	[rxrpc_call_got_skb]		= "Gsk",
	[rxrpc_call_got_userid]		= "Gus",
	[rxrpc_call_put]		= "PUT",
	[rxrpc_call_put_skb]		= "Psk",
	[rxrpc_call_put_userid]		= "Pus",
	[rxrpc_call_put_noqueue]	= "PNQ",
};

struct kmem_cache *rxrpc_call_jar;
LIST_HEAD(rxrpc_calls);
DEFINE_RWLOCK(rxrpc_call_lock);

static void rxrpc_call_life_expired(unsigned long _call);
static void rxrpc_ack_time_expired(unsigned long _call);
static void rxrpc_resend_time_expired(unsigned long _call);

/*
 * find an extant server call
 * - called in process context with IRQs enabled
 */
struct rxrpc_call *rxrpc_find_call_by_user_ID(struct rxrpc_sock *rx,
					      unsigned long user_call_ID)
{
	struct rxrpc_call *call;
	struct rb_node *p;

	_enter("%p,%lx", rx, user_call_ID);

	read_lock(&rx->call_lock);

	p = rx->calls.rb_node;
	while (p) {
		call = rb_entry(p, struct rxrpc_call, sock_node);

		if (user_call_ID < call->user_call_ID)
			p = p->rb_left;
		else if (user_call_ID > call->user_call_ID)
			p = p->rb_right;
		else
			goto found_extant_call;
	}

	read_unlock(&rx->call_lock);
	_leave(" = NULL");
	return NULL;

found_extant_call:
	rxrpc_get_call(call, rxrpc_call_got);
	read_unlock(&rx->call_lock);
	_leave(" = %p [%d]", call, atomic_read(&call->usage));
	return call;
}

/*
 * allocate a new call
 */
struct rxrpc_call *rxrpc_alloc_call(gfp_t gfp)
{
	struct rxrpc_call *call;

	call = kmem_cache_zalloc(rxrpc_call_jar, gfp);
	if (!call)
		return NULL;

	call->acks_winsz = 16;
	call->acks_window = kmalloc(call->acks_winsz * sizeof(unsigned long),
				    gfp);
	if (!call->acks_window) {
		kmem_cache_free(rxrpc_call_jar, call);
		return NULL;
	}

	setup_timer(&call->lifetimer, &rxrpc_call_life_expired,
		    (unsigned long) call);
	setup_timer(&call->ack_timer, &rxrpc_ack_time_expired,
		    (unsigned long) call);
	setup_timer(&call->resend_timer, &rxrpc_resend_time_expired,
		    (unsigned long) call);
	INIT_WORK(&call->processor, &rxrpc_process_call);
	INIT_LIST_HEAD(&call->link);
	INIT_LIST_HEAD(&call->chan_wait_link);
	INIT_LIST_HEAD(&call->accept_link);
	skb_queue_head_init(&call->rx_queue);
	skb_queue_head_init(&call->rx_oos_queue);
	skb_queue_head_init(&call->knlrecv_queue);
	init_waitqueue_head(&call->waitq);
	spin_lock_init(&call->lock);
	rwlock_init(&call->state_lock);
	atomic_set(&call->usage, 1);
	call->debug_id = atomic_inc_return(&rxrpc_debug_id);

	memset(&call->sock_node, 0xed, sizeof(call->sock_node));

	call->rx_data_expect = 1;
	call->rx_data_eaten = 0;
	call->rx_first_oos = 0;
	call->ackr_win_top = call->rx_data_eaten + 1 + rxrpc_rx_window_size;
	call->creation_jif = jiffies;
	return call;
}

/*
 * Allocate a new client call.
 */
static struct rxrpc_call *rxrpc_alloc_client_call(struct rxrpc_sock *rx,
						  struct sockaddr_rxrpc *srx,
						  gfp_t gfp)
{
	struct rxrpc_call *call;

	_enter("");

	ASSERT(rx->local != NULL);

	call = rxrpc_alloc_call(gfp);
	if (!call)
		return ERR_PTR(-ENOMEM);
	call->state = RXRPC_CALL_CLIENT_AWAIT_CONN;
	call->rx_data_post = 1;
	call->service_id = srx->srx_service;
	rcu_assign_pointer(call->socket, rx);

	_leave(" = %p", call);
	return call;
}

/*
 * Begin client call.
 */
static int rxrpc_begin_client_call(struct rxrpc_call *call,
				   struct rxrpc_conn_parameters *cp,
				   struct sockaddr_rxrpc *srx,
				   gfp_t gfp)
{
	int ret;

	/* Set up or get a connection record and set the protocol parameters,
	 * including channel number and call ID.
	 */
	ret = rxrpc_connect_call(call, cp, srx, gfp);
	if (ret < 0)
		return ret;

	spin_lock(&call->conn->params.peer->lock);
	hlist_add_head(&call->error_link, &call->conn->params.peer->error_targets);
	spin_unlock(&call->conn->params.peer->lock);

	call->lifetimer.expires = jiffies + rxrpc_max_call_lifetime;
	add_timer(&call->lifetimer);
	return 0;
}

/*
 * set up a call for the given data
 * - called in process context with IRQs enabled
 */
struct rxrpc_call *rxrpc_new_client_call(struct rxrpc_sock *rx,
					 struct rxrpc_conn_parameters *cp,
					 struct sockaddr_rxrpc *srx,
					 unsigned long user_call_ID,
					 gfp_t gfp)
{
	struct rxrpc_call *call, *xcall;
	struct rb_node *parent, **pp;
	const void *here = __builtin_return_address(0);
	int ret;

	_enter("%p,%lx", rx, user_call_ID);

	call = rxrpc_alloc_client_call(rx, srx, gfp);
	if (IS_ERR(call)) {
		_leave(" = %ld", PTR_ERR(call));
		return call;
	}

	trace_rxrpc_call(call, 0, atomic_read(&call->usage), here,
			 (const void *)user_call_ID);

	/* Publish the call, even though it is incompletely set up as yet */
	call->user_call_ID = user_call_ID;
	__set_bit(RXRPC_CALL_HAS_USERID, &call->flags);

	write_lock(&rx->call_lock);

	pp = &rx->calls.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		xcall = rb_entry(parent, struct rxrpc_call, sock_node);

		if (user_call_ID < xcall->user_call_ID)
			pp = &(*pp)->rb_left;
		else if (user_call_ID > xcall->user_call_ID)
			pp = &(*pp)->rb_right;
		else
			goto found_user_ID_now_present;
	}

	rxrpc_get_call(call, rxrpc_call_got_userid);
	rb_link_node(&call->sock_node, parent, pp);
	rb_insert_color(&call->sock_node, &rx->calls);
	write_unlock(&rx->call_lock);

	write_lock_bh(&rxrpc_call_lock);
	list_add_tail(&call->link, &rxrpc_calls);
	write_unlock_bh(&rxrpc_call_lock);

	ret = rxrpc_begin_client_call(call, cp, srx, gfp);
	if (ret < 0)
		goto error;

	_net("CALL new %d on CONN %d", call->debug_id, call->conn->debug_id);

	_leave(" = %p [new]", call);
	return call;

error:
	write_lock(&rx->call_lock);
	rb_erase(&call->sock_node, &rx->calls);
	write_unlock(&rx->call_lock);
	rxrpc_put_call(call, rxrpc_call_put_userid);

	write_lock_bh(&rxrpc_call_lock);
	list_del_init(&call->link);
	write_unlock_bh(&rxrpc_call_lock);

error_out:
	__rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR,
				    RX_CALL_DEAD, ret);
	set_bit(RXRPC_CALL_RELEASED, &call->flags);
	rxrpc_put_call(call, rxrpc_call_put);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

	/* We unexpectedly found the user ID in the list after taking
	 * the call_lock.  This shouldn't happen unless the user races
	 * with itself and tries to add the same user ID twice at the
	 * same time in different threads.
	 */
found_user_ID_now_present:
	write_unlock(&rx->call_lock);
	ret = -EEXIST;
	goto error_out;
}

/*
 * set up an incoming call
 * - called in process context with IRQs enabled
 */
struct rxrpc_call *rxrpc_incoming_call(struct rxrpc_sock *rx,
				       struct rxrpc_connection *conn,
				       struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_call *call, *candidate;
	const void *here = __builtin_return_address(0);
	u32 call_id, chan;

	_enter(",%d", conn->debug_id);

	ASSERT(rx != NULL);

	candidate = rxrpc_alloc_call(GFP_NOIO);
	if (!candidate)
		return ERR_PTR(-EBUSY);

	trace_rxrpc_call(candidate, rxrpc_call_new_service,
			 atomic_read(&candidate->usage), here, NULL);

	chan = sp->hdr.cid & RXRPC_CHANNELMASK;
	candidate->conn		= conn;
	candidate->peer		= conn->params.peer;
	candidate->cid		= sp->hdr.cid;
	candidate->call_id	= sp->hdr.callNumber;
	candidate->security_ix	= sp->hdr.securityIndex;
	candidate->rx_data_post	= 0;
	candidate->state	= RXRPC_CALL_SERVER_ACCEPTING;
	candidate->flags	|= (1 << RXRPC_CALL_IS_SERVICE);
	if (conn->security_ix > 0)
		candidate->state = RXRPC_CALL_SERVER_SECURING;
	rcu_assign_pointer(candidate->socket, rx);

	spin_lock(&conn->channel_lock);

	/* set the channel for this call */
	call = rcu_dereference_protected(conn->channels[chan].call,
					 lockdep_is_held(&conn->channel_lock));

	_debug("channel[%u] is %p", candidate->cid & RXRPC_CHANNELMASK, call);
	if (call && call->call_id == sp->hdr.callNumber) {
		/* already set; must've been a duplicate packet */
		_debug("extant call [%d]", call->state);
		ASSERTCMP(call->conn, ==, conn);

		read_lock(&call->state_lock);
		switch (call->state) {
		case RXRPC_CALL_LOCALLY_ABORTED:
			if (!test_and_set_bit(RXRPC_CALL_EV_ABORT, &call->events))
				rxrpc_queue_call(call);
		case RXRPC_CALL_REMOTELY_ABORTED:
			read_unlock(&call->state_lock);
			goto aborted_call;
		default:
			rxrpc_get_call(call, rxrpc_call_got);
			read_unlock(&call->state_lock);
			goto extant_call;
		}
	}

	if (call) {
		/* it seems the channel is still in use from the previous call
		 * - ditch the old binding if its call is now complete */
		_debug("CALL: %u { %s }",
		       call->debug_id, rxrpc_call_states[call->state]);

		if (call->state == RXRPC_CALL_COMPLETE) {
			__rxrpc_disconnect_call(conn, call);
		} else {
			spin_unlock(&conn->channel_lock);
			kmem_cache_free(rxrpc_call_jar, candidate);
			_leave(" = -EBUSY");
			return ERR_PTR(-EBUSY);
		}
	}

	/* check the call number isn't duplicate */
	_debug("check dup");
	call_id = sp->hdr.callNumber;

	/* We just ignore calls prior to the current call ID.  Terminated calls
	 * are handled via the connection.
	 */
	if (call_id <= conn->channels[chan].call_counter)
		goto old_call; /* TODO: Just drop packet */

	/* Temporary: Mirror the backlog prealloc ref (TODO: use prealloc) */
	rxrpc_get_call(candidate, rxrpc_call_got);

	/* make the call available */
	_debug("new call");
	call = candidate;
	candidate = NULL;
	conn->channels[chan].call_counter = call_id;
	rcu_assign_pointer(conn->channels[chan].call, call);
	rxrpc_get_connection(conn);
	rxrpc_get_peer(call->peer);
	spin_unlock(&conn->channel_lock);

	spin_lock(&conn->params.peer->lock);
	hlist_add_head(&call->error_link, &conn->params.peer->error_targets);
	spin_unlock(&conn->params.peer->lock);

	write_lock_bh(&rxrpc_call_lock);
	list_add_tail(&call->link, &rxrpc_calls);
	write_unlock_bh(&rxrpc_call_lock);

	call->service_id = conn->params.service_id;

	_net("CALL incoming %d on CONN %d", call->debug_id, call->conn->debug_id);

	call->lifetimer.expires = jiffies + rxrpc_max_call_lifetime;
	add_timer(&call->lifetimer);
	_leave(" = %p {%d} [new]", call, call->debug_id);
	return call;

extant_call:
	spin_unlock(&conn->channel_lock);
	kmem_cache_free(rxrpc_call_jar, candidate);
	_leave(" = %p {%d} [extant]", call, call ? call->debug_id : -1);
	return call;

aborted_call:
	spin_unlock(&conn->channel_lock);
	kmem_cache_free(rxrpc_call_jar, candidate);
	_leave(" = -ECONNABORTED");
	return ERR_PTR(-ECONNABORTED);

old_call:
	spin_unlock(&conn->channel_lock);
	kmem_cache_free(rxrpc_call_jar, candidate);
	_leave(" = -ECONNRESET [old]");
	return ERR_PTR(-ECONNRESET);
}

/*
 * Queue a call's work processor, getting a ref to pass to the work queue.
 */
bool rxrpc_queue_call(struct rxrpc_call *call)
{
	const void *here = __builtin_return_address(0);
	int n = __atomic_add_unless(&call->usage, 1, 0);
	if (n == 0)
		return false;
	if (rxrpc_queue_work(&call->processor))
		trace_rxrpc_call(call, rxrpc_call_queued, n + 1, here, NULL);
	else
		rxrpc_put_call(call, rxrpc_call_put_noqueue);
	return true;
}

/*
 * Queue a call's work processor, passing the callers ref to the work queue.
 */
bool __rxrpc_queue_call(struct rxrpc_call *call)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_read(&call->usage);
	ASSERTCMP(n, >=, 1);
	if (rxrpc_queue_work(&call->processor))
		trace_rxrpc_call(call, rxrpc_call_queued_ref, n, here, NULL);
	else
		rxrpc_put_call(call, rxrpc_call_put_noqueue);
	return true;
}

/*
 * Note the re-emergence of a call.
 */
void rxrpc_see_call(struct rxrpc_call *call)
{
	const void *here = __builtin_return_address(0);
	if (call) {
		int n = atomic_read(&call->usage);

		trace_rxrpc_call(call, rxrpc_call_seen, n, here, NULL);
	}
}

/*
 * Note the addition of a ref on a call.
 */
void rxrpc_get_call(struct rxrpc_call *call, enum rxrpc_call_trace op)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(&call->usage);

	trace_rxrpc_call(call, op, n, here, NULL);
}

/*
 * Note the addition of a ref on a call for a socket buffer.
 */
void rxrpc_get_call_for_skb(struct rxrpc_call *call, struct sk_buff *skb)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(&call->usage);

	trace_rxrpc_call(call, rxrpc_call_got_skb, n, here, skb);
}

/*
 * detach a call from a socket and set up for release
 */
void rxrpc_release_call(struct rxrpc_sock *rx, struct rxrpc_call *call)
{
	_enter("{%d,%d,%d,%d}",
	       call->debug_id, atomic_read(&call->usage),
	       atomic_read(&call->ackr_not_idle),
	       call->rx_first_oos);

	rxrpc_see_call(call);

	spin_lock_bh(&call->lock);
	if (test_and_set_bit(RXRPC_CALL_RELEASED, &call->flags))
		BUG();
	spin_unlock_bh(&call->lock);

	/* dissociate from the socket
	 * - the socket's ref on the call is passed to the death timer
	 */
	_debug("RELEASE CALL %p (%d)", call, call->debug_id);

	if (call->peer) {
		spin_lock(&call->peer->lock);
		hlist_del_init(&call->error_link);
		spin_unlock(&call->peer->lock);
	}

	write_lock_bh(&rx->call_lock);
	if (!list_empty(&call->accept_link)) {
		_debug("unlinking once-pending call %p { e=%lx f=%lx }",
		       call, call->events, call->flags);
		ASSERT(!test_bit(RXRPC_CALL_HAS_USERID, &call->flags));
		list_del_init(&call->accept_link);
		sk_acceptq_removed(&rx->sk);
	} else if (test_bit(RXRPC_CALL_HAS_USERID, &call->flags)) {
		rb_erase(&call->sock_node, &rx->calls);
		memset(&call->sock_node, 0xdd, sizeof(call->sock_node));
		clear_bit(RXRPC_CALL_HAS_USERID, &call->flags);
		rxrpc_put_call(call, rxrpc_call_put_userid);
	}
	write_unlock_bh(&rx->call_lock);

	/* free up the channel for reuse */
	if (call->state == RXRPC_CALL_CLIENT_FINAL_ACK) {
		clear_bit(RXRPC_CALL_EV_ACK_FINAL, &call->events);
		rxrpc_send_call_packet(call, RXRPC_PACKET_TYPE_ACK);
		rxrpc_call_completed(call);
	} else {
		write_lock_bh(&call->state_lock);

		if (call->state < RXRPC_CALL_COMPLETE) {
			_debug("+++ ABORTING STATE %d +++\n", call->state);
			__rxrpc_abort_call("SKT", call, 0, RX_CALL_DEAD, ECONNRESET);
			clear_bit(RXRPC_CALL_EV_ACK_FINAL, &call->events);
			rxrpc_send_call_packet(call, RXRPC_PACKET_TYPE_ABORT);
		}

		write_unlock_bh(&call->state_lock);
	}

	if (call->conn)
		rxrpc_disconnect_call(call);

	/* clean up the Rx queue */
	if (!skb_queue_empty(&call->rx_queue) ||
	    !skb_queue_empty(&call->rx_oos_queue)) {
		struct rxrpc_skb_priv *sp;
		struct sk_buff *skb;

		_debug("purge Rx queues");

		spin_lock_bh(&call->lock);
		while ((skb = skb_dequeue(&call->rx_queue)) ||
		       (skb = skb_dequeue(&call->rx_oos_queue))) {
			spin_unlock_bh(&call->lock);

			sp = rxrpc_skb(skb);
			_debug("- zap %s %%%u #%u",
			       rxrpc_pkts[sp->hdr.type],
			       sp->hdr.serial, sp->hdr.seq);
			rxrpc_free_skb(skb);
			spin_lock_bh(&call->lock);
		}
		spin_unlock_bh(&call->lock);
	}
	rxrpc_purge_queue(&call->knlrecv_queue);

	del_timer_sync(&call->resend_timer);
	del_timer_sync(&call->ack_timer);
	del_timer_sync(&call->lifetimer);

	/* We have to release the prealloc backlog ref */
	if (rxrpc_is_service_call(call))
		rxrpc_put_call(call, rxrpc_call_put);
	_leave("");
}

/*
 * release all the calls associated with a socket
 */
void rxrpc_release_calls_on_socket(struct rxrpc_sock *rx)
{
	struct rxrpc_call *call;
	struct rb_node *p;

	_enter("%p", rx);

	read_lock_bh(&rx->call_lock);

	/* kill the not-yet-accepted incoming calls */
	list_for_each_entry(call, &rx->secureq, accept_link) {
		rxrpc_release_call(rx, call);
	}

	list_for_each_entry(call, &rx->acceptq, accept_link) {
		rxrpc_release_call(rx, call);
	}

	/* mark all the calls as no longer wanting incoming packets */
	for (p = rb_first(&rx->calls); p; p = rb_next(p)) {
		call = rb_entry(p, struct rxrpc_call, sock_node);
		rxrpc_release_call(rx, call);
	}

	read_unlock_bh(&rx->call_lock);
	_leave("");
}

/*
 * release a call
 */
void rxrpc_put_call(struct rxrpc_call *call, enum rxrpc_call_trace op)
{
	const void *here = __builtin_return_address(0);
	int n;

	ASSERT(call != NULL);

	n = atomic_dec_return(&call->usage);
	trace_rxrpc_call(call, op, n, here, NULL);
	ASSERTCMP(n, >=, 0);
	if (n == 0) {
		_debug("call %d dead", call->debug_id);
		rxrpc_cleanup_call(call);
	}
}

/*
 * Release a call ref held by a socket buffer.
 */
void rxrpc_put_call_for_skb(struct rxrpc_call *call, struct sk_buff *skb)
{
	const void *here = __builtin_return_address(0);
	int n;

	n = atomic_dec_return(&call->usage);
	trace_rxrpc_call(call, rxrpc_call_put_skb, n, here, skb);
	ASSERTCMP(n, >=, 0);
	if (n == 0) {
		_debug("call %d dead", call->debug_id);
		rxrpc_cleanup_call(call);
	}
}

/*
 * Final call destruction under RCU.
 */
static void rxrpc_rcu_destroy_call(struct rcu_head *rcu)
{
	struct rxrpc_call *call = container_of(rcu, struct rxrpc_call, rcu);

	rxrpc_purge_queue(&call->rx_queue);
	rxrpc_purge_queue(&call->knlrecv_queue);
	rxrpc_put_peer(call->peer);
	kmem_cache_free(rxrpc_call_jar, call);
}

/*
 * clean up a call
 */
void rxrpc_cleanup_call(struct rxrpc_call *call)
{
	_net("DESTROY CALL %d", call->debug_id);

	write_lock_bh(&rxrpc_call_lock);
	list_del_init(&call->link);
	write_unlock_bh(&rxrpc_call_lock);

	memset(&call->sock_node, 0xcd, sizeof(call->sock_node));

	del_timer_sync(&call->lifetimer);
	del_timer_sync(&call->ack_timer);
	del_timer_sync(&call->resend_timer);

	ASSERTCMP(call->state, ==, RXRPC_CALL_COMPLETE);
	ASSERT(test_bit(RXRPC_CALL_RELEASED, &call->flags));
	ASSERT(!work_pending(&call->processor));
	ASSERTCMP(call->conn, ==, NULL);

	if (call->acks_window) {
		_debug("kill Tx window %d",
		       CIRC_CNT(call->acks_head, call->acks_tail,
				call->acks_winsz));
		smp_mb();
		while (CIRC_CNT(call->acks_head, call->acks_tail,
				call->acks_winsz) > 0) {
			struct rxrpc_skb_priv *sp;
			unsigned long _skb;

			_skb = call->acks_window[call->acks_tail] & ~1;
			sp = rxrpc_skb((struct sk_buff *)_skb);
			_debug("+++ clear Tx %u", sp->hdr.seq);
			rxrpc_free_skb((struct sk_buff *)_skb);
			call->acks_tail =
				(call->acks_tail + 1) & (call->acks_winsz - 1);
		}

		kfree(call->acks_window);
	}

	rxrpc_free_skb(call->tx_pending);

	rxrpc_purge_queue(&call->rx_queue);
	ASSERT(skb_queue_empty(&call->rx_oos_queue));
	rxrpc_purge_queue(&call->knlrecv_queue);
	call_rcu(&call->rcu, rxrpc_rcu_destroy_call);
}

/*
 * Make sure that all calls are gone.
 */
void __exit rxrpc_destroy_all_calls(void)
{
	struct rxrpc_call *call;

	_enter("");

	if (list_empty(&rxrpc_calls))
		return;
	
	write_lock_bh(&rxrpc_call_lock);

	while (!list_empty(&rxrpc_calls)) {
		call = list_entry(rxrpc_calls.next, struct rxrpc_call, link);
		_debug("Zapping call %p", call);

		rxrpc_see_call(call);
		list_del_init(&call->link);

		pr_err("Call %p still in use (%d,%d,%s,%lx,%lx)!\n",
		       call, atomic_read(&call->usage),
		       atomic_read(&call->ackr_not_idle),
		       rxrpc_call_states[call->state],
		       call->flags, call->events);
		if (!skb_queue_empty(&call->rx_queue))
			pr_err("Rx queue occupied\n");
		if (!skb_queue_empty(&call->rx_oos_queue))
			pr_err("OOS queue occupied\n");

		write_unlock_bh(&rxrpc_call_lock);
		cond_resched();
		write_lock_bh(&rxrpc_call_lock);
	}

	write_unlock_bh(&rxrpc_call_lock);
	_leave("");
}

/*
 * handle call lifetime being exceeded
 */
static void rxrpc_call_life_expired(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_enter("{%d}", call->debug_id);

	rxrpc_see_call(call);
	if (call->state >= RXRPC_CALL_COMPLETE)
		return;

	set_bit(RXRPC_CALL_EV_LIFE_TIMER, &call->events);
	rxrpc_queue_call(call);
}

/*
 * handle resend timer expiry
 * - may not take call->state_lock as this can deadlock against del_timer_sync()
 */
static void rxrpc_resend_time_expired(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_enter("{%d}", call->debug_id);

	rxrpc_see_call(call);
	if (call->state >= RXRPC_CALL_COMPLETE)
		return;

	clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
	if (!test_and_set_bit(RXRPC_CALL_EV_RESEND_TIMER, &call->events))
		rxrpc_queue_call(call);
}

/*
 * handle ACK timer expiry
 */
static void rxrpc_ack_time_expired(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_enter("{%d}", call->debug_id);

	rxrpc_see_call(call);
	if (call->state >= RXRPC_CALL_COMPLETE)
		return;

	if (!test_and_set_bit(RXRPC_CALL_EV_ACK, &call->events))
		rxrpc_queue_call(call);
}
