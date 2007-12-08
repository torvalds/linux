/*
 *  net/dccp/packet_history.c
 *
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
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/string.h>
#include <linux/slab.h>
#include "packet_history.h"
#include "../../dccp.h"

/**
 *  tfrc_tx_hist_entry  -  Simple singly-linked TX history list
 *  @next:  next oldest entry (LIFO order)
 *  @seqno: sequence number of this entry
 *  @stamp: send time of packet with sequence number @seqno
 */
struct tfrc_tx_hist_entry {
	struct tfrc_tx_hist_entry *next;
	u64			  seqno;
	ktime_t			  stamp;
};

/*
 * Transmitter History Routines
 */
static struct kmem_cache *tfrc_tx_hist_slab;

static struct tfrc_tx_hist_entry *
	tfrc_tx_hist_find_entry(struct tfrc_tx_hist_entry *head, u64 seqno)
{
	while (head != NULL && head->seqno != seqno)
		head = head->next;

	return head;
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
EXPORT_SYMBOL_GPL(tfrc_tx_hist_add);

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
EXPORT_SYMBOL_GPL(tfrc_tx_hist_purge);

u32 tfrc_tx_hist_rtt(struct tfrc_tx_hist_entry *head, const u64 seqno,
		     const ktime_t now)
{
	u32 rtt = 0;
	struct tfrc_tx_hist_entry *packet = tfrc_tx_hist_find_entry(head, seqno);

	if (packet != NULL) {
		rtt = ktime_us_delta(now, packet->stamp);
		/*
		 * Garbage-collect older (irrelevant) entries:
		 */
		tfrc_tx_hist_purge(&packet->next);
	}

	return rtt;
}
EXPORT_SYMBOL_GPL(tfrc_tx_hist_rtt);


/*
 * 	Receiver History Routines
 */
static struct kmem_cache *tfrc_rx_hist_slab;

/**
 * tfrc_rx_hist_index - index to reach n-th entry after loss_start
 */
static inline u8 tfrc_rx_hist_index(const struct tfrc_rx_hist *h, const u8 n)
{
	return (h->loss_start + n) & TFRC_NDUPACK;
}

/**
 * tfrc_rx_hist_last_rcv - entry with highest-received-seqno so far
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_last_rcv(const struct tfrc_rx_hist *h)
{
	return h->ring[tfrc_rx_hist_index(h, h->loss_count)];
}

void tfrc_rx_hist_add_packet(struct tfrc_rx_hist *h,
			     const struct sk_buff *skb,
			     const u32 ndp)
{
	struct tfrc_rx_hist_entry *entry = tfrc_rx_hist_last_rcv(h);
	const struct dccp_hdr *dh = dccp_hdr(skb);

	entry->tfrchrx_seqno = DCCP_SKB_CB(skb)->dccpd_seq;
	entry->tfrchrx_ccval = dh->dccph_ccval;
	entry->tfrchrx_type  = dh->dccph_type;
	entry->tfrchrx_ndp   = ndp;
	entry->tfrchrx_tstamp = ktime_get_real();
}
EXPORT_SYMBOL_GPL(tfrc_rx_hist_add_packet);

/**
 * tfrc_rx_hist_entry - return the n-th history entry after loss_start
 */
static inline struct tfrc_rx_hist_entry *
		tfrc_rx_hist_entry(const struct tfrc_rx_hist *h, const u8 n)
{
	return h->ring[tfrc_rx_hist_index(h, n)];
}

/**
 * tfrc_rx_hist_loss_prev - entry with highest-received-seqno before loss was detected
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_loss_prev(const struct tfrc_rx_hist *h)
{
	return h->ring[h->loss_start];
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
EXPORT_SYMBOL_GPL(tfrc_rx_hist_duplicate);

/* initialise loss detection and disable RTT sampling */
static inline void tfrc_rx_hist_loss_indicated(struct tfrc_rx_hist *h)
{
	h->loss_count = 1;
}

/* indicate whether previously a packet was detected missing */
static inline int tfrc_rx_hist_loss_pending(const struct tfrc_rx_hist *h)
{
	return h->loss_count;
}

/* any data packets missing between last reception and skb ? */
int tfrc_rx_hist_new_loss_indicated(struct tfrc_rx_hist *h,
				    const struct sk_buff *skb, u32 ndp)
{
	int delta = dccp_delta_seqno(tfrc_rx_hist_last_rcv(h)->tfrchrx_seqno,
				     DCCP_SKB_CB(skb)->dccpd_seq);

	if (delta > 1 && ndp < delta)
		tfrc_rx_hist_loss_indicated(h);

	return tfrc_rx_hist_loss_pending(h);
}
EXPORT_SYMBOL_GPL(tfrc_rx_hist_new_loss_indicated);

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
EXPORT_SYMBOL_GPL(tfrc_rx_hist_alloc);

void tfrc_rx_hist_purge(struct tfrc_rx_hist *h)
{
	int i;

	for (i = 0; i <= TFRC_NDUPACK; ++i)
		if (h->ring[i] != NULL) {
			kmem_cache_free(tfrc_rx_hist_slab, h->ring[i]);
			h->ring[i] = NULL;
		}
}
EXPORT_SYMBOL_GPL(tfrc_rx_hist_purge);

/**
 * tfrc_rx_hist_rtt_last_s - reference entry to compute RTT samples against
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_rtt_last_s(const struct tfrc_rx_hist *h)
{
	return h->ring[0];
}

/**
 * tfrc_rx_hist_rtt_prev_s: previously suitable (wrt rtt_last_s) RTT-sampling entry
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
EXPORT_SYMBOL_GPL(tfrc_rx_hist_sample_rtt);

__init int packet_history_init(void)
{
	tfrc_tx_hist_slab = kmem_cache_create("tfrc_tx_hist",
					      sizeof(struct tfrc_tx_hist_entry), 0,
					      SLAB_HWCACHE_ALIGN, NULL);
	if (tfrc_tx_hist_slab == NULL)
		goto out_err;

	tfrc_rx_hist_slab = kmem_cache_create("tfrc_rx_hist",
					      sizeof(struct tfrc_rx_hist_entry), 0,
					      SLAB_HWCACHE_ALIGN, NULL);
	if (tfrc_rx_hist_slab == NULL)
		goto out_free_tx;

	return 0;

out_free_tx:
	kmem_cache_destroy(tfrc_tx_hist_slab);
	tfrc_tx_hist_slab = NULL;
out_err:
	return -ENOBUFS;
}

void packet_history_exit(void)
{
	if (tfrc_tx_hist_slab != NULL) {
		kmem_cache_destroy(tfrc_tx_hist_slab);
		tfrc_tx_hist_slab = NULL;
	}

	if (tfrc_rx_hist_slab != NULL) {
		kmem_cache_destroy(tfrc_rx_hist_slab);
		tfrc_rx_hist_slab = NULL;
	}
}
