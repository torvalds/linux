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

#include <linux/module.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>

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
 * Return NULL on error (failure to kmalloc), new ns otherwise
 */
static struct uts_namespace *clone_uts_ns(struct task_struct *tsk,
					  struct uts_namespace *old_ns)
{
	struct uts_namespace *ns;

	ns = create_uts_ns();
	if (!ns)
		return ERR_PTR(-ENOMEM);

	down_read(&uts_sem);
	memcpy(&ns->name, &old_ns->name, sizeof(ns->name));
	ns->user_ns = get_user_ns(task_cred_xxx(tsk, user)->user_ns);
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
				   struct task_struct *tsk)
{
	struct uts_namespace *old_ns = tsk->nsproxy->uts_ns;
	struct uts_namespace *new_ns;

	BUG_ON(!old_ns);
	get_uts_ns(old_ns);

	if (!(flags & CLONE_NEWUTS))
		return old_ns;

	new_ns = clone_uts_ns(tsk, old_ns);

	put_uts_ns(old_ns);
	return new_ns;
}

void free_uts_ns(struct kref *kref)
{
	struct uts_namespace *ns;

	ns = container_of(kref, struct uts_namespace, kref);
	put_user_ns(ns->user_ns);
	kfree(ns);
}
