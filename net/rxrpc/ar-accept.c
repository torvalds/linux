/* incoming call handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/errqueue.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include "ar-internal.h"

/*
 * generate a connection-level abort
 */
static int rxrpc_busy(struct rxrpc_local *local, struct sockaddr_rxrpc *srx,
		      struct rxrpc_header *hdr)
{
	struct msghdr msg;
	struct kvec iov[1];
	size_t len;
	int ret;

	_enter("%d,,", local->debug_id);

	msg.msg_name	= &srx->transport.sin;
	msg.msg_namelen	= sizeof(srx->transport.sin);
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	hdr->seq	= 0;
	hdr->type	= RXRPC_PACKET_TYPE_BUSY;
	hdr->flags	= 0;
	hdr->userStatus	= 0;
	hdr->_rsvd	= 0;

	iov[0].iov_base	= hdr;
	iov[0].iov_len	= sizeof(*hdr);

	len = iov[0].iov_len;

	hdr->serial = htonl(1);
	_proto("Tx BUSY %%%u", ntohl(hdr->serial));

	ret = kernel_sendmsg(local->socket, &msg, iov, 1, len);
	if (ret < 0) {
		_leave(" = -EAGAIN [sendmsg failed: %d]", ret);
		return -EAGAIN;
	}

	_leave(" = 0");
	return 0;
}

/*
 * accept an incoming call that needs peer, transport and/or connection setting
 * up
 */
static int rxrpc_accept_incoming_call(struct rxrpc_local *local,
				      struct rxrpc_sock *rx,
				      struct sk_buff *skb,
				      struct sockaddr_rxrpc *srx)
{
	struct rxrpc_connection *conn;
	struct rxrpc_transport *trans;
	struct rxrpc_skb_priv *sp, *nsp;
	struct rxrpc_peer *peer;
	struct rxrpc_call *call;
	struct sk_buff *notification;
	int ret;

	_enter("");

	sp = rxrpc_skb(skb);

	/* get a notification message to send to the server app */
	notification = alloc_skb(0, GFP_NOFS);
	rxrpc_new_skb(notification);
	notification->mark = RXRPC_SKB_MARK_NEW_CALL;

	peer = rxrpc_get_peer(srx, GFP_NOIO);
	if (IS_ERR(peer)) {
		_debug("no peer");
		ret = -EBUSY;
		goto error;
	}

	trans = rxrpc_get_transport(local, peer, GFP_NOIO);
	rxrpc_put_peer(peer);
	if (IS_ERR(trans)) {
		_debug("no trans");
		ret = -EBUSY;
		goto error;
	}

	conn = rxrpc_incoming_connection(trans, &sp->hdr, GFP_NOIO);
	rxrpc_put_transport(trans);
	if (IS_ERR(conn)) {
		_debug("no conn");
		ret = PTR_ERR(conn);
		goto error;
	}

	call = rxrpc_incoming_call(rx, conn, &sp->hdr, GFP_NOIO);
	rxrpc_put_connection(conn);
	if (IS_ERR(call)) {
		_debug("no call");
		ret = PTR_ERR(call);
		goto error;
	}

	/* attach the call to the socket */
	read_lock_bh(&local->services_lock);
	if (rx->sk.sk_state == RXRPC_CLOSE)
		goto invalid_service;

	write_lock(&rx->call_lock);
	if (!test_and_set_bit(RXRPC_CALL_INIT_ACCEPT, &call->flags)) {
		rxrpc_get_call(call);

		spin_lock(&call->conn->state_lock);
		if (sp->hdr.securityIndex > 0 &&
		    call->conn->state == RXRPC_CONN_SERVER_UNSECURED) {
			_debug("await conn sec");
			list_add_tail(&call->accept_link, &rx->secureq);
			call->conn->state = RXRPC_CONN_SERVER_CHALLENGING;
			atomic_inc(&call->conn->usage);
			set_bit(RXRPC_CONN_CHALLENGE, &call->conn->events);
			rxrpc_queue_conn(call->conn);
		} else {
			_debug("conn ready");
			call->state = RXRPC_CALL_SERVER_ACCEPTING;
			list_add_tail(&call->accept_link, &rx->acceptq);
			rxrpc_get_call(call);
			nsp = rxrpc_skb(notification);
			nsp->call = call;

			ASSERTCMP(atomic_read(&call->usage), >=, 3);

			_debug("notify");
			spin_lock(&call->lock);
			ret = rxrpc_queue_rcv_skb(call, notification, true,
						  false);
			spin_unlock(&call->lock);
			notification = NULL;
			BUG_ON(ret < 0);
		}
		spin_unlock(&call->conn->state_lock);

		_debug("queued");
	}
	write_unlock(&rx->call_lock);

	_debug("process");
	rxrpc_fast_process_packet(call, skb);

	_debug("done");
	read_unlock_bh(&local->services_lock);
	rxrpc_free_skb(notification);
	rxrpc_put_call(call);
	_leave(" = 0");
	return 0;

invalid_service:
	_debug("invalid");
	read_unlock_bh(&local->services_lock);

	read_lock_bh(&call->state_lock);
	if (!test_bit(RXRPC_CALL_RELEASE, &call->flags) &&
	    !test_and_set_bit(RXRPC_CALL_RELEASE, &call->events)) {
		rxrpc_get_call(call);
		rxrpc_queue_call(call);
	}
	read_unlock_bh(&call->state_lock);
	rxrpc_put_call(call);
	ret = -ECONNREFUSED;
error:
	rxrpc_free_skb(notification);
	_leave(" = %d", ret);
	return ret;
}

/*
 * accept incoming calls that need peer, transport and/or connection setting up
 * - the packets we get are all incoming client DATA packets that have seq == 1
 */
void rxrpc_accept_incoming_calls(struct work_struct *work)
{
	struct rxrpc_local *local =
		container_of(work, struct rxrpc_local, acceptor);
	struct rxrpc_skb_priv *sp;
	struct sockaddr_rxrpc srx;
	struct rxrpc_sock *rx;
	struct sk_buff *skb;
	__be16 service_id;
	int ret;

	_enter("%d", local->debug_id);

	read_lock_bh(&rxrpc_local_lock);
	if (atomic_read(&local->usage) > 0)
		rxrpc_get_local(local);
	else
		local = NULL;
	read_unlock_bh(&rxrpc_local_lock);
	if (!local) {
		_leave(" [local dead]");
		return;
	}

process_next_packet:
	skb = skb_dequeue(&local->accept_queue);
	if (!skb) {
		rxrpc_put_local(local);
		_leave("\n");
		return;
	}

	_net("incoming call skb %p", skb);

	sp = rxrpc_skb(skb);

	/* determine the remote address */
	memset(&srx, 0, sizeof(srx));
	srx.srx_family = AF_RXRPC;
	srx.transport.family = local->srx.transport.family;
	srx.transport_type = local->srx.transport_type;
	switch (srx.transport.family) {
	case AF_INET:
		srx.transport_len = sizeof(struct sockaddr_in);
		srx.transport.sin.sin_port = udp_hdr(skb)->source;
		srx.transport.sin.sin_addr.s_addr = ip_hdr(skb)->saddr;
		break;
	default:
		goto busy;
	}

	/* get the socket providing the service */
	service_id = sp->hdr.serviceId;
	read_lock_bh(&local->services_lock);
	list_for_each_entry(rx, &local->services, listen_link) {
		if (rx->service_id == service_id &&
		    rx->sk.sk_state != RXRPC_CLOSE)
			goto found_service;
	}
	read_unlock_bh(&local->services_lock);
	goto invalid_service;

found_service:
	_debug("found service %hd", ntohs(rx->service_id));
	if (sk_acceptq_is_full(&rx->sk))
		goto backlog_full;
	sk_acceptq_added(&rx->sk);
	sock_hold(&rx->sk);
	read_unlock_bh(&local->services_lock);

	ret = rxrpc_accept_incoming_call(local, rx, skb, &srx);
	if (ret < 0)
		sk_acceptq_removed(&rx->sk);
	sock_put(&rx->sk);
	switch (ret) {
	case -ECONNRESET: /* old calls are ignored */
	case -ECONNABORTED: /* aborted calls are reaborted or ignored */
	case 0:
		goto process_next_packet;
	case -ECONNREFUSED:
		goto invalid_service;
	case -EBUSY:
		goto busy;
	case -EKEYREJECTED:
		goto security_mismatch;
	default:
		BUG();
	}

backlog_full:
	read_unlock_bh(&local->services_lock);
busy:
	rxrpc_busy(local, &srx, &sp->hdr);
	rxrpc_free_skb(skb);
	goto process_next_packet;

invalid_service:
	skb->priority = RX_INVALID_OPERATION;
	rxrpc_reject_packet(local, skb);
	goto process_next_packet;

	/* can't change connection security type mid-flow */
security_mismatch:
	skb->priority = RX_PROTOCOL_ERROR;
	rxrpc_reject_packet(local, skb);
	goto process_next_packet;
}

/*
 * handle acceptance of a call by userspace
 * - assign the user call ID to the call at the front of the queue
 */
struct rxrpc_call *rxrpc_accept_call(struct rxrpc_sock *rx,
				     unsigned long user_call_ID)
{
	struct rxrpc_call *call;
	struct rb_node *parent, **pp;
	int ret;

	_enter(",%lx", user_call_ID);

	ASSERT(!irqs_disabled());

	write_lock(&rx->call_lock);

	ret = -ENODATA;
	if (list_empty(&rx->acceptq))
		goto out;

	/* check the user ID isn't already in use */
	ret = -EBADSLT;
	pp = &rx->calls.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		call = rb_entry(parent, struct rxrpc_call, sock_node);

		if (user_call_ID < call->user_call_ID)
			pp = &(*pp)->rb_left;
		else if (user_call_ID > call->user_call_ID)
			pp = &(*pp)->rb_right;
		else
			goto out;
	}

	/* dequeue the first call and check it's still valid */
	call = list_entry(rx->acceptq.next, struct rxrpc_call, accept_link);
	list_del_init(&call->accept_link);
	sk_acceptq_removed(&rx->sk);

	write_lock_bh(&call->state_lock);
	switch (call->state) {
	case RXRPC_CALL_SERVER_ACCEPTING:
		call->state = RXRPC_CALL_SERVER_RECV_REQUEST;
		break;
	case RXRPC_CALL_REMOTELY_ABORTED:
	case RXRPC_CALL_LOCALLY_ABORTED:
		ret = -ECONNABORTED;
		goto out_release;
	case RXRPC_CALL_NETWORK_ERROR:
		ret = call->conn->error;
		goto out_release;
	case RXRPC_CALL_DEAD:
		ret = -ETIME;
		goto out_discard;
	default:
		BUG();
	}

	/* formalise the acceptance */
	call->user_call_ID = user_call_ID;
	rb_link_node(&call->sock_node, parent, pp);
	rb_insert_color(&call->sock_node, &rx->calls);
	if (test_and_set_bit(RXRPC_CALL_HAS_USERID, &call->flags))
		BUG();
	if (test_and_set_bit(RXRPC_CALL_ACCEPTED, &call->events))
		BUG();
	rxrpc_queue_call(call);

	rxrpc_get_call(call);
	write_unlock_bh(&call->state_lock);
	write_unlock(&rx->call_lock);
	_leave(" = %p{%d}", call, call->debug_id);
	return call;

	/* if the call is already dying or dead, then we leave the socket's ref
	 * on it to be released by rxrpc_dead_call_expired() as induced by
	 * rxrpc_release_call() */
out_release:
	_debug("release %p", call);
	if (!test_bit(RXRPC_CALL_RELEASED, &call->flags) &&
	    !test_and_set_bit(RXRPC_CALL_RELEASE, &call->events))
		rxrpc_queue_call(call);
out_discard:
	write_unlock_bh(&call->state_lock);
	_debug("discard %p", call);
out:
	write_unlock(&rx->call_lock);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * handle rejectance of a call by userspace
 * - reject the call at the front of the queue
 */
int rxrpc_reject_call(struct rxrpc_sock *rx)
{
	struct rxrpc_call *call;
	int ret;

	_enter("");

	ASSERT(!irqs_disabled());

	write_lock(&rx->call_lock);

	ret = -ENODATA;
	if (list_empty(&rx->acceptq))
		goto out;

	/* dequeue the first call and check it's still valid */
	call = list_entry(rx->acceptq.next, struct rxrpc_call, accept_link);
	list_del_init(&call->accept_link);
	sk_acceptq_removed(&rx->sk);

	write_lock_bh(&call->state_lock);
	switch (call->state) {
	case RXRPC_CALL_SERVER_ACCEPTING:
		call->state = RXRPC_CALL_SERVER_BUSY;
		if (test_and_set_bit(RXRPC_CALL_REJECT_BUSY, &call->events))
			rxrpc_queue_call(call);
		ret = 0;
		goto out_release;
	case RXRPC_CALL_REMOTELY_ABORTED:
	case RXRPC_CALL_LOCALLY_ABORTED:
		ret = -ECONNABORTED;
		goto out_release;
	case RXRPC_CALL_NETWORK_ERROR:
		ret = call->conn->error;
		goto out_release;
	case RXRPC_CALL_DEAD:
		ret = -ETIME;
		goto out_discard;
	default:
		BUG();
	}

	/* if the call is already dying or dead, then we leave the socket's ref
	 * on it to be released by rxrpc_dead_call_expired() as induced by
	 * rxrpc_release_call() */
out_release:
	_debug("release %p", call);
	if (!test_bit(RXRPC_CALL_RELEASED, &call->flags) &&
	    !test_and_set_bit(RXRPC_CALL_RELEASE, &call->events))
		rxrpc_queue_call(call);
out_discard:
	write_unlock_bh(&call->state_lock);
	_debug("discard %p", call);
out:
	write_unlock(&rx->call_lock);
	_leave(" = %d", ret);
	return ret;
}

/**
 * rxrpc_kernel_accept_call - Allow a kernel service to accept an incoming call
 * @sock: The socket on which the impending call is waiting
 * @user_call_ID: The tag to attach to the call
 *
 * Allow a kernel service to accept an incoming call, assuming the incoming
 * call is still valid.
 */
struct rxrpc_call *rxrpc_kernel_accept_call(struct socket *sock,
					    unsigned long user_call_ID)
{
	struct rxrpc_call *call;

	_enter(",%lx", user_call_ID);
	call = rxrpc_accept_call(rxrpc_sk(sock->sk), user_call_ID);
	_leave(" = %p", call);
	return call;
}

EXPORT_SYMBOL(rxrpc_kernel_accept_call);

/**
 * rxrpc_kernel_reject_call - Allow a kernel service to reject an incoming call
 * @sock: The socket on which the impending call is waiting
 *
 * Allow a kernel service to reject an incoming call with a BUSY message,
 * assuming the incoming call is still valid.
 */
int rxrpc_kernel_reject_call(struct socket *sock)
{
	int ret;

	_enter("");
	ret = rxrpc_reject_call(rxrpc_sk(sock->sk));
	_leave(" = %d", ret);
	return ret;
}

EXPORT_SYMBOL(rxrpc_kernel_reject_call);
