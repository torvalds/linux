// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *
 *  An implementation of the DCCP protocol
 *
 *  This code has been developed by the University of Waikato WAND
 *  research group. For further information please see http://www.wand.net.nz/
 *  or e-mail Ian McDonald - ian.mcdonald@jandi.co.nz
 *
 *  This code also uses code from Lulea University, rereleased as GPL by its
 *  authors:
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  Changes to meet Linux coding standards, to make it meet latest ccid3 draft
 *  and to make it work as a loadable module in the DCCP stack written by
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>.
 *
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/string.h>
#include <linux/slab.h>
#include "packet_history.h"
#include "../../dccp.h"

/*
 * Transmitter History Routines
 */
static struct kmem_cache *tfrc_tx_hist_slab;

int __init tfrc_tx_packet_history_init(void)
{
	tfrc_tx_hist_slab = kmem_cache_create("tfrc_tx_hist",
					      sizeof(struct tfrc_tx_hist_entry),
					      0, SLAB_HWCACHE_ALIGN, NULL);
	return tfrc_tx_hist_slab == NULL ? -ENOBUFS : 0;
}

void tfrc_tx_packet_history_exit(void)
{
	if (tfrc_tx_hist_slab != NULL) {
		kmem_cache_destroy(tfrc_tx_hist_slab);
		tfrc_tx_hist_slab = NULL;
	}
}

int tfrc_tx_hist_add(struct tfrc_tx_hist_entry **headp, u64 seqno)
{
	struct tfrc_tx_hist_entry *entry = kmem_cache_alloc(tfrc_tx_hist_slab, gfp_any());

	if (entry == NULL)
		return -ENOBUFS;
	entry->seqno = seqno;
	entry->stamp = ktime_get_real();
	entry->next  = *headp;
	*headp	     = entry;
	return 0;
}

void tfrc_tx_hist_purge(struct tfrc_tx_hist_entry **headp)
{
	struct tfrc_tx_hist_entry *head = *headp;

	while (head != NULL) {
		struct tfrc_tx_hist_entry *next = head->next;

		kmem_cache_free(tfrc_tx_hist_slab, head);
		head = next;
	}

	*headp = NULL;
}

/*
 *	Receiver History Routines
 */
static struct kmem_cache *tfrc_rx_hist_slab;

int __init tfrc_rx_packet_history_init(void)
{
	tfrc_rx_hist_slab = kmem_cache_create("tfrc_rxh_cache",
					      sizeof(struct tfrc_rx_hist_entry),
					      0, SLAB_HWCACHE_ALIGN, NULL);
	return tfrc_rx_hist_slab == NULL ? -ENOBUFS : 0;
}

void tfrc_rx_packet_history_exit(void)
{
	if (tfrc_rx_hist_slab != NULL) {
		kmem_cache_destroy(tfrc_rx_hist_slab);
		tfrc_rx_hist_slab = NULL;
	}
}

static inline void tfrc_rx_hist_entry_from_skb(struct tfrc_rx_hist_entry *entry,
					       const struct sk_buff *skb,
					       const u64 ndp)
{
	const struct dccp_hdr *dh = dccp_hdr(skb);

	entry->tfrchrx_seqno = DCCP_SKB_CB(skb)->dccpd_seq;
	entry->tfrchrx_ccval = dh->dccph_ccval;
	entry->tfrchrx_type  = dh->dccph_type;
	entry->tfrchrx_ndp   = ndp;
	entry->tfrchrx_tstamp = ktime_get_real();
}

void tfrc_rx_hist_add_packet(struct tfrc_rx_hist *h,
			     const struct sk_buff *skb,
			     const u64 ndp)
{
	struct tfrc_rx_hist_entry *entry = tfrc_rx_hist_last_rcv(h);

	tfrc_rx_hist_entry_from_skb(entry, skb, ndp);
}

/* has the packet contained in skb been seen before? */
int tfrc_rx_hist_duplicate(struct tfrc_rx_hist *h, struct sk_buff *skb)
{
	const u64 seq = DCCP_SKB_CB(skb)->dccpd_seq;
	int i;

	if (dccp_delta_seqno(tfrc_rx_hist_loss_prev(h)->tfrchrx_seqno, seq) <= 0)
		return 1;

	for (i = 1; i <= h->loss_count; i++)
		if (tfrc_rx_hist_entry(h, i)->tfrchrx_seqno == seq)
			return 1;

	return 0;
}

static void tfrc_rx_hist_swap(struct tfrc_rx_hist *h, const u8 a, const u8 b)
{
	const u8 idx_a = tfrc_rx_hist_index(h, a),
		 idx_b = tfrc_rx_hist_index(h, b);

	swap(h->ring[idx_a], h->ring[idx_b]);
}

/*
 * Private helper functions for loss detection.
 *
 * In the descriptions, `Si' refers to the sequence number of entry number i,
 * whose NDP count is `Ni' (lower case is used for variables).
 * Note: All __xxx_loss functions expect that a test against duplicates has been
 *       performed already: the seqno of the skb must not be less than the seqno
 *       of loss_prev; and it must not equal that of any valid history entry.
 */
static void __do_track_loss(struct tfrc_rx_hist *h, struct sk_buff *skb, u64 n1)
{
	u64 s0 = tfrc_rx_hist_loss_prev(h)->tfrchrx_seqno,
	    s1 = DCCP_SKB_CB(skb)->dccpd_seq;

	if (!dccp_loss_free(s0, s1, n1)) {	/* gap between S0 and S1 */
		h->loss_count = 1;
		tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_entry(h, 1), skb, n1);
	}
}

static void __one_after_loss(struct tfrc_rx_hist *h, struct sk_buff *skb, u32 n2)
{
	u64 s0 = tfrc_rx_hist_loss_prev(h)->tfrchrx_seqno,
	    s1 = tfrc_rx_hist_entry(h, 1)->tfrchrx_seqno,
	    s2 = DCCP_SKB_CB(skb)->dccpd_seq;

	if (likely(dccp_delta_seqno(s1, s2) > 0)) {	/* S1  <  S2 */
		h->loss_count = 2;
		tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_entry(h, 2), skb, n2);
		return;
	}

	/* S0  <  S2  <  S1 */

	if (dccp_loss_free(s0, s2, n2)) {
		u64 n1 = tfrc_rx_hist_entry(h, 1)->tfrchrx_ndp;

		if (dccp_loss_free(s2, s1, n1)) {
			/* hole is filled: S0, S2, and S1 are consecutive */
			h->loss_count = 0;
			h->loss_start = tfrc_rx_hist_index(h, 1);
		} else
			/* gap between S2 and S1: just update loss_prev */
			tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_loss_prev(h), skb, n2);

	} else {	/* gap between S0 and S2 */
		/*
		 * Reorder history to insert S2 between S0 and S1
		 */
		tfrc_rx_hist_swap(h, 0, 3);
		h->loss_start = tfrc_rx_hist_index(h, 3);
		tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_entry(h, 1), skb, n2);
		h->loss_count = 2;
	}
}

/* return 1 if a new loss event has been identified */
static int __two_after_loss(struct tfrc_rx_hist *h, struct sk_buff *skb, u32 n3)
{
	u64 s0 = tfrc_rx_hist_loss_prev(h)->tfrchrx_seqno,
	    s1 = tfrc_rx_hist_entry(h, 1)->tfrchrx_seqno,
	    s2 = tfrc_rx_hist_entry(h, 2)->tfrchrx_seqno,
	    s3 = DCCP_SKB_CB(skb)->dccpd_seq;

	if (likely(dccp_delta_seqno(s2, s3) > 0)) {	/* S2  <  S3 */
		h->loss_count = 3;
		tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_entry(h, 3), skb, n3);
		return 1;
	}

	/* S3  <  S2 */

	if (dccp_delta_seqno(s1, s3) > 0) {		/* S1  <  S3  <  S2 */
		/*
		 * Reorder history to insert S3 between S1 and S2
		 */
		tfrc_rx_hist_swap(h, 2, 3);
		tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_entry(h, 2), skb, n3);
		h->loss_count = 3;
		return 1;
	}

	/* S0  <  S3  <  S1 */

	if (dccp_loss_free(s0, s3, n3)) {
		u64 n1 = tfrc_rx_hist_entry(h, 1)->tfrchrx_ndp;

		if (dccp_loss_free(s3, s1, n1)) {
			/* hole between S0 and S1 filled by S3 */
			u64 n2 = tfrc_rx_hist_entry(h, 2)->tfrchrx_ndp;

			if (dccp_loss_free(s1, s2, n2)) {
				/* entire hole filled by S0, S3, S1, S2 */
				h->loss_start = tfrc_rx_hist_index(h, 2);
				h->loss_count = 0;
			} else {
				/* gap remains between S1 and S2 */
				h->loss_start = tfrc_rx_hist_index(h, 1);
				h->loss_count = 1;
			}

		} else /* gap exists between S3 and S1, loss_count stays at 2 */
			tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_loss_prev(h), skb, n3);

		return 0;
	}

	/*
	 * The remaining case:  S0  <  S3  <  S1  <  S2;  gap between S0 and S3
	 * Reorder history to insert S3 between S0 and S1.
	 */
	tfrc_rx_hist_swap(h, 0, 3);
	h->loss_start = tfrc_rx_hist_index(h, 3);
	tfrc_rx_hist_entry_from_skb(tfrc_rx_hist_entry(h, 1), skb, n3);
	h->loss_count = 3;

	return 1;
}

/* recycle RX history records to continue loss detection if necessary */
static void __three_after_loss(struct tfrc_rx_hist *h)
{
	/*
	 * At this stage we know already that there is a gap between S0 and S1
	 * (since S0 was the highest sequence number received before detecting
	 * the loss). To recycle the loss record, it is	thus only necessary to
	 * check for other possible gaps between S1/S2 and between S2/S3.
	 */
	u64 s1 = tfrc_rx_hist_entry(h, 1)->tfrchrx_seqno,
	    s2 = tfrc_rx_hist_entry(h, 2)->tfrchrx_seqno,
	    s3 = tfrc_rx_hist_entry(h, 3)->tfrchrx_seqno;
	u64 n2 = tfrc_rx_hist_entry(h, 2)->tfrchrx_ndp,
	    n3 = tfrc_rx_hist_entry(h, 3)->tfrchrx_ndp;

	if (dccp_loss_free(s1, s2, n2)) {

		if (dccp_loss_free(s2, s3, n3)) {
			/* no gap between S2 and S3: entire hole is filled */
			h->loss_start = tfrc_rx_hist_index(h, 3);
			h->loss_count = 0;
		} else {
			/* gap between S2 and S3 */
			h->loss_start = tfrc_rx_hist_index(h, 2);
			h->loss_count = 1;
		}

	} else {	/* gap between S1 and S2 */
		h->loss_start = tfrc_rx_hist_index(h, 1);
		h->loss_count = 2;
	}
}

/**
 *  tfrc_rx_handle_loss  -  Loss detection and further processing
 *  @h:		    The non-empty RX history object
 *  @lh:	    Loss Intervals database to update
 *  @skb:	    Currently received packet
 *  @ndp:	    The NDP count belonging to @skb
 *  @calc_first_li: Caller-dependent computation of first loss interval in @lh
 *  @sk:	    Used by @calc_first_li (see tfrc_lh_interval_add)
 *
 *  Chooses action according to pending loss, updates LI database when a new
 *  loss was detected, and does required post-processing. Returns 1 when caller
 *  should send feedback, 0 otherwise.
 *  Since it also takes care of reordering during loss detection and updates the
 *  records accordingly, the caller should not perform any more RX history
 *  operations when loss_count is greater than 0 after calling this function.
 */
int tfrc_rx_handle_loss(struct tfrc_rx_hist *h,
			struct tfrc_loss_hist *lh,
			struct sk_buff *skb, const u64 ndp,
			u32 (*calc_first_li)(struct sock *), struct sock *sk)
{
	int is_new_loss = 0;

	if (h->loss_count == 0) {
		__do_track_loss(h, skb, ndp);
	} else if (h->loss_count == 1) {
		__one_after_loss(h, skb, ndp);
	} else if (h->loss_count != 2) {
		DCCP_BUG("invalid loss_count %d", h->loss_count);
	} else if (__two_after_loss(h, skb, ndp)) {
		/*
		 * Update Loss Interval database and recycle RX records
		 */
		is_new_loss = tfrc_lh_interval_add(lh, h, calc_first_li, sk);
		__three_after_loss(h);
	}
	return is_new_loss;
}

int tfrc_rx_hist_alloc(struct tfrc_rx_hist *h)
{
	int i;

	for (i = 0; i <= TFRC_NDUPACK; i++) {
		h->ring[i] = kmem_cache_alloc(tfrc_rx_hist_slab, GFP_ATOMIC);
		if (h->ring[i] == NULL)
			goto out_free;
	}

	h->loss_count = h->loss_start = 0;
	return 0;

out_free:
	while (i-- != 0) {
		kmem_cache_free(tfrc_rx_hist_slab, h->ring[i]);
		h->ring[i] = NULL;
	}
	return -ENOBUFS;
}

void tfrc_rx_hist_purge(struct tfrc_rx_hist *h)
{
	int i;

	for (i = 0; i <= TFRC_NDUPACK; ++i)
		if (h->ring[i] != NULL) {
			kmem_cache_free(tfrc_rx_hist_slab, h->ring[i]);
			h->ring[i] = NULL;
		}
}

/**
 * tfrc_rx_hist_rtt_last_s - reference entry to compute RTT samples against
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_rtt_last_s(const struct tfrc_rx_hist *h)
{
	return h->ring[0];
}

/**
 * tfrc_rx_hist_rtt_prev_s - previously suitable (wrt rtt_last_s) RTT-sampling entry
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_rtt_prev_s(const struct tfrc_rx_hist *h)
{
	return h->ring[h->rtt_sample_prev];
}

/**
 * tfrc_rx_hist_sample_rtt  -  Sample RTT from timestamp / CCVal
 * Based on ideas presented in RFC 4342, 8.1. Returns 0 if it was not able
 * to compute a sample with given data - calling function should check this.
 */
u32 tfrc_rx_hist_sample_rtt(struct tfrc_rx_hist *h, const struct sk_buff *skb)
{
	u32 sample = 0,
	    delta_v = SUB16(dccp_hdr(skb)->dccph_ccval,
			    tfrc_rx_hist_rtt_last_s(h)->tfrchrx_ccval);

	if (delta_v < 1 || delta_v > 4) {	/* unsuitable CCVal delta */
		if (h->rtt_sample_prev == 2) {	/* previous candidate stored */
			sample = SUB16(tfrc_rx_hist_rtt_prev_s(h)->tfrchrx_ccval,
				       tfrc_rx_hist_rtt_last_s(h)->tfrchrx_ccval);
			if (sample)
				sample = 4 / sample *
				         ktime_us_delta(tfrc_rx_hist_rtt_prev_s(h)->tfrchrx_tstamp,
							tfrc_rx_hist_rtt_last_s(h)->tfrchrx_tstamp);
			else    /*
				 * FIXME: This condition is in principle not
				 * possible but occurs when CCID is used for
				 * two-way data traffic. I have tried to trace
				 * it, but the cause does not seem to be here.
				 */
				DCCP_BUG("please report to dccp@vger.kernel.org"
					 " => prev = %u, last = %u",
					 tfrc_rx_hist_rtt_prev_s(h)->tfrchrx_ccval,
					 tfrc_rx_hist_rtt_last_s(h)->tfrchrx_ccval);
		} else if (delta_v < 1) {
			h->rtt_sample_prev = 1;
			goto keep_ref_for_next_time;
		}

	} else if (delta_v == 4) /* optimal match */
		sample = ktime_to_us(net_timedelta(tfrc_rx_hist_rtt_last_s(h)->tfrchrx_tstamp));
	else {			 /* suboptimal match */
		h->rtt_sample_prev = 2;
		goto keep_ref_for_next_time;
	}

	if (unlikely(sample > DCCP_SANE_RTT_MAX)) {
		DCCP_WARN("RTT sample %u too large, using max\n", sample);
		sample = DCCP_SANE_RTT_MAX;
	}

	h->rtt_sample_prev = 0;	       /* use current entry as next reference */
keep_ref_for_next_time:

	return sample;
}
