/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007-2017  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _NET_BATMAN_ADV_SEND_H_
#define _NET_BATMAN_ADV_SEND_H_

#include "main.h"

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <uapi/linux/batadv_packet.h>

struct sk_buff;

void batadv_forw_packet_free(struct batadv_forw_packet *forw_packet,
			     bool dropped);
struct batadv_forw_packet *
batadv_forw_packet_alloc(struct batadv_hard_iface *if_incoming,
			 struct batadv_hard_iface *if_outgoing,
			 atomic_t *queue_left,
			 struct batadv_priv *bat_priv,
			 struct sk_buff *skb);
bool batadv_forw_packet_steal(struct batadv_forw_packet *packet, spinlock_t *l);
void batadv_forw_packet_ogmv1_queue(struct batadv_priv *bat_priv,
				    struct batadv_forw_packet *forw_packet,
				    unsigned long send_time);
bool batadv_forw_packet_is_rebroadcast(struct batadv_forw_packet *forw_packet);

int batadv_send_skb_to_orig(struct sk_buff *skb,
			    struct batadv_orig_node *orig_node,
			    struct batadv_hard_iface *recv_if);
int batadv_send_skb_packet(struct sk_buff *skb,
			   struct batadv_hard_iface *hard_iface,
			   const u8 *dst_addr);
int batadv_send_broadcast_skb(struct sk_buff *skb,
			      struct batadv_hard_iface *hard_iface);
int batadv_send_unicast_skb(struct sk_buff *skb,
			    struct batadv_neigh_node *neigh_node);
int batadv_add_bcast_packet_to_list(struct batadv_priv *bat_priv,
				    const struct sk_buff *skb,
				    unsigned long delay,
				    bool own_packet);
void
batadv_purge_outstanding_packets(struct batadv_priv *bat_priv,
				 const struct batadv_hard_iface *hard_iface);
bool batadv_send_skb_prepare_unicast_4addr(struct batadv_priv *bat_priv,
					   struct sk_buff *skb,
					   struct batadv_orig_node *orig_node,
					   int packet_subtype);
int batadv_send_skb_unicast(struct batadv_priv *bat_priv,
			    struct sk_buff *skb, int packet_type,
			    int packet_subtype,
			    struct batadv_orig_node *orig_node,
			    unsigned short vid);
int batadv_send_skb_via_tt_generic(struct batadv_priv *bat_priv,
				   struct sk_buff *skb, int packet_type,
				   int packet_subtype, u8 *dst_hint,
				   unsigned short vid);
int batadv_send_skb_via_gw(struct batadv_priv *bat_priv, struct sk_buff *skb,
			   unsigned short vid);

/**
 * batadv_send_skb_via_tt() - send an skb via TT lookup
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the payload to send
 * @dst_hint: can be used to override the destination contained in the skb
 * @vid: the vid to be used to search the translation table
 *
 * Look up the recipient node for the destination address in the ethernet
 * header via the translation table. Wrap the given skb into a batman-adv
 * unicast header. Then send this frame to the according destination node.
 *
 * Return: NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
static inline int batadv_send_skb_via_tt(struct batadv_priv *bat_priv,
					 struct sk_buff *skb, u8 *dst_hint,
					 unsigned short vid)
{
	return batadv_send_skb_via_tt_generic(bat_priv, skb, BATADV_UNICAST, 0,
					      dst_hint, vid);
}

/**
 * batadv_send_skb_via_tt_4addr() - send an skb via TT lookup
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the payload to send
 * @packet_subtype: the unicast 4addr packet subtype to use
 * @dst_hint: can be used to override the destination contained in the skb
 * @vid: the vid to be used to search the translation table
 *
 * Look up the recipient node for the destination address in the ethernet
 * header via the translation table. Wrap the given skb into a batman-adv
 * unicast-4addr header. Then send this frame to the according destination
 * node.
 *
 * Return: NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
static inline int batadv_send_skb_via_tt_4addr(struct batadv_priv *bat_priv,
					       struct sk_buff *skb,
					       int packet_subtype,
					       u8 *dst_hint,
					       unsigned short vid)
{
	return batadv_send_skb_via_tt_generic(bat_priv, skb,
					      BATADV_UNICAST_4ADDR,
					      packet_subtype, dst_hint, vid);
}

#endif /* _NET_BATMAN_ADV_SEND_H_ */
