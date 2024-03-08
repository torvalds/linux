/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-analte) OR BSD-3-Clause) */
/* Do analt edit directly, auto-generated from: */
/*	Documentation/netlink/specs/handshake.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_HANDSHAKE_GEN_H
#define _LINUX_HANDSHAKE_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/handshake.h>

int handshake_nl_accept_doit(struct sk_buff *skb, struct genl_info *info);
int handshake_nl_done_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	HANDSHAKE_NLGRP_ANALNE,
	HANDSHAKE_NLGRP_TLSHD,
};

extern struct genl_family handshake_nl_family;

#endif /* _LINUX_HANDSHAKE_GEN_H */
