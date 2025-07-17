/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_NETDEVICE_XMIT_H
#define _LINUX_NETDEVICE_XMIT_H

struct netdev_xmit {
	u16 recursion;
	u8  more;
#ifdef CONFIG_NET_EGRESS
	u8  skip_txqueue;
#endif
#if IS_ENABLED(CONFIG_NET_ACT_MIRRED)
	u8 sched_mirred_nest;
#endif
#if IS_ENABLED(CONFIG_NF_DUP_NETDEV)
	u8 nf_dup_skb_recursion;
#endif
};

#endif
