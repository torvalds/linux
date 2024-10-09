/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/net_shaper.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_NET_SHAPER_GEN_H
#define _LINUX_NET_SHAPER_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/net_shaper.h>

/* Common nested types */
extern const struct nla_policy net_shaper_handle_nl_policy[NET_SHAPER_A_HANDLE_ID + 1];
extern const struct nla_policy net_shaper_leaf_info_nl_policy[NET_SHAPER_A_WEIGHT + 1];

int net_shaper_nl_pre_doit(const struct genl_split_ops *ops,
			   struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_cap_pre_doit(const struct genl_split_ops *ops,
			       struct sk_buff *skb, struct genl_info *info);
void
net_shaper_nl_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
			struct genl_info *info);
void
net_shaper_nl_cap_post_doit(const struct genl_split_ops *ops,
			    struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_pre_dumpit(struct netlink_callback *cb);
int net_shaper_nl_cap_pre_dumpit(struct netlink_callback *cb);
int net_shaper_nl_post_dumpit(struct netlink_callback *cb);
int net_shaper_nl_cap_post_dumpit(struct netlink_callback *cb);

int net_shaper_nl_get_doit(struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int net_shaper_nl_set_doit(struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_delete_doit(struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_group_doit(struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_cap_get_doit(struct sk_buff *skb, struct genl_info *info);
int net_shaper_nl_cap_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb);

extern struct genl_family net_shaper_nl_family;

#endif /* _LINUX_NET_SHAPER_GEN_H */
