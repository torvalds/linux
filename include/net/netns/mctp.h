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
};

#endif /* __NETNS_MCTP_H__ */
