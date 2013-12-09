/* Copyright (C) 2013 B.A.T.M.A.N. contributors:
 *
 * Martin Hundebøll <martin@hundeboll.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _NET_BATMAN_ADV_FRAGMENTATION_H_
#define _NET_BATMAN_ADV_FRAGMENTATION_H_

void batadv_frag_purge_orig(struct batadv_orig_node *orig,
			    bool (*check_cb)(struct batadv_frag_table_entry *));
bool batadv_frag_skb_fwd(struct sk_buff *skb,
			 struct batadv_hard_iface *recv_if,
			 struct batadv_orig_node *orig_node_src);
bool batadv_frag_skb_buffer(struct sk_buff **skb,
			    struct batadv_orig_node *orig_node);
bool batadv_frag_send_packet(struct sk_buff *skb,
			     struct batadv_orig_node *orig_node,
			     struct batadv_neigh_node *neigh_node);

/**
 * batadv_frag_check_entry - check if a list of fragments has timed out
 * @frags_entry: table entry to check
 *
 * Returns true if the frags entry has timed out, false otherwise.
 */
static inline bool
batadv_frag_check_entry(struct batadv_frag_table_entry *frags_entry)
{
	if (!hlist_empty(&frags_entry->head) &&
	    batadv_has_timed_out(frags_entry->timestamp, BATADV_FRAG_TIMEOUT))
		return true;
	else
		return false;
}

#endif /* _NET_BATMAN_ADV_FRAGMENTATION_H_ */
