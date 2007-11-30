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

#include <linux/module.h>
#include <linux/string.h>
#include "packet_history.h"

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
static struct kmem_cache *tfrc_tx_hist;

static struct tfrc_tx_hist_entry *
	tfrc_tx_hist_find_entry(struct tfrc_tx_hist_entry *head, u64 seqno)
{
	while (head != NULL && head->seqno != seqno)
		head = head->next;

	return head;
}

int tfrc_tx_hist_add(struct tfrc_tx_hist_entry **headp, u64 seqno)
{
	struct tfrc_tx_hist_entry *entry = kmem_cache_alloc(tfrc_tx_hist, gfp_any());

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

		kmem_cache_free(tfrc_tx_hist, head);
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
struct dccp_rx_hist *dccp_rx_hist_new(const char *name)
{
	struct dccp_rx_hist *hist = kmalloc(sizeof(*hist), GFP_ATOMIC);
	static const char dccp_rx_hist_mask[] = "rx_hist_%s";
	char *slab_name;

	if (hist == NULL)
		goto out;

	slab_name = kmalloc(strlen(name) + sizeof(dccp_rx_hist_mask) - 1,
			    GFP_ATOMIC);
	if (slab_name == NULL)
		goto out_free_hist;

	sprintf(slab_name, dccp_rx_hist_mask, name);
	hist->dccprxh_slab = kmem_cache_create(slab_name,
					     sizeof(struct dccp_rx_hist_entry),
					     0, SLAB_HWCACHE_ALIGN, NULL);
	if (hist->dccprxh_slab == NULL)
		goto out_free_slab_name;
out:
	return hist;
out_free_slab_name:
	kfree(slab_name);
out_free_hist:
	kfree(hist);
	hist = NULL;
	goto out;
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_new);

void dccp_rx_hist_delete(struct dccp_rx_hist *hist)
{
	const char* name = kmem_cache_name(hist->dccprxh_slab);

	kmem_cache_destroy(hist->dccprxh_slab);
	kfree(name);
	kfree(hist);
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_delete);

int dccp_rx_hist_find_entry(const struct list_head *list, const u64 seq,
			    u8 *ccval)
{
	struct dccp_rx_hist_entry *packet = NULL, *entry;

	list_for_each_entry(entry, list, dccphrx_node)
		if (entry->dccphrx_seqno == seq) {
			packet = entry;
			break;
		}

	if (packet)
		*ccval = packet->dccphrx_ccval;

	return packet != NULL;
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_find_entry);
struct dccp_rx_hist_entry *
		dccp_rx_hist_find_data_packet(const struct list_head *list)
{
	struct dccp_rx_hist_entry *entry, *packet = NULL;

	list_for_each_entry(entry, list, dccphrx_node)
		if (entry->dccphrx_type == DCCP_PKT_DATA ||
		    entry->dccphrx_type == DCCP_PKT_DATAACK) {
			packet = entry;
			break;
		}

	return packet;
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_find_data_packet);

void dccp_rx_hist_add_packet(struct dccp_rx_hist *hist,
			    struct list_head *rx_list,
			    struct list_head *li_list,
			    struct dccp_rx_hist_entry *packet,
			    u64 nonloss_seqno)
{
	struct dccp_rx_hist_entry *entry, *next;
	u8 num_later = 0;

	list_add(&packet->dccphrx_node, rx_list);

	num_later = TFRC_RECV_NUM_LATE_LOSS + 1;

	if (!list_empty(li_list)) {
		list_for_each_entry_safe(entry, next, rx_list, dccphrx_node) {
			if (num_later == 0) {
				if (after48(nonloss_seqno,
				   entry->dccphrx_seqno)) {
					list_del_init(&entry->dccphrx_node);
					dccp_rx_hist_entry_delete(hist, entry);
				}
			} else if (dccp_rx_hist_entry_data_packet(entry))
				--num_later;
		}
	} else {
		int step = 0;
		u8 win_count = 0; /* Not needed, but lets shut up gcc */
		int tmp;
		/*
		 * We have no loss interval history so we need at least one
		 * rtt:s of data packets to approximate rtt.
		 */
		list_for_each_entry_safe(entry, next, rx_list, dccphrx_node) {
			if (num_later == 0) {
				switch (step) {
				case 0:
					step = 1;
					/* OK, find next data packet */
					num_later = 1;
					break;
				case 1:
					step = 2;
					/* OK, find next data packet */
					num_later = 1;
					win_count = entry->dccphrx_ccval;
					break;
				case 2:
					tmp = win_count - entry->dccphrx_ccval;
					if (tmp < 0)
						tmp += TFRC_WIN_COUNT_LIMIT;
					if (tmp > TFRC_WIN_COUNT_PER_RTT + 1) {
						/*
						 * We have found a packet older
						 * than one rtt remove the rest
						 */
						step = 3;
					} else /* OK, find next data packet */
						num_later = 1;
					break;
				case 3:
					list_del_init(&entry->dccphrx_node);
					dccp_rx_hist_entry_delete(hist, entry);
					break;
				}
			} else if (dccp_rx_hist_entry_data_packet(entry))
				--num_later;
		}
	}
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_add_packet);

void dccp_rx_hist_purge(struct dccp_rx_hist *hist, struct list_head *list)
{
	struct dccp_rx_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, list, dccphrx_node) {
		list_del_init(&entry->dccphrx_node);
		kmem_cache_free(hist->dccprxh_slab, entry);
	}
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_purge);

extern int __init dccp_li_init(void);
extern void dccp_li_exit(void);

static __init int packet_history_init(void)
{
	if (dccp_li_init() != 0)
		goto out;

	tfrc_tx_hist = kmem_cache_create("tfrc_tx_hist",
					 sizeof(struct tfrc_tx_hist_entry), 0,
					 SLAB_HWCACHE_ALIGN, NULL);
	if (tfrc_tx_hist == NULL)
		goto out_li_exit;

	return 0;
out_li_exit:
	dccp_li_exit();
out:
	return -ENOBUFS;
}
module_init(packet_history_init);

static __exit void packet_history_exit(void)
{
	if (tfrc_tx_hist != NULL) {
		kmem_cache_destroy(tfrc_tx_hist);
		tfrc_tx_hist = NULL;
	}
	dccp_li_exit();
}
module_exit(packet_history_exit);

MODULE_AUTHOR("Ian McDonald <ian.mcdonald@jandi.co.nz>, "
	      "Arnaldo Carvalho de Melo <acme@ghostprotocols.net>");
MODULE_DESCRIPTION("DCCP TFRC library");
MODULE_LICENSE("GPL");
