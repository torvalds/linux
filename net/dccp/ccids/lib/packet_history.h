/*
 *  net/dccp/packet_history.h
 *
 *  Copyright (c) 2005-6 The University of Waikato, Hamilton, New Zealand.
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

#ifndef _DCCP_PKT_HIST_
#define _DCCP_PKT_HIST_

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/time.h>

#include "../../dccp.h"

/* Number of later packets received before one is considered lost */
#define TFRC_RECV_NUM_LATE_LOSS	 3

#define TFRC_WIN_COUNT_PER_RTT	 4
#define TFRC_WIN_COUNT_LIMIT	16

/*
 * 	Transmitter History data structures and declarations
 */
struct dccp_tx_hist_entry {
	struct list_head dccphtx_node;
	u64		 dccphtx_seqno:48,
			 dccphtx_sent:1;
	u32		 dccphtx_rtt;
	struct timeval	 dccphtx_tstamp;
};

struct dccp_tx_hist {
	struct kmem_cache *dccptxh_slab;
};

extern struct dccp_tx_hist *dccp_tx_hist_new(const char *name);
extern void 		    dccp_tx_hist_delete(struct dccp_tx_hist *hist);

static inline struct dccp_tx_hist_entry *
			dccp_tx_hist_entry_new(struct dccp_tx_hist *hist,
					       const gfp_t prio)
{
	struct dccp_tx_hist_entry *entry = kmem_cache_alloc(hist->dccptxh_slab,
							    prio);

	if (entry != NULL)
		entry->dccphtx_sent = 0;

	return entry;
}

static inline struct dccp_tx_hist_entry *
			dccp_tx_hist_head(struct list_head *list)
{
	struct dccp_tx_hist_entry *head = NULL;

	if (!list_empty(list))
		head = list_entry(list->next, struct dccp_tx_hist_entry,
				  dccphtx_node);
	return head;
}

extern struct dccp_tx_hist_entry *
			dccp_tx_hist_find_entry(const struct list_head *list,
						const u64 seq);

static inline void dccp_tx_hist_add_entry(struct list_head *list,
					  struct dccp_tx_hist_entry *entry)
{
	list_add(&entry->dccphtx_node, list);
}

static inline void dccp_tx_hist_entry_delete(struct dccp_tx_hist *hist,
					     struct dccp_tx_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(hist->dccptxh_slab, entry);
}

extern void dccp_tx_hist_purge(struct dccp_tx_hist *hist,
			       struct list_head *list);

extern void dccp_tx_hist_purge_older(struct dccp_tx_hist *hist,
				     struct list_head *list,
				     struct dccp_tx_hist_entry *next);

/*
 * 	Receiver History data structures and declarations
 */
struct dccp_rx_hist_entry {
	struct list_head dccphrx_node;
	u64		 dccphrx_seqno:48,
			 dccphrx_ccval:4,
			 dccphrx_type:4;
	u32		 dccphrx_ndp; /* In fact it is from 8 to 24 bits */
	struct timeval	 dccphrx_tstamp;
};

struct dccp_rx_hist {
	struct kmem_cache *dccprxh_slab;
};

extern struct dccp_rx_hist *dccp_rx_hist_new(const char *name);
extern void 		dccp_rx_hist_delete(struct dccp_rx_hist *hist);

static inline struct dccp_rx_hist_entry *
			dccp_rx_hist_entry_new(struct dccp_rx_hist *hist,
					       const struct sock *sk,
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
		dccp_timestamp(sk, &entry->dccphrx_tstamp);
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
