/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/psp.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_PSP_GEN_H
#define _LINUX_PSP_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/psp.h>

int psp_device_get_locked(const struct genl_split_ops *ops,
			  struct sk_buff *skb, struct genl_info *info);
void
psp_device_unlock(const struct genl_split_ops *ops, struct sk_buff *skb,
		  struct genl_info *info);

int psp_nl_dev_get_doit(struct sk_buff *skb, struct genl_info *info);
int psp_nl_dev_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int psp_nl_dev_set_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	PSP_NLGRP_MGMT,
};

extern struct genl_family psp_nl_family;

#endif /* _LINUX_PSP_GEN_H */
