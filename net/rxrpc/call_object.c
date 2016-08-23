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

/*
 * Time till dead call expires after last use (in jiffies).
 */
unsigned int rxrpc_dead_call_expiry = 2 * HZ;

const char *const rxrpc_call_states[NR__RXRPC_CALL_STATES] = {
	[RXRPC_CALL_UNINITIALISED]		= "Uninit",
	[RXRPC_CALL_CLIENT_AWAIT_CONN]		= "ClWtConn",
	[RXRPC_CALL_CLIENT_SEND_REQUEST]	= "ClSndReq",
	[RXRPC_CALL_CLIENT_AWAIT_REPLY]		= "ClAwtRpl",
	[RXRPC_CALL_CLIENT_RECV_REPLY]		= "ClRcvRpl",
	[RXRPC_CALL_CLIENT_FINAL_ACK]		= "ClFnlACK",
	[RXRPC_CALL_SERVER_SECURING]		= "SvSecure",
	[RXRPC_CALL_SERVER_ACCEPTING]		= "SvAccept",
	[RXRPC_CALL_SERVER_RECV_REQUEST]	= "SvRcvReq",
	[RXRPC_CALL_SERVER_ACK_REQUEST]		= "SvAckReq",
	[RXRPC_CALL_SERVER_SEND_REPLY]		= "SvSndRpl",
	[RXRPC_CALL_SERVER_AWAIT_ACK]		= "SvAwtACK",
	[RXRPC_CALL_COMPLETE]			= "Complete",
	[RXRPC_CALL_SERVER_BUSY]		= "SvBusy  ",
	[RXRPC_CALL_REMOTELY_ABORTED]		= "RmtAbort",
	[RXRPC_CALL_LOCALLY_ABORTED]		= "LocAbort",
	[RXRPC_CALL_NETWORK_ERROR]		= "NetError",
	[RXRPC_CALL_DEAD]			= "Dead    ",
};

struct kmem_cache *rxrpc_call_jar;
LIST_HEAD(rxrpc_calls);
DEFINE_RWLOCK(rxrpc_call_lock);

static void rxrpc_destroy_call(struct work_struct *work);
static void rxrpc_call_life_expired(unsigned long _call);
static void rxrpc_dead_call_expired(unsigned long _call);
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
	rxrpc_get_call(call);
	read_unlock(&rx->call_lock);
	_leave(" = %p [%d]", call, atomic_read(&call->usage));
	return call;
}

/*
 * allocate a new call
 */
static struct rxrpc_call *rxrpc_alloc_call(gfp_t gfp)
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
	setup_timer(&call->deadspan, &rxrpc_dead_call_expired,
		    (unsigned long) call);
	setup_timer(&call->ack_timer, &rxrpc_ack_time_expired,
		    (unsigned long) call);
	setup_timer(&call->resend_timer, &rxrpc_resend_time_expired,
		    (unsigned long) call);
	INIT_WORK(&call->destroyer, &rxrpc_destroy_call);
	INIT_WORK(&call->processor, &rxrpc_process_call);
	INIT_LIST_HEAD(&call->link);
	INIT_LIST_HEAD(&call->accept_link);
	skb_queue_head_init(&call->rx_queue);
	skb_queue_head_init(&call->rx_oos_queue);
	init_waitqueue_head(&call->tx_waitq);
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

	sock_hold(&rx->sk);
	call->socket = rx;
	call->rx_data_post = 1;
	call->service_id = srx->srx_service;

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

	call->state = RXRPC_CALL_CLIENT_SEND_REQUEST;

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
	int ret;

	_enter("%p,%lx", rx, user_call_ID);

	call = rxrpc_alloc_client_call(rx, srx, gfp);
	if (IS_ERR(call)) {
		_leave(" = %ld", PTR_ERR(call));
		return call;
	}

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

	rxrpc_get_call(call);

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
	rxrpc_put_call(call);

	write_lock_bh(&rxrpc_call_lock);
	list_del_init(&call->link);
	write_unlock_bh(&rxrpc_call_lock);

	set_bit(RXRPC_CALL_RELEASED, &call->flags);
	call->state = RXRPC_CALL_DEAD;
	rxrpc_put_call(call);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

	/* We unexpectedly found the user ID in the list after taking
	 * the call_lock.  This shouldn't happen unless the user races
	 * with itself and tries to add the same user ID twice at the
	 * same time in different threads.
	 */
found_user_ID_now_present:
	write_unlock(&rx->call_lock);
	set_bit(RXRPC_CALL_RELEASED, &call->flags);
	call->state = RXRPC_CALL_DEAD;
	rxrpc_put_call(call);
	_leave(" = -EEXIST [%p]", call);
	return ERR_PTR(-EEXIST);
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
	u32 call_id, chan;

	_enter(",%d", conn->debug_id);

	ASSERT(rx != NULL);

	candidate = rxrpc_alloc_call(GFP_NOIO);
	if (!candidate)
		return ERR_PTR(-EBUSY);

	chan = sp->hdr.cid & RXRPC_CHANNELMASK;
	candidate->socket	= rx;
	candidate->conn		= conn;
	candidate->cid		= sp->hdr.cid;
	candidate->call_id	= sp->hdr.callNumber;
	candidate->channel	= chan;
	candidate->rx_data_post	= 0;
	candidate->state	= RXRPC_CALL_SERVER_ACCEPTING;
	candidate->flags	|= (1 << RXRPC_CALL_IS_SERVICE);
	if (conn->security_ix > 0)
		candidate->state = RXRPC_CALL_SERVER_SECURING;

	spin_lock(&conn->channel_lock);

	/* set the channel for this call */
	call = rcu_dereference_protected(conn->channels[chan].call,
					 lockdep_is_held(&conn->channel_lock));

	_debug("channel[%u] is %p", candidate->channel, call);
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
			rxrpc_get_call(call);
			read_unlock(&call->state_lock);
			goto extant_call;
		}
	}

	if (call) {
		/* it seems the channel is still in use from the previous call
		 * - ditch the old binding if its call is now complete */
		_debug("CALL: %u { %s }",
		       call->debug_id, rxrpc_call_states[call->state]);

		if (call->state >= RXRPC_CALL_COMPLETE) {
			__rxrpc_disconnect_call(call);
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

	/* make the call available */
	_debug("new call");
	call = candidate;
	candidate = NULL;
	conn->channels[chan].call_counter = call_id;
	rcu_assign_pointer(conn->channels[chan].call, call);
	sock_hold(&rx->sk);
	rxrpc_get_connection(conn);
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
 * detach a call from a socket and set up for release
 */
void rxrpc_release_call(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_sock *rx = call->socket;

	_enter("{%d,%d,%d,%d}",
	       call->debug_id, atomic_read(&call->usage),
	       atomic_read(&call->ackr_not_idle),
	       call->rx_first_oos);

	spin_lock_bh(&call->lock);
	if (test_and_set_bit(RXRPC_CALL_RELEASED, &call->flags))
		BUG();
	spin_unlock_bh(&call->lock);

	/* dissociate from the socket
	 * - the socket's ref on the call is passed to the death timer
	 */
	_debug("RELEASE CALL %p (%d CONN %p)", call, call->debug_id, conn);

	spin_lock(&conn->params.peer->lock);
	hlist_del_init(&call->error_link);
	spin_unlock(&conn->params.peer->lock);

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
	}
	write_unlock_bh(&rx->call_lock);

	/* free up the channel for reuse */
	write_lock_bh(&call->state_lock);

	if (call->state < RXRPC_CALL_COMPLETE &&
	    call->state != RXRPC_CALL_CLIENT_FINAL_ACK) {
		_debug("+++ ABORTING STATE %d +++\n", call->state);
		call->state = RXRPC_CALL_LOCALLY_ABORTED;
		call->local_abort = RX_CALL_DEAD;
	}
	write_unlock_bh(&call->state_lock);

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

		ASSERTCMP(call->state, !=, RXRPC_CALL_COMPLETE);
	}

	del_timer_sync(&call->resend_timer);
	del_timer_sync(&call->ack_timer);
	del_timer_sync(&call->lifetimer);
	call->deadspan.expires = jiffies + rxrpc_dead_call_expiry;
	add_timer(&call->deadspan);

	_leave("");
}

/*
 * handle a dead call being ready for reaping
 */
static void rxrpc_dead_call_expired(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_enter("{%d}", call->debug_id);

	write_lock_bh(&call->state_lock);
	call->state = RXRPC_CALL_DEAD;
	write_unlock_bh(&call->state_lock);
	rxrpc_put_call(call);
}

/*
 * mark a call as to be released, aborting it if it's still in progress
 * - called with softirqs disabled
 */
static void rxrpc_mark_call_released(struct rxrpc_call *call)
{
	bool sched;

	write_lock(&call->state_lock);
	if (call->state < RXRPC_CALL_DEAD) {
		sched = false;
		if (call->state < RXRPC_CALL_COMPLETE) {
			_debug("abort call %p", call);
			call->state = RXRPC_CALL_LOCALLY_ABORTED;
			call->local_abort = RX_CALL_DEAD;
			if (!test_and_set_bit(RXRPC_CALL_EV_ABORT, &call->events))
				sched = true;
		}
		if (!test_and_set_bit(RXRPC_CALL_EV_RELEASE, &call->events))
			sched = true;
		if (sched)
			rxrpc_queue_call(call);
	}
	write_unlock(&call->state_lock);
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

	/* mark all the calls as no longer wanting incoming packets */
	for (p = rb_first(&rx->calls); p; p = rb_next(p)) {
		call = rb_entry(p, struct rxrpc_call, sock_node);
		rxrpc_mark_call_released(call);
	}

	/* kill the not-yet-accepted incoming calls */
	list_for_each_entry(call, &rx->secureq, accept_link) {
		rxrpc_mark_call_released(call);
	}

	list_for_each_entry(call, &rx->acceptq, accept_link) {
		rxrpc_mark_call_released(call);
	}

	read_unlock_bh(&rx->call_lock);
	_leave("");
}

/*
 * release a call
 */
void __rxrpc_put_call(struct rxrpc_call *call)
{
	ASSERT(call != NULL);

	_enter("%p{u=%d}", call, atomic_read(&call->usage));

	ASSERTCMP(atomic_read(&call->usage), >, 0);

	if (atomic_dec_and_test(&call->usage)) {
		_debug("call %d dead", call->debug_id);
		WARN_ON(atomic_read(&call->skb_count) != 0);
		ASSERTCMP(call->state, ==, RXRPC_CALL_DEAD);
		rxrpc_queue_work(&call->destroyer);
	}
	_leave("");
}

/*
 * Final call destruction under RCU.
 */
static void rxrpc_rcu_destroy_call(struct rcu_head *rcu)
{
	struct rxrpc_call *call = container_of(rcu, struct rxrpc_call, rcu);

	rxrpc_purge_queue(&call->rx_queue);
	kmem_cache_free(rxrpc_call_jar, call);
}

/*
 * clean up a call
 */
static void rxrpc_cleanup_call(struct rxrpc_call *call)
{
	_net("DESTROY CALL %d", call->debug_id);

	ASSERT(call->socket);

	memset(&call->sock_node, 0xcd, sizeof(call->sock_node));

	del_timer_sync(&call->lifetimer);
	del_timer_sync(&call->deadspan);
	del_timer_sync(&call->ack_timer);
	del_timer_sync(&call->resend_timer);

	ASSERT(test_bit(RXRPC_CALL_RELEASED, &call->flags));
	ASSERTCMP(call->events, ==, 0);
	if (work_pending(&call->processor)) {
		_debug("defer destroy");
		rxrpc_queue_work(&call->destroyer);
		return;
	}

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
	sock_put(&call->socket->sk);
	call_rcu(&call->rcu, rxrpc_rcu_destroy_call);
}

/*
 * destroy a call
 */
static void rxrpc_destroy_call(struct work_struct *work)
{
	struct rxrpc_call *call =
		container_of(work, struct rxrpc_call, destroyer);

	_enter("%p{%d,%d,%p}",
	       call, atomic_read(&call->usage), call->channel, call->conn);

	ASSERTCMP(call->state, ==, RXRPC_CALL_DEAD);

	write_lock_bh(&rxrpc_call_lock);
	list_del_init(&call->link);
	write_unlock_bh(&rxrpc_call_lock);

	rxrpc_cleanup_call(call);
	_leave("");
}

/*
 * preemptively destroy all the call records from a transport endpoint rather
 * than waiting for them to time out
 */
void __exit rxrpc_destroy_all_calls(void)
{
	struct rxrpc_call *call;

	_enter("");
	write_lock_bh(&rxrpc_call_lock);

	while (!list_empty(&rxrpc_calls)) {
		call = list_entry(rxrpc_calls.next, struct rxrpc_call, link);
		_debug("Zapping call %p", call);

		list_del_init(&call->link);

		switch (atomic_read(&call->usage)) {
		case 0:
			ASSERTCMP(call->state, ==, RXRPC_CALL_DEAD);
			break;
		case 1:
			if (del_timer_sync(&call->deadspan) != 0 &&
			    call->state != RXRPC_CALL_DEAD)
				rxrpc_dead_call_expired((unsigned long) call);
			if (call->state != RXRPC_CALL_DEAD)
				break;
		default:
			pr_err("Call %p still in use (%d,%d,%s,%lx,%lx)!\n",
			       call, atomic_read(&call->usage),
			       atomic_read(&call->ackr_not_idle),
			       rxrpc_call_states[call->state],
			       call->flags, call->events);
			if (!skb_queue_empty(&call->rx_queue))
				pr_err("Rx queue occupied\n");
			if (!skb_queue_empty(&call->rx_oos_queue))
				pr_err("OOS queue occupied\n");
			break;
		}

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

	if (call->state >= RXRPC_CALL_COMPLETE)
		return;

	_enter("{%d}", call->debug_id);
	read_lock_bh(&call->state_lock);
	if (call->state < RXRPC_CALL_COMPLETE) {
		set_bit(RXRPC_CALL_EV_LIFE_TIMER, &call->events);
		rxrpc_queue_call(call);
	}
	read_unlock_bh(&call->state_lock);
}

/*
 * handle resend timer expiry
 * - may not take call->state_lock as this can deadlock against del_timer_sync()
 */
static void rxrpc_resend_time_expired(unsigned long _call)
{
	struct rxrpc_call *call = (struct rxrpc_call *) _call;

	_enter("{%d}", call->debug_id);

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

	if (call->state >= RXRPC_CALL_COMPLETE)
		return;

	read_lock_bh(&call->state_lock);
	if (call->state < RXRPC_CALL_COMPLETE &&
	    !test_and_set_bit(RXRPC_CALL_EV_ACK, &call->events))
		rxrpc_queue_call(call);
	read_unlock_bh(&call->state_lock);
}
