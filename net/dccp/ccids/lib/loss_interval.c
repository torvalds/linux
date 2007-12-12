/*
 *  net/dccp/ccids/lib/loss_interval.c
 *
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-7 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include <net/sock.h>
#include "tfrc.h"

#define DCCP_LI_HIST_IVAL_F_LENGTH  8

struct dccp_li_hist_entry {
	struct list_head dccplih_node;
	u64		 dccplih_seqno:48,
			 dccplih_win_count:4;
	u32		 dccplih_interval;
};

static struct kmem_cache  *tfrc_lh_slab  __read_mostly;
/* Loss Interval weights from [RFC 3448, 5.4], scaled by 10 */
static const int tfrc_lh_weights[NINTERVAL] = { 10, 10, 10, 10, 8, 6, 4, 2 };

/* implements LIFO semantics on the array */
static inline u8 LIH_INDEX(const u8 ctr)
{
	return (LIH_SIZE - 1 - (ctr % LIH_SIZE));
}

/* the `counter' index always points at the next entry to be populated */
static inline struct tfrc_loss_interval *tfrc_lh_peek(struct tfrc_loss_hist *lh)
{
	return lh->counter ? lh->ring[LIH_INDEX(lh->counter - 1)] : NULL;
}

/* given i with 0 <= i <= k, return I_i as per the rfc3448bis notation */
static inline u32 tfrc_lh_get_interval(struct tfrc_loss_hist *lh, const u8 i)
{
	BUG_ON(i >= lh->counter);
	return lh->ring[LIH_INDEX(lh->counter - i - 1)]->li_length;
}

/*
 *	On-demand allocation and de-allocation of entries
 */
static struct tfrc_loss_interval *tfrc_lh_demand_next(struct tfrc_loss_hist *lh)
{
	if (lh->ring[LIH_INDEX(lh->counter)] == NULL)
		lh->ring[LIH_INDEX(lh->counter)] = kmem_cache_alloc(tfrc_lh_slab,
								    GFP_ATOMIC);
	return lh->ring[LIH_INDEX(lh->counter)];
}

void tfrc_lh_cleanup(struct tfrc_loss_hist *lh)
{
	if (!tfrc_lh_is_initialised(lh))
		return;

	for (lh->counter = 0; lh->counter < LIH_SIZE; lh->counter++)
		if (lh->ring[LIH_INDEX(lh->counter)] != NULL) {
			kmem_cache_free(tfrc_lh_slab,
					lh->ring[LIH_INDEX(lh->counter)]);
			lh->ring[LIH_INDEX(lh->counter)] = NULL;
		}
}
EXPORT_SYMBOL_GPL(tfrc_lh_cleanup);

static struct kmem_cache *dccp_li_cachep __read_mostly;

static inline struct dccp_li_hist_entry *dccp_li_hist_entry_new(const gfp_t prio)
{
	return kmem_cache_alloc(dccp_li_cachep, prio);
}

static inline void dccp_li_hist_entry_delete(struct dccp_li_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(dccp_li_cachep, entry);
}

void dccp_li_hist_purge(struct list_head *list)
{
	struct dccp_li_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, list, dccplih_node) {
		list_del_init(&entry->dccplih_node);
		kmem_cache_free(dccp_li_cachep, entry);
	}
}

EXPORT_SYMBOL_GPL(dccp_li_hist_purge);

/* Weights used to calculate loss event rate */
/*
 * These are integers as per section 8 of RFC3448. We can then divide by 4 *
 * when we use it.
 */
static const int dccp_li_hist_w[DCCP_LI_HIST_IVAL_F_LENGTH] = {
	4, 4, 4, 4, 3, 2, 1, 1,
};

u32 dccp_li_hist_calc_i_mean(struct list_head *list)
{
	struct dccp_li_hist_entry *li_entry, *li_next;
	int i = 0;
	u32 i_tot;
	u32 i_tot0 = 0;
	u32 i_tot1 = 0;
	u32 w_tot  = 0;

	list_for_each_entry_safe(li_entry, li_next, list, dccplih_node) {
		if (li_entry->dccplih_interval != ~0U) {
			i_tot0 += li_entry->dccplih_interval * dccp_li_hist_w[i];
			w_tot  += dccp_li_hist_w[i];
			if (i != 0)
				i_tot1 += li_entry->dccplih_interval * dccp_li_hist_w[i - 1];
		}


		if (++i > DCCP_LI_HIST_IVAL_F_LENGTH)
			break;
	}

	if (i != DCCP_LI_HIST_IVAL_F_LENGTH)
		return 0;

	i_tot = max(i_tot0, i_tot1);

	if (!w_tot) {
		DCCP_WARN("w_tot = 0\n");
		return 1;
	}

	return i_tot / w_tot;
}

EXPORT_SYMBOL_GPL(dccp_li_hist_calc_i_mean);

static void tfrc_lh_calc_i_mean(struct tfrc_loss_hist *lh)
{
	u32 i_i, i_tot0 = 0, i_tot1 = 0, w_tot = 0;
	int i, k = tfrc_lh_length(lh) - 1; /* k is as in rfc3448bis, 5.4 */

	for (i=0; i <= k; i++) {
		i_i = tfrc_lh_get_interval(lh, i);

		if (i < k) {
			i_tot0 += i_i * tfrc_lh_weights[i];
			w_tot  += tfrc_lh_weights[i];
		}
		if (i > 0)
			i_tot1 += i_i * tfrc_lh_weights[i-1];
	}

	BUG_ON(w_tot == 0);
	lh->i_mean = max(i_tot0, i_tot1) / w_tot;
}

/**
 * tfrc_lh_update_i_mean  -  Update the `open' loss interval I_0
 * For recomputing p: returns `true' if p > p_prev  <=>  1/p < 1/p_prev
 */
u8 tfrc_lh_update_i_mean(struct tfrc_loss_hist *lh, struct sk_buff *skb)
{
	struct tfrc_loss_interval *cur = tfrc_lh_peek(lh);
	u32 old_i_mean = lh->i_mean;
	s64 length;

	if (cur == NULL)			/* not initialised */
		return 0;

	length = dccp_delta_seqno(cur->li_seqno, DCCP_SKB_CB(skb)->dccpd_seq);

	if (length - cur->li_length <= 0)	/* duplicate or reordered */
		return 0;

	if (SUB16(dccp_hdr(skb)->dccph_ccval, cur->li_ccval) > 4)
		/*
		 * Implements RFC 4342, 10.2:
		 * If a packet S (skb) exists whose seqno comes `after' the one
		 * starting the current loss interval (cur) and if the modulo-16
		 * distance from C(cur) to C(S) is greater than 4, consider all
		 * subsequent packets as belonging to a new loss interval. This
		 * test is necessary since CCVal may wrap between intervals.
		 */
		cur->li_is_closed = 1;

	if (tfrc_lh_length(lh) == 1)		/* due to RFC 3448, 6.3.1 */
		return 0;

	cur->li_length = length;
	tfrc_lh_calc_i_mean(lh);

	return (lh->i_mean < old_i_mean);
}
EXPORT_SYMBOL_GPL(tfrc_lh_update_i_mean);

static int dccp_li_hist_interval_new(struct list_head *list,
				     const u64 seq_loss, const u8 win_loss)
{
	struct dccp_li_hist_entry *entry;
	int i;

	for (i = 0; i < DCCP_LI_HIST_IVAL_F_LENGTH; i++) {
		entry = dccp_li_hist_entry_new(GFP_ATOMIC);
		if (entry == NULL) {
			dccp_li_hist_purge(list);
			DCCP_BUG("loss interval list entry is NULL");
			return 0;
		}
		entry->dccplih_interval = ~0;
		list_add(&entry->dccplih_node, list);
	}

	entry->dccplih_seqno     = seq_loss;
	entry->dccplih_win_count = win_loss;
	return 1;
}

/* calculate first loss interval
 *
 * returns estimated loss interval in usecs */
static u32 dccp_li_calc_first_li(struct sock *sk,
				 struct list_head *hist_list,
				 ktime_t last_feedback,
				 u16 s, u32 bytes_recv,
				 u32 previous_x_recv)
{
/*
 * FIXME:
 * Will be rewritten in the upcoming new loss intervals code.
 * Has to be commented ou because it relies on the old rx history
 * data structures
 */
#if 0
	struct tfrc_rx_hist_entry *entry, *next, *tail = NULL;
	u32 x_recv, p;
	suseconds_t rtt, delta;
	ktime_t tstamp = ktime_set(0, 0);
	int interval = 0;
	int win_count = 0;
	int step = 0;
	u64 fval;

	list_for_each_entry_safe(entry, next, hist_list, tfrchrx_node) {
		if (tfrc_rx_hist_entry_data_packet(entry)) {
			tail = entry;

			switch (step) {
			case 0:
				tstamp	  = entry->tfrchrx_tstamp;
				win_count = entry->tfrchrx_ccval;
				step = 1;
				break;
			case 1:
				interval = win_count - entry->tfrchrx_ccval;
				if (interval < 0)
					interval += TFRC_WIN_COUNT_LIMIT;
				if (interval > 4)
					goto found;
				break;
			}
		}
	}

	if (unlikely(step == 0)) {
		DCCP_WARN("%s(%p), packet history has no data packets!\n",
			  dccp_role(sk), sk);
		return ~0;
	}

	if (unlikely(interval == 0)) {
		DCCP_WARN("%s(%p), Could not find a win_count interval > 0. "
			  "Defaulting to 1\n", dccp_role(sk), sk);
		interval = 1;
	}
found:
	if (!tail) {
		DCCP_CRIT("tail is null\n");
		return ~0;
	}

	delta = ktime_us_delta(tstamp, tail->tfrchrx_tstamp);
	DCCP_BUG_ON(delta < 0);

	rtt = delta * 4 / interval;
	dccp_pr_debug("%s(%p), approximated RTT to %dus\n",
		      dccp_role(sk), sk, (int)rtt);

	/*
	 * Determine the length of the first loss interval via inverse lookup.
	 * Assume that X_recv can be computed by the throughput equation
	 *		    s
	 *	X_recv = --------
	 *		 R * fval
	 * Find some p such that f(p) = fval; return 1/p [RFC 3448, 6.3.1].
	 */
	if (rtt == 0) {			/* would result in divide-by-zero */
		DCCP_WARN("RTT==0\n");
		return ~0;
	}

	delta = ktime_us_delta(ktime_get_real(), last_feedback);
	DCCP_BUG_ON(delta <= 0);

	x_recv = scaled_div32(bytes_recv, delta);
	if (x_recv == 0) {		/* would also trigger divide-by-zero */
		DCCP_WARN("X_recv==0\n");
		if (previous_x_recv == 0) {
			DCCP_BUG("stored value of X_recv is zero");
			return ~0;
		}
		x_recv = previous_x_recv;
	}

	fval = scaled_div(s, rtt);
	fval = scaled_div32(fval, x_recv);
	p = tfrc_calc_x_reverse_lookup(fval);

	dccp_pr_debug("%s(%p), receive rate=%u bytes/s, implied "
		      "loss rate=%u\n", dccp_role(sk), sk, x_recv, p);

	if (p != 0)
		return 1000000 / p;
#endif
	return ~0;
}

void dccp_li_update_li(struct sock *sk,
		       struct list_head *li_hist_list,
		       struct list_head *hist_list,
		       ktime_t last_feedback, u16 s, u32 bytes_recv,
		       u32 previous_x_recv, u64 seq_loss, u8 win_loss)
{
	struct dccp_li_hist_entry *head;
	u64 seq_temp;

	if (list_empty(li_hist_list)) {
		if (!dccp_li_hist_interval_new(li_hist_list, seq_loss,
					       win_loss))
			return;

		head = list_entry(li_hist_list->next, struct dccp_li_hist_entry,
				  dccplih_node);
		head->dccplih_interval = dccp_li_calc_first_li(sk, hist_list,
							       last_feedback,
							       s, bytes_recv,
							       previous_x_recv);
	} else {
		struct dccp_li_hist_entry *entry;
		struct list_head *tail;

		head = list_entry(li_hist_list->next, struct dccp_li_hist_entry,
				  dccplih_node);
		/* FIXME win count check removed as was wrong */
		/* should make this check with receive history */
		/* and compare there as per section 10.2 of RFC4342 */

		/* new loss event detected */
		/* calculate last interval length */
		seq_temp = dccp_delta_seqno(head->dccplih_seqno, seq_loss);
		entry = dccp_li_hist_entry_new(GFP_ATOMIC);

		if (entry == NULL) {
			DCCP_BUG("out of memory - can not allocate entry");
			return;
		}

		list_add(&entry->dccplih_node, li_hist_list);

		tail = li_hist_list->prev;
		list_del(tail);
		kmem_cache_free(dccp_li_cachep, tail);

		/* Create the newest interval */
		entry->dccplih_seqno = seq_loss;
		entry->dccplih_interval = seq_temp;
		entry->dccplih_win_count = win_loss;
	}
}

EXPORT_SYMBOL_GPL(dccp_li_update_li);

/* Determine if `new_loss' does begin a new loss interval [RFC 4342, 10.2] */
static inline u8 tfrc_lh_is_new_loss(struct tfrc_loss_interval *cur,
				     struct tfrc_rx_hist_entry *new_loss)
{
	return	dccp_delta_seqno(cur->li_seqno, new_loss->tfrchrx_seqno) > 0 &&
		(cur->li_is_closed || SUB16(new_loss->tfrchrx_ccval, cur->li_ccval) > 4);
}

/** tfrc_lh_interval_add  -  Insert new record into the Loss Interval database
 * @lh:		   Loss Interval database
 * @rh:		   Receive history containing a fresh loss event
 * @calc_first_li: Caller-dependent routine to compute length of first interval
 * @sk:		   Used by @calc_first_li in caller-specific way (subtyping)
 * Updates I_mean and returns 1 if a new interval has in fact been added to @lh.
 */
int tfrc_lh_interval_add(struct tfrc_loss_hist *lh, struct tfrc_rx_hist *rh,
			 u32 (*calc_first_li)(struct sock *), struct sock *sk)
{
	struct tfrc_loss_interval *cur = tfrc_lh_peek(lh), *new;

	if (cur != NULL && !tfrc_lh_is_new_loss(cur, tfrc_rx_hist_loss_prev(rh)))
		return 0;

	new = tfrc_lh_demand_next(lh);
	if (unlikely(new == NULL)) {
		DCCP_CRIT("Cannot allocate/add loss record.");
		return 0;
	}

	new->li_seqno	  = tfrc_rx_hist_loss_prev(rh)->tfrchrx_seqno;
	new->li_ccval	  = tfrc_rx_hist_loss_prev(rh)->tfrchrx_ccval;
	new->li_is_closed = 0;

	if (++lh->counter == 1)
		lh->i_mean = new->li_length = (*calc_first_li)(sk);
	else {
		cur->li_length = dccp_delta_seqno(cur->li_seqno, new->li_seqno);
		new->li_length = dccp_delta_seqno(new->li_seqno,
				  tfrc_rx_hist_last_rcv(rh)->tfrchrx_seqno);
		if (lh->counter > (2*LIH_SIZE))
			lh->counter -= LIH_SIZE;

		tfrc_lh_calc_i_mean(lh);
	}
	return 1;
}
EXPORT_SYMBOL_GPL(tfrc_lh_interval_add);

int __init tfrc_li_init(void)
{
	tfrc_lh_slab = kmem_cache_create("tfrc_li_hist",
					 sizeof(struct tfrc_loss_interval), 0,
					 SLAB_HWCACHE_ALIGN, NULL);
	return tfrc_lh_slab == NULL ? -ENOBUFS : 0;
}

void tfrc_li_exit(void)
{
	if (tfrc_lh_slab != NULL) {
		kmem_cache_destroy(tfrc_lh_slab);
		tfrc_lh_slab = NULL;
	}
}
