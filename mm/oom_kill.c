/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *  Copyright (C)  2010  Google, Inc.
 *	Rewritten by David Rientjes
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from __alloc_pages()
 *  in mm/page_alloc.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/cpuset.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/security.h>
#include <linux/ptrace.h>
#include <linux/freezer.h>
#include <linux/ftrace.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include <linux/init.h>

#include <asm/tlb.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/oom.h>

int sysctl_panic_on_oom;
int sysctl_oom_kill_allocating_task;
int sysctl_oom_dump_tasks = 1;

DEFINE_MUTEX(oom_lock);

#ifdef CONFIG_NUMA
/**
 * has_intersects_mems_allowed() - check task eligiblity for kill
 * @start: task struct of which task to consider
 * @mask: nodemask passed to page allocator for mempolicy ooms
 *
 * Task eligibility is determined by whether or not a candidate task, @tsk,
 * shares the same mempolicy nodes as current if it is bound by such a policy
 * and whether or not it has the same set of allowed cpuset nodes.
 */
static bool has_intersects_mems_allowed(struct task_struct *start,
					const nodemask_t *mask)
{
	struct task_struct *tsk;
	bool ret = false;

	rcu_read_lock();
	for_each_thread(start, tsk) {
		if (mask) {
			/*
			 * If this is a mempolicy constrained oom, tsk's
			 * cpuset is irrelevant.  Only return true if its
			 * mempolicy intersects current, otherwise it may be
			 * needlessly killed.
			 */
			ret = mempolicy_nodemask_intersects(tsk, mask);
		} else {
			/*
			 * This is not a mempolicy constrained oom, so only
			 * check the mems of tsk's cpuset.
			 */
			ret = cpuset_mems_allowed_intersects(current, tsk);
		}
		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}
#else
static bool has_intersects_mems_allowed(struct task_struct *tsk,
					const nodemask_t *mask)
{
	return true;
}
#endif /* CONFIG_NUMA */

/*
 * The process p may have detached its own ->mm while exiting or through
 * use_mm(), but one or more of its subthreads may still have a valid
 * pointer.  Return p, or any of its subthreads with a valid ->mm, with
 * task_lock() held.
 */
struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	struct task_struct *t;

	rcu_read_lock();

	for_each_thread(p, t) {
		task_lock(t);
		if (likely(t->mm))
			goto found;
		task_unlock(t);
	}
	t = NULL;
found:
	rcu_read_unlock();

	return t;
}

/*
 * order == -1 means the oom kill is required by sysrq, otherwise only
 * for display purposes.
 */
static inline bool is_sysrq_oom(struct oom_control *oc)
{
	return oc->order == -1;
}

/* return true if the task is not adequate as candidate victim task. */
static bool oom_unkillable_task(struct task_struct *p,
		struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	if (is_global_init(p))
		return true;
	if (p->flags & PF_KTHREAD)
		return true;

	/* When mem_cgroup_out_of_memory() and p is not member of the group */
	if (memcg && !task_in_mem_cgroup(p, memcg))
		return true;

	/* p may not have freeable memory in nodemask */
	if (!has_intersects_mems_allowed(p, nodemask))
		return true;

	return false;
}

/**
 * oom_badness - heuristic function to determine which candidate task to kill
 * @p: task struct of which task we should calculate
 * @totalpages: total present RAM allowed for page allocation
 *
 * The heuristic for determining which task to kill is made to be as simple and
 * predictable as possible.  The goal is to return the highest value for the
 * task consuming the most memory to avoid subsequent oom failures.
 */
unsigned long oom_badness(struct task_struct *p, struct mem_cgroup *memcg,
			  const nodemask_t *nodemask, unsigned long totalpages)
{
	long points;
	long adj;

	if (oom_unkillable_task(p, memcg, nodemask))
		return 0;

	p = find_lock_task_mm(p);
	if (!p)
		return 0;

	adj = (long)p->signal->oom_score_adj;
	if (adj == OOM_SCORE_ADJ_MIN) {
		task_unlock(p);
		return 0;
	}

	/*
	 * The baseline for the badness score is the proportion of RAM that each
	 * task's rss, pagetable and swap space use.
	 */
	points = get_mm_rss(p->mm) + get_mm_counter(p->mm, MM_SWAPENTS) +
		atomic_long_read(&p->mm->nr_ptes) + mm_nr_pmds(p->mm);
	task_unlock(p);

	/*
	 * Root processes get 3% bonus, just like the __vm_enough_memory()
	 * implementation used by LSMs.
	 */
	if (has_capability_noaudit(p, CAP_SYS_ADMIN))
		points -= (points * 3) / 100;

	/* Normalize to oom_score_adj units */
	adj *= totalpages / 1000;
	points += adj;

	/*
	 * Never return 0 for an eligible task regardless of the root bonus and
	 * oom_score_adj (oom_score_adj can't be OOM_SCORE_ADJ_MIN here).
	 */
	return points > 0 ? points : 1;
}

/*
 * Determine the type of allocation constraint.
 */
#ifdef CONFIG_NUMA
static enum oom_constraint constrained_alloc(struct oom_control *oc,
					     unsigned long *totalpages)
{
	struct zone *zone;
	struct zoneref *z;
	enum zone_type high_zoneidx = gfp_zone(oc->gfp_mask);
	bool cpuset_limited = false;
	int nid;

	/* Default to all available memory */
	*totalpages = totalram_pages + total_swap_pages;

	if (!oc->zonelist)
		return CONSTRAINT_NONE;
	/*
	 * Reach here only when __GFP_NOFAIL is used. So, we should avoid
	 * to kill current.We have to random task kill in this case.
	 * Hopefully, CONSTRAINT_THISNODE...but no way to handle it, now.
	 */
	if (oc->gfp_mask & __GFP_THISNODE)
		return CONSTRAINT_NONE;

	/*
	 * This is not a __GFP_THISNODE allocation, so a truncated nodemask in
	 * the page allocator means a mempolicy is in effect.  Cpuset policy
	 * is enforced in get_page_from_freelist().
	 */
	if (oc->nodemask &&
	    !nodes_subset(node_states[N_MEMORY], *oc->nodemask)) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, *oc->nodemask)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_MEMORY_POLICY;
	}

	/* Check this allocation failure is caused by cpuset's wall function */
	for_each_zone_zonelist_nodemask(zone, z, oc->zonelist,
			high_zoneidx, oc->nodemask)
		if (!cpuset_zone_allowed(zone, oc->gfp_mask))
			cpuset_limited = true;

	if (cpuset_limited) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, cpuset_current_mems_allowed)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_CPUSET;
	}
	return CONSTRAINT_NONE;
}
#else
static enum oom_constraint constrained_alloc(struct oom_control *oc,
					     unsigned long *totalpages)
{
	*totalpages = totalram_pages + total_swap_pages;
	return CONSTRAINT_NONE;
}
#endif

enum oom_scan_t oom_scan_process_thread(struct oom_control *oc,
			struct task_struct *task, unsigned long totalpages)
{
	if (oom_unkillable_task(task, NULL, oc->nodemask))
		return OOM_SCAN_CONTINUE;

	/*
	 * This task already has access to memory reserves and is being killed.
	 * Don't allow any other task to have access to the reserves.
	 */
	if (test_tsk_thread_flag(task, TIF_MEMDIE)) {
		if (!is_sysrq_oom(oc))
			return OOM_SCAN_ABORT;
	}
	if (!task->mm)
		return OOM_SCAN_CONTINUE;

	/*
	 * If task is allocating a lot of memory and has been marked to be
	 * killed first if it triggers an oom, then select it.
	 */
	if (oom_task_origin(task))
		return OOM_SCAN_SELECT;

	return OOM_SCAN_OK;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'.  Returns -1 on scan abort.
 */
static struct task_struct *select_bad_process(struct oom_control *oc,
		unsigned int *ppoints, unsigned long totalpages)
{
	struct task_struct *g, *p;
	struct task_struct *chosen = NULL;
	unsigned long chosen_points = 0;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		unsigned int points;

		switch (oom_scan_process_thread(oc, p, totalpages)) {
		case OOM_SCAN_SELECT:
			chosen = p;
			chosen_points = ULONG_MAX;
			/* fall through */
		case OOM_SCAN_CONTINUE:
			continue;
		case OOM_SCAN_ABORT:
			rcu_read_unlock();
			return (struct task_struct *)(-1UL);
		case OOM_SCAN_OK:
			break;
		};
		points = oom_badness(p, NULL, oc->nodemask, totalpages);
		if (!points || points < chosen_points)
			continue;
		/* Prefer thread group leaders for display purposes */
		if (points == chosen_points && thread_group_leader(chosen))
			continue;

		chosen = p;
		chosen_points = points;
	}
	if (chosen)
		get_task_struct(chosen);
	rcu_read_unlock();

	*ppoints = chosen_points * 1000 / totalpages;
	return chosen;
}

/**
 * dump_tasks - dump current memory state of all system tasks
 * @memcg: current's memory controller, if constrained
 * @nodemask: nodemask passed to page allocator for mempolicy ooms
 *
 * Dumps the current memory state of all eligible tasks.  Tasks not in the same
 * memcg, not in the same cpuset, or bound to a disjoint set of mempolicy nodes
 * are not shown.
 * State information includes task's pid, uid, tgid, vm size, rss, nr_ptes,
 * swapents, oom_score_adj value, and name.
 */
static void dump_tasks(struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	struct task_struct *p;
	struct task_struct *task;

	pr_info("[ pid ]   uid  tgid total_vm      rss nr_ptes nr_pmds swapents oom_score_adj name\n");
	rcu_read_lock();
	for_each_process(p) {
		if (oom_unkillable_task(p, memcg, nodemask))
			continue;

		task = find_lock_task_mm(p);
		if (!task) {
			/*
			 * This is a kthread or all of p's threads have already
			 * detached their mm's.  There's no need to report
			 * them; they can't be oom killed anyway.
			 */
			continue;
		}

		pr_info("[%5d] %5d %5d %8lu %8lu %7ld %7ld %8lu         %5hd %s\n",
			task->pid, from_kuid(&init_user_ns, task_uid(task)),
			task->tgid, task->mm->total_vm, get_mm_rss(task->mm),
			atomic_long_read(&task->mm->nr_ptes),
			mm_nr_pmds(task->mm),
			get_mm_counter(task->mm, MM_SWAPENTS),
			task->signal->oom_score_adj, task->comm);
		task_unlock(task);
	}
	rcu_read_unlock();
}

static void dump_header(struct oom_control *oc, struct task_struct *p,
			struct mem_cgroup *memcg)
{
	pr_warn("%s invoked oom-killer: gfp_mask=%#x(%pGg), order=%d, oom_score_adj=%hd\n",
		current->comm, oc->gfp_mask, &oc->gfp_mask, oc->order,
		current->signal->oom_score_adj);

	cpuset_print_current_mems_allowed();
	dump_stack();
	if (memcg)
		mem_cgroup_print_oom_info(memcg, p);
	else
		show_mem(SHOW_MEM_FILTER_NODES);
	if (sysctl_oom_dump_tasks)
		dump_tasks(memcg, oc->nodemask);
}

/*
 * Number of OOM victims in flight
 */
static atomic_t oom_victims = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(oom_victims_wait);

bool oom_killer_disabled __read_mostly;

#define K(x) ((x) << (PAGE_SHIFT-10))

/*
 * task->mm can be NULL if the task is the exited group leader.  So to
 * determine whether the task is using a particular mm, we examine all the
 * task's threads: if one of those is using this mm then this task was also
 * using it.
 */
static bool process_shares_mm(struct task_struct *p, struct mm_struct *mm)
{
	struct task_struct *t;

	for_each_thread(p, t) {
		struct mm_struct *t_mm = READ_ONCE(t->mm);
		if (t_mm)
			return t_mm == mm;
	}
	return false;
}


#ifdef CONFIG_MMU
/*
 * OOM Reaper kernel thread which tries to reap the memory used by the OOM
 * victim (if that is possible) to help the OOM killer to move on.
 */
static struct task_struct *oom_reaper_th;
static DECLARE_WAIT_QUEUE_HEAD(oom_reaper_wait);
static struct task_struct *oom_reaper_list;
static DEFINE_SPINLOCK(oom_reaper_lock);


static bool __oom_reap_task(struct task_struct *tsk)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct task_struct *p;
	struct zap_details details = {.check_swap_entries = true,
				      .ignore_dirty = true};
	bool ret = true;

	/*
	 * Make sure we find the associated mm_struct even when the particular
	 * thread has already terminated and cleared its mm.
	 * We might have race with exit path so consider our work done if there
	 * is no mm.
	 */
	p = find_lock_task_mm(tsk);
	if (!p)
		return true;

	mm = p->mm;
	if (!atomic_inc_not_zero(&mm->mm_users)) {
		task_unlock(p);
		return true;
	}

	task_unlock(p);

	if (!down_read_trylock(&mm->mmap_sem)) {
		ret = false;
		goto out;
	}

	tlb_gather_mmu(&tlb, mm, 0, -1);
	for (vma = mm->mmap ; vma; vma = vma->vm_next) {
		if (is_vm_hugetlb_page(vma))
			continue;

		/*
		 * mlocked VMAs require explicit munlocking before unmap.
		 * Let's keep it simple here and skip such VMAs.
		 */
		if (vma->vm_flags & VM_LOCKED)
			continue;

		/*
		 * Only anonymous pages have a good chance to be dropped
		 * without additional steps which we cannot afford as we
		 * are OOM already.
		 *
		 * We do not even care about fs backed pages because all
		 * which are reclaimable have already been reclaimed and
		 * we do not want to block exit_mmap by keeping mm ref
		 * count elevated without a good reason.
		 */
		if (vma_is_anonymous(vma) || !(vma->vm_flags & VM_SHARED))
			unmap_page_range(&tlb, vma, vma->vm_start, vma->vm_end,
					 &details);
	}
	tlb_finish_mmu(&tlb, 0, -1);
	pr_info("oom_reaper: reaped process %d (%s), now anon-rss:%lukB, file-rss:%lukB, shmem-rss:%lukB\n",
			task_pid_nr(tsk), tsk->comm,
			K(get_mm_counter(mm, MM_ANONPAGES)),
			K(get_mm_counter(mm, MM_FILEPAGES)),
			K(get_mm_counter(mm, MM_SHMEMPAGES)));
	up_read(&mm->mmap_sem);

	/*
	 * Clear TIF_MEMDIE because the task shouldn't be sitting on a
	 * reasonably reclaimable memory anymore. OOM killer can continue
	 * by selecting other victim if unmapping hasn't led to any
	 * improvements. This also means that selecting this task doesn't
	 * make any sense.
	 */
	tsk->signal->oom_score_adj = OOM_SCORE_ADJ_MIN;
	exit_oom_victim(tsk);
out:
	mmput(mm);
	return ret;
}

#define MAX_OOM_REAP_RETRIES 10
static void oom_reap_task(struct task_struct *tsk)
{
	int attempts = 0;

	/* Retry the down_read_trylock(mmap_sem) a few times */
	while (attempts++ < MAX_OOM_REAP_RETRIES && !__oom_reap_task(tsk))
		schedule_timeout_idle(HZ/10);

	if (attempts > MAX_OOM_REAP_RETRIES) {
		pr_info("oom_reaper: unable to reap pid:%d (%s)\n",
				task_pid_nr(tsk), tsk->comm);
		debug_show_all_locks();
	}

	/* Drop a reference taken by wake_oom_reaper */
	put_task_struct(tsk);
}

static int oom_reaper(void *unused)
{
	set_freezable();

	while (true) {
		struct task_struct *tsk = NULL;

		wait_event_freezable(oom_reaper_wait, oom_reaper_list != NULL);
		spin_lock(&oom_reaper_lock);
		if (oom_reaper_list != NULL) {
			tsk = oom_reaper_list;
			oom_reaper_list = tsk->oom_reaper_list;
		}
		spin_unlock(&oom_reaper_lock);

		if (tsk)
			oom_reap_task(tsk);
	}

	return 0;
}

static void wake_oom_reaper(struct task_struct *tsk)
{
	if (!oom_reaper_th)
		return;

	/* tsk is already queued? */
	if (tsk == oom_reaper_list || tsk->oom_reaper_list)
		return;

	get_task_struct(tsk);

	spin_lock(&oom_reaper_lock);
	tsk->oom_reaper_list = oom_reaper_list;
	oom_reaper_list = tsk;
	spin_unlock(&oom_reaper_lock);
	wake_up(&oom_reaper_wait);
}

/* Check if we can reap the given task. This has to be called with stable
 * tsk->mm
 */
void try_oom_reaper(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;
	struct task_struct *p;

	if (!mm)
		return;

	/*
	 * There might be other threads/processes which are either not
	 * dying or even not killable.
	 */
	if (atomic_read(&mm->mm_users) > 1) {
		rcu_read_lock();
		for_each_process(p) {
			bool exiting;

			if (!process_shares_mm(p, mm))
				continue;
			if (same_thread_group(p, tsk))
				continue;
			if (fatal_signal_pending(p))
				continue;

			/*
			 * If the task is exiting make sure the whole thread group
			 * is exiting and cannot acces mm anymore.
			 */
			spin_lock_irq(&p->sighand->siglock);
			exiting = signal_group_exit(p->signal);
			spin_unlock_irq(&p->sighand->siglock);
			if (exiting)
				continue;

			/* Give up */
			rcu_read_unlock();
			return;
		}
		rcu_read_unlock();
	}

	wake_oom_reaper(tsk);
}

static int __init oom_init(void)
{
	oom_reaper_th = kthread_run(oom_reaper, NULL, "oom_reaper");
	if (IS_ERR(oom_reaper_th)) {
		pr_err("Unable to start OOM reaper %ld. Continuing regardless\n",
				PTR_ERR(oom_reaper_th));
		oom_reaper_th = NULL;
	}
	return 0;
}
subsys_initcall(oom_init)
#else
static void wake_oom_reaper(struct task_struct *tsk)
{
}
#endif

/**
 * mark_oom_victim - mark the given task as OOM victim
 * @tsk: task to mark
 *
 * Has to be called with oom_lock held and never after
 * oom has been disabled already.
 */
void mark_oom_victim(struct task_struct *tsk)
{
	WARN_ON(oom_killer_disabled);
	/* OOM killer might race with memcg OOM */
	if (test_and_set_tsk_thread_flag(tsk, TIF_MEMDIE))
		return;
	/*
	 * Make sure that the task is woken up from uninterruptible sleep
	 * if it is frozen because OOM killer wouldn't be able to free
	 * any memory and livelock. freezing_slow_path will tell the freezer
	 * that TIF_MEMDIE tasks should be ignored.
	 */
	__thaw_task(tsk);
	atomic_inc(&oom_victims);
}

/**
 * exit_oom_victim - note the exit of an OOM victim
 */
void exit_oom_victim(struct task_struct *tsk)
{
	if (!test_and_clear_tsk_thread_flag(tsk, TIF_MEMDIE))
		return;

	if (!atomic_dec_return(&oom_victims))
		wake_up_all(&oom_victims_wait);
}

/**
 * oom_killer_disable - disable OOM killer
 *
 * Forces all page allocations to fail rather than trigger OOM killer.
 * Will block and wait until all OOM victims are killed.
 *
 * The function cannot be called when there are runnable user tasks because
 * the userspace would see unexpected allocation failures as a result. Any
 * new usage of this function should be consulted with MM people.
 *
 * Returns true if successful and false if the OOM killer cannot be
 * disabled.
 */
bool oom_killer_disable(void)
{
	/*
	 * Make sure to not race with an ongoing OOM killer. Check that the
	 * current is not killed (possibly due to sharing the victim's memory).
	 */
	if (mutex_lock_killable(&oom_lock))
		return false;
	oom_killer_disabled = true;
	mutex_unlock(&oom_lock);

	wait_event(oom_victims_wait, !atomic_read(&oom_victims));

	return true;
}

/**
 * oom_killer_enable - enable OOM killer
 */
void oom_killer_enable(void)
{
	oom_killer_disabled = false;
}

/*
 * Must be called while holding a reference to p, which will be released upon
 * returning.
 */
void oom_kill_process(struct oom_control *oc, struct task_struct *p,
		      unsigned int points, unsigned long totalpages,
		      struct mem_cgroup *memcg, const char *message)
{
	struct task_struct *victim = p;
	struct task_struct *child;
	struct task_struct *t;
	struct mm_struct *mm;
	unsigned int victim_points = 0;
	static DEFINE_RATELIMIT_STATE(oom_rs, DEFAULT_RATELIMIT_INTERVAL,
					      DEFAULT_RATELIMIT_BURST);
	bool can_oom_reap = true;

	/*
	 * If the task is already exiting, don't alarm the sysadmin or kill
	 * its children or threads, just set TIF_MEMDIE so it can die quickly
	 */
	task_lock(p);
	if (p->mm && task_will_free_mem(p)) {
		mark_oom_victim(p);
		try_oom_reaper(p);
		task_unlock(p);
		put_task_struct(p);
		return;
	}
	task_unlock(p);

	if (__ratelimit(&oom_rs))
		dump_header(oc, p, memcg);

	pr_err("%s: Kill process %d (%s) score %u or sacrifice child\n",
		message, task_pid_nr(p), p->comm, points);

	/*
	 * If any of p's children has a different mm and is eligible for kill,
	 * the one with the highest oom_badness() score is sacrificed for its
	 * parent.  This attempts to lose the minimal amount of work done while
	 * still freeing memory.
	 */
	read_lock(&tasklist_lock);
	for_each_thread(p, t) {
		list_for_each_entry(child, &t->children, sibling) {
			unsigned int child_points;

			if (process_shares_mm(child, p->mm))
				continue;
			/*
			 * oom_badness() returns 0 if the thread is unkillable
			 */
			child_points = oom_badness(child, memcg, oc->nodemask,
								totalpages);
			if (child_points > victim_points) {
				put_task_struct(victim);
				victim = child;
				victim_points = child_points;
				get_task_struct(victim);
			}
		}
	}
	read_unlock(&tasklist_lock);

	p = find_lock_task_mm(victim);
	if (!p) {
		put_task_struct(victim);
		return;
	} else if (victim != p) {
		get_task_struct(p);
		put_task_struct(victim);
		victim = p;
	}

	/* Get a reference to safely compare mm after task_unlock(victim) */
	mm = victim->mm;
	atomic_inc(&mm->mm_count);
	/*
	 * We should send SIGKILL before setting TIF_MEMDIE in order to prevent
	 * the OOM victim from depleting the memory reserves from the user
	 * space under its control.
	 */
	do_send_sig_info(SIGKILL, SEND_SIG_FORCED, victim, true);
	mark_oom_victim(victim);
	pr_err("Killed process %d (%s) total-vm:%lukB, anon-rss:%lukB, file-rss:%lukB, shmem-rss:%lukB\n",
		task_pid_nr(victim), victim->comm, K(victim->mm->total_vm),
		K(get_mm_counter(victim->mm, MM_ANONPAGES)),
		K(get_mm_counter(victim->mm, MM_FILEPAGES)),
		K(get_mm_counter(victim->mm, MM_SHMEMPAGES)));
	task_unlock(victim);

	/*
	 * Kill all user processes sharing victim->mm in other thread groups, if
	 * any.  They don't get access to memory reserves, though, to avoid
	 * depletion of all memory.  This prevents mm->mmap_sem livelock when an
	 * oom killed thread cannot exit because it requires the semaphore and
	 * its contended by another thread trying to allocate memory itself.
	 * That thread will now get access to memory reserves since it has a
	 * pending fatal signal.
	 */
	rcu_read_lock();
	for_each_process(p) {
		if (!process_shares_mm(p, mm))
			continue;
		if (same_thread_group(p, victim))
			continue;
		if (unlikely(p->flags & PF_KTHREAD) || is_global_init(p) ||
		    p->signal->oom_score_adj == OOM_SCORE_ADJ_MIN) {
			/*
			 * We cannot use oom_reaper for the mm shared by this
			 * process because it wouldn't get killed and so the
			 * memory might be still used.
			 */
			can_oom_reap = false;
			continue;
		}
		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, p, true);
	}
	rcu_read_unlock();

	if (can_oom_reap)
		wake_oom_reaper(victim);

	mmdrop(mm);
	put_task_struct(victim);
}
#undef K

/*
 * Determines whether the kernel must panic because of the panic_on_oom sysctl.
 */
void check_panic_on_oom(struct oom_control *oc, enum oom_constraint constraint,
			struct mem_cgroup *memcg)
{
	if (likely(!sysctl_panic_on_oom))
		return;
	if (sysctl_panic_on_oom != 2) {
		/*
		 * panic_on_oom == 1 only affects CONSTRAINT_NONE, the kernel
		 * does not panic for cpuset, mempolicy, or memcg allocation
		 * failures.
		 */
		if (constraint != CONSTRAINT_NONE)
			return;
	}
	/* Do not panic for oom kills triggered by sysrq */
	if (is_sysrq_oom(oc))
		return;
	dump_header(oc, NULL, memcg);
	panic("Out of memory: %s panic_on_oom is enabled\n",
		sysctl_panic_on_oom == 2 ? "compulsory" : "system-wide");
}

static BLOCKING_NOTIFIER_HEAD(oom_notify_list);

int register_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_oom_notifier);

int unregister_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_oom_notifier);

/**
 * out_of_memory - kill the "best" process when we run out of memory
 * @oc: pointer to struct oom_control
 *
 * If we run out of memory, we have the choice between either
 * killing a random task (bad), letting the system crash (worse)
 * OR try to be smart about which process to kill. Note that we
 * don't have to be perfect here, we just have to be good.
 */
bool out_of_memory(struct oom_control *oc)
{
	struct task_struct *p;
	unsigned long totalpages;
	unsigned long freed = 0;
	unsigned int uninitialized_var(points);
	enum oom_constraint constraint = CONSTRAINT_NONE;

	if (oom_killer_disabled)
		return false;

	blocking_notifier_call_chain(&oom_notify_list, 0, &freed);
	if (freed > 0)
		/* Got some memory back in the last second. */
		return true;

	/*
	 * If current has a pending SIGKILL or is exiting, then automatically
	 * select it.  The goal is to allow it to allocate so that it may
	 * quickly exit and free its memory.
	 *
	 * But don't select if current has already released its mm and cleared
	 * TIF_MEMDIE flag at exit_mm(), otherwise an OOM livelock may occur.
	 */
	if (current->mm &&
	    (fatal_signal_pending(current) || task_will_free_mem(current))) {
		mark_oom_victim(current);
		try_oom_reaper(current);
		return true;
	}

	/*
	 * The OOM killer does not compensate for IO-less reclaim.
	 * pagefault_out_of_memory lost its gfp context so we have to
	 * make sure exclude 0 mask - all other users should have at least
	 * ___GFP_DIRECT_RECLAIM to get here.
	 */
	if (oc->gfp_mask && !(oc->gfp_mask & (__GFP_FS|__GFP_NOFAIL)))
		return true;

	/*
	 * Check if there were limitations on the allocation (only relevant for
	 * NUMA) that may require different handling.
	 */
	constraint = constrained_alloc(oc, &totalpages);
	if (constraint != CONSTRAINT_MEMORY_POLICY)
		oc->nodemask = NULL;
	check_panic_on_oom(oc, constraint, NULL);

	if (sysctl_oom_kill_allocating_task && current->mm &&
	    !oom_unkillable_task(current, NULL, oc->nodemask) &&
	    current->signal->oom_score_adj != OOM_SCORE_ADJ_MIN) {
		get_task_struct(current);
		oom_kill_process(oc, current, 0, totalpages, NULL,
				 "Out of memory (oom_kill_allocating_task)");
		return true;
	}

	p = select_bad_process(oc, &points, totalpages);
	/* Found nothing?!?! Either we hang forever, or we panic. */
	if (!p && !is_sysrq_oom(oc)) {
		dump_header(oc, NULL, NULL);
		panic("Out of memory and no killable processes...\n");
	}
	if (p && p != (void *)-1UL) {
		oom_kill_process(oc, p, points, totalpages, NULL,
				 "Out of memory");
		/*
		 * Give the killed process a good chance to exit before trying
		 * to allocate memory again.
		 */
		schedule_timeout_killable(1);
	}
	return true;
}

/*
 * The pagefault handler calls here because it is out of memory, so kill a
 * memory-hogging task.  If any populated zone has ZONE_OOM_LOCKED set, a
 * parallel oom killing is already in progress so do nothing.
 */
void pagefault_out_of_memory(void)
{
	struct oom_control oc = {
		.zonelist = NULL,
		.nodemask = NULL,
		.gfp_mask = 0,
		.order = 0,
	};

	if (mem_cgroup_oom_synchronize(true))
		return;

	if (!mutex_trylock(&oom_lock))
		return;

	if (!out_of_memory(&oc)) {
		/*
		 * There shouldn't be any user tasks runnable while the
		 * OOM killer is disabled, so the current task has to
		 * be a racing OOM victim for which oom_killer_disable()
		 * is waiting for.
		 */
		WARN_ON(test_thread_flag(TIF_MEMDIE));
	}

	mutex_unlock(&oom_lock);
}
