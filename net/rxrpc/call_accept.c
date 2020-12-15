// SPDX-License-Identifier: GPL-2.0-or-later
/* incoming call handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/errqueue.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <linux/gfp.h>
#include <linux/circ_buf.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include "ar-internal.h"

static void rxrpc_dummy_notify(struct sock *sk, struct rxrpc_call *call,
			       unsigned long user_call_ID)
{
}

/*
 * Preallocate a single service call, connection and peer and, if possible,
 * give them a user ID and attach the user's side of the ID to them.
 */
static int rxrpc_service_prealloc_one(struct rxrpc_sock *rx,
				      struct rxrpc_backlog *b,
				      rxrpc_notify_rx_t notify_rx,
				      rxrpc_user_attach_call_t user_attach_call,
				      unsigned long user_call_ID, gfp_t gfp,
				      unsigned int debug_id)
{
	const void *here = __builtin_return_address(0);
	struct rxrpc_call *call, *xcall;
	struct rxrpc_net *rxnet = rxrpc_net(sock_net(&rx->sk));
	struct rb_node *parent, **pp;
	int max, tmp;
	unsigned int size = RXRPC_BACKLOG_MAX;
	unsigned int head, tail, call_head, call_tail;

	max = rx->sk.sk_max_ack_backlog;
	tmp = rx->sk.sk_ack_backlog;
	if (tmp >= max) {
		_leave(" = -ENOBUFS [full %u]", max);
		return -ENOBUFS;
	}
	max -= tmp;

	/* We don't need more conns and peers than we have calls, but on the
	 * other hand, we shouldn't ever use more peers than conns or conns
	 * than calls.
	 */
	call_head = b->call_backlog_head;
	call_tail = READ_ONCE(b->call_backlog_tail);
	tmp = CIRC_CNT(call_head, call_tail, size);
	if (tmp >= max) {
		_leave(" = -ENOBUFS [enough %u]", tmp);
		return -ENOBUFS;
	}
	max = tmp + 1;

	head = b->peer_backlog_head;
	tail = READ_ONCE(b->peer_backlog_tail);
	if (CIRC_CNT(head, tail, size) < max) {
		struct rxrpc_peer *peer = rxrpc_alloc_peer(rx->local, gfp);
		if (!peer)
			return -ENOMEM;
		b->peer_backlog[head] = peer;
		smp_store_release(&b->peer_backlog_head,
				  (head + 1) & (size - 1));
	}

	head = b->conn_backlog_head;
	tail = READ_ONCE(b->conn_backlog_tail);
	if (CIRC_CNT(head, tail, size) < max) {
		struct rxrpc_connection *conn;

		conn = rxrpc_prealloc_service_connection(rxnet, gfp);
		if (!conn)
			return -ENOMEM;
		b->conn_backlog[head] = conn;
		smp_store_release(&b->conn_backlog_head,
				  (head + 1) & (size - 1));

		trace_rxrpc_conn(conn->debug_id, rxrpc_conn_new_service,
				 atomic_read(&conn->usage), here);
	}

	/* Now it gets complicated, because calls get registered with the
	 * socket here, with a user ID preassigned by the user.
	 */
	call = rxrpc_alloc_call(rx, gfp, debug_id);
	if (!call)
		return -ENOMEM;
	call->flags |= (1 << RXRPC_CALL_IS_SERVICE);
	call->state = RXRPC_CALL_SERVER_PREALLOC;

	trace_rxrpc_call(call->debug_id, rxrpc_call_new_service,
			 atomic_read(&call->usage),
			 here, (const void *)user_call_ID);

	write_lock(&rx->call_lock);

	/* Check the user ID isn't already in use */
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
			goto id_in_use;
	}

	call->user_call_ID = user_call_ID;
	call->notify_rx = notify_rx;
	if (user_attach_call) {
		rxrpc_get_call(call, rxrpc_call_got_kernel);
		user_attach_call(call, user_call_ID);
	}

	rxrpc_get_call(call, rxrpc_call_got_userid);
	rb_link_node(&call->sock_node, parent, pp);
	rb_insert_color(&call->sock_node, &rx->calls);
	set_bit(RXRPC_CALL_HAS_USERID, &call->flags);

	list_add(&call->sock_link, &rx->sock_calls);

	write_unlock(&rx->call_lock);

	rxnet = call->rxnet;
	write_lock(&rxnet->call_lock);
	list_add_tail(&call->link, &rxnet->calls);
	write_unlock(&rxnet->call_lock);

	b->call_backlog[call_head] = call;
	smp_store_release(&b->call_backlog_head, (call_head + 1) & (size - 1));
	_leave(" = 0 [%d -> %lx]", call->debug_id, user_call_ID);
	return 0;

id_in_use:
	write_unlock(&rx->call_lock);
	rxrpc_cleanup_call(call);
	_leave(" = -EBADSLT");
	return -EBADSLT;
}

/*
 * Allocate the preallocation buffers for incoming service calls.  These must
 * be charged manually.
 */
int rxrpc_service_prealloc(struct rxrpc_sock *rx, gfp_t gfp)
{
	struct rxrpc_backlog *b = rx->backlog;

	if (!b) {
		b = kzalloc(sizeof(struct rxrpc_backlog), gfp);
		if (!b)
			return -ENOMEM;
		rx->backlog = b;
	}

	return 0;
}

/*
 * Discard the preallocation on a service.
 */
void rxrpc_discard_prealloc(struct rxrpc_sock *rx)
{
	struct rxrpc_backlog *b = rx->backlog;
	struct rxrpc_net *rxnet = rxrpc_net(sock_net(&rx->sk));
	unsigned int size = RXRPC_BACKLOG_MAX, head, tail;

	if (!b)
		return;
	rx->backlog = NULL;

	/* Make sure that there aren't any incoming calls in progress before we
	 * clear the preallocation buffers.
	 */
	spin_lock_bh(&rx->incoming_lock);
	spin_unlock_bh(&rx->incoming_lock);

	head = b->peer_backlog_head;
	tail = b->peer_backlog_tail;
	while (CIRC_CNT(head, tail, size) > 0) {
		struct rxrpc_peer *peer = b->peer_backlog[tail];
		kfree(peer);
		tail = (tail + 1) & (size - 1);
	}

	head = b->conn_backlog_head;
	tail = b->conn_backlog_tail;
	while (CIRC_CNT(head, tail, size) > 0) {
		struct rxrpc_connection *conn = b->conn_backlog[tail];
		write_lock(&rxnet->conn_lock);
		list_del(&conn->link);
		list_del(&conn->proc_link);
		write_unlock(&rxnet->conn_lock);
		kfree(conn);
		if (atomic_dec_and_test(&rxnet->nr_conns))
			wake_up_var(&rxnet->nr_conns);
		tail = (tail + 1) & (size - 1);
	}

	head = b->call_backlog_head;
	tail = b->call_backlog_tail;
	while (CIRC_CNT(head, tail, size) > 0) {
		struct rxrpc_call *call = b->call_backlog[tail];
		rcu_assign_pointer(call->socket, rx);
		if (rx->discard_new_call) {
			_debug("discard %lx", call->user_call_ID);
			rx->discard_new_call(call, call->user_call_ID);
			if (call->notify_rx)
				call->notify_rx = rxrpc_dummy_notify;
			rxrpc_put_call(call, rxrpc_call_put_kernel);
		}
		rxrpc_call_completed(call);
		rxrpc_release_call(rx, call);
		rxrpc_put_call(call, rxrpc_call_put);
		tail = (tail + 1) & (size - 1);
	}

	kfree(b);
}

/*
 * Ping the other end to fill our RTT cache and to retrieve the rwind
 * and MTU parameters.
 */
static void rxrpc_send_ping(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	ktime_t now = skb->tstamp;

	if (call->peer->rtt_count < 3 ||
	    ktime_before(ktime_add_ms(call->peer->rtt_last_req, 1000), now))
		rxrpc_propose_ACK(call, RXRPC_ACK_PING, sp->hdr.serial,
				  true, true,
				  rxrpc_propose_ack_ping_for_params);
}

/*
 * Allocate a new incoming call from the prealloc pool, along with a connection
 * and a peer as necessary.
 */
static struct rxrpc_call *rxrpc_alloc_incoming_call(struct rxrpc_sock *rx,
						    struct rxrpc_local *local,
						    struct rxrpc_peer *peer,
						    struct rxrpc_connection *conn,
						    const struct rxrpc_security *sec,
						    struct sk_buff *skb)
{
	struct rxrpc_backlog *b = rx->backlog;
	struct rxrpc_call *call;
	unsigned short call_head, conn_head, peer_head;
	unsigned short call_tail, conn_tail, peer_tail;
	unsigned short call_count, conn_count;

	/* #calls >= #conns >= #peers must hold true. */
	call_head = smp_load_acquire(&b->call_backlog_head);
	call_tail = b->call_backlog_tail;
	call_count = CIRC_CNT(call_head, call_tail, RXRPC_BACKLOG_MAX);
	conn_head = smp_load_acquire(&b->conn_backlog_head);
	conn_tail = b->conn_backlog_tail;
	conn_count = CIRC_CNT(conn_head, conn_tail, RXRPC_BACKLOG_MAX);
	ASSERTCMP(conn_count, >=, call_count);
	peer_head = smp_load_acquire(&b->peer_backlog_head);
	peer_tail = b->peer_backlog_tail;
	ASSERTCMP(CIRC_CNT(peer_head, peer_tail, RXRPC_BACKLOG_MAX), >=,
		  conn_count);

	if (call_count == 0)
		return NULL;

	if (!conn) {
		if (peer && !rxrpc_get_peer_maybe(peer))
			peer = NULL;
		if (!peer) {
			peer = b->peer_backlog[peer_tail];
			if (rxrpc_extract_addr_from_skb(&peer->srx, skb) < 0)
				return NULL;
			b->peer_backlog[peer_tail] = NULL;
			smp_store_release(&b->peer_backlog_tail,
					  (peer_tail + 1) &
					  (RXRPC_BACKLOG_MAX - 1));

			rxrpc_new_incoming_peer(rx, local, peer);
		}

		/* Now allocate and set up the connection */
		conn = b->conn_backlog[conn_tail];
		b->conn_backlog[conn_tail] = NULL;
		smp_store_release(&b->conn_backlog_tail,
				  (conn_tail + 1) & (RXRPC_BACKLOG_MAX - 1));
		conn->params.local = rxrpc_get_local(local);
		conn->params.peer = peer;
		rxrpc_see_connection(conn);
		rxrpc_new_incoming_connection(rx, conn, sec, skb);
	} else {
		rxrpc_get_connection(conn);
	}

	/* And now we can allocate and set up a new call */
	call = b->call_backlog[call_tail];
	b->call_backlog[call_tail] = NULL;
	smp_store_release(&b->call_backlog_tail,
			  (call_tail + 1) & (RXRPC_BACKLOG_MAX - 1));

	rxrpc_see_call(call);
	call->conn = conn;
	call->security = conn->security;
	call->security_ix = conn->security_ix;
	call->peer = rxrpc_get_peer(conn->params.peer);
	call->cong_cwnd = call->peer->cong_cwnd;
	return call;
}

/*
 * Set up a new incoming call.  Called in BH context with the RCU read lock
 * held.
 *
 * If this is for a kernel service, when we allocate the call, it will have
 * three refs on it: (1) the kernel service, (2) the user_call_ID tree, (3) the
 * retainer ref obtained from the backlog buffer.  Prealloc calls for userspace
 * services only have the ref from the backlog buffer.  We want to pass this
 * ref to non-BH context to dispose of.
 *
 * If we want to report an error, we mark the skb with the packet type and
 * abort code and return NULL.
 *
 * The call is returned with the user access mutex held.
 */
struct rxrpc_call *rxrpc_new_incoming_call(struct rxrpc_local *local,
					   struct rxrpc_sock *rx,
					   struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	const struct rxrpc_security *sec = NULL;
	struct rxrpc_connection *conn;
	struct rxrpc_peer *peer = NULL;
	struct rxrpc_call *call = NULL;

	_enter("");

	spin_lock(&rx->incoming_lock);
	if (rx->sk.sk_state == RXRPC_SERVER_LISTEN_DISABLED ||
	    rx->sk.sk_state == RXRPC_CLOSE) {
		trace_rxrpc_abort(0, "CLS", sp->hdr.cid, sp->hdr.callNumber,
				  sp->hdr.seq, RX_INVALID_OPERATION, ESHUTDOWN);
		skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
		skb->priority = RX_INVALID_OPERATION;
		goto no_call;
	}

	/* The peer, connection and call may all have sprung into existence due
	 * to a duplicate packet being handled on another CPU in parallel, so
	 * we have to recheck the routing.  However, we're now holding
	 * rx->incoming_lock, so the values should remain stable.
	 */
	conn = rxrpc_find_connection_rcu(local, skb, &peer);

	if (!conn) {
		sec = rxrpc_get_incoming_security(rx, skb);
		if (!sec)
			goto no_call;
	}

	call = rxrpc_alloc_incoming_call(rx, local, peer, conn, sec, skb);
	if (!call) {
		skb->mark = RXRPC_SKB_MARK_REJECT_BUSY;
		goto no_call;
	}

	trace_rxrpc_receive(call, rxrpc_receive_incoming,
			    sp->hdr.serial, sp->hdr.seq);

	/* Make the call live. */
	rxrpc_incoming_call(rx, call, skb);
	conn = call->conn;

	if (rx->notify_new_call)
		rx->notify_new_call(&rx->sk, call, call->user_call_ID);

	spin_lock(&conn->state_lock);
	switch (conn->state) {
	case RXRPC_CONN_SERVICE_UNSECURED:
		conn->state = RXRPC_CONN_SERVICE_CHALLENGING;
		set_bit(RXRPC_CONN_EV_CHALLENGE, &call->conn->events);
		rxrpc_queue_conn(call->conn);
		break;

	case RXRPC_CONN_SERVICE:
		write_lock(&call->state_lock);
		if (call->state < RXRPC_CALL_COMPLETE)
			call->state = RXRPC_CALL_SERVER_RECV_REQUEST;
		write_unlock(&call->state_lock);
		break;

	case RXRPC_CONN_REMOTELY_ABORTED:
		rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
					  conn->abort_code, conn->error);
		break;
	case RXRPC_CONN_LOCALLY_ABORTED:
		rxrpc_abort_call("CON", call, sp->hdr.seq,
				 conn->abort_code, conn->error);
		break;
	default:
		BUG();
	}
	spin_unlock(&conn->state_lock);
	spin_unlock(&rx->incoming_lock);

	rxrpc_send_ping(call, skb);

	/* We have to discard the prealloc queue's ref here and rely on a
	 * combination of the RCU read lock and refs held either by the socket
	 * (recvmsg queue, to-be-accepted queue or user ID tree) or the kernel
	 * service to prevent the call from being deallocated too early.
	 */
	rxrpc_put_call(call, rxrpc_call_put);

	_leave(" = %p{%d}", call, call->debug_id);
	return call;

no_call:
	spin_unlock(&rx->incoming_lock);
	_leave(" = NULL [%u]", skb->mark);
	return NULL;
}

/*
 * Charge up socket with preallocated calls, attaching user call IDs.
 */
int rxrpc_user_charge_accept(struct rxrpc_sock *rx, unsigned long user_call_ID)
{
	struct rxrpc_backlog *b = rx->backlog;

	if (rx->sk.sk_state == RXRPC_CLOSE)
		return -ESHUTDOWN;

	return rxrpc_service_prealloc_one(rx, b, NULL, NULL, user_call_ID,
					  GFP_KERNEL,
					  atomic_inc_return(&rxrpc_debug_id));
}

/*
 * rxrpc_kernel_charge_accept - Charge up socket with preallocated calls
 * @sock: The socket on which to preallocate
 * @notify_rx: Event notification function for the call
 * @user_attach_call: Func to attach call to user_call_ID
 * @user_call_ID: The tag to attach to the preallocated call
 * @gfp: The allocation conditions.
 * @debug_id: The tracing debug ID.
 *
 * Charge up the socket with preallocated calls, each with a user ID.  A
 * function should be provided to effect the attachment from the user's side.
 * The user is given a ref to hold on the call.
 *
 * Note that the call may be come connected before this function returns.
 */
int rxrpc_kernel_charge_accept(struct socket *sock,
			       rxrpc_notify_rx_t notify_rx,
			       rxrpc_user_attach_call_t user_attach_call,
			       unsigned long user_call_ID, gfp_t gfp,
			       unsigned int debug_id)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	struct rxrpc_backlog *b = rx->backlog;

	if (sock->sk->sk_state == RXRPC_CLOSE)
		return -ESHUTDOWN;

	return rxrpc_service_prealloc_one(rx, b, notify_rx,
					  user_attach_call, user_call_ID,
					  gfp, debug_id);
}
EXPORT_SYMBOL(rxrpc_kernel_charge_accept);
