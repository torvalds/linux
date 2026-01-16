/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dev-energymodel.yaml */
/* YNL-GEN kernel header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_DEV_ENERGYMODEL_GEN_H
#define _LINUX_DEV_ENERGYMODEL_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/dev_energymodel.h>

int dev_energymodel_nl_get_perf_domains_doit(struct sk_buff *skb,
					     struct genl_info *info);
int dev_energymodel_nl_get_perf_domains_dumpit(struct sk_buff *skb,
					       struct netlink_callback *cb);
int dev_energymodel_nl_get_perf_table_doit(struct sk_buff *skb,
					   struct genl_info *info);

enum {
	DEV_ENERGYMODEL_NLGRP_EVENT,
};

extern struct genl_family dev_energymodel_nl_family;

#endif /* _LINUX_DEV_ENERGYMODEL_GEN_H */
