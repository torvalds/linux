/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
 */

#ifndef _NET_BATMAN_ADV_NETLINK_H_
#define _NET_BATMAN_ADV_NETLINK_H_

#include "main.h"

#include <linux/netlink.h>
#include <linux/types.h>

void batadv_netlink_register(void);
void batadv_netlink_unregister(void);
struct net_device *batadv_netlink_get_meshif(struct netlink_callback *cb);
struct batadv_hard_iface *
batadv_netlink_get_hardif(struct batadv_priv *bat_priv,
			  struct netlink_callback *cb);

int batadv_netlink_tpmeter_notify(struct batadv_priv *bat_priv, const u8 *dst,
				  u8 result, u32 test_time, u64 total_bytes,
				  u32 cookie);

extern struct genl_family batadv_netlink_family;

#endif /* _NET_BATMAN_ADV_NETLINK_H_ */
