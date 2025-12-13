// SPDX-License-Identifier: GPL-2.0
#include "cgroup-internal.h"

#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>
#include <linux/nstree.h>

/* cgroup namespaces */

static struct ucounts *inc_cgroup_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_CGROUP_NAMESPACES);
}

static void dec_cgroup_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_CGROUP_NAMESPACES);
}

static struct cgroup_namespace *alloc_cgroup_ns(void)
{
	struct cgroup_namespace *new_ns __free(kfree) = NULL;
	int ret;

	new_ns = kzalloc(sizeof(struct cgroup_namespace), GFP_KERNEL_ACCOUNT);
	if (!new_ns)
		return ERR_PTR(-ENOMEM);
	ret = ns_common_init(new_ns);
	if (ret)
		return ERR_PTR(ret);
	ns_tree_add(new_ns);
	return no_free_ptr(new_ns);
}

void free_cgroup_ns(struct cgroup_namespace *ns)
{
	ns_tree_remove(ns);
	put_css_set(ns->root_cset);
	dec_cgroup_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	ns_common_free(ns);
	/* Concurrent nstree traversal depends on a grace period. */
	kfree_rcu(ns, ns.ns_rcu);
}
EXPORT_SYMBOL(free_cgroup_ns);

struct cgroup_namespace *copy_cgroup_ns(u64 flags,
					struct user_namespace *user_ns,
					struct cgroup_namespace *old_ns)
{
	struct cgroup_namespace *new_ns;
	struct ucounts *ucounts;
	struct css_set *cset;

	BUG_ON(!old_ns);

	if (!(flags & CLONE_NEWCGROUP)) {
		get_cgroup_ns(old_ns);
		return old_ns;
	}

	/* Allow only sysadmin to create cgroup namespace. */
	if (!ns_capable(user_ns, CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	ucounts = inc_cgroup_namespaces(user_ns);
	if (!ucounts)
		return ERR_PTR(-ENOSPC);

	/* It is not safe to take cgroup_mutex here */
	spin_lock_irq(&css_set_lock);
	cset = task_css_set(current);
	get_css_set(cset);
	spin_unlock_irq(&css_set_lock);

	new_ns = alloc_cgroup_ns();
	if (IS_ERR(new_ns)) {
		put_css_set(cset);
		dec_cgroup_namespaces(ucounts);
		return new_ns;
	}

	new_ns->user_ns = get_user_ns(user_ns);
	new_ns->ucounts = ucounts;
	new_ns->root_cset = cset;

	return new_ns;
}

static int cgroupns_install(struct nsset *nsset, struct ns_common *ns)
{
	struct nsproxy *nsproxy = nsset->nsproxy;
	struct cgroup_namespace *cgroup_ns = to_cg_ns(ns);

	if (!ns_capable(nsset->cred->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(cgroup_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/* Don't need to do anything if we are attaching to our own cgroupns. */
	if (cgroup_ns == nsproxy->cgroup_ns)
		return 0;

	get_cgroup_ns(cgroup_ns);
	put_cgroup_ns(nsproxy->cgroup_ns);
	nsproxy->cgroup_ns = cgroup_ns;

	return 0;
}

static struct ns_common *cgroupns_get(struct task_struct *task)
{
	struct cgroup_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->cgroup_ns;
		get_cgroup_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static void cgroupns_put(struct ns_common *ns)
{
	put_cgroup_ns(to_cg_ns(ns));
}

static struct user_namespace *cgroupns_owner(struct ns_common *ns)
{
	return to_cg_ns(ns)->user_ns;
}

const struct proc_ns_operations cgroupns_operations = {
	.name		= "cgroup",
	.get		= cgroupns_get,
	.put		= cgroupns_put,
	.install	= cgroupns_install,
	.owner		= cgroupns_owner,
};
