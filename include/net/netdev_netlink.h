/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_NETDEV_NETLINK_H
#define __NET_NETDEV_NETLINK_H

#include <linux/list.h>

struct netdev_nl_sock {
	struct mutex lock;
	struct list_head bindings;
};

#endif	/* __NET_NETDEV_NETLINK_H */
