/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_NFSD_GEN_H
#define _LINUX_NFSD_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/nfsd_netlink.h>

int nfsd_nl_rpc_status_get_start(struct netlink_callback *cb);
int nfsd_nl_rpc_status_get_done(struct netlink_callback *cb);

int nfsd_nl_rpc_status_get_dumpit(struct sk_buff *skb,
				  struct netlink_callback *cb);

extern struct genl_family nfsd_nl_family;

#endif /* _LINUX_NFSD_GEN_H */
