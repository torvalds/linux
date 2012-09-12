/*
 * cls_cgroup.h			Control Group Classifier
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef _NET_CLS_CGROUP_H
#define _NET_CLS_CGROUP_H

#include <linux/cgroup.h>
#include <linux/hardirq.h>
#include <linux/rcupdate.h>

#ifdef CONFIG_CGROUPS
struct cgroup_cls_state
{
	struct cgroup_subsys_state css;
	u32 classid;
};

extern void sock_update_classid(struct sock *sk);

#ifdef CONFIG_NET_CLS_CGROUP
static inline u32 task_cls_classid(struct task_struct *p)
{
	int classid;

	if (in_interrupt())
		return 0;

	rcu_read_lock();
	classid = container_of(task_subsys_state(p, net_cls_subsys_id),
			       struct cgroup_cls_state, css)->classid;
	rcu_read_unlock();

	return classid;
}
#else
extern int net_cls_subsys_id;

static inline u32 task_cls_classid(struct task_struct *p)
{
	int id;
	u32 classid = 0;

	if (in_interrupt())
		return 0;

	rcu_read_lock();
	id = rcu_dereference_index_check(net_cls_subsys_id,
					 rcu_read_lock_held());
	if (id >= 0)
		classid = container_of(task_subsys_state(p, id),
				       struct cgroup_cls_state, css)->classid;
	rcu_read_unlock();

	return classid;
}
#endif
#else
static inline void sock_update_classid(struct sock *sk)
{
}

static inline u32 task_cls_classid(struct task_struct *p)
{
	return 0;
}
#endif
#endif  /* _NET_CLS_CGROUP_H */
