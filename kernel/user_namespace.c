/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/highuid.h>
#include <linux/cred.h>

static struct kmem_cache *user_ns_cachep __read_mostly;

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

	ns = kmem_cache_alloc(user_ns_cachep, GFP_KERNEL);
	if (!ns)
		return -ENOMEM;

	kref_init(&ns->kref);

	for (n = 0; n < UIDHASH_SZ; ++n)
		INIT_HLIST_HEAD(ns->uidhash_table + n);

	/* Alloc new root user.  */
	root_user = alloc_uid(ns, 0);
	if (!root_user) {
		kmem_cache_free(user_ns_cachep, ns);
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

	/* root_user holds a reference to ns, our reference can be dropped */
	put_user_ns(ns);

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
	kmem_cache_free(user_ns_cachep, ns);
}

void free_user_ns(struct kref *kref)
{
	struct user_namespace *ns =
		container_of(kref, struct user_namespace, kref);

	INIT_WORK(&ns->destroyer, free_user_ns_work);
	schedule_work(&ns->destroyer);
}
EXPORT_SYMBOL(free_user_ns);

uid_t user_ns_map_uid(struct user_namespace *to, const struct cred *cred, uid_t uid)
{
	struct user_namespace *tmp;

	if (likely(to == cred->user->user_ns))
		return uid;


	/* Is cred->user the creator of the target user_ns
	 * or the creator of one of it's parents?
	 */
	for ( tmp = to; tmp != &init_user_ns;
	      tmp = tmp->creator->user_ns ) {
		if (cred->user == tmp->creator) {
			return (uid_t)0;
		}
	}

	/* No useful relationship so no mapping */
	return overflowuid;
}

gid_t user_ns_map_gid(struct user_namespace *to, const struct cred *cred, gid_t gid)
{
	struct user_namespace *tmp;

	if (likely(to == cred->user->user_ns))
		return gid;

	/* Is cred->user the creator of the target user_ns
	 * or the creator of one of it's parents?
	 */
	for ( tmp = to; tmp != &init_user_ns;
	      tmp = tmp->creator->user_ns ) {
		if (cred->user == tmp->creator) {
			return (gid_t)0;
		}
	}

	/* No useful relationship so no mapping */
	return overflowgid;
}

static __init int user_namespaces_init(void)
{
	user_ns_cachep = KMEM_CACHE(user_namespace, SLAB_PANIC);
	return 0;
}
module_init(user_namespaces_init);
