/*
 * Copyright (C) 2011 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich
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

#ifndef _NET_BATMAN_ADV_BLA_H_
#define _NET_BATMAN_ADV_BLA_H_

int bla_rx(struct bat_priv *bat_priv, struct sk_buff *skb, short vid);
int bla_tx(struct bat_priv *bat_priv, struct sk_buff *skb, short vid);
int bla_is_backbone_gw(struct sk_buff *skb,
		       struct orig_node *orig_node, int hdr_size);
void bla_update_orig_address(struct bat_priv *bat_priv,
			     struct hard_iface *primary_if,
			     struct hard_iface *oldif);
int bla_init(struct bat_priv *bat_priv);
void bla_free(struct bat_priv *bat_priv);

#define BLA_CRC_INIT	0

#endif /* ifndef _NET_BATMAN_ADV_BLA_H_ */
