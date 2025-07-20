// SPDX-License-Identifier: GPL-2.0-or-later
/* Out of band message handling (e.g. challenge-response)
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

enum rxrpc_oob_command {
	RXRPC_OOB_CMD_UNSET,
	RXRPC_OOB_CMD_RESPOND,
} __mode(byte);

struct rxrpc_oob_params {
	u64			oob_id;		/* ID number of message if reply */
	s32			abort_code;
	enum rxrpc_oob_command	command;
	bool			have_oob_id:1;
};

/*
 * Post an out-of-band message for attention by the socket or kernel service
 * associated with a reference call.
 */
void rxrpc_notify_socket_oob(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_sock *rx;
	struct sock *sk;

	rcu_read_lock();

	rx = rcu_dereference(call->socket);
	if (rx) {
		sk = &rx->sk;
		spin_lock_irq(&rx->recvmsg_lock);

		if (sk->sk_state < RXRPC_CLOSE) {
			skb->skb_mstamp_ns = rx->oob_id_counter++;
			rxrpc_get_skb(skb, rxrpc_skb_get_post_oob);
			skb_queue_tail(&rx->recvmsg_oobq, skb);

			trace_rxrpc_notify_socket(call->debug_id, sp->hdr.serial);
			if (rx->app_ops)
				rx->app_ops->notify_oob(sk, skb);
		}

		spin_unlock_irq(&rx->recvmsg_lock);
		if (!rx->app_ops && !sock_flag(sk, SOCK_DEAD))
			sk->sk_data_ready(sk);
	}

	rcu_read_unlock();
}

/*
 * Locate the OOB message to respond to by its ID.
 */
static struct sk_buff *rxrpc_find_pending_oob(struct rxrpc_sock *rx, u64 oob_id)
{
	struct rb_node *p;
	struct sk_buff *skb;

	p = rx->pending_oobq.rb_node;
	while (p) {
		skb = rb_entry(p, struct sk_buff, rbnode);

		if (oob_id < skb->skb_mstamp_ns)
			p = p->rb_left;
		else if (oob_id > skb->skb_mstamp_ns)
			p = p->rb_right;
		else
			return skb;
	}

	return NULL;
}

/*
 * Add an OOB message into the pending-response set.  We always assign the next
 * value from a 64-bit counter to the oob_id, so just assume we're always going
 * to be on the right-hand edge of the tree and that the counter won't wrap.
 * The tree is also given a ref to the message.
 */
void rxrpc_add_pending_oob(struct rxrpc_sock *rx, struct sk_buff *skb)
{
	struct rb_node **pp = &rx->pending_oobq.rb_node, *p = NULL;

	while (*pp) {
		p = *pp;
		pp = &(*pp)->rb_right;
	}

	rb_link_node(&skb->rbnode, p, pp);
	rb_insert_color(&skb->rbnode, &rx->pending_oobq);
}

/*
 * Extract control messages from the sendmsg() control buffer.
 */
static int rxrpc_sendmsg_oob_cmsg(struct msghdr *msg, struct rxrpc_oob_params *p)
{
	struct cmsghdr *cmsg;
	int len;

	if (msg->msg_controllen == 0)
		return -EINVAL;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;

		len = cmsg->cmsg_len - sizeof(struct cmsghdr);
		_debug("CMSG %d, %d, %d",
		       cmsg->cmsg_level, cmsg->cmsg_type, len);

		if (cmsg->cmsg_level != SOL_RXRPC)
			continue;

		switch (cmsg->cmsg_type) {
		case RXRPC_OOB_ID:
			if (len != sizeof(p->oob_id) || p->have_oob_id)
				return -EINVAL;
			memcpy(&p->oob_id, CMSG_DATA(cmsg), sizeof(p->oob_id));
			p->have_oob_id = true;
			break;
		case RXRPC_RESPOND:
			if (p->command != RXRPC_OOB_CMD_UNSET)
				return -EINVAL;
			p->command = RXRPC_OOB_CMD_RESPOND;
			break;
		case RXRPC_ABORT:
			if (len != sizeof(p->abort_code) || p->abort_code)
				return -EINVAL;
			memcpy(&p->abort_code, CMSG_DATA(cmsg), sizeof(p->abort_code));
			if (p->abort_code == 0)
				return -EINVAL;
			break;
		case RXRPC_RESP_RXGK_APPDATA:
			if (p->command != RXRPC_OOB_CMD_RESPOND)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	}

	switch (p->command) {
	case RXRPC_OOB_CMD_RESPOND:
		if (!p->have_oob_id)
			return -EBADSLT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Allow userspace to respond to an OOB using sendmsg().
 */
static int rxrpc_respond_to_oob(struct rxrpc_sock *rx,
				struct rxrpc_oob_params *p,
				struct msghdr *msg)
{
	struct rxrpc_connection *conn;
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	int ret;

	skb = rxrpc_find_pending_oob(rx, p->oob_id);
	if (skb)
		rb_erase(&skb->rbnode, &rx->pending_oobq);
	release_sock(&rx->sk);
	if (!skb)
		return -EBADSLT;

	sp = rxrpc_skb(skb);

	switch (p->command) {
	case RXRPC_OOB_CMD_RESPOND:
		ret = -EPROTO;
		if (skb->mark != RXRPC_OOB_CHALLENGE)
			break;
		conn = sp->chall.conn;
		ret = -EOPNOTSUPP;
		if (!conn->security->sendmsg_respond_to_challenge)
			break;
		if (p->abort_code) {
			rxrpc_abort_conn(conn, NULL, p->abort_code, -ECONNABORTED,
					 rxrpc_abort_response_sendmsg);
			ret = 0;
		} else {
			ret = conn->security->sendmsg_respond_to_challenge(skb, msg);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	rxrpc_free_skb(skb, rxrpc_skb_put_oob);
	return ret;
}

/*
 * Send an out-of-band message or respond to a received out-of-band message.
 * - caller gives us the socket lock
 * - the socket may be either a client socket or a server socket
 */
int rxrpc_sendmsg_oob(struct rxrpc_sock *rx, struct msghdr *msg, size_t len)
{
	struct rxrpc_oob_params p = {};
	int ret;

	_enter("");

	ret = rxrpc_sendmsg_oob_cmsg(msg, &p);
	if (ret < 0)
		goto error_release_sock;

	if (p.have_oob_id)
		return rxrpc_respond_to_oob(rx, &p, msg);

	release_sock(&rx->sk);

	switch (p.command) {
	default:
		ret = -EINVAL;
		break;
	}

	_leave(" = %d", ret);
	return ret;

error_release_sock:
	release_sock(&rx->sk);
	return ret;
}

/**
 * rxrpc_kernel_query_oob - Query the parameters of an out-of-band message
 * @oob: The message to query
 * @_peer: Where to return the peer record
 * @_peer_appdata: The application data attached to a peer record
 *
 * Extract useful parameters from an out-of-band message.  The source peer
 * parameters are returned through the argument list and the message type is
 * returned.
 *
 * Return:
 * * %RXRPC_OOB_CHALLENGE - Challenge wanting a response.
 */
enum rxrpc_oob_type rxrpc_kernel_query_oob(struct sk_buff *oob,
					   struct rxrpc_peer **_peer,
					   unsigned long *_peer_appdata)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(oob);
	enum rxrpc_oob_type type = oob->mark;

	switch (type) {
	case RXRPC_OOB_CHALLENGE:
		*_peer		= sp->chall.conn->peer;
		*_peer_appdata	= sp->chall.conn->peer->app_data;
		break;
	default:
		WARN_ON_ONCE(1);
		*_peer		= NULL;
		*_peer_appdata	= 0;
		break;
	}

	return type;
}
EXPORT_SYMBOL(rxrpc_kernel_query_oob);

/**
 * rxrpc_kernel_dequeue_oob - Dequeue and return the front OOB message
 * @sock: The socket to query
 * @_type: Where to return the message type
 *
 * Dequeue the front OOB message, if there is one, and return it and
 * its type.
 *
 * Return: The sk_buff representing the OOB message or %NULL if the queue was
 * empty.
 */
struct sk_buff *rxrpc_kernel_dequeue_oob(struct socket *sock,
					 enum rxrpc_oob_type *_type)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	struct sk_buff *oob;

	oob = skb_dequeue(&rx->recvmsg_oobq);
	if (oob)
		*_type = oob->mark;
	return oob;
}
EXPORT_SYMBOL(rxrpc_kernel_dequeue_oob);

/**
 * rxrpc_kernel_free_oob - Free an out-of-band message
 * @oob: The OOB message to free
 *
 * Free an OOB message along with any resources it holds.
 */
void rxrpc_kernel_free_oob(struct sk_buff *oob)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(oob);

	switch (oob->mark) {
	case RXRPC_OOB_CHALLENGE:
		rxrpc_put_connection(sp->chall.conn, rxrpc_conn_put_oob);
		break;
	}

	rxrpc_free_skb(oob, rxrpc_skb_put_purge_oob);
}
EXPORT_SYMBOL(rxrpc_kernel_free_oob);

/**
 * rxrpc_kernel_query_challenge - Query the parameters of a challenge
 * @challenge: The challenge to query
 * @_peer: Where to return the peer record
 * @_peer_appdata: The application data attached to a peer record
 * @_service_id: Where to return the connection service ID
 * @_security_index: Where to return the connection security index
 *
 * Extract useful parameters from a CHALLENGE message.
 */
void rxrpc_kernel_query_challenge(struct sk_buff *challenge,
				  struct rxrpc_peer **_peer,
				  unsigned long *_peer_appdata,
				  u16 *_service_id, u8 *_security_index)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(challenge);

	*_peer		= sp->chall.conn->peer;
	*_peer_appdata	= sp->chall.conn->peer->app_data;
	*_service_id	= sp->hdr.serviceId;
	*_security_index = sp->hdr.securityIndex;
}
EXPORT_SYMBOL(rxrpc_kernel_query_challenge);

/**
 * rxrpc_kernel_reject_challenge - Allow a kernel service to reject a challenge
 * @challenge: The challenge to be rejected
 * @abort_code: The abort code to stick into the ABORT packet
 * @error: Local error value
 * @why: Indication as to why.
 *
 * Allow a kernel service to reject a challenge by aborting the connection if
 * it's still in an abortable state.  The error is returned so this function
 * can be used with a return statement.
 *
 * Return: The %error parameter.
 */
int rxrpc_kernel_reject_challenge(struct sk_buff *challenge, u32 abort_code,
				  int error, enum rxrpc_abort_reason why)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(challenge);

	_enter("{%x},%d,%d,%u", sp->hdr.serial, abort_code, error, why);

	rxrpc_abort_conn(sp->chall.conn, NULL, abort_code, error, why);
	return error;
}
EXPORT_SYMBOL(rxrpc_kernel_reject_challenge);
