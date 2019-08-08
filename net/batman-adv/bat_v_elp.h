/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2013-2019  B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing, Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_BAT_V_ELP_H_
#define _NET_BATMAN_ADV_BAT_V_ELP_H_

#include "main.h"

struct sk_buff;
struct work_struct;

int batadv_v_elp_iface_enable(struct batadv_hard_iface *hard_iface);
void batadv_v_elp_iface_disable(struct batadv_hard_iface *hard_iface);
void batadv_v_elp_iface_activate(struct batadv_hard_iface *primary_iface,
				 struct batadv_hard_iface *hard_iface);
void batadv_v_elp_primary_iface_set(struct batadv_hard_iface *primary_iface);
int batadv_v_elp_packet_recv(struct sk_buff *skb,
			     struct batadv_hard_iface *if_incoming);
void batadv_v_elp_throughput_metric_update(struct work_struct *work);

#endif /* _NET_BATMAN_ADV_BAT_V_ELP_H_ */
