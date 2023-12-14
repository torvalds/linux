/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_VSOCK_H
#define _LINUX_VIRTIO_VSOCK_H

#include <uapi/linux/virtio_vsock.h>
#include <linux/socket.h>
#include <net/sock.h>
#include <net/af_vsock.h>

#define VIRTIO_VSOCK_SKB_HEADROOM (sizeof(struct virtio_vsock_hdr))

struct virtio_vsock_skb_cb {
	bool reply;
	bool tap_delivered;
};

#define VIRTIO_VSOCK_SKB_CB(skb) ((struct virtio_vsock_skb_cb *)((skb)->cb))

static inline struct virtio_vsock_hdr *virtio_vsock_hdr(struct sk_buff *skb)
{
	return (struct virtio_vsock_hdr *)skb->head;
}

static inline bool virtio_vsock_skb_reply(struct sk_buff *skb)
{
	return VIRTIO_VSOCK_SKB_CB(skb)->reply;
}

static inline void virtio_vsock_skb_set_reply(struct sk_buff *skb)
{
	VIRTIO_VSOCK_SKB_CB(skb)->reply = true;
}

static inline bool virtio_vsock_skb_tap_delivered(struct sk_buff *skb)
{
	return VIRTIO_VSOCK_SKB_CB(skb)->tap_delivered;
}

static inline void virtio_vsock_skb_set_tap_delivered(struct sk_buff *skb)
{
	VIRTIO_VSOCK_SKB_CB(skb)->tap_delivered = true;
}

static inline void virtio_vsock_skb_clear_tap_delivered(struct sk_buff *skb)
{
	VIRTIO_VSOCK_SKB_CB(skb)->tap_delivered = false;
}

static inline void virtio_vsock_skb_rx_put(struct sk_buff *skb)
{
	u32 len;

	len = le32_to_cpu(virtio_vsock_hdr(skb)->len);

	if (len > 0)
		skb_put(skb, len);
}

static inline struct sk_buff *virtio_vsock_alloc_skb(unsigned int size, gfp_t mask)
{
	struct sk_buff *skb;

	if (size < VIRTIO_VSOCK_SKB_HEADROOM)
		return NULL;

	skb = alloc_skb(size, mask);
	if (!skb)
		return NULL;

	skb_reserve(skb, VIRTIO_VSOCK_SKB_HEADROOM);
	return skb;
}

static inline void
virtio_vsock_skb_queue_head(struct sk_buff_head *list, struct sk_buff *skb)
{
	spin_lock_bh(&list->lock);
	__skb_queue_head(list, skb);
	spin_unlock_bh(&list->lock);
}

static inline void
virtio_vsock_skb_queue_tail(struct sk_buff_head *list, struct sk_buff *skb)
{
	spin_lock_bh(&list->lock);
	__skb_queue_tail(list, skb);
	spin_unlock_bh(&list->lock);
}

static inline struct sk_buff *virtio_vsock_skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *skb;

	spin_lock_bh(&list->lock);
	skb = __skb_dequeue(list);
	spin_unlock_bh(&list->lock);

	return skb;
}

static inline void virtio_vsock_skb_queue_purge(struct sk_buff_head *list)
{
	spin_lock_bh(&list->lock);
	__skb_queue_purge(list);
	spin_unlock_bh(&list->lock);
}

static inline size_t virtio_vsock_skb_len(struct sk_buff *skb)
{
	return (size_t)(skb_end_pointer(skb) - skb->head);
}

#define VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE	(1024 * 4)
#define VIRTIO_VSOCK_MAX_BUF_SIZE		0xFFFFFFFFUL
#define VIRTIO_VSOCK_MAX_PKT_BUF_SIZE		(1024 * 64)

enum {
	VSOCK_VQ_RX     = 0, /* for host to guest data */
	VSOCK_VQ_TX     = 1, /* for guest to host data */
	VSOCK_VQ_EVENT  = 2,
	VSOCK_VQ_MAX    = 3,
};

/* Per-socket state (accessed via vsk->trans) */
struct virtio_vsock_sock {
	struct vsock_sock *vsk;

	spinlock_t tx_lock;
	spinlock_t rx_lock;

	/* Protected by tx_lock */
	u32 tx_cnt;
	u32 peer_fwd_cnt;
	u32 peer_buf_alloc;

	/* Protected by rx_lock */
	u32 fwd_cnt;
	u32 last_fwd_cnt;
	u32 rx_bytes;
	u32 buf_alloc;
	struct sk_buff_head rx_queue;
	u32 msg_count;
};

struct virtio_vsock_pkt_info {
	u32 remote_cid, remote_port;
	struct vsock_sock *vsk;
	struct msghdr *msg;
	u32 pkt_len;
	u16 type;
	u16 op;
	u32 flags;
	bool reply;
};

struct virtio_transport {
	/* This must be the first field */
	struct vsock_transport transport;

	/* Takes ownership of the packet */
	int (*send_pkt)(struct sk_buff *skb);
};

ssize_t
virtio_transport_stream_dequeue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len,
				int type);
int
virtio_transport_dgram_dequeue(struct vsock_sock *vsk,
			       struct msghdr *msg,
			       size_t len, int flags);

int
virtio_transport_seqpacket_enqueue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   size_t len);
ssize_t
virtio_transport_seqpacket_dequeue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   int flags);
s64 virtio_transport_stream_has_data(struct vsock_sock *vsk);
s64 virtio_transport_stream_has_space(struct vsock_sock *vsk);
u32 virtio_transport_seqpacket_has_data(struct vsock_sock *vsk);

int virtio_transport_do_socket_init(struct vsock_sock *vsk,
				 struct vsock_sock *psk);
int
virtio_transport_notify_poll_in(struct vsock_sock *vsk,
				size_t target,
				bool *data_ready_now);
int
virtio_transport_notify_poll_out(struct vsock_sock *vsk,
				 size_t target,
				 bool *space_available_now);

int virtio_transport_notify_recv_init(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_recv_pre_block(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_recv_pre_dequeue(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_recv_post_dequeue(struct vsock_sock *vsk,
	size_t target, ssize_t copied, bool data_read,
	struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_send_init(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data);
int virtio_transport_notify_send_pre_block(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data);
int virtio_transport_notify_send_pre_enqueue(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data);
int virtio_transport_notify_send_post_enqueue(struct vsock_sock *vsk,
	ssize_t written, struct vsock_transport_send_notify_data *data);
void virtio_transport_notify_buffer_size(struct vsock_sock *vsk, u64 *val);

u64 virtio_transport_stream_rcvhiwat(struct vsock_sock *vsk);
bool virtio_transport_stream_is_active(struct vsock_sock *vsk);
bool virtio_transport_stream_allow(u32 cid, u32 port);
int virtio_transport_dgram_bind(struct vsock_sock *vsk,
				struct sockaddr_vm *addr);
bool virtio_transport_dgram_allow(u32 cid, u32 port);

int virtio_transport_connect(struct vsock_sock *vsk);

int virtio_transport_shutdown(struct vsock_sock *vsk, int mode);

void virtio_transport_release(struct vsock_sock *vsk);

ssize_t
virtio_transport_stream_enqueue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len);
int
virtio_transport_dgram_enqueue(struct vsock_sock *vsk,
			       struct sockaddr_vm *remote_addr,
			       struct msghdr *msg,
			       size_t len);

void virtio_transport_destruct(struct vsock_sock *vsk);

void virtio_transport_recv_pkt(struct virtio_transport *t,
			       struct sk_buff *skb);
void virtio_transport_inc_tx_pkt(struct virtio_vsock_sock *vvs, struct sk_buff *skb);
u32 virtio_transport_get_credit(struct virtio_vsock_sock *vvs, u32 wanted);
void virtio_transport_put_credit(struct virtio_vsock_sock *vvs, u32 credit);
void virtio_transport_deliver_tap_pkt(struct sk_buff *skb);
int virtio_transport_purge_skbs(void *vsk, struct sk_buff_head *list);
int virtio_transport_read_skb(struct vsock_sock *vsk, skb_read_actor_t read_actor);
int virtio_transport_notify_set_rcvlowat(struct vsock_sock *vsk, int val);
#endif /* _LINUX_VIRTIO_VSOCK_H */
