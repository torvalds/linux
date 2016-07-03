/* Copyright (C) 2016 B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
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

#ifndef _NET_BATMAN_ADV_NETLINK_H_
#define _NET_BATMAN_ADV_NETLINK_H_

#include "main.h"

#include <linux/types.h>
#include <net/genetlink.h>

struct nlmsghdr;

void batadv_netlink_register(void);
void batadv_netlink_unregister(void);
int batadv_netlink_get_ifindex(const struct nlmsghdr *nlh, int attrtype);

int batadv_netlink_tpmeter_notify(struct batadv_priv *bat_priv, const u8 *dst,
				  u8 result, u32 test_time, u64 total_bytes,
				  u32 cookie);

extern struct genl_family batadv_netlink_family;

#endif /* _NET_BATMAN_ADV_NETLINK_H_ */
