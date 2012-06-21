/* Copyright (C) 2009-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
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

#ifndef _NET_BATMAN_ADV_GATEWAY_CLIENT_H_
#define _NET_BATMAN_ADV_GATEWAY_CLIENT_H_

void batadv_gw_deselect(struct bat_priv *bat_priv);
void batadv_gw_election(struct bat_priv *bat_priv);
struct orig_node *batadv_gw_get_selected_orig(struct bat_priv *bat_priv);
void batadv_gw_check_election(struct bat_priv *bat_priv,
			      struct orig_node *orig_node);
void batadv_gw_node_update(struct bat_priv *bat_priv,
			   struct orig_node *orig_node, uint8_t new_gwflags);
void batadv_gw_node_delete(struct bat_priv *bat_priv,
			   struct orig_node *orig_node);
void batadv_gw_node_purge(struct bat_priv *bat_priv);
int batadv_gw_client_seq_print_text(struct seq_file *seq, void *offset);
bool batadv_gw_is_dhcp_target(struct sk_buff *skb, unsigned int *header_len);
bool batadv_gw_out_of_range(struct bat_priv *bat_priv,
			    struct sk_buff *skb, struct ethhdr *ethhdr);

#endif /* _NET_BATMAN_ADV_GATEWAY_CLIENT_H_ */
