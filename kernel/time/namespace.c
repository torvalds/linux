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
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/mm.h>

#include <vdso/datapage.h>

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

	refcount_set(&ns->ns.count, 1);

	ns->vvar_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!ns->vvar_page)
		goto fail_free;

	err = ns_alloc_inum(&ns->ns);
	if (err)
		goto fail_free_page;

	ns->ucounts = ucounts;
	ns->ns.ops = &timens_operations;
	ns->user_ns = get_user_ns(user_ns);
	ns->offsets = old_ns->offsets;
	ns->frozen_offsets = false;
	return ns;

fail_free_page:
	__free_page(ns->vvar_page);
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

static struct timens_offset offset_from_ts(struct timespec64 off)
{
	struct timens_offset ret;

	ret.sec = off.tv_sec;
	ret.nsec = off.tv_nsec;

	return ret;
}

/*
 * A time namespace VVAR page has the same layout as the VVAR page which
 * contains the system wide VDSO data.
 *
 * For a normal task the VVAR pages are installed in the normal ordering:
 *     VVAR
 *     PVCLOCK
 *     HVCLOCK
 *     TIMENS   <- Not really required
 *
 * Now for a timens task the pages are installed in the following order:
 *     TIMENS
 *     PVCLOCK
 *     HVCLOCK
 *     VVAR
 *
 * The check for vdso_data->clock_mode is in the unlikely path of
 * the seq begin magic. So for the non-timens case most of the time
 * 'seq' is even, so the branch is not taken.
 *
 * If 'seq' is odd, i.e. a concurrent update is in progress, the extra check
 * for vdso_data->clock_mode is a non-issue. The task is spin waiting for the
 * update to finish and for 'seq' to become even anyway.
 *
 * Timens page has vdso_data->clock_mode set to VDSO_CLOCKMODE_TIMENS which
 * enforces the time namespace handling path.
 */
static void timens_setup_vdso_data(struct vdso_data *vdata,
				   struct time_namespace *ns)
{
	struct timens_offset *offset = vdata->offset;
	struct timens_offset monotonic = offset_from_ts(ns->offsets.monotonic);
	struct timens_offset boottime = offset_from_ts(ns->offsets.boottime);

	vdata->seq			= 1;
	vdata->clock_mode		= VDSO_CLOCKMODE_TIMENS;
	offset[CLOCK_MONOTONIC]		= monotonic;
	offset[CLOCK_MONOTONIC_RAW]	= monotonic;
	offset[CLOCK_MONOTONIC_COARSE]	= monotonic;
	offset[CLOCK_BOOTTIME]		= boottime;
	offset[CLOCK_BOOTTIME_ALARM]	= boottime;
}

/*
 * Protects possibly multiple offsets writers racing each other
 * and tasks entering the namespace.
 */
static DEFINE_MUTEX(offset_lock);

static void timens_set_vvar_page(struct task_struct *task,
				struct time_namespace *ns)
{
	struct vdso_data *vdata;
	unsigned int i;

	if (ns == &init_time_ns)
		return;

	/* Fast-path, taken by every task in namespace except the first. */
	if (likely(ns->frozen_offsets))
		return;

	mutex_lock(&offset_lock);
	/* Nothing to-do: vvar_page has been already initialized. */
	if (ns->frozen_offsets)
		goto out;

	ns->frozen_offsets = true;
	vdata = arch_get_vdso_data(page_address(ns->vvar_page));

	for (i = 0; i < CS_BASES; i++)
		timens_setup_vdso_data(&vdata[i], ns);

out:
	mutex_unlock(&offset_lock);
}

void free_time_ns(struct time_namespace *ns)
{
	dec_time_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
	__free_page(ns->vvar_page);
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

void timens_commit(struct task_struct *tsk, struct time_namespace *ns)
{
	timens_set_vvar_page(tsk, ns);
	vdso_join_timens(tsk, ns);
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
	struct ns_common *ns;
	struct time_namespace *time_ns;

	ns = timens_for_children_get(p);
	if (!ns)
		return;
	time_ns = to_time_ns(ns);

	show_offset(m, CLOCK_MONOTONIC, &time_ns->offsets.monotonic);
	show_offset(m, CLOCK_BOOTTIME, &time_ns->offsets.boottime);
	put_time_ns(time_ns);
}

int proc_timens_set_offset(struct file *file, struct task_struct *p,
			   struct proc_timens_offset *offsets, int noffsets)
{
	struct ns_common *ns;
	struct time_namespace *time_ns;
	struct timespec64 tp;
	int i, err;

	ns = timens_for_children_get(p);
	if (!ns)
		return -ESRCH;
	time_ns = to_time_ns(ns);

	if (!file_ns_capable(file, time_ns->user_ns, CAP_SYS_TIME)) {
		put_time_ns(time_ns);
		return -EPERM;
	}

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
			err = -EINVAL;
			goto out;
		}

		err = -ERANGE;

		if (off->val.tv_sec > KTIME_SEC_MAX ||
		    off->val.tv_sec < -KTIME_SEC_MAX)
			goto out;

		tp = timespec64_add(tp, off->val);
		/*
		 * KTIME_SEC_MAX is divided by 2 to be sure that KTIME_MAX is
		 * still unreachable.
		 */
		if (tp.tv_sec < 0 || tp.tv_sec > KTIME_SEC_MAX / 2)
			goto out;
	}

	mutex_lock(&offset_lock);
	if (time_ns->frozen_offsets) {
		err = -EACCES;
		goto out_unlock;
	}

	err = 0;
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

out_unlock:
	mutex_unlock(&offset_lock);
out:
	put_time_ns(time_ns);

	return err;
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
	.real_ns_name	= "time",
	.type		= CLONE_NEWTIME,
	.get		= timens_for_children_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

struct time_namespace init_time_ns = {
	.ns.count	= REFCOUNT_INIT(3),
	.user_ns	= &init_user_ns,
	.ns.inum	= PROC_TIME_INIT_INO,
	.ns.ops		= &timens_operations,
	.frozen_offsets	= true,
};

static int __init time_ns_init(void)
{
	return 0;
}
subsys_initcall(time_ns_init);
