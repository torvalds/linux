/*
 * ns_cgroup.c - namespace cgroup subsystem
 *
 * Copyright 2006, 2007 IBM Corp
 */

#include <linux/module.h>
#include <linux/cgroup.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/nsproxy.h>

struct ns_cgroup {
	struct cgroup_subsys_state css;
};

struct cgroup_subsys ns_subsys;

static inline struct ns_cgroup *cgroup_to_ns(
		struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, ns_subsys_id),
			    struct ns_cgroup, css);
}

int ns_cgroup_clone(struct task_struct *task, struct pid *pid)
{
	char name[PROC_NUMBUF];

	snprintf(name, PROC_NUMBUF, "%d", pid_vnr(pid));
	return cgroup_clone(task, &ns_subsys, name);
}

/*
 * Rules:
 *   1. you can only enter a cgroup which is a descendant of your current
 *     cgroup
 *   2. you can only place another process into a cgroup if
 *     a. you have CAP_SYS_ADMIN
 *     b. your cgroup is an ancestor of task's destination cgroup
 *       (hence either you are in the same cgroup as task, or in an
 *        ancestor cgroup thereof)
 */
static int ns_can_attach(struct cgroup_subsys *ss, struct cgroup *new_cgroup,
			 struct task_struct *task, bool threadgroup)
{
	if (current != task) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!cgroup_is_descendant(new_cgroup, current))
			return -EPERM;
	}

	if (!cgroup_is_descendant(new_cgroup, task))
		return -EPERM;

	if (threadgroup) {
		struct task_struct *c;
		rcu_read_lock();
		list_for_each_entry_rcu(c, &task->thread_group, thread_group) {
			if (!cgroup_is_descendant(new_cgroup, c)) {
				rcu_read_unlock();
				return -EPERM;
			}
		}
		rcu_read_unlock();
	}

	return 0;
}

/*
 * Rules: you can only create a cgroup if
 *     1. you are capable(CAP_SYS_ADMIN)
 *     2. the target cgroup is a descendant of your own cgroup
 */
static struct cgroup_subsys_state *ns_create(struct cgroup_subsys *ss,
						struct cgroup *cgroup)
{
	struct ns_cgroup *ns_cgroup;

	if (!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);
	if (!cgroup_is_descendant(cgroup, current))
		return ERR_PTR(-EPERM);

	ns_cgroup = kzalloc(sizeof(*ns_cgroup), GFP_KERNEL);
	if (!ns_cgroup)
		return ERR_PTR(-ENOMEM);
	return &ns_cgroup->css;
}

static void ns_destroy(struct cgroup_subsys *ss,
			struct cgroup *cgroup)
{
	struct ns_cgroup *ns_cgroup;

	ns_cgroup = cgroup_to_ns(cgroup);
	kfree(ns_cgroup);
}

struct cgroup_subsys ns_subsys = {
	.name = "ns",
	.can_attach = ns_can_attach,
	.create = ns_create,
	.destroy  = ns_destroy,
	.subsys_id = ns_subsys_id,
};
