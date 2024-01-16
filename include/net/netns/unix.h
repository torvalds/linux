/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unix network namespace
 */
#ifndef __NETNS_UNIX_H__
#define __NETNS_UNIX_H__

#include <linux/spinlock.h>

struct unix_table {
	spinlock_t		*locks;
	struct hlist_head	*buckets;
};

struct ctl_table_header;
struct netns_unix {
	struct unix_table	table;
	int			sysctl_max_dgram_qlen;
	struct ctl_table_header	*ctl;
};

#endif /* __NETNS_UNIX_H__ */
