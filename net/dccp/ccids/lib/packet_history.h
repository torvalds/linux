/*
 *  Packet RX/TX history data structures and routines for TFRC-based protocols.
 *
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005-6 The University of Waikato, Hamilton, New Zealand.
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

#ifndef _DCCP_PKT_HIST_
#define _DCCP_PKT_HIST_

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "../../dccp.h"

/* Number of later packets received before one is considered lost */
#define TFRC_RECV_NUM_LATE_LOSS	 3

#define TFRC_WIN_COUNT_PER_RTT	 4
#define TFRC_WIN_COUNT_LIMIT	16

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

extern int  tfrc_tx_hist_add(struct tfrc_tx_hist_entry **headp, u64 seqno);
extern void tfrc_tx_hist_purge(struct tfrc_tx_hist_entry **headp);

extern struct tfrc_tx_hist_entry *
	tfrc_tx_hist_find_entry(struct tfrc_tx_hist_entry *head, u64 ackno);

/*
 * 	Receiver History data structures and declarations
 */
struct dccp_rx_hist_entry {
	struct list_head dccphrx_node;
	u64		 dccphrx_seqno:48,
			 dccphrx_ccval:4,
			 dccphrx_type:4;
	u32		 dccphrx_ndp; /* In fact it is from 8 to 24 bits */
	ktime_t		 dccphrx_tstamp;
};

struct dccp_rx_hist {
	struct kmem_cache *dccprxh_slab;
};

extern struct dccp_rx_hist *dccp_rx_hist_new(const char *name);
extern void 		dccp_rx_hist_delete(struct dccp_rx_hist *hist);

static inline struct dccp_rx_hist_entry *
			dccp_rx_hist_entry_new(struct dccp_rx_hist *hist,
					       const u32 ndp,
					       const struct sk_buff *skb,
					       const gfp_t prio)
{
	struct dccp_rx_hist_entry *entry = kmem_cache_alloc(hist->dccprxh_slab,
							    prio);

	if (entry != NULL) {
		const struct dccp_hdr *dh = dccp_hdr(skb);

		entry->dccphrx_seqno = DCCP_SKB_CB(skb)->dccpd_seq;
		entry->dccphrx_ccval = dh->dccph_ccval;
		entry->dccphrx_type  = dh->dccph_type;
		entry->dccphrx_ndp   = ndp;
		entry->dccphrx_tstamp = ktime_get_real();
	}

	return entry;
}

static inline struct dccp_rx_hist_entry *
			dccp_rx_hist_head(struct list_head *list)
{
	struct dccp_rx_hist_entry *head = NULL;

	if (!list_empty(list))
		head = list_entry(list->next, struct dccp_rx_hist_entry,
				  dccphrx_node);
	return head;
}

extern int dccp_rx_hist_find_entry(const struct list_head *list, const u64 seq,
				   u8 *ccval);
extern struct dccp_rx_hist_entry *
		dccp_rx_hist_find_data_packet(const struct list_head *list);

extern void dccp_rx_hist_add_packet(struct dccp_rx_hist *hist,
				    struct list_head *rx_list,
				    struct list_head *li_list,
				    struct dccp_rx_hist_entry *packet,
				    u64 nonloss_seqno);

static inline void dccp_rx_hist_entry_delete(struct dccp_rx_hist *hist,
					     struct dccp_rx_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(hist->dccprxh_slab, entry);
}

extern void dccp_rx_hist_purge(struct dccp_rx_hist *hist,
			       struct list_head *list);

static inline int
	dccp_rx_hist_entry_data_packet(const struct dccp_rx_hist_entry *entry)
{
	return entry->dccphrx_type == DCCP_PKT_DATA ||
	       entry->dccphrx_type == DCCP_PKT_DATAACK;
}

extern u64 dccp_rx_hist_detect_loss(struct list_head *rx_list,
				    struct list_head *li_list, u8 *win_loss);

#endif /* _DCCP_PKT_HIST_ */
