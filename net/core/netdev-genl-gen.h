/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_NETDEV_GEN_H
#define _LINUX_NETDEV_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/netdev.h>
#include <net/netdev_netlink.h>

/* Common nested types */
extern const struct nla_policy netdev_page_pool_info_nl_policy[NETDEV_A_PAGE_POOL_IFINDEX + 1];
extern const struct nla_policy netdev_queue_id_nl_policy[NETDEV_A_QUEUE_TYPE + 1];

int netdev_nl_dev_get_doit(struct sk_buff *skb, struct genl_info *info);
int netdev_nl_dev_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int netdev_nl_page_pool_get_doit(struct sk_buff *skb, struct genl_info *info);
int netdev_nl_page_pool_get_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb);
int netdev_nl_page_pool_stats_get_doit(struct sk_buff *skb,
				       struct genl_info *info);
int netdev_nl_page_pool_stats_get_dumpit(struct sk_buff *skb,
					 struct netlink_callback *cb);
int netdev_nl_queue_get_doit(struct sk_buff *skb, struct genl_info *info);
int netdev_nl_queue_get_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);
int netdev_nl_napi_get_doit(struct sk_buff *skb, struct genl_info *info);
int netdev_nl_napi_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int netdev_nl_qstats_get_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb);
int netdev_nl_bind_rx_doit(struct sk_buff *skb, struct genl_info *info);
int netdev_nl_napi_set_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	NETDEV_NLGRP_MGMT,
	NETDEV_NLGRP_PAGE_POOL,
};

extern struct genl_family netdev_nl_family;

void netdev_nl_sock_priv_init(struct netdev_nl_sock *priv);
void netdev_nl_sock_priv_destroy(struct netdev_nl_sock *priv);

#endif /* _LINUX_NETDEV_GEN_H */
