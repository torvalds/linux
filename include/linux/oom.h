#ifndef __INCLUDE_LINUX_OOM_H
#define __INCLUDE_LINUX_OOM_H


#include <linux/sched.h>
#include <linux/types.h>
#include <linux/nodemask.h>
#include <uapi/linux/oom.h>

struct zonelist;
struct notifier_block;
struct mem_cgroup;
struct task_struct;

/*
 * Details of the page allocation that triggered the oom killer that are used to
 * determine what should be killed.
 */
struct oom_control {
	/* Used to determine cpuset */
	struct zonelist *zonelist;

	/* Used to determine mempolicy */
	nodemask_t *nodemask;

	/* Memory cgroup in which oom is invoked, or NULL for global oom */
	struct mem_cgroup *memcg;

	/* Used to determine cpuset and node locality requirement */
	const gfp_t gfp_mask;

	/*
	 * order == -1 means the oom kill is required by sysrq, otherwise only
	 * for display purposes.
	 */
	const int order;
};

/*
 * Types of limitations to the nodes from which allocations may occur
 */
enum oom_constraint {
	CONSTRAINT_NONE,
	CONSTRAINT_CPUSET,
	CONSTRAINT_MEMORY_POLICY,
	CONSTRAINT_MEMCG,
};

enum oom_scan_t {
	OOM_SCAN_OK,		/* scan thread and find its badness */
	OOM_SCAN_CONTINUE,	/* do not consider thread for oom kill */
	OOM_SCAN_ABORT,		/* abort the iteration and return */
	OOM_SCAN_SELECT,	/* always select this thread first */
};

extern struct mutex oom_lock;

static inline void set_current_oom_origin(void)
{
	current->signal->oom_flag_origin = true;
}

static inline void clear_current_oom_origin(void)
{
	current->signal->oom_flag_origin = false;
}

static inline bool oom_task_origin(const struct task_struct *p)
{
	return p->signal->oom_flag_origin;
}

extern void mark_oom_victim(struct task_struct *tsk);

#ifdef CONFIG_MMU
extern void wake_oom_reaper(struct task_struct *tsk);
#else
static inline void wake_oom_reaper(struct task_struct *tsk)
{
}
#endif

extern unsigned long oom_badness(struct task_struct *p,
		struct mem_cgroup *memcg, const nodemask_t *nodemask,
		unsigned long totalpages);

extern void oom_kill_process(struct oom_control *oc, struct task_struct *p,
			     unsigned int points, unsigned long totalpages,
			     const char *message);

extern void check_panic_on_oom(struct oom_control *oc,
			       enum oom_constraint constraint);

extern enum oom_scan_t oom_scan_process_thread(struct oom_control *oc,
					       struct task_struct *task);

extern bool out_of_memory(struct oom_control *oc);

extern void exit_oom_victim(struct task_struct *tsk);

extern int register_oom_notifier(struct notifier_block *nb);
extern int unregister_oom_notifier(struct notifier_block *nb);

extern bool oom_killer_disabled;
extern bool oom_killer_disable(void);
extern void oom_killer_enable(void);

extern struct task_struct *find_lock_task_mm(struct task_struct *p);

bool task_will_free_mem(struct task_struct *task);

/* sysctls */
extern int sysctl_oom_dump_tasks;
extern int sysctl_oom_kill_allocating_task;
extern int sysctl_panic_on_oom;
#endif /* _INCLUDE_LINUX_OOM_H */
