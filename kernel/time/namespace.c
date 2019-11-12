// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Andrei Vagin <avagin@openvz.org>
 * Author: Dmitry Safonov <dima@arista.com>
 */

#include <linux/time_namespace.h>
#include <linux/user_namespace.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/proc_ns.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/mm.h>

ktime_t do_timens_ktime_to_host(clockid_t clockid, ktime_t tim,
				struct timens_offsets *ns_offsets)
{
	ktime_t offset;

	switch (clockid) {
	case CLOCK_MONOTONIC:
		offset = timespec64_to_ktime(ns_offsets->monotonic);
		break;
	case CLOCK_BOOTTIME:
	case CLOCK_BOOTTIME_ALARM:
		offset = timespec64_to_ktime(ns_offsets->boottime);
		break;
	default:
		return tim;
	}

	/*
	 * Check that @tim value is in [offset, KTIME_MAX + offset]
	 * and subtract offset.
	 */
	if (tim < offset) {
		/*
		 * User can specify @tim *absolute* value - if it's lesser than
		 * the time namespace's offset - it's already expired.
		 */
		tim = 0;
	} else {
		tim = ktime_sub(tim, offset);
		if (unlikely(tim > KTIME_MAX))
			tim = KTIME_MAX;
	}

	return tim;
}

static struct ucounts *inc_time_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_TIME_NAMESPACES);
}

static void dec_time_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_TIME_NAMESPACES);
}

/**
 * clone_time_ns - Clone a time namespace
 * @user_ns:	User namespace which owns a new namespace.
 * @old_ns:	Namespace to clone
 *
 * Clone @old_ns and set the clone refcount to 1
 *
 * Return: The new namespace or ERR_PTR.
 */
static struct time_namespace *clone_time_ns(struct user_namespace *user_ns,
					  struct time_namespace *old_ns)
{
	struct time_namespace *ns;
	struct ucounts *ucounts;
	int err;

	err = -ENOSPC;
	ucounts = inc_time_namespaces(user_ns);
	if (!ucounts)
		goto fail;

	err = -ENOMEM;
	ns = kmalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		goto fail_dec;

	kref_init(&ns->kref);

	err = ns_alloc_inum(&ns->ns);
	if (err)
		goto fail_free;

	ns->ucounts = ucounts;
	ns->ns.ops = &timens_operations;
	ns->user_ns = get_user_ns(user_ns);
	ns->offsets = old_ns->offsets;
	return ns;

fail_free:
	kfree(ns);
fail_dec:
	dec_time_namespaces(ucounts);
fail:
	return ERR_PTR(err);
}

/**
 * copy_time_ns - Create timens_for_children from @old_ns
 * @flags:	Cloning flags
 * @user_ns:	User namespace which owns a new namespace.
 * @old_ns:	Namespace to clone
 *
 * If CLONE_NEWTIME specified in @flags, creates a new timens_for_children;
 * adds a refcounter to @old_ns otherwise.
 *
 * Return: timens_for_children namespace or ERR_PTR.
 */
struct time_namespace *copy_time_ns(unsigned long flags,
	struct user_namespace *user_ns, struct time_namespace *old_ns)
{
	if (!(flags & CLONE_NEWTIME))
		return get_time_ns(old_ns);

	return clone_time_ns(user_ns, old_ns);
}

void free_time_ns(struct kref *kref)
{
	struct time_namespace *ns;

	ns = container_of(kref, struct time_namespace, kref);
	dec_time_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
	kfree(ns);
}

static struct time_namespace *to_time_ns(struct ns_common *ns)
{
	return container_of(ns, struct time_namespace, ns);
}

static struct ns_common *timens_get(struct task_struct *task)
{
	struct time_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->time_ns;
		get_time_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static struct ns_common *timens_for_children_get(struct task_struct *task)
{
	struct time_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->time_ns_for_children;
		get_time_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static void timens_put(struct ns_common *ns)
{
	put_time_ns(to_time_ns(ns));
}

static int timens_install(struct nsproxy *nsproxy, struct ns_common *new)
{
	struct time_namespace *ns = to_time_ns(new);

	if (!current_is_single_threaded())
		return -EUSERS;

	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns);
	nsproxy->time_ns = ns;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns_for_children);
	nsproxy->time_ns_for_children = ns;
	return 0;
}

int timens_on_fork(struct nsproxy *nsproxy, struct task_struct *tsk)
{
	struct ns_common *nsc = &nsproxy->time_ns_for_children->ns;
	struct time_namespace *ns = to_time_ns(nsc);

	/* create_new_namespaces() already incremented the ref counter */
	if (nsproxy->time_ns == nsproxy->time_ns_for_children)
		return 0;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns);
	nsproxy->time_ns = ns;

	return 0;
}

static struct user_namespace *timens_owner(struct ns_common *ns)
{
	return to_time_ns(ns)->user_ns;
}

const struct proc_ns_operations timens_operations = {
	.name		= "time",
	.type		= CLONE_NEWTIME,
	.get		= timens_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

const struct proc_ns_operations timens_for_children_operations = {
	.name		= "time_for_children",
	.type		= CLONE_NEWTIME,
	.get		= timens_for_children_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

struct time_namespace init_time_ns = {
	.kref		= KREF_INIT(3),
	.user_ns	= &init_user_ns,
	.ns.inum	= PROC_TIME_INIT_INO,
	.ns.ops		= &timens_operations,
};

static int __init time_ns_init(void)
{
	return 0;
}
subsys_initcall(time_ns_init);
