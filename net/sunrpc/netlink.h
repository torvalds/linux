/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/sunrpc_cache.yaml */
/* YNL-GEN kernel header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_SUNRPC_GEN_H
#define _LINUX_SUNRPC_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/sunrpc_netlink.h>

/* Common nested types */
extern const struct nla_policy sunrpc_ip_map_nl_policy[SUNRPC_A_IP_MAP_EXPIRY + 1];
extern const struct nla_policy sunrpc_unix_gid_nl_policy[SUNRPC_A_UNIX_GID_EXPIRY + 1];

int sunrpc_nl_ip_map_get_reqs_dumpit(struct sk_buff *skb,
				     struct netlink_callback *cb);
int sunrpc_nl_ip_map_set_reqs_doit(struct sk_buff *skb, struct genl_info *info);
int sunrpc_nl_unix_gid_get_reqs_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb);
int sunrpc_nl_unix_gid_set_reqs_doit(struct sk_buff *skb,
				     struct genl_info *info);
int sunrpc_nl_cache_flush_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	SUNRPC_NLGRP_NONE,
	SUNRPC_NLGRP_EXPORTD,
};

extern struct genl_family sunrpc_nl_family;

#endif /* _LINUX_SUNRPC_GEN_H */
