// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/qrtr.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/spinlock.h>
#include <linux/wait.h>

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
 * @src_node_id: source node
 * @src_port_id: source port
 * @confirm_rx: boolean; whether a resume-tx packet should be send in reply
 * @size: length of packet, excluding this header
 * @dst_node_id: destination node
 * @dst_port_id: destination port
 */
struct qrtr_hdr_v1 {
	__le32 version;
	__le32 type;
	__le32 src_node_id;
	__le32 src_port_id;
	__le32 confirm_rx;
	__le32 size;
	__le32 dst_node_id;
	__le32 dst_port_id;
} __packed;

/**
 * struct qrtr_hdr_v2 - (I|R)PCrouter packet header later versions
 * @version: protocol version
 * @type: packet type; one of QRTR_TYPE_*
 * @flags: bitmask of QRTR_FLAGS_*
 * @optlen: length of optional header data
 * @size: length of packet, excluding this header and optlen
 * @src_node_id: source node
 * @src_port_id: source port
 * @dst_node_id: destination node
 * @dst_port_id: destination port
 */
struct qrtr_hdr_v2 {
	u8 version;
	u8 type;
	u8 flags;
	u8 optlen;
	__le32 size;
	__le16 src_node_id;
	__le16 src_port_id;
	__le16 dst_node_id;
	__le16 dst_port_id;
};

#define QRTR_FLAGS_CONFIRM_RX	BIT(0)

struct qrtr_cb {
	u32 src_node;
	u32 src_port;
	u32 dst_node;
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

static unsigned int qrtr_local_nid = 1;

/* for node ids */
static RADIX_TREE(qrtr_nodes, GFP_ATOMIC);
static DEFINE_SPINLOCK(qrtr_nodes_lock);
/* broadcast list */
static LIST_HEAD(qrtr_all_nodes);
/* lock for qrtr_all_nodes and node reference */
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
 * @qrtr_tx_flow: tree of qrtr_tx_flow, keyed by node << 32 | port
 * @qrtr_tx_lock: lock for qrtr_tx_flow inserts
 * @rx_queue: receive queue
 * @item: list item for broadcast list
 */
struct qrtr_node {
	struct mutex ep_lock;
	struct qrtr_endpoint *ep;
	struct kref ref;
	unsigned int nid;

	struct radix_tree_root qrtr_tx_flow;
	struct mutex qrtr_tx_lock; /* for qrtr_tx_flow */

	struct sk_buff_head rx_queue;
	struct list_head item;
};

/**
 * struct qrtr_tx_flow - tx flow control
 * @resume_tx: waiters for a resume tx from the remote
 * @pending: number of waiting senders
 * @tx_failed: indicates that a message with confirm_rx flag was lost
 */
struct qrtr_tx_flow {
	struct wait_queue_head resume_tx;
	int pending;
	int tx_failed;
};

#define QRTR_TX_FLOW_HIGH	10
#define QRTR_TX_FLOW_LOW	5

static int qrtr_local_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to);
static int qrtr_bcast_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to);
static struct qrtr_sock *qrtr_port_lookup(int port);
static void qrtr_port_put(struct qrtr_sock *ipc);

/* Release node resources and free the node.
 *
 * Do not call directly, use qrtr_node_release.  To be used with
 * kref_put_mutex.  As such, the node mutex is expected to be locked on call.
 */
static void __qrtr_node_release(struct kref *kref)
{
	struct qrtr_node *node = container_of(kref, struct qrtr_node, ref);
	struct radix_tree_iter iter;
	struct qrtr_tx_flow *flow;
	unsigned long flags;
	void __rcu **slot;

	spin_lock_irqsave(&qrtr_nodes_lock, flags);
	/* If the node is a bridge for other nodes, there are possibly
	 * multiple entries pointing to our released node, delete them all.
	 */
	radix_tree_for_each_slot(slot, &qrtr_nodes, &iter, 0) {
		if (*slot == node)
			radix_tree_iter_delete(&qrtr_nodes, &iter, slot);
	}
	spin_unlock_irqrestore(&qrtr_nodes_lock, flags);

	list_del(&node->item);
	mutex_unlock(&qrtr_node_lock);

	skb_queue_purge(&node->rx_queue);

	/* Free tx flow counters */
	radix_tree_for_each_slot(slot, &node->qrtr_tx_flow, &iter, 0) {
		flow = *slot;
		radix_tree_iter_delete(&node->qrtr_tx_flow, &iter, slot);
		kfree(flow);
	}
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

/**
 * qrtr_tx_resume() - reset flow control counter
 * @node:	qrtr_node that the QRTR_TYPE_RESUME_TX packet arrived on
 * @skb:	resume_tx packet
 */
static void qrtr_tx_resume(struct qrtr_node *node, struct sk_buff *skb)
{
	struct qrtr_ctrl_pkt *pkt = (struct qrtr_ctrl_pkt *)skb->data;
	u64 remote_node = le32_to_cpu(pkt->client.node);
	u32 remote_port = le32_to_cpu(pkt->client.port);
	struct qrtr_tx_flow *flow;
	unsigned long key;

	key = remote_node << 32 | remote_port;

	rcu_read_lock();
	flow = radix_tree_lookup(&node->qrtr_tx_flow, key);
	rcu_read_unlock();
	if (flow) {
		spin_lock(&flow->resume_tx.lock);
		flow->pending = 0;
		spin_unlock(&flow->resume_tx.lock);
		wake_up_interruptible_all(&flow->resume_tx);
	}

	consume_skb(skb);
}

/**
 * qrtr_tx_wait() - flow control for outgoing packets
 * @node:	qrtr_node that the packet is to be send to
 * @dest_node:	node id of the destination
 * @dest_port:	port number of the destination
 * @type:	type of message
 *
 * The flow control scheme is based around the low and high "watermarks". When
 * the low watermark is passed the confirm_rx flag is set on the outgoing
 * message, which will trigger the remote to send a control message of the type
 * QRTR_TYPE_RESUME_TX to reset the counter. If the high watermark is hit
 * further transmision should be paused.
 *
 * Return: 1 if confirm_rx should be set, 0 otherwise or errno failure
 */
static int qrtr_tx_wait(struct qrtr_node *node, int dest_node, int dest_port,
			int type)
{
	unsigned long key = (u64)dest_node << 32 | dest_port;
	struct qrtr_tx_flow *flow;
	int confirm_rx = 0;
	int ret;

	/* Never set confirm_rx on non-data packets */
	if (type != QRTR_TYPE_DATA)
		return 0;

	mutex_lock(&node->qrtr_tx_lock);
	flow = radix_tree_lookup(&node->qrtr_tx_flow, key);
	if (!flow) {
		flow = kzalloc(sizeof(*flow), GFP_KERNEL);
		if (flow) {
			init_waitqueue_head(&flow->resume_tx);
			radix_tree_insert(&node->qrtr_tx_flow, key, flow);
		}
	}
	mutex_unlock(&node->qrtr_tx_lock);

	/* Set confirm_rx if we where unable to find and allocate a flow */
	if (!flow)
		return 1;

	spin_lock_irq(&flow->resume_tx.lock);
	ret = wait_event_interruptible_locked_irq(flow->resume_tx,
						  flow->pending < QRTR_TX_FLOW_HIGH ||
						  flow->tx_failed ||
						  !node->ep);
	if (ret < 0) {
		confirm_rx = ret;
	} else if (!node->ep) {
		confirm_rx = -EPIPE;
	} else if (flow->tx_failed) {
		flow->tx_failed = 0;
		confirm_rx = 1;
	} else {
		flow->pending++;
		confirm_rx = flow->pending == QRTR_TX_FLOW_LOW;
	}
	spin_unlock_irq(&flow->resume_tx.lock);

	return confirm_rx;
}

/**
 * qrtr_tx_flow_failed() - flag that tx of confirm_rx flagged messages failed
 * @node:	qrtr_node that the packet is to be send to
 * @dest_node:	node id of the destination
 * @dest_port:	port number of the destination
 *
 * Signal that the transmission of a message with confirm_rx flag failed. The
 * flow's "pending" counter will keep incrementing towards QRTR_TX_FLOW_HIGH,
 * at which point transmission would stall forever waiting for the resume TX
 * message associated with the dropped confirm_rx message.
 * Work around this by marking the flow as having a failed transmission and
 * cause the next transmission attempt to be sent with the confirm_rx.
 */
static void qrtr_tx_flow_failed(struct qrtr_node *node, int dest_node,
				int dest_port)
{
	unsigned long key = (u64)dest_node << 32 | dest_port;
	struct qrtr_tx_flow *flow;

	rcu_read_lock();
	flow = radix_tree_lookup(&node->qrtr_tx_flow, key);
	rcu_read_unlock();
	if (flow) {
		spin_lock_irq(&flow->resume_tx.lock);
		flow->tx_failed = 1;
		spin_unlock_irq(&flow->resume_tx.lock);
	}
}

/* Pass an outgoing packet socket buffer to the endpoint driver. */
static int qrtr_node_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			     int type, struct sockaddr_qrtr *from,
			     struct sockaddr_qrtr *to)
{
	struct qrtr_hdr_v1 *hdr;
	size_t len = skb->len;
	int rc, confirm_rx;

	confirm_rx = qrtr_tx_wait(node, to->sq_node, to->sq_port, type);
	if (confirm_rx < 0) {
		kfree_skb(skb);
		return confirm_rx;
	}

	hdr = skb_push(skb, sizeof(*hdr));
	hdr->version = cpu_to_le32(QRTR_PROTO_VER_1);
	hdr->type = cpu_to_le32(type);
	hdr->src_node_id = cpu_to_le32(from->sq_node);
	hdr->src_port_id = cpu_to_le32(from->sq_port);
	if (to->sq_port == QRTR_PORT_CTRL) {
		hdr->dst_node_id = cpu_to_le32(node->nid);
		hdr->dst_port_id = cpu_to_le32(QRTR_PORT_CTRL);
	} else {
		hdr->dst_node_id = cpu_to_le32(to->sq_node);
		hdr->dst_port_id = cpu_to_le32(to->sq_port);
	}

	hdr->size = cpu_to_le32(len);
	hdr->confirm_rx = !!confirm_rx;

	rc = skb_put_padto(skb, ALIGN(len, 4) + sizeof(*hdr));

	if (!rc) {
		mutex_lock(&node->ep_lock);
		rc = -ENODEV;
		if (node->ep)
			rc = node->ep->xmit(node->ep, skb);
		else
			kfree_skb(skb);
		mutex_unlock(&node->ep_lock);
	}
	/* Need to ensure that a subsequent message carries the otherwise lost
	 * confirm_rx flag if we dropped this one */
	if (rc && confirm_rx)
		qrtr_tx_flow_failed(node, to->sq_node, to->sq_port);

	return rc;
}

/* Lookup node by id.
 *
 * callers must release with qrtr_node_release()
 */
static struct qrtr_node *qrtr_node_lookup(unsigned int nid)
{
	struct qrtr_node *node;
	unsigned long flags;

	spin_lock_irqsave(&qrtr_nodes_lock, flags);
	node = radix_tree_lookup(&qrtr_nodes, nid);
	node = qrtr_node_acquire(node);
	spin_unlock_irqrestore(&qrtr_nodes_lock, flags);

	return node;
}

/* Assign node id to node.
 *
 * This is mostly useful for automatic node id assignment, based on
 * the source id in the incoming packet.
 */
static void qrtr_node_assign(struct qrtr_node *node, unsigned int nid)
{
	unsigned long flags;

	if (nid == QRTR_EP_NID_AUTO)
		return;

	spin_lock_irqsave(&qrtr_nodes_lock, flags);
	radix_tree_insert(&qrtr_nodes, nid, node);
	if (node->nid == QRTR_EP_NID_AUTO)
		node->nid = nid;
	spin_unlock_irqrestore(&qrtr_nodes_lock, flags);
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
	const struct qrtr_hdr_v1 *v1;
	const struct qrtr_hdr_v2 *v2;
	struct qrtr_sock *ipc;
	struct sk_buff *skb;
	struct qrtr_cb *cb;
	unsigned int size;
	unsigned int ver;
	size_t hdrlen;

	if (len == 0 || len & 3)
		return -EINVAL;

	skb = netdev_alloc_skb(NULL, len);
	if (!skb)
		return -ENOMEM;

	cb = (struct qrtr_cb *)skb->cb;

	/* Version field in v1 is little endian, so this works for both cases */
	ver = *(u8*)data;

	switch (ver) {
	case QRTR_PROTO_VER_1:
		if (len < sizeof(*v1))
			goto err;
		v1 = data;
		hdrlen = sizeof(*v1);

		cb->type = le32_to_cpu(v1->type);
		cb->src_node = le32_to_cpu(v1->src_node_id);
		cb->src_port = le32_to_cpu(v1->src_port_id);
		cb->confirm_rx = !!v1->confirm_rx;
		cb->dst_node = le32_to_cpu(v1->dst_node_id);
		cb->dst_port = le32_to_cpu(v1->dst_port_id);

		size = le32_to_cpu(v1->size);
		break;
	case QRTR_PROTO_VER_2:
		if (len < sizeof(*v2))
			goto err;
		v2 = data;
		hdrlen = sizeof(*v2) + v2->optlen;

		cb->type = v2->type;
		cb->confirm_rx = !!(v2->flags & QRTR_FLAGS_CONFIRM_RX);
		cb->src_node = le16_to_cpu(v2->src_node_id);
		cb->src_port = le16_to_cpu(v2->src_port_id);
		cb->dst_node = le16_to_cpu(v2->dst_node_id);
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

	if (cb->dst_port != QRTR_PORT_CTRL && cb->type != QRTR_TYPE_DATA &&
	    cb->type != QRTR_TYPE_RESUME_TX)
		goto err;

	skb_put_data(skb, data + hdrlen, size);

	qrtr_node_assign(node, cb->src_node);

	if (cb->type == QRTR_TYPE_NEW_SERVER) {
		/* Remote node endpoint can bridge other distant nodes */
		const struct qrtr_ctrl_pkt *pkt = data + hdrlen;

		qrtr_node_assign(node, le32_to_cpu(pkt->server.node));
	}

	if (cb->type == QRTR_TYPE_RESUME_TX) {
		qrtr_tx_resume(node, skb);
	} else {
		ipc = qrtr_port_lookup(cb->dst_port);
		if (!ipc)
			goto err;

		if (sock_queue_rcv_skb(&ipc->sk, skb))
			goto err;

		qrtr_port_put(ipc);
	}

	return 0;

err:
	kfree_skb(skb);
	return -EINVAL;

}
EXPORT_SYMBOL_GPL(qrtr_endpoint_post);

/**
 * qrtr_alloc_ctrl_packet() - allocate control packet skb
 * @pkt: reference to qrtr_ctrl_pkt pointer
 * @flags: the type of memory to allocate
 *
 * Returns newly allocated sk_buff, or NULL on failure
 *
 * This function allocates a sk_buff large enough to carry a qrtr_ctrl_pkt and
 * on success returns a reference to the control packet in @pkt.
 */
static struct sk_buff *qrtr_alloc_ctrl_packet(struct qrtr_ctrl_pkt **pkt,
					      gfp_t flags)
{
	const int pkt_len = sizeof(struct qrtr_ctrl_pkt);
	struct sk_buff *skb;

	skb = alloc_skb(QRTR_HDR_MAX_SIZE + pkt_len, flags);
	if (!skb)
		return NULL;

	skb_reserve(skb, QRTR_HDR_MAX_SIZE);
	*pkt = skb_put_zero(skb, pkt_len);

	return skb;
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

	kref_init(&node->ref);
	mutex_init(&node->ep_lock);
	skb_queue_head_init(&node->rx_queue);
	node->nid = QRTR_EP_NID_AUTO;
	node->ep = ep;

	INIT_RADIX_TREE(&node->qrtr_tx_flow, GFP_KERNEL);
	mutex_init(&node->qrtr_tx_lock);

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
	struct sockaddr_qrtr src = {AF_QIPCRTR, node->nid, QRTR_PORT_CTRL};
	struct sockaddr_qrtr dst = {AF_QIPCRTR, qrtr_local_nid, QRTR_PORT_CTRL};
	struct radix_tree_iter iter;
	struct qrtr_ctrl_pkt *pkt;
	struct qrtr_tx_flow *flow;
	struct sk_buff *skb;
	unsigned long flags;
	void __rcu **slot;

	mutex_lock(&node->ep_lock);
	node->ep = NULL;
	mutex_unlock(&node->ep_lock);

	/* Notify the local controller about the event */
	spin_lock_irqsave(&qrtr_nodes_lock, flags);
	radix_tree_for_each_slot(slot, &qrtr_nodes, &iter, 0) {
		if (*slot != node)
			continue;
		src.sq_node = iter.index;
		skb = qrtr_alloc_ctrl_packet(&pkt, GFP_ATOMIC);
		if (skb) {
			pkt->cmd = cpu_to_le32(QRTR_TYPE_BYE);
			qrtr_local_enqueue(NULL, skb, QRTR_TYPE_BYE, &src, &dst);
		}
	}
	spin_unlock_irqrestore(&qrtr_nodes_lock, flags);

	/* Wake up any transmitters waiting for resume-tx from the node */
	mutex_lock(&node->qrtr_tx_lock);
	radix_tree_for_each_slot(slot, &node->qrtr_tx_flow, &iter, 0) {
		flow = *slot;
		wake_up_interruptible_all(&flow->resume_tx);
	}
	mutex_unlock(&node->qrtr_tx_lock);

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

	rcu_read_lock();
	ipc = idr_find(&qrtr_ports, port);
	if (ipc)
		sock_hold(&ipc->sk);
	rcu_read_unlock();

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
	to.sq_node = QRTR_NODE_BCAST;
	to.sq_port = QRTR_PORT_CTRL;

	skb = qrtr_alloc_ctrl_packet(&pkt, GFP_KERNEL);
	if (skb) {
		pkt->cmd = cpu_to_le32(QRTR_TYPE_DEL_CLIENT);
		pkt->client.node = cpu_to_le32(ipc->us.sq_node);
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

	/* Ensure that if qrtr_port_lookup() did enter the RCU read section we
	 * wait for it to up increment the refcount */
	synchronize_rcu();
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
	u32 min_port;
	int rc;

	mutex_lock(&qrtr_port_lock);
	if (!*port) {
		min_port = QRTR_MIN_EPH_SOCKET;
		rc = idr_alloc_u32(&qrtr_ports, ipc, &min_port, QRTR_MAX_EPH_SOCKET, GFP_ATOMIC);
		if (!rc)
			*port = min_port;
	} else if (*port < QRTR_MIN_EPH_SOCKET && !capable(CAP_NET_ADMIN)) {
		rc = -EACCES;
	} else if (*port == QRTR_PORT_CTRL) {
		min_port = 0;
		rc = idr_alloc_u32(&qrtr_ports, ipc, &min_port, 0, GFP_ATOMIC);
	} else {
		min_port = *port;
		rc = idr_alloc_u32(&qrtr_ports, ipc, &min_port, *port, GFP_ATOMIC);
		if (!rc)
			*port = min_port;
	}
	mutex_unlock(&qrtr_port_lock);

	if (rc == -ENOSPC)
		return -EADDRINUSE;
	else if (rc < 0)
		return rc;

	sock_hold(&ipc->sk);

	return 0;
}

/* Reset all non-control ports */
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
static int qrtr_local_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to)
{
	struct qrtr_sock *ipc;
	struct qrtr_cb *cb;

	ipc = qrtr_port_lookup(to->sq_port);
	if (!ipc || &ipc->sk == skb->sk) { /* do not send to self */
		kfree_skb(skb);
		return -ENODEV;
	}

	cb = (struct qrtr_cb *)skb->cb;
	cb->src_node = from->sq_node;
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
static int qrtr_bcast_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to)
{
	struct sk_buff *skbn;

	mutex_lock(&qrtr_node_lock);
	list_for_each_entry(node, &qrtr_all_nodes, item) {
		skbn = skb_clone(skb, GFP_KERNEL);
		if (!skbn)
			break;
		skb_set_owner_w(skbn, skb->sk);
		qrtr_node_enqueue(node, skbn, type, from, to);
	}
	mutex_unlock(&qrtr_node_lock);

	qrtr_local_enqueue(NULL, skb, type, from, to);

	return 0;
}

static int qrtr_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	int (*enqueue_fn)(struct qrtr_node *, struct sk_buff *, int,
			  struct sockaddr_qrtr *, struct sockaddr_qrtr *);
	__le32 qrtr_type = cpu_to_le32(QRTR_TYPE_DATA);
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	struct qrtr_node *node;
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

	node = NULL;
	if (addr->sq_node == QRTR_NODE_BCAST) {
		if (addr->sq_port != QRTR_PORT_CTRL &&
		    qrtr_local_nid != QRTR_NODE_BCAST) {
			release_sock(sk);
			return -ENOTCONN;
		}
		enqueue_fn = qrtr_bcast_enqueue;
	} else if (addr->sq_node == ipc->us.sq_node) {
		enqueue_fn = qrtr_local_enqueue;
	} else {
		node = qrtr_node_lookup(addr->sq_node);
		if (!node) {
			release_sock(sk);
			return -ECONNRESET;
		}
		enqueue_fn = qrtr_node_enqueue;
	}

	plen = (len + 3) & ~3;
	skb = sock_alloc_send_skb(sk, plen + QRTR_HDR_MAX_SIZE,
				  msg->msg_flags & MSG_DONTWAIT, &rc);
	if (!skb)
		goto out_node;

	skb_reserve(skb, QRTR_HDR_MAX_SIZE);

	rc = memcpy_from_msg(skb_put(skb, len), msg, len);
	if (rc) {
		kfree_skb(skb);
		goto out_node;
	}

	if (ipc->us.sq_port == QRTR_PORT_CTRL) {
		if (len < 4) {
			rc = -EINVAL;
			kfree_skb(skb);
			goto out_node;
		}

		/* control messages already require the type as 'command' */
		skb_copy_bits(skb, 0, &qrtr_type, 4);
	}

	type = le32_to_cpu(qrtr_type);
	rc = enqueue_fn(node, skb, type, &ipc->us, addr);
	if (rc >= 0)
		rc = len;

out_node:
	qrtr_node_release(node);
	release_sock(sk);

	return rc;
}

static int qrtr_send_resume_tx(struct qrtr_cb *cb)
{
	struct sockaddr_qrtr remote = { AF_QIPCRTR, cb->src_node, cb->src_port };
	struct sockaddr_qrtr local = { AF_QIPCRTR, cb->dst_node, cb->dst_port };
	struct qrtr_ctrl_pkt *pkt;
	struct qrtr_node *node;
	struct sk_buff *skb;
	int ret;

	node = qrtr_node_lookup(remote.sq_node);
	if (!node)
		return -EINVAL;

	skb = qrtr_alloc_ctrl_packet(&pkt, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	pkt->cmd = cpu_to_le32(QRTR_TYPE_RESUME_TX);
	pkt->client.node = cpu_to_le32(cb->dst_node);
	pkt->client.port = cpu_to_le32(cb->dst_port);

	ret = qrtr_node_enqueue(node, skb, QRTR_TYPE_RESUME_TX, &local, &remote);

	qrtr_node_release(node);

	return ret;
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
	cb = (struct qrtr_cb *)skb->cb;

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
		addr->sq_family = AF_QIPCRTR;
		addr->sq_node = cb->src_node;
		addr->sq_port = cb->src_port;
		msg->msg_namelen = sizeof(*addr);
	}

out:
	if (cb->confirm_rx)
		qrtr_send_resume_tx(cb);

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
	sock_orphan(sk);
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
	.gettstamp	= sock_gettstamp,
	.poll		= datagram_poll,
	.shutdown	= sock_no_shutdown,
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

	qrtr_ns_init();

	return rc;
}
postcore_initcall(qrtr_proto_init);

static void __exit qrtr_proto_fini(void)
{
	qrtr_ns_remove();
	sock_unregister(qrtr_family.family);
	proto_unregister(&qrtr_proto);
}
module_exit(qrtr_proto_fini);

MODULE_DESCRIPTION("Qualcomm IPC-router driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(PF_QIPCRTR);
