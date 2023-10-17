/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#ifndef _NET_BATMAN_ADV_ROUTING_H_
#define _NET_BATMAN_ADV_ROUTING_H_

#include "main.h"

#include <linux/skbuff.h>
#include <linux/types.h>

bool batadv_check_management_packet(struct sk_buff *skb,
				    struct batadv_hard_iface *hard_iface,
				    int header_len);
void batadv_update_route(struct batadv_priv *bat_priv,
			 struct batadv_orig_node *orig_node,
			 struct batadv_hard_iface *recv_if,
			 struct batadv_neigh_node *neigh_node);
int batadv_recv_icmp_packet(struct sk_buff *skb,
			    struct batadv_hard_iface *recv_if);
int batadv_recv_unicast_packet(struct sk_buff *skb,
			       struct batadv_hard_iface *recv_if);
int batadv_recv_frag_packet(struct sk_buff *skb,
			    struct batadv_hard_iface *iface);
int batadv_recv_bcast_packet(struct sk_buff *skb,
			     struct batadv_hard_iface *recv_if);
int batadv_recv_unicast_tvlv(struct sk_buff *skb,
			     struct batadv_hard_iface *recv_if);
int batadv_recv_unhandled_unicast_packet(struct sk_buff *skb,
					 struct batadv_hard_iface *recv_if);
struct batadv_neigh_node *
batadv_find_router(struct batadv_priv *bat_priv,
		   struct batadv_orig_node *orig_node,
		   struct batadv_hard_iface *recv_if);
bool batadv_window_protected(struct batadv_priv *bat_priv, s32 seq_num_diff,
			     s32 seq_old_max_diff, unsigned long *last_reset,
			     bool *protection_started);

#endif /* _NET_BATMAN_ADV_ROUTING_H_ */
