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
 * Copy task tsk's utsname namespace, or clone it if flags
 * specifies CLONE_NEWUTS.  In latter case, changes to the
 * utsname of this process won't be seen by parent, and vice
 * versa.
 */
int copy_utsname(int flags, struct task_struct *tsk)
{
	struct uts_namespace *old_ns = tsk->nsproxy->uts_ns;
	int err = 0;

	if (!old_ns)
		return 0;

	get_uts_ns(old_ns);

	return err;
}

void free_uts_ns(struct kref *kref)
{
	struct uts_namespace *ns;

	ns = container_of(kref, struct uts_namespace, kref);
	kfree(ns);
}
