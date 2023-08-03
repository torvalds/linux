/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_DEVLINK_GEN_H
#define _LINUX_DEVLINK_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/devlink.h>

/* Ops table for devlink */
extern const struct genl_split_ops devlink_nl_ops[4];

int devlink_nl_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
			struct genl_info *info);
void
devlink_nl_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		     struct genl_info *info);

int devlink_nl_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int devlink_nl_info_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_info_get_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);

#endif /* _LINUX_DEVLINK_GEN_H */
