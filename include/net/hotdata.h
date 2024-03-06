/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_HOTDATA_H
#define _NET_HOTDATA_H

#include <linux/types.h>

/* Read mostly data used in network fast paths. */
struct net_hotdata {
	struct list_head	offload_base;
	int			gro_normal_batch;
	int			netdev_budget;
	int			netdev_budget_usecs;
};

extern struct net_hotdata net_hotdata;

#endif /* _NET_HOTDATA_H */
