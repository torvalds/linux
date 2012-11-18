#ifdef CONFIG_SCHED_AUTOGROUP

#include "sched.h"

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/utsname.h>
#include <linux/security.h>
#include <linux/export.h>

unsigned int __read_mostly sysctl_sched_autogroup_enabled = 1;
static struct autogroup autogroup_default;
static atomic_t autogroup_seq_nr;

void __init autogroup_init(struct task_struct *init_task)
{
	autogroup_default.tg = &root_task_group;
	kref_init(&autogroup_default.kref);
	init_rwsem(&autogroup_default.lock);
	init_task->signal->autogroup = &autogroup_default;
}

void autogroup_free(struct task_group *tg)
{
	kfree(tg->autogroup);
}

static inline void autogroup_destroy(struct kref *kref)
{
	struct autogroup *ag = container_of(kref, struct autogroup, kref);

#ifdef CONFIG_RT_GROUP_SCHED
	/* We've redirected RT tasks to the root task group... */
	ag->tg->rt_se = NULL;
	ag->tg->rt_rq = NULL;
#endif
	sched_destroy_group(ag->tg);
}

static inline void autogroup_kref_put(struct autogroup *ag)
{
	kref_put(&ag->kref, autogroup_destroy);
}

static inline struct autogroup *autogroup_kref_get(struct autogroup *ag)
{
	kref_get(&ag->kref);
	return ag;
}

static inline struct autogroup *autogroup_task_get(struct task_struct *p)
{
	struct autogroup *ag;
	unsigned long flags;

	if (!lock_task_sighand(p, &flags))
		return autogroup_kref_get(&autogroup_default);

	ag = autogroup_kref_get(p->signal->autogroup);
	unlock_task_sighand(p, &flags);

	return ag;
}

static inline struct autogroup *autogroup_create(void)
{
	struct autogroup *ag = kzalloc(sizeof(*ag), GFP_KERNEL);
	struct task_group *tg;

	if (!ag)
		goto out_fail;

	tg = sched_create_group(&root_task_group);

	if (IS_ERR(tg))
		goto out_free;

	kref_init(&ag->kref);
	init_rwsem(&ag->lock);
	ag->id = atomic_inc_return(&autogroup_seq_nr);
	ag->tg = tg;
#ifdef CONFIG_RT_GROUP_SCHED
	/*
	 * Autogroup RT tasks are redirected to the root task group
	 * so we don't have to move tasks around upon policy change,
	 * or flail around trying to allocate bandwidth on the fly.
	 * A bandwidth exception in __sched_setscheduler() allows
	 * the policy change to proceed.  Thereafter, task_group()
	 * returns &root_task_group, so zero bandwidth is required.
	 */
	free_rt_sched_group(tg);
	tg->rt_se = root_task_group.rt_se;
	tg->rt_rq = root_task_group.rt_rq;
#endif
	tg->autogroup = ag;

	return ag;

out_free:
	kfree(ag);
out_fail:
	if (printk_ratelimit()) {
		printk(KERN_WARNING "autogroup_create: %s failure.\n",
			ag ? "sched_create_group()" : "kmalloc()");
	}

	return autogroup_kref_get(&autogroup_default);
}

bool task_wants_autogroup(struct task_struct *p, struct task_group *tg)
{
	if (!sysctl_sched_autogroup_enabled)
		return false;

	if (tg != &root_task_group)
		return false;

	if (p->sched_class != &fair_sched_class)
		return false;

	/*
	 * We can only assume the task group can't go away on us if
	 * autogroup_move_group() can see us on ->thread_group list.
	 */
	if (p->flags & PF_EXITING)
		return false;

	return true;
}

static void
autogroup_move_group(struct task_struct *p, struct autogroup *ag)
{
	struct autogroup *prev;
	struct task_struct *t;
	unsigned long flags;

	BUG_ON(!lock_task_sighand(p, &flags));

	prev = p->signal->autogroup;
	if (prev == ag) {
		unlock_task_sighand(p, &flags);
		return;
	}

	p->signal->autogroup = autogroup_kref_get(ag);

	t = p;
	do {
		sched_move_task(t);
	} while_each_thread(p, t);

	unlock_task_sighand(p, &flags);
	autogroup_kref_put(prev);
}

/* Allocates GFP_KERNEL, cannot be called under any spinlock */
void sched_autogroup_create_attach(struct task_struct *p)
{
	struct autogroup *ag;

	if (!sysctl_sched_autogroup_enabled)
		return;
	ag = autogroup_create();
	autogroup_move_group(p, ag);
	/* drop extra reference added by autogroup_create() */
	autogroup_kref_put(ag);
}
EXPORT_SYMBOL(sched_autogroup_create_attach);

/* Cannot be called under siglock.  Currently has no users */
void sched_autogroup_detach(struct task_struct *p)
{
	autogroup_move_group(p, &autogroup_default);
}
EXPORT_SYMBOL(sched_autogroup_detach);

void sched_autogroup_fork(struct signal_struct *sig)
{
	if (!sysctl_sched_autogroup_enabled)
		return;
	sig->autogroup = autogroup_task_get(current);
}

void sched_autogroup_exit(struct signal_struct *sig)
{
	if (!sysctl_sched_autogroup_enabled)
		return;
	autogroup_kref_put(sig->autogroup);
}

static int __init setup_autogroup(char *str)
{
	sysctl_sched_autogroup_enabled = 0;

	return 1;
}

__setup("noautogroup", setup_autogroup);

#ifdef CONFIG_SCHED_DEBUG
int autogroup_path(struct task_group *tg, char *buf, int buflen)
{
	if (!task_group_is_autogroup(tg))
		return 0;

	return snprintf(buf, buflen, "%s-%ld", "/autogroup", tg->autogroup->id);
}
#endif /* CONFIG_SCHED_DEBUG */

#endif /* CONFIG_SCHED_AUTOGROUP */
