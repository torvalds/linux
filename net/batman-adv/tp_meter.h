/* Copyright (C) 2012-2016 B.A.T.M.A.N. contributors:
 *
 * Edo Monticelli, Antonio Quartulli
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

#ifndef _NET_BATMAN_ADV_TP_METER_H_
#define _NET_BATMAN_ADV_TP_METER_H_

#include "main.h"

#include <linux/types.h>

struct sk_buff;

void batadv_tp_meter_init(void);
void batadv_tp_start(struct batadv_priv *bat_priv, const u8 *dst,
		     u32 test_length, u32 *cookie);
void batadv_tp_stop(struct batadv_priv *bat_priv, const u8 *dst,
		    u8 return_value);
void batadv_tp_meter_recv(struct batadv_priv *bat_priv, struct sk_buff *skb);

#endif /* _NET_BATMAN_ADV_TP_METER_H_ */
