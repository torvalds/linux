/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_GENETLINK_H
#define __NET_GENETLINK_H

#include <linux/wait.h>

/* for synchronisation between af_netlink and genetlink */
extern atomic_t genl_sk_destructing_cnt;
extern wait_queue_head_t genl_sk_destructing_waitq;

#endif	/* __LINUX_GENERIC_NETLINK_H */
