/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCTP per-net structures
 */

#ifndef __NETNS_MCTP_H__
#define __NETNS_MCTP_H__

#include <linux/types.h>

struct netns_mctp {
	/* Only updated under RTNL, entries freed via RCU */
	struct list_head routes;

	/* Bound sockets: list of sockets bound by type.
	 * This list is updated from non-atomic contexts (under bind_lock),
	 * and read (under rcu) in packet rx
	 */
	struct mutex bind_lock;
	struct hlist_head binds;

	/* tag allocations. This list is read and updated from atomic contexts,
	 * but elements are free()ed after a RCU grace-period
	 */
	spinlock_t keys_lock;
	struct hlist_head keys;

	/* MCTP network */
	unsigned int default_net;

	/* neighbour table */
	struct mutex neigh_lock;
	struct list_head neighbours;
};

#endif /* __NETNS_MCTP_H__ */
