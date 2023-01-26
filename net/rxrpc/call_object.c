// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC individual remote procedure call handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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

static struct semaphore rxrpc_call_limiter =
	__SEMAPHORE_INITIALIZER(rxrpc_call_limiter, 1000);
static struct semaphore rxrpc_kernel_call_limiter =
	__SEMAPHORE_INITIALIZER(rxrpc_kernel_call_limiter, 1000);

void rxrpc_poke_call(struct rxrpc_call *call, enum rxrpc_call_poke_trace what)
{
	struct rxrpc_local *local = call->local;
	bool busy;

	if (!test_bit(RXRPC_CALL_DISCONNECTED, &call->flags)) {
		spin_lock_bh(&local->lock);
		busy = !list_empty(&call->attend_link);
		trace_rxrpc_poke_call(call, busy, what);
		if (!busy) {
			rxrpc_get_call(call, rxrpc_call_get_poke);
			list_add_tail(&call->attend_link, &local->call_attend_q);
		}
		spin_unlock_bh(&local->lock);
		rxrpc_wake_up_io_thread(local);
	}
}

static void rxrpc_call_timer_expired(struct timer_list *t)
{
	struct rxrpc_call *call = from_timer(call, t, timer);

	_enter("%d", call->debug_id);

	if (!__rxrpc_call_is_complete(call)) {
		trace_rxrpc_timer_expired(call, jiffies);
		rxrpc_poke_call(call, rxrpc_call_poke_timer);
	}
}

void rxrpc_reduce_call_timer(struct rxrpc_call *call,
			     unsigned long expire_at,
			     unsigned long now,
			     enum rxrpc_timer_trace why)
{
	trace_rxrpc_timer(call, why, now);
	timer_reduce(&call->timer, expire_at);
}

static struct lock_class_key rxrpc_call_user_mutex_lock_class_key;

static void rxrpc_destroy_call(struct work_struct *);

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
	rxrpc_get_call(call, rxrpc_call_get_sendmsg);
	read_unlock(&rx->call_lock);
	_leave(" = %p [%d]", call, refcount_read(&call->ref));
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

	mutex_init(&call->user_mutex);

	/* Prevent lockdep reporting a deadlock false positive between the afs
	 * filesystem and sys_sendmsg() via the mmap sem.
	 */
	if (rx->sk.sk_kern_sock)
		lockdep_set_class(&call->user_mutex,
				  &rxrpc_call_user_mutex_lock_class_key);

	timer_setup(&call->timer, rxrpc_call_timer_expired, 0);
	INIT_WORK(&call->destroyer, rxrpc_destroy_call);
	INIT_LIST_HEAD(&call->link);
	INIT_LIST_HEAD(&call->wait_link);
	INIT_LIST_HEAD(&call->accept_link);
	INIT_LIST_HEAD(&call->recvmsg_link);
	INIT_LIST_HEAD(&call->sock_link);
	INIT_LIST_HEAD(&call->attend_link);
	INIT_LIST_HEAD(&call->tx_sendmsg);
	INIT_LIST_HEAD(&call->tx_buffer);
	skb_queue_head_init(&call->recvmsg_queue);
	skb_queue_head_init(&call->rx_oos_queue);
	init_waitqueue_head(&call->waitq);
	spin_lock_init(&call->notify_lock);
	spin_lock_init(&call->tx_lock);
	refcount_set(&call->ref, 1);
	call->debug_id = debug_id;
	call->tx_total_len = -1;
	call->next_rx_timo = 20 * HZ;
	call->next_req_timo = 1 * HZ;
	atomic64_set(&call->ackr_window, 0x100000001ULL);

	memset(&call->sock_node, 0xed, sizeof(call->sock_node));

	call->rx_winsize = rxrpc_rx_window_size;
	call->tx_winsize = 16;

	if (RXRPC_TX_SMSS > 2190)
		call->cong_cwnd = 2;
	else if (RXRPC_TX_SMSS > 1095)
		call->cong_cwnd = 3;
	else
		call->cong_cwnd = 4;
	call->cong_ssthresh = RXRPC_TX_MAX_WINDOW;

	call->rxnet = rxnet;
	call->rtt_avail = RXRPC_CALL_RTT_AVAIL_MASK;
	atomic_inc(&rxnet->nr_calls);
	return call;
}

/*
 * Allocate a new client call.
 */
static struct rxrpc_call *rxrpc_alloc_client_call(struct rxrpc_sock *rx,
						  struct sockaddr_rxrpc *srx,
						  struct rxrpc_conn_parameters *cp,
						  struct rxrpc_call_params *p,
						  gfp_t gfp,
						  unsigned int debug_id)
{
	struct rxrpc_call *call;
	ktime_t now;
	int ret;

	_enter("");

	call = rxrpc_alloc_call(rx, gfp, debug_id);
	if (!call)
		return ERR_PTR(-ENOMEM);
	now = ktime_get_real();
	call->acks_latest_ts	= now;
	call->cong_tstamp	= now;
	call->dest_srx		= *srx;
	call->interruptibility	= p->interruptibility;
	call->tx_total_len	= p->tx_total_len;
	call->key		= key_get(cp->key);
	call->local		= rxrpc_get_local(cp->local, rxrpc_local_get_call);
	call->security_level	= cp->security_level;
	if (p->kernel)
		__set_bit(RXRPC_CALL_KERNEL, &call->flags);
	if (cp->upgrade)
		__set_bit(RXRPC_CALL_UPGRADE, &call->flags);
	if (cp->exclusive)
		__set_bit(RXRPC_CALL_EXCLUSIVE, &call->flags);

	ret = rxrpc_init_client_call_security(call);
	if (ret < 0) {
		rxrpc_prefail_call(call, RXRPC_CALL_LOCAL_ERROR, ret);
		rxrpc_put_call(call, rxrpc_call_put_discard_error);
		return ERR_PTR(ret);
	}

	rxrpc_set_call_state(call, RXRPC_CALL_CLIENT_AWAIT_CONN);

	trace_rxrpc_call(call->debug_id, refcount_read(&call->ref),
			 p->user_call_ID, rxrpc_call_new_client);

	_leave(" = %p", call);
	return call;
}

/*
 * Initiate the call ack/resend/expiry timer.
 */
void rxrpc_start_call_timer(struct rxrpc_call *call)
{
	unsigned long now = jiffies;
	unsigned long j = now + MAX_JIFFY_OFFSET;

	call->delay_ack_at = j;
	call->ack_lost_at = j;
	call->resend_at = j;
	call->ping_at = j;
	call->keepalive_at = j;
	call->expect_rx_by = j;
	call->expect_req_by = j;
	call->expect_term_by = j;
	call->timer.expires = now;
}

/*
 * Wait for a call slot to become available.
 */
static struct semaphore *rxrpc_get_call_slot(struct rxrpc_call_params *p, gfp_t gfp)
{
	struct semaphore *limiter = &rxrpc_call_limiter;

	if (p->kernel)
		limiter = &rxrpc_kernel_call_limiter;
	if (p->interruptibility == RXRPC_UNINTERRUPTIBLE) {
		down(limiter);
		return limiter;
	}
	return down_interruptible(limiter) < 0 ? NULL : limiter;
}

/*
 * Release a call slot.
 */
static void rxrpc_put_call_slot(struct rxrpc_call *call)
{
	struct semaphore *limiter = &rxrpc_call_limiter;

	if (test_bit(RXRPC_CALL_KERNEL, &call->flags))
		limiter = &rxrpc_kernel_call_limiter;
	up(limiter);
}

/*
 * Start the process of connecting a call.  We obtain a peer and a connection
 * bundle, but the actual association of a call with a connection is offloaded
 * to the I/O thread to simplify locking.
 */
static int rxrpc_connect_call(struct rxrpc_call *call, gfp_t gfp)
{
	struct rxrpc_local *local = call->local;
	int ret = -ENOMEM;

	_enter("{%d,%lx},", call->debug_id, call->user_call_ID);

	call->peer = rxrpc_lookup_peer(local, &call->dest_srx, gfp);
	if (!call->peer)
		goto error;

	ret = rxrpc_look_up_bundle(call, gfp);
	if (ret < 0)
		goto error;

	trace_rxrpc_client(NULL, -1, rxrpc_client_queue_new_call);
	rxrpc_get_call(call, rxrpc_call_get_io_thread);
	spin_lock(&local->client_call_lock);
	list_add_tail(&call->wait_link, &local->new_client_calls);
	spin_unlock(&local->client_call_lock);
	rxrpc_wake_up_io_thread(local);
	return 0;

error:
	__set_bit(RXRPC_CALL_DISCONNECTED, &call->flags);
	return ret;
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
	struct semaphore *limiter;
	struct rb_node *parent, **pp;
	int ret;

	_enter("%p,%lx", rx, p->user_call_ID);

	limiter = rxrpc_get_call_slot(p, gfp);
	if (!limiter) {
		release_sock(&rx->sk);
		return ERR_PTR(-ERESTARTSYS);
	}

	call = rxrpc_alloc_client_call(rx, srx, cp, p, gfp, debug_id);
	if (IS_ERR(call)) {
		release_sock(&rx->sk);
		up(limiter);
		_leave(" = %ld", PTR_ERR(call));
		return call;
	}

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
	rxrpc_get_call(call, rxrpc_call_get_userid);
	rb_link_node(&call->sock_node, parent, pp);
	rb_insert_color(&call->sock_node, &rx->calls);
	list_add(&call->sock_link, &rx->sock_calls);

	write_unlock(&rx->call_lock);

	rxnet = call->rxnet;
	spin_lock(&rxnet->call_lock);
	list_add_tail_rcu(&call->link, &rxnet->calls);
	spin_unlock(&rxnet->call_lock);

	/* From this point on, the call is protected by its own lock. */
	release_sock(&rx->sk);

	/* Set up or get a connection record and set the protocol parameters,
	 * including channel number and call ID.
	 */
	ret = rxrpc_connect_call(call, gfp);
	if (ret < 0)
		goto error_attached_to_socket;

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
	rxrpc_prefail_call(call, RXRPC_CALL_LOCAL_ERROR, -EEXIST);
	trace_rxrpc_call(call->debug_id, refcount_read(&call->ref), 0,
			 rxrpc_call_see_userid_exists);
	mutex_unlock(&call->user_mutex);
	rxrpc_put_call(call, rxrpc_call_put_userid_exists);
	_leave(" = -EEXIST");
	return ERR_PTR(-EEXIST);

	/* We got an error, but the call is attached to the socket and is in
	 * need of release.  However, we might now race with recvmsg() when it
	 * completion notifies the socket.  Return 0 from sys_sendmsg() and
	 * leave the error to recvmsg() to deal with.
	 */
error_attached_to_socket:
	trace_rxrpc_call(call->debug_id, refcount_read(&call->ref), ret,
			 rxrpc_call_see_connect_failed);
	rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR, 0, ret);
	_leave(" = c=%08x [err]", call->debug_id);
	return call;
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
	call->dest_srx.srx_service = sp->hdr.serviceId;
	call->cid		= sp->hdr.cid;
	call->cong_tstamp	= skb->tstamp;

	__set_bit(RXRPC_CALL_EXPOSED, &call->flags);
	rxrpc_set_call_state(call, RXRPC_CALL_SERVER_SECURING);

	spin_lock(&conn->state_lock);

	switch (conn->state) {
	case RXRPC_CONN_SERVICE_UNSECURED:
	case RXRPC_CONN_SERVICE_CHALLENGING:
		rxrpc_set_call_state(call, RXRPC_CALL_SERVER_SECURING);
		break;
	case RXRPC_CONN_SERVICE:
		rxrpc_set_call_state(call, RXRPC_CALL_SERVER_RECV_REQUEST);
		break;

	case RXRPC_CONN_ABORTED:
		rxrpc_set_call_completion(call, conn->completion,
					  conn->abort_code, conn->error);
		break;
	default:
		BUG();
	}

	rxrpc_get_call(call, rxrpc_call_get_io_thread);

	/* Set the channel for this call.  We don't get channel_lock as we're
	 * only defending against the data_ready handler (which we're called
	 * from) and the RESPONSE packet parser (which is only really
	 * interested in call_counter and can cope with a disagreement with the
	 * call pointer).
	 */
	chan = sp->hdr.cid & RXRPC_CHANNELMASK;
	conn->channels[chan].call_counter = call->call_id;
	conn->channels[chan].call_id = call->call_id;
	conn->channels[chan].call = call;
	spin_unlock(&conn->state_lock);

	spin_lock(&conn->peer->lock);
	hlist_add_head(&call->error_link, &conn->peer->error_targets);
	spin_unlock(&conn->peer->lock);

	rxrpc_start_call_timer(call);
	_leave("");
}

/*
 * Note the re-emergence of a call.
 */
void rxrpc_see_call(struct rxrpc_call *call, enum rxrpc_call_trace why)
{
	if (call) {
		int r = refcount_read(&call->ref);

		trace_rxrpc_call(call->debug_id, r, 0, why);
	}
}

struct rxrpc_call *rxrpc_try_get_call(struct rxrpc_call *call,
				      enum rxrpc_call_trace why)
{
	int r;

	if (!call || !__refcount_inc_not_zero(&call->ref, &r))
		return NULL;
	trace_rxrpc_call(call->debug_id, r + 1, 0, why);
	return call;
}

/*
 * Note the addition of a ref on a call.
 */
void rxrpc_get_call(struct rxrpc_call *call, enum rxrpc_call_trace why)
{
	int r;

	__refcount_inc(&call->ref, &r);
	trace_rxrpc_call(call->debug_id, r + 1, 0, why);
}

/*
 * Clean up the Rx skb ring.
 */
static void rxrpc_cleanup_ring(struct rxrpc_call *call)
{
	skb_queue_purge(&call->recvmsg_queue);
	skb_queue_purge(&call->rx_oos_queue);
}

/*
 * Detach a call from its owning socket.
 */
void rxrpc_release_call(struct rxrpc_sock *rx, struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = call->conn;
	bool put = false, putu = false;

	_enter("{%d,%d}", call->debug_id, refcount_read(&call->ref));

	trace_rxrpc_call(call->debug_id, refcount_read(&call->ref),
			 call->flags, rxrpc_call_see_release);

	if (test_and_set_bit(RXRPC_CALL_RELEASED, &call->flags))
		BUG();

	rxrpc_put_call_slot(call);

	/* Make sure we don't get any more notifications */
	write_lock(&rx->recvmsg_lock);

	if (!list_empty(&call->recvmsg_link)) {
		_debug("unlinking once-pending call %p { e=%lx f=%lx }",
		       call, call->events, call->flags);
		list_del(&call->recvmsg_link);
		put = true;
	}

	/* list_empty() must return false in rxrpc_notify_socket() */
	call->recvmsg_link.next = NULL;
	call->recvmsg_link.prev = NULL;

	write_unlock(&rx->recvmsg_lock);
	if (put)
		rxrpc_put_call(call, rxrpc_call_put_unnotify);

	write_lock(&rx->call_lock);

	if (test_and_clear_bit(RXRPC_CALL_HAS_USERID, &call->flags)) {
		rb_erase(&call->sock_node, &rx->calls);
		memset(&call->sock_node, 0xdd, sizeof(call->sock_node));
		putu = true;
	}

	list_del(&call->sock_link);
	write_unlock(&rx->call_lock);

	_debug("RELEASE CALL %p (%d CONN %p)", call, call->debug_id, conn);

	if (putu)
		rxrpc_put_call(call, rxrpc_call_put_userid);

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
		rxrpc_propose_abort(call, RX_CALL_DEAD, -ECONNRESET,
				    rxrpc_abort_call_sock_release_tba);
		rxrpc_put_call(call, rxrpc_call_put_release_sock_tba);
	}

	while (!list_empty(&rx->sock_calls)) {
		call = list_entry(rx->sock_calls.next,
				  struct rxrpc_call, sock_link);
		rxrpc_get_call(call, rxrpc_call_get_release_sock);
		rxrpc_propose_abort(call, RX_CALL_DEAD, -ECONNRESET,
				    rxrpc_abort_call_sock_release);
		rxrpc_release_call(rx, call);
		rxrpc_put_call(call, rxrpc_call_put_release_sock);
	}

	_leave("");
}

/*
 * release a call
 */
void rxrpc_put_call(struct rxrpc_call *call, enum rxrpc_call_trace why)
{
	struct rxrpc_net *rxnet = call->rxnet;
	unsigned int debug_id = call->debug_id;
	bool dead;
	int r;

	ASSERT(call != NULL);

	dead = __refcount_dec_and_test(&call->ref, &r);
	trace_rxrpc_call(debug_id, r - 1, 0, why);
	if (dead) {
		ASSERTCMP(__rxrpc_call_state(call), ==, RXRPC_CALL_COMPLETE);

		if (!list_empty(&call->link)) {
			spin_lock(&rxnet->call_lock);
			list_del_init(&call->link);
			spin_unlock(&rxnet->call_lock);
		}

		rxrpc_cleanup_call(call);
	}
}

/*
 * Free up the call under RCU.
 */
static void rxrpc_rcu_free_call(struct rcu_head *rcu)
{
	struct rxrpc_call *call = container_of(rcu, struct rxrpc_call, rcu);
	struct rxrpc_net *rxnet = READ_ONCE(call->rxnet);

	kmem_cache_free(rxrpc_call_jar, call);
	if (atomic_dec_and_test(&rxnet->nr_calls))
		wake_up_var(&rxnet->nr_calls);
}

/*
 * Final call destruction - but must be done in process context.
 */
static void rxrpc_destroy_call(struct work_struct *work)
{
	struct rxrpc_call *call = container_of(work, struct rxrpc_call, destroyer);
	struct rxrpc_txbuf *txb;

	del_timer_sync(&call->timer);

	rxrpc_cleanup_ring(call);
	while ((txb = list_first_entry_or_null(&call->tx_sendmsg,
					       struct rxrpc_txbuf, call_link))) {
		list_del(&txb->call_link);
		rxrpc_put_txbuf(txb, rxrpc_txbuf_put_cleaned);
	}
	while ((txb = list_first_entry_or_null(&call->tx_buffer,
					       struct rxrpc_txbuf, call_link))) {
		list_del(&txb->call_link);
		rxrpc_put_txbuf(txb, rxrpc_txbuf_put_cleaned);
	}

	rxrpc_put_txbuf(call->tx_pending, rxrpc_txbuf_put_cleaned);
	rxrpc_put_connection(call->conn, rxrpc_conn_put_call);
	rxrpc_deactivate_bundle(call->bundle);
	rxrpc_put_bundle(call->bundle, rxrpc_bundle_put_call);
	rxrpc_put_peer(call->peer, rxrpc_peer_put_call);
	rxrpc_put_local(call->local, rxrpc_local_put_call);
	call_rcu(&call->rcu, rxrpc_rcu_free_call);
}

/*
 * clean up a call
 */
void rxrpc_cleanup_call(struct rxrpc_call *call)
{
	memset(&call->sock_node, 0xcd, sizeof(call->sock_node));

	ASSERTCMP(__rxrpc_call_state(call), ==, RXRPC_CALL_COMPLETE);
	ASSERT(test_bit(RXRPC_CALL_RELEASED, &call->flags));

	del_timer(&call->timer);

	if (rcu_read_lock_held())
		/* Can't use the rxrpc workqueue as we need to cancel/flush
		 * something that may be running/waiting there.
		 */
		schedule_work(&call->destroyer);
	else
		rxrpc_destroy_call(&call->destroyer);
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
		spin_lock(&rxnet->call_lock);

		while (!list_empty(&rxnet->calls)) {
			call = list_entry(rxnet->calls.next,
					  struct rxrpc_call, link);
			_debug("Zapping call %p", call);

			rxrpc_see_call(call, rxrpc_call_see_zap);
			list_del_init(&call->link);

			pr_err("Call %p still in use (%d,%s,%lx,%lx)!\n",
			       call, refcount_read(&call->ref),
			       rxrpc_call_states[__rxrpc_call_state(call)],
			       call->flags, call->events);

			spin_unlock(&rxnet->call_lock);
			cond_resched();
			spin_lock(&rxnet->call_lock);
		}

		spin_unlock(&rxnet->call_lock);
	}

	atomic_dec(&rxnet->nr_calls);
	wait_var_event(&rxnet->nr_calls, !atomic_read(&rxnet->nr_calls));
}
