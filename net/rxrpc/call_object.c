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

const char *const rxrpc_call_states[NR__RXRPC_CALL_STATES] = {
	[RXRPC_CALL_UNINITIALISED]		= "Uninit  ",
	[RXRPC_CALL_CLIENT_AWAIT_CONN]		= "ClWtConn",
	[RXRPC_CALL_CLIENT_SEND_REQUEST]	= "ClSndReq",
	[RXRPC_CALL_CLIENT_AWAIT_REPLY]		= "ClAwtRpl",
	[RXRPC_CALL_CLIENT_RECV_REPLY]		= "ClRcvRpl",
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
	[RXRPC_CALL_REMOTELY_ABORTED]		= "RmtAbort",
	[RXRPC_CALL_LOCALLY_ABORTED]		= "LocAbort",
	[RXRPC_CALL_LOCAL_ERROR]		= "LocError",
	[RXRPC_CALL_NETWORK_ERROR]		= "NetError",
};

struct kmem_cache *rxrpc_call_jar;

static void rxrpc_call_timer_expired(struct timer_list *t)
{
	struct rxrpc_call *call = from_timer(call, t, timer);

	_enter("%d", call->debug_id);

	if (call->state < RXRPC_CALL_COMPLETE) {
		trace_rxrpc_timer(call, rxrpc_timer_expired, jiffies);
		rxrpc_queue_call(call);
	}
}

static struct lock_class_key rxrpc_call_user_mutex_lock_class_key;

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
struct rxrpc_call *rxrpc_alloc_call(struct rxrpc_sock *rx, gfp_t gfp,
				    unsigned int debug_id)
{
	struct rxrpc_call *call;
	struct rxrpc_net *rxnet = rxrpc_net(sock_net(&rx->sk));

	call = kmem_cache_zalloc(rxrpc_call_jar, gfp);
	if (!call)
		return NULL;

	call->rxtx_buffer = kcalloc(RXRPC_RXTX_BUFF_SIZE,
				    sizeof(struct sk_buff *),
				    gfp);
	if (!call->rxtx_buffer)
		goto nomem;

	call->rxtx_annotations = kcalloc(RXRPC_RXTX_BUFF_SIZE, sizeof(u8), gfp);
	if (!call->rxtx_annotations)
		goto nomem_2;

	mutex_init(&call->user_mutex);

	/* Prevent lockdep reporting a deadlock false positive between the afs
	 * filesystem and sys_sendmsg() via the mmap sem.
	 */
	if (rx->sk.sk_kern_sock)
		lockdep_set_class(&call->user_mutex,
				  &rxrpc_call_user_mutex_lock_class_key);

	timer_setup(&call->timer, rxrpc_call_timer_expired, 0);
	INIT_WORK(&call->processor, &rxrpc_process_call);
	INIT_LIST_HEAD(&call->link);
	INIT_LIST_HEAD(&call->chan_wait_link);
	INIT_LIST_HEAD(&call->accept_link);
	INIT_LIST_HEAD(&call->recvmsg_link);
	INIT_LIST_HEAD(&call->sock_link);
	init_waitqueue_head(&call->waitq);
	spin_lock_init(&call->lock);
	spin_lock_init(&call->notify_lock);
	spin_lock_init(&call->input_lock);
	rwlock_init(&call->state_lock);
	atomic_set(&call->usage, 1);
	call->debug_id = debug_id;
	call->tx_total_len = -1;
	call->next_rx_timo = 20 * HZ;
	call->next_req_timo = 1 * HZ;

	memset(&call->sock_node, 0xed, sizeof(call->sock_node));

	/* Leave space in the ring to handle a maxed-out jumbo packet */
	call->rx_winsize = rxrpc_rx_window_size;
	call->tx_winsize = 16;
	call->rx_expect_next = 1;

	call->cong_cwnd = 2;
	call->cong_ssthresh = RXRPC_RXTX_BUFF_SIZE - 1;

	call->rxnet = rxnet;
	atomic_inc(&rxnet->nr_calls);
	return call;

nomem_2:
	kfree(call->rxtx_buffer);
nomem:
	kmem_cache_free(rxrpc_call_jar, call);
	return NULL;
}

/*
 * Allocate a new client call.
 */
static struct rxrpc_call *rxrpc_alloc_client_call(struct rxrpc_sock *rx,
						  struct sockaddr_rxrpc *srx,
						  gfp_t gfp,
						  unsigned int debug_id)
{
	struct rxrpc_call *call;
	ktime_t now;

	_enter("");

	call = rxrpc_alloc_call(rx, gfp, debug_id);
	if (!call)
		return ERR_PTR(-ENOMEM);
	call->state = RXRPC_CALL_CLIENT_AWAIT_CONN;
	call->service_id = srx->srx_service;
	call->tx_phase = true;
	now = ktime_get_real();
	call->acks_latest_ts = now;
	call->cong_tstamp = now;

	_leave(" = %p", call);
	return call;
}

/*
 * Initiate the call ack/resend/expiry timer.
 */
static void rxrpc_start_call_timer(struct rxrpc_call *call)
{
	unsigned long now = jiffies;
	unsigned long j = now + MAX_JIFFY_OFFSET;

	call->ack_at = j;
	call->ack_lost_at = j;
	call->resend_at = j;
	call->ping_at = j;
	call->expect_rx_by = j;
	call->expect_req_by = j;
	call->expect_term_by = j;
	call->timer.expires = now;
}

/*
 * Set up a call for the given parameters.
 * - Called with the socket lock held, which it must release.
 * - If it returns a call, the call's lock will need releasing by the caller.
 */
struct rxrpc_call *rxrpc_new_client_call(struct rxrpc_sock *rx,
					 struct rxrpc_conn_parameters *cp,
					 struct sockaddr_rxrpc *srx,
					 struct rxrpc_call_params *p,
					 gfp_t gfp,
					 unsigned int debug_id)
	__releases(&rx->sk.sk_lock.slock)
	__acquires(&call->user_mutex)
{
	struct rxrpc_call *call, *xcall;
	struct rxrpc_net *rxnet;
	struct rb_node *parent, **pp;
	const void *here = __builtin_return_address(0);
	int ret;

	_enter("%p,%lx", rx, p->user_call_ID);

	call = rxrpc_alloc_client_call(rx, srx, gfp, debug_id);
	if (IS_ERR(call)) {
		release_sock(&rx->sk);
		_leave(" = %ld", PTR_ERR(call));
		return call;
	}

	call->tx_total_len = p->tx_total_len;
	trace_rxrpc_call(call, rxrpc_call_new_client, atomic_read(&call->usage),
			 here, (const void *)p->user_call_ID);

	/* We need to protect a partially set up call against the user as we
	 * will be acting outside the socket lock.
	 */
	mutex_lock(&call->user_mutex);

	/* Publish the call, even though it is incompletely set up as yet */
	write_lock(&rx->call_lock);

	pp = &rx->calls.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		xcall = rb_entry(parent, struct rxrpc_call, sock_node);

		if (p->user_call_ID < xcall->user_call_ID)
			pp = &(*pp)->rb_left;
		else if (p->user_call_ID > xcall->user_call_ID)
			pp = &(*pp)->rb_right;
		else
			goto error_dup_user_ID;
	}

	rcu_assign_pointer(call->socket, rx);
	call->user_call_ID = p->user_call_ID;
	__set_bit(RXRPC_CALL_HAS_USERID, &call->flags);
	rxrpc_get_call(call, rxrpc_call_got_userid);
	rb_link_node(&call->sock_node, parent, pp);
	rb_insert_color(&call->sock_node, &rx->calls);
	list_add(&call->sock_link, &rx->sock_calls);

	write_unlock(&rx->call_lock);

	rxnet = call->rxnet;
	write_lock(&rxnet->call_lock);
	list_add_tail(&call->link, &rxnet->calls);
	write_unlock(&rxnet->call_lock);

	/* From this point on, the call is protected by its own lock. */
	release_sock(&rx->sk);

	/* Set up or get a connection record and set the protocol parameters,
	 * including channel number and call ID.
	 */
	ret = rxrpc_connect_call(rx, call, cp, srx, gfp);
	if (ret < 0)
		goto error;

	trace_rxrpc_call(call, rxrpc_call_connected, atomic_read(&call->usage),
			 here, NULL);

	rxrpc_start_call_timer(call);

	_net("CALL new %d on CONN %d", call->debug_id, call->conn->debug_id);

	_leave(" = %p [new]", call);
	return call;

	/* We unexpectedly found the user ID in the list after taking
	 * the call_lock.  This shouldn't happen unless the user races
	 * with itself and tries to add the same user ID twice at the
	 * same time in different threads.
	 */
error_dup_user_ID:
	write_unlock(&rx->call_lock);
	release_sock(&rx->sk);
	ret = -EEXIST;

error:
	__rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR,
				    RX_CALL_DEAD, ret);
	trace_rxrpc_call(call, rxrpc_call_error, atomic_read(&call->usage),
			 here, ERR_PTR(ret));
	rxrpc_release_call(rx, call);
	mutex_unlock(&call->user_mutex);
	rxrpc_put_call(call, rxrpc_call_put);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * Set up an incoming call.  call->conn points to the connection.
 * This is called in BH context and isn't allowed to fail.
 */
void rxrpc_incoming_call(struct rxrpc_sock *rx,
			 struct rxrpc_call *call,
			 struct sk_buff *skb)
{
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	u32 chan;

	_enter(",%d", call->conn->debug_id);

	rcu_assign_pointer(call->socket, rx);
	call->call_id		= sp->hdr.callNumber;
	call->service_id	= sp->hdr.serviceId;
	call->cid		= sp->hdr.cid;
	call->state		= RXRPC_CALL_SERVER_ACCEPTING;
	if (sp->hdr.securityIndex > 0)
		call->state	= RXRPC_CALL_SERVER_SECURING;
	call->cong_tstamp	= skb->tstamp;

	/* Set the channel for this call.  We don't get channel_lock as we're
	 * only defending against the data_ready handler (which we're called
	 * from) and the RESPONSE packet parser (which is only really
	 * interested in call_counter and can cope with a disagreement with the
	 * call pointer).
	 */
	chan = sp->hdr.cid & RXRPC_CHANNELMASK;
	conn->channels[chan].call_counter = call->call_id;
	conn->channels[chan].call_id = call->call_id;
	rcu_assign_pointer(conn->channels[chan].call, call);

	spin_lock(&conn->params.peer->lock);
	hlist_add_head_rcu(&call->error_link, &conn->params.peer->error_targets);
	spin_unlock(&conn->params.peer->lock);

	_net("CALL incoming %d on CONN %d", call->debug_id, call->conn->debug_id);

	rxrpc_start_call_timer(call);
	_leave("");
}

/*
 * Queue a call's work processor, getting a ref to pass to the work queue.
 */
bool rxrpc_queue_call(struct rxrpc_call *call)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_fetch_add_unless(&call->usage, 1, 0);
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
 * Detach a call from its owning socket.
 */
void rxrpc_release_call(struct rxrpc_sock *rx, struct rxrpc_call *call)
{
	const void *here = __builtin_return_address(0);
	struct rxrpc_connection *conn = call->conn;
	bool put = false;
	int i;

	_enter("{%d,%d}", call->debug_id, atomic_read(&call->usage));

	trace_rxrpc_call(call, rxrpc_call_release, atomic_read(&call->usage),
			 here, (const void *)call->flags);

	ASSERTCMP(call->state, ==, RXRPC_CALL_COMPLETE);

	spin_lock_bh(&call->lock);
	if (test_and_set_bit(RXRPC_CALL_RELEASED, &call->flags))
		BUG();
	spin_unlock_bh(&call->lock);

	del_timer_sync(&call->timer);

	/* Make sure we don't get any more notifications */
	write_lock_bh(&rx->recvmsg_lock);

	if (!list_empty(&call->recvmsg_link)) {
		_debug("unlinking once-pending call %p { e=%lx f=%lx }",
		       call, call->events, call->flags);
		list_del(&call->recvmsg_link);
		put = true;
	}

	/* list_empty() must return false in rxrpc_notify_socket() */
	call->recvmsg_link.next = NULL;
	call->recvmsg_link.prev = NULL;

	write_unlock_bh(&rx->recvmsg_lock);
	if (put)
		rxrpc_put_call(call, rxrpc_call_put);

	write_lock(&rx->call_lock);

	if (test_and_clear_bit(RXRPC_CALL_HAS_USERID, &call->flags)) {
		rb_erase(&call->sock_node, &rx->calls);
		memset(&call->sock_node, 0xdd, sizeof(call->sock_node));
		rxrpc_put_call(call, rxrpc_call_put_userid);
	}

	list_del(&call->sock_link);
	write_unlock(&rx->call_lock);

	_debug("RELEASE CALL %p (%d CONN %p)", call, call->debug_id, conn);

	if (conn)
		rxrpc_disconnect_call(call);

	for (i = 0; i < RXRPC_RXTX_BUFF_SIZE; i++) {
		rxrpc_free_skb(call->rxtx_buffer[i],
			       (call->tx_phase ? rxrpc_skb_tx_cleaned :
				rxrpc_skb_rx_cleaned));
		call->rxtx_buffer[i] = NULL;
	}

	_leave("");
}

/*
 * release all the calls associated with a socket
 */
void rxrpc_release_calls_on_socket(struct rxrpc_sock *rx)
{
	struct rxrpc_call *call;

	_enter("%p", rx);

	while (!list_empty(&rx->to_be_accepted)) {
		call = list_entry(rx->to_be_accepted.next,
				  struct rxrpc_call, accept_link);
		list_del(&call->accept_link);
		rxrpc_abort_call("SKR", call, 0, RX_CALL_DEAD, -ECONNRESET);
		rxrpc_put_call(call, rxrpc_call_put);
	}

	while (!list_empty(&rx->sock_calls)) {
		call = list_entry(rx->sock_calls.next,
				  struct rxrpc_call, sock_link);
		rxrpc_get_call(call, rxrpc_call_got);
		rxrpc_abort_call("SKT", call, 0, RX_CALL_DEAD, -ECONNRESET);
		rxrpc_send_abort_packet(call);
		rxrpc_release_call(rx, call);
		rxrpc_put_call(call, rxrpc_call_put);
	}

	_leave("");
}

/*
 * release a call
 */
void rxrpc_put_call(struct rxrpc_call *call, enum rxrpc_call_trace op)
{
	struct rxrpc_net *rxnet = call->rxnet;
	const void *here = __builtin_return_address(0);
	int n;

	ASSERT(call != NULL);

	n = atomic_dec_return(&call->usage);
	trace_rxrpc_call(call, op, n, here, NULL);
	ASSERTCMP(n, >=, 0);
	if (n == 0) {
		_debug("call %d dead", call->debug_id);
		ASSERTCMP(call->state, ==, RXRPC_CALL_COMPLETE);

		if (!list_empty(&call->link)) {
			write_lock(&rxnet->call_lock);
			list_del_init(&call->link);
			write_unlock(&rxnet->call_lock);
		}

		rxrpc_cleanup_call(call);
	}
}

/*
 * Final call destruction under RCU.
 */
static void rxrpc_rcu_destroy_call(struct rcu_head *rcu)
{
	struct rxrpc_call *call = container_of(rcu, struct rxrpc_call, rcu);
	struct rxrpc_net *rxnet = call->rxnet;

	rxrpc_put_peer(call->peer);
	kfree(call->rxtx_buffer);
	kfree(call->rxtx_annotations);
	kmem_cache_free(rxrpc_call_jar, call);
	if (atomic_dec_and_test(&rxnet->nr_calls))
		wake_up_var(&rxnet->nr_calls);
}

/*
 * clean up a call
 */
void rxrpc_cleanup_call(struct rxrpc_call *call)
{
	int i;

	_net("DESTROY CALL %d", call->debug_id);

	memset(&call->sock_node, 0xcd, sizeof(call->sock_node));

	del_timer_sync(&call->timer);

	ASSERTCMP(call->state, ==, RXRPC_CALL_COMPLETE);
	ASSERT(test_bit(RXRPC_CALL_RELEASED, &call->flags));
	ASSERTCMP(call->conn, ==, NULL);

	/* Clean up the Rx/Tx buffer */
	for (i = 0; i < RXRPC_RXTX_BUFF_SIZE; i++)
		rxrpc_free_skb(call->rxtx_buffer[i],
			       (call->tx_phase ? rxrpc_skb_tx_cleaned :
				rxrpc_skb_rx_cleaned));

	rxrpc_free_skb(call->tx_pending, rxrpc_skb_tx_cleaned);

	call_rcu(&call->rcu, rxrpc_rcu_destroy_call);
}

/*
 * Make sure that all calls are gone from a network namespace.  To reach this
 * point, any open UDP sockets in that namespace must have been closed, so any
 * outstanding calls cannot be doing I/O.
 */
void rxrpc_destroy_all_calls(struct rxrpc_net *rxnet)
{
	struct rxrpc_call *call;

	_enter("");

	if (!list_empty(&rxnet->calls)) {
		write_lock(&rxnet->call_lock);

		while (!list_empty(&rxnet->calls)) {
			call = list_entry(rxnet->calls.next,
					  struct rxrpc_call, link);
			_debug("Zapping call %p", call);

			rxrpc_see_call(call);
			list_del_init(&call->link);

			pr_err("Call %p still in use (%d,%s,%lx,%lx)!\n",
			       call, atomic_read(&call->usage),
			       rxrpc_call_states[call->state],
			       call->flags, call->events);

			write_unlock(&rxnet->call_lock);
			cond_resched();
			write_lock(&rxnet->call_lock);
		}

		write_unlock(&rxnet->call_lock);
	}

	atomic_dec(&rxnet->nr_calls);
	wait_var_event(&rxnet->nr_calls, !atomic_read(&rxnet->nr_calls));
}
