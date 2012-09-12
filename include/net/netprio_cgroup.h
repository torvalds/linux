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


#if IS_ENABLED(CONFIG_NETPRIO_CGROUP)
struct netprio_map {
	struct rcu_head rcu;
	u32 priomap_len;
	u32 priomap[];
};

struct cgroup_netprio_state {
	struct cgroup_subsys_state css;
	u32 prioidx;
};

#ifndef CONFIG_NETPRIO_CGROUP
extern int net_prio_subsys_id;
#endif

extern void sock_update_netprioidx(struct sock *sk, struct task_struct *task);

#if IS_BUILTIN(CONFIG_NETPRIO_CGROUP)

static inline u32 task_netprioidx(struct task_struct *p)
{
	struct cgroup_netprio_state *state;
	u32 idx;

	rcu_read_lock();
	state = container_of(task_subsys_state(p, net_prio_subsys_id),
			     struct cgroup_netprio_state, css);
	idx = state->prioidx;
	rcu_read_unlock();
	return idx;
}

#elif IS_MODULE(CONFIG_NETPRIO_CGROUP)

static inline u32 task_netprioidx(struct task_struct *p)
{
	struct cgroup_netprio_state *state;
	int subsys_id;
	u32 idx = 0;

	rcu_read_lock();
	subsys_id = rcu_dereference_index_check(net_prio_subsys_id,
						rcu_read_lock_held());
	if (subsys_id >= 0) {
		state = container_of(task_subsys_state(p, subsys_id),
				     struct cgroup_netprio_state, css);
		idx = state->prioidx;
	}
	rcu_read_unlock();
	return idx;
}
#endif

#else /* !CONFIG_NETPRIO_CGROUP */

static inline u32 task_netprioidx(struct task_struct *p)
{
	return 0;
}

#define sock_update_netprioidx(sk, task)

#endif /* CONFIG_NETPRIO_CGROUP */

#endif  /* _NET_CLS_CGROUP_H */
