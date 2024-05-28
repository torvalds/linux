/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_PLIST_TYPES_H
#define _LINUX_PLIST_TYPES_H

#include <linux/types.h>

struct plist_head {
	struct list_head node_list;
};

struct plist_node {
	int			prio;
	struct list_head	prio_list;
	struct list_head	node_list;
};

#endif /* _LINUX_PLIST_TYPES_H */
