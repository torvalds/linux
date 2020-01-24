// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, Nikolay Aleksandrov <nikolay@cumulusnetworks.com>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "br_private.h"

/* check if the options between two vlans are equal */
bool br_vlan_opts_eq(const struct net_bridge_vlan *v1,
		     const struct net_bridge_vlan *v2)
{
	return true;
}

bool br_vlan_opts_fill(struct sk_buff *skb, const struct net_bridge_vlan *v)
{
	return true;
}

size_t br_vlan_opts_nl_size(void)
{
	return 0;
}
