/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2020  B.A.T.M.A.N. contributors:
 *
 * Edo Monticelli, Antonio Quartulli
 */

#ifndef _NET_BATMAN_ADV_TP_METER_H_
#define _NET_BATMAN_ADV_TP_METER_H_

#include "main.h"

#include <linux/skbuff.h>
#include <linux/types.h>

void batadv_tp_meter_init(void);
void batadv_tp_start(struct batadv_priv *bat_priv, const u8 *dst,
		     u32 test_length, u32 *cookie);
void batadv_tp_stop(struct batadv_priv *bat_priv, const u8 *dst,
		    u8 return_value);
void batadv_tp_meter_recv(struct batadv_priv *bat_priv, struct sk_buff *skb);

#endif /* _NET_BATMAN_ADV_TP_METER_H_ */
