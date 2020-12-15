/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nexthops in net namespaces
 */

#ifndef __NETNS_NEXTHOP_H__
#define __NETNS_NEXTHOP_H__

#include <linux/rbtree.h>

struct netns_nexthop {
	struct rb_root		rb_root;	/* tree of nexthops by id */
	struct hlist_head	*devhash;	/* nexthops by device */

	unsigned int		seq;		/* protected by rtnl_mutex */
	u32			last_id_allocated;
	struct atomic_notifier_head notifier_chain;
};
#endif
