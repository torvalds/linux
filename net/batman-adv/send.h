/* Copyright (C) 2007-2015 B.A.T.M.A.N. contributors:
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
#include <linux/types.h>

#include "packet.h"

struct batadv_hard_iface;
struct batadv_orig_node;
struct batadv_priv;
struct sk_buff;
struct work_struct;

int batadv_send_skb_packet(struct sk_buff *skb,
			   struct batadv_hard_iface *hard_iface,
			   const uint8_t *dst_addr);
int batadv_send_skb_to_orig(struct sk_buff *skb,
			    struct batadv_orig_node *orig_node,
			    struct batadv_hard_iface *recv_if);
void batadv_schedule_bat_ogm(struct batadv_hard_iface *hard_iface);
int batadv_add_bcast_packet_to_list(struct batadv_priv *bat_priv,
				    const struct sk_buff *skb,
				    unsigned long delay);
void batadv_send_outstanding_bat_ogm_packet(struct work_struct *work);
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
				   int packet_subtype, uint8_t *dst_hint,
				   unsigned short vid);
int batadv_send_skb_via_gw(struct batadv_priv *bat_priv, struct sk_buff *skb,
			   unsigned short vid);

/**
 * batadv_send_skb_via_tt - send an skb via TT lookup
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the payload to send
 * @dst_hint: can be used to override the destination contained in the skb
 * @vid: the vid to be used to search the translation table
 *
 * Look up the recipient node for the destination address in the ethernet
 * header via the translation table. Wrap the given skb into a batman-adv
 * unicast header. Then send this frame to the according destination node.
 *
 * Returns NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
static inline int batadv_send_skb_via_tt(struct batadv_priv *bat_priv,
					 struct sk_buff *skb, uint8_t *dst_hint,
					 unsigned short vid)
{
	return batadv_send_skb_via_tt_generic(bat_priv, skb, BATADV_UNICAST, 0,
					      dst_hint, vid);
}

/**
 * batadv_send_skb_via_tt_4addr - send an skb via TT lookup
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
 * Returns NET_XMIT_DROP in case of error or NET_XMIT_SUCCESS otherwise.
 */
static inline int batadv_send_skb_via_tt_4addr(struct batadv_priv *bat_priv,
					       struct sk_buff *skb,
					       int packet_subtype,
					       uint8_t *dst_hint,
					       unsigned short vid)
{
	return batadv_send_skb_via_tt_generic(bat_priv, skb,
					      BATADV_UNICAST_4ADDR,
					      packet_subtype, dst_hint, vid);
}

#endif /* _NET_BATMAN_ADV_SEND_H_ */
