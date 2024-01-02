// SPDX-License-Identifier: GPL-2.0-only
/*
 * common code for virtio vsock
 *
 * Copyright (C) 2013-2015 Red Hat, Inc.
 * Author: Asias He <asias@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/virtio_vsock.h>
#include <uapi/linux/vsockmon.h>

#include <net/sock.h>
#include <net/af_vsock.h>

#define CREATE_TRACE_POINTS
#include <trace/events/vsock_virtio_transport_common.h>

/* How long to wait for graceful shutdown of a connection */
#define VSOCK_CLOSE_TIMEOUT (8 * HZ)

/* Threshold for detecting small packets to copy */
#define GOOD_COPY_LEN  128

static const struct virtio_transport *
virtio_transport_get_ops(struct vsock_sock *vsk)
{
	const struct vsock_transport *t = vsock_core_get_transport(vsk);

	if (WARN_ON(!t))
		return NULL;

	return container_of(t, struct virtio_transport, transport);
}

static bool virtio_transport_can_zcopy(const struct virtio_transport *t_ops,
				       struct virtio_vsock_pkt_info *info,
				       size_t pkt_len)
{
	struct iov_iter *iov_iter;

	if (!info->msg)
		return false;

	iov_iter = &info->msg->msg_iter;

	if (iov_iter->iov_offset)
		return false;

	/* We can't send whole iov. */
	if (iov_iter->count > pkt_len)
		return false;

	/* Check that transport can send data in zerocopy mode. */
	t_ops = virtio_transport_get_ops(info->vsk);

	if (t_ops->can_msgzerocopy) {
		int pages_to_send = iov_iter_npages(iov_iter, MAX_SKB_FRAGS);

		/* +1 is for packet header. */
		return t_ops->can_msgzerocopy(pages_to_send + 1);
	}

	return true;
}

static int virtio_transport_init_zcopy_skb(struct vsock_sock *vsk,
					   struct sk_buff *skb,
					   struct msghdr *msg,
					   bool zerocopy)
{
	struct ubuf_info *uarg;

	if (msg->msg_ubuf) {
		uarg = msg->msg_ubuf;
		net_zcopy_get(uarg);
	} else {
		struct iov_iter *iter = &msg->msg_iter;
		struct ubuf_info_msgzc *uarg_zc;

		uarg = msg_zerocopy_realloc(sk_vsock(vsk),
					    iter->count,
					    NULL);
		if (!uarg)
			return -1;

		uarg_zc = uarg_to_msgzc(uarg);
		uarg_zc->zerocopy = zerocopy ? 1 : 0;
	}

	skb_zcopy_init(skb, uarg);

	return 0;
}

static int virtio_transport_fill_skb(struct sk_buff *skb,
				     struct virtio_vsock_pkt_info *info,
				     size_t len,
				     bool zcopy)
{
	if (zcopy)
		return __zerocopy_sg_from_iter(info->msg, NULL, skb,
					       &info->msg->msg_iter,
					       len);

	return memcpy_from_msg(skb_put(skb, len), info->msg, len);
}

static void virtio_transport_init_hdr(struct sk_buff *skb,
				      struct virtio_vsock_pkt_info *info,
				      size_t payload_len,
				      u32 src_cid,
				      u32 src_port,
				      u32 dst_cid,
				      u32 dst_port)
{
	struct virtio_vsock_hdr *hdr;

	hdr = virtio_vsock_hdr(skb);
	hdr->type	= cpu_to_le16(info->type);
	hdr->op		= cpu_to_le16(info->op);
	hdr->src_cid	= cpu_to_le64(src_cid);
	hdr->dst_cid	= cpu_to_le64(dst_cid);
	hdr->src_port	= cpu_to_le32(src_port);
	hdr->dst_port	= cpu_to_le32(dst_port);
	hdr->flags	= cpu_to_le32(info->flags);
	hdr->len	= cpu_to_le32(payload_len);
	hdr->buf_alloc	= cpu_to_le32(0);
	hdr->fwd_cnt	= cpu_to_le32(0);
}

static void virtio_transport_copy_nonlinear_skb(const struct sk_buff *skb,
						void *dst,
						size_t len)
{
	struct iov_iter iov_iter = { 0 };
	struct kvec kvec;
	size_t to_copy;

	kvec.iov_base = dst;
	kvec.iov_len = len;

	iov_iter.iter_type = ITER_KVEC;
	iov_iter.kvec = &kvec;
	iov_iter.nr_segs = 1;

	to_copy = min_t(size_t, len, skb->len);

	skb_copy_datagram_iter(skb, VIRTIO_VSOCK_SKB_CB(skb)->offset,
			       &iov_iter, to_copy);
}

/* Packet capture */
static struct sk_buff *virtio_transport_build_skb(void *opaque)
{
	struct virtio_vsock_hdr *pkt_hdr;
	struct sk_buff *pkt = opaque;
	struct af_vsockmon_hdr *hdr;
	struct sk_buff *skb;
	size_t payload_len;

	/* A packet could be split to fit the RX buffer, so we can retrieve
	 * the payload length from the header and the buffer pointer taking
	 * care of the offset in the original packet.
	 */
	pkt_hdr = virtio_vsock_hdr(pkt);
	payload_len = pkt->len;

	skb = alloc_skb(sizeof(*hdr) + sizeof(*pkt_hdr) + payload_len,
			GFP_ATOMIC);
	if (!skb)
		return NULL;

	hdr = skb_put(skb, sizeof(*hdr));

	/* pkt->hdr is little-endian so no need to byteswap here */
	hdr->src_cid = pkt_hdr->src_cid;
	hdr->src_port = pkt_hdr->src_port;
	hdr->dst_cid = pkt_hdr->dst_cid;
	hdr->dst_port = pkt_hdr->dst_port;

	hdr->transport = cpu_to_le16(AF_VSOCK_TRANSPORT_VIRTIO);
	hdr->len = cpu_to_le16(sizeof(*pkt_hdr));
	memset(hdr->reserved, 0, sizeof(hdr->reserved));

	switch (le16_to_cpu(pkt_hdr->op)) {
	case VIRTIO_VSOCK_OP_REQUEST:
	case VIRTIO_VSOCK_OP_RESPONSE:
		hdr->op = cpu_to_le16(AF_VSOCK_OP_CONNECT);
		break;
	case VIRTIO_VSOCK_OP_RST:
	case VIRTIO_VSOCK_OP_SHUTDOWN:
		hdr->op = cpu_to_le16(AF_VSOCK_OP_DISCONNECT);
		break;
	case VIRTIO_VSOCK_OP_RW:
		hdr->op = cpu_to_le16(AF_VSOCK_OP_PAYLOAD);
		break;
	case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
	case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
		hdr->op = cpu_to_le16(AF_VSOCK_OP_CONTROL);
		break;
	default:
		hdr->op = cpu_to_le16(AF_VSOCK_OP_UNKNOWN);
		break;
	}

	skb_put_data(skb, pkt_hdr, sizeof(*pkt_hdr));

	if (payload_len) {
		if (skb_is_nonlinear(pkt)) {
			void *data = skb_put(skb, payload_len);

			virtio_transport_copy_nonlinear_skb(pkt, data, payload_len);
		} else {
			skb_put_data(skb, pkt->data, payload_len);
		}
	}

	return skb;
}

void virtio_transport_deliver_tap_pkt(struct sk_buff *skb)
{
	if (virtio_vsock_skb_tap_delivered(skb))
		return;

	vsock_deliver_tap(virtio_transport_build_skb, skb);
	virtio_vsock_skb_set_tap_delivered(skb);
}
EXPORT_SYMBOL_GPL(virtio_transport_deliver_tap_pkt);

static u16 virtio_transport_get_type(struct sock *sk)
{
	if (sk->sk_type == SOCK_STREAM)
		return VIRTIO_VSOCK_TYPE_STREAM;
	else
		return VIRTIO_VSOCK_TYPE_SEQPACKET;
}

/* Returns new sk_buff on success, otherwise returns NULL. */
static struct sk_buff *virtio_transport_alloc_skb(struct virtio_vsock_pkt_info *info,
						  size_t payload_len,
						  bool zcopy,
						  u32 src_cid,
						  u32 src_port,
						  u32 dst_cid,
						  u32 dst_port)
{
	struct vsock_sock *vsk;
	struct sk_buff *skb;
	size_t skb_len;

	skb_len = VIRTIO_VSOCK_SKB_HEADROOM;

	if (!zcopy)
		skb_len += payload_len;

	skb = virtio_vsock_alloc_skb(skb_len, GFP_KERNEL);
	if (!skb)
		return NULL;

	virtio_transport_init_hdr(skb, info, payload_len, src_cid, src_port,
				  dst_cid, dst_port);

	vsk = info->vsk;

	/* If 'vsk' != NULL then payload is always present, so we
	 * will never call '__zerocopy_sg_from_iter()' below without
	 * setting skb owner in 'skb_set_owner_w()'. The only case
	 * when 'vsk' == NULL is VIRTIO_VSOCK_OP_RST control message
	 * without payload.
	 */
	WARN_ON_ONCE(!(vsk && (info->msg && payload_len)) && zcopy);

	/* Set owner here, because '__zerocopy_sg_from_iter()' uses
	 * owner of skb without check to update 'sk_wmem_alloc'.
	 */
	if (vsk)
		skb_set_owner_w(skb, sk_vsock(vsk));

	if (info->msg && payload_len > 0) {
		int err;

		err = virtio_transport_fill_skb(skb, info, payload_len, zcopy);
		if (err)
			goto out;

		if (msg_data_left(info->msg) == 0 &&
		    info->type == VIRTIO_VSOCK_TYPE_SEQPACKET) {
			struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);

			hdr->flags |= cpu_to_le32(VIRTIO_VSOCK_SEQ_EOM);

			if (info->msg->msg_flags & MSG_EOR)
				hdr->flags |= cpu_to_le32(VIRTIO_VSOCK_SEQ_EOR);
		}
	}

	if (info->reply)
		virtio_vsock_skb_set_reply(skb);

	trace_virtio_transport_alloc_pkt(src_cid, src_port,
					 dst_cid, dst_port,
					 payload_len,
					 info->type,
					 info->op,
					 info->flags,
					 zcopy);

	return skb;
out:
	kfree_skb(skb);
	return NULL;
}

/* This function can only be used on connecting/connected sockets,
 * since a socket assigned to a transport is required.
 *
 * Do not use on listener sockets!
 */
static int virtio_transport_send_pkt_info(struct vsock_sock *vsk,
					  struct virtio_vsock_pkt_info *info)
{
	u32 max_skb_len = VIRTIO_VSOCK_MAX_PKT_BUF_SIZE;
	u32 src_cid, src_port, dst_cid, dst_port;
	const struct virtio_transport *t_ops;
	struct virtio_vsock_sock *vvs;
	u32 pkt_len = info->pkt_len;
	bool can_zcopy = false;
	u32 rest_len;
	int ret;

	info->type = virtio_transport_get_type(sk_vsock(vsk));

	t_ops = virtio_transport_get_ops(vsk);
	if (unlikely(!t_ops))
		return -EFAULT;

	src_cid = t_ops->transport.get_local_cid();
	src_port = vsk->local_addr.svm_port;
	if (!info->remote_cid) {
		dst_cid	= vsk->remote_addr.svm_cid;
		dst_port = vsk->remote_addr.svm_port;
	} else {
		dst_cid = info->remote_cid;
		dst_port = info->remote_port;
	}

	vvs = vsk->trans;

	/* virtio_transport_get_credit might return less than pkt_len credit */
	pkt_len = virtio_transport_get_credit(vvs, pkt_len);

	/* Do not send zero length OP_RW pkt */
	if (pkt_len == 0 && info->op == VIRTIO_VSOCK_OP_RW)
		return pkt_len;

	if (info->msg) {
		/* If zerocopy is not enabled by 'setsockopt()', we behave as
		 * there is no MSG_ZEROCOPY flag set.
		 */
		if (!sock_flag(sk_vsock(vsk), SOCK_ZEROCOPY))
			info->msg->msg_flags &= ~MSG_ZEROCOPY;

		if (info->msg->msg_flags & MSG_ZEROCOPY)
			can_zcopy = virtio_transport_can_zcopy(t_ops, info, pkt_len);

		if (can_zcopy)
			max_skb_len = min_t(u32, VIRTIO_VSOCK_MAX_PKT_BUF_SIZE,
					    (MAX_SKB_FRAGS * PAGE_SIZE));
	}

	rest_len = pkt_len;

	do {
		struct sk_buff *skb;
		size_t skb_len;

		skb_len = min(max_skb_len, rest_len);

		skb = virtio_transport_alloc_skb(info, skb_len, can_zcopy,
						 src_cid, src_port,
						 dst_cid, dst_port);
		if (!skb) {
			ret = -ENOMEM;
			break;
		}

		/* We process buffer part by part, allocating skb on
		 * each iteration. If this is last skb for this buffer
		 * and MSG_ZEROCOPY mode is in use - we must allocate
		 * completion for the current syscall.
		 */
		if (info->msg && info->msg->msg_flags & MSG_ZEROCOPY &&
		    skb_len == rest_len && info->op == VIRTIO_VSOCK_OP_RW) {
			if (virtio_transport_init_zcopy_skb(vsk, skb,
							    info->msg,
							    can_zcopy)) {
				ret = -ENOMEM;
				break;
			}
		}

		virtio_transport_inc_tx_pkt(vvs, skb);

		ret = t_ops->send_pkt(skb);
		if (ret < 0)
			break;

		/* Both virtio and vhost 'send_pkt()' returns 'skb_len',
		 * but for reliability use 'ret' instead of 'skb_len'.
		 * Also if partial send happens (e.g. 'ret' != 'skb_len')
		 * somehow, we break this loop, but account such returned
		 * value in 'virtio_transport_put_credit()'.
		 */
		rest_len -= ret;

		if (WARN_ONCE(ret != skb_len,
			      "'send_pkt()' returns %i, but %zu expected\n",
			      ret, skb_len))
			break;
	} while (rest_len);

	virtio_transport_put_credit(vvs, rest_len);

	/* Return number of bytes, if any data has been sent. */
	if (rest_len != pkt_len)
		ret = pkt_len - rest_len;

	return ret;
}

static bool virtio_transport_inc_rx_pkt(struct virtio_vsock_sock *vvs,
					u32 len)
{
	if (vvs->rx_bytes + len > vvs->buf_alloc)
		return false;

	vvs->rx_bytes += len;
	return true;
}

static void virtio_transport_dec_rx_pkt(struct virtio_vsock_sock *vvs,
					u32 len)
{
	vvs->rx_bytes -= len;
	vvs->fwd_cnt += len;
}

void virtio_transport_inc_tx_pkt(struct virtio_vsock_sock *vvs, struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);

	spin_lock_bh(&vvs->rx_lock);
	vvs->last_fwd_cnt = vvs->fwd_cnt;
	hdr->fwd_cnt = cpu_to_le32(vvs->fwd_cnt);
	hdr->buf_alloc = cpu_to_le32(vvs->buf_alloc);
	spin_unlock_bh(&vvs->rx_lock);
}
EXPORT_SYMBOL_GPL(virtio_transport_inc_tx_pkt);

u32 virtio_transport_get_credit(struct virtio_vsock_sock *vvs, u32 credit)
{
	u32 ret;

	if (!credit)
		return 0;

	spin_lock_bh(&vvs->tx_lock);
	ret = vvs->peer_buf_alloc - (vvs->tx_cnt - vvs->peer_fwd_cnt);
	if (ret > credit)
		ret = credit;
	vvs->tx_cnt += ret;
	spin_unlock_bh(&vvs->tx_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_credit);

void virtio_transport_put_credit(struct virtio_vsock_sock *vvs, u32 credit)
{
	if (!credit)
		return;

	spin_lock_bh(&vvs->tx_lock);
	vvs->tx_cnt -= credit;
	spin_unlock_bh(&vvs->tx_lock);
}
EXPORT_SYMBOL_GPL(virtio_transport_put_credit);

static int virtio_transport_send_credit_update(struct vsock_sock *vsk)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_CREDIT_UPDATE,
		.vsk = vsk,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}

static ssize_t
virtio_transport_stream_do_peek(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	struct sk_buff *skb;
	size_t total = 0;
	int err;

	spin_lock_bh(&vvs->rx_lock);

	skb_queue_walk(&vvs->rx_queue, skb) {
		size_t bytes;

		bytes = len - total;
		if (bytes > skb->len)
			bytes = skb->len;

		spin_unlock_bh(&vvs->rx_lock);

		/* sk_lock is held by caller so no one else can dequeue.
		 * Unlock rx_lock since skb_copy_datagram_iter() may sleep.
		 */
		err = skb_copy_datagram_iter(skb, VIRTIO_VSOCK_SKB_CB(skb)->offset,
					     &msg->msg_iter, bytes);
		if (err)
			goto out;

		total += bytes;

		spin_lock_bh(&vvs->rx_lock);

		if (total == len)
			break;
	}

	spin_unlock_bh(&vvs->rx_lock);

	return total;

out:
	if (total)
		err = total;
	return err;
}

static ssize_t
virtio_transport_stream_do_dequeue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   size_t len)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	size_t bytes, total = 0;
	struct sk_buff *skb;
	int err = -EFAULT;
	u32 free_space;

	spin_lock_bh(&vvs->rx_lock);

	if (WARN_ONCE(skb_queue_empty(&vvs->rx_queue) && vvs->rx_bytes,
		      "rx_queue is empty, but rx_bytes is non-zero\n")) {
		spin_unlock_bh(&vvs->rx_lock);
		return err;
	}

	while (total < len && !skb_queue_empty(&vvs->rx_queue)) {
		skb = skb_peek(&vvs->rx_queue);

		bytes = min_t(size_t, len - total,
			      skb->len - VIRTIO_VSOCK_SKB_CB(skb)->offset);

		/* sk_lock is held by caller so no one else can dequeue.
		 * Unlock rx_lock since skb_copy_datagram_iter() may sleep.
		 */
		spin_unlock_bh(&vvs->rx_lock);

		err = skb_copy_datagram_iter(skb,
					     VIRTIO_VSOCK_SKB_CB(skb)->offset,
					     &msg->msg_iter, bytes);
		if (err)
			goto out;

		spin_lock_bh(&vvs->rx_lock);

		total += bytes;

		VIRTIO_VSOCK_SKB_CB(skb)->offset += bytes;

		if (skb->len == VIRTIO_VSOCK_SKB_CB(skb)->offset) {
			u32 pkt_len = le32_to_cpu(virtio_vsock_hdr(skb)->len);

			virtio_transport_dec_rx_pkt(vvs, pkt_len);
			__skb_unlink(skb, &vvs->rx_queue);
			consume_skb(skb);
		}
	}

	free_space = vvs->buf_alloc - (vvs->fwd_cnt - vvs->last_fwd_cnt);

	spin_unlock_bh(&vvs->rx_lock);

	/* To reduce the number of credit update messages,
	 * don't update credits as long as lots of space is available.
	 * Note: the limit chosen here is arbitrary. Setting the limit
	 * too high causes extra messages. Too low causes transmitter
	 * stalls. As stalls are in theory more expensive than extra
	 * messages, we set the limit to a high value. TODO: experiment
	 * with different values.
	 */
	if (free_space < VIRTIO_VSOCK_MAX_PKT_BUF_SIZE)
		virtio_transport_send_credit_update(vsk);

	return total;

out:
	if (total)
		err = total;
	return err;
}

static ssize_t
virtio_transport_seqpacket_do_peek(struct vsock_sock *vsk,
				   struct msghdr *msg)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	struct sk_buff *skb;
	size_t total, len;

	spin_lock_bh(&vvs->rx_lock);

	if (!vvs->msg_count) {
		spin_unlock_bh(&vvs->rx_lock);
		return 0;
	}

	total = 0;
	len = msg_data_left(msg);

	skb_queue_walk(&vvs->rx_queue, skb) {
		struct virtio_vsock_hdr *hdr;

		if (total < len) {
			size_t bytes;
			int err;

			bytes = len - total;
			if (bytes > skb->len)
				bytes = skb->len;

			spin_unlock_bh(&vvs->rx_lock);

			/* sk_lock is held by caller so no one else can dequeue.
			 * Unlock rx_lock since skb_copy_datagram_iter() may sleep.
			 */
			err = skb_copy_datagram_iter(skb, VIRTIO_VSOCK_SKB_CB(skb)->offset,
						     &msg->msg_iter, bytes);
			if (err)
				return err;

			spin_lock_bh(&vvs->rx_lock);
		}

		total += skb->len;
		hdr = virtio_vsock_hdr(skb);

		if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SEQ_EOM) {
			if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SEQ_EOR)
				msg->msg_flags |= MSG_EOR;

			break;
		}
	}

	spin_unlock_bh(&vvs->rx_lock);

	return total;
}

static int virtio_transport_seqpacket_do_dequeue(struct vsock_sock *vsk,
						 struct msghdr *msg,
						 int flags)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	int dequeued_len = 0;
	size_t user_buf_len = msg_data_left(msg);
	bool msg_ready = false;
	struct sk_buff *skb;

	spin_lock_bh(&vvs->rx_lock);

	if (vvs->msg_count == 0) {
		spin_unlock_bh(&vvs->rx_lock);
		return 0;
	}

	while (!msg_ready) {
		struct virtio_vsock_hdr *hdr;
		size_t pkt_len;

		skb = __skb_dequeue(&vvs->rx_queue);
		if (!skb)
			break;
		hdr = virtio_vsock_hdr(skb);
		pkt_len = (size_t)le32_to_cpu(hdr->len);

		if (dequeued_len >= 0) {
			size_t bytes_to_copy;

			bytes_to_copy = min(user_buf_len, pkt_len);

			if (bytes_to_copy) {
				int err;

				/* sk_lock is held by caller so no one else can dequeue.
				 * Unlock rx_lock since skb_copy_datagram_iter() may sleep.
				 */
				spin_unlock_bh(&vvs->rx_lock);

				err = skb_copy_datagram_iter(skb, 0,
							     &msg->msg_iter,
							     bytes_to_copy);
				if (err) {
					/* Copy of message failed. Rest of
					 * fragments will be freed without copy.
					 */
					dequeued_len = err;
				} else {
					user_buf_len -= bytes_to_copy;
				}

				spin_lock_bh(&vvs->rx_lock);
			}

			if (dequeued_len >= 0)
				dequeued_len += pkt_len;
		}

		if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SEQ_EOM) {
			msg_ready = true;
			vvs->msg_count--;

			if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SEQ_EOR)
				msg->msg_flags |= MSG_EOR;
		}

		virtio_transport_dec_rx_pkt(vvs, pkt_len);
		kfree_skb(skb);
	}

	spin_unlock_bh(&vvs->rx_lock);

	virtio_transport_send_credit_update(vsk);

	return dequeued_len;
}

ssize_t
virtio_transport_stream_dequeue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len, int flags)
{
	if (flags & MSG_PEEK)
		return virtio_transport_stream_do_peek(vsk, msg, len);
	else
		return virtio_transport_stream_do_dequeue(vsk, msg, len);
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_dequeue);

ssize_t
virtio_transport_seqpacket_dequeue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   int flags)
{
	if (flags & MSG_PEEK)
		return virtio_transport_seqpacket_do_peek(vsk, msg);
	else
		return virtio_transport_seqpacket_do_dequeue(vsk, msg, flags);
}
EXPORT_SYMBOL_GPL(virtio_transport_seqpacket_dequeue);

int
virtio_transport_seqpacket_enqueue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   size_t len)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	spin_lock_bh(&vvs->tx_lock);

	if (len > vvs->peer_buf_alloc) {
		spin_unlock_bh(&vvs->tx_lock);
		return -EMSGSIZE;
	}

	spin_unlock_bh(&vvs->tx_lock);

	return virtio_transport_stream_enqueue(vsk, msg, len);
}
EXPORT_SYMBOL_GPL(virtio_transport_seqpacket_enqueue);

int
virtio_transport_dgram_dequeue(struct vsock_sock *vsk,
			       struct msghdr *msg,
			       size_t len, int flags)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_dequeue);

s64 virtio_transport_stream_has_data(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	s64 bytes;

	spin_lock_bh(&vvs->rx_lock);
	bytes = vvs->rx_bytes;
	spin_unlock_bh(&vvs->rx_lock);

	return bytes;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_has_data);

u32 virtio_transport_seqpacket_has_data(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	u32 msg_count;

	spin_lock_bh(&vvs->rx_lock);
	msg_count = vvs->msg_count;
	spin_unlock_bh(&vvs->rx_lock);

	return msg_count;
}
EXPORT_SYMBOL_GPL(virtio_transport_seqpacket_has_data);

static s64 virtio_transport_has_space(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	s64 bytes;

	bytes = (s64)vvs->peer_buf_alloc - (vvs->tx_cnt - vvs->peer_fwd_cnt);
	if (bytes < 0)
		bytes = 0;

	return bytes;
}

s64 virtio_transport_stream_has_space(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	s64 bytes;

	spin_lock_bh(&vvs->tx_lock);
	bytes = virtio_transport_has_space(vsk);
	spin_unlock_bh(&vvs->tx_lock);

	return bytes;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_has_space);

int virtio_transport_do_socket_init(struct vsock_sock *vsk,
				    struct vsock_sock *psk)
{
	struct virtio_vsock_sock *vvs;

	vvs = kzalloc(sizeof(*vvs), GFP_KERNEL);
	if (!vvs)
		return -ENOMEM;

	vsk->trans = vvs;
	vvs->vsk = vsk;
	if (psk && psk->trans) {
		struct virtio_vsock_sock *ptrans = psk->trans;

		vvs->peer_buf_alloc = ptrans->peer_buf_alloc;
	}

	if (vsk->buffer_size > VIRTIO_VSOCK_MAX_BUF_SIZE)
		vsk->buffer_size = VIRTIO_VSOCK_MAX_BUF_SIZE;

	vvs->buf_alloc = vsk->buffer_size;

	spin_lock_init(&vvs->rx_lock);
	spin_lock_init(&vvs->tx_lock);
	skb_queue_head_init(&vvs->rx_queue);

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_do_socket_init);

/* sk_lock held by the caller */
void virtio_transport_notify_buffer_size(struct vsock_sock *vsk, u64 *val)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	if (*val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		*val = VIRTIO_VSOCK_MAX_BUF_SIZE;

	vvs->buf_alloc = *val;

	virtio_transport_send_credit_update(vsk);
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_buffer_size);

int
virtio_transport_notify_poll_in(struct vsock_sock *vsk,
				size_t target,
				bool *data_ready_now)
{
	*data_ready_now = vsock_stream_has_data(vsk) >= target;

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_poll_in);

int
virtio_transport_notify_poll_out(struct vsock_sock *vsk,
				 size_t target,
				 bool *space_avail_now)
{
	s64 free_space;

	free_space = vsock_stream_has_space(vsk);
	if (free_space > 0)
		*space_avail_now = true;
	else if (free_space == 0)
		*space_avail_now = false;

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_poll_out);

int virtio_transport_notify_recv_init(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_init);

int virtio_transport_notify_recv_pre_block(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_pre_block);

int virtio_transport_notify_recv_pre_dequeue(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_pre_dequeue);

int virtio_transport_notify_recv_post_dequeue(struct vsock_sock *vsk,
	size_t target, ssize_t copied, bool data_read,
	struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_post_dequeue);

int virtio_transport_notify_send_init(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_init);

int virtio_transport_notify_send_pre_block(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_pre_block);

int virtio_transport_notify_send_pre_enqueue(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_pre_enqueue);

int virtio_transport_notify_send_post_enqueue(struct vsock_sock *vsk,
	ssize_t written, struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_post_enqueue);

u64 virtio_transport_stream_rcvhiwat(struct vsock_sock *vsk)
{
	return vsk->buffer_size;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_rcvhiwat);

bool virtio_transport_stream_is_active(struct vsock_sock *vsk)
{
	return true;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_is_active);

bool virtio_transport_stream_allow(u32 cid, u32 port)
{
	return true;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_allow);

int virtio_transport_dgram_bind(struct vsock_sock *vsk,
				struct sockaddr_vm *addr)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_bind);

bool virtio_transport_dgram_allow(u32 cid, u32 port)
{
	return false;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_allow);

int virtio_transport_connect(struct vsock_sock *vsk)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_REQUEST,
		.vsk = vsk,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_connect);

int virtio_transport_shutdown(struct vsock_sock *vsk, int mode)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_SHUTDOWN,
		.flags = (mode & RCV_SHUTDOWN ?
			  VIRTIO_VSOCK_SHUTDOWN_RCV : 0) |
			 (mode & SEND_SHUTDOWN ?
			  VIRTIO_VSOCK_SHUTDOWN_SEND : 0),
		.vsk = vsk,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_shutdown);

int
virtio_transport_dgram_enqueue(struct vsock_sock *vsk,
			       struct sockaddr_vm *remote_addr,
			       struct msghdr *msg,
			       size_t dgram_len)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_enqueue);

ssize_t
virtio_transport_stream_enqueue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RW,
		.msg = msg,
		.pkt_len = len,
		.vsk = vsk,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_enqueue);

void virtio_transport_destruct(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	kfree(vvs);
}
EXPORT_SYMBOL_GPL(virtio_transport_destruct);

static int virtio_transport_reset(struct vsock_sock *vsk,
				  struct sk_buff *skb)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RST,
		.reply = !!skb,
		.vsk = vsk,
	};

	/* Send RST only if the original pkt is not a RST pkt */
	if (skb && le16_to_cpu(virtio_vsock_hdr(skb)->op) == VIRTIO_VSOCK_OP_RST)
		return 0;

	return virtio_transport_send_pkt_info(vsk, &info);
}

/* Normally packets are associated with a socket.  There may be no socket if an
 * attempt was made to connect to a socket that does not exist.
 */
static int virtio_transport_reset_no_sock(const struct virtio_transport *t,
					  struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RST,
		.type = le16_to_cpu(hdr->type),
		.reply = true,
	};
	struct sk_buff *reply;

	/* Send RST only if the original pkt is not a RST pkt */
	if (le16_to_cpu(hdr->op) == VIRTIO_VSOCK_OP_RST)
		return 0;

	if (!t)
		return -ENOTCONN;

	reply = virtio_transport_alloc_skb(&info, 0, false,
					   le64_to_cpu(hdr->dst_cid),
					   le32_to_cpu(hdr->dst_port),
					   le64_to_cpu(hdr->src_cid),
					   le32_to_cpu(hdr->src_port));
	if (!reply)
		return -ENOMEM;

	return t->send_pkt(reply);
}

/* This function should be called with sk_lock held and SOCK_DONE set */
static void virtio_transport_remove_sock(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	/* We don't need to take rx_lock, as the socket is closing and we are
	 * removing it.
	 */
	__skb_queue_purge(&vvs->rx_queue);
	vsock_remove_sock(vsk);
}

static void virtio_transport_wait_close(struct sock *sk, long timeout)
{
	if (timeout) {
		DEFINE_WAIT_FUNC(wait, woken_wake_function);

		add_wait_queue(sk_sleep(sk), &wait);

		do {
			if (sk_wait_event(sk, &timeout,
					  sock_flag(sk, SOCK_DONE), &wait))
				break;
		} while (!signal_pending(current) && timeout);

		remove_wait_queue(sk_sleep(sk), &wait);
	}
}

static void virtio_transport_do_close(struct vsock_sock *vsk,
				      bool cancel_timeout)
{
	struct sock *sk = sk_vsock(vsk);

	sock_set_flag(sk, SOCK_DONE);
	vsk->peer_shutdown = SHUTDOWN_MASK;
	if (vsock_stream_has_data(vsk) <= 0)
		sk->sk_state = TCP_CLOSING;
	sk->sk_state_change(sk);

	if (vsk->close_work_scheduled &&
	    (!cancel_timeout || cancel_delayed_work(&vsk->close_work))) {
		vsk->close_work_scheduled = false;

		virtio_transport_remove_sock(vsk);

		/* Release refcnt obtained when we scheduled the timeout */
		sock_put(sk);
	}
}

static void virtio_transport_close_timeout(struct work_struct *work)
{
	struct vsock_sock *vsk =
		container_of(work, struct vsock_sock, close_work.work);
	struct sock *sk = sk_vsock(vsk);

	sock_hold(sk);
	lock_sock(sk);

	if (!sock_flag(sk, SOCK_DONE)) {
		(void)virtio_transport_reset(vsk, NULL);

		virtio_transport_do_close(vsk, false);
	}

	vsk->close_work_scheduled = false;

	release_sock(sk);
	sock_put(sk);
}

/* User context, vsk->sk is locked */
static bool virtio_transport_close(struct vsock_sock *vsk)
{
	struct sock *sk = &vsk->sk;

	if (!(sk->sk_state == TCP_ESTABLISHED ||
	      sk->sk_state == TCP_CLOSING))
		return true;

	/* Already received SHUTDOWN from peer, reply with RST */
	if ((vsk->peer_shutdown & SHUTDOWN_MASK) == SHUTDOWN_MASK) {
		(void)virtio_transport_reset(vsk, NULL);
		return true;
	}

	if ((sk->sk_shutdown & SHUTDOWN_MASK) != SHUTDOWN_MASK)
		(void)virtio_transport_shutdown(vsk, SHUTDOWN_MASK);

	if (sock_flag(sk, SOCK_LINGER) && !(current->flags & PF_EXITING))
		virtio_transport_wait_close(sk, sk->sk_lingertime);

	if (sock_flag(sk, SOCK_DONE)) {
		return true;
	}

	sock_hold(sk);
	INIT_DELAYED_WORK(&vsk->close_work,
			  virtio_transport_close_timeout);
	vsk->close_work_scheduled = true;
	schedule_delayed_work(&vsk->close_work, VSOCK_CLOSE_TIMEOUT);
	return false;
}

void virtio_transport_release(struct vsock_sock *vsk)
{
	struct sock *sk = &vsk->sk;
	bool remove_sock = true;

	if (sk->sk_type == SOCK_STREAM || sk->sk_type == SOCK_SEQPACKET)
		remove_sock = virtio_transport_close(vsk);

	if (remove_sock) {
		sock_set_flag(sk, SOCK_DONE);
		virtio_transport_remove_sock(vsk);
	}
}
EXPORT_SYMBOL_GPL(virtio_transport_release);

static int
virtio_transport_recv_connecting(struct sock *sk,
				 struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct vsock_sock *vsk = vsock_sk(sk);
	int skerr;
	int err;

	switch (le16_to_cpu(hdr->op)) {
	case VIRTIO_VSOCK_OP_RESPONSE:
		sk->sk_state = TCP_ESTABLISHED;
		sk->sk_socket->state = SS_CONNECTED;
		vsock_insert_connected(vsk);
		sk->sk_state_change(sk);
		break;
	case VIRTIO_VSOCK_OP_INVALID:
		break;
	case VIRTIO_VSOCK_OP_RST:
		skerr = ECONNRESET;
		err = 0;
		goto destroy;
	default:
		skerr = EPROTO;
		err = -EINVAL;
		goto destroy;
	}
	return 0;

destroy:
	virtio_transport_reset(vsk, skb);
	sk->sk_state = TCP_CLOSE;
	sk->sk_err = skerr;
	sk_error_report(sk);
	return err;
}

static void
virtio_transport_recv_enqueue(struct vsock_sock *vsk,
			      struct sk_buff *skb)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	bool can_enqueue, free_pkt = false;
	struct virtio_vsock_hdr *hdr;
	u32 len;

	hdr = virtio_vsock_hdr(skb);
	len = le32_to_cpu(hdr->len);

	spin_lock_bh(&vvs->rx_lock);

	can_enqueue = virtio_transport_inc_rx_pkt(vvs, len);
	if (!can_enqueue) {
		free_pkt = true;
		goto out;
	}

	if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SEQ_EOM)
		vvs->msg_count++;

	/* Try to copy small packets into the buffer of last packet queued,
	 * to avoid wasting memory queueing the entire buffer with a small
	 * payload.
	 */
	if (len <= GOOD_COPY_LEN && !skb_queue_empty(&vvs->rx_queue)) {
		struct virtio_vsock_hdr *last_hdr;
		struct sk_buff *last_skb;

		last_skb = skb_peek_tail(&vvs->rx_queue);
		last_hdr = virtio_vsock_hdr(last_skb);

		/* If there is space in the last packet queued, we copy the
		 * new packet in its buffer. We avoid this if the last packet
		 * queued has VIRTIO_VSOCK_SEQ_EOM set, because this is
		 * delimiter of SEQPACKET message, so 'pkt' is the first packet
		 * of a new message.
		 */
		if (skb->len < skb_tailroom(last_skb) &&
		    !(le32_to_cpu(last_hdr->flags) & VIRTIO_VSOCK_SEQ_EOM)) {
			memcpy(skb_put(last_skb, skb->len), skb->data, skb->len);
			free_pkt = true;
			last_hdr->flags |= hdr->flags;
			le32_add_cpu(&last_hdr->len, len);
			goto out;
		}
	}

	__skb_queue_tail(&vvs->rx_queue, skb);

out:
	spin_unlock_bh(&vvs->rx_lock);
	if (free_pkt)
		kfree_skb(skb);
}

static int
virtio_transport_recv_connected(struct sock *sk,
				struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct vsock_sock *vsk = vsock_sk(sk);
	int err = 0;

	switch (le16_to_cpu(hdr->op)) {
	case VIRTIO_VSOCK_OP_RW:
		virtio_transport_recv_enqueue(vsk, skb);
		vsock_data_ready(sk);
		return err;
	case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
		virtio_transport_send_credit_update(vsk);
		break;
	case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
		sk->sk_write_space(sk);
		break;
	case VIRTIO_VSOCK_OP_SHUTDOWN:
		if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SHUTDOWN_RCV)
			vsk->peer_shutdown |= RCV_SHUTDOWN;
		if (le32_to_cpu(hdr->flags) & VIRTIO_VSOCK_SHUTDOWN_SEND)
			vsk->peer_shutdown |= SEND_SHUTDOWN;
		if (vsk->peer_shutdown == SHUTDOWN_MASK) {
			if (vsock_stream_has_data(vsk) <= 0 && !sock_flag(sk, SOCK_DONE)) {
				(void)virtio_transport_reset(vsk, NULL);
				virtio_transport_do_close(vsk, true);
			}
			/* Remove this socket anyway because the remote peer sent
			 * the shutdown. This way a new connection will succeed
			 * if the remote peer uses the same source port,
			 * even if the old socket is still unreleased, but now disconnected.
			 */
			vsock_remove_sock(vsk);
		}
		if (le32_to_cpu(virtio_vsock_hdr(skb)->flags))
			sk->sk_state_change(sk);
		break;
	case VIRTIO_VSOCK_OP_RST:
		virtio_transport_do_close(vsk, true);
		break;
	default:
		err = -EINVAL;
		break;
	}

	kfree_skb(skb);
	return err;
}

static void
virtio_transport_recv_disconnecting(struct sock *sk,
				    struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct vsock_sock *vsk = vsock_sk(sk);

	if (le16_to_cpu(hdr->op) == VIRTIO_VSOCK_OP_RST)
		virtio_transport_do_close(vsk, true);
}

static int
virtio_transport_send_response(struct vsock_sock *vsk,
			       struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RESPONSE,
		.remote_cid = le64_to_cpu(hdr->src_cid),
		.remote_port = le32_to_cpu(hdr->src_port),
		.reply = true,
		.vsk = vsk,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}

static bool virtio_transport_space_update(struct sock *sk,
					  struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct vsock_sock *vsk = vsock_sk(sk);
	struct virtio_vsock_sock *vvs = vsk->trans;
	bool space_available;

	/* Listener sockets are not associated with any transport, so we are
	 * not able to take the state to see if there is space available in the
	 * remote peer, but since they are only used to receive requests, we
	 * can assume that there is always space available in the other peer.
	 */
	if (!vvs)
		return true;

	/* buf_alloc and fwd_cnt is always included in the hdr */
	spin_lock_bh(&vvs->tx_lock);
	vvs->peer_buf_alloc = le32_to_cpu(hdr->buf_alloc);
	vvs->peer_fwd_cnt = le32_to_cpu(hdr->fwd_cnt);
	space_available = virtio_transport_has_space(vsk);
	spin_unlock_bh(&vvs->tx_lock);
	return space_available;
}

/* Handle server socket */
static int
virtio_transport_recv_listen(struct sock *sk, struct sk_buff *skb,
			     struct virtio_transport *t)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct vsock_sock *vsk = vsock_sk(sk);
	struct vsock_sock *vchild;
	struct sock *child;
	int ret;

	if (le16_to_cpu(hdr->op) != VIRTIO_VSOCK_OP_REQUEST) {
		virtio_transport_reset_no_sock(t, skb);
		return -EINVAL;
	}

	if (sk_acceptq_is_full(sk)) {
		virtio_transport_reset_no_sock(t, skb);
		return -ENOMEM;
	}

	child = vsock_create_connected(sk);
	if (!child) {
		virtio_transport_reset_no_sock(t, skb);
		return -ENOMEM;
	}

	sk_acceptq_added(sk);

	lock_sock_nested(child, SINGLE_DEPTH_NESTING);

	child->sk_state = TCP_ESTABLISHED;

	vchild = vsock_sk(child);
	vsock_addr_init(&vchild->local_addr, le64_to_cpu(hdr->dst_cid),
			le32_to_cpu(hdr->dst_port));
	vsock_addr_init(&vchild->remote_addr, le64_to_cpu(hdr->src_cid),
			le32_to_cpu(hdr->src_port));

	ret = vsock_assign_transport(vchild, vsk);
	/* Transport assigned (looking at remote_addr) must be the same
	 * where we received the request.
	 */
	if (ret || vchild->transport != &t->transport) {
		release_sock(child);
		virtio_transport_reset_no_sock(t, skb);
		sock_put(child);
		return ret;
	}

	if (virtio_transport_space_update(child, skb))
		child->sk_write_space(child);

	vsock_insert_connected(vchild);
	vsock_enqueue_accept(sk, child);
	virtio_transport_send_response(vchild, skb);

	release_sock(child);

	sk->sk_data_ready(sk);
	return 0;
}

static bool virtio_transport_valid_type(u16 type)
{
	return (type == VIRTIO_VSOCK_TYPE_STREAM) ||
	       (type == VIRTIO_VSOCK_TYPE_SEQPACKET);
}

/* We are under the virtio-vsock's vsock->rx_lock or vhost-vsock's vq->mutex
 * lock.
 */
void virtio_transport_recv_pkt(struct virtio_transport *t,
			       struct sk_buff *skb)
{
	struct virtio_vsock_hdr *hdr = virtio_vsock_hdr(skb);
	struct sockaddr_vm src, dst;
	struct vsock_sock *vsk;
	struct sock *sk;
	bool space_available;

	vsock_addr_init(&src, le64_to_cpu(hdr->src_cid),
			le32_to_cpu(hdr->src_port));
	vsock_addr_init(&dst, le64_to_cpu(hdr->dst_cid),
			le32_to_cpu(hdr->dst_port));

	trace_virtio_transport_recv_pkt(src.svm_cid, src.svm_port,
					dst.svm_cid, dst.svm_port,
					le32_to_cpu(hdr->len),
					le16_to_cpu(hdr->type),
					le16_to_cpu(hdr->op),
					le32_to_cpu(hdr->flags),
					le32_to_cpu(hdr->buf_alloc),
					le32_to_cpu(hdr->fwd_cnt));

	if (!virtio_transport_valid_type(le16_to_cpu(hdr->type))) {
		(void)virtio_transport_reset_no_sock(t, skb);
		goto free_pkt;
	}

	/* The socket must be in connected or bound table
	 * otherwise send reset back
	 */
	sk = vsock_find_connected_socket(&src, &dst);
	if (!sk) {
		sk = vsock_find_bound_socket(&dst);
		if (!sk) {
			(void)virtio_transport_reset_no_sock(t, skb);
			goto free_pkt;
		}
	}

	if (virtio_transport_get_type(sk) != le16_to_cpu(hdr->type)) {
		(void)virtio_transport_reset_no_sock(t, skb);
		sock_put(sk);
		goto free_pkt;
	}

	if (!skb_set_owner_sk_safe(skb, sk)) {
		WARN_ONCE(1, "receiving vsock socket has sk_refcnt == 0\n");
		goto free_pkt;
	}

	vsk = vsock_sk(sk);

	lock_sock(sk);

	/* Check if sk has been closed before lock_sock */
	if (sock_flag(sk, SOCK_DONE)) {
		(void)virtio_transport_reset_no_sock(t, skb);
		release_sock(sk);
		sock_put(sk);
		goto free_pkt;
	}

	space_available = virtio_transport_space_update(sk, skb);

	/* Update CID in case it has changed after a transport reset event */
	if (vsk->local_addr.svm_cid != VMADDR_CID_ANY)
		vsk->local_addr.svm_cid = dst.svm_cid;

	if (space_available)
		sk->sk_write_space(sk);

	switch (sk->sk_state) {
	case TCP_LISTEN:
		virtio_transport_recv_listen(sk, skb, t);
		kfree_skb(skb);
		break;
	case TCP_SYN_SENT:
		virtio_transport_recv_connecting(sk, skb);
		kfree_skb(skb);
		break;
	case TCP_ESTABLISHED:
		virtio_transport_recv_connected(sk, skb);
		break;
	case TCP_CLOSING:
		virtio_transport_recv_disconnecting(sk, skb);
		kfree_skb(skb);
		break;
	default:
		(void)virtio_transport_reset_no_sock(t, skb);
		kfree_skb(skb);
		break;
	}

	release_sock(sk);

	/* Release refcnt obtained when we fetched this socket out of the
	 * bound or connected list.
	 */
	sock_put(sk);
	return;

free_pkt:
	kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(virtio_transport_recv_pkt);

/* Remove skbs found in a queue that have a vsk that matches.
 *
 * Each skb is freed.
 *
 * Returns the count of skbs that were reply packets.
 */
int virtio_transport_purge_skbs(void *vsk, struct sk_buff_head *queue)
{
	struct sk_buff_head freeme;
	struct sk_buff *skb, *tmp;
	int cnt = 0;

	skb_queue_head_init(&freeme);

	spin_lock_bh(&queue->lock);
	skb_queue_walk_safe(queue, skb, tmp) {
		if (vsock_sk(skb->sk) != vsk)
			continue;

		__skb_unlink(skb, queue);
		__skb_queue_tail(&freeme, skb);

		if (virtio_vsock_skb_reply(skb))
			cnt++;
	}
	spin_unlock_bh(&queue->lock);

	__skb_queue_purge(&freeme);

	return cnt;
}
EXPORT_SYMBOL_GPL(virtio_transport_purge_skbs);

int virtio_transport_read_skb(struct vsock_sock *vsk, skb_read_actor_t recv_actor)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	struct sock *sk = sk_vsock(vsk);
	struct sk_buff *skb;
	int off = 0;
	int err;

	spin_lock_bh(&vvs->rx_lock);
	/* Use __skb_recv_datagram() for race-free handling of the receive. It
	 * works for types other than dgrams.
	 */
	skb = __skb_recv_datagram(sk, &vvs->rx_queue, MSG_DONTWAIT, &off, &err);
	spin_unlock_bh(&vvs->rx_lock);

	if (!skb)
		return err;

	return recv_actor(sk, skb);
}
EXPORT_SYMBOL_GPL(virtio_transport_read_skb);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Asias He");
MODULE_DESCRIPTION("common code for virtio vsock");
