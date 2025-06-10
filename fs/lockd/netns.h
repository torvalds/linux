/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LOCKD_NETNS_H__
#define __LOCKD_NETNS_H__

#include <linux/fs.h>
#include <linux/filelock.h>
#include <net/netns/generic.h>

struct lockd_net {
	unsigned int nlmsvc_users;
	unsigned long next_gc;
	unsigned long nrhosts;
	u32 gracetime;
	u16 tcp_port;
	u16 udp_port;

	struct delayed_work grace_period_end;
	struct lock_manager lockd_manager;

	struct list_head nsm_handles;
};

extern unsigned int lockd_net_id;

#endif
