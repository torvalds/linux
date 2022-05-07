/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2016-2020  B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
 */

#ifndef _NET_BATMAN_ADV_NETLINK_H_
#define _NET_BATMAN_ADV_NETLINK_H_

#include "main.h"

#include <linux/netlink.h>
#include <linux/types.h>
#include <net/genetlink.h>

void batadv_netlink_register(void);
void batadv_netlink_unregister(void);
int batadv_netlink_get_ifindex(const struct nlmsghdr *nlh, int attrtype);

int batadv_netlink_tpmeter_notify(struct batadv_priv *bat_priv, const u8 *dst,
				  u8 result, u32 test_time, u64 total_bytes,
				  u32 cookie);

int batadv_netlink_notify_mesh(struct batadv_priv *bat_priv);
int batadv_netlink_notify_hardif(struct batadv_priv *bat_priv,
				 struct batadv_hard_iface *hard_iface);
int batadv_netlink_notify_vlan(struct batadv_priv *bat_priv,
			       struct batadv_softif_vlan *vlan);

extern struct genl_family batadv_netlink_family;

#endif /* _NET_BATMAN_ADV_NETLINK_H_ */
