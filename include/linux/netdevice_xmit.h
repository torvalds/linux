/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_NETDEVICE_XMIT_H
#define _LINUX_NETDEVICE_XMIT_H

#if IS_ENABLED(CONFIG_NET_ACT_MIRRED)
#define MIRRED_NEST_LIMIT	4
#endif

struct net_device;

struct netdev_xmit {
	u16 recursion;
	u8  more;
#ifdef CONFIG_NET_EGRESS
	u8  skip_txqueue;
#endif
#if IS_ENABLED(CONFIG_NET_ACT_MIRRED)
	u8			sched_mirred_nest;
	struct net_device	*sched_mirred_dev[MIRRED_NEST_LIMIT];
#endif
#if IS_ENABLED(CONFIG_NF_DUP_NETDEV)
	u8 nf_dup_skb_recursion;
#endif
};

#endif
