// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/qrtr.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/rwsem.h>
#include <linux/uidgid.h>
#include <linux/pm_wakeup.h>
#include <linux/of_device.h>
#include <linux/ipc_logging.h>
#include <linux/completion.h>

#include <net/sock.h>
#include <uapi/linux/sched/types.h>

#include "qrtr.h"

#define QRTR_LOG_PAGE_CNT 4
#define QRTR_INFO(ctx, x, ...)				\
	ipc_log_string(ctx, x, ##__VA_ARGS__)

#define QRTR_PROTO_VER_1 1
#define QRTR_PROTO_VER_2 3

/* auto-bind range */
#define QRTR_MIN_EPH_SOCKET 0x4000
#define QRTR_MAX_EPH_SOCKET 0x7fff
#define QRTR_EPH_PORT_RANGE \
		XA_LIMIT(QRTR_MIN_EPH_SOCKET, QRTR_MAX_EPH_SOCKET)

#define QRTR_PORT_CTRL_LEGACY 0xffff

/* qrtr socket states */
#define QRTR_STATE_MULTI	-2
#define QRTR_STATE_INIT		-1

#define AID_VENDOR_QRTR	KGIDT_INIT(2906)

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

	int state;

	struct completion rx_queue_has_space;
	bool signal_on_recv;
	/* protect above signal variables */
	spinlock_t signal_lock;
};

static inline struct qrtr_sock *qrtr_sk(struct sock *sk)
{
	BUILD_BUG_ON(offsetof(struct qrtr_sock, sk) != 0);
	return container_of(sk, struct qrtr_sock, sk);
}

static unsigned int qrtr_local_nid = CONFIG_QRTR_NODE_ID;
static unsigned int qrtr_wakeup_ms = CONFIG_QRTR_WAKEUP_MS;

/* For local IPC logging context*/
static void *qrtr_local_ilc;

/* for node ids */
static RADIX_TREE(qrtr_nodes, GFP_ATOMIC);
static DEFINE_SPINLOCK(qrtr_nodes_lock);
/* broadcast list */
static LIST_HEAD(qrtr_all_epts);
/* lock for qrtr_all_epts */
static DECLARE_RWSEM(qrtr_epts_lock);

/* local port allocation management */
static DEFINE_XARRAY_ALLOC(qrtr_ports);
u32 qrtr_ports_next = QRTR_MIN_EPH_SOCKET;
static DEFINE_SPINLOCK(qrtr_port_lock);

/* backup buffers */
#define QRTR_BACKUP_HI_NUM	5
#define QRTR_BACKUP_HI_SIZE	SZ_16K
#define QRTR_BACKUP_LO_NUM	20
#define QRTR_BACKUP_LO_SIZE	SZ_1K
static struct sk_buff_head qrtr_backup_lo;
static struct sk_buff_head qrtr_backup_hi;
static struct work_struct qrtr_backup_work;

/**
 * struct qrtr_node - endpoint node
 * @ep_lock: lock for endpoint management and callbacks
 * @ep: endpoint
 * @ref: reference count for node
 * @nid: node id
 * @net_id: network cluster identifer
 * @qrtr_tx_flow: tree of qrtr_tx_flow, keyed by node << 32 | port
 * @qrtr_tx_lock: lock for qrtr_tx_flow inserts
 * @hello_sent: hello packet sent to endpoint
 * @hello_rcvd: hello packet received from endpoint
 * @rx_queue: receive queue
 * @item: list item for broadcast list
 * @kworker: worker thread for recv work
 * @task: task to run the worker thread
 * @read_data: scheduled work for recv work
 * @say_hello: scheduled work for initiating hello
 * @ws: wakeupsource avoid system suspend
 * @ilc: ipc logging context reference
 */
struct qrtr_node {
	struct mutex ep_lock;
	struct qrtr_endpoint *ep;
	struct kref ref;
	unsigned int nid;
	unsigned int net_id;
	atomic_t hello_sent;
	atomic_t hello_rcvd;

	struct radix_tree_root qrtr_tx_flow;
	struct mutex qrtr_tx_lock; /* for qrtr_tx_flow */

	struct sk_buff_head rx_queue;
	struct list_head item;

	struct kthread_worker kworker;
	struct task_struct *task;
	struct kthread_work read_data;
	struct kthread_work say_hello;

	struct wakeup_source *ws;
	void *ilc;

	struct xarray no_wake_svc; /* services that will not wake up APPS */
};

struct qrtr_tx_flow_waiter {
	struct list_head node;
	struct sock *sk;
};

/**
 * struct qrtr_tx_flow - tx flow control
 * @resume_tx: waiters for a resume tx from the remote
 * @pending: number of waiting senders
 * @tx_failed: indicates that a message with confirm_rx flag was lost
 * @waiters: list of ports to notify when this flow resumes
 * @lock: lock to protect flow variables
 */
struct qrtr_tx_flow {
	struct wait_queue_head resume_tx;
	int pending;
	int tx_failed;
	struct list_head waiters;
	/* protect above flow variables */
	spinlock_t lock;
};

#define QRTR_TX_FLOW_HIGH	10
#define QRTR_TX_FLOW_LOW	5

static int qrtr_local_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to, unsigned int flags);
static int qrtr_bcast_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to, unsigned int flags);
static struct qrtr_sock *qrtr_port_lookup(int port);
static void qrtr_port_put(struct qrtr_sock *ipc);
static void qrtr_handle_del_proc(struct qrtr_node *node, struct sk_buff *skb);

static void qrtr_log_tx_msg(struct qrtr_node *node, struct qrtr_hdr_v1 *hdr,
			    struct sk_buff *skb)
{
	struct qrtr_ctrl_pkt pkt = {0,};
	u64 pl_buf = 0;
	int type;

	if (!hdr || !skb)
		return;

	type = le32_to_cpu(hdr->type);
	if (type == QRTR_TYPE_DATA) {
		skb_copy_bits(skb, QRTR_HDR_MAX_SIZE, &pl_buf, sizeof(pl_buf));
		QRTR_INFO(node->ilc,
			  "TX DATA: Len:0x%x CF:0x%x src[0x%x:0x%x] dst[0x%x:0x%x] [%08x %08x] [%s]\n",
			  hdr->size, hdr->confirm_rx,
			  hdr->src_node_id, hdr->src_port_id,
			  hdr->dst_node_id, hdr->dst_port_id,
			  (unsigned int)pl_buf, (unsigned int)(pl_buf >> 32),
			  current->comm);
	} else {
		skb_copy_bits(skb, QRTR_HDR_MAX_SIZE, &pkt, sizeof(pkt));
		if (type == QRTR_TYPE_NEW_SERVER ||
		    type == QRTR_TYPE_DEL_SERVER)
			QRTR_INFO(node->ilc,
				  "TX CTRL: cmd:0x%x SVC[0x%x:0x%x] addr[0x%x:0x%x]\n",
				  type, le32_to_cpu(pkt.server.service),
				  le32_to_cpu(pkt.server.instance),
				  le32_to_cpu(pkt.server.node),
				  le32_to_cpu(pkt.server.port));
		else if (type == QRTR_TYPE_DEL_CLIENT ||
			 type == QRTR_TYPE_RESUME_TX)
			QRTR_INFO(node->ilc,
				  "TX CTRL: cmd:0x%x addr[0x%x:0x%x]\n",
				  type, le32_to_cpu(pkt.client.node),
				  le32_to_cpu(pkt.client.port));
		else if (type == QRTR_TYPE_HELLO ||
			 type == QRTR_TYPE_BYE)
			QRTR_INFO(node->ilc,
				  "TX CTRL: cmd:0x%x node[0x%x]\n",
				  type, hdr->src_node_id);
		else if (type == QRTR_TYPE_DEL_PROC)
			QRTR_INFO(node->ilc,
				  "TX CTRL: cmd:0x%x node[0x%x]\n",
				  type, pkt.proc.node);
	}
}

static void qrtr_log_rx_msg(struct qrtr_node *node, struct sk_buff *skb)
{
	struct qrtr_ctrl_pkt pkt = {0,};
	struct qrtr_cb *cb;
	u64 pl_buf = 0;

	if (!skb)
		return;

	cb = (struct qrtr_cb *)skb->cb;

	if (cb->type == QRTR_TYPE_DATA) {
		skb_copy_bits(skb, 0, &pl_buf, sizeof(pl_buf));
		QRTR_INFO(node->ilc,
			  "RX DATA: Len:0x%x CF:0x%x src[0x%x:0x%x] dst[0x%x:0x%x] [%08x %08x]\n",
			  skb->len, cb->confirm_rx, cb->src_node, cb->src_port,
			  cb->dst_node, cb->dst_port,
			  (unsigned int)pl_buf, (unsigned int)(pl_buf >> 32));
	} else {
		skb_copy_bits(skb, 0, &pkt, sizeof(pkt));
		if (cb->type == QRTR_TYPE_NEW_SERVER ||
		    cb->type == QRTR_TYPE_DEL_SERVER)
			QRTR_INFO(node->ilc,
				  "RX CTRL: cmd:0x%x SVC[0x%x:0x%x] addr[0x%x:0x%x]\n",
				  cb->type, le32_to_cpu(pkt.server.service),
				  le32_to_cpu(pkt.server.instance),
				  le32_to_cpu(pkt.server.node),
				  le32_to_cpu(pkt.server.port));
		else if (cb->type == QRTR_TYPE_DEL_CLIENT ||
			 cb->type == QRTR_TYPE_RESUME_TX)
			QRTR_INFO(node->ilc,
				  "RX CTRL: cmd:0x%x addr[0x%x:0x%x]\n",
				  cb->type, le32_to_cpu(pkt.client.node),
				  le32_to_cpu(pkt.client.port));
		else if (cb->type == QRTR_TYPE_HELLO ||
			 cb->type == QRTR_TYPE_BYE)
			QRTR_INFO(node->ilc,
				  "RX CTRL: cmd:0x%x node[0x%x]\n",
				  cb->type, cb->src_node);
		else if (cb->type == QRTR_TYPE_DEL_PROC)
			QRTR_INFO(node->ilc,
				  "RX CTRL: cmd:0x%x node[0x%x]\n",
				  cb->type, le32_to_cpu(pkt.proc.node));
	}
}

void qrtr_print_wakeup_reason(const void *data)
{
	const struct qrtr_hdr_v1 *v1;
	const struct qrtr_hdr_v2 *v2;
	struct qrtr_cb cb;
	unsigned int size;
	unsigned int ver;
	int service_id;
	size_t hdrlen;
	u64 preview = 0;

	ver = *(u8 *)data;
	switch (ver) {
	case QRTR_PROTO_VER_1:
		v1 = data;
		hdrlen = sizeof(*v1);
		cb.src_node = le32_to_cpu(v1->src_node_id);
		cb.src_port = le32_to_cpu(v1->src_port_id);
		cb.dst_node = le32_to_cpu(v1->dst_node_id);
		cb.dst_port = le32_to_cpu(v1->dst_port_id);

		size = le32_to_cpu(v1->size);
		break;
	case QRTR_PROTO_VER_2:
		v2 = data;
		hdrlen = sizeof(*v2) + v2->optlen;
		cb.src_node = le16_to_cpu(v2->src_node_id);
		cb.src_port = le16_to_cpu(v2->src_port_id);
		cb.dst_node = le16_to_cpu(v2->dst_node_id);
		cb.dst_port = le16_to_cpu(v2->dst_port_id);

		if (cb.src_port == (u16)QRTR_PORT_CTRL)
			cb.src_port = QRTR_PORT_CTRL;
		if (cb.dst_port == (u16)QRTR_PORT_CTRL)
			cb.dst_port = QRTR_PORT_CTRL;

		size = le32_to_cpu(v2->size);
		break;
	default:
		return;
	}

	service_id = qrtr_get_service_id(cb.src_node, cb.src_port);
	if (service_id < 0)
		service_id = qrtr_get_service_id(cb.dst_node, cb.dst_port);

	size = (sizeof(preview) > size) ? size : sizeof(preview);
	memcpy(&preview, data + hdrlen, size);

	pr_info("%s: src[0x%x:0x%x] dst[0x%x:0x%x] [%08x %08x] service[0x%x]\n",
		__func__,
		cb.src_node, cb.src_port,
		cb.dst_node, cb.dst_port,
		(unsigned int)preview, (unsigned int)(preview >> 32),
		service_id);
}
EXPORT_SYMBOL(qrtr_print_wakeup_reason);

static bool refcount_dec_and_rwsem_lock(refcount_t *r,
					struct rw_semaphore *sem)
{
	if (refcount_dec_not_one(r))
		return false;

	down_write(sem);
	if (!refcount_dec_and_test(r)) {
		up_write(sem);
		return false;
	}

	return true;
}

static inline int kref_put_rwsem_lock(struct kref *kref,
				      void (*release)(struct kref *kref),
				      struct rw_semaphore *sem)
{
	if (refcount_dec_and_rwsem_lock(&kref->refcount, sem)) {
		release(kref);
		return 1;
	}
	return 0;
}

/* Release node resources and free the node.
 *
 * Do not call directly, use qrtr_node_release.  To be used with
 * kref_put_mutex.  As such, the node mutex is expected to be locked on call.
 */
static void __qrtr_node_release(struct kref *kref)
{
	struct qrtr_node *node = container_of(kref, struct qrtr_node, ref);
	struct qrtr_tx_flow_waiter *waiter;
	struct qrtr_tx_flow_waiter *temp;
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
	up_write(&qrtr_epts_lock);

	kthread_flush_worker(&node->kworker);
	kthread_stop(node->task);
	skb_queue_purge(&node->rx_queue);
	wakeup_source_unregister(node->ws);
	xa_destroy(&node->no_wake_svc);

	/* Free tx flow counters */
	mutex_lock(&node->qrtr_tx_lock);
	radix_tree_for_each_slot(slot, &node->qrtr_tx_flow, &iter, 0) {
		flow = *slot;
		list_for_each_entry_safe(waiter, temp, &flow->waiters, node) {
			list_del(&waiter->node);
			sock_put(waiter->sk);
			kfree(waiter);
		}
		radix_tree_iter_delete(&node->qrtr_tx_flow, &iter, slot);
		kfree(flow);
	}
	mutex_unlock(&node->qrtr_tx_lock);

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
	kref_put_rwsem_lock(&node->ref, __qrtr_node_release, &qrtr_epts_lock);
}

/**
 * qrtr_tx_resume() - reset flow control counter
 * @node:	qrtr_node that the QRTR_TYPE_RESUME_TX packet arrived on
 * @skb:	resume_tx packet
 */
static void qrtr_tx_resume(struct qrtr_node *node, struct sk_buff *skb)
{
	struct qrtr_tx_flow_waiter *waiter;
	struct qrtr_tx_flow_waiter *temp;
	struct qrtr_ctrl_pkt pkt = {0,};
	struct qrtr_tx_flow *flow;
	struct sockaddr_qrtr src;
	struct qrtr_sock *ipc;
	struct sk_buff *skbn;
	unsigned long flags;
	unsigned long key;

	skb_copy_bits(skb, 0, &pkt, sizeof(pkt));
	if (le32_to_cpu(pkt.cmd) != QRTR_TYPE_RESUME_TX)
		return;

	src.sq_family = AF_QIPCRTR;
	src.sq_node = le32_to_cpu(pkt.client.node);
	src.sq_port = le32_to_cpu(pkt.client.port);
	key = (u64)src.sq_node << 32 | src.sq_port;

	mutex_lock(&node->qrtr_tx_lock);
	flow = radix_tree_lookup(&node->qrtr_tx_flow, key);
	mutex_unlock(&node->qrtr_tx_lock);
	if (!flow)
		return;

	spin_lock_irqsave(&flow->lock, flags);
	flow->pending = 0;
	wake_up_interruptible_all(&flow->resume_tx);

	list_for_each_entry_safe(waiter, temp, &flow->waiters, node) {
		list_del(&waiter->node);

		skbn = alloc_skb(0, GFP_ATOMIC);
		if (skbn) {
			ipc = qrtr_sk(waiter->sk);
			qrtr_local_enqueue(NULL, skbn, QRTR_TYPE_RESUME_TX,
					   &src, &ipc->us, 0);
		}
		sock_put(waiter->sk);
		kfree(waiter);
	}
	spin_unlock_irqrestore(&flow->lock, flags);

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
static int qrtr_tx_wait(struct qrtr_node *node, struct sockaddr_qrtr *to,
			struct sock *sk, int type, unsigned int flags)
{
	unsigned long key = (u64)to->sq_node << 32 | to->sq_port;
	struct qrtr_tx_flow_waiter *waiter;
	struct qrtr_tx_flow *flow;
	int confirm_rx = 0;
	long timeo;
	long ret;

	/* Never set confirm_rx on non-data packets */
	if (type != QRTR_TYPE_DATA)
		return 0;

	/* Assume sk is set correctly for all data type packets */
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);

	mutex_lock(&node->qrtr_tx_lock);
	flow = radix_tree_lookup(&node->qrtr_tx_flow, key);
	if (!flow) {
		flow = kzalloc(sizeof(*flow), GFP_KERNEL);
		if (flow) {
			INIT_LIST_HEAD(&flow->waiters);
			init_waitqueue_head(&flow->resume_tx);
			spin_lock_init(&flow->lock);
			if (radix_tree_insert(&node->qrtr_tx_flow, key, flow)) {
				kfree(flow);
				flow = NULL;
			}
		}
	}
	mutex_unlock(&node->qrtr_tx_lock);

	/* Set confirm_rx if we where unable to find and allocate a flow */
	if (!flow)
		return 1;

	spin_lock_irq(&flow->lock);
	ret = wait_event_interruptible_lock_irq_timeout(flow->resume_tx,
							flow->pending < QRTR_TX_FLOW_HIGH ||
							flow->tx_failed ||
							!node->ep,
							flow->lock,
							timeo);
	if (ret < 0) {
		confirm_rx = ret;
	} else if (!node->ep) {
		confirm_rx = -EPIPE;
	} else if (flow->tx_failed) {
		flow->tx_failed = 0;
		confirm_rx = 1;
	} else if (!ret && flow->pending >= QRTR_TX_FLOW_HIGH) {
		list_for_each_entry(waiter, &flow->waiters, node) {
			if (waiter->sk == sk) {
				spin_unlock_irq(&flow->lock);
				return -EAGAIN;
			}
		}

		waiter = kzalloc(sizeof(*waiter), GFP_ATOMIC);
		if (!waiter) {
			spin_unlock_irq(&flow->lock);
			return -ENOMEM;
		}
		waiter->sk = sk;
		sock_hold(sk);
		list_add_tail(&waiter->node, &flow->waiters);

		confirm_rx = -EAGAIN;
		QRTR_INFO(node->ilc, "new waiter %s[%d] for [0x%x:0x%x]\n",
			  current->comm, current->pid,
			  to->sq_node, to->sq_port);
	} else {
		flow->pending++;
		confirm_rx = flow->pending == QRTR_TX_FLOW_LOW;
	}
	spin_unlock_irq(&flow->lock);

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

	mutex_lock(&node->qrtr_tx_lock);
	flow = radix_tree_lookup(&node->qrtr_tx_flow, key);
	mutex_unlock(&node->qrtr_tx_lock);
	if (flow) {
		spin_lock_irq(&flow->lock);
		flow->tx_failed = 1;
		spin_unlock_irq(&flow->lock);
	}
}

static int qrtr_pad_word_pskb(struct sk_buff *skb)
{
	unsigned int padding_len;
	unsigned int padto;
	int nfrags;
	int count;
	int i;

	padto = ALIGN(skb->len, 4);
	padding_len = padto - skb->len;
	if (!padding_len)
		return 0;

	count = skb_headlen(skb);
	nfrags = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < nfrags; i++) {
		u32 p_off, p_len, copied;
		u32 f_off, f_len;
		u32 d_off, d_len;
		skb_frag_t *frag;
		struct page *p;
		u8 *vaddr;

		frag = &skb_shinfo(skb)->frags[i];
		f_off = skb_frag_off(frag);
		f_len = skb_frag_size(frag);
		if (count + f_len < skb->len) {
			count += f_len;
			continue;
		}

		/* fragment can fit all padding */
		if (count + f_len >= padto) {
			skb_frag_foreach_page(frag, f_off, f_len, p, p_off,
					      p_len, copied) {
				if (count + p_len < padto) {
					count += p_len;
					continue;
				}

				d_off = skb->len - count;
				vaddr = kmap_atomic(p);
				memset(vaddr + p_off + d_off, 0, padding_len);
				kunmap_atomic(vaddr);
				count += d_off + padding_len;
				skb->len = padto;
				skb->data_len += padding_len;
				break;
			}
		} else {
			 /* messy case, padding split between pages */
			skb_frag_foreach_page(frag, f_off, f_len, p, p_off,
					      p_len, copied) {
				if (count + p_len < skb->len) {
					count += p_len;
					continue;
				}

				/* need to add padding into next page */
				if (count + p_len < padto) {
					d_off = skb->len - count;
					d_len = p_len - d_off;

					vaddr = kmap_atomic(p);
					memset(vaddr + p_off + d_off, 0, d_len);
					kunmap_atomic(vaddr);

					count += p_len;
					padding_len -= d_len;
					skb->len += d_len;
					skb->data_len += padding_len;
					continue;
				}

				d_off = (count < skb->len) ? skb->len - count : 0;
				vaddr = kmap_atomic(p);
				memset(vaddr + p_off + d_off, 0, padding_len);
				kunmap_atomic(vaddr);
				count += d_off + padding_len;
				skb->len += padding_len;
				skb->data_len += padding_len;
			}
		}

		if (skb->len == padto)
			break;
	}
	WARN_ON(skb->len != padto);

	return 0;
}

/* Pass an outgoing packet socket buffer to the endpoint driver. */
static int qrtr_node_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			     int type, struct sockaddr_qrtr *from,
			     struct sockaddr_qrtr *to, unsigned int flags)
{
	struct qrtr_hdr_v1 *hdr;
	size_t len = skb->len;
	int rc, confirm_rx;

	mutex_lock(&node->ep_lock);
	if (!atomic_read(&node->hello_sent) && type != QRTR_TYPE_HELLO) {
		kfree_skb(skb);
		mutex_unlock(&node->ep_lock);
		return 0;
	}
	if (atomic_read(&node->hello_sent) && type == QRTR_TYPE_HELLO) {
		kfree_skb(skb);
		mutex_unlock(&node->ep_lock);
		return 0;
	}

	if (!atomic_read(&node->hello_sent) && type == QRTR_TYPE_HELLO)
		atomic_inc(&node->hello_sent);

	mutex_unlock(&node->ep_lock);

	/* If sk is null, this is a forwarded packet and should not wait */
	if (!skb->sk) {
		struct qrtr_cb *cb = (struct qrtr_cb *)skb->cb;

		confirm_rx = cb->confirm_rx;
	} else {
		confirm_rx = qrtr_tx_wait(node, to, skb->sk, type, flags);
		if (confirm_rx < 0) {
			kfree_skb(skb);
			return confirm_rx;
		}
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

	qrtr_log_tx_msg(node, hdr, skb);
	/* word align the data and pad with 0s */
	if (skb_is_nonlinear(skb))
		rc = qrtr_pad_word_pskb(skb);
	else
		rc = skb_put_padto(skb, ALIGN(len, 4) + sizeof(*hdr));

	if (rc) {
		pr_err("%s: failed to pad size %lu to %lu rc:%d\n", __func__,
		       skb->len, ALIGN(skb->len, 4), rc);
	}

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
	if (rc && type == QRTR_TYPE_HELLO) {
		atomic_dec(&node->hello_sent);
		kthread_queue_work(&node->kworker, &node->say_hello);
	}

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

	down_read(&qrtr_epts_lock);

	spin_lock_irqsave(&qrtr_nodes_lock, flags);
	node = radix_tree_lookup(&qrtr_nodes, nid);
	node = qrtr_node_acquire(node);
	spin_unlock_irqrestore(&qrtr_nodes_lock, flags);
	up_read(&qrtr_epts_lock);

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

	if (nid == node->nid || nid == QRTR_EP_NID_AUTO)
		return;

	spin_lock_irqsave(&qrtr_nodes_lock, flags);
	if (!radix_tree_lookup(&qrtr_nodes, nid))
		radix_tree_insert(&qrtr_nodes, nid, node);

	if (node->nid == QRTR_EP_NID_AUTO)
		node->nid = nid;
	spin_unlock_irqrestore(&qrtr_nodes_lock, flags);
}

/**
 * qrtr_peek_pkt_size() - Peek into the packet header to get potential pkt size
 *
 * @data: Starting address of the packet which points to router header.
 *
 * @returns: potential packet size on success, < 0 on error.
 *
 * This function is used by the underlying transport abstraction layer to
 * peek into the potential packet size of an incoming packet. This information
 * is used to perform link layer fragmentation and re-assembly
 */
int qrtr_peek_pkt_size(const void *data)
{
	const struct qrtr_hdr_v1 *v1;
	const struct qrtr_hdr_v2 *v2;
	unsigned int hdrlen;
	unsigned int size;
	unsigned int ver;

	/* Version field in v1 is little endian, so this works for both cases */
	ver = *(u8 *)data;

	switch (ver) {
	case QRTR_PROTO_VER_1:
		v1 = data;
		hdrlen = sizeof(*v1);
		size = le32_to_cpu(v1->size);
		break;
	case QRTR_PROTO_VER_2:
		v2 = data;
		hdrlen = sizeof(*v2) + v2->optlen;
		size = le32_to_cpu(v2->size);
		break;
	default:
		pr_err("qrtr: Invalid version %d\n", ver);
		return -EINVAL;
	}

	return ALIGN(size, 4) + hdrlen;
}
EXPORT_SYMBOL(qrtr_peek_pkt_size);

static void qrtr_alloc_backup(struct work_struct *work)
{
	struct sk_buff *skb;
	int errcode;

	while (skb_queue_len(&qrtr_backup_lo) < QRTR_BACKUP_LO_NUM) {
		skb = alloc_skb_with_frags(sizeof(struct qrtr_hdr_v1),
					   QRTR_BACKUP_LO_SIZE, 0, &errcode,
					   GFP_KERNEL);
		if (!skb)
			break;
		skb_queue_tail(&qrtr_backup_lo, skb);
	}
	while (skb_queue_len(&qrtr_backup_hi) < QRTR_BACKUP_HI_NUM) {
		skb = alloc_skb_with_frags(sizeof(struct qrtr_hdr_v1),
					   QRTR_BACKUP_HI_SIZE, 0, &errcode,
					   GFP_KERNEL);
		if (!skb)
			break;
		skb_queue_tail(&qrtr_backup_hi, skb);
	}
}

static struct sk_buff *qrtr_get_backup(size_t len)
{
	struct sk_buff *skb = NULL;

	if (len < QRTR_BACKUP_LO_SIZE)
		skb = skb_dequeue(&qrtr_backup_lo);
	else if (len < QRTR_BACKUP_HI_SIZE)
		skb = skb_dequeue(&qrtr_backup_hi);

	if (skb)
		queue_work(system_unbound_wq, &qrtr_backup_work);

	return skb;
}

static void qrtr_backup_init(void)
{
	skb_queue_head_init(&qrtr_backup_lo);
	skb_queue_head_init(&qrtr_backup_hi);
	INIT_WORK(&qrtr_backup_work, qrtr_alloc_backup);
	queue_work(system_unbound_wq, &qrtr_backup_work);
}

static void qrtr_backup_deinit(void)
{
	cancel_work_sync(&qrtr_backup_work);
	skb_queue_purge(&qrtr_backup_lo);
	skb_queue_purge(&qrtr_backup_hi);
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
	size_t size;
	unsigned int ver;
	size_t hdrlen;
	int errcode;
	int svc_id;

	if (len == 0 || len & 3)
		return -EINVAL;

	skb = alloc_skb_with_frags(sizeof(*v1), len, 0, &errcode, GFP_ATOMIC);
	if (!skb) {
		skb = qrtr_get_backup(len);
		if (!skb) {
			pr_err("qrtr: Unable to get skb with len:%lu\n", len);
			return -ENOMEM;
		}
	}

	skb_reserve(skb, sizeof(*v1));
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

	if (cb->dst_port == QRTR_PORT_CTRL_LEGACY)
		cb->dst_port = QRTR_PORT_CTRL;

	if (!size || len != ALIGN(size, 4) + hdrlen)
		goto err;

	if ((cb->type == QRTR_TYPE_NEW_SERVER ||
	     cb->type == QRTR_TYPE_RESUME_TX) &&
	    size < sizeof(struct qrtr_ctrl_pkt))
		goto err;

	if (cb->dst_port != QRTR_PORT_CTRL && cb->type != QRTR_TYPE_DATA &&
	    cb->type != QRTR_TYPE_RESUME_TX)
		goto err;

	skb->data_len = size;
	skb->len = size;
	skb_store_bits(skb, 0, data + hdrlen, size);

	qrtr_node_assign(node, cb->src_node);

	if (cb->type == QRTR_TYPE_NEW_SERVER) {
		/* Remote node endpoint can bridge other distant nodes */
		const struct qrtr_ctrl_pkt *pkt;

		pkt = data + hdrlen;
		qrtr_node_assign(node, le32_to_cpu(pkt->server.node));
	}

	qrtr_log_rx_msg(node, skb);
	/* All control packets and non-local destined data packets should be
	 * queued to the worker for forwarding handling.
	 */
	svc_id = qrtr_get_service_id(cb->src_node, cb->src_port);
	if (cb->type != QRTR_TYPE_DATA || cb->dst_node != qrtr_local_nid) {
		skb_queue_tail(&node->rx_queue, skb);
		kthread_queue_work(&node->kworker, &node->read_data);
		pm_wakeup_ws_event(node->ws, qrtr_wakeup_ms, true);
	} else {
		ipc = qrtr_port_lookup(cb->dst_port);
		if (!ipc) {
			kfree_skb(skb);
			return -ENODEV;
		}

		if (sock_queue_rcv_skb(&ipc->sk, skb)) {
			qrtr_port_put(ipc);
			goto err;
		}

		/* Force wakeup based on services */
		if (!xa_load(&node->no_wake_svc, svc_id))
			pm_wakeup_ws_event(node->ws, qrtr_wakeup_ms, true);

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

static bool qrtr_must_forward(struct qrtr_node *src,
			      struct qrtr_node *dst, u32 type)
{
	/* Node structure is not maintained for local processor.
	 * Hence src is null in that case.
	 */
	if (!src)
		return true;

	if (!dst)
		return false;

	if (type == QRTR_TYPE_HELLO || type == QRTR_TYPE_RESUME_TX)
		return false;

	if (dst == src || dst->nid == QRTR_EP_NID_AUTO)
		return false;

	if (abs(dst->net_id - src->net_id) > 1)
		return true;

	return false;
}

static void qrtr_fwd_ctrl_pkt(struct qrtr_node *src, struct sk_buff *skb)
{
	struct qrtr_node *node;
	struct qrtr_cb *cb = (struct qrtr_cb *)skb->cb;

	down_read(&qrtr_epts_lock);
	list_for_each_entry(node, &qrtr_all_epts, item) {
		struct sockaddr_qrtr from;
		struct sockaddr_qrtr to;
		struct sk_buff *skbn;

		if (!qrtr_must_forward(src, node, cb->type))
			continue;

		skbn = skb_clone(skb, GFP_KERNEL);
		if (!skbn)
			break;

		from.sq_family = AF_QIPCRTR;
		from.sq_node = cb->src_node;
		from.sq_port = cb->src_port;

		to.sq_family = AF_QIPCRTR;
		to.sq_node = node->nid;
		to.sq_port = QRTR_PORT_CTRL;

		qrtr_node_enqueue(node, skbn, cb->type, &from, &to, 0);
	}
	up_read(&qrtr_epts_lock);
}

static void qrtr_fwd_pkt(struct sk_buff *skb, struct qrtr_cb *cb)
{
	struct sockaddr_qrtr from = {AF_QIPCRTR, cb->src_node, cb->src_port};
	struct sockaddr_qrtr to = {AF_QIPCRTR, cb->dst_node, cb->dst_port};
	struct qrtr_node *node;

	node = qrtr_node_lookup(cb->dst_node);
	if (!node) {
		kfree_skb(skb);
		return;
	}

	qrtr_node_enqueue(node, skb, cb->type, &from, &to, 0);
	qrtr_node_release(node);
}

static int qrtr_sock_queue_ctrl_skb(struct qrtr_sock *ipc, struct sk_buff *skb)
{
	unsigned long flags;
	int rc;

	while (1) {
		rc = sock_queue_rcv_skb(&ipc->sk, skb);
		if (rc == -ENOMEM || rc == -ENOBUFS) {
			spin_lock_irqsave(&ipc->signal_lock, flags);
			reinit_completion(&ipc->rx_queue_has_space);
			ipc->signal_on_recv = true;
			spin_unlock_irqrestore(&ipc->signal_lock, flags);
			wait_for_completion(&ipc->rx_queue_has_space);
		} else {
			return rc;
		}
	}

	return 0;
}

static void qrtr_sock_queue_skb(struct qrtr_node *node, struct sk_buff *skb,
				struct qrtr_sock *ipc)
{
	struct qrtr_cb *cb = (struct qrtr_cb *)skb->cb;
	int rc;

	/* Don't queue HELLO if control port already received */
	if (cb->type == QRTR_TYPE_HELLO) {
		if (atomic_read(&node->hello_rcvd)) {
			kfree_skb(skb);
			return;
		}
		atomic_inc(&node->hello_rcvd);
	}

	rc = (ipc->us.sq_port == QRTR_PORT_CTRL) ?
		qrtr_sock_queue_ctrl_skb(ipc, skb) :
		sock_queue_rcv_skb(&ipc->sk, skb);
	if (rc) {
		pr_err("%s: qrtr pkt dropped flow[%d] rc[%d]\n",
		       __func__, cb->confirm_rx, rc);
		kfree_skb(skb);
	}
}

/* Handle not atomic operations for a received packet. */
static void qrtr_node_rx_work(struct kthread_work *work)
{
	struct qrtr_node *node = container_of(work, struct qrtr_node,
					      read_data);
	struct sk_buff *skb;
	char name[32] = {0,};

	if (unlikely(!node->ilc)) {
		snprintf(name, sizeof(name), "qrtr_%d", node->nid);
		node->ilc = ipc_log_context_create(QRTR_LOG_PAGE_CNT, name, 0);
	}

	while ((skb = skb_dequeue(&node->rx_queue)) != NULL) {
		struct qrtr_cb *cb = (struct qrtr_cb *)skb->cb;
		struct qrtr_sock *ipc;

		if (cb->type != QRTR_TYPE_DATA)
			qrtr_fwd_ctrl_pkt(node, skb);

		if (cb->type == QRTR_TYPE_RESUME_TX) {
			if (cb->dst_node != qrtr_local_nid) {
				qrtr_fwd_pkt(skb, cb);
				continue;
			}
			qrtr_tx_resume(node, skb);
		} else if (cb->dst_node != qrtr_local_nid &&
			   cb->type == QRTR_TYPE_DATA) {
			qrtr_fwd_pkt(skb, cb);
		} else if (cb->type == QRTR_TYPE_DEL_PROC) {
			qrtr_handle_del_proc(node, skb);
		} else {
			ipc = qrtr_port_lookup(cb->dst_port);
			if (!ipc) {
				kfree_skb(skb);
			} else {
				qrtr_sock_queue_skb(node, skb, ipc);
				qrtr_port_put(ipc);
			}
		}
	}
}

static void qrtr_handle_del_proc(struct qrtr_node *node, struct sk_buff *skb)
{
	struct sockaddr_qrtr src = {AF_QIPCRTR, 0, QRTR_PORT_CTRL};
	struct sockaddr_qrtr dst = {AF_QIPCRTR, qrtr_local_nid, QRTR_PORT_CTRL};
	struct qrtr_ctrl_pkt pkt = {0,};
	struct qrtr_tx_flow_waiter *waiter;
	struct qrtr_tx_flow_waiter *temp;
	struct radix_tree_iter iter;
	struct qrtr_tx_flow *flow;
	unsigned long node_id;
	void __rcu **slot;

	skb_copy_bits(skb, 0, &pkt, sizeof(pkt));
	src.sq_node = le32_to_cpu(pkt.proc.node);
	/* Free tx flow counters */
	mutex_lock(&node->qrtr_tx_lock);
	radix_tree_for_each_slot(slot, &node->qrtr_tx_flow, &iter, 0) {
		flow = rcu_dereference(*slot);
		/* extract node id from the index key */
		node_id = (iter.index & 0xFFFFFFFF00000000) >> 32;
		if (node_id != src.sq_node)
			continue;
		list_for_each_entry_safe(waiter, temp, &flow->waiters, node) {
			list_del(&waiter->node);
			sock_put(waiter->sk);
			kfree(waiter);
		}
		kfree(flow);
		radix_tree_delete(&node->qrtr_tx_flow, iter.index);
	}
	mutex_unlock(&node->qrtr_tx_lock);

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_BYE);
	skb_store_bits(skb, 0, &pkt, sizeof(pkt));
	qrtr_local_enqueue(NULL, skb, QRTR_TYPE_BYE, &src, &dst, 0);
}

static void qrtr_hello_work(struct kthread_work *work)
{
	struct sockaddr_qrtr from = {AF_QIPCRTR, 0, QRTR_PORT_CTRL};
	struct sockaddr_qrtr to = {AF_QIPCRTR, 0, QRTR_PORT_CTRL};
	struct qrtr_ctrl_pkt *pkt;
	struct qrtr_node *node;
	struct qrtr_sock *ctrl;
	struct sk_buff *skb;

	ctrl = qrtr_port_lookup(QRTR_PORT_CTRL);
	if (!ctrl)
		return;

	skb = qrtr_alloc_ctrl_packet(&pkt, GFP_KERNEL);
	if (!skb) {
		qrtr_port_put(ctrl);
		return;
	}

	node = container_of(work, struct qrtr_node, say_hello);
	pkt->cmd = cpu_to_le32(QRTR_TYPE_HELLO);
	from.sq_node = qrtr_local_nid;
	to.sq_node = node->nid;
	qrtr_node_enqueue(node, skb, QRTR_TYPE_HELLO, &from, &to, 0);
	qrtr_port_put(ctrl);
}

/**
 * qrtr_endpoint_register() - register a new endpoint
 * @ep: endpoint to register
 * @nid: desired node id; may be QRTR_EP_NID_AUTO for auto-assignment
 * @rt: flag to notify real time low latency endpoint
 * @no_wake: array of services to not wake up
 * Return: 0 on success; negative error code on failure
 *
 * The specified endpoint must have the xmit function pointer set on call.
 */
int qrtr_endpoint_register(struct qrtr_endpoint *ep, unsigned int net_id,
			   bool rt, struct qrtr_array *no_wake)
{
	int rc, i;
	size_t size;
	struct qrtr_node *node;
	struct sched_param param = {.sched_priority = 1};

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
	atomic_set(&node->hello_sent, 0);
	atomic_set(&node->hello_rcvd, 0);

	kthread_init_work(&node->read_data, qrtr_node_rx_work);
	kthread_init_work(&node->say_hello, qrtr_hello_work);
	kthread_init_worker(&node->kworker);
	node->task = kthread_run(kthread_worker_fn, &node->kworker, "qrtr_rx");
	if (IS_ERR(node->task)) {
		kfree(node);
		return -ENOMEM;
	}
	if (rt)
		sched_setscheduler(node->task, SCHED_FIFO, &param);

	xa_init(&node->no_wake_svc);
	size = no_wake ? no_wake->size : 0;
	for (i = 0; i < size; i++) {
		rc = xa_insert(&node->no_wake_svc, no_wake->arr[i], node,
			       GFP_KERNEL);
		if (rc) {
			kfree(node);
			return rc;
		}
	}

	INIT_RADIX_TREE(&node->qrtr_tx_flow, GFP_KERNEL);
	mutex_init(&node->qrtr_tx_lock);

	qrtr_node_assign(node, node->nid);
	node->net_id = net_id;

	down_write(&qrtr_epts_lock);
	list_add(&node->item, &qrtr_all_epts);
	up_write(&qrtr_epts_lock);
	ep->node = node;

	node->ws = wakeup_source_register(NULL, "qrtr_ws");

	kthread_queue_work(&node->kworker, &node->say_hello);
	return 0;
}
EXPORT_SYMBOL_GPL(qrtr_endpoint_register);

static u32 qrtr_calc_checksum(struct qrtr_ctrl_pkt *pkt)
{
	u32 checksum = 0;
	u32 mask = 0xffff;
	u16 upper_nb;
	u16 lower_nb;
	u32 *msg;
	int i;

	if (!pkt)
		return checksum;
	msg = (u32 *)pkt;

	for (i = 0; i < sizeof(*pkt) / sizeof(*msg); i++) {
		lower_nb = *msg & mask;
		upper_nb = (*msg >> 16) & mask;
		checksum += (upper_nb + lower_nb);
		msg++;
	}
	while (checksum > 0xffff)
		checksum = (checksum & mask) + ((checksum >> 16) & mask);

	checksum = ~checksum & mask;

	return checksum;
}

static void qrtr_fwd_del_proc(struct qrtr_node *src, unsigned int nid)
{
	struct sockaddr_qrtr from = {AF_QIPCRTR, 0, QRTR_PORT_CTRL};
	struct sockaddr_qrtr to = {AF_QIPCRTR, 0, QRTR_PORT_CTRL};
	struct qrtr_ctrl_pkt *pkt;
	struct qrtr_node *dst;
	struct sk_buff *skb;

	down_read(&qrtr_epts_lock);
	list_for_each_entry(dst, &qrtr_all_epts, item) {
		if (!qrtr_must_forward(src, dst, QRTR_TYPE_DEL_PROC))
			continue;

		skb = qrtr_alloc_ctrl_packet(&pkt, GFP_KERNEL);
		if (!skb)
			return;

		pkt->cmd = cpu_to_le32(QRTR_TYPE_DEL_PROC);
		pkt->proc.rsvd = QRTR_DEL_PROC_MAGIC;
		pkt->proc.node = cpu_to_le32(nid);
		pkt->proc.rsvd = cpu_to_le32(qrtr_calc_checksum(pkt));

		from.sq_node = src->nid;
		to.sq_node = dst->nid;
		qrtr_node_enqueue(dst, skb, QRTR_TYPE_DEL_PROC, &from, &to, 0);
	}
	up_read(&qrtr_epts_lock);
}

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
		spin_unlock_irqrestore(&qrtr_nodes_lock, flags);
		skb = qrtr_alloc_ctrl_packet(&pkt, GFP_KERNEL);
		if (skb) {
			pkt->cmd = cpu_to_le32(QRTR_TYPE_BYE);
			qrtr_local_enqueue(NULL, skb, QRTR_TYPE_BYE, &src, &dst, 0);
		}
		qrtr_fwd_del_proc(node, iter.index);
		spin_lock_irqsave(&qrtr_nodes_lock, flags);
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
	unsigned long flags;

	if (port == QRTR_PORT_CTRL)
		port = 0;

	spin_lock_irqsave(&qrtr_port_lock, flags);
	ipc = xa_load(&qrtr_ports, port);
	if (ipc)
		sock_hold(&ipc->sk);
	spin_unlock_irqrestore(&qrtr_port_lock, flags);

	return ipc;
}

/* Release acquired socket. */
static void qrtr_port_put(struct qrtr_sock *ipc)
{
	sock_put(&ipc->sk);
}

static void qrtr_send_del_client(struct qrtr_sock *ipc)
{
	struct qrtr_ctrl_pkt *pkt;
	struct sockaddr_qrtr to;
	struct qrtr_node *node;
	struct sk_buff *skbn;
	struct sk_buff *skb;
	int type = QRTR_TYPE_DEL_CLIENT;

	skb = qrtr_alloc_ctrl_packet(&pkt, GFP_KERNEL);
	if (!skb)
		return;

	to.sq_family = AF_QIPCRTR;
	to.sq_node = QRTR_NODE_BCAST;
	to.sq_port = QRTR_PORT_CTRL;

	pkt->cmd = cpu_to_le32(QRTR_TYPE_DEL_CLIENT);
	pkt->client.node = cpu_to_le32(ipc->us.sq_node);
	pkt->client.port = cpu_to_le32(ipc->us.sq_port);

	skb_set_owner_w(skb, &ipc->sk);

	if (ipc->state == QRTR_STATE_MULTI) {
		qrtr_bcast_enqueue(NULL, skb, type, &ipc->us, &to, 0);
		return;
	}

	if (ipc->state > QRTR_STATE_INIT) {
		node = qrtr_node_lookup(ipc->state);
		if (!node)
			goto exit;

		skbn = skb_clone(skb, GFP_KERNEL);
		if (!skbn) {
			qrtr_node_release(node);
			goto exit;
		}

		skb_set_owner_w(skbn, &ipc->sk);
		qrtr_node_enqueue(node, skbn, type, &ipc->us, &to, 0);
		qrtr_node_release(node);
	}
exit:
	qrtr_local_enqueue(NULL, skb, type, &ipc->us, &to, 0);
}

/* Remove port assignment. */
static void qrtr_port_remove(struct qrtr_sock *ipc)
{
	int port = ipc->us.sq_port;
	unsigned long flags;

	qrtr_send_del_client(ipc);

	if (port == QRTR_PORT_CTRL)
		port = 0;

	__sock_put(&ipc->sk);

	spin_lock_irqsave(&qrtr_port_lock, flags);
	xa_erase(&qrtr_ports, port);
	spin_unlock_irqrestore(&qrtr_port_lock, flags);
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

	if (!*port) {
		rc = xa_alloc_cyclic(&qrtr_ports, port, ipc,
				     QRTR_EPH_PORT_RANGE, &qrtr_ports_next,
				     GFP_ATOMIC);
	} else if (*port < QRTR_MIN_EPH_SOCKET &&
		   !(capable(CAP_NET_ADMIN) ||
		   in_egroup_p(AID_VENDOR_QRTR) ||
		   in_egroup_p(GLOBAL_ROOT_GID))) {
		rc = -EACCES;
	} else if (*port == QRTR_PORT_CTRL) {
		rc = xa_insert(&qrtr_ports, 0, ipc, GFP_ATOMIC);
	} else {
		rc = xa_insert(&qrtr_ports, *port, ipc, GFP_ATOMIC);
	}

	if (rc == -EBUSY)
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
	unsigned long index;

	rcu_read_lock();
	xa_for_each_start(&qrtr_ports, index, ipc, 1) {
		sock_hold(&ipc->sk);
		ipc->sk.sk_err = ENETRESET;
		sk_error_report(&ipc->sk);
		sock_put(&ipc->sk);
	}
	rcu_read_unlock();
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
	unsigned long flags;
	int port;
	int rc;

	/* rebinding ok */
	if (!zapped && addr->sq_port == ipc->us.sq_port)
		return 0;

	spin_lock_irqsave(&qrtr_port_lock, flags);
	port = addr->sq_port;
	rc = qrtr_port_assign(ipc, &port);
	if (rc) {
		spin_unlock_irqrestore(&qrtr_port_lock, flags);
		return rc;
	}

	if (port == QRTR_PORT_CTRL)
		qrtr_reset_ports();
	spin_unlock_irqrestore(&qrtr_port_lock, flags);

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
static int qrtr_local_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to, unsigned int flags)
{
	struct qrtr_sock *ipc;
	struct qrtr_cb *cb;
	struct sock *sk = skb->sk;
	int rc;

	ipc = qrtr_port_lookup(to->sq_port);
	if (!ipc && to->sq_port == QRTR_PORT_CTRL) {
		kfree_skb(skb);
		return 0;
	}
	if (!ipc || &ipc->sk == skb->sk) { /* do not send to self */
		if (ipc)
			qrtr_port_put(ipc);
		kfree_skb(skb);
		return -ENODEV;
	}

	/* Keep resetting NETRESET until socket is closed */
	if (sk && sk->sk_err == ENETRESET) {
		sk->sk_err = ENETRESET;
		sk_error_report(sk);
		qrtr_port_put(ipc);
		kfree_skb(skb);
		return 0;
	}

	cb = (struct qrtr_cb *)skb->cb;
	cb->src_node = from->sq_node;
	cb->src_port = from->sq_port;

	QRTR_INFO(qrtr_local_ilc,
		  "LOCAL ENQUEUE: cmd:0x%x src[0x%x:0x%x] dst[0x%x:0x%x] [%s] pid:%d\n",
		  type, from->sq_node, from->sq_port,
		  to->sq_node, to->sq_port, current->comm,
		  current->pid);

	rc = (ipc->us.sq_port == QRTR_PORT_CTRL) ?
		qrtr_sock_queue_ctrl_skb(ipc, skb) :
		sock_queue_rcv_skb(&ipc->sk, skb);
	qrtr_port_put(ipc);
	if (rc) {
		kfree_skb(skb);
		return -ENOSPC;
	}

	return 0;
}

/* Queue packet for broadcast. */
static int qrtr_bcast_enqueue(struct qrtr_node *node, struct sk_buff *skb,
			      int type, struct sockaddr_qrtr *from,
			      struct sockaddr_qrtr *to, unsigned int flags)
{
	struct sk_buff *skbn;

	down_read(&qrtr_epts_lock);
	list_for_each_entry(node, &qrtr_all_epts, item) {
		if (node->nid == QRTR_EP_NID_AUTO && type != QRTR_TYPE_HELLO)
			continue;

		skbn = skb_clone(skb, GFP_KERNEL);
		if (!skbn)
			break;
		skb_set_owner_w(skbn, skb->sk);
		qrtr_node_enqueue(node, skbn, type, from, to, flags);
	}
	up_read(&qrtr_epts_lock);

	qrtr_local_enqueue(NULL, skb, type, from, to, flags);

	return 0;
}

static int qrtr_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	int (*enqueue_fn)(struct qrtr_node *, struct sk_buff *, int,
			  struct sockaddr_qrtr *, struct sockaddr_qrtr *,
			  unsigned int);
	__le32 qrtr_type = cpu_to_le32(QRTR_TYPE_DATA);
	struct qrtr_sock *ipc = qrtr_sk(sock->sk);
	struct sock *sk = sock->sk;
	struct qrtr_ctrl_pkt pkt;
	struct qrtr_node *node;
	struct qrtr_node *srv_node;
	struct sk_buff *skb;
	int pdata_len = 0;
	int data_len = 0;
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
	srv_node = NULL;
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

		if (ipc->state > QRTR_STATE_INIT && ipc->state != node->nid)
			ipc->state = QRTR_STATE_MULTI;
		else if (ipc->state == QRTR_STATE_INIT)
			ipc->state = node->nid;
	}

	plen = (len + 3) & ~3;
	if (plen > SKB_MAX_ALLOC) {
		data_len = min_t(size_t,
				 plen - SKB_MAX_ALLOC,
				 MAX_SKB_FRAGS * PAGE_SIZE);
		pdata_len = PAGE_ALIGN(data_len);

		BUILD_BUG_ON(SKB_MAX_ALLOC < PAGE_SIZE);
	}
	skb = sock_alloc_send_pskb(sk, QRTR_HDR_MAX_SIZE + (plen - data_len),
				   pdata_len, msg->msg_flags & MSG_DONTWAIT,
				   &rc, PAGE_ALLOC_COSTLY_ORDER);
	if (!skb) {
		rc = -ENOMEM;
		goto out_node;
	}

	skb_reserve(skb, QRTR_HDR_MAX_SIZE);

	/* len is used by the enqueue functions and should remain accurate
	 * regardless of padding or allocation size
	 */
	skb_put(skb, len - data_len);
	skb->data_len = data_len;
	skb->len = len;
	rc = skb_copy_datagram_from_iter(skb, 0, &msg->msg_iter, len);
	if (rc) {
		kfree_skb(skb);
		goto out_node;
	}

	if (ipc->us.sq_port == QRTR_PORT_CTRL ||
	    addr->sq_port == QRTR_PORT_CTRL) {
		if (len < 4) {
			rc = -EINVAL;
			kfree_skb(skb);
			goto out_node;
		}

		/* control messages already require the type as 'command' */
		skb_copy_bits(skb, 0, &qrtr_type, 4);
	}

	type = le32_to_cpu(qrtr_type);
	if (addr->sq_port == QRTR_PORT_CTRL && type == QRTR_TYPE_NEW_SERVER) {
		ipc->state = QRTR_STATE_MULTI;

		/* drop new server cmds that are not forwardable to dst node*/
		skb_copy_bits(skb, 0, &pkt, sizeof(pkt));
		srv_node = qrtr_node_lookup(pkt.server.node);
		if (!qrtr_must_forward(srv_node, node, type)) {
			rc = 0;
			kfree_skb(skb);
			qrtr_node_release(srv_node);
			goto out_node;
		}
		qrtr_node_release(srv_node);
	}

	rc = enqueue_fn(node, skb, type, &ipc->us, addr, msg->msg_flags);

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
	if (!skb) {
		qrtr_node_release(node);
		return -ENOMEM;
	}

	pkt->cmd = cpu_to_le32(QRTR_TYPE_RESUME_TX);
	pkt->client.node = cpu_to_le32(cb->dst_node);
	pkt->client.port = cpu_to_le32(cb->dst_port);

	ret = qrtr_node_enqueue(node, skb, QRTR_TYPE_RESUME_TX, &local, &remote, 0);

	qrtr_node_release(node);

	return ret;
}

static int qrtr_recvmsg(struct socket *sock, struct msghdr *msg,
			size_t size, int flags)
{
	DECLARE_SOCKADDR(struct sockaddr_qrtr *, addr, msg->msg_name);
	struct sock *sk = sock->sk;
	unsigned long lock_flags;
	struct qrtr_sock *ipc;
	struct sk_buff *skb;
	struct qrtr_cb *cb;
	int copied, rc;


	if (sock_flag(sk, SOCK_ZAPPED))
		return -EADDRNOTAVAIL;

	skb = skb_recv_datagram(sk, flags, &rc);
	if (!skb)
		return rc;

	lock_sock(sk);
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
		/* There is an anonymous 2-byte hole after sq_family,
		 * make sure to clear it.
		 */
		memset(addr, 0, sizeof(*addr));

		addr->sq_family = AF_QIPCRTR;
		addr->sq_node = cb->src_node;
		addr->sq_port = cb->src_port;
		msg->msg_namelen = sizeof(*addr);
	}

out:
	if (cb->confirm_rx)
		qrtr_send_resume_tx(cb);

	skb_free_datagram(sk, skb);

	ipc = qrtr_sk(sk);
	if (ipc->us.sq_port == QRTR_PORT_CTRL) {
		spin_lock_irqsave(&ipc->signal_lock, lock_flags);
		if (ipc->signal_on_recv) {
			complete_all(&ipc->rx_queue_has_space);
			ipc->signal_on_recv = false;
		}
		spin_unlock_irqrestore(&ipc->signal_lock, lock_flags);
	}

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
		if (get_user_ifreq(&ifr, NULL, argp)) {
			rc = -EFAULT;
			break;
		}

		sq = (struct sockaddr_qrtr *)&ifr.ifr_addr;
		*sq = ipc->us;
		if (put_user_ifreq(&ifr, argp)) {
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

	if (ipc->us.sq_port == QRTR_PORT_CTRL) {
		struct qrtr_node *node;

		down_write(&qrtr_epts_lock);
		list_for_each_entry(node, &qrtr_all_epts, item) {
			atomic_set(&node->hello_sent, 0);
			atomic_set(&node->hello_rcvd, 0);
		}
		up_write(&qrtr_epts_lock);
	}

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
	sk->sk_allocation |= __GFP_RETRY_MAYFAIL;

	sock_init_data(sock, sk);
	sock->ops = &qrtr_proto_ops;

	ipc = qrtr_sk(sk);
	ipc->us.sq_family = AF_QIPCRTR;
	ipc->us.sq_node = qrtr_local_nid;
	ipc->us.sq_port = 0;
	ipc->state = QRTR_STATE_INIT;
	ipc->signal_on_recv = false;
	init_completion(&ipc->rx_queue_has_space);
	spin_lock_init(&ipc->signal_lock);

	return 0;
}

static const struct net_proto_family qrtr_family = {
	.owner	= THIS_MODULE,
	.family	= AF_QIPCRTR,
	.create	= qrtr_create,
};

static void qrtr_update_node_id(void)
{
	const char *compat = "qcom,qrtr";
	struct device_node *np = NULL;
	u32 node_id;
	int ret;

	while ((np = of_find_compatible_node(np, NULL, compat))) {
		ret = of_property_read_u32(np, "qcom,node-id", &node_id);
		of_node_put(np);
		if (ret)
			continue;

		qrtr_local_nid = node_id;
		break;
	}
}

static int __init qrtr_proto_init(void)
{
	int rc;

	qrtr_update_node_id();

	qrtr_local_ilc = ipc_log_context_create(QRTR_LOG_PAGE_CNT,
						"qrtr_local", 0);

	rc = proto_register(&qrtr_proto, 1);
	if (rc)
		return rc;

	rc = sock_register(&qrtr_family);
	if (rc)
		goto err_proto;

	rc = qrtr_ns_init();
	if (rc)
		goto err_sock;

	qrtr_backup_init();

	return 0;

err_sock:
	sock_unregister(qrtr_family.family);
err_proto:
	proto_unregister(&qrtr_proto);
	return rc;
}
postcore_initcall(qrtr_proto_init);

static void __exit qrtr_proto_fini(void)
{
	qrtr_ns_remove();
	sock_unregister(qrtr_family.family);
	proto_unregister(&qrtr_proto);

	qrtr_backup_deinit();
}
module_exit(qrtr_proto_fini);

MODULE_DESCRIPTION("Qualcomm IPC-router driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(PF_QIPCRTR);
