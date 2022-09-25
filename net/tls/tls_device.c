/* Copyright (c) 2018, Mellanox Technologies All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <crypto/aead.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/dst.h>
#include <net/inet_connection_sock.h>
#include <net/tcp.h>
#include <net/tls.h>

#include "tls.h"
#include "trace.h"

/* device_offload_lock is used to synchronize tls_dev_add
 * against NETDEV_DOWN notifications.
 */
static DECLARE_RWSEM(device_offload_lock);

static struct workqueue_struct *destruct_wq __read_mostly;

static LIST_HEAD(tls_device_list);
static LIST_HEAD(tls_device_down_list);
static DEFINE_SPINLOCK(tls_device_lock);

static void tls_device_free_ctx(struct tls_context *ctx)
{
	if (ctx->tx_conf == TLS_HW) {
		kfree(tls_offload_ctx_tx(ctx));
		kfree(ctx->tx.rec_seq);
		kfree(ctx->tx.iv);
	}

	if (ctx->rx_conf == TLS_HW)
		kfree(tls_offload_ctx_rx(ctx));

	tls_ctx_free(NULL, ctx);
}

static void tls_device_tx_del_task(struct work_struct *work)
{
	struct tls_offload_context_tx *offload_ctx =
		container_of(work, struct tls_offload_context_tx, destruct_work);
	struct tls_context *ctx = offload_ctx->ctx;
	struct net_device *netdev;

	/* Safe, because this is the destroy flow, refcount is 0, so
	 * tls_device_down can't store this field in parallel.
	 */
	netdev = rcu_dereference_protected(ctx->netdev,
					   !refcount_read(&ctx->refcount));

	netdev->tlsdev_ops->tls_dev_del(netdev, ctx, TLS_OFFLOAD_CTX_DIR_TX);
	dev_put(netdev);
	ctx->netdev = NULL;
	tls_device_free_ctx(ctx);
}

static void tls_device_queue_ctx_destruction(struct tls_context *ctx)
{
	struct net_device *netdev;
	unsigned long flags;
	bool async_cleanup;

	spin_lock_irqsave(&tls_device_lock, flags);
	if (unlikely(!refcount_dec_and_test(&ctx->refcount))) {
		spin_unlock_irqrestore(&tls_device_lock, flags);
		return;
	}

	list_del(&ctx->list); /* Remove from tls_device_list / tls_device_down_list */

	/* Safe, because this is the destroy flow, refcount is 0, so
	 * tls_device_down can't store this field in parallel.
	 */
	netdev = rcu_dereference_protected(ctx->netdev,
					   !refcount_read(&ctx->refcount));

	async_cleanup = netdev && ctx->tx_conf == TLS_HW;
	if (async_cleanup) {
		struct tls_offload_context_tx *offload_ctx = tls_offload_ctx_tx(ctx);

		/* queue_work inside the spinlock
		 * to make sure tls_device_down waits for that work.
		 */
		queue_work(destruct_wq, &offload_ctx->destruct_work);
	}
	spin_unlock_irqrestore(&tls_device_lock, flags);

	if (!async_cleanup)
		tls_device_free_ctx(ctx);
}

/* We assume that the socket is already connected */
static struct net_device *get_netdev_for_sock(struct sock *sk)
{
	struct dst_entry *dst = sk_dst_get(sk);
	struct net_device *netdev = NULL;

	if (likely(dst)) {
		netdev = netdev_sk_get_lowest_dev(dst->dev, sk);
		dev_hold(netdev);
	}

	dst_release(dst);

	return netdev;
}

static void destroy_record(struct tls_record_info *record)
{
	int i;

	for (i = 0; i < record->num_frags; i++)
		__skb_frag_unref(&record->frags[i], false);
	kfree(record);
}

static void delete_all_records(struct tls_offload_context_tx *offload_ctx)
{
	struct tls_record_info *info, *temp;

	list_for_each_entry_safe(info, temp, &offload_ctx->records_list, list) {
		list_del(&info->list);
		destroy_record(info);
	}

	offload_ctx->retransmit_hint = NULL;
}

static void tls_icsk_clean_acked(struct sock *sk, u32 acked_seq)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_record_info *info, *temp;
	struct tls_offload_context_tx *ctx;
	u64 deleted_records = 0;
	unsigned long flags;

	if (!tls_ctx)
		return;

	ctx = tls_offload_ctx_tx(tls_ctx);

	spin_lock_irqsave(&ctx->lock, flags);
	info = ctx->retransmit_hint;
	if (info && !before(acked_seq, info->end_seq))
		ctx->retransmit_hint = NULL;

	list_for_each_entry_safe(info, temp, &ctx->records_list, list) {
		if (before(acked_seq, info->end_seq))
			break;
		list_del(&info->list);

		destroy_record(info);
		deleted_records++;
	}

	ctx->unacked_record_sn += deleted_records;
	spin_unlock_irqrestore(&ctx->lock, flags);
}

/* At this point, there should be no references on this
 * socket and no in-flight SKBs associated with this
 * socket, so it is safe to free all the resources.
 */
void tls_device_sk_destruct(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context_tx *ctx = tls_offload_ctx_tx(tls_ctx);

	tls_ctx->sk_destruct(sk);

	if (tls_ctx->tx_conf == TLS_HW) {
		if (ctx->open_record)
			destroy_record(ctx->open_record);
		delete_all_records(ctx);
		crypto_free_aead(ctx->aead_send);
		clean_acked_data_disable(inet_csk(sk));
	}

	tls_device_queue_ctx_destruction(tls_ctx);
}
EXPORT_SYMBOL_GPL(tls_device_sk_destruct);

void tls_device_free_resources_tx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);

	tls_free_partial_record(sk, tls_ctx);
}

void tls_offload_tx_resync_request(struct sock *sk, u32 got_seq, u32 exp_seq)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);

	trace_tls_device_tx_resync_req(sk, got_seq, exp_seq);
	WARN_ON(test_and_set_bit(TLS_TX_SYNC_SCHED, &tls_ctx->flags));
}
EXPORT_SYMBOL_GPL(tls_offload_tx_resync_request);

static void tls_device_resync_tx(struct sock *sk, struct tls_context *tls_ctx,
				 u32 seq)
{
	struct net_device *netdev;
	struct sk_buff *skb;
	int err = 0;
	u8 *rcd_sn;

	skb = tcp_write_queue_tail(sk);
	if (skb)
		TCP_SKB_CB(skb)->eor = 1;

	rcd_sn = tls_ctx->tx.rec_seq;

	trace_tls_device_tx_resync_send(sk, seq, rcd_sn);
	down_read(&device_offload_lock);
	netdev = rcu_dereference_protected(tls_ctx->netdev,
					   lockdep_is_held(&device_offload_lock));
	if (netdev)
		err = netdev->tlsdev_ops->tls_dev_resync(netdev, sk, seq,
							 rcd_sn,
							 TLS_OFFLOAD_CTX_DIR_TX);
	up_read(&device_offload_lock);
	if (err)
		return;

	clear_bit_unlock(TLS_TX_SYNC_SCHED, &tls_ctx->flags);
}

static void tls_append_frag(struct tls_record_info *record,
			    struct page_frag *pfrag,
			    int size)
{
	skb_frag_t *frag;

	frag = &record->frags[record->num_frags - 1];
	if (skb_frag_page(frag) == pfrag->page &&
	    skb_frag_off(frag) + skb_frag_size(frag) == pfrag->offset) {
		skb_frag_size_add(frag, size);
	} else {
		++frag;
		__skb_frag_set_page(frag, pfrag->page);
		skb_frag_off_set(frag, pfrag->offset);
		skb_frag_size_set(frag, size);
		++record->num_frags;
		get_page(pfrag->page);
	}

	pfrag->offset += size;
	record->len += size;
}

static int tls_push_record(struct sock *sk,
			   struct tls_context *ctx,
			   struct tls_offload_context_tx *offload_ctx,
			   struct tls_record_info *record,
			   int flags)
{
	struct tls_prot_info *prot = &ctx->prot_info;
	struct tcp_sock *tp = tcp_sk(sk);
	skb_frag_t *frag;
	int i;

	record->end_seq = tp->write_seq + record->len;
	list_add_tail_rcu(&record->list, &offload_ctx->records_list);
	offload_ctx->open_record = NULL;

	if (test_bit(TLS_TX_SYNC_SCHED, &ctx->flags))
		tls_device_resync_tx(sk, ctx, tp->write_seq);

	tls_advance_record_sn(sk, prot, &ctx->tx);

	for (i = 0; i < record->num_frags; i++) {
		frag = &record->frags[i];
		sg_unmark_end(&offload_ctx->sg_tx_data[i]);
		sg_set_page(&offload_ctx->sg_tx_data[i], skb_frag_page(frag),
			    skb_frag_size(frag), skb_frag_off(frag));
		sk_mem_charge(sk, skb_frag_size(frag));
		get_page(skb_frag_page(frag));
	}
	sg_mark_end(&offload_ctx->sg_tx_data[record->num_frags - 1]);

	/* all ready, send */
	return tls_push_sg(sk, ctx, offload_ctx->sg_tx_data, 0, flags);
}

static int tls_device_record_close(struct sock *sk,
				   struct tls_context *ctx,
				   struct tls_record_info *record,
				   struct page_frag *pfrag,
				   unsigned char record_type)
{
	struct tls_prot_info *prot = &ctx->prot_info;
	int ret;

	/* append tag
	 * device will fill in the tag, we just need to append a placeholder
	 * use socket memory to improve coalescing (re-using a single buffer
	 * increases frag count)
	 * if we can't allocate memory now, steal some back from data
	 */
	if (likely(skb_page_frag_refill(prot->tag_size, pfrag,
					sk->sk_allocation))) {
		ret = 0;
		tls_append_frag(record, pfrag, prot->tag_size);
	} else {
		ret = prot->tag_size;
		if (record->len <= prot->overhead_size)
			return -ENOMEM;
	}

	/* fill prepend */
	tls_fill_prepend(ctx, skb_frag_address(&record->frags[0]),
			 record->len - prot->overhead_size,
			 record_type);
	return ret;
}

static int tls_create_new_record(struct tls_offload_context_tx *offload_ctx,
				 struct page_frag *pfrag,
				 size_t prepend_size)
{
	struct tls_record_info *record;
	skb_frag_t *frag;

	record = kmalloc(sizeof(*record), GFP_KERNEL);
	if (!record)
		return -ENOMEM;

	frag = &record->frags[0];
	__skb_frag_set_page(frag, pfrag->page);
	skb_frag_off_set(frag, pfrag->offset);
	skb_frag_size_set(frag, prepend_size);

	get_page(pfrag->page);
	pfrag->offset += prepend_size;

	record->num_frags = 1;
	record->len = prepend_size;
	offload_ctx->open_record = record;
	return 0;
}

static int tls_do_allocation(struct sock *sk,
			     struct tls_offload_context_tx *offload_ctx,
			     struct page_frag *pfrag,
			     size_t prepend_size)
{
	int ret;

	if (!offload_ctx->open_record) {
		if (unlikely(!skb_page_frag_refill(prepend_size, pfrag,
						   sk->sk_allocation))) {
			READ_ONCE(sk->sk_prot)->enter_memory_pressure(sk);
			sk_stream_moderate_sndbuf(sk);
			return -ENOMEM;
		}

		ret = tls_create_new_record(offload_ctx, pfrag, prepend_size);
		if (ret)
			return ret;

		if (pfrag->size > pfrag->offset)
			return 0;
	}

	if (!sk_page_frag_refill(sk, pfrag))
		return -ENOMEM;

	return 0;
}

static int tls_device_copy_data(void *addr, size_t bytes, struct iov_iter *i)
{
	size_t pre_copy, nocache;

	pre_copy = ~((unsigned long)addr - 1) & (SMP_CACHE_BYTES - 1);
	if (pre_copy) {
		pre_copy = min(pre_copy, bytes);
		if (copy_from_iter(addr, pre_copy, i) != pre_copy)
			return -EFAULT;
		bytes -= pre_copy;
		addr += pre_copy;
	}

	nocache = round_down(bytes, SMP_CACHE_BYTES);
	if (copy_from_iter_nocache(addr, nocache, i) != nocache)
		return -EFAULT;
	bytes -= nocache;
	addr += nocache;

	if (bytes && copy_from_iter(addr, bytes, i) != bytes)
		return -EFAULT;

	return 0;
}

union tls_iter_offset {
	struct iov_iter *msg_iter;
	int offset;
};

static int tls_push_data(struct sock *sk,
			 union tls_iter_offset iter_offset,
			 size_t size, int flags,
			 unsigned char record_type,
			 struct page *zc_page)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_offload_context_tx *ctx = tls_offload_ctx_tx(tls_ctx);
	struct tls_record_info *record;
	int tls_push_record_flags;
	struct page_frag *pfrag;
	size_t orig_size = size;
	u32 max_open_record_len;
	bool more = false;
	bool done = false;
	int copy, rc = 0;
	long timeo;

	if (flags &
	    ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL | MSG_SENDPAGE_NOTLAST))
		return -EOPNOTSUPP;

	if (unlikely(sk->sk_err))
		return -sk->sk_err;

	flags |= MSG_SENDPAGE_DECRYPTED;
	tls_push_record_flags = flags | MSG_SENDPAGE_NOTLAST;

	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	if (tls_is_partially_sent_record(tls_ctx)) {
		rc = tls_push_partial_record(sk, tls_ctx, flags);
		if (rc < 0)
			return rc;
	}

	pfrag = sk_page_frag(sk);

	/* TLS_HEADER_SIZE is not counted as part of the TLS record, and
	 * we need to leave room for an authentication tag.
	 */
	max_open_record_len = TLS_MAX_PAYLOAD_SIZE +
			      prot->prepend_size;
	do {
		rc = tls_do_allocation(sk, ctx, pfrag, prot->prepend_size);
		if (unlikely(rc)) {
			rc = sk_stream_wait_memory(sk, &timeo);
			if (!rc)
				continue;

			record = ctx->open_record;
			if (!record)
				break;
handle_error:
			if (record_type != TLS_RECORD_TYPE_DATA) {
				/* avoid sending partial
				 * record with type !=
				 * application_data
				 */
				size = orig_size;
				destroy_record(record);
				ctx->open_record = NULL;
			} else if (record->len > prot->prepend_size) {
				goto last_record;
			}

			break;
		}

		record = ctx->open_record;

		copy = min_t(size_t, size, max_open_record_len - record->len);
		if (copy && zc_page) {
			struct page_frag zc_pfrag;

			zc_pfrag.page = zc_page;
			zc_pfrag.offset = iter_offset.offset;
			zc_pfrag.size = copy;
			tls_append_frag(record, &zc_pfrag, copy);
		} else if (copy) {
			copy = min_t(size_t, copy, pfrag->size - pfrag->offset);

			rc = tls_device_copy_data(page_address(pfrag->page) +
						  pfrag->offset, copy,
						  iter_offset.msg_iter);
			if (rc)
				goto handle_error;
			tls_append_frag(record, pfrag, copy);
		}

		size -= copy;
		if (!size) {
last_record:
			tls_push_record_flags = flags;
			if (flags & (MSG_SENDPAGE_NOTLAST | MSG_MORE)) {
				more = true;
				break;
			}

			done = true;
		}

		if (done || record->len >= max_open_record_len ||
		    (record->num_frags >= MAX_SKB_FRAGS - 1)) {
			rc = tls_device_record_close(sk, tls_ctx, record,
						     pfrag, record_type);
			if (rc) {
				if (rc > 0) {
					size += rc;
				} else {
					size = orig_size;
					destroy_record(record);
					ctx->open_record = NULL;
					break;
				}
			}

			rc = tls_push_record(sk,
					     tls_ctx,
					     ctx,
					     record,
					     tls_push_record_flags);
			if (rc < 0)
				break;
		}
	} while (!done);

	tls_ctx->pending_open_record_frags = more;

	if (orig_size - size > 0)
		rc = orig_size - size;

	return rc;
}

int tls_device_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	unsigned char record_type = TLS_RECORD_TYPE_DATA;
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	union tls_iter_offset iter;
	int rc;

	mutex_lock(&tls_ctx->tx_lock);
	lock_sock(sk);

	if (unlikely(msg->msg_controllen)) {
		rc = tls_process_cmsg(sk, msg, &record_type);
		if (rc)
			goto out;
	}

	iter.msg_iter = &msg->msg_iter;
	rc = tls_push_data(sk, iter, size, msg->msg_flags, record_type, NULL);

out:
	release_sock(sk);
	mutex_unlock(&tls_ctx->tx_lock);
	return rc;
}

int tls_device_sendpage(struct sock *sk, struct page *page,
			int offset, size_t size, int flags)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	union tls_iter_offset iter_offset;
	struct iov_iter msg_iter;
	char *kaddr;
	struct kvec iov;
	int rc;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	mutex_lock(&tls_ctx->tx_lock);
	lock_sock(sk);

	if (flags & MSG_OOB) {
		rc = -EOPNOTSUPP;
		goto out;
	}

	if (tls_ctx->zerocopy_sendfile) {
		iter_offset.offset = offset;
		rc = tls_push_data(sk, iter_offset, size,
				   flags, TLS_RECORD_TYPE_DATA, page);
		goto out;
	}

	kaddr = kmap(page);
	iov.iov_base = kaddr + offset;
	iov.iov_len = size;
	iov_iter_kvec(&msg_iter, WRITE, &iov, 1, size);
	iter_offset.msg_iter = &msg_iter;
	rc = tls_push_data(sk, iter_offset, size, flags, TLS_RECORD_TYPE_DATA,
			   NULL);
	kunmap(page);

out:
	release_sock(sk);
	mutex_unlock(&tls_ctx->tx_lock);
	return rc;
}

struct tls_record_info *tls_get_record(struct tls_offload_context_tx *context,
				       u32 seq, u64 *p_record_sn)
{
	u64 record_sn = context->hint_record_sn;
	struct tls_record_info *info, *last;

	info = context->retransmit_hint;
	if (!info ||
	    before(seq, info->end_seq - info->len)) {
		/* if retransmit_hint is irrelevant start
		 * from the beginning of the list
		 */
		info = list_first_entry_or_null(&context->records_list,
						struct tls_record_info, list);
		if (!info)
			return NULL;
		/* send the start_marker record if seq number is before the
		 * tls offload start marker sequence number. This record is
		 * required to handle TCP packets which are before TLS offload
		 * started.
		 *  And if it's not start marker, look if this seq number
		 * belongs to the list.
		 */
		if (likely(!tls_record_is_start_marker(info))) {
			/* we have the first record, get the last record to see
			 * if this seq number belongs to the list.
			 */
			last = list_last_entry(&context->records_list,
					       struct tls_record_info, list);

			if (!between(seq, tls_record_start_seq(info),
				     last->end_seq))
				return NULL;
		}
		record_sn = context->unacked_record_sn;
	}

	/* We just need the _rcu for the READ_ONCE() */
	rcu_read_lock();
	list_for_each_entry_from_rcu(info, &context->records_list, list) {
		if (before(seq, info->end_seq)) {
			if (!context->retransmit_hint ||
			    after(info->end_seq,
				  context->retransmit_hint->end_seq)) {
				context->hint_record_sn = record_sn;
				context->retransmit_hint = info;
			}
			*p_record_sn = record_sn;
			goto exit_rcu_unlock;
		}
		record_sn++;
	}
	info = NULL;

exit_rcu_unlock:
	rcu_read_unlock();
	return info;
}
EXPORT_SYMBOL(tls_get_record);

static int tls_device_push_pending_record(struct sock *sk, int flags)
{
	union tls_iter_offset iter;
	struct iov_iter msg_iter;

	iov_iter_kvec(&msg_iter, WRITE, NULL, 0, 0);
	iter.msg_iter = &msg_iter;
	return tls_push_data(sk, iter, 0, flags, TLS_RECORD_TYPE_DATA, NULL);
}

void tls_device_write_space(struct sock *sk, struct tls_context *ctx)
{
	if (tls_is_partially_sent_record(ctx)) {
		gfp_t sk_allocation = sk->sk_allocation;

		WARN_ON_ONCE(sk->sk_write_pending);

		sk->sk_allocation = GFP_ATOMIC;
		tls_push_partial_record(sk, ctx,
					MSG_DONTWAIT | MSG_NOSIGNAL |
					MSG_SENDPAGE_DECRYPTED);
		sk->sk_allocation = sk_allocation;
	}
}

static void tls_device_resync_rx(struct tls_context *tls_ctx,
				 struct sock *sk, u32 seq, u8 *rcd_sn)
{
	struct tls_offload_context_rx *rx_ctx = tls_offload_ctx_rx(tls_ctx);
	struct net_device *netdev;

	trace_tls_device_rx_resync_send(sk, seq, rcd_sn, rx_ctx->resync_type);
	rcu_read_lock();
	netdev = rcu_dereference(tls_ctx->netdev);
	if (netdev)
		netdev->tlsdev_ops->tls_dev_resync(netdev, sk, seq, rcd_sn,
						   TLS_OFFLOAD_CTX_DIR_RX);
	rcu_read_unlock();
	TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSRXDEVICERESYNC);
}

static bool
tls_device_rx_resync_async(struct tls_offload_resync_async *resync_async,
			   s64 resync_req, u32 *seq, u16 *rcd_delta)
{
	u32 is_async = resync_req & RESYNC_REQ_ASYNC;
	u32 req_seq = resync_req >> 32;
	u32 req_end = req_seq + ((resync_req >> 16) & 0xffff);
	u16 i;

	*rcd_delta = 0;

	if (is_async) {
		/* shouldn't get to wraparound:
		 * too long in async stage, something bad happened
		 */
		if (WARN_ON_ONCE(resync_async->rcd_delta == USHRT_MAX))
			return false;

		/* asynchronous stage: log all headers seq such that
		 * req_seq <= seq <= end_seq, and wait for real resync request
		 */
		if (before(*seq, req_seq))
			return false;
		if (!after(*seq, req_end) &&
		    resync_async->loglen < TLS_DEVICE_RESYNC_ASYNC_LOGMAX)
			resync_async->log[resync_async->loglen++] = *seq;

		resync_async->rcd_delta++;

		return false;
	}

	/* synchronous stage: check against the logged entries and
	 * proceed to check the next entries if no match was found
	 */
	for (i = 0; i < resync_async->loglen; i++)
		if (req_seq == resync_async->log[i] &&
		    atomic64_try_cmpxchg(&resync_async->req, &resync_req, 0)) {
			*rcd_delta = resync_async->rcd_delta - i;
			*seq = req_seq;
			resync_async->loglen = 0;
			resync_async->rcd_delta = 0;
			return true;
		}

	resync_async->loglen = 0;
	resync_async->rcd_delta = 0;

	if (req_seq == *seq &&
	    atomic64_try_cmpxchg(&resync_async->req,
				 &resync_req, 0))
		return true;

	return false;
}

void tls_device_rx_resync_new_rec(struct sock *sk, u32 rcd_len, u32 seq)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context_rx *rx_ctx;
	u8 rcd_sn[TLS_MAX_REC_SEQ_SIZE];
	u32 sock_data, is_req_pending;
	struct tls_prot_info *prot;
	s64 resync_req;
	u16 rcd_delta;
	u32 req_seq;

	if (tls_ctx->rx_conf != TLS_HW)
		return;
	if (unlikely(test_bit(TLS_RX_DEV_DEGRADED, &tls_ctx->flags)))
		return;

	prot = &tls_ctx->prot_info;
	rx_ctx = tls_offload_ctx_rx(tls_ctx);
	memcpy(rcd_sn, tls_ctx->rx.rec_seq, prot->rec_seq_size);

	switch (rx_ctx->resync_type) {
	case TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ:
		resync_req = atomic64_read(&rx_ctx->resync_req);
		req_seq = resync_req >> 32;
		seq += TLS_HEADER_SIZE - 1;
		is_req_pending = resync_req;

		if (likely(!is_req_pending) || req_seq != seq ||
		    !atomic64_try_cmpxchg(&rx_ctx->resync_req, &resync_req, 0))
			return;
		break;
	case TLS_OFFLOAD_SYNC_TYPE_CORE_NEXT_HINT:
		if (likely(!rx_ctx->resync_nh_do_now))
			return;

		/* head of next rec is already in, note that the sock_inq will
		 * include the currently parsed message when called from parser
		 */
		sock_data = tcp_inq(sk);
		if (sock_data > rcd_len) {
			trace_tls_device_rx_resync_nh_delay(sk, sock_data,
							    rcd_len);
			return;
		}

		rx_ctx->resync_nh_do_now = 0;
		seq += rcd_len;
		tls_bigint_increment(rcd_sn, prot->rec_seq_size);
		break;
	case TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ_ASYNC:
		resync_req = atomic64_read(&rx_ctx->resync_async->req);
		is_req_pending = resync_req;
		if (likely(!is_req_pending))
			return;

		if (!tls_device_rx_resync_async(rx_ctx->resync_async,
						resync_req, &seq, &rcd_delta))
			return;
		tls_bigint_subtract(rcd_sn, rcd_delta);
		break;
	}

	tls_device_resync_rx(tls_ctx, sk, seq, rcd_sn);
}

static void tls_device_core_ctrl_rx_resync(struct tls_context *tls_ctx,
					   struct tls_offload_context_rx *ctx,
					   struct sock *sk, struct sk_buff *skb)
{
	struct strp_msg *rxm;

	/* device will request resyncs by itself based on stream scan */
	if (ctx->resync_type != TLS_OFFLOAD_SYNC_TYPE_CORE_NEXT_HINT)
		return;
	/* already scheduled */
	if (ctx->resync_nh_do_now)
		return;
	/* seen decrypted fragments since last fully-failed record */
	if (ctx->resync_nh_reset) {
		ctx->resync_nh_reset = 0;
		ctx->resync_nh.decrypted_failed = 1;
		ctx->resync_nh.decrypted_tgt = TLS_DEVICE_RESYNC_NH_START_IVAL;
		return;
	}

	if (++ctx->resync_nh.decrypted_failed <= ctx->resync_nh.decrypted_tgt)
		return;

	/* doing resync, bump the next target in case it fails */
	if (ctx->resync_nh.decrypted_tgt < TLS_DEVICE_RESYNC_NH_MAX_IVAL)
		ctx->resync_nh.decrypted_tgt *= 2;
	else
		ctx->resync_nh.decrypted_tgt += TLS_DEVICE_RESYNC_NH_MAX_IVAL;

	rxm = strp_msg(skb);

	/* head of next rec is already in, parser will sync for us */
	if (tcp_inq(sk) > rxm->full_len) {
		trace_tls_device_rx_resync_nh_schedule(sk);
		ctx->resync_nh_do_now = 1;
	} else {
		struct tls_prot_info *prot = &tls_ctx->prot_info;
		u8 rcd_sn[TLS_MAX_REC_SEQ_SIZE];

		memcpy(rcd_sn, tls_ctx->rx.rec_seq, prot->rec_seq_size);
		tls_bigint_increment(rcd_sn, prot->rec_seq_size);

		tls_device_resync_rx(tls_ctx, sk, tcp_sk(sk)->copied_seq,
				     rcd_sn);
	}
}

static int
tls_device_reencrypt(struct sock *sk, struct tls_sw_context_rx *sw_ctx)
{
	int err, offset, copy, data_len, pos;
	struct sk_buff *skb, *skb_iter;
	struct scatterlist sg[1];
	struct strp_msg *rxm;
	char *orig_buf, *buf;

	rxm = strp_msg(tls_strp_msg(sw_ctx));
	orig_buf = kmalloc(rxm->full_len + TLS_HEADER_SIZE +
			   TLS_CIPHER_AES_GCM_128_IV_SIZE, sk->sk_allocation);
	if (!orig_buf)
		return -ENOMEM;
	buf = orig_buf;

	err = tls_strp_msg_cow(sw_ctx);
	if (unlikely(err))
		goto free_buf;

	skb = tls_strp_msg(sw_ctx);
	rxm = strp_msg(skb);
	offset = rxm->offset;

	sg_init_table(sg, 1);
	sg_set_buf(&sg[0], buf,
		   rxm->full_len + TLS_HEADER_SIZE +
		   TLS_CIPHER_AES_GCM_128_IV_SIZE);
	err = skb_copy_bits(skb, offset, buf,
			    TLS_HEADER_SIZE + TLS_CIPHER_AES_GCM_128_IV_SIZE);
	if (err)
		goto free_buf;

	/* We are interested only in the decrypted data not the auth */
	err = decrypt_skb(sk, sg);
	if (err != -EBADMSG)
		goto free_buf;
	else
		err = 0;

	data_len = rxm->full_len - TLS_CIPHER_AES_GCM_128_TAG_SIZE;

	if (skb_pagelen(skb) > offset) {
		copy = min_t(int, skb_pagelen(skb) - offset, data_len);

		if (skb->decrypted) {
			err = skb_store_bits(skb, offset, buf, copy);
			if (err)
				goto free_buf;
		}

		offset += copy;
		buf += copy;
	}

	pos = skb_pagelen(skb);
	skb_walk_frags(skb, skb_iter) {
		int frag_pos;

		/* Practically all frags must belong to msg if reencrypt
		 * is needed with current strparser and coalescing logic,
		 * but strparser may "get optimized", so let's be safe.
		 */
		if (pos + skb_iter->len <= offset)
			goto done_with_frag;
		if (pos >= data_len + rxm->offset)
			break;

		frag_pos = offset - pos;
		copy = min_t(int, skb_iter->len - frag_pos,
			     data_len + rxm->offset - offset);

		if (skb_iter->decrypted) {
			err = skb_store_bits(skb_iter, frag_pos, buf, copy);
			if (err)
				goto free_buf;
		}

		offset += copy;
		buf += copy;
done_with_frag:
		pos += skb_iter->len;
	}

free_buf:
	kfree(orig_buf);
	return err;
}

int tls_device_decrypted(struct sock *sk, struct tls_context *tls_ctx)
{
	struct tls_offload_context_rx *ctx = tls_offload_ctx_rx(tls_ctx);
	struct tls_sw_context_rx *sw_ctx = tls_sw_ctx_rx(tls_ctx);
	struct sk_buff *skb = tls_strp_msg(sw_ctx);
	struct strp_msg *rxm = strp_msg(skb);
	int is_decrypted = skb->decrypted;
	int is_encrypted = !is_decrypted;
	struct sk_buff *skb_iter;
	int left;

	left = rxm->full_len - skb->len;
	/* Check if all the data is decrypted already */
	skb_iter = skb_shinfo(skb)->frag_list;
	while (skb_iter && left > 0) {
		is_decrypted &= skb_iter->decrypted;
		is_encrypted &= !skb_iter->decrypted;

		left -= skb_iter->len;
		skb_iter = skb_iter->next;
	}

	trace_tls_device_decrypted(sk, tcp_sk(sk)->copied_seq - rxm->full_len,
				   tls_ctx->rx.rec_seq, rxm->full_len,
				   is_encrypted, is_decrypted);

	if (unlikely(test_bit(TLS_RX_DEV_DEGRADED, &tls_ctx->flags))) {
		if (likely(is_encrypted || is_decrypted))
			return is_decrypted;

		/* After tls_device_down disables the offload, the next SKB will
		 * likely have initial fragments decrypted, and final ones not
		 * decrypted. We need to reencrypt that single SKB.
		 */
		return tls_device_reencrypt(sk, sw_ctx);
	}

	/* Return immediately if the record is either entirely plaintext or
	 * entirely ciphertext. Otherwise handle reencrypt partially decrypted
	 * record.
	 */
	if (is_decrypted) {
		ctx->resync_nh_reset = 1;
		return is_decrypted;
	}
	if (is_encrypted) {
		tls_device_core_ctrl_rx_resync(tls_ctx, ctx, sk, skb);
		return 0;
	}

	ctx->resync_nh_reset = 1;
	return tls_device_reencrypt(sk, sw_ctx);
}

static void tls_device_attach(struct tls_context *ctx, struct sock *sk,
			      struct net_device *netdev)
{
	if (sk->sk_destruct != tls_device_sk_destruct) {
		refcount_set(&ctx->refcount, 1);
		dev_hold(netdev);
		RCU_INIT_POINTER(ctx->netdev, netdev);
		spin_lock_irq(&tls_device_lock);
		list_add_tail(&ctx->list, &tls_device_list);
		spin_unlock_irq(&tls_device_lock);

		ctx->sk_destruct = sk->sk_destruct;
		smp_store_release(&sk->sk_destruct, tls_device_sk_destruct);
	}
}

int tls_set_device_offload(struct sock *sk, struct tls_context *ctx)
{
	u16 nonce_size, tag_size, iv_size, rec_seq_size, salt_size;
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_record_info *start_marker_record;
	struct tls_offload_context_tx *offload_ctx;
	struct tls_crypto_info *crypto_info;
	struct net_device *netdev;
	char *iv, *rec_seq;
	struct sk_buff *skb;
	__be64 rcd_sn;
	int rc;

	if (!ctx)
		return -EINVAL;

	if (ctx->priv_ctx_tx)
		return -EEXIST;

	netdev = get_netdev_for_sock(sk);
	if (!netdev) {
		pr_err_ratelimited("%s: netdev not found\n", __func__);
		return -EINVAL;
	}

	if (!(netdev->features & NETIF_F_HW_TLS_TX)) {
		rc = -EOPNOTSUPP;
		goto release_netdev;
	}

	crypto_info = &ctx->crypto_send.info;
	if (crypto_info->version != TLS_1_2_VERSION) {
		rc = -EOPNOTSUPP;
		goto release_netdev;
	}

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		nonce_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		tag_size = TLS_CIPHER_AES_GCM_128_TAG_SIZE;
		iv_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		iv = ((struct tls12_crypto_info_aes_gcm_128 *)crypto_info)->iv;
		rec_seq_size = TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE;
		salt_size = TLS_CIPHER_AES_GCM_128_SALT_SIZE;
		rec_seq =
		 ((struct tls12_crypto_info_aes_gcm_128 *)crypto_info)->rec_seq;
		break;
	default:
		rc = -EINVAL;
		goto release_netdev;
	}

	/* Sanity-check the rec_seq_size for stack allocations */
	if (rec_seq_size > TLS_MAX_REC_SEQ_SIZE) {
		rc = -EINVAL;
		goto release_netdev;
	}

	prot->version = crypto_info->version;
	prot->cipher_type = crypto_info->cipher_type;
	prot->prepend_size = TLS_HEADER_SIZE + nonce_size;
	prot->tag_size = tag_size;
	prot->overhead_size = prot->prepend_size + prot->tag_size;
	prot->iv_size = iv_size;
	prot->salt_size = salt_size;
	ctx->tx.iv = kmalloc(iv_size + TLS_CIPHER_AES_GCM_128_SALT_SIZE,
			     GFP_KERNEL);
	if (!ctx->tx.iv) {
		rc = -ENOMEM;
		goto release_netdev;
	}

	memcpy(ctx->tx.iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE, iv, iv_size);

	prot->rec_seq_size = rec_seq_size;
	ctx->tx.rec_seq = kmemdup(rec_seq, rec_seq_size, GFP_KERNEL);
	if (!ctx->tx.rec_seq) {
		rc = -ENOMEM;
		goto free_iv;
	}

	start_marker_record = kmalloc(sizeof(*start_marker_record), GFP_KERNEL);
	if (!start_marker_record) {
		rc = -ENOMEM;
		goto free_rec_seq;
	}

	offload_ctx = kzalloc(TLS_OFFLOAD_CONTEXT_SIZE_TX, GFP_KERNEL);
	if (!offload_ctx) {
		rc = -ENOMEM;
		goto free_marker_record;
	}

	rc = tls_sw_fallback_init(sk, offload_ctx, crypto_info);
	if (rc)
		goto free_offload_ctx;

	/* start at rec_seq - 1 to account for the start marker record */
	memcpy(&rcd_sn, ctx->tx.rec_seq, sizeof(rcd_sn));
	offload_ctx->unacked_record_sn = be64_to_cpu(rcd_sn) - 1;

	start_marker_record->end_seq = tcp_sk(sk)->write_seq;
	start_marker_record->len = 0;
	start_marker_record->num_frags = 0;

	INIT_WORK(&offload_ctx->destruct_work, tls_device_tx_del_task);
	offload_ctx->ctx = ctx;

	INIT_LIST_HEAD(&offload_ctx->records_list);
	list_add_tail(&start_marker_record->list, &offload_ctx->records_list);
	spin_lock_init(&offload_ctx->lock);
	sg_init_table(offload_ctx->sg_tx_data,
		      ARRAY_SIZE(offload_ctx->sg_tx_data));

	clean_acked_data_enable(inet_csk(sk), &tls_icsk_clean_acked);
	ctx->push_pending_record = tls_device_push_pending_record;

	/* TLS offload is greatly simplified if we don't send
	 * SKBs where only part of the payload needs to be encrypted.
	 * So mark the last skb in the write queue as end of record.
	 */
	skb = tcp_write_queue_tail(sk);
	if (skb)
		TCP_SKB_CB(skb)->eor = 1;

	/* Avoid offloading if the device is down
	 * We don't want to offload new flows after
	 * the NETDEV_DOWN event
	 *
	 * device_offload_lock is taken in tls_devices's NETDEV_DOWN
	 * handler thus protecting from the device going down before
	 * ctx was added to tls_device_list.
	 */
	down_read(&device_offload_lock);
	if (!(netdev->flags & IFF_UP)) {
		rc = -EINVAL;
		goto release_lock;
	}

	ctx->priv_ctx_tx = offload_ctx;
	rc = netdev->tlsdev_ops->tls_dev_add(netdev, sk, TLS_OFFLOAD_CTX_DIR_TX,
					     &ctx->crypto_send.info,
					     tcp_sk(sk)->write_seq);
	trace_tls_device_offload_set(sk, TLS_OFFLOAD_CTX_DIR_TX,
				     tcp_sk(sk)->write_seq, rec_seq, rc);
	if (rc)
		goto release_lock;

	tls_device_attach(ctx, sk, netdev);
	up_read(&device_offload_lock);

	/* following this assignment tls_is_sk_tx_device_offloaded
	 * will return true and the context might be accessed
	 * by the netdev's xmit function.
	 */
	smp_store_release(&sk->sk_validate_xmit_skb, tls_validate_xmit_skb);
	dev_put(netdev);

	return 0;

release_lock:
	up_read(&device_offload_lock);
	clean_acked_data_disable(inet_csk(sk));
	crypto_free_aead(offload_ctx->aead_send);
free_offload_ctx:
	kfree(offload_ctx);
	ctx->priv_ctx_tx = NULL;
free_marker_record:
	kfree(start_marker_record);
free_rec_seq:
	kfree(ctx->tx.rec_seq);
free_iv:
	kfree(ctx->tx.iv);
release_netdev:
	dev_put(netdev);
	return rc;
}

int tls_set_device_offload_rx(struct sock *sk, struct tls_context *ctx)
{
	struct tls12_crypto_info_aes_gcm_128 *info;
	struct tls_offload_context_rx *context;
	struct net_device *netdev;
	int rc = 0;

	if (ctx->crypto_recv.info.version != TLS_1_2_VERSION)
		return -EOPNOTSUPP;

	netdev = get_netdev_for_sock(sk);
	if (!netdev) {
		pr_err_ratelimited("%s: netdev not found\n", __func__);
		return -EINVAL;
	}

	if (!(netdev->features & NETIF_F_HW_TLS_RX)) {
		rc = -EOPNOTSUPP;
		goto release_netdev;
	}

	/* Avoid offloading if the device is down
	 * We don't want to offload new flows after
	 * the NETDEV_DOWN event
	 *
	 * device_offload_lock is taken in tls_devices's NETDEV_DOWN
	 * handler thus protecting from the device going down before
	 * ctx was added to tls_device_list.
	 */
	down_read(&device_offload_lock);
	if (!(netdev->flags & IFF_UP)) {
		rc = -EINVAL;
		goto release_lock;
	}

	context = kzalloc(TLS_OFFLOAD_CONTEXT_SIZE_RX, GFP_KERNEL);
	if (!context) {
		rc = -ENOMEM;
		goto release_lock;
	}
	context->resync_nh_reset = 1;

	ctx->priv_ctx_rx = context;
	rc = tls_set_sw_offload(sk, ctx, 0);
	if (rc)
		goto release_ctx;

	rc = netdev->tlsdev_ops->tls_dev_add(netdev, sk, TLS_OFFLOAD_CTX_DIR_RX,
					     &ctx->crypto_recv.info,
					     tcp_sk(sk)->copied_seq);
	info = (void *)&ctx->crypto_recv.info;
	trace_tls_device_offload_set(sk, TLS_OFFLOAD_CTX_DIR_RX,
				     tcp_sk(sk)->copied_seq, info->rec_seq, rc);
	if (rc)
		goto free_sw_resources;

	tls_device_attach(ctx, sk, netdev);
	up_read(&device_offload_lock);

	dev_put(netdev);

	return 0;

free_sw_resources:
	up_read(&device_offload_lock);
	tls_sw_free_resources_rx(sk);
	down_read(&device_offload_lock);
release_ctx:
	ctx->priv_ctx_rx = NULL;
release_lock:
	up_read(&device_offload_lock);
release_netdev:
	dev_put(netdev);
	return rc;
}

void tls_device_offload_cleanup_rx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct net_device *netdev;

	down_read(&device_offload_lock);
	netdev = rcu_dereference_protected(tls_ctx->netdev,
					   lockdep_is_held(&device_offload_lock));
	if (!netdev)
		goto out;

	netdev->tlsdev_ops->tls_dev_del(netdev, tls_ctx,
					TLS_OFFLOAD_CTX_DIR_RX);

	if (tls_ctx->tx_conf != TLS_HW) {
		dev_put(netdev);
		rcu_assign_pointer(tls_ctx->netdev, NULL);
	} else {
		set_bit(TLS_RX_DEV_CLOSED, &tls_ctx->flags);
	}
out:
	up_read(&device_offload_lock);
	tls_sw_release_resources_rx(sk);
}

static int tls_device_down(struct net_device *netdev)
{
	struct tls_context *ctx, *tmp;
	unsigned long flags;
	LIST_HEAD(list);

	/* Request a write lock to block new offload attempts */
	down_write(&device_offload_lock);

	spin_lock_irqsave(&tls_device_lock, flags);
	list_for_each_entry_safe(ctx, tmp, &tls_device_list, list) {
		struct net_device *ctx_netdev =
			rcu_dereference_protected(ctx->netdev,
						  lockdep_is_held(&device_offload_lock));

		if (ctx_netdev != netdev ||
		    !refcount_inc_not_zero(&ctx->refcount))
			continue;

		list_move(&ctx->list, &list);
	}
	spin_unlock_irqrestore(&tls_device_lock, flags);

	list_for_each_entry_safe(ctx, tmp, &list, list)	{
		/* Stop offloaded TX and switch to the fallback.
		 * tls_is_sk_tx_device_offloaded will return false.
		 */
		WRITE_ONCE(ctx->sk->sk_validate_xmit_skb, tls_validate_xmit_skb_sw);

		/* Stop the RX and TX resync.
		 * tls_dev_resync must not be called after tls_dev_del.
		 */
		rcu_assign_pointer(ctx->netdev, NULL);

		/* Start skipping the RX resync logic completely. */
		set_bit(TLS_RX_DEV_DEGRADED, &ctx->flags);

		/* Sync with inflight packets. After this point:
		 * TX: no non-encrypted packets will be passed to the driver.
		 * RX: resync requests from the driver will be ignored.
		 */
		synchronize_net();

		/* Release the offload context on the driver side. */
		if (ctx->tx_conf == TLS_HW)
			netdev->tlsdev_ops->tls_dev_del(netdev, ctx,
							TLS_OFFLOAD_CTX_DIR_TX);
		if (ctx->rx_conf == TLS_HW &&
		    !test_bit(TLS_RX_DEV_CLOSED, &ctx->flags))
			netdev->tlsdev_ops->tls_dev_del(netdev, ctx,
							TLS_OFFLOAD_CTX_DIR_RX);

		dev_put(netdev);

		/* Move the context to a separate list for two reasons:
		 * 1. When the context is deallocated, list_del is called.
		 * 2. It's no longer an offloaded context, so we don't want to
		 *    run offload-specific code on this context.
		 */
		spin_lock_irqsave(&tls_device_lock, flags);
		list_move_tail(&ctx->list, &tls_device_down_list);
		spin_unlock_irqrestore(&tls_device_lock, flags);

		/* Device contexts for RX and TX will be freed in on sk_destruct
		 * by tls_device_free_ctx. rx_conf and tx_conf stay in TLS_HW.
		 * Now release the ref taken above.
		 */
		if (refcount_dec_and_test(&ctx->refcount)) {
			/* sk_destruct ran after tls_device_down took a ref, and
			 * it returned early. Complete the destruction here.
			 */
			list_del(&ctx->list);
			tls_device_free_ctx(ctx);
		}
	}

	up_write(&device_offload_lock);

	flush_workqueue(destruct_wq);

	return NOTIFY_DONE;
}

static int tls_dev_event(struct notifier_block *this, unsigned long event,
			 void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (!dev->tlsdev_ops &&
	    !(dev->features & (NETIF_F_HW_TLS_RX | NETIF_F_HW_TLS_TX)))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
	case NETDEV_FEAT_CHANGE:
		if (netif_is_bond_master(dev))
			return NOTIFY_DONE;
		if ((dev->features & NETIF_F_HW_TLS_RX) &&
		    !dev->tlsdev_ops->tls_dev_resync)
			return NOTIFY_BAD;

		if  (dev->tlsdev_ops &&
		     dev->tlsdev_ops->tls_dev_add &&
		     dev->tlsdev_ops->tls_dev_del)
			return NOTIFY_DONE;
		else
			return NOTIFY_BAD;
	case NETDEV_DOWN:
		return tls_device_down(dev);
	}
	return NOTIFY_DONE;
}

static struct notifier_block tls_dev_notifier = {
	.notifier_call	= tls_dev_event,
};

int __init tls_device_init(void)
{
	int err;

	destruct_wq = alloc_workqueue("ktls_device_destruct", 0, 0);
	if (!destruct_wq)
		return -ENOMEM;

	err = register_netdevice_notifier(&tls_dev_notifier);
	if (err)
		destroy_workqueue(destruct_wq);

	return err;
}

void __exit tls_device_cleanup(void)
{
	unregister_netdevice_notifier(&tls_dev_notifier);
	destroy_workqueue(destruct_wq);
	clean_acked_data_flush();
}
