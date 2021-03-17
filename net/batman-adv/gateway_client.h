/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2009-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_GATEWAY_CLIENT_H_
#define _NET_BATMAN_ADV_GATEWAY_CLIENT_H_

#include "main.h"

#include <linux/netlink.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <uapi/linux/batadv_packet.h>

void batadv_gw_check_client_stop(struct batadv_priv *bat_priv);
void batadv_gw_reselect(struct batadv_priv *bat_priv);
void batadv_gw_election(struct batadv_priv *bat_priv);
struct batadv_orig_node *
batadv_gw_get_selected_orig(struct batadv_priv *bat_priv);
void batadv_gw_check_election(struct batadv_priv *bat_priv,
			      struct batadv_orig_node *orig_node);
void batadv_gw_node_update(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node,
			   struct batadv_tvlv_gateway_data *gateway);
void batadv_gw_node_delete(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node);
void batadv_gw_node_free(struct batadv_priv *bat_priv);
void batadv_gw_node_put(struct batadv_gw_node *gw_node);
struct batadv_gw_node *
batadv_gw_get_selected_gw_node(struct batadv_priv *bat_priv);
int batadv_gw_client_seq_print_text(struct seq_file *seq, void *offset);
int batadv_gw_dump(struct sk_buff *msg, struct netlink_callback *cb);
bool batadv_gw_out_of_range(struct batadv_priv *bat_priv, struct sk_buff *skb);
enum batadv_dhcp_recipient
batadv_gw_dhcp_recipient_get(struct sk_buff *skb, unsigned int *header_len,
			     u8 *chaddr);
struct batadv_gw_node *batadv_gw_node_get(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig_node);

#endif /* _NET_BATMAN_ADV_GATEWAY_CLIENT_H_ */
