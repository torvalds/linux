/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_NETDEVICE_XMIT_H
#define _LINUX_NETDEVICE_XMIT_H

struct netdev_xmit {
	u16 recursion;
	u8  more;
#ifdef CONFIG_NET_EGRESS
	u8  skip_txqueue;
#endif
};

#endif
