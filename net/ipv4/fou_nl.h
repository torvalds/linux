/* SPDX-License-Identifier: BSD-3-Clause */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/fou.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_FOU_GEN_H
#define _LINUX_FOU_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <linux/fou.h>

/* Global operation policy for fou */
extern const struct nla_policy fou_nl_policy[FOU_ATTR_IFINDEX + 1];

/* Ops table for fou */
extern const struct genl_small_ops fou_nl_ops[3];

int fou_nl_add_doit(struct sk_buff *skb, struct genl_info *info);
int fou_nl_del_doit(struct sk_buff *skb, struct genl_info *info);
int fou_nl_get_doit(struct sk_buff *skb, struct genl_info *info);
int fou_nl_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);

#endif /* _LINUX_FOU_GEN_H */
