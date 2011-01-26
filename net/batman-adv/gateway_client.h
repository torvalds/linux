/*
 * Copyright (C) 2009-2010 B.A.T.M.A.N. contributors:
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
 *
 */

#ifndef _NET_BATMAN_ADV_GATEWAY_CLIENT_H_
#define _NET_BATMAN_ADV_GATEWAY_CLIENT_H_

void gw_deselect(struct bat_priv *bat_priv);
void gw_election(struct bat_priv *bat_priv);
void *gw_get_selected(struct bat_priv *bat_priv);
void gw_check_election(struct bat_priv *bat_priv, struct orig_node *orig_node);
void gw_node_update(struct bat_priv *bat_priv,
		    struct orig_node *orig_node, uint8_t new_gwflags);
void gw_node_delete(struct bat_priv *bat_priv, struct orig_node *orig_node);
void gw_node_purge(struct bat_priv *bat_priv);
int gw_client_seq_print_text(struct seq_file *seq, void *offset);
int gw_is_target(struct bat_priv *bat_priv, struct sk_buff *skb);

#endif /* _NET_BATMAN_ADV_GATEWAY_CLIENT_H_ */
