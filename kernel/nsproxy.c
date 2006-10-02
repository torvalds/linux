/*
 *  Copyright (C) 2006 IBM Corporation
 *
 *  Author: Serge Hallyn <serue@us.ibm.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/nsproxy.h>

static inline void get_nsproxy(struct nsproxy *ns)
{
	atomic_inc(&ns->count);
}

void get_task_namespaces(struct task_struct *tsk)
{
	struct nsproxy *ns = tsk->nsproxy;
	if (ns) {
		get_nsproxy(ns);
	}
}

/*
 * creates a copy of "orig" with refcount 1.
 * This does not grab references to the contained namespaces,
 * so that needs to be done by dup_namespaces.
 */
static inline struct nsproxy *clone_namespaces(struct nsproxy *orig)
{
	struct nsproxy *ns;

	ns = kmalloc(sizeof(struct nsproxy), GFP_KERNEL);
	if (ns) {
		memcpy(ns, orig, sizeof(struct nsproxy));
		atomic_set(&ns->count, 1);
	}
	return ns;
}

/*
 * copies the nsproxy, setting refcount to 1, and grabbing a
 * reference to all contained namespaces.  Called from
 * sys_unshare()
 */
struct nsproxy *dup_namespaces(struct nsproxy *orig)
{
	struct nsproxy *ns = clone_namespaces(orig);

	return ns;
}

/*
 * called from clone.  This now handles copy for nsproxy and all
 * namespaces therein.
 */
int copy_namespaces(int flags, struct task_struct *tsk)
{
	struct nsproxy *old_ns = tsk->nsproxy;

	if (!old_ns)
		return 0;

	get_nsproxy(old_ns);

	return 0;
}

void free_nsproxy(struct nsproxy *ns)
{
		kfree(ns);
}
