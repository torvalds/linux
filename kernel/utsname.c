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
#include <linux/version.h>

/*
 * Clone a new ns copying an original utsname, setting refcount to 1
 * @old_ns: namespace to clone
 * Return NULL on error (failure to kmalloc), new ns otherwise
 */
static struct uts_namespace *clone_uts_ns(struct uts_namespace *old_ns)
{
	struct uts_namespace *ns;

	ns = kmalloc(sizeof(struct uts_namespace), GFP_KERNEL);
	if (ns) {
		memcpy(&ns->name, &old_ns->name, sizeof(ns->name));
		kref_init(&ns->kref);
	}
	return ns;
}

/*
 * unshare the current process' utsname namespace.
 * called only in sys_unshare()
 */
int unshare_utsname(unsigned long unshare_flags, struct uts_namespace **new_uts)
{
	if (unshare_flags & CLONE_NEWUTS) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		*new_uts = clone_uts_ns(current->nsproxy->uts_ns);
		if (!*new_uts)
			return -ENOMEM;
	}

	return 0;
}

/*
 * Copy task tsk's utsname namespace, or clone it if flags
 * specifies CLONE_NEWUTS.  In latter case, changes to the
 * utsname of this process won't be seen by parent, and vice
 * versa.
 */
int copy_utsname(int flags, struct task_struct *tsk)
{
	struct uts_namespace *old_ns = tsk->nsproxy->uts_ns;
	struct uts_namespace *new_ns;
	int err = 0;

	if (!old_ns)
		return 0;

	get_uts_ns(old_ns);

	if (!(flags & CLONE_NEWUTS))
		return 0;

	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto out;
	}

	new_ns = clone_uts_ns(old_ns);
	if (!new_ns) {
		err = -ENOMEM;
		goto out;
	}
	tsk->nsproxy->uts_ns = new_ns;

out:
	put_uts_ns(old_ns);
	return err;
}

void free_uts_ns(struct kref *kref)
{
	struct uts_namespace *ns;

	ns = container_of(kref, struct uts_namespace, kref);
	kfree(ns);
}
