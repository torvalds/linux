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

/* device_offload_lock is used to synchronize tls_dev_add
 * against NETDEV_DOWN notifications.
 */
static DECLARE_RWSEM(device_offload_lock);

static void tls_device_gc_task(struct work_struct *work);

static DECLARE_WORK(tls_device_gc_work, tls_device_gc_task);
static LIST_HEAD(tls_device_gc_list);
static LIST_HEAD(tls_device_list);
static DEFINE_SPINLOCK(tls_device_lock);

static void tls_device_free_ctx(struct tls_context *ctx)
{
	struct tls_offload_context *offload_ctx = tls_offload_ctx(ctx);

	kfree(offload_ctx);
	kfree(ctx);
}

static void tls_device_gc_task(struct work_struct *work)
{
	struct tls_context *ctx, *tmp;
	unsigned long flags;
	LIST_HEAD(gc_list);

	spin_lock_irqsave(&tls_device_lock, flags);
	list_splice_init(&tls_device_gc_list, &gc_list);
	spin_unlock_irqrestore(&tls_device_lock, flags);

	list_for_each_entry_safe(ctx, tmp, &gc_list, list) {
		struct net_device *netdev = ctx->netdev;

		if (netdev) {
			netdev->tlsdev_ops->tls_dev_del(netdev, ctx,
							TLS_OFFLOAD_CTX_DIR_TX);
			dev_put(netdev);
		}

		list_del(&ctx->list);
		tls_device_free_ctx(ctx);
	}
}

static void tls_device_queue_ctx_destruction(struct tls_context *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&tls_device_lock, flags);
	list_move_tail(&ctx->list, &tls_device_gc_list);

	/* schedule_work inside the spinlock
	 * to make sure tls_device_down waits for that work.
	 */
	schedule_work(&tls_device_gc_work);

	spin_unlock_irqrestore(&tls_device_lock, flags);
}

/* We assume that the socket is already connected */
static struct net_device *get_netdev_for_sock(struct sock *sk)
{
	struct dst_entry *dst = sk_dst_get(sk);
	struct net_device *netdev = NULL;

	if (likely(dst)) {
		netdev = dst->dev;
		dev_hold(netdev);
	}

	dst_release(dst);

	return netdev;
}

static void destroy_record(struct tls_record_info *record)
{
	int nr_frags = record->num_frags;
	skb_frag_t *frag;

	while (nr_frags-- > 0) {
		frag = &record->frags[nr_frags];
		__skb_frag_unref(frag);
	}
	kfree(record);
}

static void delete_all_records(struct tls_offload_context *offload_ctx)
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
	struct tls_offload_context *ctx;
	u64 deleted_records = 0;
	unsigned long flags;

	if (!tls_ctx)
		return;

	ctx = tls_offload_ctx(tls_ctx);

	spin_lock_irqsave(&ctx->lock, flags);
	info = ctx->retransmit_hint;
	if (info && !before(acked_seq, info->end_seq)) {
		ctx->retransmit_hint = NULL;
		list_del(&info->list);
		destroy_record(info);
		deleted_records++;
	}

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
	struct tls_offload_context *ctx = tls_offload_ctx(tls_ctx);

	if (ctx->open_record)
		destroy_record(ctx->open_record);

	delete_all_records(ctx);
	crypto_free_aead(ctx->aead_send);
	ctx->sk_destruct(sk);
	clean_acked_data_disable(inet_csk(sk));

	if (refcount_dec_and_test(&tls_ctx->refcount))
		tls_device_queue_ctx_destruction(tls_ctx);
}
EXPORT_SYMBOL(tls_device_sk_destruct);

static void tls_append_frag(struct tls_record_info *record,
			    struct page_frag *pfrag,
			    int size)
{
	skb_frag_t *frag;

	frag = &record->frags[record->num_frags - 1];
	if (frag->page.p == pfrag->page &&
	    frag->page_offset + frag->size == pfrag->offset) {
		frag->size += size;
	} else {
		++frag;
		frag->page.p = pfrag->page;
		frag->page_offset = pfrag->offset;
		frag->size = size;
		++record->num_frags;
		get_page(pfrag->page);
	}

	pfrag->offset += size;
	record->len += size;
}

static int tls_push_record(struct sock *sk,
			   struct tls_context *ctx,
			   struct tls_offload_context *offload_ctx,
			   struct tls_record_info *record,
			   struct page_frag *pfrag,
			   int flags,
			   unsigned char record_type)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct page_frag dummy_tag_frag;
	skb_frag_t *frag;
	int i;

	/* fill prepend */
	frag = &record->frags[0];
	tls_fill_prepend(ctx,
			 skb_frag_address(frag),
			 record->len - ctx->tx.prepend_size,
			 record_type);

	/* HW doesn't care about the data in the tag, because it fills it. */
	dummy_tag_frag.page = skb_frag_page(frag);
	dummy_tag_frag.offset = 0;

	tls_append_frag(record, &dummy_tag_frag, ctx->tx.tag_size);
	record->end_seq = tp->write_seq + record->len;
	spin_lock_irq(&offload_ctx->lock);
	list_add_tail(&record->list, &offload_ctx->records_list);
	spin_unlock_irq(&offload_ctx->lock);
	offload_ctx->open_record = NULL;
	set_bit(TLS_PENDING_CLOSED_RECORD, &ctx->flags);
	tls_advance_record_sn(sk, &ctx->tx);

	for (i = 0; i < record->num_frags; i++) {
		frag = &record->frags[i];
		sg_unmark_end(&offload_ctx->sg_tx_data[i]);
		sg_set_page(&offload_ctx->sg_tx_data[i], skb_frag_page(frag),
			    frag->size, frag->page_offset);
		sk_mem_charge(sk, frag->size);
		get_page(skb_frag_page(frag));
	}
	sg_mark_end(&offload_ctx->sg_tx_data[record->num_frags - 1]);

	/* all ready, send */
	return tls_push_sg(sk, ctx, offload_ctx->sg_tx_data, 0, flags);
}

static int tls_create_new_record(struct tls_offload_context *offload_ctx,
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
	frag->page_offset = pfrag->offset;
	skb_frag_size_set(frag, prepend_size);

	get_page(pfrag->page);
	pfrag->offset += prepend_size;

	record->num_frags = 1;
	record->len = prepend_size;
	offload_ctx->open_record = record;
	return 0;
}

static int tls_do_allocation(struct sock *sk,
			     struct tls_offload_context *offload_ctx,
			     struct page_frag *pfrag,
			     size_t prepend_size)
{
	int ret;

	if (!offload_ctx->open_record) {
		if (unlikely(!skb_page_frag_refill(prepend_size, pfrag,
						   sk->sk_allocation))) {
			sk->sk_prot->enter_memory_pressure(sk);
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

static int tls_push_data(struct sock *sk,
			 struct iov_iter *msg_iter,
			 size_t size, int flags,
			 unsigned char record_type)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context *ctx = tls_offload_ctx(tls_ctx);
	int tls_push_record_flags = flags | MSG_SENDPAGE_NOTLAST;
	int more = flags & (MSG_SENDPAGE_NOTLAST | MSG_MORE);
	struct tls_record_info *record = ctx->open_record;
	struct page_frag *pfrag;
	size_t orig_size = size;
	u32 max_open_record_len;
	int copy, rc = 0;
	bool done = false;
	long timeo;

	if (flags &
	    ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL | MSG_SENDPAGE_NOTLAST))
		return -ENOTSUPP;

	if (sk->sk_err)
		return -sk->sk_err;

	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	rc = tls_complete_pending_work(sk, tls_ctx, flags, &timeo);
	if (rc < 0)
		return rc;

	pfrag = sk_page_frag(sk);

	/* TLS_HEADER_SIZE is not counted as part of the TLS record, and
	 * we need to leave room for an authentication tag.
	 */
	max_open_record_len = TLS_MAX_PAYLOAD_SIZE +
			      tls_ctx->tx.prepend_size;
	do {
		rc = tls_do_allocation(sk, ctx, pfrag,
				       tls_ctx->tx.prepend_size);
		if (rc) {
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
			} else if (record->len > tls_ctx->tx.prepend_size) {
				goto last_record;
			}

			break;
		}

		record = ctx->open_record;
		copy = min_t(size_t, size, (pfrag->size - pfrag->offset));
		copy = min_t(size_t, copy, (max_open_record_len - record->len));

		if (copy_from_iter_nocache(page_address(pfrag->page) +
					       pfrag->offset,
					   copy, msg_iter) != copy) {
			rc = -EFAULT;
			goto handle_error;
		}
		tls_append_frag(record, pfrag, copy);

		size -= copy;
		if (!size) {
last_record:
			tls_push_record_flags = flags;
			if (more) {
				tls_ctx->pending_open_record_frags =
						record->num_frags;
				break;
			}

			done = true;
		}

		if (done || record->len >= max_open_record_len ||
		    (record->num_frags >= MAX_SKB_FRAGS - 1)) {
			rc = tls_push_record(sk,
					     tls_ctx,
					     ctx,
					     record,
					     pfrag,
					     tls_push_record_flags,
					     record_type);
			if (rc < 0)
				break;
		}
	} while (!done);

	if (orig_size - size > 0)
		rc = orig_size - size;

	return rc;
}

int tls_device_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	unsigned char record_type = TLS_RECORD_TYPE_DATA;
	int rc;

	lock_sock(sk);

	if (unlikely(msg->msg_controllen)) {
		rc = tls_proccess_cmsg(sk, msg, &record_type);
		if (rc)
			goto out;
	}

	rc = tls_push_data(sk, &msg->msg_iter, size,
			   msg->msg_flags, record_type);

out:
	release_sock(sk);
	return rc;
}

int tls_device_sendpage(struct sock *sk, struct page *page,
			int offset, size_t size, int flags)
{
	struct iov_iter	msg_iter;
	char *kaddr = kmap(page);
	struct kvec iov;
	int rc;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	lock_sock(sk);

	if (flags & MSG_OOB) {
		rc = -ENOTSUPP;
		goto out;
	}

	iov.iov_base = kaddr + offset;
	iov.iov_len = size;
	iov_iter_kvec(&msg_iter, WRITE | ITER_KVEC, &iov, 1, size);
	rc = tls_push_data(sk, &msg_iter, size,
			   flags, TLS_RECORD_TYPE_DATA);
	kunmap(page);

out:
	release_sock(sk);
	return rc;
}

struct tls_record_info *tls_get_record(struct tls_offload_context *context,
				       u32 seq, u64 *p_record_sn)
{
	u64 record_sn = context->hint_record_sn;
	struct tls_record_info *info;

	info = context->retransmit_hint;
	if (!info ||
	    before(seq, info->end_seq - info->len)) {
		/* if retransmit_hint is irrelevant start
		 * from the beggining of the list
		 */
		info = list_first_entry(&context->records_list,
					struct tls_record_info, list);
		record_sn = context->unacked_record_sn;
	}

	list_for_each_entry_from(info, &context->records_list, list) {
		if (before(seq, info->end_seq)) {
			if (!context->retransmit_hint ||
			    after(info->end_seq,
				  context->retransmit_hint->end_seq)) {
				context->hint_record_sn = record_sn;
				context->retransmit_hint = info;
			}
			*p_record_sn = record_sn;
			return info;
		}
		record_sn++;
	}

	return NULL;
}
EXPORT_SYMBOL(tls_get_record);

static int tls_device_push_pending_record(struct sock *sk, int flags)
{
	struct iov_iter	msg_iter;

	iov_iter_kvec(&msg_iter, WRITE | ITER_KVEC, NULL, 0, 0);
	return tls_push_data(sk, &msg_iter, 0, flags, TLS_RECORD_TYPE_DATA);
}

int tls_set_device_offload(struct sock *sk, struct tls_context *ctx)
{
	u16 nonce_size, tag_size, iv_size, rec_seq_size;
	struct tls_record_info *start_marker_record;
	struct tls_offload_context *offload_ctx;
	struct tls_crypto_info *crypto_info;
	struct net_device *netdev;
	char *iv, *rec_seq;
	struct sk_buff *skb;
	int rc = -EINVAL;
	__be64 rcd_sn;

	if (!ctx)
		goto out;

	if (ctx->priv_ctx_tx) {
		rc = -EEXIST;
		goto out;
	}

	start_marker_record = kmalloc(sizeof(*start_marker_record), GFP_KERNEL);
	if (!start_marker_record) {
		rc = -ENOMEM;
		goto out;
	}

	offload_ctx = kzalloc(TLS_OFFLOAD_CONTEXT_SIZE, GFP_KERNEL);
	if (!offload_ctx) {
		rc = -ENOMEM;
		goto free_marker_record;
	}

	crypto_info = &ctx->crypto_send;
	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		nonce_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		tag_size = TLS_CIPHER_AES_GCM_128_TAG_SIZE;
		iv_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		iv = ((struct tls12_crypto_info_aes_gcm_128 *)crypto_info)->iv;
		rec_seq_size = TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE;
		rec_seq =
		 ((struct tls12_crypto_info_aes_gcm_128 *)crypto_info)->rec_seq;
		break;
	default:
		rc = -EINVAL;
		goto free_offload_ctx;
	}

	ctx->tx.prepend_size = TLS_HEADER_SIZE + nonce_size;
	ctx->tx.tag_size = tag_size;
	ctx->tx.overhead_size = ctx->tx.prepend_size + ctx->tx.tag_size;
	ctx->tx.iv_size = iv_size;
	ctx->tx.iv = kmalloc(iv_size + TLS_CIPHER_AES_GCM_128_SALT_SIZE,
			     GFP_KERNEL);
	if (!ctx->tx.iv) {
		rc = -ENOMEM;
		goto free_offload_ctx;
	}

	memcpy(ctx->tx.iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE, iv, iv_size);

	ctx->tx.rec_seq_size = rec_seq_size;
	ctx->tx.rec_seq = kmalloc(rec_seq_size, GFP_KERNEL);
	if (!ctx->tx.rec_seq) {
		rc = -ENOMEM;
		goto free_iv;
	}
	memcpy(ctx->tx.rec_seq, rec_seq, rec_seq_size);

	rc = tls_sw_fallback_init(sk, offload_ctx, crypto_info);
	if (rc)
		goto free_rec_seq;

	/* start at rec_seq - 1 to account for the start marker record */
	memcpy(&rcd_sn, ctx->tx.rec_seq, sizeof(rcd_sn));
	offload_ctx->unacked_record_sn = be64_to_cpu(rcd_sn) - 1;

	start_marker_record->end_seq = tcp_sk(sk)->write_seq;
	start_marker_record->len = 0;
	start_marker_record->num_frags = 0;

	INIT_LIST_HEAD(&offload_ctx->records_list);
	list_add_tail(&start_marker_record->list, &offload_ctx->records_list);
	spin_lock_init(&offload_ctx->lock);
	sg_init_table(offload_ctx->sg_tx_data,
		      ARRAY_SIZE(offload_ctx->sg_tx_data));

	clean_acked_data_enable(inet_csk(sk), &tls_icsk_clean_acked);
	ctx->push_pending_record = tls_device_push_pending_record;
	offload_ctx->sk_destruct = sk->sk_destruct;

	/* TLS offload is greatly simplified if we don't send
	 * SKBs where only part of the payload needs to be encrypted.
	 * So mark the last skb in the write queue as end of record.
	 */
	skb = tcp_write_queue_tail(sk);
	if (skb)
		TCP_SKB_CB(skb)->eor = 1;

	refcount_set(&ctx->refcount, 1);

	/* We support starting offload on multiple sockets
	 * concurrently, so we only need a read lock here.
	 * This lock must precede get_netdev_for_sock to prevent races between
	 * NETDEV_DOWN and setsockopt.
	 */
	down_read(&device_offload_lock);
	netdev = get_netdev_for_sock(sk);
	if (!netdev) {
		pr_err_ratelimited("%s: netdev not found\n", __func__);
		rc = -EINVAL;
		goto release_lock;
	}

	if (!(netdev->features & NETIF_F_HW_TLS_TX)) {
		rc = -ENOTSUPP;
		goto release_netdev;
	}

	/* Avoid offloading if the device is down
	 * We don't want to offload new flows after
	 * the NETDEV_DOWN event
	 */
	if (!(netdev->flags & IFF_UP)) {
		rc = -EINVAL;
		goto release_netdev;
	}

	ctx->priv_ctx_tx = offload_ctx;
	rc = netdev->tlsdev_ops->tls_dev_add(netdev, sk, TLS_OFFLOAD_CTX_DIR_TX,
					     &ctx->crypto_send,
					     tcp_sk(sk)->write_seq);
	if (rc)
		goto release_netdev;

	ctx->netdev = netdev;

	spin_lock_irq(&tls_device_lock);
	list_add_tail(&ctx->list, &tls_device_list);
	spin_unlock_irq(&tls_device_lock);

	sk->sk_validate_xmit_skb = tls_validate_xmit_skb;
	/* following this assignment tls_is_sk_tx_device_offloaded
	 * will return true and the context might be accessed
	 * by the netdev's xmit function.
	 */
	smp_store_release(&sk->sk_destruct,
			  &tls_device_sk_destruct);
	up_read(&device_offload_lock);
	goto out;

release_netdev:
	dev_put(netdev);
release_lock:
	up_read(&device_offload_lock);
	clean_acked_data_disable(inet_csk(sk));
	crypto_free_aead(offload_ctx->aead_send);
free_rec_seq:
	kfree(ctx->tx.rec_seq);
free_iv:
	kfree(ctx->tx.iv);
free_offload_ctx:
	kfree(offload_ctx);
	ctx->priv_ctx_tx = NULL;
free_marker_record:
	kfree(start_marker_record);
out:
	return rc;
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
		if (ctx->netdev != netdev ||
		    !refcount_inc_not_zero(&ctx->refcount))
			continue;

		list_move(&ctx->list, &list);
	}
	spin_unlock_irqrestore(&tls_device_lock, flags);

	list_for_each_entry_safe(ctx, tmp, &list, list)	{
		netdev->tlsdev_ops->tls_dev_del(netdev, ctx,
						TLS_OFFLOAD_CTX_DIR_TX);
		ctx->netdev = NULL;
		dev_put(netdev);
		list_del_init(&ctx->list);

		if (refcount_dec_and_test(&ctx->refcount))
			tls_device_free_ctx(ctx);
	}

	up_write(&device_offload_lock);

	flush_work(&tls_device_gc_work);

	return NOTIFY_DONE;
}

static int tls_dev_event(struct notifier_block *this, unsigned long event,
			 void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (!(dev->features & NETIF_F_HW_TLS_TX))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
	case NETDEV_FEAT_CHANGE:
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

void __init tls_device_init(void)
{
	register_netdevice_notifier(&tls_dev_notifier);
}

void __exit tls_device_cleanup(void)
{
	unregister_netdevice_notifier(&tls_dev_notifier);
	flush_work(&tls_device_gc_work);
}
