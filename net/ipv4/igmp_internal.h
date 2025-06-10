/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IGMP_INTERNAL_H
#define _LINUX_IGMP_INTERNAL_H

struct inet_fill_args {
	u32 portid;
	u32 seq;
	int event;
	unsigned int flags;
	int netnsid;
	int ifindex;
};

int inet_fill_ifmcaddr(struct sk_buff *skb, struct net_device *dev,
		       const struct ip_mc_list *im,
		       struct inet_fill_args *args);
#endif
