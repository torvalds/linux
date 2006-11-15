/* transport.c: Rx Transport routines
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
#include <rxrpc/transport.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include <rxrpc/krxiod.h>
#include <rxrpc/krxsecd.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/ip.h>
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/ipv6.h>	/* this should _really_ be in errqueue.h.. */
#endif
#include <linux/errqueue.h>
#include <asm/uaccess.h>
#include "internal.h"

struct errormsg {
	struct cmsghdr			cmsg;		/* control message header */
	struct sock_extended_err	ee;		/* extended error information */
	struct sockaddr_in		icmp_src;	/* ICMP packet source address */
};

static DEFINE_SPINLOCK(rxrpc_transports_lock);
static struct list_head rxrpc_transports = LIST_HEAD_INIT(rxrpc_transports);

__RXACCT_DECL(atomic_t rxrpc_transport_count);
LIST_HEAD(rxrpc_proc_transports);
DECLARE_RWSEM(rxrpc_proc_transports_sem);

static void rxrpc_data_ready(struct sock *sk, int count);
static void rxrpc_error_report(struct sock *sk);
static int rxrpc_trans_receive_new_call(struct rxrpc_transport *trans,
					struct list_head *msgq);
static void rxrpc_trans_receive_error_report(struct rxrpc_transport *trans);

/*****************************************************************************/
/*
 * create a new transport endpoint using the specified UDP port
 */
int rxrpc_create_transport(unsigned short port,
			   struct rxrpc_transport **_trans)
{
	struct rxrpc_transport *trans;
	struct sockaddr_in sin;
	mm_segment_t oldfs;
	struct sock *sock;
	int ret, opt;

	_enter("%hu", port);

	trans = kzalloc(sizeof(struct rxrpc_transport), GFP_KERNEL);
	if (!trans)
		return -ENOMEM;

	atomic_set(&trans->usage, 1);
	INIT_LIST_HEAD(&trans->services);
	INIT_LIST_HEAD(&trans->link);
	INIT_LIST_HEAD(&trans->krxiodq_link);
	spin_lock_init(&trans->lock);
	INIT_LIST_HEAD(&trans->peer_active);
	INIT_LIST_HEAD(&trans->peer_graveyard);
	spin_lock_init(&trans->peer_gylock);
	init_waitqueue_head(&trans->peer_gy_waitq);
	rwlock_init(&trans->peer_lock);
	atomic_set(&trans->peer_count, 0);
	trans->port = port;

	/* create a UDP socket to be my actual transport endpoint */
	ret = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &trans->socket);
	if (ret < 0)
		goto error;

	/* use the specified port */
	if (port) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(port);
		ret = trans->socket->ops->bind(trans->socket,
					       (struct sockaddr *) &sin,
					       sizeof(sin));
		if (ret < 0)
			goto error;
	}

	opt = 1;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = trans->socket->ops->setsockopt(trans->socket, SOL_IP, IP_RECVERR,
					     (char *) &opt, sizeof(opt));
	set_fs(oldfs);

	spin_lock(&rxrpc_transports_lock);
	list_add(&trans->link, &rxrpc_transports);
	spin_unlock(&rxrpc_transports_lock);

	/* set the socket up */
	sock = trans->socket->sk;
	sock->sk_user_data	= trans;
	sock->sk_data_ready	= rxrpc_data_ready;
	sock->sk_error_report	= rxrpc_error_report;

	down_write(&rxrpc_proc_transports_sem);
	list_add_tail(&trans->proc_link, &rxrpc_proc_transports);
	up_write(&rxrpc_proc_transports_sem);

	__RXACCT(atomic_inc(&rxrpc_transport_count));

	*_trans = trans;
	_leave(" = 0 (%p)", trans);
	return 0;

 error:
	/* finish cleaning up the transport (not really needed here, but...) */
	if (trans->socket)
		trans->socket->ops->shutdown(trans->socket, 2);

	/* close the socket */
	if (trans->socket) {
		trans->socket->sk->sk_user_data = NULL;
		sock_release(trans->socket);
		trans->socket = NULL;
	}

	kfree(trans);


	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_create_transport() */

/*****************************************************************************/
/*
 * destroy a transport endpoint
 */
void rxrpc_put_transport(struct rxrpc_transport *trans)
{
	_enter("%p{u=%d p=%hu}",
	       trans, atomic_read(&trans->usage), trans->port);

	BUG_ON(atomic_read(&trans->usage) <= 0);

	/* to prevent a race, the decrement and the dequeue must be
	 * effectively atomic */
	spin_lock(&rxrpc_transports_lock);
	if (likely(!atomic_dec_and_test(&trans->usage))) {
		spin_unlock(&rxrpc_transports_lock);
		_leave("");
		return;
	}

	list_del(&trans->link);
	spin_unlock(&rxrpc_transports_lock);

	/* finish cleaning up the transport */
	if (trans->socket)
		trans->socket->ops->shutdown(trans->socket, 2);

	rxrpc_krxsecd_clear_transport(trans);
	rxrpc_krxiod_dequeue_transport(trans);

	/* discard all peer information */
	rxrpc_peer_clearall(trans);

	down_write(&rxrpc_proc_transports_sem);
	list_del(&trans->proc_link);
	up_write(&rxrpc_proc_transports_sem);
	__RXACCT(atomic_dec(&rxrpc_transport_count));

	/* close the socket */
	if (trans->socket) {
		trans->socket->sk->sk_user_data = NULL;
		sock_release(trans->socket);
		trans->socket = NULL;
	}

	kfree(trans);

	_leave("");
} /* end rxrpc_put_transport() */

/*****************************************************************************/
/*
 * add a service to a transport to be listened upon
 */
int rxrpc_add_service(struct rxrpc_transport *trans,
		      struct rxrpc_service *newsrv)
{
	struct rxrpc_service *srv;
	struct list_head *_p;
	int ret = -EEXIST;

	_enter("%p{%hu},%p{%hu}",
	       trans, trans->port, newsrv, newsrv->service_id);

	/* verify that the service ID is not already present */
	spin_lock(&trans->lock);

	list_for_each(_p, &trans->services) {
		srv = list_entry(_p, struct rxrpc_service, link);
		if (srv->service_id == newsrv->service_id)
			goto out;
	}

	/* okay - add the transport to the list */
	list_add_tail(&newsrv->link, &trans->services);
	rxrpc_get_transport(trans);
	ret = 0;

 out:
	spin_unlock(&trans->lock);

	_leave("= %d", ret);
	return ret;
} /* end rxrpc_add_service() */

/*****************************************************************************/
/*
 * remove a service from a transport
 */
void rxrpc_del_service(struct rxrpc_transport *trans, struct rxrpc_service *srv)
{
	_enter("%p{%hu},%p{%hu}", trans, trans->port, srv, srv->service_id);

	spin_lock(&trans->lock);
	list_del(&srv->link);
	spin_unlock(&trans->lock);

	rxrpc_put_transport(trans);

	_leave("");
} /* end rxrpc_del_service() */

/*****************************************************************************/
/*
 * INET callback when data has been received on the socket.
 */
static void rxrpc_data_ready(struct sock *sk, int count)
{
	struct rxrpc_transport *trans;

	_enter("%p{t=%p},%d", sk, sk->sk_user_data, count);

	/* queue the transport for attention by krxiod */
	trans = (struct rxrpc_transport *) sk->sk_user_data;
	if (trans)
		rxrpc_krxiod_queue_transport(trans);

	/* wake up anyone waiting on the socket */
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);

	_leave("");
} /* end rxrpc_data_ready() */

/*****************************************************************************/
/*
 * INET callback when an ICMP error packet is received
 * - sk->err is error (EHOSTUNREACH, EPROTO or EMSGSIZE)
 */
static void rxrpc_error_report(struct sock *sk)
{
	struct rxrpc_transport *trans;

	_enter("%p{t=%p}", sk, sk->sk_user_data);

	/* queue the transport for attention by krxiod */
	trans = (struct rxrpc_transport *) sk->sk_user_data;
	if (trans) {
		trans->error_rcvd = 1;
		rxrpc_krxiod_queue_transport(trans);
	}

	/* wake up anyone waiting on the socket */
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);

	_leave("");
} /* end rxrpc_error_report() */

/*****************************************************************************/
/*
 * split a message up, allocating message records and filling them in
 * from the contents of a socket buffer
 */
static int rxrpc_incoming_msg(struct rxrpc_transport *trans,
			      struct sk_buff *pkt,
			      struct list_head *msgq)
{
	struct rxrpc_message *msg;
	int ret;

	_enter("");

	msg = kzalloc(sizeof(struct rxrpc_message), GFP_KERNEL);
	if (!msg) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	atomic_set(&msg->usage, 1);
	list_add_tail(&msg->link,msgq);

	/* dig out the Rx routing parameters */
	if (skb_copy_bits(pkt, sizeof(struct udphdr),
			  &msg->hdr, sizeof(msg->hdr)) < 0) {
		ret = -EBADMSG;
		goto error;
	}

	msg->trans = trans;
	msg->state = RXRPC_MSG_RECEIVED;
	skb_get_timestamp(pkt, &msg->stamp);
	if (msg->stamp.tv_sec == 0) {
		do_gettimeofday(&msg->stamp); 
		if (pkt->sk) 
			sock_enable_timestamp(pkt->sk);
	} 
	msg->seq = ntohl(msg->hdr.seq);

	/* attach the packet */
	skb_get(pkt);
	msg->pkt = pkt;

	msg->offset = sizeof(struct udphdr) + sizeof(struct rxrpc_header);
	msg->dsize = msg->pkt->len - msg->offset;

	_net("Rx Received packet from %s (%08x;%08x,%1x,%d,%s,%02x,%d,%d)",
	     msg->hdr.flags & RXRPC_CLIENT_INITIATED ? "client" : "server",
	     ntohl(msg->hdr.epoch),
	     (ntohl(msg->hdr.cid) & RXRPC_CIDMASK) >> RXRPC_CIDSHIFT,
	     ntohl(msg->hdr.cid) & RXRPC_CHANNELMASK,
	     ntohl(msg->hdr.callNumber),
	     rxrpc_pkts[msg->hdr.type],
	     msg->hdr.flags,
	     ntohs(msg->hdr.serviceId),
	     msg->hdr.securityIndex);

	__RXACCT(atomic_inc(&rxrpc_message_count));

	/* split off jumbo packets */
	while (msg->hdr.type == RXRPC_PACKET_TYPE_DATA &&
	       msg->hdr.flags & RXRPC_JUMBO_PACKET
	       ) {
		struct rxrpc_jumbo_header jumbo;
		struct rxrpc_message *jumbomsg = msg;

		_debug("split jumbo packet");

		/* quick sanity check */
		ret = -EBADMSG;
		if (msg->dsize <
		    RXRPC_JUMBO_DATALEN + sizeof(struct rxrpc_jumbo_header))
			goto error;
		if (msg->hdr.flags & RXRPC_LAST_PACKET)
			goto error;

		/* dig out the secondary header */
		if (skb_copy_bits(pkt, msg->offset + RXRPC_JUMBO_DATALEN,
				  &jumbo, sizeof(jumbo)) < 0)
			goto error;

		/* allocate a new message record */
		ret = -ENOMEM;
		msg = kmemdup(jumbomsg, sizeof(struct rxrpc_message), GFP_KERNEL);
		if (!msg)
			goto error;

		list_add_tail(&msg->link, msgq);

		/* adjust the jumbo packet */
		jumbomsg->dsize = RXRPC_JUMBO_DATALEN;

		/* attach the packet here too */
		skb_get(pkt);

		/* adjust the parameters */
		msg->seq++;
		msg->hdr.seq = htonl(msg->seq);
		msg->hdr.serial = htonl(ntohl(msg->hdr.serial) + 1);
		msg->offset += RXRPC_JUMBO_DATALEN +
			sizeof(struct rxrpc_jumbo_header);
		msg->dsize -= RXRPC_JUMBO_DATALEN +
			sizeof(struct rxrpc_jumbo_header);
		msg->hdr.flags = jumbo.flags;
		msg->hdr._rsvd = jumbo._rsvd;

		_net("Rx Split jumbo packet from %s"
		     " (%08x;%08x,%1x,%d,%s,%02x,%d,%d)",
		     msg->hdr.flags & RXRPC_CLIENT_INITIATED ? "client" : "server",
		     ntohl(msg->hdr.epoch),
		     (ntohl(msg->hdr.cid) & RXRPC_CIDMASK) >> RXRPC_CIDSHIFT,
		     ntohl(msg->hdr.cid) & RXRPC_CHANNELMASK,
		     ntohl(msg->hdr.callNumber),
		     rxrpc_pkts[msg->hdr.type],
		     msg->hdr.flags,
		     ntohs(msg->hdr.serviceId),
		     msg->hdr.securityIndex);

		__RXACCT(atomic_inc(&rxrpc_message_count));
	}

	_leave(" = 0 #%d", atomic_read(&rxrpc_message_count));
	return 0;

 error:
	while (!list_empty(msgq)) {
		msg = list_entry(msgq->next, struct rxrpc_message, link);
		list_del_init(&msg->link);

		rxrpc_put_message(msg);
	}

	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_incoming_msg() */

/*****************************************************************************/
/*
 * accept a new call
 * - called from krxiod in process context
 */
void rxrpc_trans_receive_packet(struct rxrpc_transport *trans)
{
	struct rxrpc_message *msg;
	struct rxrpc_peer *peer;
	struct sk_buff *pkt;
	int ret;
	__be32 addr;
	__be16 port;

	LIST_HEAD(msgq);

	_enter("%p{%d}", trans, trans->port);

	for (;;) {
		/* deal with outstanting errors first */
		if (trans->error_rcvd)
			rxrpc_trans_receive_error_report(trans);

		/* attempt to receive a packet */
		pkt = skb_recv_datagram(trans->socket->sk, 0, 1, &ret);
		if (!pkt) {
			if (ret == -EAGAIN) {
				_leave(" EAGAIN");
				return;
			}

			/* an icmp error may have occurred */
			rxrpc_krxiod_queue_transport(trans);
			_leave(" error %d\n", ret);
			return;
		}

		/* we'll probably need to checksum it (didn't call
		 * sock_recvmsg) */
		if (skb_checksum_complete(pkt)) {
			kfree_skb(pkt);
			rxrpc_krxiod_queue_transport(trans);
			_leave(" CSUM failed");
			return;
		}

		addr = pkt->nh.iph->saddr;
		port = pkt->h.uh->source;

		_net("Rx Received UDP packet from %08x:%04hu",
		     ntohl(addr), ntohs(port));

		/* unmarshall the Rx parameters and split jumbo packets */
		ret = rxrpc_incoming_msg(trans, pkt, &msgq);
		if (ret < 0) {
			kfree_skb(pkt);
			rxrpc_krxiod_queue_transport(trans);
			_leave(" bad packet");
			return;
		}

		BUG_ON(list_empty(&msgq));

		msg = list_entry(msgq.next, struct rxrpc_message, link);

		/* locate the record for the peer from which it
		 * originated */
		ret = rxrpc_peer_lookup(trans, addr, &peer);
		if (ret < 0) {
			kdebug("Rx No connections from that peer");
			rxrpc_trans_immediate_abort(trans, msg, -EINVAL);
			goto finished_msg;
		}

		/* try and find a matching connection */
		ret = rxrpc_connection_lookup(peer, msg, &msg->conn);
		if (ret < 0) {
			kdebug("Rx Unknown Connection");
			rxrpc_trans_immediate_abort(trans, msg, -EINVAL);
			rxrpc_put_peer(peer);
			goto finished_msg;
		}
		rxrpc_put_peer(peer);

		/* deal with the first packet of a new call */
		if (msg->hdr.flags & RXRPC_CLIENT_INITIATED &&
		    msg->hdr.type == RXRPC_PACKET_TYPE_DATA &&
		    ntohl(msg->hdr.seq) == 1
		    ) {
			_debug("Rx New server call");
			rxrpc_trans_receive_new_call(trans, &msgq);
			goto finished_msg;
		}

		/* deal with subsequent packet(s) of call */
		_debug("Rx Call packet");
		while (!list_empty(&msgq)) {
			msg = list_entry(msgq.next, struct rxrpc_message, link);
			list_del_init(&msg->link);

			ret = rxrpc_conn_receive_call_packet(msg->conn, NULL, msg);
			if (ret < 0) {
				rxrpc_trans_immediate_abort(trans, msg, ret);
				rxrpc_put_message(msg);
				goto finished_msg;
			}

			rxrpc_put_message(msg);
		}

		goto finished_msg;

		/* dispose of the packets */
	finished_msg:
		while (!list_empty(&msgq)) {
			msg = list_entry(msgq.next, struct rxrpc_message, link);
			list_del_init(&msg->link);

			rxrpc_put_message(msg);
		}
		kfree_skb(pkt);
	}

	_leave("");

} /* end rxrpc_trans_receive_packet() */

/*****************************************************************************/
/*
 * accept a new call from a client trying to connect to one of my services
 * - called in process context
 */
static int rxrpc_trans_receive_new_call(struct rxrpc_transport *trans,
					struct list_head *msgq)
{
	struct rxrpc_message *msg;

	_enter("");

	/* only bother with the first packet */
	msg = list_entry(msgq->next, struct rxrpc_message, link);
	list_del_init(&msg->link);
	rxrpc_krxsecd_queue_incoming_call(msg);
	rxrpc_put_message(msg);

	_leave(" = 0");

	return 0;
} /* end rxrpc_trans_receive_new_call() */

/*****************************************************************************/
/*
 * perform an immediate abort without connection or call structures
 */
int rxrpc_trans_immediate_abort(struct rxrpc_transport *trans,
				struct rxrpc_message *msg,
				int error)
{
	struct rxrpc_header ahdr;
	struct sockaddr_in sin;
	struct msghdr msghdr;
	struct kvec iov[2];
	__be32 _error;
	int len, ret;

	_enter("%p,%p,%d", trans, msg, error);

	/* don't abort an abort packet */
	if (msg->hdr.type == RXRPC_PACKET_TYPE_ABORT) {
		_leave(" = 0");
		return 0;
	}

	_error = htonl(-error);

	/* set up the message to be transmitted */
	memcpy(&ahdr, &msg->hdr, sizeof(ahdr));
	ahdr.epoch	= msg->hdr.epoch;
	ahdr.serial	= htonl(1);
	ahdr.seq	= 0;
	ahdr.type	= RXRPC_PACKET_TYPE_ABORT;
	ahdr.flags	= RXRPC_LAST_PACKET;
	ahdr.flags	|= ~msg->hdr.flags & RXRPC_CLIENT_INITIATED;

	iov[0].iov_len	= sizeof(ahdr);
	iov[0].iov_base	= &ahdr;
	iov[1].iov_len	= sizeof(_error);
	iov[1].iov_base	= &_error;

	len = sizeof(ahdr) + sizeof(_error);

	memset(&sin,0,sizeof(sin));
	sin.sin_family		= AF_INET;
	sin.sin_port		= msg->pkt->h.uh->source;
	sin.sin_addr.s_addr	= msg->pkt->nh.iph->saddr;

	msghdr.msg_name		= &sin;
	msghdr.msg_namelen	= sizeof(sin);
	msghdr.msg_control	= NULL;
	msghdr.msg_controllen	= 0;
	msghdr.msg_flags	= MSG_DONTWAIT;

	_net("Sending message type %d of %d bytes to %08x:%d",
	     ahdr.type,
	     len,
	     ntohl(sin.sin_addr.s_addr),
	     ntohs(sin.sin_port));

	/* send the message */
	ret = kernel_sendmsg(trans->socket, &msghdr, iov, 2, len);

	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_trans_immediate_abort() */

/*****************************************************************************/
/*
 * receive an ICMP error report and percolate it to all connections
 * heading to the affected host or port
 */
static void rxrpc_trans_receive_error_report(struct rxrpc_transport *trans)
{
	struct rxrpc_connection *conn;
	struct sockaddr_in sin;
	struct rxrpc_peer *peer;
	struct list_head connq, *_p;
	struct errormsg emsg;
	struct msghdr msg;
	__be16 port;
	int local, err;

	_enter("%p", trans);

	for (;;) {
		trans->error_rcvd = 0;

		/* try and receive an error message */
		msg.msg_name	= &sin;
		msg.msg_namelen	= sizeof(sin);
		msg.msg_control	= &emsg;
		msg.msg_controllen = sizeof(emsg);
		msg.msg_flags	= 0;

		err = kernel_recvmsg(trans->socket, &msg, NULL, 0, 0,
				   MSG_ERRQUEUE | MSG_DONTWAIT | MSG_TRUNC);

		if (err == -EAGAIN) {
			_leave("");
			return;
		}

		if (err < 0) {
			printk("%s: unable to recv an error report: %d\n",
			       __FUNCTION__, err);
			_leave("");
			return;
		}

		msg.msg_controllen = (char *) msg.msg_control - (char *) &emsg;

		if (msg.msg_controllen < sizeof(emsg.cmsg) ||
		    msg.msg_namelen < sizeof(sin)) {
			printk("%s: short control message"
			       " (nlen=%u clen=%Zu fl=%x)\n",
			       __FUNCTION__,
			       msg.msg_namelen,
			       msg.msg_controllen,
			       msg.msg_flags);
			continue;
		}

		_net("Rx Received control message"
		     " { len=%Zu level=%u type=%u }",
		     emsg.cmsg.cmsg_len,
		     emsg.cmsg.cmsg_level,
		     emsg.cmsg.cmsg_type);

		if (sin.sin_family != AF_INET) {
			printk("Rx Ignoring error report with non-INET address"
			       " (fam=%u)",
			       sin.sin_family);
			continue;
		}

		_net("Rx Received message pertaining to host addr=%x port=%hu",
		     ntohl(sin.sin_addr.s_addr), ntohs(sin.sin_port));

		if (emsg.cmsg.cmsg_level != SOL_IP ||
		    emsg.cmsg.cmsg_type != IP_RECVERR) {
			printk("Rx Ignoring unknown error report"
			       " { level=%u type=%u }",
			       emsg.cmsg.cmsg_level,
			       emsg.cmsg.cmsg_type);
			continue;
		}

		if (msg.msg_controllen < sizeof(emsg.cmsg) + sizeof(emsg.ee)) {
			printk("%s: short error message (%Zu)\n",
			       __FUNCTION__, msg.msg_controllen);
			_leave("");
			return;
		}

		port = sin.sin_port;

		switch (emsg.ee.ee_origin) {
		case SO_EE_ORIGIN_ICMP:
			local = 0;
			switch (emsg.ee.ee_type) {
			case ICMP_DEST_UNREACH:
				switch (emsg.ee.ee_code) {
				case ICMP_NET_UNREACH:
					_net("Rx Received ICMP Network Unreachable");
					port = 0;
					err = -ENETUNREACH;
					break;
				case ICMP_HOST_UNREACH:
					_net("Rx Received ICMP Host Unreachable");
					port = 0;
					err = -EHOSTUNREACH;
					break;
				case ICMP_PORT_UNREACH:
					_net("Rx Received ICMP Port Unreachable");
					err = -ECONNREFUSED;
					break;
				case ICMP_NET_UNKNOWN:
					_net("Rx Received ICMP Unknown Network");
					port = 0;
					err = -ENETUNREACH;
					break;
				case ICMP_HOST_UNKNOWN:
					_net("Rx Received ICMP Unknown Host");
					port = 0;
					err = -EHOSTUNREACH;
					break;
				default:
					_net("Rx Received ICMP DestUnreach { code=%u }",
					     emsg.ee.ee_code);
					err = emsg.ee.ee_errno;
					break;
				}
				break;

			case ICMP_TIME_EXCEEDED:
				_net("Rx Received ICMP TTL Exceeded");
				err = emsg.ee.ee_errno;
				break;

			default:
				_proto("Rx Received ICMP error { type=%u code=%u }",
				       emsg.ee.ee_type, emsg.ee.ee_code);
				err = emsg.ee.ee_errno;
				break;
			}
			break;

		case SO_EE_ORIGIN_LOCAL:
			_proto("Rx Received local error { error=%d }",
			       emsg.ee.ee_errno);
			local = 1;
			err = emsg.ee.ee_errno;
			break;

		case SO_EE_ORIGIN_NONE:
		case SO_EE_ORIGIN_ICMP6:
		default:
			_proto("Rx Received error report { orig=%u }",
			       emsg.ee.ee_origin);
			local = 0;
			err = emsg.ee.ee_errno;
			break;
		}

		/* find all the connections between this transport and the
		 * affected destination */
		INIT_LIST_HEAD(&connq);

		if (rxrpc_peer_lookup(trans, sin.sin_addr.s_addr,
				      &peer) == 0) {
			read_lock(&peer->conn_lock);
			list_for_each(_p, &peer->conn_active) {
				conn = list_entry(_p, struct rxrpc_connection,
						  link);
				if (port && conn->addr.sin_port != port)
					continue;
				if (!list_empty(&conn->err_link))
					continue;

				rxrpc_get_connection(conn);
				list_add_tail(&conn->err_link, &connq);
			}
			read_unlock(&peer->conn_lock);

			/* service all those connections */
			while (!list_empty(&connq)) {
				conn = list_entry(connq.next,
						  struct rxrpc_connection,
						  err_link);
				list_del(&conn->err_link);

				rxrpc_conn_handle_error(conn, local, err);

				rxrpc_put_connection(conn);
			}

			rxrpc_put_peer(peer);
		}
	}

	_leave("");
	return;
} /* end rxrpc_trans_receive_error_report() */
