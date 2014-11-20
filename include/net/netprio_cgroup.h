/*
 * netprio_cgroup.h			Control Group Priority set
 *
 *
 * Authors:	Neil Horman <nhorman@tuxdriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _NETPRIO_CGROUP_H
#define _NETPRIO_CGROUP_H

#include <linux/cgroup.h>
#include <linux/hardirq.h>
#include <linux/rcupdate.h>

#if IS_ENABLED(CONFIG_CGROUP_NET_PRIO)
struct netprio_map {
	struct rcu_head rcu;
	u32 priomap_len;
	u32 priomap[];
};

void sock_update_netprioidx(struct sock *sk);

static inline u32 task_netprioidx(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	u32 idx;

	rcu_read_lock();
	css = task_css(p, net_prio_cgrp_id);
	idx = css->cgroup->id;
	rcu_read_unlock();
	return idx;
}
#else /* !CONFIG_CGROUP_NET_PRIO */
static inline u32 task_netprioidx(struct task_struct *p)
{
	return 0;
}

#define sock_update_netprioidx(sk)

#endif /* CONFIG_CGROUP_NET_PRIO */
#endif  /* _NET_CLS_CGROUP_H */
