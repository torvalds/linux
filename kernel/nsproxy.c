/*
 *  Copyright (C) 2006 IBM Corporation
 *
 *  Author: Serge Hallyn <serue@us.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 *
 *  Jun 2006 - namespaces support
 *             OpenVZ, SWsoft Inc.
 *             Pavel Emelianov <xemul@openvz.org>
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/nsproxy.h>
#include <linux/init_task.h>
#include <linux/mnt_namespace.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <net/net_namespace.h>

static struct kmem_cache *nsproxy_cachep;

struct nsproxy init_nsproxy = INIT_NSPROXY(init_nsproxy);

/*
 * creates a copy of "orig" with refcount 1.
 */
static inline struct nsproxy *clone_nsproxy(struct nsproxy *orig)
{
	struct nsproxy *ns;

	ns = kmem_cache_alloc(nsproxy_cachep, GFP_KERNEL);
	if (ns) {
		memcpy(ns, orig, sizeof(struct nsproxy));
		atomic_set(&ns->count, 1);
	}
	return ns;
}

/*
 * Create new nsproxy and all of its the associated namespaces.
 * Return the newly created nsproxy.  Do not attach this to the task,
 * leave it to the caller to do proper locking and attach it to task.
 */
static struct nsproxy *create_new_namespaces(unsigned long flags,
			struct task_struct *tsk, struct fs_struct *new_fs)
{
	struct nsproxy *new_nsp;
	int err;

	new_nsp = clone_nsproxy(tsk->nsproxy);
	if (!new_nsp)
		return ERR_PTR(-ENOMEM);

	new_nsp->mnt_ns = copy_mnt_ns(flags, tsk->nsproxy->mnt_ns, new_fs);
	if (IS_ERR(new_nsp->mnt_ns)) {
		err = PTR_ERR(new_nsp->mnt_ns);
		goto out_ns;
	}

	new_nsp->uts_ns = copy_utsname(flags, tsk->nsproxy->uts_ns);
	if (IS_ERR(new_nsp->uts_ns)) {
		err = PTR_ERR(new_nsp->uts_ns);
		goto out_uts;
	}

	new_nsp->ipc_ns = copy_ipcs(flags, tsk->nsproxy->ipc_ns);
	if (IS_ERR(new_nsp->ipc_ns)) {
		err = PTR_ERR(new_nsp->ipc_ns);
		goto out_ipc;
	}

	new_nsp->pid_ns = copy_pid_ns(flags, task_active_pid_ns(tsk));
	if (IS_ERR(new_nsp->pid_ns)) {
		err = PTR_ERR(new_nsp->pid_ns);
		goto out_pid;
	}

	new_nsp->user_ns = copy_user_ns(flags, tsk->nsproxy->user_ns);
	if (IS_ERR(new_nsp->user_ns)) {
		err = PTR_ERR(new_nsp->user_ns);
		goto out_user;
	}

	new_nsp->net_ns = copy_net_ns(flags, tsk->nsproxy->net_ns);
	if (IS_ERR(new_nsp->net_ns)) {
		err = PTR_ERR(new_nsp->net_ns);
		goto out_net;
	}

	return new_nsp;

out_net:
	if (new_nsp->user_ns)
		put_user_ns(new_nsp->user_ns);
out_user:
	if (new_nsp->pid_ns)
		put_pid_ns(new_nsp->pid_ns);
out_pid:
	if (new_nsp->ipc_ns)
		put_ipc_ns(new_nsp->ipc_ns);
out_ipc:
	if (new_nsp->uts_ns)
		put_uts_ns(new_nsp->uts_ns);
out_uts:
	if (new_nsp->mnt_ns)
		put_mnt_ns(new_nsp->mnt_ns);
out_ns:
	kmem_cache_free(nsproxy_cachep, new_nsp);
	return ERR_PTR(err);
}

/*
 * called from clone.  This now handles copy for nsproxy and all
 * namespaces therein.
 */
int copy_namespaces(unsigned long flags, struct task_struct *tsk)
{
	struct nsproxy *old_ns = tsk->nsproxy;
	struct nsproxy *new_ns;
	int err = 0;

	if (!old_ns)
		return 0;

	get_nsproxy(old_ns);

	if (!(flags & (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWUSER | CLONE_NEWNET)))
		return 0;

	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto out;
	}

	new_ns = create_new_namespaces(flags, tsk, tsk->fs);
	if (IS_ERR(new_ns)) {
		err = PTR_ERR(new_ns);
		goto out;
	}

	err = ns_cgroup_clone(tsk);
	if (err) {
		put_nsproxy(new_ns);
		goto out;
	}

	tsk->nsproxy = new_ns;

out:
	put_nsproxy(old_ns);
	return err;
}

void free_nsproxy(struct nsproxy *ns)
{
	if (ns->mnt_ns)
		put_mnt_ns(ns->mnt_ns);
	if (ns->uts_ns)
		put_uts_ns(ns->uts_ns);
	if (ns->ipc_ns)
		put_ipc_ns(ns->ipc_ns);
	if (ns->pid_ns)
		put_pid_ns(ns->pid_ns);
	if (ns->user_ns)
		put_user_ns(ns->user_ns);
	put_net(ns->net_ns);
	kmem_cache_free(nsproxy_cachep, ns);
}

/*
 * Called from unshare. Unshare all the namespaces part of nsproxy.
 * On success, returns the new nsproxy.
 */
int unshare_nsproxy_namespaces(unsigned long unshare_flags,
		struct nsproxy **new_nsp, struct fs_struct *new_fs)
{
	int err = 0;

	if (!(unshare_flags & (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
			       CLONE_NEWUSER | CLONE_NEWNET)))
		return 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	*new_nsp = create_new_namespaces(unshare_flags, current,
				new_fs ? new_fs : current->fs);
	if (IS_ERR(*new_nsp)) {
		err = PTR_ERR(*new_nsp);
		goto out;
	}

	err = ns_cgroup_clone(current);
	if (err)
		put_nsproxy(*new_nsp);

out:
	return err;
}

void switch_task_namespaces(struct task_struct *p, struct nsproxy *new)
{
	struct nsproxy *ns;

	might_sleep();

	ns = p->nsproxy;

	rcu_assign_pointer(p->nsproxy, new);

	if (ns && atomic_dec_and_test(&ns->count)) {
		/*
		 * wait for others to get what they want from this nsproxy.
		 *
		 * cannot release this nsproxy via the call_rcu() since
		 * put_mnt_ns() will want to sleep
		 */
		synchronize_rcu();
		free_nsproxy(ns);
	}
}

void exit_task_namespaces(struct task_struct *p)
{
	switch_task_namespaces(p, NULL);
}

static int __init nsproxy_cache_init(void)
{
	nsproxy_cachep = KMEM_CACHE(nsproxy, SLAB_PANIC);
	return 0;
}

module_init(nsproxy_cache_init);
