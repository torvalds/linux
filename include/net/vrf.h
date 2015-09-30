/*
 * include/net/net_vrf.h - adds vrf dev structure definitions
 * Copyright (c) 2015 Cumulus Networks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_NET_VRF_H
#define __LINUX_NET_VRF_H

struct slave {
	struct list_head	list;
	struct net_device	*dev;
};

struct slave_queue {
	struct list_head	all_slaves;
};

struct net_vrf {
	struct slave_queue	queue;
	struct rtable           *rth;
	u32			tb_id;
};

#endif /* __LINUX_NET_VRF_H */
