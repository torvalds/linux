#ifdef CONFIG_SCHED_AUTOGROUP

#include <linux/kref.h>
#include <linux/rwsem.h>
#include <linux/sched/autogroup.h>

struct autogroup {
	/*
	 * reference doesn't mean how many thread attach to this
	 * autogroup now. It just stands for the number of task
	 * could use this autogroup.
	 */
	struct kref		kref;
	struct task_group	*tg;
	struct rw_semaphore	lock;
	unsigned long		id;
	int			nice;
};

extern void autogroup_init(struct task_struct *init_task);
extern void autogroup_free(struct task_group *tg);

static inline bool task_group_is_autogroup(struct task_group *tg)
{
	return !!tg->autogroup;
}

extern bool task_wants_autogroup(struct task_struct *p, struct task_group *tg);

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg)
{
	int enabled = READ_ONCE(sysctl_sched_autogroup_enabled);

	if (enabled && task_wants_autogroup(p, tg))
		return p->signal->autogroup->tg;

	return tg;
}

extern int autogroup_path(struct task_group *tg, char *buf, int buflen);

#else /* !CONFIG_SCHED_AUTOGROUP */

static inline void autogroup_init(struct task_struct *init_task) {  }
static inline void autogroup_free(struct task_group *tg) { }
static inline bool task_group_is_autogroup(struct task_group *tg)
{
	return 0;
}

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg)
{
	return tg;
}

#ifdef CONFIG_SCHED_DEBUG
static inline int autogroup_path(struct task_group *tg, char *buf, int buflen)
{
	return 0;
}
#endif

#endif /* CONFIG_SCHED_AUTOGROUP */
