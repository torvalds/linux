// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Tom Herbert <tom@herbertland.com> */

#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/strparser.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/tls.h>

#include "tls.h"

static struct workqueue_struct *tls_strp_wq;

static void tls_strp_abort_strp(struct tls_strparser *strp, int err)
{
	if (strp->stopped)
		return;

	strp->stopped = 1;

	/* Report an error on the lower socket */
	WRITE_ONCE(strp->sk->sk_err, -err);
	/* Paired with smp_rmb() in tcp_poll() */
	smp_wmb();
	sk_error_report(strp->sk);
}

static void tls_strp_anchor_free(struct tls_strparser *strp)
{
	struct skb_shared_info *shinfo = skb_shinfo(strp->anchor);

	DEBUG_NET_WARN_ON_ONCE(atomic_read(&shinfo->dataref) != 1);
	if (!strp->copy_mode)
		shinfo->frag_list = NULL;
	consume_skb(strp->anchor);
	strp->anchor = NULL;
}

static struct sk_buff *
tls_strp_skb_copy(struct tls_strparser *strp, struct sk_buff *in_skb,
		  int offset, int len)
{
	struct sk_buff *skb;
	int i, err;

	skb = alloc_skb_with_frags(0, len, TLS_PAGE_ORDER,
				   &err, strp->sk->sk_allocation);
	if (!skb)
		return NULL;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON_ONCE(skb_copy_bits(in_skb, offset,
					   skb_frag_address(frag),
					   skb_frag_size(frag)));
		offset += skb_frag_size(frag);
	}

	skb->len = len;
	skb->data_len = len;
	skb_copy_header(skb, in_skb);
	return skb;
}

/* Create a new skb with the contents of input copied to its page frags */
static struct sk_buff *tls_strp_msg_make_copy(struct tls_strparser *strp)
{
	struct strp_msg *rxm;
	struct sk_buff *skb;

	skb = tls_strp_skb_copy(strp, strp->anchor, strp->stm.offset,
				strp->stm.full_len);
	if (!skb)
		return NULL;

	rxm = strp_msg(skb);
	rxm->offset = 0;
	return skb;
}

/* Steal the input skb, input msg is invalid after calling this function */
struct sk_buff *tls_strp_msg_detach(struct tls_sw_context_rx *ctx)
{
	struct tls_strparser *strp = &ctx->strp;

#ifdef CONFIG_TLS_DEVICE
	DEBUG_NET_WARN_ON_ONCE(!strp->anchor->decrypted);
#else
	/* This function turns an input into an output,
	 * that can only happen if we have offload.
	 */
	WARN_ON(1);
#endif

	if (strp->copy_mode) {
		struct sk_buff *skb;

		/* Replace anchor with an empty skb, this is a little
		 * dangerous but __tls_cur_msg() warns on empty skbs
		 * so hopefully we'll catch abuses.
		 */
		skb = alloc_skb(0, strp->sk->sk_allocation);
		if (!skb)
			return NULL;

		swap(strp->anchor, skb);
		return skb;
	}

	return tls_strp_msg_make_copy(strp);
}

/* Force the input skb to be in copy mode. The data ownership remains
 * with the input skb itself (meaning unpause will wipe it) but it can
 * be modified.
 */
int tls_strp_msg_cow(struct tls_sw_context_rx *ctx)
{
	struct tls_strparser *strp = &ctx->strp;
	struct sk_buff *skb;

	if (strp->copy_mode)
		return 0;

	skb = tls_strp_msg_make_copy(strp);
	if (!skb)
		return -ENOMEM;

	tls_strp_anchor_free(strp);
	strp->anchor = skb;

	tcp_read_done(strp->sk, strp->stm.full_len);
	strp->copy_mode = 1;

	return 0;
}

/* Make a clone (in the skb sense) of the input msg to keep a reference
 * to the underlying data. The reference-holding skbs get placed on
 * @dst.
 */
int tls_strp_msg_hold(struct tls_strparser *strp, struct sk_buff_head *dst)
{
	struct skb_shared_info *shinfo = skb_shinfo(strp->anchor);

	if (strp->copy_mode) {
		struct sk_buff *skb;

		WARN_ON_ONCE(!shinfo->nr_frags);

		/* We can't skb_clone() the anchor, it gets wiped by unpause */
		skb = alloc_skb(0, strp->sk->sk_allocation);
		if (!skb)
			return -ENOMEM;

		__skb_queue_tail(dst, strp->anchor);
		strp->anchor = skb;
	} else {
		struct sk_buff *iter, *clone;
		int chunk, len, offset;

		offset = strp->stm.offset;
		len = strp->stm.full_len;
		iter = shinfo->frag_list;

		while (len > 0) {
			if (iter->len <= offset) {
				offset -= iter->len;
				goto next;
			}

			chunk = iter->len - offset;
			offset = 0;

			clone = skb_clone(iter, strp->sk->sk_allocation);
			if (!clone)
				return -ENOMEM;
			__skb_queue_tail(dst, clone);

			len -= chunk;
next:
			iter = iter->next;
		}
	}

	return 0;
}

static void tls_strp_flush_anchor_copy(struct tls_strparser *strp)
{
	struct skb_shared_info *shinfo = skb_shinfo(strp->anchor);
	int i;

	DEBUG_NET_WARN_ON_ONCE(atomic_read(&shinfo->dataref) != 1);

	for (i = 0; i < shinfo->nr_frags; i++)
		__skb_frag_unref(&shinfo->frags[i], false);
	shinfo->nr_frags = 0;
	if (strp->copy_mode) {
		kfree_skb_list(shinfo->frag_list);
		shinfo->frag_list = NULL;
	}
	strp->copy_mode = 0;
	strp->mixed_decrypted = 0;
}

static int tls_strp_copyin_frag(struct tls_strparser *strp, struct sk_buff *skb,
				struct sk_buff *in_skb, unsigned int offset,
				size_t in_len)
{
	size_t len, chunk;
	skb_frag_t *frag;
	int sz;

	frag = &skb_shinfo(skb)->frags[skb->len / PAGE_SIZE];

	len = in_len;
	/* First make sure we got the header */
	if (!strp->stm.full_len) {
		/* Assume one page is more than enough for headers */
		chunk =	min_t(size_t, len, PAGE_SIZE - skb_frag_size(frag));
		WARN_ON_ONCE(skb_copy_bits(in_skb, offset,
					   skb_frag_address(frag) +
					   skb_frag_size(frag),
					   chunk));

		skb->len += chunk;
		skb->data_len += chunk;
		skb_frag_size_add(frag, chunk);

		sz = tls_rx_msg_size(strp, skb);
		if (sz < 0)
			return sz;

		/* We may have over-read, sz == 0 is guaranteed under-read */
		if (unlikely(sz && sz < skb->len)) {
			int over = skb->len - sz;

			WARN_ON_ONCE(over > chunk);
			skb->len -= over;
			skb->data_len -= over;
			skb_frag_size_add(frag, -over);

			chunk -= over;
		}

		frag++;
		len -= chunk;
		offset += chunk;

		strp->stm.full_len = sz;
		if (!strp->stm.full_len)
			goto read_done;
	}

	/* Load up more data */
	while (len && strp->stm.full_len > skb->len) {
		chunk =	min_t(size_t, len, strp->stm.full_len - skb->len);
		chunk = min_t(size_t, chunk, PAGE_SIZE - skb_frag_size(frag));
		WARN_ON_ONCE(skb_copy_bits(in_skb, offset,
					   skb_frag_address(frag) +
					   skb_frag_size(frag),
					   chunk));

		skb->len += chunk;
		skb->data_len += chunk;
		skb_frag_size_add(frag, chunk);
		frag++;
		len -= chunk;
		offset += chunk;
	}

read_done:
	return in_len - len;
}

static int tls_strp_copyin_skb(struct tls_strparser *strp, struct sk_buff *skb,
			       struct sk_buff *in_skb, unsigned int offset,
			       size_t in_len)
{
	struct sk_buff *nskb, *first, *last;
	struct skb_shared_info *shinfo;
	size_t chunk;
	int sz;

	if (strp->stm.full_len)
		chunk = strp->stm.full_len - skb->len;
	else
		chunk = TLS_MAX_PAYLOAD_SIZE + PAGE_SIZE;
	chunk = min(chunk, in_len);

	nskb = tls_strp_skb_copy(strp, in_skb, offset, chunk);
	if (!nskb)
		return -ENOMEM;

	shinfo = skb_shinfo(skb);
	if (!shinfo->frag_list) {
		shinfo->frag_list = nskb;
		nskb->prev = nskb;
	} else {
		first = shinfo->frag_list;
		last = first->prev;
		last->next = nskb;
		first->prev = nskb;
	}

	skb->len += chunk;
	skb->data_len += chunk;

	if (!strp->stm.full_len) {
		sz = tls_rx_msg_size(strp, skb);
		if (sz < 0)
			return sz;

		/* We may have over-read, sz == 0 is guaranteed under-read */
		if (unlikely(sz && sz < skb->len)) {
			int over = skb->len - sz;

			WARN_ON_ONCE(over > chunk);
			skb->len -= over;
			skb->data_len -= over;
			__pskb_trim(nskb, nskb->len - over);

			chunk -= over;
		}

		strp->stm.full_len = sz;
	}

	return chunk;
}

static int tls_strp_copyin(read_descriptor_t *desc, struct sk_buff *in_skb,
			   unsigned int offset, size_t in_len)
{
	struct tls_strparser *strp = (struct tls_strparser *)desc->arg.data;
	struct sk_buff *skb;
	int ret;

	if (strp->msg_ready)
		return 0;

	skb = strp->anchor;
	if (!skb->len)
		skb_copy_decrypted(skb, in_skb);
	else
		strp->mixed_decrypted |= !!skb_cmp_decrypted(skb, in_skb);

	if (IS_ENABLED(CONFIG_TLS_DEVICE) && strp->mixed_decrypted)
		ret = tls_strp_copyin_skb(strp, skb, in_skb, offset, in_len);
	else
		ret = tls_strp_copyin_frag(strp, skb, in_skb, offset, in_len);
	if (ret < 0) {
		desc->error = ret;
		ret = 0;
	}

	if (strp->stm.full_len && strp->stm.full_len == skb->len) {
		desc->count = 0;

		strp->msg_ready = 1;
		tls_rx_msg_ready(strp);
	}

	return ret;
}

static int tls_strp_read_copyin(struct tls_strparser *strp)
{
	read_descriptor_t desc;

	desc.arg.data = strp;
	desc.error = 0;
	desc.count = 1; /* give more than one skb per call */

	/* sk should be locked here, so okay to do read_sock */
	tcp_read_sock(strp->sk, &desc, tls_strp_copyin);

	return desc.error;
}

static int tls_strp_read_copy(struct tls_strparser *strp, bool qshort)
{
	struct skb_shared_info *shinfo;
	struct page *page;
	int need_spc, len;

	/* If the rbuf is small or rcv window has collapsed to 0 we need
	 * to read the data out. Otherwise the connection will stall.
	 * Without pressure threshold of INT_MAX will never be ready.
	 */
	if (likely(qshort && !tcp_epollin_ready(strp->sk, INT_MAX)))
		return 0;

	shinfo = skb_shinfo(strp->anchor);
	shinfo->frag_list = NULL;

	/* If we don't know the length go max plus page for cipher overhead */
	need_spc = strp->stm.full_len ?: TLS_MAX_PAYLOAD_SIZE + PAGE_SIZE;

	for (len = need_spc; len > 0; len -= PAGE_SIZE) {
		page = alloc_page(strp->sk->sk_allocation);
		if (!page) {
			tls_strp_flush_anchor_copy(strp);
			return -ENOMEM;
		}

		skb_fill_page_desc(strp->anchor, shinfo->nr_frags++,
				   page, 0, 0);
	}

	strp->copy_mode = 1;
	strp->stm.offset = 0;

	strp->anchor->len = 0;
	strp->anchor->data_len = 0;
	strp->anchor->truesize = round_up(need_spc, PAGE_SIZE);

	tls_strp_read_copyin(strp);

	return 0;
}

static bool tls_strp_check_queue_ok(struct tls_strparser *strp)
{
	unsigned int len = strp->stm.offset + strp->stm.full_len;
	struct sk_buff *first, *skb;
	u32 seq;

	first = skb_shinfo(strp->anchor)->frag_list;
	skb = first;
	seq = TCP_SKB_CB(first)->seq;

	/* Make sure there's no duplicate data in the queue,
	 * and the decrypted status matches.
	 */
	while (skb->len < len) {
		seq += skb->len;
		len -= skb->len;
		skb = skb->next;

		if (TCP_SKB_CB(skb)->seq != seq)
			return false;
		if (skb_cmp_decrypted(first, skb))
			return false;
	}

	return true;
}

static void tls_strp_load_anchor_with_queue(struct tls_strparser *strp, int len)
{
	struct tcp_sock *tp = tcp_sk(strp->sk);
	struct sk_buff *first;
	u32 offset;

	first = tcp_recv_skb(strp->sk, tp->copied_seq, &offset);
	if (WARN_ON_ONCE(!first))
		return;

	/* Bestow the state onto the anchor */
	strp->anchor->len = offset + len;
	strp->anchor->data_len = offset + len;
	strp->anchor->truesize = offset + len;

	skb_shinfo(strp->anchor)->frag_list = first;

	skb_copy_header(strp->anchor, first);
	strp->anchor->destructor = NULL;

	strp->stm.offset = offset;
}

void tls_strp_msg_load(struct tls_strparser *strp, bool force_refresh)
{
	struct strp_msg *rxm;
	struct tls_msg *tlm;

	DEBUG_NET_WARN_ON_ONCE(!strp->msg_ready);
	DEBUG_NET_WARN_ON_ONCE(!strp->stm.full_len);

	if (!strp->copy_mode && force_refresh) {
		if (WARN_ON(tcp_inq(strp->sk) < strp->stm.full_len))
			return;

		tls_strp_load_anchor_with_queue(strp, strp->stm.full_len);
	}

	rxm = strp_msg(strp->anchor);
	rxm->full_len	= strp->stm.full_len;
	rxm->offset	= strp->stm.offset;
	tlm = tls_msg(strp->anchor);
	tlm->control	= strp->mark;
}

/* Called with lock held on lower socket */
static int tls_strp_read_sock(struct tls_strparser *strp)
{
	int sz, inq;

	inq = tcp_inq(strp->sk);
	if (inq < 1)
		return 0;

	if (unlikely(strp->copy_mode))
		return tls_strp_read_copyin(strp);

	if (inq < strp->stm.full_len)
		return tls_strp_read_copy(strp, true);

	if (!strp->stm.full_len) {
		tls_strp_load_anchor_with_queue(strp, inq);

		sz = tls_rx_msg_size(strp, strp->anchor);
		if (sz < 0) {
			tls_strp_abort_strp(strp, sz);
			return sz;
		}

		strp->stm.full_len = sz;

		if (!strp->stm.full_len || inq < strp->stm.full_len)
			return tls_strp_read_copy(strp, true);
	}

	if (!tls_strp_check_queue_ok(strp))
		return tls_strp_read_copy(strp, false);

	strp->msg_ready = 1;
	tls_rx_msg_ready(strp);

	return 0;
}

void tls_strp_check_rcv(struct tls_strparser *strp)
{
	if (unlikely(strp->stopped) || strp->msg_ready)
		return;

	if (tls_strp_read_sock(strp) == -ENOMEM)
		queue_work(tls_strp_wq, &strp->work);
}

/* Lower sock lock held */
void tls_strp_data_ready(struct tls_strparser *strp)
{
	/* This check is needed to synchronize with do_tls_strp_work.
	 * do_tls_strp_work acquires a process lock (lock_sock) whereas
	 * the lock held here is bh_lock_sock. The two locks can be
	 * held by different threads at the same time, but bh_lock_sock
	 * allows a thread in BH context to safely check if the process
	 * lock is held. In this case, if the lock is held, queue work.
	 */
	if (sock_owned_by_user_nocheck(strp->sk)) {
		queue_work(tls_strp_wq, &strp->work);
		return;
	}

	tls_strp_check_rcv(strp);
}

static void tls_strp_work(struct work_struct *w)
{
	struct tls_strparser *strp =
		container_of(w, struct tls_strparser, work);

	lock_sock(strp->sk);
	tls_strp_check_rcv(strp);
	release_sock(strp->sk);
}

void tls_strp_msg_done(struct tls_strparser *strp)
{
	WARN_ON(!strp->stm.full_len);

	if (likely(!strp->copy_mode))
		tcp_read_done(strp->sk, strp->stm.full_len);
	else
		tls_strp_flush_anchor_copy(strp);

	strp->msg_ready = 0;
	memset(&strp->stm, 0, sizeof(strp->stm));

	tls_strp_check_rcv(strp);
}

void tls_strp_stop(struct tls_strparser *strp)
{
	strp->stopped = 1;
}

int tls_strp_init(struct tls_strparser *strp, struct sock *sk)
{
	memset(strp, 0, sizeof(*strp));

	strp->sk = sk;

	strp->anchor = alloc_skb(0, GFP_KERNEL);
	if (!strp->anchor)
		return -ENOMEM;

	INIT_WORK(&strp->work, tls_strp_work);

	return 0;
}

/* strp must already be stopped so that tls_strp_recv will no longer be called.
 * Note that tls_strp_done is not called with the lower socket held.
 */
void tls_strp_done(struct tls_strparser *strp)
{
	WARN_ON(!strp->stopped);

	cancel_work_sync(&strp->work);
	tls_strp_anchor_free(strp);
}

int __init tls_strp_dev_init(void)
{
	tls_strp_wq = create_workqueue("tls-strp");
	if (unlikely(!tls_strp_wq))
		return -ENOMEM;

	return 0;
}

void tls_strp_dev_exit(void)
{
	destroy_workqueue(tls_strp_wq);
}
