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

#if IS_ENABLED(CONFIG_NET_CLS_CGROUP)
struct cgroup_cls_state
{
	struct cgroup_subsys_state css;
	u32 classid;
};

extern void sock_update_classid(struct sock *sk, struct task_struct *task);

#if IS_BUILTIN(CONFIG_NET_CLS_CGROUP)
static inline u32 task_cls_classid(struct task_struct *p)
{
	u32 classid;

	if (in_interrupt())
		return 0;

	rcu_read_lock();
	classid = container_of(task_subsys_state(p, net_cls_subsys_id),
			       struct cgroup_cls_state, css)->classid;
	rcu_read_unlock();

	return classid;
}
#elif IS_MODULE(CONFIG_NET_CLS_CGROUP)
static inline u32 task_cls_classid(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	u32 classid = 0;

	if (in_interrupt())
		return 0;

	rcu_read_lock();
	css = task_subsys_state(p, net_cls_subsys_id);
	if (css)
		classid = container_of(css,
				       struct cgroup_cls_state, css)->classid;
	rcu_read_unlock();

	return classid;
}
#endif
#else /* !CGROUP_NET_CLS_CGROUP */
static inline void sock_update_classid(struct sock *sk, struct task_struct *task)
{
}

static inline u32 task_cls_classid(struct task_struct *p)
{
	return 0;
}
#endif /* CGROUP_NET_CLS_CGROUP */
#endif  /* _NET_CLS_CGROUP_H */
