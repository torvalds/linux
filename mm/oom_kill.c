/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
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

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/cpuset.h>

/* #define DEBUG */

/**
 * oom_badness - calculate a numeric value for how bad this task has been
 * @p: task struct of which task we should calculate
 * @uptime: current uptime in seconds
 *
 * The formula used is relatively simple and documented inline in the
 * function. The main rationale is that we want to select a good task
 * to kill when we run out of memory.
 *
 * Good in this context means that:
 * 1) we lose the minimum amount of work done
 * 2) we recover a large amount of memory
 * 3) we don't kill anything innocent of eating tons of memory
 * 4) we want to kill the minimum amount of processes (one)
 * 5) we try to kill the process the user expects us to kill, this
 *    algorithm has been meticulously tuned to meet the principle
 *    of least surprise ... (be careful when you change it)
 */

unsigned long badness(struct task_struct *p, unsigned long uptime)
{
	unsigned long points, cpu_time, run_time, s;
	struct list_head *tsk;

	if (!p->mm)
		return 0;

	/*
	 * The memory size of the process is the basis for the badness.
	 */
	points = p->mm->total_vm;

	/*
	 * Processes which fork a lot of child processes are likely
	 * a good choice. We add half the vmsize of the children if they
	 * have an own mm. This prevents forking servers to flood the
	 * machine with an endless amount of children. In case a single
	 * child is eating the vast majority of memory, adding only half
	 * to the parents will make the child our kill candidate of choice.
	 */
	list_for_each(tsk, &p->children) {
		struct task_struct *chld;
		chld = list_entry(tsk, struct task_struct, sibling);
		if (chld->mm != p->mm && chld->mm)
			points += chld->mm->total_vm/2 + 1;
	}

	/*
	 * CPU time is in tens of seconds and run time is in thousands
         * of seconds. There is no particular reason for this other than
         * that it turned out to work very well in practice.
	 */
	cpu_time = (cputime_to_jiffies(p->utime) + cputime_to_jiffies(p->stime))
		>> (SHIFT_HZ + 3);

	if (uptime >= p->start_time.tv_sec)
		run_time = (uptime - p->start_time.tv_sec) >> 10;
	else
		run_time = 0;

	s = int_sqrt(cpu_time);
	if (s)
		points /= s;
	s = int_sqrt(int_sqrt(run_time));
	if (s)
		points /= s;

	/*
	 * Niced processes are most likely less important, so double
	 * their badness points.
	 */
	if (task_nice(p) > 0)
		points *= 2;

	/*
	 * Superuser processes are usually more important, so we make it
	 * less likely that we kill those.
	 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_ADMIN) ||
				p->uid == 0 || p->euid == 0)
		points /= 4;

	/*
	 * We don't want to kill a process with direct hardware access.
	 * Not only could that mess up the hardware, but usually users
	 * tend to only have this flag set on applications they think
	 * of as important.
	 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_RAWIO))
		points /= 4;

	/*
	 * Adjust the score by oomkilladj.
	 */
	if (p->oomkilladj) {
		if (p->oomkilladj > 0)
			points <<= p->oomkilladj;
		else
			points >>= -(p->oomkilladj);
	}

#ifdef DEBUG
	printk(KERN_DEBUG "OOMkill: task %d (%s) got %d points\n",
	p->pid, p->comm, points);
#endif
	return points;
}

/*
 * Types of limitations to the nodes from which allocations may occur
 */
#define CONSTRAINT_NONE 1
#define CONSTRAINT_MEMORY_POLICY 2
#define CONSTRAINT_CPUSET 3

/*
 * Determine the type of allocation constraint.
 */
static inline int constrained_alloc(struct zonelist *zonelist, gfp_t gfp_mask)
{
#ifdef CONFIG_NUMA
	struct zone **z;
	nodemask_t nodes = node_online_map;

	for (z = zonelist->zones; *z; z++)
		if (cpuset_zone_allowed(*z, gfp_mask))
			node_clear((*z)->zone_pgdat->node_id,
					nodes);
		else
			return CONSTRAINT_CPUSET;

	if (!nodes_empty(nodes))
		return CONSTRAINT_MEMORY_POLICY;
#endif

	return CONSTRAINT_NONE;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'. We expect the caller will lock the tasklist.
 *
 * (not docbooked, we don't want this one cluttering up the manual)
 */
static struct task_struct *select_bad_process(unsigned long *ppoints)
{
	struct task_struct *g, *p;
	struct task_struct *chosen = NULL;
	struct timespec uptime;
	*ppoints = 0;

	do_posix_clock_monotonic_gettime(&uptime);
	do_each_thread(g, p) {
		unsigned long points;
		int releasing;

		/* skip the init task with pid == 1 */
		if (p->pid == 1)
			continue;
		if (p->oomkilladj == OOM_DISABLE)
			continue;
		/* If p's nodes don't overlap ours, it won't help to kill p. */
		if (!cpuset_excl_nodes_overlap(p))
			continue;

		/*
		 * This is in the process of releasing memory so for wait it
		 * to finish before killing some other task by mistake.
		 */
		releasing = test_tsk_thread_flag(p, TIF_MEMDIE) ||
						p->flags & PF_EXITING;
		if (releasing && !(p->flags & PF_DEAD))
			return ERR_PTR(-1UL);
		if (p->flags & PF_SWAPOFF)
			return p;

		points = badness(p, uptime.tv_sec);
		if (points > *ppoints || !chosen) {
			chosen = p;
			*ppoints = points;
		}
	} while_each_thread(g, p);
	return chosen;
}

/**
 * We must be careful though to never send SIGKILL a process with
 * CAP_SYS_RAW_IO set, send SIGTERM instead (but it's unlikely that
 * we select a process with CAP_SYS_RAW_IO set).
 */
static void __oom_kill_task(task_t *p, const char *message)
{
	if (p->pid == 1) {
		WARN_ON(1);
		printk(KERN_WARNING "tried to kill init!\n");
		return;
	}

	task_lock(p);
	if (!p->mm || p->mm == &init_mm) {
		WARN_ON(1);
		printk(KERN_WARNING "tried to kill an mm-less task!\n");
		task_unlock(p);
		return;
	}
	task_unlock(p);
	printk(KERN_ERR "%s: Killed process %d (%s).\n",
				message, p->pid, p->comm);

	/*
	 * We give our sacrificial lamb high priority and access to
	 * all the memory it needs. That way it should be able to
	 * exit() and clear out its resources quickly...
	 */
	p->time_slice = HZ;
	set_tsk_thread_flag(p, TIF_MEMDIE);

	force_sig(SIGKILL, p);
}

static struct mm_struct *oom_kill_task(task_t *p, const char *message)
{
	struct mm_struct *mm = get_task_mm(p);
	task_t * g, * q;

	if (!mm)
		return NULL;
	if (mm == &init_mm) {
		mmput(mm);
		return NULL;
	}

	__oom_kill_task(p, message);
	/*
	 * kill all processes that share the ->mm (i.e. all threads),
	 * but are in a different thread group
	 */
	do_each_thread(g, q)
		if (q->mm == mm && q->tgid != p->tgid)
			__oom_kill_task(q, message);
	while_each_thread(g, q);

	return mm;
}

static struct mm_struct *oom_kill_process(struct task_struct *p,
				unsigned long points, const char *message)
{
 	struct mm_struct *mm;
	struct task_struct *c;
	struct list_head *tsk;

	printk(KERN_ERR "Out of Memory: Kill process %d (%s) score %li and "
		"children.\n", p->pid, p->comm, points);
	/* Try to kill a child first */
	list_for_each(tsk, &p->children) {
		c = list_entry(tsk, struct task_struct, sibling);
		if (c->mm == p->mm)
			continue;
		mm = oom_kill_task(c, message);
		if (mm)
			return mm;
	}
	return oom_kill_task(p, message);
}

/**
 * oom_kill - kill the "best" process when we run out of memory
 *
 * If we run out of memory, we have the choice between either
 * killing a random task (bad), letting the system crash (worse)
 * OR try to be smart about which process to kill. Note that we
 * don't have to be perfect here, we just have to be good.
 */
void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask, int order)
{
	struct mm_struct *mm = NULL;
	task_t *p;
	unsigned long points;

	if (printk_ratelimit()) {
		printk("oom-killer: gfp_mask=0x%x, order=%d\n",
			gfp_mask, order);
		dump_stack();
		show_mem();
	}

	cpuset_lock();
	read_lock(&tasklist_lock);

	/*
	 * Check if there were limitations on the allocation (only relevant for
	 * NUMA) that may require different handling.
	 */
	switch (constrained_alloc(zonelist, gfp_mask)) {
	case CONSTRAINT_MEMORY_POLICY:
		mm = oom_kill_process(current, points,
				"No available memory (MPOL_BIND)");
		break;

	case CONSTRAINT_CPUSET:
		mm = oom_kill_process(current, points,
				"No available memory in cpuset");
		break;

	case CONSTRAINT_NONE:
retry:
		/*
		 * Rambo mode: Shoot down a process and hope it solves whatever
		 * issues we may have.
		 */
		p = select_bad_process(&points);

		if (PTR_ERR(p) == -1UL)
			goto out;

		/* Found nothing?!?! Either we hang forever, or we panic. */
		if (!p) {
			read_unlock(&tasklist_lock);
			cpuset_unlock();
			panic("Out of memory and no killable processes...\n");
		}

		mm = oom_kill_process(p, points, "Out of memory");
		if (!mm)
			goto retry;

		break;
	}

out:
	cpuset_unlock();
	if (mm)
		mmput(mm);

	/*
	 * Give "p" a good chance of killing itself before we
	 * retry to allocate memory unless "p" is current
	 */
	if (!test_thread_flag(TIF_MEMDIE))
		schedule_timeout_interruptible(1);
}
