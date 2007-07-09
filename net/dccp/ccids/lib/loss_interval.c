/*
 *  net/dccp/ccids/lib/loss_interval.c
 *
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-7 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/module.h>
#include <net/sock.h>
#include "../../dccp.h"
#include "loss_interval.h"
#include "packet_history.h"
#include "tfrc.h"

#define DCCP_LI_HIST_IVAL_F_LENGTH  8

struct dccp_li_hist_entry {
	struct list_head dccplih_node;
	u64		 dccplih_seqno:48,
			 dccplih_win_count:4;
	u32		 dccplih_interval;
};

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
				 struct timeval *last_feedback,
				 u16 s, u32 bytes_recv,
				 u32 previous_x_recv)
{
	struct dccp_rx_hist_entry *entry, *next, *tail = NULL;
	u32 x_recv, p;
	suseconds_t rtt, delta;
	struct timeval tstamp = { 0, 0 };
	int interval = 0;
	int win_count = 0;
	int step = 0;
	u64 fval;

	list_for_each_entry_safe(entry, next, hist_list, dccphrx_node) {
		if (dccp_rx_hist_entry_data_packet(entry)) {
			tail = entry;

			switch (step) {
			case 0:
				tstamp	  = entry->dccphrx_tstamp;
				win_count = entry->dccphrx_ccval;
				step = 1;
				break;
			case 1:
				interval = win_count - entry->dccphrx_ccval;
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
		DCCP_WARN("%s(%p), Could not find a win_count interval > 0."
			  "Defaulting to 1\n", dccp_role(sk), sk);
		interval = 1;
	}
found:
	if (!tail) {
		DCCP_CRIT("tail is null\n");
		return ~0;
	}

	delta = timeval_delta(&tstamp, &tail->dccphrx_tstamp);
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

	dccp_timestamp(sk, &tstamp);
	delta = timeval_delta(&tstamp, last_feedback);
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

	if (p == 0)
		return ~0;
	else
		return 1000000 / p;
}

void dccp_li_update_li(struct sock *sk,
		       struct list_head *li_hist_list,
		       struct list_head *hist_list,
		       struct timeval *last_feedback, u16 s, u32 bytes_recv,
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

static __init int dccp_li_init(void)
{
	dccp_li_cachep = kmem_cache_create("dccp_li_hist",
					   sizeof(struct dccp_li_hist_entry),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	return dccp_li_cachep == NULL ? -ENOBUFS : 0;
}

static __exit void dccp_li_exit(void)
{
	kmem_cache_destroy(dccp_li_cachep);
}

module_init(dccp_li_init);
module_exit(dccp_li_exit);
