/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/qrtr.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */

#include <net/sock.h>

#include "qrtr.h"

#define QRTR_PROTO_VER 1

/* auto-bind range */
#define QRTR_MIN_EPH_SOCKET 0x4000
#define QRTR_MAX_EPH_SOCKET 0x7fff

enum qrtr_pkt_type {
	QRTR_TYPE_DATA		= 1,
	QRTR_TYPE_HELLO		= 2,
	QRTR_TYPE_BYE		= 3,
	QRTR_TYPE_NEW_SERVER	= 4,
	QRTR_TYPE_DEL_SERVER	= 5,
	QRTR_TYPE_DEL_CLIENT	= 6,
	QRTR_TYPE_RESUME_TX	= 7,
	QRTR_TYPE_EXIT		= 8,
	QRTR_TYPE_PING		= 9,
};

/**
 * struct qrtr_hdr - (I|R)PCrouter packet header
 * @version: protocol version
 * @type: packet type; one of QRTR_TYPE_*
 * @src_node_id: source node
 * @src_port_id: source port
 * @confirm_rx: boolean; whether a resume-tx packet should be send in reply
 * @size: length of packet, excluding this header
 * @dst_node_id: destination node
 * @dst_port_id: destination port
 */
struct qrtr_hdr {
	__le32 version;
	__le32 type;
	__le32 src_node_id;
	__le32 src_port_id;
	__le32 confirm_rx;
	__le32 size;
	__le32 dst_node_id;
	__le32 dst_port_id;
} __packed;

#define QRTR_HDR_SIZE sizeof(struct qrtr_hdr)
#define QRTR_NODE_BCAST ((unsigned int)-1)
#define QRTR_PORT_CTRL ((unsigned int)-2)

struct qrtr_sock {
	/* WARNING: sk must be the first member */
	struct sock sk;
	struct sockaddr_qrtr us;
	struct sockaddr_qrtr peer;
};

static inline struct qrtr_sock *qrtr_sk(struct sock *sk)
{
	BUILD_BUG_ON(offsetof(struct qrtr_sock, sk) != 0);
	return container_of(sk, struct qrtr_sock, sk);
}

static unsigned int qrtr_local_nid = -1;

/* for node ids */
static RADIX_TREE(qrtr_nodes, GFP_KERNEL);
/* broadcast list */
static LIST_HEAD(qrtr_all_nodes);
/* lock for qrtr_nodes, qrtr_all_nodes and node reference */
static DEFINE_MUTEX(qrtr_node_lock);

/* local port allocation management */
static DEFINE_IDR(qrtr_ports);
static DEFINE_MUTEX(qrtr_port_lock);

/**
 * struct qrtr_node - endpoint node
 * @ep_lock: lock for endpoint management and callbacks
 * @ep: endpoint
 * @ref: reference count for node
 * @nid: node id
 * @rx_queue: receive queue
 * @work: scheduled work struct for recv work
 * @item: list item for broadcast list
 */
struct qrtr_node {
	struct mutex ep_lock;
	struct qrtr_endpoint *ep;
	struct kref ref;
	unsigned int nid;

	struct sk_buff_head rx_queue;
	struct work_struct work;
	struct list_head item;
};

/* Release node resources and free the node.
 *
 * Do not call directly, use qrtr_node_release.  To be used with
 * kref_put_mutex.  As such, the node mutex is expected to be locked on call.
 */
static void __qrtr_node_release(struct kref *kref)
{
	struct qrtr_node *node = container_of(kref, struct qrtr_node, ref);

	if (node->nid != QRTR_EP_NID_AUTO)
		radix_tree_delete(&qrtr_nodes, node->nid);

	list_del(&node->item);
	mutex_unlock(&qrtr_node_lock);

	skb_queue_purge(&node->rx_queue);
	kfree(node);
}

/* Increment reference to node. */
static struct qrtr_node *qrtr_node_acquire(struct qrtr_node *node)
{
	if (node)
		kref_get(&node->ref);
	return node;
}

/* Decrement reference to node and release as necessary. */
static void qrtr_node_release(struct qrtr_node *node)
{
	if (!node)
		return;
	kref_put_mutex(&node->ref, __qrtr_node_release, &qrtr_node_lock);
}

/* Pass an outgoing packet socket buffer to the endpoint driver. */
static int qrtr_node_enqueue(struct qrtr_node *node, struct sk_buff *skb)
{
	int rc = -ENODEV;

	mutex_lock(&node->ep_lock);
	if (node->ep)
		rc = node->ep->xmit(node->ep, skb);
	else
		kfree_skb(skb);
	mutex_unlock(&node->ep_lock);

	return rc;
}

/* Lookup node by id.
 *
 * callers must release with qrtr_node_release()
 */
static struct qrtr_node *qrtr_node_lookup(unsigned int nid)
{
	struct qrtr_node *node;

	mutex_lock(&qrtr_node_lock);
	node = radix_tree_lookup(&qrtr_nodes, nid);
	node = qrtr_node_acquire(node);
	mutex_unlock(&qrtr_node_lock);

	return node;
}

/* Assign node id to node.
 *
 * This is mostly useful for automatic node id assignment, based on
 * the source id in the incoming packet.
 */
static void qrtr_node_assign(struct qrtr_node *node, unsigned int nid)
{
	if (node->nid != QRTR_EP_NID_AUTO || nid == QRTR_EP_NID_AUTO)
		return;

	mutex_lock(&qrtr_node_lock);
	radix_tree_insert(&qrtr_nodes, nid, node);
	node->nid = nid;
	mutex_unlock(&qrtr_node_lock);
}

/**
 * qrtr_endpoint_post() - post incoming data
 * @ep: endpoint handle
 * @data: data pointer
 * @len: size of data in bytes
 *
 * Return: 0 on success; negative error code on failure
 */
int qrtr_endpoint_post(struct qrtr_endpoint *ep, const void *data, size_t len)
{
	struct qrtr_node *node = ep->node;
	const struct qrtr_hdr *phdr = data;
	struct sk_buff *skb;
	unsigned int psize;
	unsigned int size;
	unsigned int type;
	unsigned int ver;
	unsigned int dst;

	if (len < QRTR_HDR_SIZE || len & 3)
		return -EINVAL;

	ver = le32_to_cpu(phdr->version);
	size = le32_to_cpu(phdr->size);
	type = le32_to_cpu(phdr->type);
	dst = le32_to_cpu(phdr->dst_port_id);

	psize = (size + 3) & ~3;

	if (ver != QRTR_PROTO_VER)
		return -EINVAL;

	if (len != psize + QRTR_HDR_SIZE)
		return -EINVAL;

	if (dst != QRTR_PORT_CTRL && type != QRTR_TYPE_DATA)
		return -EINVAL;

	skb = netdev_alloc_skb(NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_reset_transport_header(skb);
	memcpy(skb_put(skb, len), data, len);

	skb_queue_tail(&node->rx_queue, skb);
	schedule_work(&node->work);

	return 0;
}
EXPORT_SYMBOL_GPL(qrtr_endpoint_post);

/* Allocate and construct a resume-tx packet. */
static struct sk_buff *qrtr_alloc_resume_tx(u32 src_node,
					    u32 dst_node, u32 port)
{
	const int pkt_len = 20;
	struct qrtr_hdr *hdr;
	struct sk_buff *skb;
	__le32 *buf;

	skb = alloc_skb(QRTR_HDR_SIZE + pkt_len, GFP_KERNEL);
	if (!skb)
		return NULL;
	skb_reset_transport_header(skb);

	hdr = (struct qrtr_hdr *)skb_put(skb, QRTR_HDR_SIZE);
	hdr->version = cpu_to_le32(QRTR_PROTO_VER);
	hdr->type = cpu_to_le32(QRTR_TYPE_RESUME_TX);
	hdr->src_node_id = cpu_to_le32(src_node);
	hdr->src_port_id = cpu_to_le32(QRTR_PORT_CTRL);
	hdr->confirm_rx = cpu_to_le32(0);
	hdr->size = cpu_to_le32(pkt_len);
	hdr->dst_node_id = cpu_to_le32(dst_node);
	hdr->dst_port_id = cpu_to_le32(QRTR_PORT_CTRL);

	buf = (__le32 *)skb_put(skb, pkt_len);
	memset(buf, 0, pkt_len);
	buf[0] = cpu_to_le32(QRTR_TYPE_RESUME_TX);
	buf[1] = cpu_to_le32(src_node);
	buf[2] = cpu_to_le32(port);

	return skb;
}

static struct qrtr_sock *qrtr_port_lookup(int port);
static void qrtr_port_put(struct qrtr_sock *ipc);

/* Handle and route a received packet.
 *
 * This will auto-reply with resume-tx packet as necessary.
 */
static void qrtr_node_rx_work(struct work_struct *work)
{
	struct qrtr_node *node = container_of(work, struct qrtr_node, work);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&node->rx_queue)) != NULL) {
		const struct qrtr_hdr *phdr;
		u32 dst_node, dst_port;
		struct qrtr_sock *ipc;
		u32 src_node;
		int confirm;

		phdr = (const struct qrtr_hdr *)skb_transport_header(skb);
		src_node = le32_to_cpu(phdr->src_node_id);
		dst_node = le32_to_cpu(phdr->dst_node_id);
		dst_port = le32_to_cpu(phdr->dst_port_id);
		confirm = !!phdr->confirm_rx;

		qrtr_node_assign(node, src_node);

		ipc = qrtr_port_lookup(dst_port);
		if (!ipc) {
			kfree_skb(skb);
		} else {
			if (sock_queue_rcv_skb(&ipc->sk, skb))
				kfree_skb(skb);

			qrtr_port_put(ipc);
		}

		if (confirm) {
			skb = qrtr_alloc_resume_tx(dst_node, node->nid, dst_port);
			if (!skb)
				break;
			if (qrtr_node_enqueue(node, skb))
				break;
		}
	}
}

/**
 * qrtr_endpoint_register() - register a new endpoint
 * @ep: endpoint to register
 * @nid: desired node id; may be QRTR_EP_NID_AUTO for auto-assignment
 * Return: 0 on success; negative error code on failure
 *
 * The specified endpoint must have the xmit function pointer set on call.
 */
int qrtr_endpoint_register(struct qrtr_endpoint *ep, unsigned int nid)
{
	struct qrtr_node *node;

	if (!ep || !ep->xmit)
		return -EINVAL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	INIT_WORK(&node->work, qrtr_node_rx_work);
	kref_init(&node->ref);
	mutex_init(&node->ep_lock);
	skb_queue_head_init(&node->rx_queue);
	node->nid = QRTR_EP_NID_AUTO;
	node->ep = ep;

	qrtr_node_assign(node, nid);

	mutex_lock(&qrtr_node_lock);
	list_add(&node->item, &qrtr_all_nodes);
	mutex_unlock(&qrtr_node_lock);
	ep->node = node;

	return 0;
}
EXPORT_SYMBOL_GPL(qrtr_endpoint_register);

/**
 * qrtr_endpoint_unregister - unregister endpoint
 * @ep: endpoint to unregister
 */
void qrtr_endpoint_unregister(struct qrtr_endpoint *ep)
{
	struct qrtr_node *node = ep->node;

	mutex_lock(&node->ep_lock);
	node->ep = NULL;
	mutex_unlock(&node->ep_lock);

	qrtr_node_release(node);
	ep->node = NULL;
}
EXPORT_SYMBOL_GPL(qrtr_endpoint_unregister);

/* Lookup socket by port.
 *
 * Callers must release with qrtr_port_put()
 */
static struct qrtr_sock *qrtr_port_lookup(int port)
{
	struct qrtr_sock *ipc;

	if (port == QRTR_PORT_CTRL)
		port = 0;

	mutex_lock(&qrtr_port_lock);
	ipc = idr_find(&qrtr_ports, port);
	if (ipc)
		sock_hold(&ipc->sk);
	mutex_unlock(&qrtr_port_lock);

	return ipc;
}

/* Release acquired socket. */
static void qrtr_port_put(struct qrtr_sock *ipc)
{
	sock_put(&ipc->sk);
}

/* Remove port assignment. */
static void qrtr_port_remove(struct qrtr_sock *ipc)
{
	int port = ipc->us.sq_port;

	if (port == QRTR_PORT_CTRL)
		port = 0;

	__sock_put(&ipc->sk);

	mutex_lock(&qrtr_port_lock);
	idr_remove(&qrtr_ports, port);
	mutex_unlock(&qrtr_port_lock);
}

/* Assign port number to socket.
 *
 * Specify port in the integer pointed to by port, and it will be adjusted
 * on return as necesssary.
 *
 * Port may be:
 *   0: Assign ephemeral port in [QRTR_MIN_EPH_SOCKET, QRTR_MAX_EPH_SOCKET]
 *   <QRTR_MIN_EPH_SOCKET: Specified; requires CAP_NET_ADMIN
 *   >QRTR_MIN_EPH_SOCKET: Specified; available to all
 */
static int qrtr_port_assign(struct qrtr_sock *ipc, int *port)
{
	int rc;

	mutex_lock(&qrtr_port_lock);
	if (!*port) {
		rc = idr_alloc(&qrtr_ports, ipc,
			       QRTR_MIN_EPH_SOCKET, QRTR_MAX_EPH_SOCKET + 1,
			       GFP_ATOMIC);
		if (rc >= 0)
			*port = rc;
	} else if (*port < QRTR_MIN_EPH_SOCKET && !capable(CAP_NET_ADMIN)) {
		rc = -EACCES;
	} else if (*port == QRTR_PORT_CTRL) {
		rc = idr_alloc(&qrtr_ports, ipc, 0, 1, GFP_ATOMIC);
	} else {
		rc = idr_alloc(&qrtr_ports, ipc, *port, *port + 1, GFP_ATOMIC);
		if (rc >= 0)
			*port = rc;
	}
	mutex_unlock(&qrtr_port_lock);

	if (rc == -ENOSPC)
		return -EADDRINUSE;
	else if (rc < 0)
		return rc;

	sock_hold(&ipc->sk);

	return 0;
}

/* Bind socket to address.
 *
 * Socket should be locked upon call.
 */
static int __qrtr_bind(struct socket *sock,
		       const struct sockaddr_qrtr *addr, int zapped)
{
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	int port;
	int rc;

	/* rebinding ok */
	if (!zapped && addr->sq_port == ipc->us.sq_port)
		return 0;

	port = addr->sq_port;
	rc = qrtr_port_assign(ipc, &port);
	if (rc)
		return rc;

	/* unbind previous, if any */
	if (!zapped)
		qrtr_port_remove(ipc);
	ipc->us.sq_port = port;

	sock_reset_flag(sk, SOCK_ZAPPED);

	return 0;
}

/* Auto bind to an ephemeral port. */
static int qrtr_autobind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct sockaddr_qrtr addr;

	if (!sock_flag(sk, SOCK_ZAPPED))
		return 0;

	addr.sq_family = AF_QIPCRTR;
	addr.sq_node = qrtr_local_nid;
	addr.sq_port = 0;

	return __qrtr_bind(sock, &addr, 1);
}

/* Bind socket to specified sockaddr. */
static int qrtr_bind(struct socket *sock, struct sockaddr *saddr, int len)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, saddr);
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	int rc;

	if (len < sizeof(*addr) || addr->sq_family != AF_QIPCRTR)
		return -EINVAL;

	if (addr->sq_node != ipc->us.sq_node)
		return -EINVAL;

	lock_sock(sk);
	rc = __qrtr_bind(sock, addr, sock_flag(sk, SOCK_ZAPPED));
	release_sock(sk);

	return rc;
}

/* Queue packet to local peer socket. */
static int qrtr_local_enqueue(struct qrtr_node *node, struct sk_buff *skb)
{
	const struct qrtr_hdr *phdr;
	struct qrtr_sock *ipc;

	phdr = (const struct qrtr_hdr *)skb_transport_header(skb);

	ipc = qrtr_port_lookup(le32_to_cpu(phdr->dst_port_id));
	if (!ipc || &ipc->sk == skb->sk) { /* do not send to self */
		kfree_skb(skb);
		return -ENODEV;
	}

	if (sock_queue_rcv_skb(&ipc->sk, skb)) {
		qrtr_port_put(ipc);
		kfree_skb(skb);
		return -ENOSPC;
	}

	qrtr_port_put(ipc);

	return 0;
}

/* Queue packet for broadcast. */
static int qrtr_bcast_enqueue(struct qrtr_node *node, struct sk_buff *skb)
{
	struct sk_buff *skbn;

	mutex_lock(&qrtr_node_lock);
	list_for_each_entry(node, &qrtr_all_nodes, item) {
		skbn = skb_clone(skb, GFP_KERNEL);
		if (!skbn)
			break;
		skb_set_owner_w(skbn, skb->sk);
		qrtr_node_enqueue(node, skbn);
	}
	mutex_unlock(&qrtr_node_lock);

	qrtr_local_enqueue(node, skb);

	return 0;
}

static int qrtr_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	int (*enqueue_fn)(struct qrtr_node *, struct sk_buff *);
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	struct qrtr_node *node;
	struct qrtr_hdr *hdr;
	struct sk_buff *skb;
	size_t plen;
	int rc;

	if (msg->msg_flags & ~(MSG_DONTWAIT))
		return -EINVAL;

	if (len > 65535)
		return -EMSGSIZE;

	lock_sock(sk);

	if (addr) {
		if (msg->msg_namelen < sizeof(*addr)) {
			release_sock(sk);
			return -EINVAL;
		}

		if (addr->sq_family != AF_QIPCRTR) {
			release_sock(sk);
			return -EINVAL;
		}

		rc = qrtr_autobind(sock);
		if (rc) {
			release_sock(sk);
			return rc;
		}
	} else if (sk->sk_state == TCP_ESTABLISHED) {
		addr = &ipc->peer;
	} else {
		release_sock(sk);
		return -ENOTCONN;
	}

	node = NULL;
	if (addr->sq_node == QRTR_NODE_BCAST) {
		enqueue_fn = qrtr_bcast_enqueue;
	} else if (addr->sq_node == ipc->us.sq_node) {
		enqueue_fn = qrtr_local_enqueue;
	} else {
		enqueue_fn = qrtr_node_enqueue;
		node = qrtr_node_lookup(addr->sq_node);
		if (!node) {
			release_sock(sk);
			return -ECONNRESET;
		}
	}

	plen = (len + 3) & ~3;
	skb = sock_alloc_send_skb(sk, plen + QRTR_HDR_SIZE,
				  msg->msg_flags & MSG_DONTWAIT, &rc);
	if (!skb)
		goto out_node;

	skb_reset_transport_header(skb);
	skb_put(skb, len + QRTR_HDR_SIZE);

	hdr = (struct qrtr_hdr *)skb_transport_header(skb);
	hdr->version = cpu_to_le32(QRTR_PROTO_VER);
	hdr->src_node_id = cpu_to_le32(ipc->us.sq_node);
	hdr->src_port_id = cpu_to_le32(ipc->us.sq_port);
	hdr->confirm_rx = cpu_to_le32(0);
	hdr->size = cpu_to_le32(len);
	hdr->dst_node_id = cpu_to_le32(addr->sq_node);
	hdr->dst_port_id = cpu_to_le32(addr->sq_port);

	rc = skb_copy_datagram_from_iter(skb, QRTR_HDR_SIZE,
					 &msg->msg_iter, len);
	if (rc) {
		kfree_skb(skb);
		goto out_node;
	}

	if (plen != len) {
		rc = skb_pad(skb, plen - len);
		if (rc)
			goto out_node;
		skb_put(skb, plen - len);
	}

	if (ipc->us.sq_port == QRTR_PORT_CTRL) {
		if (len < 4) {
			rc = -EINVAL;
			kfree_skb(skb);
			goto out_node;
		}

		/* control messages already require the type as 'command' */
		skb_copy_bits(skb, QRTR_HDR_SIZE, &hdr->type, 4);
	} else {
		hdr->type = cpu_to_le32(QRTR_TYPE_DATA);
	}

	rc = enqueue_fn(node, skb);
	if (rc >= 0)
		rc = len;

out_node:
	qrtr_node_release(node);
	release_sock(sk);

	return rc;
}

static int qrtr_recvmsg(struct socket *sock, struct msghdr *msg,
			size_t size, int flags)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	const struct qrtr_hdr *phdr;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, rc;

	lock_sock(sk);

	if (sock_flag(sk, SOCK_ZAPPED)) {
		release_sock(sk);
		return -EADDRNOTAVAIL;
	}

	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &rc);
	if (!skb) {
		release_sock(sk);
		return rc;
	}

	phdr = (const struct qrtr_hdr *)skb_transport_header(skb);
	copied = le32_to_cpu(phdr->size);
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	rc = skb_copy_datagram_msg(skb, QRTR_HDR_SIZE, msg, copied);
	if (rc < 0)
		goto out;
	rc = copied;

	if (addr) {
		addr->sq_family = AF_QIPCRTR;
		addr->sq_node = le32_to_cpu(phdr->src_node_id);
		addr->sq_port = le32_to_cpu(phdr->src_port_id);
		msg->msg_namelen = sizeof(*addr);
	}

out:
	skb_free_datagram(sk, skb);
	release_sock(sk);

	return rc;
}

static int qrtr_connect(struct socket *sock, struct sockaddr *saddr,
			int len, int flags)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, saddr);
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	int rc;

	if (len < sizeof(*addr) || addr->sq_family != AF_QIPCRTR)
		return -EINVAL;

	lock_sock(sk);

	sk->sk_state = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	rc = qrtr_autobind(sock);
	if (rc) {
		release_sock(sk);
		return rc;
	}

	ipc->peer = *addr;
	sock->state = SS_CONNECTED;
	sk->sk_state = TCP_ESTABLISHED;

	release_sock(sk);

	return 0;
}

static int qrtr_getname(struct socket *sock, struct sockaddr *saddr,
			int *len, int peer)
{
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sockaddr_qrtr qaddr;
	struct sock *sk = sock->sk;

	lock_sock(sk);
	if (peer) {
		if (sk->sk_state != TCP_ESTABLISHED) {
			release_sock(sk);
			return -ENOTCONN;
		}

		qaddr = ipc->peer;
	} else {
		qaddr = ipc->us;
	}
	release_sock(sk);

	*len = sizeof(qaddr);
	qaddr.sq_family = AF_QIPCRTR;

	memcpy(saddr, &qaddr, sizeof(qaddr));

	return 0;
}

static int qrtr_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	struct sockaddr_qrtr *sq;
	struct sk_buff *skb;
	struct ifreq ifr;
	long len = 0;
	int rc = 0;

	lock_sock(sk);

	switch (cmd) {
	case TIOCOUTQ:
		len = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (len < 0)
			len = 0;
		rc = put_user(len, (int __user *)argp);
		break;
	case TIOCINQ:
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb)
			len = skb->len - QRTR_HDR_SIZE;
		rc = put_user(len, (int __user *)argp);
		break;
	case SIOCGIFADDR:
		if (copy_from_user(&ifr, argp, sizeof(ifr))) {
			rc = -EFAULT;
			break;
		}

		sq = (struct sockaddr_qrtr *)&ifr.ifr_addr;
		*sq = ipc->us;
		if (copy_to_user(argp, &ifr, sizeof(ifr))) {
			rc = -EFAULT;
			break;
		}
		break;
	case SIOCGSTAMP:
		rc = sock_get_timestamp(sk, argp);
		break;
	case SIOCADDRT:
	case SIOCDELRT:
	case SIOCSIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
		rc = -EINVAL;
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	release_sock(sk);

	return rc;
}

static int qrtr_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct qrtr_sock *ipc;

	if (!sk)
		return 0;

	lock_sock(sk);

	ipc = qrtr_sk(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);

	sock_set_flag(sk, SOCK_DEAD);
	sock->sk = NULL;

	if (!sock_flag(sk, SOCK_ZAPPED))
		qrtr_port_remove(ipc);

	skb_queue_purge(&sk->sk_receive_queue);

	release_sock(sk);
	sock_put(sk);

	return 0;
}

static const struct proto_ops qrtr_proto_ops = {
	.owner		= THIS_MODULE,
	.family		= AF_QIPCRTR,
	.bind		= qrtr_bind,
	.connect	= qrtr_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.listen		= sock_no_listen,
	.sendmsg	= qrtr_sendmsg,
	.recvmsg	= qrtr_recvmsg,
	.getname	= qrtr_getname,
	.ioctl		= qrtr_ioctl,
	.poll		= datagram_poll,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.release	= qrtr_release,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

static struct proto qrtr_proto = {
	.name		= "QIPCRTR",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct qrtr_sock),
};

static int qrtr_create(struct net *net, struct socket *sock,
		       int protocol, int kern)
{
	struct qrtr_sock *ipc;
	struct sock *sk;

	if (sock->type != SOCK_DGRAM)
		return -EPROTOTYPE;

	sk = sk_alloc(net, AF_QIPCRTR, GFP_KERNEL, &qrtr_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_set_flag(sk, SOCK_ZAPPED);

	sock_init_data(sock, sk);
	sock->ops = &qrtr_proto_ops;

	ipc = qrtr_sk(sk);
	ipc->us.sq_family = AF_QIPCRTR;
	ipc->us.sq_node = qrtr_local_nid;
	ipc->us.sq_port = 0;

	return 0;
}

static const struct nla_policy qrtr_policy[IFA_MAX + 1] = {
	[IFA_LOCAL] = { .type = NLA_U32 },
};

static int qrtr_addr_doit(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct nlattr *tb[IFA_MAX + 1];
	struct ifaddrmsg *ifm;
	int rc;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!netlink_capable(skb, CAP_SYS_ADMIN))
		return -EPERM;

	ASSERT_RTNL();

	rc = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, qrtr_policy);
	if (rc < 0)
		return rc;

	ifm = nlmsg_data(nlh);
	if (!tb[IFA_LOCAL])
		return -EINVAL;

	qrtr_local_nid = nla_get_u32(tb[IFA_LOCAL]);
	return 0;
}

static const struct net_proto_family qrtr_family = {
	.owner	= THIS_MODULE,
	.family	= AF_QIPCRTR,
	.create	= qrtr_create,
};

static int __init qrtr_proto_init(void)
{
	int rc;

	rc = proto_register(&qrtr_proto, 1);
	if (rc)
		return rc;

	rc = sock_register(&qrtr_family);
	if (rc) {
		proto_unregister(&qrtr_proto);
		return rc;
	}

	rtnl_register(PF_QIPCRTR, RTM_NEWADDR, qrtr_addr_doit, NULL, NULL);

	return 0;
}
module_init(qrtr_proto_init);

static void __exit qrtr_proto_fini(void)
{
	rtnl_unregister(PF_QIPCRTR, RTM_NEWADDR);
	sock_unregister(qrtr_family.family);
	proto_unregister(&qrtr_proto);
}
module_exit(qrtr_proto_fini);

MODULE_DESCRIPTION("Qualcomm IPC-router driver");
MODULE_LICENSE("GPL v2");
