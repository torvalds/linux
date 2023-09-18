/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright Samuel Mendoza-Jonas, IBM Corporation 2018.
 */

#ifndef __NCSI_NETLINK_H__
#define __NCSI_NETLINK_H__

#include <linux/netdevice.h>

#include "internal.h"

int ncsi_send_netlink_rsp(struct ncsi_request *nr,
			  struct ncsi_package *np,
			  struct ncsi_channel *nc);
int ncsi_send_netlink_timeout(struct ncsi_request *nr,
			      struct ncsi_package *np,
			      struct ncsi_channel *nc);
int ncsi_send_netlink_err(struct net_device *dev,
			  u32 snd_seq,
			  u32 snd_portid,
			  const struct nlmsghdr *nlhdr,
			  int err);

#endif /* __NCSI_NETLINK_H__ */
