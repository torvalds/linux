/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_NETDEV_GEN_H
#define _LINUX_NETDEV_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <linux/netdev.h>

int netdev_nl_dev_get_doit(struct sk_buff *skb, struct genl_info *info);
int netdev_nl_dev_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);

enum {
	NETDEV_NLGRP_MGMT,
};

extern struct genl_family netdev_nl_family;

#endif /* _LINUX_NETDEV_GEN_H */
