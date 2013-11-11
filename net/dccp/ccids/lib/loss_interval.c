/*
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

static struct kmem_cache  *tfrc_lh_slab  __read_mostly;
/* Loss Interval weights from [RFC 3448, 5.4], scaled by 10 */
static const int tfrc_lh_weights[NINTERVAL] = { 10, 10, 10, 10, 8, 6, 4, 2 };

/* implements LIFO semantics on the array */
static inline u8 LIH_INDEX(const u8 ctr)
{
	return LIH_SIZE - 1 - (ctr % LIH_SIZE);
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

static void tfrc_lh_calc_i_mean(struct tfrc_loss_hist *lh)
{
	u32 i_i, i_tot0 = 0, i_tot1 = 0, w_tot = 0;
	int i, k = tfrc_lh_length(lh) - 1; /* k is as in rfc3448bis, 5.4 */

	if (k <= 0)
		return;

	for (i = 0; i <= k; i++) {
		i_i = tfrc_lh_get_interval(lh, i);

		if (i < k) {
			i_tot0 += i_i * tfrc_lh_weights[i];
			w_tot  += tfrc_lh_weights[i];
		}
		if (i > 0)
			i_tot1 += i_i * tfrc_lh_weights[i-1];
	}

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
	s64 len;

	if (cur == NULL)			/* not initialised */
		return 0;

	len = dccp_delta_seqno(cur->li_seqno, DCCP_SKB_CB(skb)->dccpd_seq) + 1;

	if (len - (s64)cur->li_length <= 0)	/* duplicate or reordered */
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

	cur->li_length = len;
	tfrc_lh_calc_i_mean(lh);

	return lh->i_mean < old_i_mean;
}

/* Determine if `new_loss' does begin a new loss interval [RFC 4342, 10.2] */
static inline u8 tfrc_lh_is_new_loss(struct tfrc_loss_interval *cur,
				     struct tfrc_rx_hist_entry *new_loss)
{
	return	dccp_delta_seqno(cur->li_seqno, new_loss->tfrchrx_seqno) > 0 &&
		(cur->li_is_closed || SUB16(new_loss->tfrchrx_ccval, cur->li_ccval) > 4);
}

/**
 * tfrc_lh_interval_add  -  Insert new record into the Loss Interval database
 * @lh:		   Loss Interval database
 * @rh:		   Receive history containing a fresh loss event
 * @calc_first_li: Caller-dependent routine to compute length of first interval
 * @sk:		   Used by @calc_first_li in caller-specific way (subtyping)
 *
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
				  tfrc_rx_hist_last_rcv(rh)->tfrchrx_seqno) + 1;
		if (lh->counter > (2*LIH_SIZE))
			lh->counter -= LIH_SIZE;

		tfrc_lh_calc_i_mean(lh);
	}
	return 1;
}

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
