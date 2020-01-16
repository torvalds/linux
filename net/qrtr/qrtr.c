// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/qrtr.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/numa.h>

#include <net/sock.h>

#include "qrtr.h"

#define QRTR_PROTO_VER_1 1
#define QRTR_PROTO_VER_2 3

/* auto-bind range */
#define QRTR_MIN_EPH_SOCKET 0x4000
#define QRTR_MAX_EPH_SOCKET 0x7fff

/**
 * struct qrtr_hdr_v1 - (I|R)PCrouter packet header version 1
 * @version: protocol version
 * @type: packet type; one of QRTR_TYPE_*
 * @src_yesde_id: source yesde
 * @src_port_id: source port
 * @confirm_rx: boolean; whether a resume-tx packet should be send in reply
 * @size: length of packet, excluding this header
 * @dst_yesde_id: destination yesde
 * @dst_port_id: destination port
 */
struct qrtr_hdr_v1 {
	__le32 version;
	__le32 type;
	__le32 src_yesde_id;
	__le32 src_port_id;
	__le32 confirm_rx;
	__le32 size;
	__le32 dst_yesde_id;
	__le32 dst_port_id;
} __packed;

/**
 * struct qrtr_hdr_v2 - (I|R)PCrouter packet header later versions
 * @version: protocol version
 * @type: packet type; one of QRTR_TYPE_*
 * @flags: bitmask of QRTR_FLAGS_*
 * @optlen: length of optional header data
 * @size: length of packet, excluding this header and optlen
 * @src_yesde_id: source yesde
 * @src_port_id: source port
 * @dst_yesde_id: destination yesde
 * @dst_port_id: destination port
 */
struct qrtr_hdr_v2 {
	u8 version;
	u8 type;
	u8 flags;
	u8 optlen;
	__le32 size;
	__le16 src_yesde_id;
	__le16 src_port_id;
	__le16 dst_yesde_id;
	__le16 dst_port_id;
};

#define QRTR_FLAGS_CONFIRM_RX	BIT(0)

struct qrtr_cb {
	u32 src_yesde;
	u32 src_port;
	u32 dst_yesde;
	u32 dst_port;

	u8 type;
	u8 confirm_rx;
};

#define QRTR_HDR_MAX_SIZE max_t(size_t, sizeof(struct qrtr_hdr_v1), \
					sizeof(struct qrtr_hdr_v2))

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

static unsigned int qrtr_local_nid = NUMA_NO_NODE;

/* for yesde ids */
static RADIX_TREE(qrtr_yesdes, GFP_KERNEL);
/* broadcast list */
static LIST_HEAD(qrtr_all_yesdes);
/* lock for qrtr_yesdes, qrtr_all_yesdes and yesde reference */
static DEFINE_MUTEX(qrtr_yesde_lock);

/* local port allocation management */
static DEFINE_IDR(qrtr_ports);
static DEFINE_MUTEX(qrtr_port_lock);

/**
 * struct qrtr_yesde - endpoint yesde
 * @ep_lock: lock for endpoint management and callbacks
 * @ep: endpoint
 * @ref: reference count for yesde
 * @nid: yesde id
 * @rx_queue: receive queue
 * @work: scheduled work struct for recv work
 * @item: list item for broadcast list
 */
struct qrtr_yesde {
	struct mutex ep_lock;
	struct qrtr_endpoint *ep;
	struct kref ref;
	unsigned int nid;

	struct sk_buff_head rx_queue;
	struct work_struct work;
	struct list_head item;
};

static int qrtr_local_enqueue(struct qrtr_yesde *yesde, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to);
static int qrtr_bcast_enqueue(struct qrtr_yesde *yesde, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to);

/* Release yesde resources and free the yesde.
 *
 * Do yest call directly, use qrtr_yesde_release.  To be used with
 * kref_put_mutex.  As such, the yesde mutex is expected to be locked on call.
 */
static void __qrtr_yesde_release(struct kref *kref)
{
	struct qrtr_yesde *yesde = container_of(kref, struct qrtr_yesde, ref);

	if (yesde->nid != QRTR_EP_NID_AUTO)
		radix_tree_delete(&qrtr_yesdes, yesde->nid);

	list_del(&yesde->item);
	mutex_unlock(&qrtr_yesde_lock);

	cancel_work_sync(&yesde->work);
	skb_queue_purge(&yesde->rx_queue);
	kfree(yesde);
}

/* Increment reference to yesde. */
static struct qrtr_yesde *qrtr_yesde_acquire(struct qrtr_yesde *yesde)
{
	if (yesde)
		kref_get(&yesde->ref);
	return yesde;
}

/* Decrement reference to yesde and release as necessary. */
static void qrtr_yesde_release(struct qrtr_yesde *yesde)
{
	if (!yesde)
		return;
	kref_put_mutex(&yesde->ref, __qrtr_yesde_release, &qrtr_yesde_lock);
}

/* Pass an outgoing packet socket buffer to the endpoint driver. */
static int qrtr_yesde_enqueue(struct qrtr_yesde *yesde, struct sk_buff *skb,
			     int type, struct sockaddr_qrtr *from,
			     struct sockaddr_qrtr *to)
{
	struct qrtr_hdr_v1 *hdr;
	size_t len = skb->len;
	int rc = -ENODEV;

	hdr = skb_push(skb, sizeof(*hdr));
	hdr->version = cpu_to_le32(QRTR_PROTO_VER_1);
	hdr->type = cpu_to_le32(type);
	hdr->src_yesde_id = cpu_to_le32(from->sq_yesde);
	hdr->src_port_id = cpu_to_le32(from->sq_port);
	if (to->sq_port == QRTR_PORT_CTRL) {
		hdr->dst_yesde_id = cpu_to_le32(yesde->nid);
		hdr->dst_port_id = cpu_to_le32(QRTR_NODE_BCAST);
	} else {
		hdr->dst_yesde_id = cpu_to_le32(to->sq_yesde);
		hdr->dst_port_id = cpu_to_le32(to->sq_port);
	}

	hdr->size = cpu_to_le32(len);
	hdr->confirm_rx = 0;

	skb_put_padto(skb, ALIGN(len, 4) + sizeof(*hdr));

	mutex_lock(&yesde->ep_lock);
	if (yesde->ep)
		rc = yesde->ep->xmit(yesde->ep, skb);
	else
		kfree_skb(skb);
	mutex_unlock(&yesde->ep_lock);

	return rc;
}

/* Lookup yesde by id.
 *
 * callers must release with qrtr_yesde_release()
 */
static struct qrtr_yesde *qrtr_yesde_lookup(unsigned int nid)
{
	struct qrtr_yesde *yesde;

	mutex_lock(&qrtr_yesde_lock);
	yesde = radix_tree_lookup(&qrtr_yesdes, nid);
	yesde = qrtr_yesde_acquire(yesde);
	mutex_unlock(&qrtr_yesde_lock);

	return yesde;
}

/* Assign yesde id to yesde.
 *
 * This is mostly useful for automatic yesde id assignment, based on
 * the source id in the incoming packet.
 */
static void qrtr_yesde_assign(struct qrtr_yesde *yesde, unsigned int nid)
{
	if (yesde->nid != QRTR_EP_NID_AUTO || nid == QRTR_EP_NID_AUTO)
		return;

	mutex_lock(&qrtr_yesde_lock);
	radix_tree_insert(&qrtr_yesdes, nid, yesde);
	yesde->nid = nid;
	mutex_unlock(&qrtr_yesde_lock);
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
	struct qrtr_yesde *yesde = ep->yesde;
	const struct qrtr_hdr_v1 *v1;
	const struct qrtr_hdr_v2 *v2;
	struct sk_buff *skb;
	struct qrtr_cb *cb;
	unsigned int size;
	unsigned int ver;
	size_t hdrlen;

	if (len & 3)
		return -EINVAL;

	skb = netdev_alloc_skb(NULL, len);
	if (!skb)
		return -ENOMEM;

	cb = (struct qrtr_cb *)skb->cb;

	/* Version field in v1 is little endian, so this works for both cases */
	ver = *(u8*)data;

	switch (ver) {
	case QRTR_PROTO_VER_1:
		v1 = data;
		hdrlen = sizeof(*v1);

		cb->type = le32_to_cpu(v1->type);
		cb->src_yesde = le32_to_cpu(v1->src_yesde_id);
		cb->src_port = le32_to_cpu(v1->src_port_id);
		cb->confirm_rx = !!v1->confirm_rx;
		cb->dst_yesde = le32_to_cpu(v1->dst_yesde_id);
		cb->dst_port = le32_to_cpu(v1->dst_port_id);

		size = le32_to_cpu(v1->size);
		break;
	case QRTR_PROTO_VER_2:
		v2 = data;
		hdrlen = sizeof(*v2) + v2->optlen;

		cb->type = v2->type;
		cb->confirm_rx = !!(v2->flags & QRTR_FLAGS_CONFIRM_RX);
		cb->src_yesde = le16_to_cpu(v2->src_yesde_id);
		cb->src_port = le16_to_cpu(v2->src_port_id);
		cb->dst_yesde = le16_to_cpu(v2->dst_yesde_id);
		cb->dst_port = le16_to_cpu(v2->dst_port_id);

		if (cb->src_port == (u16)QRTR_PORT_CTRL)
			cb->src_port = QRTR_PORT_CTRL;
		if (cb->dst_port == (u16)QRTR_PORT_CTRL)
			cb->dst_port = QRTR_PORT_CTRL;

		size = le32_to_cpu(v2->size);
		break;
	default:
		pr_err("qrtr: Invalid version %d\n", ver);
		goto err;
	}

	if (len != ALIGN(size, 4) + hdrlen)
		goto err;

	if (cb->dst_port != QRTR_PORT_CTRL && cb->type != QRTR_TYPE_DATA)
		goto err;

	skb_put_data(skb, data + hdrlen, size);

	skb_queue_tail(&yesde->rx_queue, skb);
	schedule_work(&yesde->work);

	return 0;

err:
	kfree_skb(skb);
	return -EINVAL;

}
EXPORT_SYMBOL_GPL(qrtr_endpoint_post);

/**
 * qrtr_alloc_ctrl_packet() - allocate control packet skb
 * @pkt: reference to qrtr_ctrl_pkt pointer
 *
 * Returns newly allocated sk_buff, or NULL on failure
 *
 * This function allocates a sk_buff large eyesugh to carry a qrtr_ctrl_pkt and
 * on success returns a reference to the control packet in @pkt.
 */
static struct sk_buff *qrtr_alloc_ctrl_packet(struct qrtr_ctrl_pkt **pkt)
{
	const int pkt_len = sizeof(struct qrtr_ctrl_pkt);
	struct sk_buff *skb;

	skb = alloc_skb(QRTR_HDR_MAX_SIZE + pkt_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, QRTR_HDR_MAX_SIZE);
	*pkt = skb_put_zero(skb, pkt_len);

	return skb;
}

static struct qrtr_sock *qrtr_port_lookup(int port);
static void qrtr_port_put(struct qrtr_sock *ipc);

/* Handle and route a received packet.
 *
 * This will auto-reply with resume-tx packet as necessary.
 */
static void qrtr_yesde_rx_work(struct work_struct *work)
{
	struct qrtr_yesde *yesde = container_of(work, struct qrtr_yesde, work);
	struct qrtr_ctrl_pkt *pkt;
	struct sockaddr_qrtr dst;
	struct sockaddr_qrtr src;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&yesde->rx_queue)) != NULL) {
		struct qrtr_sock *ipc;
		struct qrtr_cb *cb;
		int confirm;

		cb = (struct qrtr_cb *)skb->cb;
		src.sq_yesde = cb->src_yesde;
		src.sq_port = cb->src_port;
		dst.sq_yesde = cb->dst_yesde;
		dst.sq_port = cb->dst_port;
		confirm = !!cb->confirm_rx;

		qrtr_yesde_assign(yesde, cb->src_yesde);

		ipc = qrtr_port_lookup(cb->dst_port);
		if (!ipc) {
			kfree_skb(skb);
		} else {
			if (sock_queue_rcv_skb(&ipc->sk, skb))
				kfree_skb(skb);

			qrtr_port_put(ipc);
		}

		if (confirm) {
			skb = qrtr_alloc_ctrl_packet(&pkt);
			if (!skb)
				break;

			pkt->cmd = cpu_to_le32(QRTR_TYPE_RESUME_TX);
			pkt->client.yesde = cpu_to_le32(dst.sq_yesde);
			pkt->client.port = cpu_to_le32(dst.sq_port);

			if (qrtr_yesde_enqueue(yesde, skb, QRTR_TYPE_RESUME_TX,
					      &dst, &src))
				break;
		}
	}
}

/**
 * qrtr_endpoint_register() - register a new endpoint
 * @ep: endpoint to register
 * @nid: desired yesde id; may be QRTR_EP_NID_AUTO for auto-assignment
 * Return: 0 on success; negative error code on failure
 *
 * The specified endpoint must have the xmit function pointer set on call.
 */
int qrtr_endpoint_register(struct qrtr_endpoint *ep, unsigned int nid)
{
	struct qrtr_yesde *yesde;

	if (!ep || !ep->xmit)
		return -EINVAL;

	yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
	if (!yesde)
		return -ENOMEM;

	INIT_WORK(&yesde->work, qrtr_yesde_rx_work);
	kref_init(&yesde->ref);
	mutex_init(&yesde->ep_lock);
	skb_queue_head_init(&yesde->rx_queue);
	yesde->nid = QRTR_EP_NID_AUTO;
	yesde->ep = ep;

	qrtr_yesde_assign(yesde, nid);

	mutex_lock(&qrtr_yesde_lock);
	list_add(&yesde->item, &qrtr_all_yesdes);
	mutex_unlock(&qrtr_yesde_lock);
	ep->yesde = yesde;

	return 0;
}
EXPORT_SYMBOL_GPL(qrtr_endpoint_register);

/**
 * qrtr_endpoint_unregister - unregister endpoint
 * @ep: endpoint to unregister
 */
void qrtr_endpoint_unregister(struct qrtr_endpoint *ep)
{
	struct qrtr_yesde *yesde = ep->yesde;
	struct sockaddr_qrtr src = {AF_QIPCRTR, yesde->nid, QRTR_PORT_CTRL};
	struct sockaddr_qrtr dst = {AF_QIPCRTR, qrtr_local_nid, QRTR_PORT_CTRL};
	struct qrtr_ctrl_pkt *pkt;
	struct sk_buff *skb;

	mutex_lock(&yesde->ep_lock);
	yesde->ep = NULL;
	mutex_unlock(&yesde->ep_lock);

	/* Notify the local controller about the event */
	skb = qrtr_alloc_ctrl_packet(&pkt);
	if (skb) {
		pkt->cmd = cpu_to_le32(QRTR_TYPE_BYE);
		qrtr_local_enqueue(NULL, skb, QRTR_TYPE_BYE, &src, &dst);
	}

	qrtr_yesde_release(yesde);
	ep->yesde = NULL;
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
	struct qrtr_ctrl_pkt *pkt;
	struct sk_buff *skb;
	int port = ipc->us.sq_port;
	struct sockaddr_qrtr to;

	to.sq_family = AF_QIPCRTR;
	to.sq_yesde = QRTR_NODE_BCAST;
	to.sq_port = QRTR_PORT_CTRL;

	skb = qrtr_alloc_ctrl_packet(&pkt);
	if (skb) {
		pkt->cmd = cpu_to_le32(QRTR_TYPE_DEL_CLIENT);
		pkt->client.yesde = cpu_to_le32(ipc->us.sq_yesde);
		pkt->client.port = cpu_to_le32(ipc->us.sq_port);

		skb_set_owner_w(skb, &ipc->sk);
		qrtr_bcast_enqueue(NULL, skb, QRTR_TYPE_DEL_CLIENT, &ipc->us,
				   &to);
	}

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

/* Reset all yesn-control ports */
static void qrtr_reset_ports(void)
{
	struct qrtr_sock *ipc;
	int id;

	mutex_lock(&qrtr_port_lock);
	idr_for_each_entry(&qrtr_ports, ipc, id) {
		/* Don't reset control port */
		if (id == 0)
			continue;

		sock_hold(&ipc->sk);
		ipc->sk.sk_err = ENETRESET;
		ipc->sk.sk_error_report(&ipc->sk);
		sock_put(&ipc->sk);
	}
	mutex_unlock(&qrtr_port_lock);
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

	/* Notify all open ports about the new controller */
	if (port == QRTR_PORT_CTRL)
		qrtr_reset_ports();

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
	addr.sq_yesde = qrtr_local_nid;
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

	if (addr->sq_yesde != ipc->us.sq_yesde)
		return -EINVAL;

	lock_sock(sk);
	rc = __qrtr_bind(sock, addr, sock_flag(sk, SOCK_ZAPPED));
	release_sock(sk);

	return rc;
}

/* Queue packet to local peer socket. */
static int qrtr_local_enqueue(struct qrtr_yesde *yesde, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to)
{
	struct qrtr_sock *ipc;
	struct qrtr_cb *cb;

	ipc = qrtr_port_lookup(to->sq_port);
	if (!ipc || &ipc->sk == skb->sk) { /* do yest send to self */
		kfree_skb(skb);
		return -ENODEV;
	}

	cb = (struct qrtr_cb *)skb->cb;
	cb->src_yesde = from->sq_yesde;
	cb->src_port = from->sq_port;

	if (sock_queue_rcv_skb(&ipc->sk, skb)) {
		qrtr_port_put(ipc);
		kfree_skb(skb);
		return -ENOSPC;
	}

	qrtr_port_put(ipc);

	return 0;
}

/* Queue packet for broadcast. */
static int qrtr_bcast_enqueue(struct qrtr_yesde *yesde, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to)
{
	struct sk_buff *skbn;

	mutex_lock(&qrtr_yesde_lock);
	list_for_each_entry(yesde, &qrtr_all_yesdes, item) {
		skbn = skb_clone(skb, GFP_KERNEL);
		if (!skbn)
			break;
		skb_set_owner_w(skbn, skb->sk);
		qrtr_yesde_enqueue(yesde, skbn, type, from, to);
	}
	mutex_unlock(&qrtr_yesde_lock);

	qrtr_local_enqueue(yesde, skb, type, from, to);

	return 0;
}

static int qrtr_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	int (*enqueue_fn)(struct qrtr_yesde *, struct sk_buff *, int,
			  struct sockaddr_qrtr *, struct sockaddr_qrtr *);
	__le32 qrtr_type = cpu_to_le32(QRTR_TYPE_DATA);
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	struct qrtr_yesde *yesde;
	struct sk_buff *skb;
	size_t plen;
	u32 type;
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

	yesde = NULL;
	if (addr->sq_yesde == QRTR_NODE_BCAST) {
		enqueue_fn = qrtr_bcast_enqueue;
		if (addr->sq_port != QRTR_PORT_CTRL) {
			release_sock(sk);
			return -ENOTCONN;
		}
	} else if (addr->sq_yesde == ipc->us.sq_yesde) {
		enqueue_fn = qrtr_local_enqueue;
	} else {
		enqueue_fn = qrtr_yesde_enqueue;
		yesde = qrtr_yesde_lookup(addr->sq_yesde);
		if (!yesde) {
			release_sock(sk);
			return -ECONNRESET;
		}
	}

	plen = (len + 3) & ~3;
	skb = sock_alloc_send_skb(sk, plen + QRTR_HDR_MAX_SIZE,
				  msg->msg_flags & MSG_DONTWAIT, &rc);
	if (!skb)
		goto out_yesde;

	skb_reserve(skb, QRTR_HDR_MAX_SIZE);

	rc = memcpy_from_msg(skb_put(skb, len), msg, len);
	if (rc) {
		kfree_skb(skb);
		goto out_yesde;
	}

	if (ipc->us.sq_port == QRTR_PORT_CTRL) {
		if (len < 4) {
			rc = -EINVAL;
			kfree_skb(skb);
			goto out_yesde;
		}

		/* control messages already require the type as 'command' */
		skb_copy_bits(skb, 0, &qrtr_type, 4);
	}

	type = le32_to_cpu(qrtr_type);
	rc = enqueue_fn(yesde, skb, type, &ipc->us, addr);
	if (rc >= 0)
		rc = len;

out_yesde:
	qrtr_yesde_release(yesde);
	release_sock(sk);

	return rc;
}

static int qrtr_recvmsg(struct socket *sock, struct msghdr *msg,
			size_t size, int flags)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	struct qrtr_cb *cb;
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

	copied = skb->len;
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	rc = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (rc < 0)
		goto out;
	rc = copied;

	if (addr) {
		cb = (struct qrtr_cb *)skb->cb;
		addr->sq_family = AF_QIPCRTR;
		addr->sq_yesde = cb->src_yesde;
		addr->sq_port = cb->src_port;
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
			int peer)
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

	qaddr.sq_family = AF_QIPCRTR;

	memcpy(saddr, &qaddr, sizeof(qaddr));

	return sizeof(qaddr);
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
			len = skb->len;
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
	.socketpair	= sock_yes_socketpair,
	.accept		= sock_yes_accept,
	.listen		= sock_yes_listen,
	.sendmsg	= qrtr_sendmsg,
	.recvmsg	= qrtr_recvmsg,
	.getname	= qrtr_getname,
	.ioctl		= qrtr_ioctl,
	.gettstamp	= sock_gettstamp,
	.poll		= datagram_poll,
	.shutdown	= sock_yes_shutdown,
	.setsockopt	= sock_yes_setsockopt,
	.getsockopt	= sock_yes_getsockopt,
	.release	= qrtr_release,
	.mmap		= sock_yes_mmap,
	.sendpage	= sock_yes_sendpage,
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
	ipc->us.sq_yesde = qrtr_local_nid;
	ipc->us.sq_port = 0;

	return 0;
}

static const struct nla_policy qrtr_policy[IFA_MAX + 1] = {
	[IFA_LOCAL] = { .type = NLA_U32 },
};

static int qrtr_addr_doit(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFA_MAX + 1];
	struct ifaddrmsg *ifm;
	int rc;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!netlink_capable(skb, CAP_SYS_ADMIN))
		return -EPERM;

	ASSERT_RTNL();

	rc = nlmsg_parse_deprecated(nlh, sizeof(*ifm), tb, IFA_MAX,
				    qrtr_policy, extack);
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

	rc = rtnl_register_module(THIS_MODULE, PF_QIPCRTR, RTM_NEWADDR, qrtr_addr_doit, NULL, 0);
	if (rc) {
		sock_unregister(qrtr_family.family);
		proto_unregister(&qrtr_proto);
	}

	return rc;
}
postcore_initcall(qrtr_proto_init);

static void __exit qrtr_proto_fini(void)
{
	rtnl_unregister(PF_QIPCRTR, RTM_NEWADDR);
	sock_unregister(qrtr_family.family);
	proto_unregister(&qrtr_proto);
}
module_exit(qrtr_proto_fini);

MODULE_DESCRIPTION("Qualcomm IPC-router driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(PF_QIPCRTR);
