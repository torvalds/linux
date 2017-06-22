/* Copyright (C) 2013-2017  B.A.T.M.A.N. contributors:
 *
 * Antonio Quartulli
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

#ifndef _BATMAN_ADV_BATADV_V_OGM_H_
#define _BATMAN_ADV_BATADV_V_OGM_H_

#include "main.h"

#include <linux/types.h>

struct sk_buff;

int batadv_v_ogm_init(struct batadv_priv *bat_priv);
void batadv_v_ogm_free(struct batadv_priv *bat_priv);
int batadv_v_ogm_iface_enable(struct batadv_hard_iface *hard_iface);
struct batadv_orig_node *batadv_v_ogm_orig_get(struct batadv_priv *bat_priv,
					       const u8 *addr);
void batadv_v_ogm_primary_iface_set(struct batadv_hard_iface *primary_iface);
int batadv_v_ogm_packet_recv(struct sk_buff *skb,
			     struct batadv_hard_iface *if_incoming);

#endif /* _BATMAN_ADV_BATADV_V_OGM_H_ */
