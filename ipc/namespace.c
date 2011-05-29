/*
 * linux/ipc/namespace.c
 * Copyright (C) 2006 Pavel Emelyanov <xemul@openvz.org> OpenVZ, SWsoft Inc.
 */

#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/ipc_namespace.h>
#include <linux/rcupdate.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/user_namespace.h>

#include "util.h"

static struct ipc_namespace *create_ipc_ns(struct task_struct *tsk,
					   struct ipc_namespace *old_ns)
{
	struct ipc_namespace *ns;
	int err;

	ns = kmalloc(sizeof(struct ipc_namespace), GFP_KERNEL);
	if (ns == NULL)
		return ERR_PTR(-ENOMEM);

	atomic_set(&ns->count, 1);
	err = mq_init_ns(ns);
	if (err) {
		kfree(ns);
		return ERR_PTR(err);
	}
	atomic_inc(&nr_ipc_ns);

	sem_init_ns(ns);
	msg_init_ns(ns);
	shm_init_ns(ns);

	/*
	 * msgmni has already been computed for the new ipc ns.
	 * Thus, do the ipcns creation notification before registering that
	 * new ipcns in the chain.
	 */
	ipcns_notify(IPCNS_CREATED);
	register_ipcns_notifier(ns);

	ns->user_ns = get_user_ns(task_cred_xxx(tsk, user)->user_ns);

	return ns;
}

struct ipc_namespace *copy_ipcs(unsigned long flags,
				struct task_struct *tsk)
{
	struct ipc_namespace *ns = tsk->nsproxy->ipc_ns;

	if (!(flags & CLONE_NEWIPC))
		return get_ipc_ns(ns);
	return create_ipc_ns(tsk, ns);
}

/*
 * free_ipcs - free all ipcs of one type
 * @ns:   the namespace to remove the ipcs from
 * @ids:  the table of ipcs to free
 * @free: the function called to free each individual ipc
 *
 * Called for each kind of ipc when an ipc_namespace exits.
 */
void free_ipcs(struct ipc_namespace *ns, struct ipc_ids *ids,
	       void (*free)(struct ipc_namespace *, struct kern_ipc_perm *))
{
	struct kern_ipc_perm *perm;
	int next_id;
	int total, in_use;

	down_write(&ids->rw_mutex);

	in_use = ids->in_use;

	for (total = 0, next_id = 0; total < in_use; next_id++) {
		perm = idr_find(&ids->ipcs_idr, next_id);
		if (perm == NULL)
			continue;
		ipc_lock_by_ptr(perm);
		free(ns, perm);
		total++;
	}
	up_write(&ids->rw_mutex);
}

static void free_ipc_ns(struct ipc_namespace *ns)
{
	/*
	 * Unregistering the hotplug notifier at the beginning guarantees
	 * that the ipc namespace won't be freed while we are inside the
	 * callback routine. Since the blocking_notifier_chain_XXX routines
	 * hold a rw lock on the notifier list, unregister_ipcns_notifier()
	 * won't take the rw lock before blocking_notifier_call_chain() has
	 * released the rd lock.
	 */
	unregister_ipcns_notifier(ns);
	sem_exit_ns(ns);
	msg_exit_ns(ns);
	shm_exit_ns(ns);
	atomic_dec(&nr_ipc_ns);

	/*
	 * Do the ipcns removal notification after decrementing nr_ipc_ns in
	 * order to have a correct value when recomputing msgmni.
	 */
	ipcns_notify(IPCNS_REMOVED);
	put_user_ns(ns->user_ns);
	kfree(ns);
}

/*
 * put_ipc_ns - drop a reference to an ipc namespace.
 * @ns: the namespace to put
 *
 * If this is the last task in the namespace exiting, and
 * it is dropping the refcount to 0, then it can race with
 * a task in another ipc namespace but in a mounts namespace
 * which has this ipcns's mqueuefs mounted, doing some action
 * with one of the mqueuefs files.  That can raise the refcount.
 * So dropping the refcount, and raising the refcount when
 * accessing it through the VFS, are protected with mq_lock.
 *
 * (Clearly, a task raising the refcount on its own ipc_ns
 * needn't take mq_lock since it can't race with the last task
 * in the ipcns exiting).
 */
void put_ipc_ns(struct ipc_namespace *ns)
{
	if (atomic_dec_and_lock(&ns->count, &mq_lock)) {
		mq_clear_sbinfo(ns);
		spin_unlock(&mq_lock);
		mq_put_mnt(ns);
		free_ipc_ns(ns);
	}
}
