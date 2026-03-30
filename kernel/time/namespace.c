// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Andrei Vagin <avagin@openvz.org>
 * Author: Dmitry Safonov <dima@arista.com>
 */

#include <linux/time_namespace.h>
#include <linux/user_namespace.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/clocksource.h>
#include <linux/seq_file.h>
#include <linux/proc_ns.h>
#include <linux/export.h>
#include <linux/nstree.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/cleanup.h>

#include "namespace_internal.h"

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
	ns = kzalloc_obj(*ns, GFP_KERNEL_ACCOUNT);
	if (!ns)
		goto fail_dec;

	err = timens_vdso_alloc_vvar_page(ns);
	if (err)
		goto fail_free;

	err = ns_common_init(ns);
	if (err)
		goto fail_free_page;

	ns->ucounts = ucounts;
	ns->user_ns = get_user_ns(user_ns);
	ns->offsets = old_ns->offsets;
	ns->frozen_offsets = false;
	ns_tree_add(ns);
	return ns;

fail_free_page:
	timens_vdso_free_vvar_page(ns);
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
struct time_namespace *copy_time_ns(u64 flags,
	struct user_namespace *user_ns, struct time_namespace *old_ns)
{
	if (!(flags & CLONE_NEWTIME))
		return get_time_ns(old_ns);

	return clone_time_ns(user_ns, old_ns);
}

DEFINE_MUTEX(timens_offset_lock);

void free_time_ns(struct time_namespace *ns)
{
	ns_tree_remove(ns);
	dec_time_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	ns_common_free(ns);
	timens_vdso_free_vvar_page(ns);
	/* Concurrent nstree traversal depends on a grace period. */
	kfree_rcu(ns, ns.ns_rcu);
}

static struct ns_common *timens_get(struct task_struct *task)
{
	struct time_namespace *ns;
	struct nsproxy *nsproxy;

	guard(task_lock)(task);
	nsproxy = task->nsproxy;
	if (!nsproxy)
		return NULL;

	ns = nsproxy->time_ns;
	get_time_ns(ns);
	return &ns->ns;
}

static struct ns_common *timens_for_children_get(struct task_struct *task)
{
	struct time_namespace *ns;
	struct nsproxy *nsproxy;

	guard(task_lock)(task);
	nsproxy = task->nsproxy;
	if (!nsproxy)
		return NULL;

	ns = nsproxy->time_ns_for_children;
	get_time_ns(ns);
	return &ns->ns;
}

static void timens_put(struct ns_common *ns)
{
	put_time_ns(to_time_ns(ns));
}

static int timens_install(struct nsset *nsset, struct ns_common *new)
{
	struct nsproxy *nsproxy = nsset->nsproxy;
	struct time_namespace *ns = to_time_ns(new);

	if (!current_is_single_threaded())
		return -EUSERS;

	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(nsset->cred->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns);
	nsproxy->time_ns = ns;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns_for_children);
	nsproxy->time_ns_for_children = ns;
	return 0;
}

void timens_on_fork(struct nsproxy *nsproxy, struct task_struct *tsk)
{
	struct ns_common *nsc = &nsproxy->time_ns_for_children->ns;
	struct time_namespace *ns = to_time_ns(nsc);

	/* create_new_namespaces() already incremented the ref counter */
	if (nsproxy->time_ns == nsproxy->time_ns_for_children)
		return;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns);
	nsproxy->time_ns = ns;

	timens_commit(tsk, ns);
}

static struct user_namespace *timens_owner(struct ns_common *ns)
{
	return to_time_ns(ns)->user_ns;
}

static void show_offset(struct seq_file *m, int clockid, struct timespec64 *ts)
{
	char *clock;

	switch (clockid) {
	case CLOCK_BOOTTIME:
		clock = "boottime";
		break;
	case CLOCK_MONOTONIC:
		clock = "monotonic";
		break;
	default:
		clock = "unknown";
		break;
	}
	seq_printf(m, "%-10s %10lld %9ld\n", clock, ts->tv_sec, ts->tv_nsec);
}

void proc_timens_show_offsets(struct task_struct *p, struct seq_file *m)
{
	struct time_namespace *time_ns __free(time_ns) = NULL;
	struct ns_common *ns = timens_for_children_get(p);

	if (!ns)
		return;

	time_ns = to_time_ns(ns);

	show_offset(m, CLOCK_MONOTONIC, &time_ns->offsets.monotonic);
	show_offset(m, CLOCK_BOOTTIME, &time_ns->offsets.boottime);
}

int proc_timens_set_offset(struct file *file, struct task_struct *p,
			   struct proc_timens_offset *offsets, int noffsets)
{
	struct time_namespace *time_ns __free(time_ns) = NULL;
	struct ns_common *ns = timens_for_children_get(p);
	struct timespec64 tp;
	int i;

	if (!ns)
		return -ESRCH;

	time_ns = to_time_ns(ns);

	if (!file_ns_capable(file, time_ns->user_ns, CAP_SYS_TIME))
		return -EPERM;

	for (i = 0; i < noffsets; i++) {
		struct proc_timens_offset *off = &offsets[i];

		switch (off->clockid) {
		case CLOCK_MONOTONIC:
			ktime_get_ts64(&tp);
			break;
		case CLOCK_BOOTTIME:
			ktime_get_boottime_ts64(&tp);
			break;
		default:
			return -EINVAL;
		}

		if (off->val.tv_sec > KTIME_SEC_MAX ||
		    off->val.tv_sec < -KTIME_SEC_MAX)
			return -ERANGE;

		tp = timespec64_add(tp, off->val);
		/*
		 * KTIME_SEC_MAX is divided by 2 to be sure that KTIME_MAX is
		 * still unreachable.
		 */
		if (tp.tv_sec < 0 || tp.tv_sec > KTIME_SEC_MAX / 2)
			return -ERANGE;
	}

	guard(mutex)(&timens_offset_lock);
	if (time_ns->frozen_offsets)
		return -EACCES;

	/* Don't report errors after this line */
	for (i = 0; i < noffsets; i++) {
		struct proc_timens_offset *off = &offsets[i];
		struct timespec64 *offset = NULL;

		switch (off->clockid) {
		case CLOCK_MONOTONIC:
			offset = &time_ns->offsets.monotonic;
			break;
		case CLOCK_BOOTTIME:
			offset = &time_ns->offsets.boottime;
			break;
		}

		*offset = off->val;
	}

	return 0;
}

const struct proc_ns_operations timens_operations = {
	.name		= "time",
	.get		= timens_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

const struct proc_ns_operations timens_for_children_operations = {
	.name		= "time_for_children",
	.real_ns_name	= "time",
	.get		= timens_for_children_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

struct time_namespace init_time_ns = {
	.ns		= NS_COMMON_INIT(init_time_ns),
	.user_ns	= &init_user_ns,
	.frozen_offsets	= true,
};

void __init time_ns_init(void)
{
	ns_tree_add(&init_time_ns);
}
