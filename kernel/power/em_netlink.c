// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Generic netlink for energy model.
 *
 * Copyright (c) 2025 Valve Corporation.
 * Author: Changwoo Min <changwoo@igalia.com>
 */

#define pr_fmt(fmt) "energy_model: " fmt

#include <linux/energy_model.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include <uapi/linux/energy_model.h>

#include "em_netlink.h"
#include "em_netlink_autogen.h"

int em_nl_get_pds_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int em_nl_get_pd_table_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

static int __init em_netlink_init(void)
{
	return genl_register_family(&em_nl_family);
}
postcore_initcall(em_netlink_init);
