/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INCLUDE_LINUX_OOM_H
#define __INCLUDE_LINUX_OOM_H


#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/nodemask.h>
#include <uapi/linux/oom.h>
#include <linux/sched/coredump.h> /* MMF_* */
#include <linux/mm.h> /* VM_FAULT* */

struct zonelist;
struct notifier_block;
struct mem_cgroup;
struct task_struct;

enum oom_constraint {
	CONSTRAINT_NONE,
	CONSTRAINT_CPUSET,
	CONSTRAINT_MEMORY_POLICY,
	CONSTRAINT_MEMCG,
};

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

	/* Used by oom implementation, do not set */
	unsigned long totalpages;
	struct task_struct *chosen;
	long chosen_points;

	/* Used to print the constraint info. */
	enum oom_constraint constraint;
};

extern struct mutex oom_lock;
extern struct mutex oom_adj_mutex;

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

static inline bool tsk_is_oom_victim(struct task_struct * tsk)
{
	return tsk->signal->oom_mm;
}

/*
 * Checks whether a page fault on the given mm is still reliable.
 * This is no longer true if the oom reaper started to reap the
 * address space which is reflected by MMF_UNSTABLE flag set in
 * the mm. At that moment any !shared mapping would lose the content
 * and could cause a memory corruption (zero pages instead of the
 * original content).
 *
 * User should call this before establishing a page table entry for
 * a !shared mapping and under the proper page table lock.
 *
 * Return 0 when the PF is safe VM_FAULT_SIGBUS otherwise.
 */
static inline vm_fault_t check_stable_address_space(struct mm_struct *mm)
{
	if (unlikely(test_bit(MMF_UNSTABLE, &mm->flags)))
		return VM_FAULT_SIGBUS;
	return 0;
}

long oom_badness(struct task_struct *p,
		unsigned long totalpages);

extern bool out_of_memory(struct oom_control *oc);

extern void exit_oom_victim(void);

extern int register_oom_notifier(struct notifier_block *nb);
extern int unregister_oom_notifier(struct notifier_block *nb);

extern bool oom_killer_disable(signed long timeout);
extern void oom_killer_enable(void);

extern struct task_struct *find_lock_task_mm(struct task_struct *p);

/* call for adding killed process to reaper. */
extern void add_to_oom_reaper(struct task_struct *p);
#endif /* _INCLUDE_LINUX_OOM_H */
