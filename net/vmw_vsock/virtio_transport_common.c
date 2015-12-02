/*
 * common code for virtio vsock
 *
 * Copyright (C) 2013-2015 Red Hat, Inc.
 * Author: Asias He <asias@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_vsock.h>
#include <linux/random.h>
#include <linux/cryptohash.h>

#include <net/sock.h>
#include <net/af_vsock.h>

#define COOKIEBITS 24
#define COOKIEMASK (((u32)1 << COOKIEBITS) - 1)
#define VSOCK_TIMEOUT_INIT 4

#define SHA_MESSAGE_WORDS 16
#define SHA_VSOCK_WORDS 5

static u32 vsockcookie_secret[2][SHA_MESSAGE_WORDS - SHA_VSOCK_WORDS +
				 SHA_DIGEST_WORDS];

static DEFINE_PER_CPU(__u32[SHA_MESSAGE_WORDS + SHA_DIGEST_WORDS +
		      SHA_WORKSPACE_WORDS], vsock_cookie_scratch);

static u32 cookie_hash(u32 saddr, u32 daddr, u16 sport, u16 dport,
		       u32 count, int c)
{
	__u32 *tmp = this_cpu_ptr(vsock_cookie_scratch);

	memcpy(tmp + SHA_VSOCK_WORDS, vsockcookie_secret[c],
	       sizeof(vsockcookie_secret[c]));
	tmp[0] = saddr;
	tmp[1] = daddr;
	tmp[2] = sport;
	tmp[3] = dport;
	tmp[4] = count;
	sha_transform(tmp + SHA_MESSAGE_WORDS, (__u8 *)tmp,
		      tmp + SHA_MESSAGE_WORDS + SHA_DIGEST_WORDS);

	return tmp[17];
}

static u32
virtio_vsock_secure_cookie(u32 saddr, u32 daddr, u32 sport, u32 dport,
			   u32 count)
{
	u32 h1, h2;

	h1 = cookie_hash(saddr, daddr, sport, dport, 0, 0);
	h2 = cookie_hash(saddr, daddr, sport, dport, count, 1);

	return h1 + (count << COOKIEBITS) + (h2 & COOKIEMASK);
}

static u32
virtio_vsock_check_cookie(u32 saddr, u32 daddr, u32 sport, u32 dport,
			  u32 count, u32 cookie, u32 maxdiff)
{
	u32 diff;
	u32 ret;

	cookie -= cookie_hash(saddr, daddr, sport, dport, 0, 0);

	diff = (count - (cookie >> COOKIEBITS)) & ((u32)-1 >> COOKIEBITS);
	pr_debug("%s: diff=%x\n", __func__, diff);
	if (diff >= maxdiff)
		return (u32)-1;

	ret = (cookie -
		cookie_hash(saddr, daddr, sport, dport, count - diff, 1))
		& COOKIEMASK;
	pr_debug("%s: ret=%x\n", __func__, diff);

	return ret;
}

void virtio_vsock_dumppkt(const char *func,  const struct virtio_vsock_pkt *pkt)
{
	pr_debug("%s: pkt=%p, op=%d, len=%d, %d:%d---%d:%d, len=%d\n",
		 func, pkt,
		 le16_to_cpu(pkt->hdr.op),
		 le32_to_cpu(pkt->hdr.len),
		 le32_to_cpu(pkt->hdr.src_cid),
		 le32_to_cpu(pkt->hdr.src_port),
		 le32_to_cpu(pkt->hdr.dst_cid),
		 le32_to_cpu(pkt->hdr.dst_port),
		 pkt->len);
}
EXPORT_SYMBOL_GPL(virtio_vsock_dumppkt);

struct virtio_vsock_pkt *
virtio_transport_alloc_pkt(struct vsock_sock *vsk,
			   struct virtio_vsock_pkt_info *info,
			   size_t len,
			   u32 src_cid,
			   u32 src_port,
			   u32 dst_cid,
			   u32 dst_port)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt *pkt;
	int err;

	BUG_ON(!trans);

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return NULL;

	pkt->hdr.type		= cpu_to_le16(info->type);
	pkt->hdr.op		= cpu_to_le16(info->op);
	pkt->hdr.src_cid	= cpu_to_le32(src_cid);
	pkt->hdr.src_port	= cpu_to_le32(src_port);
	pkt->hdr.dst_cid	= cpu_to_le32(dst_cid);
	pkt->hdr.dst_port	= cpu_to_le32(dst_port);
	pkt->hdr.flags		= cpu_to_le32(info->flags);
	pkt->len		= len;
	pkt->trans		= trans;
	if (info->type == VIRTIO_VSOCK_TYPE_DGRAM)
		pkt->hdr.len	= cpu_to_le32(len + (info->dgram_len << 16));
	else if (info->type == VIRTIO_VSOCK_TYPE_STREAM)
		pkt->hdr.len	= cpu_to_le32(len);

	if (info->msg && len > 0) {
		pkt->buf = kmalloc(len, GFP_KERNEL);
		if (!pkt->buf)
			goto out_pkt;
		err = memcpy_from_msg(pkt->buf, info->msg, len);
		if (err)
			goto out;
	}

	return pkt;

out:
	kfree(pkt->buf);
out_pkt:
	kfree(pkt);
	return NULL;
}
EXPORT_SYMBOL_GPL(virtio_transport_alloc_pkt);

struct sock *
virtio_transport_get_pending(struct sock *listener,
			     struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vlistener;
	struct vsock_sock *vpending;
	struct sockaddr_vm src;
	struct sockaddr_vm dst;
	struct sock *pending;

	vsock_addr_init(&src, le32_to_cpu(pkt->hdr.src_cid), le32_to_cpu(pkt->hdr.src_port));
	vsock_addr_init(&dst, le32_to_cpu(pkt->hdr.dst_cid), le32_to_cpu(pkt->hdr.dst_port));

	vlistener = vsock_sk(listener);
	list_for_each_entry(vpending, &vlistener->pending_links,
			    pending_links) {
		if (vsock_addr_equals_addr(&src, &vpending->remote_addr) &&
		    vsock_addr_equals_addr(&dst, &vpending->local_addr)) {
			pending = sk_vsock(vpending);
			sock_hold(pending);
			return pending;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_pending);

static void virtio_transport_inc_rx_pkt(struct virtio_vsock_pkt *pkt)
{
	pkt->trans->rx_bytes += pkt->len;
}

static void virtio_transport_dec_rx_pkt(struct virtio_vsock_pkt *pkt)
{
	pkt->trans->rx_bytes -= pkt->len;
	pkt->trans->fwd_cnt += pkt->len;
}

void virtio_transport_inc_tx_pkt(struct virtio_vsock_pkt *pkt)
{
	mutex_lock(&pkt->trans->tx_lock);
	pkt->hdr.fwd_cnt = cpu_to_le32(pkt->trans->fwd_cnt);
	pkt->hdr.buf_alloc = cpu_to_le32(pkt->trans->buf_alloc);
	mutex_unlock(&pkt->trans->tx_lock);
}
EXPORT_SYMBOL_GPL(virtio_transport_inc_tx_pkt);

void virtio_transport_dec_tx_pkt(struct virtio_vsock_pkt *pkt)
{
}
EXPORT_SYMBOL_GPL(virtio_transport_dec_tx_pkt);

u32 virtio_transport_get_credit(struct virtio_transport *trans, u32 credit)
{
	u32 ret;

	mutex_lock(&trans->tx_lock);
	ret = trans->peer_buf_alloc - (trans->tx_cnt - trans->peer_fwd_cnt);
	if (ret > credit)
		ret = credit;
	trans->tx_cnt += ret;
	mutex_unlock(&trans->tx_lock);

	pr_debug("%s: ret=%d, buf_alloc=%d, peer_buf_alloc=%d,"
		 "tx_cnt=%d, fwd_cnt=%d, peer_fwd_cnt=%d\n", __func__,
		 ret, trans->buf_alloc, trans->peer_buf_alloc,
		 trans->tx_cnt, trans->fwd_cnt, trans->peer_fwd_cnt);

	return ret;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_credit);

void virtio_transport_put_credit(struct virtio_transport *trans, u32 credit)
{
	mutex_lock(&trans->tx_lock);
	trans->tx_cnt -= credit;
	mutex_unlock(&trans->tx_lock);
}
EXPORT_SYMBOL_GPL(virtio_transport_put_credit);

static int virtio_transport_send_credit_update(struct vsock_sock *vsk, int type, struct virtio_vsock_hdr *hdr)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_CREDIT_UPDATE,
		.type = type,
	};

	if (hdr && type == VIRTIO_VSOCK_TYPE_DGRAM) {
		info.remote_cid = le32_to_cpu(hdr->src_cid);
		info.remote_port = le32_to_cpu(hdr->src_port);
	}

	pr_debug("%s: sk=%p send_credit_update\n", __func__, vsk);
	return trans->ops->send_pkt(vsk, &info);
}

static int virtio_transport_send_credit_request(struct vsock_sock *vsk, int type)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_CREDIT_REQUEST,
		.type = type,
	};

	pr_debug("%s: sk=%p send_credit_request\n", __func__, vsk);
	return trans->ops->send_pkt(vsk, &info);
}

static ssize_t
virtio_transport_stream_do_dequeue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   size_t len)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt *pkt;
	size_t bytes, total = 0;
	int err = -EFAULT;

	mutex_lock(&trans->rx_lock);
	while (total < len && trans->rx_bytes > 0  &&
			!list_empty(&trans->rx_queue)) {
		pkt = list_first_entry(&trans->rx_queue,
				       struct virtio_vsock_pkt, list);

		bytes = len - total;
		if (bytes > pkt->len - pkt->off)
			bytes = pkt->len - pkt->off;

		err = memcpy_to_msg(msg, pkt->buf + pkt->off, bytes);
		if (err)
			goto out;
		total += bytes;
		pkt->off += bytes;
		if (pkt->off == pkt->len) {
			virtio_transport_dec_rx_pkt(pkt);
			list_del(&pkt->list);
			virtio_transport_free_pkt(pkt);
		}
	}
	mutex_unlock(&trans->rx_lock);

	/* Send a credit pkt to peer */
	virtio_transport_send_credit_update(vsk, VIRTIO_VSOCK_TYPE_STREAM,
					    NULL);

	return total;

out:
	mutex_unlock(&trans->rx_lock);
	if (total)
		err = total;
	return err;
}

ssize_t
virtio_transport_stream_dequeue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len, int flags)
{
	if (flags & MSG_PEEK)
		return -EOPNOTSUPP;

	return virtio_transport_stream_do_dequeue(vsk, msg, len);
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_dequeue);

struct dgram_skb {
	struct list_head list;
	struct sk_buff *skb;
	u16 id;
};

static struct dgram_skb *dgram_id_to_skb(struct virtio_transport *trans,
					 u16 id)
{
	struct dgram_skb *dgram_skb;

	list_for_each_entry(dgram_skb, &trans->incomplete_dgrams, list) {
		if (dgram_skb->id == id)
			return dgram_skb;
	}

	return NULL;
}

static void
virtio_transport_recv_dgram(struct sock *sk,
			    struct virtio_vsock_pkt *pkt)
{
	struct sk_buff *skb = NULL;
	struct vsock_sock *vsk;
	struct virtio_transport *trans;
	size_t size;
	u16 dgram_id, pkt_off, dgram_len, pkt_len;
	u32 flags, len;
	struct dgram_skb *dgram_skb;

	vsk = vsock_sk(sk);
	trans = vsk->trans;

	/* len:   dgram_len | pkt_len */
	len = le32_to_cpu(pkt->hdr.len);
	dgram_len = len >> 16;
	pkt_len = len & 0xFFFF;

	/* flags: dgram_id  | pkt_off */
	flags = le32_to_cpu(pkt->hdr.flags);
	dgram_id = flags >> 16;
	pkt_off = flags & 0xFFFF;

	pr_debug("%s: dgram_len=%d, pkt_len=%d, id=%d, off=%d\n", __func__,
		 dgram_len, pkt_len, dgram_id, pkt_off);

	dgram_skb = dgram_id_to_skb(trans, dgram_id);
	if (dgram_skb) {
		/* This pkt is for a existing dgram */
		skb = dgram_skb->skb;
		pr_debug("%s:found skb\n", __func__);
	}

	/* Packet payload must be within datagram bounds */
	if (pkt_len > VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE)
		goto drop;
	if (pkt_len > dgram_len)
		goto drop;
	if (pkt_off > dgram_len)
		goto drop;
	if (dgram_len - pkt_off < pkt_len)
		goto drop;

	if (!skb) {
		/* This pkt is for a new dgram */
		pr_debug("%s:create skb\n", __func__);

		size = sizeof(pkt->hdr) + dgram_len;
		/* Attach the packet to the socket's receive queue as an sk_buff. */
		dgram_skb = kzalloc(sizeof(struct dgram_skb), GFP_ATOMIC);
		if (!dgram_skb)
			goto drop;

		skb = alloc_skb(size, GFP_ATOMIC);
		if (!skb) {
			kfree(dgram_skb);
			dgram_skb = NULL;
			goto drop;
		}
		dgram_skb->id = dgram_id;
		dgram_skb->skb = skb;
		list_add_tail(&dgram_skb->list, &trans->incomplete_dgrams);

		/* sk_receive_skb() will do a sock_put(), so hold here. */
		sock_hold(sk);
		skb_put(skb, size);
		memcpy(skb->data, &pkt->hdr, sizeof(pkt->hdr));
	}

	memcpy(skb->data + sizeof(pkt->hdr) + pkt_off, pkt->buf, pkt_len);

	pr_debug("%s:C, off=%d, pkt_len=%d, dgram_len=%d\n", __func__,
		 pkt_off, pkt_len, dgram_len);

	/* We are done with this dgram */
	if (pkt_off + pkt_len == dgram_len) {
		pr_debug("%s:dgram_id=%d is done\n", __func__, dgram_id);
		list_del(&dgram_skb->list);
		kfree(dgram_skb);
		sk_receive_skb(sk, skb, 0);
	}
	virtio_transport_free_pkt(pkt);
	return;

drop:
	if (dgram_skb) {
		list_del(&dgram_skb->list);
		kfree(dgram_skb);
		kfree_skb(skb);
		sock_put(sk);
	}
	virtio_transport_free_pkt(pkt);
}

int
virtio_transport_dgram_dequeue(struct vsock_sock *vsk,
			       struct msghdr *msg,
			       size_t len, int flags)
{
	struct virtio_vsock_hdr *hdr;
	struct sk_buff *skb;
	int noblock;
	int err;
	int dgram_len;

	noblock = flags & MSG_DONTWAIT;

	if (flags & MSG_OOB || flags & MSG_ERRQUEUE)
		return -EOPNOTSUPP;

	/* Retrieve the head sk_buff from the socket's receive queue. */
	err = 0;
	skb = skb_recv_datagram(&vsk->sk, flags, noblock, &err);
	if (err)
		return err;
	if (!skb)
		return -EAGAIN;

	hdr = (struct virtio_vsock_hdr *)skb->data;
	if (!hdr)
		goto out;

	dgram_len = le32_to_cpu(hdr->len) >> 16;
	/* Place the datagram payload in the user's iovec. */
	err = skb_copy_datagram_msg(skb, sizeof(*hdr), msg, dgram_len);
	if (err)
		goto out;

	if (msg->msg_name) {
		/* Provide the address of the sender. */
		DECLARE_SOCKADDR(struct sockaddr_vm *, vm_addr, msg->msg_name);
		vsock_addr_init(vm_addr, le32_to_cpu(hdr->src_cid), le32_to_cpu(hdr->src_port));
		msg->msg_namelen = sizeof(*vm_addr);
	}
	err = dgram_len;

	/* Send a credit pkt to peer */
	virtio_transport_send_credit_update(vsk, VIRTIO_VSOCK_TYPE_DGRAM, hdr);

	pr_debug("%s:done, recved =%d\n", __func__, dgram_len);
out:
	skb_free_datagram(&vsk->sk, skb);
	return err;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_dequeue);

s64 virtio_transport_stream_has_data(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;
	s64 bytes;

	mutex_lock(&trans->rx_lock);
	bytes = trans->rx_bytes;
	mutex_unlock(&trans->rx_lock);

	return bytes;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_has_data);

static s64 virtio_transport_has_space(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;
	s64 bytes;

	bytes = trans->peer_buf_alloc - (trans->tx_cnt - trans->peer_fwd_cnt);
	if (bytes < 0)
		bytes = 0;

	return bytes;
}

s64 virtio_transport_stream_has_space(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;
	s64 bytes;

	mutex_lock(&trans->tx_lock);
	bytes = virtio_transport_has_space(vsk);
	mutex_unlock(&trans->tx_lock);

	pr_debug("%s: bytes=%lld\n", __func__, bytes);

	return bytes;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_has_space);

int virtio_transport_do_socket_init(struct vsock_sock *vsk,
				    struct vsock_sock *psk)
{
	struct virtio_transport *trans;

	trans = kzalloc(sizeof(*trans), GFP_KERNEL);
	if (!trans)
		return -ENOMEM;

	vsk->trans = trans;
	trans->vsk = vsk;
	if (psk) {
		struct virtio_transport *ptrans = psk->trans;
		trans->buf_size	= ptrans->buf_size;
		trans->buf_size_min = ptrans->buf_size_min;
		trans->buf_size_max = ptrans->buf_size_max;
		trans->peer_buf_alloc = ptrans->peer_buf_alloc;
	} else {
		trans->buf_size = VIRTIO_VSOCK_DEFAULT_BUF_SIZE;
		trans->buf_size_min = VIRTIO_VSOCK_DEFAULT_MIN_BUF_SIZE;
		trans->buf_size_max = VIRTIO_VSOCK_DEFAULT_MAX_BUF_SIZE;
	}

	trans->buf_alloc = trans->buf_size;

	pr_debug("%s: trans->buf_alloc=%d\n", __func__, trans->buf_alloc);

	mutex_init(&trans->rx_lock);
	mutex_init(&trans->tx_lock);
	INIT_LIST_HEAD(&trans->rx_queue);
	INIT_LIST_HEAD(&trans->incomplete_dgrams);

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_do_socket_init);

u64 virtio_transport_get_buffer_size(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;

	return trans->buf_size;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_buffer_size);

u64 virtio_transport_get_min_buffer_size(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;

	return trans->buf_size_min;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_min_buffer_size);

u64 virtio_transport_get_max_buffer_size(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;

	return trans->buf_size_max;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_max_buffer_size);

void virtio_transport_set_buffer_size(struct vsock_sock *vsk, u64 val)
{
	struct virtio_transport *trans = vsk->trans;

	if (val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		val = VIRTIO_VSOCK_MAX_BUF_SIZE;
	if (val < trans->buf_size_min)
		trans->buf_size_min = val;
	if (val > trans->buf_size_max)
		trans->buf_size_max = val;
	trans->buf_size = val;
	trans->buf_alloc = val;
}
EXPORT_SYMBOL_GPL(virtio_transport_set_buffer_size);

void virtio_transport_set_min_buffer_size(struct vsock_sock *vsk, u64 val)
{
	struct virtio_transport *trans = vsk->trans;

	if (val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		val = VIRTIO_VSOCK_MAX_BUF_SIZE;
	if (val > trans->buf_size)
		trans->buf_size = val;
	trans->buf_size_min = val;
}
EXPORT_SYMBOL_GPL(virtio_transport_set_min_buffer_size);

void virtio_transport_set_max_buffer_size(struct vsock_sock *vsk, u64 val)
{
	struct virtio_transport *trans = vsk->trans;

	if (val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		val = VIRTIO_VSOCK_MAX_BUF_SIZE;
	if (val < trans->buf_size)
		trans->buf_size = val;
	trans->buf_size_max = val;
}
EXPORT_SYMBOL_GPL(virtio_transport_set_max_buffer_size);

int
virtio_transport_notify_poll_in(struct vsock_sock *vsk,
				size_t target,
				bool *data_ready_now)
{
	if (vsock_stream_has_data(vsk))
		*data_ready_now = true;
	else
		*data_ready_now = false;

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
	struct virtio_transport *trans = vsk->trans;

	return trans->buf_size;
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
	return vsock_bind_dgram_generic(vsk, addr);
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_bind);

bool virtio_transport_dgram_allow(u32 cid, u32 port)
{
	return true;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_allow);

int virtio_transport_connect(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_REQUEST,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
	};

	pr_debug("%s: vsk=%p send_request\n", __func__, vsk);
	return trans->ops->send_pkt(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_connect);

int virtio_transport_shutdown(struct vsock_sock *vsk, int mode)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_SHUTDOWN,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.flags = (mode & RCV_SHUTDOWN ?
			  VIRTIO_VSOCK_SHUTDOWN_RCV : 0) |
			 (mode & SEND_SHUTDOWN ?
			  VIRTIO_VSOCK_SHUTDOWN_SEND : 0),
	};

	pr_debug("%s: vsk=%p: send_shutdown\n", __func__, vsk);
	return trans->ops->send_pkt(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_shutdown);

void virtio_transport_release(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;
	struct sock *sk = &vsk->sk;
	struct dgram_skb *dgram_skb;
	struct dgram_skb *dgram_skb_tmp;

	pr_debug("%s: vsk=%p\n", __func__, vsk);

	/* Tell other side to terminate connection */
	if (sk->sk_type == SOCK_STREAM && sk->sk_state == SS_CONNECTED) {
		virtio_transport_shutdown(vsk, SHUTDOWN_MASK);
	}

	/* Free incomplete dgrams */
	lock_sock(sk);
	list_for_each_entry_safe(dgram_skb, dgram_skb_tmp,
				 &trans->incomplete_dgrams, list) {
		list_del(&dgram_skb->list);
		kfree_skb(dgram_skb->skb);
		kfree(dgram_skb);
		sock_put(sk); /* held in virtio_transport_recv_dgram() */
	}
	release_sock(sk);
}
EXPORT_SYMBOL_GPL(virtio_transport_release);

int
virtio_transport_dgram_enqueue(struct vsock_sock *vsk,
			       struct sockaddr_vm *remote_addr,
			       struct msghdr *msg,
			       size_t dgram_len)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RW,
		.type = VIRTIO_VSOCK_TYPE_DGRAM,
		.msg = msg,
	};
	size_t total_written = 0, pkt_off = 0, written;
	u16 dgram_id;

	/* The max size of a single dgram we support is 64KB */
	if (dgram_len > VIRTIO_VSOCK_MAX_DGRAM_SIZE)
		return -EMSGSIZE;

	info.dgram_len = dgram_len;
	vsk->remote_addr = *remote_addr;

	dgram_id = trans->dgram_id++;

	/* TODO: To optimize, if we have enough credit to send the pkt already,
	 * do not ask the peer to send credit to use  */
	virtio_transport_send_credit_request(vsk, VIRTIO_VSOCK_TYPE_DGRAM);

	while (total_written < dgram_len) {
		info.pkt_len = dgram_len - total_written;
		info.flags = dgram_id << 16 | pkt_off;
		written = trans->ops->send_pkt(vsk, &info);
		if (written < 0)
			return -ENOMEM;
		if (written == 0) {
			/* TODO: if written = 0, we need a sleep & wakeup
			 * instead of sleep */
			pr_debug("%s: SHOULD WAIT written==0", __func__);
			msleep(10);
		}
		total_written += written;
		pkt_off += written;
		pr_debug("%s:id=%d, dgram_len=%zu, off=%zu, total_written=%zu, written=%zu\n",
			  __func__, dgram_id, dgram_len, pkt_off, total_written, written);
	}

	return dgram_len;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_enqueue);

ssize_t
virtio_transport_stream_enqueue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RW,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.msg = msg,
		.pkt_len = len,
	};

	return trans->ops->send_pkt(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_enqueue);

void virtio_transport_destruct(struct vsock_sock *vsk)
{
	struct virtio_transport *trans = vsk->trans;

	pr_debug("%s: vsk=%p\n", __func__, vsk);
	kfree(trans);
}
EXPORT_SYMBOL_GPL(virtio_transport_destruct);

static int virtio_transport_send_ack(struct vsock_sock *vsk, u32 cookie)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_ACK,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.flags = cpu_to_le32(cookie),
	};

	pr_debug("%s: sk=%p send_offer\n", __func__, vsk);
	return trans->ops->send_pkt(vsk, &info);
}

static int virtio_transport_send_reset(struct vsock_sock *vsk,
				       struct virtio_vsock_pkt *pkt)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RST,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
	};

	pr_debug("%s\n", __func__);

	/* Send RST only if the original pkt is not a RST pkt */
	if (le16_to_cpu(pkt->hdr.op) == VIRTIO_VSOCK_OP_RST)
		return 0;

	return trans->ops->send_pkt(vsk, &info);
}

static int
virtio_transport_recv_connecting(struct sock *sk,
				 struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	int err;
	int skerr;
	u32 cookie;

	pr_debug("%s: vsk=%p\n", __func__, vsk);
	switch (le16_to_cpu(pkt->hdr.op)) {
	case VIRTIO_VSOCK_OP_RESPONSE:
		cookie = le32_to_cpu(pkt->hdr.flags);
		pr_debug("%s: got RESPONSE and send ACK, cookie=%x\n", __func__, cookie);
		err = virtio_transport_send_ack(vsk, cookie);
		if (err < 0) {
			skerr = -err;
			goto destroy;
		}
		sk->sk_state = SS_CONNECTED;
		sk->sk_socket->state = SS_CONNECTED;
		vsock_insert_connected(vsk);
		sk->sk_state_change(sk);
		break;
	case VIRTIO_VSOCK_OP_INVALID:
		pr_debug("%s: got invalid\n", __func__);
		break;
	case VIRTIO_VSOCK_OP_RST:
		pr_debug("%s: got rst\n", __func__);
		skerr = ECONNRESET;
		err = 0;
		goto destroy;
	default:
		pr_debug("%s: got def\n", __func__);
		skerr = EPROTO;
		err = -EINVAL;
		goto destroy;
	}
	return 0;

destroy:
	virtio_transport_send_reset(vsk, pkt);
	sk->sk_state = SS_UNCONNECTED;
	sk->sk_err = skerr;
	sk->sk_error_report(sk);
	return err;
}

static int
virtio_transport_recv_connected(struct sock *sk,
				struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct virtio_transport *trans = vsk->trans;
	int err = 0;

	switch (le16_to_cpu(pkt->hdr.op)) {
	case VIRTIO_VSOCK_OP_RW:
		pkt->len = le32_to_cpu(pkt->hdr.len);
		pkt->off = 0;
		pkt->trans = trans;

		mutex_lock(&trans->rx_lock);
		virtio_transport_inc_rx_pkt(pkt);
		list_add_tail(&pkt->list, &trans->rx_queue);
		mutex_unlock(&trans->rx_lock);

		sk->sk_data_ready(sk);
		return err;
	case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
		sk->sk_write_space(sk);
		break;
	case VIRTIO_VSOCK_OP_SHUTDOWN:
		pr_debug("%s: got shutdown\n", __func__);
		if (le32_to_cpu(pkt->hdr.flags) & VIRTIO_VSOCK_SHUTDOWN_RCV)
			vsk->peer_shutdown |= RCV_SHUTDOWN;
		if (le32_to_cpu(pkt->hdr.flags) & VIRTIO_VSOCK_SHUTDOWN_SEND)
			vsk->peer_shutdown |= SEND_SHUTDOWN;
		if (le32_to_cpu(pkt->hdr.flags))
			sk->sk_state_change(sk);
		break;
	case VIRTIO_VSOCK_OP_RST:
		pr_debug("%s: got rst\n", __func__);
		sock_set_flag(sk, SOCK_DONE);
		vsk->peer_shutdown = SHUTDOWN_MASK;
		if (vsock_stream_has_data(vsk) <= 0)
			sk->sk_state = SS_DISCONNECTING;
		sk->sk_state_change(sk);
		break;
	default:
		err = -EINVAL;
		break;
	}

	virtio_transport_free_pkt(pkt);
	return err;
}

static int
virtio_transport_send_response(struct vsock_sock *vsk,
			       struct virtio_vsock_pkt *pkt)
{
	struct virtio_transport *trans = vsk->trans;
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RESPONSE,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.remote_cid = le32_to_cpu(pkt->hdr.src_cid),
		.remote_port = le32_to_cpu(pkt->hdr.src_port),
	};
	u32 cookie;

	cookie = virtio_vsock_secure_cookie(le32_to_cpu(pkt->hdr.src_cid),
					    le32_to_cpu(pkt->hdr.dst_cid),
					    le32_to_cpu(pkt->hdr.src_port),
					    le32_to_cpu(pkt->hdr.dst_port),
					    jiffies / (HZ * 60));
	info.flags = cpu_to_le32(cookie);

	pr_debug("%s: send_response, cookie=%x\n", __func__, le32_to_cpu(cookie));

	return trans->ops->send_pkt(vsk, &info);
}

/* Handle server socket */
static int
virtio_transport_recv_listen(struct sock *sk, struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct vsock_sock *vpending;
	struct sock *pending;
	int err;
	u32 cookie;

	switch (le16_to_cpu(pkt->hdr.op)) {
	case VIRTIO_VSOCK_OP_REQUEST:
		err = virtio_transport_send_response(vsk, pkt);
		if (err < 0) {
			// FIXME vsk should be vpending
			virtio_transport_send_reset(vsk, pkt);
			return err;
		}
		break;
	case VIRTIO_VSOCK_OP_ACK:
		cookie = le32_to_cpu(pkt->hdr.flags);
		err = virtio_vsock_check_cookie(le32_to_cpu(pkt->hdr.src_cid),
						le32_to_cpu(pkt->hdr.dst_cid),
						le32_to_cpu(pkt->hdr.src_port),
						le32_to_cpu(pkt->hdr.dst_port),
						jiffies / (HZ * 60),
						le32_to_cpu(pkt->hdr.flags),
						VSOCK_TIMEOUT_INIT);
		pr_debug("%s: cookie=%x, err=%d\n", __func__, cookie, err);
		if (err)
			return err;

		/* So no pending socket are responsible for this pkt, create one */
		pr_debug("%s: create pending\n", __func__);
		pending = __vsock_create(sock_net(sk), NULL, sk, GFP_KERNEL,
				sk->sk_type, 0);
		if (!pending) {
			virtio_transport_send_reset(vsk, pkt);
			return -ENOMEM;
		}
		sk->sk_ack_backlog++;
		pending->sk_state = SS_CONNECTING;

		vpending = vsock_sk(pending);
		vsock_addr_init(&vpending->local_addr, le32_to_cpu(pkt->hdr.dst_cid),
				le32_to_cpu(pkt->hdr.dst_port));
		vsock_addr_init(&vpending->remote_addr, le32_to_cpu(pkt->hdr.src_cid),
				le32_to_cpu(pkt->hdr.src_port));
		vsock_add_pending(sk, pending);

		pr_debug("%s: get pending\n", __func__);
		pending = virtio_transport_get_pending(sk, pkt);
		vpending = vsock_sk(pending);
		lock_sock(pending);
		switch (pending->sk_state) {
			case SS_CONNECTING:
				if (le16_to_cpu(pkt->hdr.op) != VIRTIO_VSOCK_OP_ACK) {
					pr_debug("%s: op=%d != OP_ACK\n", __func__,
						 le16_to_cpu(pkt->hdr.op));
					virtio_transport_send_reset(vpending, pkt);
					pending->sk_err = EPROTO;
					pending->sk_state = SS_UNCONNECTED;
					sock_put(pending);
				} else {
					pending->sk_state = SS_CONNECTED;
					vsock_insert_connected(vpending);

					vsock_remove_pending(sk, pending);
					vsock_enqueue_accept(sk, pending);

					sk->sk_data_ready(sk);
				}
				err = 0;
				break;
			default:
				pr_debug("%s: sk->sk_ack_backlog=%d\n", __func__,
						sk->sk_ack_backlog);
				virtio_transport_send_reset(vpending, pkt);
				err = -EINVAL;
				break;
		}
		if (err < 0)
			vsock_remove_pending(sk, pending);
		release_sock(pending);

		/* Release refcnt obtained in virtio_transport_get_pending */
		sock_put(pending);
		break;
	default:
		break;
	}

	return 0;
}

static void virtio_transport_space_update(struct sock *sk,
					  struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct virtio_transport *trans = vsk->trans;
	bool space_available;

	/* buf_alloc and fwd_cnt is always included in the hdr */
	mutex_lock(&trans->tx_lock);
	trans->peer_buf_alloc = le32_to_cpu(pkt->hdr.buf_alloc);
	trans->peer_fwd_cnt = le32_to_cpu(pkt->hdr.fwd_cnt);
	space_available = virtio_transport_has_space(vsk);
	mutex_unlock(&trans->tx_lock);

	if (space_available)
		sk->sk_write_space(sk);
}

/* We are under the virtio-vsock's vsock->rx_lock or
 * vhost-vsock's vq->mutex lock */
void virtio_transport_recv_pkt(struct virtio_vsock_pkt *pkt)
{
	struct virtio_transport *trans;
	struct sockaddr_vm src, dst;
	struct vsock_sock *vsk;
	struct sock *sk;

	vsock_addr_init(&src, le32_to_cpu(pkt->hdr.src_cid), le32_to_cpu(pkt->hdr.src_port));
	vsock_addr_init(&dst, le32_to_cpu(pkt->hdr.dst_cid), le32_to_cpu(pkt->hdr.dst_port));

	virtio_vsock_dumppkt(__func__, pkt);

	if (le16_to_cpu(pkt->hdr.type) == VIRTIO_VSOCK_TYPE_DGRAM) {
		sk = vsock_find_unbound_socket(&dst);
		if (!sk)
			goto free_pkt;

		vsk = vsock_sk(sk);
		trans = vsk->trans;
		BUG_ON(!trans);

		virtio_transport_space_update(sk, pkt);

		lock_sock(sk);
		switch (le16_to_cpu(pkt->hdr.op)) {
		case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
			virtio_transport_free_pkt(pkt);
			break;
		case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
			virtio_transport_send_credit_update(vsk, VIRTIO_VSOCK_TYPE_DGRAM,
							    &pkt->hdr);
			virtio_transport_free_pkt(pkt);
			break;
		case VIRTIO_VSOCK_OP_RW:
			virtio_transport_recv_dgram(sk, pkt);
			break;
		default:
			virtio_transport_free_pkt(pkt);
			break;
		}
		release_sock(sk);

		/* Release refcnt obtained when we fetched this socket out of
		 * the unbound list.
		 */
		sock_put(sk);
		return;
	} else if (le16_to_cpu(pkt->hdr.type) == VIRTIO_VSOCK_TYPE_STREAM) {
		/* The socket must be in connected or bound table
		 * otherwise send reset back
		 */
		sk = vsock_find_connected_socket(&src, &dst);
		if (!sk) {
			sk = vsock_find_bound_socket(&dst);
			if (!sk) {
				pr_debug("%s: can not find bound_socket\n", __func__);
				virtio_vsock_dumppkt(__func__, pkt);
				/* Ignore this pkt instead of sending reset back */
				/* TODO send a RST unless this packet is a RST (to avoid infinite loops) */
				goto free_pkt;
			}
		}

		vsk = vsock_sk(sk);
		trans = vsk->trans;
		BUG_ON(!trans);

		virtio_transport_space_update(sk, pkt);

		lock_sock(sk);
		switch (sk->sk_state) {
		case VSOCK_SS_LISTEN:
			virtio_transport_recv_listen(sk, pkt);
			virtio_transport_free_pkt(pkt);
			break;
		case SS_CONNECTING:
			virtio_transport_recv_connecting(sk, pkt);
			virtio_transport_free_pkt(pkt);
			break;
		case SS_CONNECTED:
			virtio_transport_recv_connected(sk, pkt);
			break;
		default:
			virtio_transport_free_pkt(pkt);
			break;
		}
		release_sock(sk);

		/* Release refcnt obtained when we fetched this socket out of the
		 * bound or connected list.
		 */
		sock_put(sk);
	}
	return;

free_pkt:
	virtio_transport_free_pkt(pkt);
}
EXPORT_SYMBOL_GPL(virtio_transport_recv_pkt);

void virtio_transport_free_pkt(struct virtio_vsock_pkt *pkt)
{
	kfree(pkt->buf);
	kfree(pkt);
}
EXPORT_SYMBOL_GPL(virtio_transport_free_pkt);

static int __init virtio_vsock_common_init(void)
{
	get_random_bytes(vsockcookie_secret, sizeof(vsockcookie_secret));
	return 0;
}

static void __exit virtio_vsock_common_exit(void)
{
}

module_init(virtio_vsock_common_init);
module_exit(virtio_vsock_common_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Asias He");
MODULE_DESCRIPTION("common code for virtio vsock");
