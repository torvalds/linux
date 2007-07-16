/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>

struct user_namespace init_user_ns = {
	.kref = {
		.refcount	= ATOMIC_INIT(2),
	},
	.root_user = &root_user,
};

EXPORT_SYMBOL_GPL(init_user_ns);

#ifdef CONFIG_USER_NS

struct user_namespace * copy_user_ns(int flags, struct user_namespace *old_ns)
{
	struct user_namespace *new_ns;

	BUG_ON(!old_ns);
	get_user_ns(old_ns);

	new_ns = old_ns;
	return new_ns;
}

void free_user_ns(struct kref *kref)
{
	struct user_namespace *ns;

	ns = container_of(kref, struct user_namespace, kref);
	kfree(ns);
}

#endif /* CONFIG_USER_NS */
