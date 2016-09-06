/*
 *  Copyright (C) 2004 IBM Corporation
 *
 *  Author: Serge Hallyn <serue@us.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/export.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/proc_ns.h>

static struct uts_namespace *create_uts_ns(void)
{
	struct uts_namespace *uts_ns;

	uts_ns = kmalloc(sizeof(struct uts_namespace), GFP_KERNEL);
	if (uts_ns)
		kref_init(&uts_ns->kref);
	return uts_ns;
}

/*
 * Clone a new ns copying an original utsname, setting refcount to 1
 * @old_ns: namespace to clone
 * Return ERR_PTR(-ENOMEM) on error (failure to kmalloc), new ns otherwise
 */
static struct uts_namespace *clone_uts_ns(struct user_namespace *user_ns,
					  struct uts_namespace *old_ns)
{
	struct uts_namespace *ns;
	int err;

	ns = create_uts_ns();
	if (!ns)
		return ERR_PTR(-ENOMEM);

	err = ns_alloc_inum(&ns->ns);
	if (err) {
		kfree(ns);
		return ERR_PTR(err);
	}

	ns->ns.ops = &utsns_operations;

	down_read(&uts_sem);
	memcpy(&ns->name, &old_ns->name, sizeof(ns->name));
	ns->user_ns = get_user_ns(user_ns);
	up_read(&uts_sem);
	return ns;
}

/*
 * Copy task tsk's utsname namespace, or clone it if flags
 * specifies CLONE_NEWUTS.  In latter case, changes to the
 * utsname of this process won't be seen by parent, and vice
 * versa.
 */
struct uts_namespace *copy_utsname(unsigned long flags,
	struct user_namespace *user_ns, struct uts_namespace *old_ns)
{
	struct uts_namespace *new_ns;

	BUG_ON(!old_ns);
	get_uts_ns(old_ns);

	if (!(flags & CLONE_NEWUTS))
		return old_ns;

	new_ns = clone_uts_ns(user_ns, old_ns);

	put_uts_ns(old_ns);
	return new_ns;
}

void free_uts_ns(struct kref *kref)
{
	struct uts_namespace *ns;

	ns = container_of(kref, struct uts_namespace, kref);
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
	kfree(ns);
}

static inline struct uts_namespace *to_uts_ns(struct ns_common *ns)
{
	return container_of(ns, struct uts_namespace, ns);
}

static struct ns_common *utsns_get(struct task_struct *task)
{
	struct uts_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->uts_ns;
		get_uts_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static void utsns_put(struct ns_common *ns)
{
	put_uts_ns(to_uts_ns(ns));
}

static int utsns_install(struct nsproxy *nsproxy, struct ns_common *new)
{
	struct uts_namespace *ns = to_uts_ns(new);

	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	get_uts_ns(ns);
	put_uts_ns(nsproxy->uts_ns);
	nsproxy->uts_ns = ns;
	return 0;
}

static struct user_namespace *utsns_owner(struct ns_common *ns)
{
	return to_uts_ns(ns)->user_ns;
}

const struct proc_ns_operations utsns_operations = {
	.name		= "uts",
	.type		= CLONE_NEWUTS,
	.get		= utsns_get,
	.put		= utsns_put,
	.install	= utsns_install,
	.owner		= utsns_owner,
};
