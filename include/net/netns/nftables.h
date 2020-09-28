/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>

struct netns_nftables {
	struct list_head	tables;
	struct list_head	commit_list;
	struct list_head	module_list;
	struct list_head	notify_list;
	struct mutex		commit_mutex;
	unsigned int		base_seq;
	u8			gencursor;
	u8			validate_state;
};

#endif
