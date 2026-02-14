// SPDX-License-Identifier: GPL-2.0
/* XDP sockets
 *
 * AF_XDP sockets allows a channel between XDP programs and userspace
 * applications.
 * Copyright(c) 2018 Intel Corporation.
 *
 * Author(s): Björn Töpel <bjorn.topel@intel.com>
 *	      Magnus Karlsson <magnus.karlsson@intel.com>
 */

#define pr_fmt(fmt) "AF_XDP: %s: " fmt, __func__

#include <linux/if_xdp.h>
#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/vmalloc.h>
#include <net/xdp_sock_drv.h>
#include <net/busy_poll.h>
#include <net/netdev_lock.h>
#include <net/netdev_rx_queue.h>
#include <net/xdp.h>

#include "xsk_queue.h"
#include "xdp_umem.h"
#include "xsk.h"

#define TX_BATCH_SIZE 32
#define MAX_PER_SOCKET_BUDGET 32

struct xsk_addrs {
	u32 num_descs;
	u64 addrs[MAX_SKB_FRAGS + 1];
};

static struct kmem_cache *xsk_tx_generic_cache;

void xsk_set_rx_need_wakeup(struct xsk_buff_pool *pool)
{
	if (pool->cached_need_wakeup & XDP_WAKEUP_RX)
		return;

	pool->fq->ring->flags |= XDP_RING_NEED_WAKEUP;
	pool->cached_need_wakeup |= XDP_WAKEUP_RX;
}
EXPORT_SYMBOL(xsk_set_rx_need_wakeup);

void xsk_set_tx_need_wakeup(struct xsk_buff_pool *pool)
{
	struct xdp_sock *xs;

	if (pool->cached_need_wakeup & XDP_WAKEUP_TX)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(xs, &pool->xsk_tx_list, tx_list) {
		xs->tx->ring->flags |= XDP_RING_NEED_WAKEUP;
	}
	rcu_read_unlock();

	pool->cached_need_wakeup |= XDP_WAKEUP_TX;
}
EXPORT_SYMBOL(xsk_set_tx_need_wakeup);

void xsk_clear_rx_need_wakeup(struct xsk_buff_pool *pool)
{
	if (!(pool->cached_need_wakeup & XDP_WAKEUP_RX))
		return;

	pool->fq->ring->flags &= ~XDP_RING_NEED_WAKEUP;
	pool->cached_need_wakeup &= ~XDP_WAKEUP_RX;
}
EXPORT_SYMBOL(xsk_clear_rx_need_wakeup);

void xsk_clear_tx_need_wakeup(struct xsk_buff_pool *pool)
{
	struct xdp_sock *xs;

	if (!(pool->cached_need_wakeup & XDP_WAKEUP_TX))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(xs, &pool->xsk_tx_list, tx_list) {
		xs->tx->ring->flags &= ~XDP_RING_NEED_WAKEUP;
	}
	rcu_read_unlock();

	pool->cached_need_wakeup &= ~XDP_WAKEUP_TX;
}
EXPORT_SYMBOL(xsk_clear_tx_need_wakeup);

bool xsk_uses_need_wakeup(struct xsk_buff_pool *pool)
{
	return pool->uses_need_wakeup;
}
EXPORT_SYMBOL(xsk_uses_need_wakeup);

struct xsk_buff_pool *xsk_get_pool_from_qid(struct net_device *dev,
					    u16 queue_id)
{
	if (queue_id < dev->real_num_rx_queues)
		return dev->_rx[queue_id].pool;
	if (queue_id < dev->real_num_tx_queues)
		return dev->_tx[queue_id].pool;

	return NULL;
}
EXPORT_SYMBOL(xsk_get_pool_from_qid);

void xsk_clear_pool_at_qid(struct net_device *dev, u16 queue_id)
{
	if (queue_id < dev->num_rx_queues)
		dev->_rx[queue_id].pool = NULL;
	if (queue_id < dev->num_tx_queues)
		dev->_tx[queue_id].pool = NULL;
}

/* The buffer pool is stored both in the _rx struct and the _tx struct as we do
 * not know if the device has more tx queues than rx, or the opposite.
 * This might also change during run time.
 */
int xsk_reg_pool_at_qid(struct net_device *dev, struct xsk_buff_pool *pool,
			u16 queue_id)
{
	if (queue_id >= max_t(unsigned int,
			      dev->real_num_rx_queues,
			      dev->real_num_tx_queues))
		return -EINVAL;

	if (queue_id < dev->real_num_rx_queues)
		dev->_rx[queue_id].pool = pool;
	if (queue_id < dev->real_num_tx_queues)
		dev->_tx[queue_id].pool = pool;

	return 0;
}

static int __xsk_rcv_zc(struct xdp_sock *xs, struct xdp_buff_xsk *xskb, u32 len,
			u32 flags)
{
	u64 addr;
	int err;

	addr = xp_get_handle(xskb, xskb->pool);
	err = xskq_prod_reserve_desc(xs->rx, addr, len, flags);
	if (err) {
		xs->rx_queue_full++;
		return err;
	}

	xp_release(xskb);
	return 0;
}

static int xsk_rcv_zc(struct xdp_sock *xs, struct xdp_buff *xdp, u32 len)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);
	u32 frags = xdp_buff_has_frags(xdp);
	struct xdp_buff_xsk *pos, *tmp;
	struct list_head *xskb_list;
	u32 contd = 0;
	int err;

	if (frags)
		contd = XDP_PKT_CONTD;

	err = __xsk_rcv_zc(xs, xskb, len, contd);
	if (err)
		goto err;
	if (likely(!frags))
		return 0;

	xskb_list = &xskb->pool->xskb_list;
	list_for_each_entry_safe(pos, tmp, xskb_list, list_node) {
		if (list_is_singular(xskb_list))
			contd = 0;
		len = pos->xdp.data_end - pos->xdp.data;
		err = __xsk_rcv_zc(xs, pos, len, contd);
		if (err)
			goto err;
		list_del(&pos->list_node);
	}

	return 0;
err:
	xsk_buff_free(xdp);
	return err;
}

static void *xsk_copy_xdp_start(struct xdp_buff *from)
{
	if (unlikely(xdp_data_meta_unsupported(from)))
		return from->data;
	else
		return from->data_meta;
}

static u32 xsk_copy_xdp(void *to, void **from, u32 to_len,
			u32 *from_len, skb_frag_t **frag, u32 rem)
{
	u32 copied = 0;

	while (1) {
		u32 copy_len = min_t(u32, *from_len, to_len);

		memcpy(to, *from, copy_len);
		copied += copy_len;
		if (rem == copied)
			return copied;

		if (*from_len == copy_len) {
			*from = skb_frag_address(*frag);
			*from_len = skb_frag_size((*frag)++);
		} else {
			*from += copy_len;
			*from_len -= copy_len;
		}
		if (to_len == copy_len)
			return copied;

		to_len -= copy_len;
		to += copy_len;
	}
}

static int __xsk_rcv(struct xdp_sock *xs, struct xdp_buff *xdp, u32 len)
{
	u32 frame_size = xsk_pool_get_rx_frame_size(xs->pool);
	void *copy_from = xsk_copy_xdp_start(xdp), *copy_to;
	u32 from_len, meta_len, rem, num_desc;
	struct xdp_buff_xsk *xskb;
	struct xdp_buff *xsk_xdp;
	skb_frag_t *frag;

	from_len = xdp->data_end - copy_from;
	meta_len = xdp->data - copy_from;
	rem = len + meta_len;

	if (len <= frame_size && !xdp_buff_has_frags(xdp)) {
		int err;

		xsk_xdp = xsk_buff_alloc(xs->pool);
		if (!xsk_xdp) {
			xs->rx_dropped++;
			return -ENOMEM;
		}
		memcpy(xsk_xdp->data - meta_len, copy_from, rem);
		xskb = container_of(xsk_xdp, struct xdp_buff_xsk, xdp);
		err = __xsk_rcv_zc(xs, xskb, len, 0);
		if (err) {
			xsk_buff_free(xsk_xdp);
			return err;
		}

		return 0;
	}

	num_desc = (len - 1) / frame_size + 1;

	if (!xsk_buff_can_alloc(xs->pool, num_desc)) {
		xs->rx_dropped++;
		return -ENOMEM;
	}
	if (xskq_prod_nb_free(xs->rx, num_desc) < num_desc) {
		xs->rx_queue_full++;
		return -ENOBUFS;
	}

	if (xdp_buff_has_frags(xdp)) {
		struct skb_shared_info *sinfo;

		sinfo = xdp_get_shared_info_from_buff(xdp);
		frag =  &sinfo->frags[0];
	}

	do {
		u32 to_len = frame_size + meta_len;
		u32 copied;

		xsk_xdp = xsk_buff_alloc(xs->pool);
		copy_to = xsk_xdp->data - meta_len;

		copied = xsk_copy_xdp(copy_to, &copy_from, to_len, &from_len, &frag, rem);
		rem -= copied;

		xskb = container_of(xsk_xdp, struct xdp_buff_xsk, xdp);
		__xsk_rcv_zc(xs, xskb, copied - meta_len, rem ? XDP_PKT_CONTD : 0);
		meta_len = 0;
	} while (rem);

	return 0;
}

static bool xsk_tx_writeable(struct xdp_sock *xs)
{
	if (xskq_cons_present_entries(xs->tx) > xs->tx->nentries / 2)
		return false;

	return true;
}

static void __xsk_tx_release(struct xdp_sock *xs)
{
	__xskq_cons_release(xs->tx);
	if (xsk_tx_writeable(xs))
		xs->sk.sk_write_space(&xs->sk);
}

static bool xsk_is_bound(struct xdp_sock *xs)
{
	if (READ_ONCE(xs->state) == XSK_BOUND) {
		/* Matches smp_wmb() in bind(). */
		smp_rmb();
		return true;
	}
	return false;
}

static int xsk_rcv_check(struct xdp_sock *xs, struct xdp_buff *xdp, u32 len)
{
	if (!xsk_is_bound(xs))
		return -ENXIO;

	if (xs->dev != xdp->rxq->dev || xs->queue_id != xdp->rxq->queue_index)
		return -EINVAL;

	if (len > xsk_pool_get_rx_frame_size(xs->pool) && !xs->sg) {
		xs->rx_dropped++;
		return -ENOSPC;
	}

	return 0;
}

static void xsk_flush(struct xdp_sock *xs)
{
	xskq_prod_submit(xs->rx);
	__xskq_cons_release(xs->pool->fq);
	sock_def_readable(&xs->sk);
}

int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	u32 len = xdp_get_buff_len(xdp);
	int err;

	err = xsk_rcv_check(xs, xdp, len);
	if (!err) {
		spin_lock_bh(&xs->pool->rx_lock);
		err = __xsk_rcv(xs, xdp, len);
		xsk_flush(xs);
		spin_unlock_bh(&xs->pool->rx_lock);
	}

	return err;
}

static int xsk_rcv(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	u32 len = xdp_get_buff_len(xdp);
	int err;

	err = xsk_rcv_check(xs, xdp, len);
	if (err)
		return err;

	if (xdp->rxq->mem.type == MEM_TYPE_XSK_BUFF_POOL) {
		len = xdp->data_end - xdp->data;
		return xsk_rcv_zc(xs, xdp, len);
	}

	err = __xsk_rcv(xs, xdp, len);
	if (!err)
		xdp_return_buff(xdp);
	return err;
}

int __xsk_map_redirect(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	int err;

	err = xsk_rcv(xs, xdp);
	if (err)
		return err;

	if (!xs->flush_node.prev) {
		struct list_head *flush_list = bpf_net_ctx_get_xskmap_flush_list();

		list_add(&xs->flush_node, flush_list);
	}

	return 0;
}

void __xsk_map_flush(struct list_head *flush_list)
{
	struct xdp_sock *xs, *tmp;

	list_for_each_entry_safe(xs, tmp, flush_list, flush_node) {
		xsk_flush(xs);
		__list_del_clearprev(&xs->flush_node);
	}
}

void xsk_tx_completed(struct xsk_buff_pool *pool, u32 nb_entries)
{
	xskq_prod_submit_n(pool->cq, nb_entries);
}
EXPORT_SYMBOL(xsk_tx_completed);

void xsk_tx_release(struct xsk_buff_pool *pool)
{
	struct xdp_sock *xs;

	rcu_read_lock();
	list_for_each_entry_rcu(xs, &pool->xsk_tx_list, tx_list)
		__xsk_tx_release(xs);
	rcu_read_unlock();
}
EXPORT_SYMBOL(xsk_tx_release);

bool xsk_tx_peek_desc(struct xsk_buff_pool *pool, struct xdp_desc *desc)
{
	bool budget_exhausted = false;
	struct xdp_sock *xs;

	rcu_read_lock();
again:
	list_for_each_entry_rcu(xs, &pool->xsk_tx_list, tx_list) {
		if (xs->tx_budget_spent >= MAX_PER_SOCKET_BUDGET) {
			budget_exhausted = true;
			continue;
		}

		if (!xskq_cons_peek_desc(xs->tx, desc, pool)) {
			if (xskq_has_descs(xs->tx))
				xskq_cons_release(xs->tx);
			continue;
		}

		xs->tx_budget_spent++;

		/* This is the backpressure mechanism for the Tx path.
		 * Reserve space in the completion queue and only proceed
		 * if there is space in it. This avoids having to implement
		 * any buffering in the Tx path.
		 */
		if (xskq_prod_reserve_addr(pool->cq, desc->addr))
			goto out;

		xskq_cons_release(xs->tx);
		rcu_read_unlock();
		return true;
	}

	if (budget_exhausted) {
		list_for_each_entry_rcu(xs, &pool->xsk_tx_list, tx_list)
			xs->tx_budget_spent = 0;

		budget_exhausted = false;
		goto again;
	}

out:
	rcu_read_unlock();
	return false;
}
EXPORT_SYMBOL(xsk_tx_peek_desc);

static u32 xsk_tx_peek_release_fallback(struct xsk_buff_pool *pool, u32 max_entries)
{
	struct xdp_desc *descs = pool->tx_descs;
	u32 nb_pkts = 0;

	while (nb_pkts < max_entries && xsk_tx_peek_desc(pool, &descs[nb_pkts]))
		nb_pkts++;

	xsk_tx_release(pool);
	return nb_pkts;
}

u32 xsk_tx_peek_release_desc_batch(struct xsk_buff_pool *pool, u32 nb_pkts)
{
	struct xdp_sock *xs;

	rcu_read_lock();
	if (!list_is_singular(&pool->xsk_tx_list)) {
		/* Fallback to the non-batched version */
		rcu_read_unlock();
		return xsk_tx_peek_release_fallback(pool, nb_pkts);
	}

	xs = list_first_or_null_rcu(&pool->xsk_tx_list, struct xdp_sock, tx_list);
	if (!xs) {
		nb_pkts = 0;
		goto out;
	}

	nb_pkts = xskq_cons_nb_entries(xs->tx, nb_pkts);

	/* This is the backpressure mechanism for the Tx path. Try to
	 * reserve space in the completion queue for all packets, but
	 * if there are fewer slots available, just process that many
	 * packets. This avoids having to implement any buffering in
	 * the Tx path.
	 */
	nb_pkts = xskq_prod_nb_free(pool->cq, nb_pkts);
	if (!nb_pkts)
		goto out;

	nb_pkts = xskq_cons_read_desc_batch(xs->tx, pool, nb_pkts);
	if (!nb_pkts) {
		xs->tx->queue_empty_descs++;
		goto out;
	}

	__xskq_cons_release(xs->tx);
	xskq_prod_write_addr_batch(pool->cq, pool->tx_descs, nb_pkts);
	xs->sk.sk_write_space(&xs->sk);

out:
	rcu_read_unlock();
	return nb_pkts;
}
EXPORT_SYMBOL(xsk_tx_peek_release_desc_batch);

static int xsk_wakeup(struct xdp_sock *xs, u8 flags)
{
	struct net_device *dev = xs->dev;

	return dev->netdev_ops->ndo_xsk_wakeup(dev, xs->queue_id, flags);
}

static int xsk_cq_reserve_locked(struct xsk_buff_pool *pool)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pool->cq_lock, flags);
	ret = xskq_prod_reserve(pool->cq);
	spin_unlock_irqrestore(&pool->cq_lock, flags);

	return ret;
}

static bool xsk_skb_destructor_is_addr(struct sk_buff *skb)
{
	return (uintptr_t)skb_shinfo(skb)->destructor_arg & 0x1UL;
}

static u64 xsk_skb_destructor_get_addr(struct sk_buff *skb)
{
	return (u64)((uintptr_t)skb_shinfo(skb)->destructor_arg & ~0x1UL);
}

static void xsk_skb_destructor_set_addr(struct sk_buff *skb, u64 addr)
{
	skb_shinfo(skb)->destructor_arg = (void *)((uintptr_t)addr | 0x1UL);
}

static void xsk_inc_num_desc(struct sk_buff *skb)
{
	struct xsk_addrs *xsk_addr;

	if (!xsk_skb_destructor_is_addr(skb)) {
		xsk_addr = (struct xsk_addrs *)skb_shinfo(skb)->destructor_arg;
		xsk_addr->num_descs++;
	}
}

static u32 xsk_get_num_desc(struct sk_buff *skb)
{
	struct xsk_addrs *xsk_addr;

	if (xsk_skb_destructor_is_addr(skb))
		return 1;

	xsk_addr = (struct xsk_addrs *)skb_shinfo(skb)->destructor_arg;

	return xsk_addr->num_descs;
}

static void xsk_cq_submit_addr_locked(struct xsk_buff_pool *pool,
				      struct sk_buff *skb)
{
	u32 num_descs = xsk_get_num_desc(skb);
	struct xsk_addrs *xsk_addr;
	u32 descs_processed = 0;
	unsigned long flags;
	u32 idx, i;

	spin_lock_irqsave(&pool->cq_lock, flags);
	idx = xskq_get_prod(pool->cq);

	if (unlikely(num_descs > 1)) {
		xsk_addr = (struct xsk_addrs *)skb_shinfo(skb)->destructor_arg;

		for (i = 0; i < num_descs; i++) {
			xskq_prod_write_addr(pool->cq, idx + descs_processed,
					     xsk_addr->addrs[i]);
			descs_processed++;
		}
		kmem_cache_free(xsk_tx_generic_cache, xsk_addr);
	} else {
		xskq_prod_write_addr(pool->cq, idx,
				     xsk_skb_destructor_get_addr(skb));
		descs_processed++;
	}
	xskq_prod_submit_n(pool->cq, descs_processed);
	spin_unlock_irqrestore(&pool->cq_lock, flags);
}

static void xsk_cq_cancel_locked(struct xsk_buff_pool *pool, u32 n)
{
	unsigned long flags;

	spin_lock_irqsave(&pool->cq_lock, flags);
	xskq_prod_cancel_n(pool->cq, n);
	spin_unlock_irqrestore(&pool->cq_lock, flags);
}

static void xsk_destruct_skb(struct sk_buff *skb)
{
	struct xsk_tx_metadata_compl *compl = &skb_shinfo(skb)->xsk_meta;

	if (compl->tx_timestamp) {
		/* sw completion timestamp, not a real one */
		*compl->tx_timestamp = ktime_get_tai_fast_ns();
	}

	xsk_cq_submit_addr_locked(xdp_sk(skb->sk)->pool, skb);
	sock_wfree(skb);
}

static void xsk_skb_init_misc(struct sk_buff *skb, struct xdp_sock *xs,
			      u64 addr)
{
	skb->dev = xs->dev;
	skb->priority = READ_ONCE(xs->sk.sk_priority);
	skb->mark = READ_ONCE(xs->sk.sk_mark);
	skb->destructor = xsk_destruct_skb;
	xsk_skb_destructor_set_addr(skb, addr);
}

static void xsk_consume_skb(struct sk_buff *skb)
{
	struct xdp_sock *xs = xdp_sk(skb->sk);
	u32 num_descs = xsk_get_num_desc(skb);
	struct xsk_addrs *xsk_addr;

	if (unlikely(num_descs > 1)) {
		xsk_addr = (struct xsk_addrs *)skb_shinfo(skb)->destructor_arg;
		kmem_cache_free(xsk_tx_generic_cache, xsk_addr);
	}

	skb->destructor = sock_wfree;
	xsk_cq_cancel_locked(xs->pool, num_descs);
	/* Free skb without triggering the perf drop trace */
	consume_skb(skb);
	xs->skb = NULL;
}

static void xsk_drop_skb(struct sk_buff *skb)
{
	xdp_sk(skb->sk)->tx->invalid_descs += xsk_get_num_desc(skb);
	xsk_consume_skb(skb);
}

static int xsk_skb_metadata(struct sk_buff *skb, void *buffer,
			    struct xdp_desc *desc, struct xsk_buff_pool *pool,
			    u32 hr)
{
	struct xsk_tx_metadata *meta = NULL;

	if (unlikely(pool->tx_metadata_len == 0))
		return -EINVAL;

	meta = buffer - pool->tx_metadata_len;
	if (unlikely(!xsk_buff_valid_tx_metadata(meta)))
		return -EINVAL;

	if (meta->flags & XDP_TXMD_FLAGS_CHECKSUM) {
		if (unlikely(meta->request.csum_start +
			     meta->request.csum_offset +
			     sizeof(__sum16) > desc->len))
			return -EINVAL;

		skb->csum_start = hr + meta->request.csum_start;
		skb->csum_offset = meta->request.csum_offset;
		skb->ip_summed = CHECKSUM_PARTIAL;

		if (unlikely(pool->tx_sw_csum)) {
			int err;

			err = skb_checksum_help(skb);
			if (err)
				return err;
		}
	}

	if (meta->flags & XDP_TXMD_FLAGS_LAUNCH_TIME)
		skb->skb_mstamp_ns = meta->request.launch_time;
	xsk_tx_metadata_to_compl(meta, &skb_shinfo(skb)->xsk_meta);

	return 0;
}

static struct sk_buff *xsk_build_skb_zerocopy(struct xdp_sock *xs,
					      struct xdp_desc *desc)
{
	struct xsk_buff_pool *pool = xs->pool;
	u32 hr, len, ts, offset, copy, copied;
	struct sk_buff *skb = xs->skb;
	struct page *page;
	void *buffer;
	int err, i;
	u64 addr;

	addr = desc->addr;
	buffer = xsk_buff_raw_get_data(pool, addr);

	if (!skb) {
		hr = max(NET_SKB_PAD, L1_CACHE_ALIGN(xs->dev->needed_headroom));

		skb = sock_alloc_send_skb(&xs->sk, hr, 1, &err);
		if (unlikely(!skb))
			return ERR_PTR(err);

		skb_reserve(skb, hr);

		xsk_skb_init_misc(skb, xs, desc->addr);
		if (desc->options & XDP_TX_METADATA) {
			err = xsk_skb_metadata(skb, buffer, desc, pool, hr);
			if (unlikely(err))
				return ERR_PTR(err);
		}
	} else {
		struct xsk_addrs *xsk_addr;

		if (xsk_skb_destructor_is_addr(skb)) {
			xsk_addr = kmem_cache_zalloc(xsk_tx_generic_cache,
						     GFP_KERNEL);
			if (!xsk_addr)
				return ERR_PTR(-ENOMEM);

			xsk_addr->num_descs = 1;
			xsk_addr->addrs[0] = xsk_skb_destructor_get_addr(skb);
			skb_shinfo(skb)->destructor_arg = (void *)xsk_addr;
		} else {
			xsk_addr = (struct xsk_addrs *)skb_shinfo(skb)->destructor_arg;
		}

		/* in case of -EOVERFLOW that could happen below,
		 * xsk_consume_skb() will release this node as whole skb
		 * would be dropped, which implies freeing all list elements
		 */
		xsk_addr->addrs[xsk_addr->num_descs] = desc->addr;
	}

	len = desc->len;
	ts = pool->unaligned ? len : pool->chunk_size;

	offset = offset_in_page(buffer);
	addr = buffer - pool->addrs;

	for (copied = 0, i = skb_shinfo(skb)->nr_frags; copied < len; i++) {
		if (unlikely(i >= MAX_SKB_FRAGS))
			return ERR_PTR(-EOVERFLOW);

		page = pool->umem->pgs[addr >> PAGE_SHIFT];
		get_page(page);

		copy = min_t(u32, PAGE_SIZE - offset, len - copied);
		skb_fill_page_desc(skb, i, page, offset, copy);

		copied += copy;
		addr += copy;
		offset = 0;
	}

	skb->len += len;
	skb->data_len += len;
	skb->truesize += ts;

	refcount_add(ts, &xs->sk.sk_wmem_alloc);

	return skb;
}

static struct sk_buff *xsk_build_skb(struct xdp_sock *xs,
				     struct xdp_desc *desc)
{
	struct net_device *dev = xs->dev;
	struct sk_buff *skb = xs->skb;
	int err;

	if (dev->priv_flags & IFF_TX_SKB_NO_LINEAR) {
		skb = xsk_build_skb_zerocopy(xs, desc);
		if (IS_ERR(skb)) {
			err = PTR_ERR(skb);
			skb = NULL;
			goto free_err;
		}
	} else {
		u32 hr, tr, len;
		void *buffer;

		buffer = xsk_buff_raw_get_data(xs->pool, desc->addr);
		len = desc->len;

		if (!skb) {
			hr = max(NET_SKB_PAD, L1_CACHE_ALIGN(dev->needed_headroom));
			tr = dev->needed_tailroom;
			skb = sock_alloc_send_skb(&xs->sk, hr + len + tr, 1, &err);
			if (unlikely(!skb))
				goto free_err;

			skb_reserve(skb, hr);
			skb_put(skb, len);

			err = skb_store_bits(skb, 0, buffer, len);
			if (unlikely(err))
				goto free_err;

			xsk_skb_init_misc(skb, xs, desc->addr);
			if (desc->options & XDP_TX_METADATA) {
				err = xsk_skb_metadata(skb, buffer, desc,
						       xs->pool, hr);
				if (unlikely(err))
					goto free_err;
			}
		} else {
			int nr_frags = skb_shinfo(skb)->nr_frags;
			struct xsk_addrs *xsk_addr;
			struct page *page;
			u8 *vaddr;

			if (xsk_skb_destructor_is_addr(skb)) {
				xsk_addr = kmem_cache_zalloc(xsk_tx_generic_cache,
							     GFP_KERNEL);
				if (!xsk_addr) {
					err = -ENOMEM;
					goto free_err;
				}

				xsk_addr->num_descs = 1;
				xsk_addr->addrs[0] = xsk_skb_destructor_get_addr(skb);
				skb_shinfo(skb)->destructor_arg = (void *)xsk_addr;
			} else {
				xsk_addr = (struct xsk_addrs *)skb_shinfo(skb)->destructor_arg;
			}

			if (unlikely(nr_frags == (MAX_SKB_FRAGS - 1) && xp_mb_desc(desc))) {
				err = -EOVERFLOW;
				goto free_err;
			}

			page = alloc_page(xs->sk.sk_allocation);
			if (unlikely(!page)) {
				err = -EAGAIN;
				goto free_err;
			}

			vaddr = kmap_local_page(page);
			memcpy(vaddr, buffer, len);
			kunmap_local(vaddr);

			skb_add_rx_frag(skb, nr_frags, page, 0, len, PAGE_SIZE);
			refcount_add(PAGE_SIZE, &xs->sk.sk_wmem_alloc);

			xsk_addr->addrs[xsk_addr->num_descs] = desc->addr;
		}
	}

	xsk_inc_num_desc(skb);

	return skb;

free_err:
	if (skb && !skb_shinfo(skb)->nr_frags)
		kfree_skb(skb);

	if (err == -EOVERFLOW) {
		/* Drop the packet */
		xsk_inc_num_desc(xs->skb);
		xsk_drop_skb(xs->skb);
		xskq_cons_release(xs->tx);
	} else {
		/* Let application retry */
		xsk_cq_cancel_locked(xs->pool, 1);
	}

	return ERR_PTR(err);
}

static int __xsk_generic_xmit(struct sock *sk)
{
	struct xdp_sock *xs = xdp_sk(sk);
	bool sent_frame = false;
	struct xdp_desc desc;
	struct sk_buff *skb;
	u32 max_batch;
	int err = 0;

	mutex_lock(&xs->mutex);

	/* Since we dropped the RCU read lock, the socket state might have changed. */
	if (unlikely(!xsk_is_bound(xs))) {
		err = -ENXIO;
		goto out;
	}

	if (xs->queue_id >= xs->dev->real_num_tx_queues)
		goto out;

	max_batch = READ_ONCE(xs->max_tx_budget);
	while (xskq_cons_peek_desc(xs->tx, &desc, xs->pool)) {
		if (max_batch-- == 0) {
			err = -EAGAIN;
			goto out;
		}

		/* This is the backpressure mechanism for the Tx path.
		 * Reserve space in the completion queue and only proceed
		 * if there is space in it. This avoids having to implement
		 * any buffering in the Tx path.
		 */
		err = xsk_cq_reserve_locked(xs->pool);
		if (err) {
			err = -EAGAIN;
			goto out;
		}

		skb = xsk_build_skb(xs, &desc);
		if (IS_ERR(skb)) {
			err = PTR_ERR(skb);
			if (err != -EOVERFLOW)
				goto out;
			err = 0;
			continue;
		}

		xskq_cons_release(xs->tx);

		if (xp_mb_desc(&desc)) {
			xs->skb = skb;
			continue;
		}

		err = __dev_direct_xmit(skb, xs->queue_id);
		if  (err == NETDEV_TX_BUSY) {
			/* Tell user-space to retry the send */
			xskq_cons_cancel_n(xs->tx, xsk_get_num_desc(skb));
			xsk_consume_skb(skb);
			err = -EAGAIN;
			goto out;
		}

		/* Ignore NET_XMIT_CN as packet might have been sent */
		if (err == NET_XMIT_DROP) {
			/* SKB completed but not sent */
			err = -EBUSY;
			xs->skb = NULL;
			goto out;
		}

		sent_frame = true;
		xs->skb = NULL;
	}

	if (xskq_has_descs(xs->tx)) {
		if (xs->skb)
			xsk_drop_skb(xs->skb);
		xskq_cons_release(xs->tx);
	}

out:
	if (sent_frame)
		__xsk_tx_release(xs);

	mutex_unlock(&xs->mutex);
	return err;
}

static int xsk_generic_xmit(struct sock *sk)
{
	int ret;

	/* Drop the RCU lock since the SKB path might sleep. */
	rcu_read_unlock();
	ret = __xsk_generic_xmit(sk);
	/* Reaquire RCU lock before going into common code. */
	rcu_read_lock();

	return ret;
}

static bool xsk_no_wakeup(struct sock *sk)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	/* Prefer busy-polling, skip the wakeup. */
	return READ_ONCE(sk->sk_prefer_busy_poll) && READ_ONCE(sk->sk_ll_usec) &&
		napi_id_valid(READ_ONCE(sk->sk_napi_id));
#else
	return false;
#endif
}

static int xsk_check_common(struct xdp_sock *xs)
{
	if (unlikely(!xsk_is_bound(xs)))
		return -ENXIO;
	if (unlikely(!(xs->dev->flags & IFF_UP)))
		return -ENETDOWN;

	return 0;
}

static int __xsk_sendmsg(struct socket *sock, struct msghdr *m, size_t total_len)
{
	bool need_wait = !(m->msg_flags & MSG_DONTWAIT);
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	struct xsk_buff_pool *pool;
	int err;

	err = xsk_check_common(xs);
	if (err)
		return err;
	if (unlikely(need_wait))
		return -EOPNOTSUPP;
	if (unlikely(!xs->tx))
		return -ENOBUFS;

	if (sk_can_busy_loop(sk))
		sk_busy_loop(sk, 1); /* only support non-blocking sockets */

	if (xs->zc && xsk_no_wakeup(sk))
		return 0;

	pool = xs->pool;
	if (pool->cached_need_wakeup & XDP_WAKEUP_TX) {
		if (xs->zc)
			return xsk_wakeup(xs, XDP_WAKEUP_TX);
		return xsk_generic_xmit(sk);
	}
	return 0;
}

static int xsk_sendmsg(struct socket *sock, struct msghdr *m, size_t total_len)
{
	int ret;

	rcu_read_lock();
	ret = __xsk_sendmsg(sock, m, total_len);
	rcu_read_unlock();

	return ret;
}

static int __xsk_recvmsg(struct socket *sock, struct msghdr *m, size_t len, int flags)
{
	bool need_wait = !(flags & MSG_DONTWAIT);
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	int err;

	err = xsk_check_common(xs);
	if (err)
		return err;
	if (unlikely(!xs->rx))
		return -ENOBUFS;
	if (unlikely(need_wait))
		return -EOPNOTSUPP;

	if (sk_can_busy_loop(sk))
		sk_busy_loop(sk, 1); /* only support non-blocking sockets */

	if (xsk_no_wakeup(sk))
		return 0;

	if (xs->pool->cached_need_wakeup & XDP_WAKEUP_RX && xs->zc)
		return xsk_wakeup(xs, XDP_WAKEUP_RX);
	return 0;
}

static int xsk_recvmsg(struct socket *sock, struct msghdr *m, size_t len, int flags)
{
	int ret;

	rcu_read_lock();
	ret = __xsk_recvmsg(sock, m, len, flags);
	rcu_read_unlock();

	return ret;
}

static __poll_t xsk_poll(struct file *file, struct socket *sock,
			     struct poll_table_struct *wait)
{
	__poll_t mask = 0;
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	struct xsk_buff_pool *pool;

	sock_poll_wait(file, sock, wait);

	rcu_read_lock();
	if (xsk_check_common(xs))
		goto out;

	pool = xs->pool;

	if (pool->cached_need_wakeup) {
		if (xs->zc)
			xsk_wakeup(xs, pool->cached_need_wakeup);
		else if (xs->tx)
			/* Poll needs to drive Tx also in copy mode */
			xsk_generic_xmit(sk);
	}

	if (xs->rx && !xskq_prod_is_empty(xs->rx))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (xs->tx && xsk_tx_writeable(xs))
		mask |= EPOLLOUT | EPOLLWRNORM;
out:
	rcu_read_unlock();
	return mask;
}

static int xsk_init_queue(u32 entries, struct xsk_queue **queue,
			  bool umem_queue)
{
	struct xsk_queue *q;

	if (entries == 0 || *queue || !is_power_of_2(entries))
		return -EINVAL;

	q = xskq_create(entries, umem_queue);
	if (!q)
		return -ENOMEM;

	/* Make sure queue is ready before it can be seen by others */
	smp_wmb();
	WRITE_ONCE(*queue, q);
	return 0;
}

static void xsk_unbind_dev(struct xdp_sock *xs)
{
	struct net_device *dev = xs->dev;

	if (xs->state != XSK_BOUND)
		return;
	WRITE_ONCE(xs->state, XSK_UNBOUND);

	/* Wait for driver to stop using the xdp socket. */
	xp_del_xsk(xs->pool, xs);
	synchronize_net();
	dev_put(dev);
}

static struct xsk_map *xsk_get_map_list_entry(struct xdp_sock *xs,
					      struct xdp_sock __rcu ***map_entry)
{
	struct xsk_map *map = NULL;
	struct xsk_map_node *node;

	*map_entry = NULL;

	spin_lock_bh(&xs->map_list_lock);
	node = list_first_entry_or_null(&xs->map_list, struct xsk_map_node,
					node);
	if (node) {
		bpf_map_inc(&node->map->map);
		map = node->map;
		*map_entry = node->map_entry;
	}
	spin_unlock_bh(&xs->map_list_lock);
	return map;
}

static void xsk_delete_from_maps(struct xdp_sock *xs)
{
	/* This function removes the current XDP socket from all the
	 * maps it resides in. We need to take extra care here, due to
	 * the two locks involved. Each map has a lock synchronizing
	 * updates to the entries, and each socket has a lock that
	 * synchronizes access to the list of maps (map_list). For
	 * deadlock avoidance the locks need to be taken in the order
	 * "map lock"->"socket map list lock". We start off by
	 * accessing the socket map list, and take a reference to the
	 * map to guarantee existence between the
	 * xsk_get_map_list_entry() and xsk_map_try_sock_delete()
	 * calls. Then we ask the map to remove the socket, which
	 * tries to remove the socket from the map. Note that there
	 * might be updates to the map between
	 * xsk_get_map_list_entry() and xsk_map_try_sock_delete().
	 */
	struct xdp_sock __rcu **map_entry = NULL;
	struct xsk_map *map;

	while ((map = xsk_get_map_list_entry(xs, &map_entry))) {
		xsk_map_try_sock_delete(map, xs, map_entry);
		bpf_map_put(&map->map);
	}
}

static int xsk_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	struct net *net;

	if (!sk)
		return 0;

	net = sock_net(sk);

	if (xs->skb)
		xsk_drop_skb(xs->skb);

	mutex_lock(&net->xdp.lock);
	sk_del_node_init_rcu(sk);
	mutex_unlock(&net->xdp.lock);

	sock_prot_inuse_add(net, sk->sk_prot, -1);

	xsk_delete_from_maps(xs);
	mutex_lock(&xs->mutex);
	xsk_unbind_dev(xs);
	mutex_unlock(&xs->mutex);

	xskq_destroy(xs->rx);
	xskq_destroy(xs->tx);
	xskq_destroy(xs->fq_tmp);
	xskq_destroy(xs->cq_tmp);

	sock_orphan(sk);
	sock->sk = NULL;

	sock_put(sk);

	return 0;
}

static struct socket *xsk_lookup_xsk_from_fd(int fd)
{
	struct socket *sock;
	int err;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		return ERR_PTR(-ENOTSOCK);

	if (sock->sk->sk_family != PF_XDP) {
		sockfd_put(sock);
		return ERR_PTR(-ENOPROTOOPT);
	}

	return sock;
}

static bool xsk_validate_queues(struct xdp_sock *xs)
{
	return xs->fq_tmp && xs->cq_tmp;
}

static int xsk_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_xdp *sxdp = (struct sockaddr_xdp *)addr;
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	struct net_device *dev;
	int bound_dev_if;
	u32 flags, qid;
	int err = 0;

	if (addr_len < sizeof(struct sockaddr_xdp))
		return -EINVAL;
	if (sxdp->sxdp_family != AF_XDP)
		return -EINVAL;

	flags = sxdp->sxdp_flags;
	if (flags & ~(XDP_SHARED_UMEM | XDP_COPY | XDP_ZEROCOPY |
		      XDP_USE_NEED_WAKEUP | XDP_USE_SG))
		return -EINVAL;

	bound_dev_if = READ_ONCE(sk->sk_bound_dev_if);
	if (bound_dev_if && bound_dev_if != sxdp->sxdp_ifindex)
		return -EINVAL;

	rtnl_lock();
	mutex_lock(&xs->mutex);
	if (xs->state != XSK_READY) {
		err = -EBUSY;
		goto out_release;
	}

	dev = dev_get_by_index(sock_net(sk), sxdp->sxdp_ifindex);
	if (!dev) {
		err = -ENODEV;
		goto out_release;
	}

	netdev_lock_ops(dev);

	if (!xs->rx && !xs->tx) {
		err = -EINVAL;
		goto out_unlock;
	}

	qid = sxdp->sxdp_queue_id;

	if (flags & XDP_SHARED_UMEM) {
		struct xdp_sock *umem_xs;
		struct socket *sock;

		if ((flags & XDP_COPY) || (flags & XDP_ZEROCOPY) ||
		    (flags & XDP_USE_NEED_WAKEUP) || (flags & XDP_USE_SG)) {
			/* Cannot specify flags for shared sockets. */
			err = -EINVAL;
			goto out_unlock;
		}

		if (xs->umem) {
			/* We have already our own. */
			err = -EINVAL;
			goto out_unlock;
		}

		sock = xsk_lookup_xsk_from_fd(sxdp->sxdp_shared_umem_fd);
		if (IS_ERR(sock)) {
			err = PTR_ERR(sock);
			goto out_unlock;
		}

		umem_xs = xdp_sk(sock->sk);
		if (!xsk_is_bound(umem_xs)) {
			err = -EBADF;
			sockfd_put(sock);
			goto out_unlock;
		}

		if (umem_xs->queue_id != qid || umem_xs->dev != dev) {
			/* Share the umem with another socket on another qid
			 * and/or device.
			 */
			xs->pool = xp_create_and_assign_umem(xs,
							     umem_xs->umem);
			if (!xs->pool) {
				err = -ENOMEM;
				sockfd_put(sock);
				goto out_unlock;
			}

			err = xp_assign_dev_shared(xs->pool, umem_xs, dev,
						   qid);
			if (err) {
				xp_destroy(xs->pool);
				xs->pool = NULL;
				sockfd_put(sock);
				goto out_unlock;
			}
		} else {
			/* Share the buffer pool with the other socket. */
			if (xs->fq_tmp || xs->cq_tmp) {
				/* Do not allow setting your own fq or cq. */
				err = -EINVAL;
				sockfd_put(sock);
				goto out_unlock;
			}

			xp_get_pool(umem_xs->pool);
			xs->pool = umem_xs->pool;

			/* If underlying shared umem was created without Tx
			 * ring, allocate Tx descs array that Tx batching API
			 * utilizes
			 */
			if (xs->tx && !xs->pool->tx_descs) {
				err = xp_alloc_tx_descs(xs->pool, xs);
				if (err) {
					xp_put_pool(xs->pool);
					xs->pool = NULL;
					sockfd_put(sock);
					goto out_unlock;
				}
			}
		}

		xdp_get_umem(umem_xs->umem);
		WRITE_ONCE(xs->umem, umem_xs->umem);
		sockfd_put(sock);
	} else if (!xs->umem || !xsk_validate_queues(xs)) {
		err = -EINVAL;
		goto out_unlock;
	} else {
		/* This xsk has its own umem. */
		xs->pool = xp_create_and_assign_umem(xs, xs->umem);
		if (!xs->pool) {
			err = -ENOMEM;
			goto out_unlock;
		}

		err = xp_assign_dev(xs->pool, dev, qid, flags);
		if (err) {
			xp_destroy(xs->pool);
			xs->pool = NULL;
			goto out_unlock;
		}
	}

	/* FQ and CQ are now owned by the buffer pool and cleaned up with it. */
	xs->fq_tmp = NULL;
	xs->cq_tmp = NULL;

	xs->dev = dev;
	xs->zc = xs->umem->zc;
	xs->sg = !!(xs->umem->flags & XDP_UMEM_SG_FLAG);
	xs->queue_id = qid;
	xp_add_xsk(xs->pool, xs);

	if (qid < dev->real_num_rx_queues) {
		struct netdev_rx_queue *rxq;

		rxq = __netif_get_rx_queue(dev, qid);
		if (rxq->napi)
			__sk_mark_napi_id_once(sk, rxq->napi->napi_id);
	}

out_unlock:
	if (err) {
		dev_put(dev);
	} else {
		/* Matches smp_rmb() in bind() for shared umem
		 * sockets, and xsk_is_bound().
		 */
		smp_wmb();
		WRITE_ONCE(xs->state, XSK_BOUND);
	}
	netdev_unlock_ops(dev);
out_release:
	mutex_unlock(&xs->mutex);
	rtnl_unlock();
	return err;
}

struct xdp_umem_reg_v1 {
	__u64 addr; /* Start of packet data area */
	__u64 len; /* Length of packet data area */
	__u32 chunk_size;
	__u32 headroom;
};

static int xsk_setsockopt(struct socket *sock, int level, int optname,
			  sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	int err;

	if (level != SOL_XDP)
		return -ENOPROTOOPT;

	switch (optname) {
	case XDP_RX_RING:
	case XDP_TX_RING:
	{
		struct xsk_queue **q;
		int entries;

		if (optlen < sizeof(entries))
			return -EINVAL;
		if (copy_from_sockptr(&entries, optval, sizeof(entries)))
			return -EFAULT;

		mutex_lock(&xs->mutex);
		if (xs->state != XSK_READY) {
			mutex_unlock(&xs->mutex);
			return -EBUSY;
		}
		q = (optname == XDP_TX_RING) ? &xs->tx : &xs->rx;
		err = xsk_init_queue(entries, q, false);
		if (!err && optname == XDP_TX_RING)
			/* Tx needs to be explicitly woken up the first time */
			xs->tx->ring->flags |= XDP_RING_NEED_WAKEUP;
		mutex_unlock(&xs->mutex);
		return err;
	}
	case XDP_UMEM_REG:
	{
		size_t mr_size = sizeof(struct xdp_umem_reg);
		struct xdp_umem_reg mr = {};
		struct xdp_umem *umem;

		if (optlen < sizeof(struct xdp_umem_reg_v1))
			return -EINVAL;
		else if (optlen < sizeof(mr))
			mr_size = sizeof(struct xdp_umem_reg_v1);

		BUILD_BUG_ON(sizeof(struct xdp_umem_reg_v1) >= sizeof(struct xdp_umem_reg));

		/* Make sure the last field of the struct doesn't have
		 * uninitialized padding. All padding has to be explicit
		 * and has to be set to zero by the userspace to make
		 * struct xdp_umem_reg extensible in the future.
		 */
		BUILD_BUG_ON(offsetof(struct xdp_umem_reg, tx_metadata_len) +
			     sizeof_field(struct xdp_umem_reg, tx_metadata_len) !=
			     sizeof(struct xdp_umem_reg));

		if (copy_from_sockptr(&mr, optval, mr_size))
			return -EFAULT;

		mutex_lock(&xs->mutex);
		if (xs->state != XSK_READY || xs->umem) {
			mutex_unlock(&xs->mutex);
			return -EBUSY;
		}

		umem = xdp_umem_create(&mr);
		if (IS_ERR(umem)) {
			mutex_unlock(&xs->mutex);
			return PTR_ERR(umem);
		}

		/* Make sure umem is ready before it can be seen by others */
		smp_wmb();
		WRITE_ONCE(xs->umem, umem);
		mutex_unlock(&xs->mutex);
		return 0;
	}
	case XDP_UMEM_FILL_RING:
	case XDP_UMEM_COMPLETION_RING:
	{
		struct xsk_queue **q;
		int entries;

		if (optlen < sizeof(entries))
			return -EINVAL;
		if (copy_from_sockptr(&entries, optval, sizeof(entries)))
			return -EFAULT;

		mutex_lock(&xs->mutex);
		if (xs->state != XSK_READY) {
			mutex_unlock(&xs->mutex);
			return -EBUSY;
		}

		q = (optname == XDP_UMEM_FILL_RING) ? &xs->fq_tmp :
			&xs->cq_tmp;
		err = xsk_init_queue(entries, q, true);
		mutex_unlock(&xs->mutex);
		return err;
	}
	case XDP_MAX_TX_SKB_BUDGET:
	{
		unsigned int budget;

		if (optlen != sizeof(budget))
			return -EINVAL;
		if (copy_from_sockptr(&budget, optval, sizeof(budget)))
			return -EFAULT;
		if (!xs->tx ||
		    budget < TX_BATCH_SIZE || budget > xs->tx->nentries)
			return -EACCES;

		WRITE_ONCE(xs->max_tx_budget, budget);
		return 0;
	}
	default:
		break;
	}

	return -ENOPROTOOPT;
}

static void xsk_enter_rxtx_offsets(struct xdp_ring_offset_v1 *ring)
{
	ring->producer = offsetof(struct xdp_rxtx_ring, ptrs.producer);
	ring->consumer = offsetof(struct xdp_rxtx_ring, ptrs.consumer);
	ring->desc = offsetof(struct xdp_rxtx_ring, desc);
}

static void xsk_enter_umem_offsets(struct xdp_ring_offset_v1 *ring)
{
	ring->producer = offsetof(struct xdp_umem_ring, ptrs.producer);
	ring->consumer = offsetof(struct xdp_umem_ring, ptrs.consumer);
	ring->desc = offsetof(struct xdp_umem_ring, desc);
}

struct xdp_statistics_v1 {
	__u64 rx_dropped;
	__u64 rx_invalid_descs;
	__u64 tx_invalid_descs;
};

static int xsk_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct xdp_sock *xs = xdp_sk(sk);
	int len;

	if (level != SOL_XDP)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case XDP_STATISTICS:
	{
		struct xdp_statistics stats = {};
		bool extra_stats = true;
		size_t stats_size;

		if (len < sizeof(struct xdp_statistics_v1)) {
			return -EINVAL;
		} else if (len < sizeof(stats)) {
			extra_stats = false;
			stats_size = sizeof(struct xdp_statistics_v1);
		} else {
			stats_size = sizeof(stats);
		}

		mutex_lock(&xs->mutex);
		stats.rx_dropped = xs->rx_dropped;
		if (extra_stats) {
			stats.rx_ring_full = xs->rx_queue_full;
			stats.rx_fill_ring_empty_descs =
				xs->pool ? xskq_nb_queue_empty_descs(xs->pool->fq) : 0;
			stats.tx_ring_empty_descs = xskq_nb_queue_empty_descs(xs->tx);
		} else {
			stats.rx_dropped += xs->rx_queue_full;
		}
		stats.rx_invalid_descs = xskq_nb_invalid_descs(xs->rx);
		stats.tx_invalid_descs = xskq_nb_invalid_descs(xs->tx);
		mutex_unlock(&xs->mutex);

		if (copy_to_user(optval, &stats, stats_size))
			return -EFAULT;
		if (put_user(stats_size, optlen))
			return -EFAULT;

		return 0;
	}
	case XDP_MMAP_OFFSETS:
	{
		struct xdp_mmap_offsets off;
		struct xdp_mmap_offsets_v1 off_v1;
		bool flags_supported = true;
		void *to_copy;

		if (len < sizeof(off_v1))
			return -EINVAL;
		else if (len < sizeof(off))
			flags_supported = false;

		if (flags_supported) {
			/* xdp_ring_offset is identical to xdp_ring_offset_v1
			 * except for the flags field added to the end.
			 */
			xsk_enter_rxtx_offsets((struct xdp_ring_offset_v1 *)
					       &off.rx);
			xsk_enter_rxtx_offsets((struct xdp_ring_offset_v1 *)
					       &off.tx);
			xsk_enter_umem_offsets((struct xdp_ring_offset_v1 *)
					       &off.fr);
			xsk_enter_umem_offsets((struct xdp_ring_offset_v1 *)
					       &off.cr);
			off.rx.flags = offsetof(struct xdp_rxtx_ring,
						ptrs.flags);
			off.tx.flags = offsetof(struct xdp_rxtx_ring,
						ptrs.flags);
			off.fr.flags = offsetof(struct xdp_umem_ring,
						ptrs.flags);
			off.cr.flags = offsetof(struct xdp_umem_ring,
						ptrs.flags);

			len = sizeof(off);
			to_copy = &off;
		} else {
			xsk_enter_rxtx_offsets(&off_v1.rx);
			xsk_enter_rxtx_offsets(&off_v1.tx);
			xsk_enter_umem_offsets(&off_v1.fr);
			xsk_enter_umem_offsets(&off_v1.cr);

			len = sizeof(off_v1);
			to_copy = &off_v1;
		}

		if (copy_to_user(optval, to_copy, len))
			return -EFAULT;
		if (put_user(len, optlen))
			return -EFAULT;

		return 0;
	}
	case XDP_OPTIONS:
	{
		struct xdp_options opts = {};

		if (len < sizeof(opts))
			return -EINVAL;

		mutex_lock(&xs->mutex);
		if (xs->zc)
			opts.flags |= XDP_OPTIONS_ZEROCOPY;
		mutex_unlock(&xs->mutex);

		len = sizeof(opts);
		if (copy_to_user(optval, &opts, len))
			return -EFAULT;
		if (put_user(len, optlen))
			return -EFAULT;

		return 0;
	}
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int xsk_mmap(struct file *file, struct socket *sock,
		    struct vm_area_struct *vma)
{
	loff_t offset = (loff_t)vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct xdp_sock *xs = xdp_sk(sock->sk);
	int state = READ_ONCE(xs->state);
	struct xsk_queue *q = NULL;

	if (state != XSK_READY && state != XSK_BOUND)
		return -EBUSY;

	if (offset == XDP_PGOFF_RX_RING) {
		q = READ_ONCE(xs->rx);
	} else if (offset == XDP_PGOFF_TX_RING) {
		q = READ_ONCE(xs->tx);
	} else {
		/* Matches the smp_wmb() in XDP_UMEM_REG */
		smp_rmb();
		if (offset == XDP_UMEM_PGOFF_FILL_RING)
			q = state == XSK_READY ? READ_ONCE(xs->fq_tmp) :
						 READ_ONCE(xs->pool->fq);
		else if (offset == XDP_UMEM_PGOFF_COMPLETION_RING)
			q = state == XSK_READY ? READ_ONCE(xs->cq_tmp) :
						 READ_ONCE(xs->pool->cq);
	}

	if (!q)
		return -EINVAL;

	/* Matches the smp_wmb() in xsk_init_queue */
	smp_rmb();
	if (size > q->ring_vmalloc_size)
		return -EINVAL;

	return remap_vmalloc_range(vma, q->ring, 0);
}

static int xsk_notifier(struct notifier_block *this,
			unsigned long msg, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct sock *sk;

	switch (msg) {
	case NETDEV_UNREGISTER:
		mutex_lock(&net->xdp.lock);
		sk_for_each(sk, &net->xdp.list) {
			struct xdp_sock *xs = xdp_sk(sk);

			mutex_lock(&xs->mutex);
			if (xs->dev == dev) {
				sk->sk_err = ENETDOWN;
				if (!sock_flag(sk, SOCK_DEAD))
					sk_error_report(sk);

				xsk_unbind_dev(xs);

				/* Clear device references. */
				xp_clear_dev(xs->pool);
			}
			mutex_unlock(&xs->mutex);
		}
		mutex_unlock(&net->xdp.lock);
		break;
	}
	return NOTIFY_DONE;
}

static struct proto xsk_proto = {
	.name =		"XDP",
	.owner =	THIS_MODULE,
	.obj_size =	sizeof(struct xdp_sock),
};

static const struct proto_ops xsk_proto_ops = {
	.family		= PF_XDP,
	.owner		= THIS_MODULE,
	.release	= xsk_release,
	.bind		= xsk_bind,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= xsk_poll,
	.ioctl		= sock_no_ioctl,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= xsk_setsockopt,
	.getsockopt	= xsk_getsockopt,
	.sendmsg	= xsk_sendmsg,
	.recvmsg	= xsk_recvmsg,
	.mmap		= xsk_mmap,
};

static void xsk_destruct(struct sock *sk)
{
	struct xdp_sock *xs = xdp_sk(sk);

	if (!sock_flag(sk, SOCK_DEAD))
		return;

	if (!xp_put_pool(xs->pool))
		xdp_put_umem(xs->umem, !xs->pool);
}

static int xsk_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct xdp_sock *xs;
	struct sock *sk;

	if (!ns_capable(net->user_ns, CAP_NET_RAW))
		return -EPERM;
	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	if (protocol)
		return -EPROTONOSUPPORT;

	sock->state = SS_UNCONNECTED;

	sk = sk_alloc(net, PF_XDP, GFP_KERNEL, &xsk_proto, kern);
	if (!sk)
		return -ENOBUFS;

	sock->ops = &xsk_proto_ops;

	sock_init_data(sock, sk);

	sk->sk_family = PF_XDP;

	sk->sk_destruct = xsk_destruct;

	sock_set_flag(sk, SOCK_RCU_FREE);

	xs = xdp_sk(sk);
	xs->state = XSK_READY;
	xs->max_tx_budget = TX_BATCH_SIZE;
	mutex_init(&xs->mutex);

	INIT_LIST_HEAD(&xs->map_list);
	spin_lock_init(&xs->map_list_lock);

	mutex_lock(&net->xdp.lock);
	sk_add_node_rcu(sk, &net->xdp.list);
	mutex_unlock(&net->xdp.lock);

	sock_prot_inuse_add(net, &xsk_proto, 1);

	return 0;
}

static const struct net_proto_family xsk_family_ops = {
	.family = PF_XDP,
	.create = xsk_create,
	.owner	= THIS_MODULE,
};

static struct notifier_block xsk_netdev_notifier = {
	.notifier_call	= xsk_notifier,
};

static int __net_init xsk_net_init(struct net *net)
{
	mutex_init(&net->xdp.lock);
	INIT_HLIST_HEAD(&net->xdp.list);
	return 0;
}

static void __net_exit xsk_net_exit(struct net *net)
{
	WARN_ON_ONCE(!hlist_empty(&net->xdp.list));
}

static struct pernet_operations xsk_net_ops = {
	.init = xsk_net_init,
	.exit = xsk_net_exit,
};

static int __init xsk_init(void)
{
	int err;

	err = proto_register(&xsk_proto, 0 /* no slab */);
	if (err)
		goto out;

	err = sock_register(&xsk_family_ops);
	if (err)
		goto out_proto;

	err = register_pernet_subsys(&xsk_net_ops);
	if (err)
		goto out_sk;

	err = register_netdevice_notifier(&xsk_netdev_notifier);
	if (err)
		goto out_pernet;

	xsk_tx_generic_cache = kmem_cache_create("xsk_generic_xmit_cache",
						 sizeof(struct xsk_addrs),
						 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!xsk_tx_generic_cache) {
		err = -ENOMEM;
		goto out_unreg_notif;
	}

	return 0;

out_unreg_notif:
	unregister_netdevice_notifier(&xsk_netdev_notifier);
out_pernet:
	unregister_pernet_subsys(&xsk_net_ops);
out_sk:
	sock_unregister(PF_XDP);
out_proto:
	proto_unregister(&xsk_proto);
out:
	return err;
}

fs_initcall(xsk_init);
