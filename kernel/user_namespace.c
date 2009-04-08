/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/module.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/cred.h>

/*
 * Create a new user namespace, deriving the creator from the user in the
 * passed credentials, and replacing that user with the new root user for the
 * new namespace.
 *
 * This is called by copy_creds(), which will finish setting the target task's
 * credentials.
 */
int create_user_ns(struct cred *new)
{
	struct user_namespace *ns;
	struct user_struct *root_user;
	int n;

	ns = kmalloc(sizeof(struct user_namespace), GFP_KERNEL);
	if (!ns)
		return -ENOMEM;

	kref_init(&ns->kref);

	for (n = 0; n < UIDHASH_SZ; ++n)
		INIT_HLIST_HEAD(ns->uidhash_table + n);

	/* Alloc new root user.  */
	root_user = alloc_uid(ns, 0);
	if (!root_user) {
		kfree(ns);
		return -ENOMEM;
	}

	/* set the new root user in the credentials under preparation */
	ns->creator = new->user;
	new->user = root_user;
	new->uid = new->euid = new->suid = new->fsuid = 0;
	new->gid = new->egid = new->sgid = new->fsgid = 0;
	put_group_info(new->group_info);
	new->group_info = get_group_info(&init_groups);
#ifdef CONFIG_KEYS
	key_put(new->request_key_auth);
	new->request_key_auth = NULL;
#endif
	/* tgcred will be cleared in our caller bc CLONE_THREAD won't be set */

	/* alloc_uid() incremented the userns refcount.  Just set it to 1 */
	kref_set(&ns->kref, 1);

	return 0;
}

/*
 * Deferred destructor for a user namespace.  This is required because
 * free_user_ns() may be called with uidhash_lock held, but we need to call
 * back to free_uid() which will want to take the lock again.
 */
static void free_user_ns_work(struct work_struct *work)
{
	struct user_namespace *ns =
		container_of(work, struct user_namespace, destroyer);
	free_uid(ns->creator);
	kfree(ns);
}

void free_user_ns(struct kref *kref)
{
	struct user_namespace *ns =
		container_of(kref, struct user_namespace, kref);

	INIT_WORK(&ns->destroyer, free_user_ns_work);
	schedule_work(&ns->destroyer);
}
EXPORT_SYMBOL(free_user_ns);
