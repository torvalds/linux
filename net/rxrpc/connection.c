/* connection.c: Rx connection routines
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include "internal.h"

__RXACCT_DECL(atomic_t rxrpc_connection_count);

LIST_HEAD(rxrpc_conns);
DECLARE_RWSEM(rxrpc_conns_sem);
unsigned long rxrpc_conn_timeout = 60 * 60;

static void rxrpc_conn_do_timeout(struct rxrpc_connection *conn);

static void __rxrpc_conn_timeout(rxrpc_timer_t *timer)
{
	struct rxrpc_connection *conn =
		list_entry(timer, struct rxrpc_connection, timeout);

	_debug("Rx CONN TIMEOUT [%p{u=%d}]", conn, atomic_read(&conn->usage));

	rxrpc_conn_do_timeout(conn);
}

static const struct rxrpc_timer_ops rxrpc_conn_timer_ops = {
	.timed_out	= __rxrpc_conn_timeout,
};

/*****************************************************************************/
/*
 * create a new connection record
 */
static inline int __rxrpc_create_connection(struct rxrpc_peer *peer,
					    struct rxrpc_connection **_conn)
{
	struct rxrpc_connection *conn;

	_enter("%p",peer);

	/* allocate and initialise a connection record */
	conn = kmalloc(sizeof(struct rxrpc_connection), GFP_KERNEL);
	if (!conn) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	memset(conn, 0, sizeof(struct rxrpc_connection));
	atomic_set(&conn->usage, 1);

	INIT_LIST_HEAD(&conn->link);
	INIT_LIST_HEAD(&conn->id_link);
	init_waitqueue_head(&conn->chanwait);
	spin_lock_init(&conn->lock);
	rxrpc_timer_init(&conn->timeout, &rxrpc_conn_timer_ops);

	do_gettimeofday(&conn->atime);
	conn->mtu_size = 1024;
	conn->peer = peer;
	conn->trans = peer->trans;

	__RXACCT(atomic_inc(&rxrpc_connection_count));
	*_conn = conn;
	_leave(" = 0 (%p)", conn);

	return 0;
} /* end __rxrpc_create_connection() */

/*****************************************************************************/
/*
 * create a new connection record for outgoing connections
 */
int rxrpc_create_connection(struct rxrpc_transport *trans,
			    __be16 port,
			    __be32 addr,
			    uint16_t service_id,
			    void *security,
			    struct rxrpc_connection **_conn)
{
	struct rxrpc_connection *candidate, *conn;
	struct rxrpc_peer *peer;
	struct list_head *_p;
	__be32 connid;
	int ret;

	_enter("%p{%hu},%u,%hu", trans, trans->port, ntohs(port), service_id);

	/* get a peer record */
	ret = rxrpc_peer_lookup(trans, addr, &peer);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	/* allocate and initialise a connection record */
	ret = __rxrpc_create_connection(peer, &candidate);
	if (ret < 0) {
		rxrpc_put_peer(peer);
		_leave(" = %d", ret);
		return ret;
	}

	/* fill in the specific bits */
	candidate->addr.sin_family	= AF_INET;
	candidate->addr.sin_port	= port;
	candidate->addr.sin_addr.s_addr	= addr;

	candidate->in_epoch		= rxrpc_epoch;
	candidate->out_epoch		= rxrpc_epoch;
	candidate->in_clientflag	= 0;
	candidate->out_clientflag	= RXRPC_CLIENT_INITIATED;
	candidate->service_id		= htons(service_id);

	/* invent a unique connection ID */
	write_lock(&peer->conn_idlock);

 try_next_id:
	connid = htonl(peer->conn_idcounter & RXRPC_CIDMASK);
	peer->conn_idcounter += RXRPC_MAXCALLS;

	list_for_each(_p, &peer->conn_idlist) {
		conn = list_entry(_p, struct rxrpc_connection, id_link);
		if (connid == conn->conn_id)
			goto try_next_id;
		if (connid > conn->conn_id)
			break;
	}

	_debug("selected candidate conn ID %x.%u",
	       ntohl(peer->addr.s_addr), ntohl(connid));

	candidate->conn_id = connid;
	list_add_tail(&candidate->id_link, _p);

	write_unlock(&peer->conn_idlock);

	/* attach to peer */
	candidate->peer = peer;

	write_lock(&peer->conn_lock);

	/* search the peer's transport graveyard list */
	spin_lock(&peer->conn_gylock);
	list_for_each(_p, &peer->conn_graveyard) {
		conn = list_entry(_p, struct rxrpc_connection, link);
		if (conn->addr.sin_port	== candidate->addr.sin_port	&&
		    conn->security_ix	== candidate->security_ix	&&
		    conn->service_id	== candidate->service_id	&& 
		    conn->in_clientflag	== 0)
			goto found_in_graveyard;
	}
	spin_unlock(&peer->conn_gylock);

	/* pick the new candidate */
	_debug("created connection: {%08x} [out]", ntohl(candidate->conn_id));
	atomic_inc(&peer->conn_count);
	conn = candidate;
	candidate = NULL;

 make_active:
	list_add_tail(&conn->link, &peer->conn_active);
	write_unlock(&peer->conn_lock);

	if (candidate) {
		write_lock(&peer->conn_idlock);
		list_del(&candidate->id_link);
		write_unlock(&peer->conn_idlock);

		__RXACCT(atomic_dec(&rxrpc_connection_count));
		kfree(candidate);
	}
	else {
		down_write(&rxrpc_conns_sem);
		list_add_tail(&conn->proc_link, &rxrpc_conns);
		up_write(&rxrpc_conns_sem);
	}

	*_conn = conn;
	_leave(" = 0 (%p)", conn);

	return 0;

	/* handle resurrecting a connection from the graveyard */
 found_in_graveyard:
	_debug("resurrecting connection: {%08x} [out]", ntohl(conn->conn_id));
	rxrpc_get_connection(conn);
	rxrpc_krxtimod_del_timer(&conn->timeout);
	list_del_init(&conn->link);
	spin_unlock(&peer->conn_gylock);
	goto make_active;
} /* end rxrpc_create_connection() */

/*****************************************************************************/
/*
 * lookup the connection for an incoming packet
 * - create a new connection record for unrecorded incoming connections
 */
int rxrpc_connection_lookup(struct rxrpc_peer *peer,
			    struct rxrpc_message *msg,
			    struct rxrpc_connection **_conn)
{
	struct rxrpc_connection *conn, *candidate = NULL;
	struct list_head *_p;
	int ret, fresh = 0;
	__be32 x_epoch, x_connid;
	__be16 x_port, x_servid;
	__u32 x_secix;
	u8 x_clflag;

	_enter("%p{{%hu}},%u,%hu",
	       peer,
	       peer->trans->port,
	       ntohs(msg->pkt->h.uh->source),
	       ntohs(msg->hdr.serviceId));

	x_port		= msg->pkt->h.uh->source;
	x_epoch		= msg->hdr.epoch;
	x_clflag	= msg->hdr.flags & RXRPC_CLIENT_INITIATED;
	x_connid	= htonl(ntohl(msg->hdr.cid) & RXRPC_CIDMASK);
	x_servid	= msg->hdr.serviceId;
	x_secix		= msg->hdr.securityIndex;

	/* [common case] search the transport's active list first */
	read_lock(&peer->conn_lock);
	list_for_each(_p, &peer->conn_active) {
		conn = list_entry(_p, struct rxrpc_connection, link);
		if (conn->addr.sin_port		== x_port	&&
		    conn->in_epoch		== x_epoch	&&
		    conn->conn_id		== x_connid	&&
		    conn->security_ix		== x_secix	&&
		    conn->service_id		== x_servid	&& 
		    conn->in_clientflag		== x_clflag)
			goto found_active;
	}
	read_unlock(&peer->conn_lock);

	/* [uncommon case] not active 
	 * - create a candidate for a new record if an inbound connection
	 * - only examine the graveyard for an outbound connection
	 */
	if (x_clflag) {
		ret = __rxrpc_create_connection(peer, &candidate);
		if (ret < 0) {
			_leave(" = %d", ret);
			return ret;
		}

		/* fill in the specifics */
		candidate->addr.sin_family	= AF_INET;
		candidate->addr.sin_port	= x_port;
		candidate->addr.sin_addr.s_addr = msg->pkt->nh.iph->saddr;
		candidate->in_epoch		= x_epoch;
		candidate->out_epoch		= x_epoch;
		candidate->in_clientflag	= RXRPC_CLIENT_INITIATED;
		candidate->out_clientflag	= 0;
		candidate->conn_id		= x_connid;
		candidate->service_id		= x_servid;
		candidate->security_ix		= x_secix;
	}

	/* search the active list again, just in case it appeared whilst we
	 * were busy */
	write_lock(&peer->conn_lock);
	list_for_each(_p, &peer->conn_active) {
		conn = list_entry(_p, struct rxrpc_connection, link);
		if (conn->addr.sin_port		== x_port	&&
		    conn->in_epoch		== x_epoch	&&
		    conn->conn_id		== x_connid	&&
		    conn->security_ix		== x_secix	&&
		    conn->service_id		== x_servid	&& 
		    conn->in_clientflag		== x_clflag)
			goto found_active_second_chance;
	}

	/* search the transport's graveyard list */
	spin_lock(&peer->conn_gylock);
	list_for_each(_p, &peer->conn_graveyard) {
		conn = list_entry(_p, struct rxrpc_connection, link);
		if (conn->addr.sin_port		== x_port	&&
		    conn->in_epoch		== x_epoch	&&
		    conn->conn_id		== x_connid	&&
		    conn->security_ix		== x_secix	&&
		    conn->service_id		== x_servid	&& 
		    conn->in_clientflag		== x_clflag)
			goto found_in_graveyard;
	}
	spin_unlock(&peer->conn_gylock);

	/* outbound connections aren't created here */
	if (!x_clflag) {
		write_unlock(&peer->conn_lock);
		_leave(" = -ENOENT");
		return -ENOENT;
	}

	/* we can now add the new candidate to the list */
	_debug("created connection: {%08x} [in]", ntohl(candidate->conn_id));
	rxrpc_get_peer(peer);
	conn = candidate;
	candidate = NULL;
	atomic_inc(&peer->conn_count);
	fresh = 1;

 make_active:
	list_add_tail(&conn->link, &peer->conn_active);

 success_uwfree:
	write_unlock(&peer->conn_lock);

	if (candidate) {
		write_lock(&peer->conn_idlock);
		list_del(&candidate->id_link);
		write_unlock(&peer->conn_idlock);

		__RXACCT(atomic_dec(&rxrpc_connection_count));
		kfree(candidate);
	}

	if (fresh) {
		down_write(&rxrpc_conns_sem);
		list_add_tail(&conn->proc_link, &rxrpc_conns);
		up_write(&rxrpc_conns_sem);
	}

 success:
	*_conn = conn;
	_leave(" = 0 (%p)", conn);
	return 0;

	/* handle the connection being found in the active list straight off */
 found_active:
	rxrpc_get_connection(conn);
	read_unlock(&peer->conn_lock);
	goto success;

	/* handle resurrecting a connection from the graveyard */
 found_in_graveyard:
	_debug("resurrecting connection: {%08x} [in]", ntohl(conn->conn_id));
	rxrpc_get_peer(peer);
	rxrpc_get_connection(conn);
	rxrpc_krxtimod_del_timer(&conn->timeout);
	list_del_init(&conn->link);
	spin_unlock(&peer->conn_gylock);
	goto make_active;

	/* handle finding the connection on the second time through the active
	 * list */
 found_active_second_chance:
	rxrpc_get_connection(conn);
	goto success_uwfree;

} /* end rxrpc_connection_lookup() */

/*****************************************************************************/
/*
 * finish using a connection record
 * - it will be transferred to the peer's connection graveyard when refcount
 *   reaches 0
 */
void rxrpc_put_connection(struct rxrpc_connection *conn)
{
	struct rxrpc_peer *peer;

	if (!conn)
		return;

	_enter("%p{u=%d p=%hu}",
	       conn, atomic_read(&conn->usage), ntohs(conn->addr.sin_port));

	peer = conn->peer;
	spin_lock(&peer->conn_gylock);

	/* sanity check */
	if (atomic_read(&conn->usage) <= 0)
		BUG();

	if (likely(!atomic_dec_and_test(&conn->usage))) {
		spin_unlock(&peer->conn_gylock);
		_leave("");
		return;
	}

	/* move to graveyard queue */
	_debug("burying connection: {%08x}", ntohl(conn->conn_id));
	list_del(&conn->link);
	list_add_tail(&conn->link, &peer->conn_graveyard);

	rxrpc_krxtimod_add_timer(&conn->timeout, rxrpc_conn_timeout * HZ);

	spin_unlock(&peer->conn_gylock);

	rxrpc_put_peer(conn->peer);

	_leave(" [killed]");
} /* end rxrpc_put_connection() */

/*****************************************************************************/
/*
 * free a connection record
 */
static void rxrpc_conn_do_timeout(struct rxrpc_connection *conn)
{
	struct rxrpc_peer *peer;

	_enter("%p{u=%d p=%hu}",
	       conn, atomic_read(&conn->usage), ntohs(conn->addr.sin_port));

	peer = conn->peer;

	if (atomic_read(&conn->usage) < 0)
		BUG();

	/* remove from graveyard if still dead */
	spin_lock(&peer->conn_gylock);
	if (atomic_read(&conn->usage) == 0) {
		list_del_init(&conn->link);
	}
	else {
		conn = NULL;
	}
	spin_unlock(&peer->conn_gylock);

	if (!conn) {
		_leave("");
		return; /* resurrected */
	}

	_debug("--- Destroying Connection %p{%08x} ---",
	       conn, ntohl(conn->conn_id));

	down_write(&rxrpc_conns_sem);
	list_del(&conn->proc_link);
	up_write(&rxrpc_conns_sem);

	write_lock(&peer->conn_idlock);
	list_del(&conn->id_link);
	write_unlock(&peer->conn_idlock);

	__RXACCT(atomic_dec(&rxrpc_connection_count));
	kfree(conn);

	/* if the graveyard is now empty, wake up anyone waiting for that */
	if (atomic_dec_and_test(&peer->conn_count))
		wake_up(&peer->conn_gy_waitq);

	_leave(" [destroyed]");
} /* end rxrpc_conn_do_timeout() */

/*****************************************************************************/
/*
 * clear all connection records from a peer endpoint
 */
void rxrpc_conn_clearall(struct rxrpc_peer *peer)
{
	DECLARE_WAITQUEUE(myself, current);

	struct rxrpc_connection *conn;
	int err;

	_enter("%p", peer);

	/* there shouldn't be any active conns remaining */
	if (!list_empty(&peer->conn_active))
		BUG();

	/* manually timeout all conns in the graveyard */
	spin_lock(&peer->conn_gylock);
	while (!list_empty(&peer->conn_graveyard)) {
		conn = list_entry(peer->conn_graveyard.next,
				  struct rxrpc_connection, link);
		err = rxrpc_krxtimod_del_timer(&conn->timeout);
		spin_unlock(&peer->conn_gylock);

		if (err == 0)
			rxrpc_conn_do_timeout(conn);

		spin_lock(&peer->conn_gylock);
	}
	spin_unlock(&peer->conn_gylock);

	/* wait for the the conn graveyard to be completely cleared */
	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&peer->conn_gy_waitq, &myself);

	while (atomic_read(&peer->conn_count) != 0) {
		schedule();
		set_current_state(TASK_UNINTERRUPTIBLE);
	}

	remove_wait_queue(&peer->conn_gy_waitq, &myself);
	set_current_state(TASK_RUNNING);

	_leave("");
} /* end rxrpc_conn_clearall() */

/*****************************************************************************/
/*
 * allocate and prepare a message for sending out through the transport
 * endpoint
 */
int rxrpc_conn_newmsg(struct rxrpc_connection *conn,
		      struct rxrpc_call *call,
		      uint8_t type,
		      int dcount,
		      struct kvec diov[],
		      unsigned int __nocast alloc_flags,
		      struct rxrpc_message **_msg)
{
	struct rxrpc_message *msg;
	int loop;

	_enter("%p{%d},%p,%u", conn, ntohs(conn->addr.sin_port), call, type);

	if (dcount > 3) {
		_leave(" = -EINVAL");
		return -EINVAL;
	}

	msg = kmalloc(sizeof(struct rxrpc_message), alloc_flags);
	if (!msg) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	memset(msg, 0, sizeof(*msg));
	atomic_set(&msg->usage, 1);

	INIT_LIST_HEAD(&msg->link);

	msg->state = RXRPC_MSG_PREPARED;

	msg->hdr.epoch		= conn->out_epoch;
	msg->hdr.cid		= conn->conn_id | (call ? call->chan_ix : 0);
	msg->hdr.callNumber	= call ? call->call_id : 0;
	msg->hdr.type		= type;
	msg->hdr.flags		= conn->out_clientflag;
	msg->hdr.securityIndex	= conn->security_ix;
	msg->hdr.serviceId	= conn->service_id;

	/* generate sequence numbers for data packets */
	if (call) {
		switch (type) {
		case RXRPC_PACKET_TYPE_DATA:
			msg->seq = ++call->snd_seq_count;
			msg->hdr.seq = htonl(msg->seq);
			break;
		case RXRPC_PACKET_TYPE_ACK:
			/* ACK sequence numbers are complicated. The following
			 * may be wrong:
			 * - jumbo packet ACKs should have a seq number
			 * - normal ACKs should not
			 */
		default:
			break;
		}
	}

	msg->dcount = dcount + 1;
	msg->dsize = sizeof(msg->hdr);
	msg->data[0].iov_len = sizeof(msg->hdr);
	msg->data[0].iov_base = &msg->hdr;

	for (loop=0; loop < dcount; loop++) {
		msg->dsize += diov[loop].iov_len;
		msg->data[loop+1].iov_len  = diov[loop].iov_len;
		msg->data[loop+1].iov_base = diov[loop].iov_base;
	}

	__RXACCT(atomic_inc(&rxrpc_message_count));
	*_msg = msg;
	_leave(" = 0 (%p) #%d", msg, atomic_read(&rxrpc_message_count));
	return 0;
} /* end rxrpc_conn_newmsg() */

/*****************************************************************************/
/*
 * free a message
 */
void __rxrpc_put_message(struct rxrpc_message *msg)
{
	int loop;

	_enter("%p #%d", msg, atomic_read(&rxrpc_message_count));

	if (msg->pkt)
		kfree_skb(msg->pkt);
	rxrpc_put_connection(msg->conn);

	for (loop = 0; loop < 8; loop++)
		if (test_bit(loop, &msg->dfree))
			kfree(msg->data[loop].iov_base);

	__RXACCT(atomic_dec(&rxrpc_message_count));
	kfree(msg);

	_leave("");
} /* end __rxrpc_put_message() */

/*****************************************************************************/
/*
 * send a message out through the transport endpoint
 */
int rxrpc_conn_sendmsg(struct rxrpc_connection *conn,
		       struct rxrpc_message *msg)
{
	struct msghdr msghdr;
	int ret;

	_enter("%p{%d}", conn, ntohs(conn->addr.sin_port));

	/* fill in some fields in the header */
	spin_lock(&conn->lock);
	msg->hdr.serial = htonl(++conn->serial_counter);
	msg->rttdone = 0;
	spin_unlock(&conn->lock);

	/* set up the message to be transmitted */
	msghdr.msg_name		= &conn->addr;
	msghdr.msg_namelen	= sizeof(conn->addr);
	msghdr.msg_control	= NULL;
	msghdr.msg_controllen	= 0;
	msghdr.msg_flags	= MSG_CONFIRM | MSG_DONTWAIT;

	_net("Sending message type %d of %Zd bytes to %08x:%d",
	     msg->hdr.type,
	     msg->dsize,
	     ntohl(conn->addr.sin_addr.s_addr),
	     ntohs(conn->addr.sin_port));

	/* send the message */
	ret = kernel_sendmsg(conn->trans->socket, &msghdr,
			     msg->data, msg->dcount, msg->dsize);
	if (ret < 0) {
		msg->state = RXRPC_MSG_ERROR;
	} else {
		msg->state = RXRPC_MSG_SENT;
		ret = 0;

		spin_lock(&conn->lock);
		do_gettimeofday(&conn->atime);
		msg->stamp = conn->atime;
		spin_unlock(&conn->lock);
	}

	_leave(" = %d", ret);

	return ret;
} /* end rxrpc_conn_sendmsg() */

/*****************************************************************************/
/*
 * deal with a subsequent call packet
 */
int rxrpc_conn_receive_call_packet(struct rxrpc_connection *conn,
				   struct rxrpc_call *call,
				   struct rxrpc_message *msg)
{
	struct rxrpc_message *pmsg;
	struct list_head *_p;
	unsigned cix, seq;
	int ret = 0;

	_enter("%p,%p,%p", conn, call, msg);

	if (!call) {
		cix = ntohl(msg->hdr.cid) & RXRPC_CHANNELMASK;

		spin_lock(&conn->lock);
		call = conn->channels[cix];

		if (!call || call->call_id != msg->hdr.callNumber) {
			spin_unlock(&conn->lock);
			rxrpc_trans_immediate_abort(conn->trans, msg, -ENOENT);
			goto out;
		}
		else {
			rxrpc_get_call(call);
			spin_unlock(&conn->lock);
		}
	}
	else {
		rxrpc_get_call(call);
	}

	_proto("Received packet %%%u [%u] on call %hu:%u:%u",
	       ntohl(msg->hdr.serial),
	       ntohl(msg->hdr.seq),
	       ntohs(msg->hdr.serviceId),
	       ntohl(conn->conn_id),
	       ntohl(call->call_id));

	call->pkt_rcv_count++;

	if (msg->pkt->dst && msg->pkt->dst->dev)
		conn->peer->if_mtu =
			msg->pkt->dst->dev->mtu -
			msg->pkt->dst->dev->hard_header_len;

	/* queue on the call in seq order */
	rxrpc_get_message(msg);
	seq = msg->seq;

	spin_lock(&call->lock);
	list_for_each(_p, &call->rcv_receiveq) {
		pmsg = list_entry(_p, struct rxrpc_message, link);
		if (pmsg->seq > seq)
			break;
	}
	list_add_tail(&msg->link, _p);

	/* reset the activity timeout */
	call->flags |= RXRPC_CALL_RCV_PKT;
	mod_timer(&call->rcv_timeout,jiffies + rxrpc_call_rcv_timeout * HZ);

	spin_unlock(&call->lock);

	rxrpc_krxiod_queue_call(call);

	rxrpc_put_call(call);
 out:
	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_conn_receive_call_packet() */

/*****************************************************************************/
/*
 * handle an ICMP error being applied to a connection
 */
void rxrpc_conn_handle_error(struct rxrpc_connection *conn,
			     int local, int errno)
{
	struct rxrpc_call *calls[4];
	int loop;

	_enter("%p{%d},%d", conn, ntohs(conn->addr.sin_port), errno);

	/* get a ref to all my calls in one go */
	memset(calls, 0, sizeof(calls));
	spin_lock(&conn->lock);

	for (loop = 3; loop >= 0; loop--) {
		if (conn->channels[loop]) {
			calls[loop] = conn->channels[loop];
			rxrpc_get_call(calls[loop]);
		}
	}

	spin_unlock(&conn->lock);

	/* now kick them all */
	for (loop = 3; loop >= 0; loop--) {
		if (calls[loop]) {
			rxrpc_call_handle_error(calls[loop], local, errno);
			rxrpc_put_call(calls[loop]);
		}
	}

	_leave("");
} /* end rxrpc_conn_handle_error() */
