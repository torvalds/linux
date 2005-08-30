/*
 *  net/dccp/packet_history.h
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *
 *  An implementation of the DCCP protocol
 *
 *  This code has been developed by the University of Waikato WAND
 *  research group. For further information please see http://www.wand.net.nz/
 *  or e-mail Ian McDonald - iam4@cs.waikato.ac.nz
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>

#include "packet_history.h"

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
					       0, SLAB_HWCACHE_ALIGN,
					       NULL, NULL);
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

void dccp_rx_hist_purge(struct dccp_rx_hist *hist, struct list_head *list)
{
	struct dccp_rx_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, list, dccphrx_node) {
		list_del_init(&entry->dccphrx_node);
		kmem_cache_free(hist->dccprxh_slab, entry);
	}
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_purge);

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

int dccp_rx_hist_add_packet(struct dccp_rx_hist *hist,
			    struct list_head *rx_list,
			    struct list_head *li_list,
			    struct dccp_rx_hist_entry *packet)
{
	struct dccp_rx_hist_entry *entry, *next, *iter;
	u8 num_later = 0;

	iter = dccp_rx_hist_head(rx_list);
	if (iter == NULL)
		dccp_rx_hist_add_entry(rx_list, packet);
	else {
		const u64 seqno = packet->dccphrx_seqno;

		if (after48(seqno, iter->dccphrx_seqno))
			dccp_rx_hist_add_entry(rx_list, packet);
		else {
			if (dccp_rx_hist_entry_data_packet(iter))
				num_later = 1;

			list_for_each_entry_continue(iter, rx_list,
						     dccphrx_node) {
				if (after48(seqno, iter->dccphrx_seqno)) {
					dccp_rx_hist_add_entry(&iter->dccphrx_node,
							       packet);
					goto trim_history;
				}

				if (dccp_rx_hist_entry_data_packet(iter))
					num_later++;

				if (num_later == TFRC_RECV_NUM_LATE_LOSS) {
					dccp_rx_hist_entry_delete(hist, packet);
					return 1;
				}
			}

			if (num_later < TFRC_RECV_NUM_LATE_LOSS)
				dccp_rx_hist_add_entry(rx_list, packet);
			/*
			 * FIXME: else what? should we destroy the packet
			 * like above?
			 */
		}
	}

trim_history:
	/*
	 * Trim history (remove all packets after the NUM_LATE_LOSS + 1
	 * data packets)
	 */
	num_later = TFRC_RECV_NUM_LATE_LOSS + 1;

	if (!list_empty(li_list)) {
		list_for_each_entry_safe(entry, next, rx_list, dccphrx_node) {
			if (num_later == 0) {
				list_del_init(&entry->dccphrx_node);
				dccp_rx_hist_entry_delete(hist, entry);
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

	return 0;
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_add_packet);

u64 dccp_rx_hist_detect_loss(struct list_head *rx_list,
			     struct list_head *li_list, u8 *win_loss)
{
	struct dccp_rx_hist_entry *entry, *next, *packet;
	struct dccp_rx_hist_entry *a_loss = NULL;
	struct dccp_rx_hist_entry *b_loss = NULL;
	u64 seq_loss = DCCP_MAX_SEQNO + 1;
	u8 num_later = TFRC_RECV_NUM_LATE_LOSS;

	list_for_each_entry_safe(entry, next, rx_list, dccphrx_node) {
		if (num_later == 0) {
			b_loss = entry;
			break;
		} else if (dccp_rx_hist_entry_data_packet(entry))
			--num_later;
	}

	if (b_loss == NULL)
		goto out;

	num_later = 1;
	list_for_each_entry_safe_continue(entry, next, rx_list, dccphrx_node) {
		if (num_later == 0) {
			a_loss = entry;
			break;
		} else if (dccp_rx_hist_entry_data_packet(entry))
			--num_later;
	}

	if (a_loss == NULL) {
		if (list_empty(li_list)) {
			/* no loss event have occured yet */
			LIMIT_NETDEBUG("%s: TODO: find a lost data packet by "
				       "comparing to initial seqno\n",
				       __FUNCTION__);
			goto out;
		} else {
			LIMIT_NETDEBUG("%s: Less than 4 data pkts in history!",
				       __FUNCTION__);
			goto out;
		}
	}

	/* Locate a lost data packet */
	entry = packet = b_loss;
	list_for_each_entry_safe_continue(entry, next, rx_list, dccphrx_node) {
		u64 delta = dccp_delta_seqno(entry->dccphrx_seqno,
					     packet->dccphrx_seqno);

		if (delta != 0) {
			if (dccp_rx_hist_entry_data_packet(packet))
				--delta;
			/*
			 * FIXME: check this, probably this % usage is because
			 * in earlier drafts the ndp count was just 8 bits
			 * long, but now it cam be up to 24 bits long.
			 */
#if 0
			if (delta % DCCP_NDP_LIMIT !=
			    (packet->dccphrx_ndp -
			     entry->dccphrx_ndp) % DCCP_NDP_LIMIT)
#endif
			if (delta != packet->dccphrx_ndp - entry->dccphrx_ndp) {
				seq_loss = entry->dccphrx_seqno;
				dccp_inc_seqno(&seq_loss);
			}
		}
		packet = entry;
		if (packet == a_loss)
			break;
	}
out:
	if (seq_loss != DCCP_MAX_SEQNO + 1)
		*win_loss = a_loss->dccphrx_ccval;
	else
		*win_loss = 0; /* Paranoia */

	return seq_loss;
}

EXPORT_SYMBOL_GPL(dccp_rx_hist_detect_loss);

struct dccp_tx_hist *dccp_tx_hist_new(const char *name)
{
	struct dccp_tx_hist *hist = kmalloc(sizeof(*hist), GFP_ATOMIC);
	static const char dccp_tx_hist_mask[] = "tx_hist_%s";
	char *slab_name;

	if (hist == NULL)
		goto out;

	slab_name = kmalloc(strlen(name) + sizeof(dccp_tx_hist_mask) - 1,
			    GFP_ATOMIC);
	if (slab_name == NULL)
		goto out_free_hist;

	sprintf(slab_name, dccp_tx_hist_mask, name);
	hist->dccptxh_slab = kmem_cache_create(slab_name,
					     sizeof(struct dccp_tx_hist_entry),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL, NULL);
	if (hist->dccptxh_slab == NULL)
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

EXPORT_SYMBOL_GPL(dccp_tx_hist_new);

void dccp_tx_hist_delete(struct dccp_tx_hist *hist)
{
	const char* name = kmem_cache_name(hist->dccptxh_slab);

	kmem_cache_destroy(hist->dccptxh_slab);
	kfree(name);
	kfree(hist);
}

EXPORT_SYMBOL_GPL(dccp_tx_hist_delete);

struct dccp_tx_hist_entry *
	dccp_tx_hist_find_entry(const struct list_head *list, const u64 seq)
{
	struct dccp_tx_hist_entry *packet = NULL, *entry;

	list_for_each_entry(entry, list, dccphtx_node)
		if (entry->dccphtx_seqno == seq) {
			packet = entry;
			break;
		}

	return packet;
}

EXPORT_SYMBOL_GPL(dccp_tx_hist_find_entry);

void dccp_tx_hist_purge_older(struct dccp_tx_hist *hist,
			      struct list_head *list,
			      struct dccp_tx_hist_entry *packet)
{
	struct dccp_tx_hist_entry *next;

	list_for_each_entry_safe_continue(packet, next, list, dccphtx_node) {
		list_del_init(&packet->dccphtx_node);
		dccp_tx_hist_entry_delete(hist, packet);
	}
}

EXPORT_SYMBOL_GPL(dccp_tx_hist_purge_older);

void dccp_tx_hist_purge(struct dccp_tx_hist *hist, struct list_head *list)
{
	struct dccp_tx_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, list, dccphtx_node) {
		list_del_init(&entry->dccphtx_node);
		dccp_tx_hist_entry_delete(hist, entry);
	}
}

EXPORT_SYMBOL_GPL(dccp_tx_hist_purge);

MODULE_AUTHOR("Ian McDonald <iam4@cs.waikato.ac.nz>, "
	      "Arnaldo Carvalho de Melo <acme@ghostprotocols.net>");
MODULE_DESCRIPTION("DCCP TFRC library");
MODULE_LICENSE("GPL");
