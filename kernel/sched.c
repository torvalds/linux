/*
 *  kernel/sched.c
 *
 *  Kernel scheduler and related syscalls
 *
 *  Copyright (C) 1991-2002  Linus Torvalds
 *
 *  1996-12-23  Modified by Dave Grothe to fix bugs in semaphores and
 *		make semaphores SMP safe
 *  1998-11-19	Implemented schedule_timeout() and related stuff
 *		by Andrea Arcangeli
 *  2002-01-04	New ultra-scalable O(1) scheduler by Ingo Molnar:
 *		hybrid priority-list and round-robin design with
 *		an array-switch method of distributing timeslices
 *		and per-CPU runqueues.  Cleanups and useful suggestions
 *		by Davide Libenzi, preemptible kernel bits by Robert Love.
 *  2003-09-03	Interactivity tuning by Con Kolivas.
 *  2004-04-02	Scheduler domains code by Nick Piggin
 *  2007-04-15  Work begun on replacing all interactivity tuning with a
 *              fair scheduling design by Con Kolivas.
 *  2007-05-05  Load balancing (smp-nice) and other improvements
 *              by Peter Williams
 *  2007-05-06  Interactivity improvements to CFS by Mike Galbraith
 *  2007-07-01  Group scheduling enhancements by Srivatsa Vaddagiri
 *  2007-11-29  RT balancing improvements by Steven Rostedt, Gregory Haskins,
 *              Thomas Gleixner, Mike Kravetz
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <asm/mmu_context.h>
#include <linux/interrupt.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/kernel_stat.h>
#include <linux/debug_locks.h>
#include <linux/security.h>
#include <linux/notifier.h>
#include <linux/profile.h>
#include <linux/freezer.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/pid_namespace.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/timer.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysctl.h>
#include <linux/syscalls.h>
#include <linux/times.h>
#include <linux/tsacct_kern.h>
#include <linux/kprobes.h>
#include <linux/delayacct.h>
#include <linux/reciprocal_div.h>
#include <linux/unistd.h>
#include <linux/pagemap.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/bootmem.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/ftrace.h>
#include <trace/sched.h>

#include <asm/tlb.h>
#include <asm/irq_regs.h>

#include "sched_cpupri.h"

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
#define NICE_TO_PRIO(nice)	(MAX_RT_PRIO + (nice) + 20)
#define PRIO_TO_NICE(prio)	((prio) - MAX_RT_PRIO - 20)
#define TASK_NICE(p)		PRIO_TO_NICE((p)->static_prio)

/*
 * 'User priority' is the nice value converted to something we
 * can work with better when scaling various scheduler parameters,
 * it's a [ 0 ... 39 ] range.
 */
#define USER_PRIO(p)		((p)-MAX_RT_PRIO)
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)
#define MAX_USER_PRIO		(USER_PRIO(MAX_PRIO))

/*
 * Helpers for converting nanosecond timing to jiffy resolution
 */
#define NS_TO_JIFFIES(TIME)	((unsigned long)(TIME) / (NSEC_PER_SEC / HZ))

#define NICE_0_LOAD		SCHED_LOAD_SCALE
#define NICE_0_SHIFT		SCHED_LOAD_SHIFT

/*
 * These are the 'tuning knobs' of the scheduler:
 *
 * default timeslice is 100 msecs (used only for SCHED_RR tasks).
 * Timeslices get refilled after they expire.
 */
#define DEF_TIMESLICE		(100 * HZ / 1000)

/*
 * single value that denotes runtime == period, ie unlimited time.
 */
#define RUNTIME_INF	((u64)~0ULL)

DEFINE_TRACE(sched_wait_task);
DEFINE_TRACE(sched_wakeup);
DEFINE_TRACE(sched_wakeup_new);
DEFINE_TRACE(sched_switch);
DEFINE_TRACE(sched_migrate_task);

#ifdef CONFIG_SMP

static void double_rq_lock(struct rq *rq1, struct rq *rq2);

/*
 * Divide a load by a sched group cpu_power : (load / sg->__cpu_power)
 * Since cpu_power is a 'constant', we can use a reciprocal divide.
 */
static inline u32 sg_div_cpu_power(const struct sched_group *sg, u32 load)
{
	return reciprocal_divide(load, sg->reciprocal_cpu_power);
}

/*
 * Each time a sched group cpu_power is changed,
 * we must compute its reciprocal value
 */
static inline void sg_inc_cpu_power(struct sched_group *sg, u32 val)
{
	sg->__cpu_power += val;
	sg->reciprocal_cpu_power = reciprocal_value(sg->__cpu_power);
}
#endif

static inline int rt_policy(int policy)
{
	if (unlikely(policy == SCHED_FIFO || policy == SCHED_RR))
		return 1;
	return 0;
}

static inline int task_has_rt_policy(struct task_struct *p)
{
	return rt_policy(p->policy);
}

/*
 * This is the priority-queue data structure of the RT scheduling class:
 */
struct rt_prio_array {
	DECLARE_BITMAP(bitmap, MAX_RT_PRIO+1); /* include 1 bit for delimiter */
	struct list_head queue[MAX_RT_PRIO];
};

struct rt_bandwidth {
	/* nests inside the rq lock: */
	spinlock_t		rt_runtime_lock;
	ktime_t			rt_period;
	u64			rt_runtime;
	struct hrtimer		rt_period_timer;
};

static struct rt_bandwidth def_rt_bandwidth;

static int do_sched_rt_period_timer(struct rt_bandwidth *rt_b, int overrun);

static enum hrtimer_restart sched_rt_period_timer(struct hrtimer *timer)
{
	struct rt_bandwidth *rt_b =
		container_of(timer, struct rt_bandwidth, rt_period_timer);
	ktime_t now;
	int overrun;
	int idle = 0;

	for (;;) {
		now = hrtimer_cb_get_time(timer);
		overrun = hrtimer_forward(timer, now, rt_b->rt_period);

		if (!overrun)
			break;

		idle = do_sched_rt_period_timer(rt_b, overrun);
	}

	return idle ? HRTIMER_NORESTART : HRTIMER_RESTART;
}

static
void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime)
{
	rt_b->rt_period = ns_to_ktime(period);
	rt_b->rt_runtime = runtime;

	spin_lock_init(&rt_b->rt_runtime_lock);

	hrtimer_init(&rt_b->rt_period_timer,
			CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rt_b->rt_period_timer.function = sched_rt_period_timer;
}

static inline int rt_bandwidth_enabled(void)
{
	return sysctl_sched_rt_runtime >= 0;
}

static void start_rt_bandwidth(struct rt_bandwidth *rt_b)
{
	ktime_t now;

	if (!rt_bandwidth_enabled() || rt_b->rt_runtime == RUNTIME_INF)
		return;

	if (hrtimer_active(&rt_b->rt_period_timer))
		return;

	spin_lock(&rt_b->rt_runtime_lock);
	for (;;) {
		unsigned long delta;
		ktime_t soft, hard;

		if (hrtimer_active(&rt_b->rt_period_timer))
			break;

		now = hrtimer_cb_get_time(&rt_b->rt_period_timer);
		hrtimer_forward(&rt_b->rt_period_timer, now, rt_b->rt_period);

		soft = hrtimer_get_softexpires(&rt_b->rt_period_timer);
		hard = hrtimer_get_expires(&rt_b->rt_period_timer);
		delta = ktime_to_ns(ktime_sub(hard, soft));
		__hrtimer_start_range_ns(&rt_b->rt_period_timer, soft, delta,
				HRTIMER_MODE_ABS, 0);
	}
	spin_unlock(&rt_b->rt_runtime_lock);
}

#ifdef CONFIG_RT_GROUP_SCHED
static void destroy_rt_bandwidth(struct rt_bandwidth *rt_b)
{
	hrtimer_cancel(&rt_b->rt_period_timer);
}
#endif

/*
 * sched_domains_mutex serializes calls to arch_init_sched_domains,
 * detach_destroy_domains and partition_sched_domains.
 */
static DEFINE_MUTEX(sched_domains_mutex);

#ifdef CONFIG_GROUP_SCHED

#include <linux/cgroup.h>

struct cfs_rq;

static LIST_HEAD(task_groups);

/* task group related information */
struct task_group {
#ifdef CONFIG_CGROUP_SCHED
	struct cgroup_subsys_state css;
#endif

#ifdef CONFIG_USER_SCHED
	uid_t uid;
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* schedulable entities of this group on each cpu */
	struct sched_entity **se;
	/* runqueue "owned" by this group on each cpu */
	struct cfs_rq **cfs_rq;
	unsigned long shares;
#endif

#ifdef CONFIG_RT_GROUP_SCHED
	struct sched_rt_entity **rt_se;
	struct rt_rq **rt_rq;

	struct rt_bandwidth rt_bandwidth;
#endif

	struct rcu_head rcu;
	struct list_head list;

	struct task_group *parent;
	struct list_head siblings;
	struct list_head children;
};

#ifdef CONFIG_USER_SCHED

/* Helper function to pass uid information to create_sched_user() */
void set_tg_uid(struct user_struct *user)
{
	user->tg->uid = user->uid;
}

/*
 * Root task group.
 * 	Every UID task group (including init_task_group aka UID-0) will
 * 	be a child to this group.
 */
struct task_group root_task_group;

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Default task group's sched entity on each cpu */
static DEFINE_PER_CPU(struct sched_entity, init_sched_entity);
/* Default task group's cfs_rq on each cpu */
static DEFINE_PER_CPU(struct cfs_rq, init_cfs_rq) ____cacheline_aligned_in_smp;
#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_RT_GROUP_SCHED
static DEFINE_PER_CPU(struct sched_rt_entity, init_sched_rt_entity);
static DEFINE_PER_CPU(struct rt_rq, init_rt_rq) ____cacheline_aligned_in_smp;
#endif /* CONFIG_RT_GROUP_SCHED */
#else /* !CONFIG_USER_SCHED */
#define root_task_group init_task_group
#endif /* CONFIG_USER_SCHED */

/* task_group_lock serializes add/remove of task groups and also changes to
 * a task group's cpu shares.
 */
static DEFINE_SPINLOCK(task_group_lock);

#ifdef CONFIG_SMP
static int root_task_group_empty(void)
{
	return list_empty(&root_task_group.children);
}
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
#ifdef CONFIG_USER_SCHED
# define INIT_TASK_GROUP_LOAD	(2*NICE_0_LOAD)
#else /* !CONFIG_USER_SCHED */
# define INIT_TASK_GROUP_LOAD	NICE_0_LOAD
#endif /* CONFIG_USER_SCHED */

/*
 * A weight of 0 or 1 can cause arithmetics problems.
 * A weight of a cfs_rq is the sum of weights of which entities
 * are queued on this cfs_rq, so a weight of a entity should not be
 * too large, so as the shares value of a task group.
 * (The default weight is 1024 - so there's no practical
 *  limitation from this.)
 */
#define MIN_SHARES	2
#define MAX_SHARES	(1UL << 18)

static int init_task_group_load = INIT_TASK_GROUP_LOAD;
#endif

/* Default task group.
 *	Every task in system belong to this group at bootup.
 */
struct task_group init_task_group;

/* return group to which a task belongs */
static inline struct task_group *task_group(struct task_struct *p)
{
	struct task_group *tg;

#ifdef CONFIG_USER_SCHED
	rcu_read_lock();
	tg = __task_cred(p)->user->tg;
	rcu_read_unlock();
#elif defined(CONFIG_CGROUP_SCHED)
	tg = container_of(task_subsys_state(p, cpu_cgroup_subsys_id),
				struct task_group, css);
#else
	tg = &init_task_group;
#endif
	return tg;
}

/* Change a task's cfs_rq and parent entity if it moves across CPUs/groups */
static inline void set_task_rq(struct task_struct *p, unsigned int cpu)
{
#ifdef CONFIG_FAIR_GROUP_SCHED
	p->se.cfs_rq = task_group(p)->cfs_rq[cpu];
	p->se.parent = task_group(p)->se[cpu];
#endif

#ifdef CONFIG_RT_GROUP_SCHED
	p->rt.rt_rq  = task_group(p)->rt_rq[cpu];
	p->rt.parent = task_group(p)->rt_se[cpu];
#endif
}

#else

#ifdef CONFIG_SMP
static int root_task_group_empty(void)
{
	return 1;
}
#endif

static inline void set_task_rq(struct task_struct *p, unsigned int cpu) { }
static inline struct task_group *task_group(struct task_struct *p)
{
	return NULL;
}

#endif	/* CONFIG_GROUP_SCHED */

/* CFS-related fields in a runqueue */
struct cfs_rq {
	struct load_weight load;
	unsigned long nr_running;

	u64 exec_clock;
	u64 min_vruntime;

	struct rb_root tasks_timeline;
	struct rb_node *rb_leftmost;

	struct list_head tasks;
	struct list_head *balance_iterator;

	/*
	 * 'curr' points to currently running entity on this cfs_rq.
	 * It is set to NULL otherwise (i.e when none are currently running).
	 */
	struct sched_entity *curr, *next, *last;

	unsigned int nr_spread_over;

#ifdef CONFIG_FAIR_GROUP_SCHED
	struct rq *rq;	/* cpu runqueue to which this cfs_rq is attached */

	/*
	 * leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
	 * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
	 * (like users, containers etc.)
	 *
	 * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a cpu. This
	 * list is used during load balance.
	 */
	struct list_head leaf_cfs_rq_list;
	struct task_group *tg;	/* group that "owns" this runqueue */

#ifdef CONFIG_SMP
	/*
	 * the part of load.weight contributed by tasks
	 */
	unsigned long task_weight;

	/*
	 *   h_load = weight * f(tg)
	 *
	 * Where f(tg) is the recursive weight fraction assigned to
	 * this group.
	 */
	unsigned long h_load;

	/*
	 * this cpu's part of tg->shares
	 */
	unsigned long shares;

	/*
	 * load.weight at the time we set shares
	 */
	unsigned long rq_weight;
#endif
#endif
};

/* Real-Time classes' related field in a runqueue: */
struct rt_rq {
	struct rt_prio_array active;
	unsigned long rt_nr_running;
#if defined CONFIG_SMP || defined CONFIG_RT_GROUP_SCHED
	struct {
		int curr; /* highest queued rt task prio */
#ifdef CONFIG_SMP
		int next; /* next highest */
#endif
	} highest_prio;
#endif
#ifdef CONFIG_SMP
	unsigned long rt_nr_migratory;
	int overloaded;
	struct plist_head pushable_tasks;
#endif
	int rt_throttled;
	u64 rt_time;
	u64 rt_runtime;
	/* Nests inside the rq lock: */
	spinlock_t rt_runtime_lock;

#ifdef CONFIG_RT_GROUP_SCHED
	unsigned long rt_nr_boosted;

	struct rq *rq;
	struct list_head leaf_rt_rq_list;
	struct task_group *tg;
	struct sched_rt_entity *rt_se;
#endif
};

#ifdef CONFIG_SMP

/*
 * We add the notion of a root-domain which will be used to define per-domain
 * variables. Each exclusive cpuset essentially defines an island domain by
 * fully partitioning the member cpus from any other cpuset. Whenever a new
 * exclusive cpuset is created, we also create and attach a new root-domain
 * object.
 *
 */
struct root_domain {
	atomic_t refcount;
	cpumask_var_t span;
	cpumask_var_t online;

	/*
	 * The "RT overload" flag: it gets set if a CPU has more than
	 * one runnable RT task.
	 */
	cpumask_var_t rto_mask;
	atomic_t rto_count;
#ifdef CONFIG_SMP
	struct cpupri cpupri;
#endif
#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	/*
	 * Preferred wake up cpu nominated by sched_mc balance that will be
	 * used when most cpus are idle in the system indicating overall very
	 * low system utilisation. Triggered at POWERSAVINGS_BALANCE_WAKEUP(2)
	 */
	unsigned int sched_mc_preferred_wakeup_cpu;
#endif
};

/*
 * By default the system creates a single root-domain with all cpus as
 * members (mimicking the global state we have today).
 */
static struct root_domain def_root_domain;

#endif

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the thread migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct rq {
	/* runqueue lock: */
	spinlock_t lock;

	/*
	 * nr_running and cpu_load should be in the same cacheline because
	 * remote CPUs use both these fields when doing load calculation.
	 */
	unsigned long nr_running;
	#define CPU_LOAD_IDX_MAX 5
	unsigned long cpu_load[CPU_LOAD_IDX_MAX];
#ifdef CONFIG_NO_HZ
	unsigned long last_tick_seen;
	unsigned char in_nohz_recently;
#endif
	/* capture load from *all* tasks on this cpu: */
	struct load_weight load;
	unsigned long nr_load_updates;
	u64 nr_switches;

	struct cfs_rq cfs;
	struct rt_rq rt;

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* list of leaf cfs_rq on this cpu: */
	struct list_head leaf_cfs_rq_list;
#endif
#ifdef CONFIG_RT_GROUP_SCHED
	struct list_head leaf_rt_rq_list;
#endif

	/*
	 * This is part of a global counter where only the total sum
	 * over all CPUs matters. A task can increase this counter on
	 * one CPU and if it got migrated afterwards it may decrease
	 * it on another CPU. Always updated under the runqueue lock:
	 */
	unsigned long nr_uninterruptible;

	struct task_struct *curr, *idle;
	unsigned long next_balance;
	struct mm_struct *prev_mm;

	u64 clock;

	atomic_t nr_iowait;

#ifdef CONFIG_SMP
	struct root_domain *rd;
	struct sched_domain *sd;

	unsigned char idle_at_tick;
	/* For active balancing */
	int active_balance;
	int push_cpu;
	/* cpu of this runqueue: */
	int cpu;
	int online;

	unsigned long avg_load_per_task;

	struct task_struct *migration_thread;
	struct list_head migration_queue;
#endif

#ifdef CONFIG_SCHED_HRTICK
#ifdef CONFIG_SMP
	int hrtick_csd_pending;
	struct call_single_data hrtick_csd;
#endif
	struct hrtimer hrtick_timer;
#endif

#ifdef CONFIG_SCHEDSTATS
	/* latency stats */
	struct sched_info rq_sched_info;
	unsigned long long rq_cpu_time;
	/* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

	/* sys_sched_yield() stats */
	unsigned int yld_count;

	/* schedule() stats */
	unsigned int sched_switch;
	unsigned int sched_count;
	unsigned int sched_goidle;

	/* try_to_wake_up() stats */
	unsigned int ttwu_count;
	unsigned int ttwu_local;

	/* BKL stats */
	unsigned int bkl_count;
#endif
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

static inline void check_preempt_curr(struct rq *rq, struct task_struct *p, int sync)
{
	rq->curr->sched_class->check_preempt_curr(rq, p, sync);
}

static inline int cpu_of(struct rq *rq)
{
#ifdef CONFIG_SMP
	return rq->cpu;
#else
	return 0;
#endif
}

/*
 * The domain tree (rq->sd) is protected by RCU's quiescent state transition.
 * See detach_destroy_domains: synchronize_sched for details.
 *
 * The domain tree of any CPU may only be accessed from within
 * preempt-disabled sections.
 */
#define for_each_domain(cpu, __sd) \
	for (__sd = rcu_dereference(cpu_rq(cpu)->sd); __sd; __sd = __sd->parent)

#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		(&__get_cpu_var(runqueues))
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)

static inline void update_rq_clock(struct rq *rq)
{
	rq->clock = sched_clock_cpu(cpu_of(rq));
}

/*
 * Tunables that become constants when CONFIG_SCHED_DEBUG is off:
 */
#ifdef CONFIG_SCHED_DEBUG
# define const_debug __read_mostly
#else
# define const_debug static const
#endif

/**
 * runqueue_is_locked
 *
 * Returns true if the current cpu runqueue is locked.
 * This interface allows printk to be called with the runqueue lock
 * held and know whether or not it is OK to wake up the klogd.
 */
int runqueue_is_locked(void)
{
	int cpu = get_cpu();
	struct rq *rq = cpu_rq(cpu);
	int ret;

	ret = spin_is_locked(&rq->lock);
	put_cpu();
	return ret;
}

/*
 * Debugging: various feature bits
 */

#define SCHED_FEAT(name, enabled)	\
	__SCHED_FEAT_##name ,

enum {
#include "sched_features.h"
};

#undef SCHED_FEAT

#define SCHED_FEAT(name, enabled)	\
	(1UL << __SCHED_FEAT_##name) * enabled |

const_debug unsigned int sysctl_sched_features =
#include "sched_features.h"
	0;

#undef SCHED_FEAT

#ifdef CONFIG_SCHED_DEBUG
#define SCHED_FEAT(name, enabled)	\
	#name ,

static __read_mostly char *sched_feat_names[] = {
#include "sched_features.h"
	NULL
};

#undef SCHED_FEAT

static int sched_feat_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; sched_feat_names[i]; i++) {
		if (!(sysctl_sched_features & (1UL << i)))
			seq_puts(m, "NO_");
		seq_printf(m, "%s ", sched_feat_names[i]);
	}
	seq_puts(m, "\n");

	return 0;
}

static ssize_t
sched_feat_write(struct file *filp, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	char buf[64];
	char *cmp = buf;
	int neg = 0;
	int i;

	if (cnt > 63)
		cnt = 63;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	if (strncmp(buf, "NO_", 3) == 0) {
		neg = 1;
		cmp += 3;
	}

	for (i = 0; sched_feat_names[i]; i++) {
		int len = strlen(sched_feat_names[i]);

		if (strncmp(cmp, sched_feat_names[i], len) == 0) {
			if (neg)
				sysctl_sched_features &= ~(1UL << i);
			else
				sysctl_sched_features |= (1UL << i);
			break;
		}
	}

	if (!sched_feat_names[i])
		return -EINVAL;

	filp->f_pos += cnt;

	return cnt;
}

static int sched_feat_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sched_feat_show, NULL);
}

static struct file_operations sched_feat_fops = {
	.open		= sched_feat_open,
	.write		= sched_feat_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static __init int sched_init_debug(void)
{
	debugfs_create_file("sched_features", 0644, NULL, NULL,
			&sched_feat_fops);

	return 0;
}
late_initcall(sched_init_debug);

#endif

#define sched_feat(x) (sysctl_sched_features & (1UL << __SCHED_FEAT_##x))

/*
 * Number of tasks to iterate in a single balance run.
 * Limited because this is done with IRQs disabled.
 */
const_debug unsigned int sysctl_sched_nr_migrate = 32;

/*
 * ratelimit for updating the group shares.
 * default: 0.25ms
 */
unsigned int sysctl_sched_shares_ratelimit = 250000;

/*
 * Inject some fuzzyness into changing the per-cpu group shares
 * this avoids remote rq-locks at the expense of fairness.
 * default: 4
 */
unsigned int sysctl_sched_shares_thresh = 4;

/*
 * period over which we measure -rt task cpu usage in us.
 * default: 1s
 */
unsigned int sysctl_sched_rt_period = 1000000;

static __read_mostly int scheduler_running;

/*
 * part of the period that we allow rt tasks to run in us.
 * default: 0.95s
 */
int sysctl_sched_rt_runtime = 950000;

static inline u64 global_rt_period(void)
{
	return (u64)sysctl_sched_rt_period * NSEC_PER_USEC;
}

static inline u64 global_rt_runtime(void)
{
	if (sysctl_sched_rt_runtime < 0)
		return RUNTIME_INF;

	return (u64)sysctl_sched_rt_runtime * NSEC_PER_USEC;
}

#ifndef prepare_arch_switch
# define prepare_arch_switch(next)	do { } while (0)
#endif
#ifndef finish_arch_switch
# define finish_arch_switch(prev)	do { } while (0)
#endif

static inline int task_current(struct rq *rq, struct task_struct *p)
{
	return rq->curr == p;
}

#ifndef __ARCH_WANT_UNLOCKED_CTXSW
static inline int task_running(struct rq *rq, struct task_struct *p)
{
	return task_current(rq, p);
}

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	/* this is a valid case when another task releases the spinlock */
	rq->lock.owner = current;
#endif
	/*
	 * If we are tracking spinlock dependencies then we have to
	 * fix up the runqueue lock - which gets 'carried over' from
	 * prev into current:
	 */
	spin_acquire(&rq->lock.dep_map, 0, 0, _THIS_IP_);

	spin_unlock_irq(&rq->lock);
}

#else /* __ARCH_WANT_UNLOCKED_CTXSW */
static inline int task_running(struct rq *rq, struct task_struct *p)
{
#ifdef CONFIG_SMP
	return p->oncpu;
#else
	return task_current(rq, p);
#endif
}

static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
#ifdef CONFIG_SMP
	/*
	 * We can optimise this out completely for !SMP, because the
	 * SMP rebalancing from interrupt is the only thing that cares
	 * here.
	 */
	next->oncpu = 1;
#endif
#ifdef __ARCH_WANT_INTERRUPTS_ON_CTXSW
	spin_unlock_irq(&rq->lock);
#else
	spin_unlock(&rq->lock);
#endif
}

static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_SMP
	/*
	 * After ->oncpu is cleared, the task can be moved to a different CPU.
	 * We must ensure this doesn't happen until the switch is completely
	 * finished.
	 */
	smp_wmb();
	prev->oncpu = 0;
#endif
#ifndef __ARCH_WANT_INTERRUPTS_ON_CTXSW
	local_irq_enable();
#endif
}
#endif /* __ARCH_WANT_UNLOCKED_CTXSW */

/*
 * __task_rq_lock - lock the runqueue a given task resides on.
 * Must be called interrupts disabled.
 */
static inline struct rq *__task_rq_lock(struct task_struct *p)
	__acquires(rq->lock)
{
	for (;;) {
		struct rq *rq = task_rq(p);
		spin_lock(&rq->lock);
		if (likely(rq == task_rq(p)))
			return rq;
		spin_unlock(&rq->lock);
	}
}

/*
 * task_rq_lock - lock the runqueue a given task resides on and disable
 * interrupts. Note the ordering: we can safely lookup the task_rq without
 * explicitly disabling preemption.
 */
static struct rq *task_rq_lock(struct task_struct *p, unsigned long *flags)
	__acquires(rq->lock)
{
	struct rq *rq;

	for (;;) {
		local_irq_save(*flags);
		rq = task_rq(p);
		spin_lock(&rq->lock);
		if (likely(rq == task_rq(p)))
			return rq;
		spin_unlock_irqrestore(&rq->lock, *flags);
	}
}

void task_rq_unlock_wait(struct task_struct *p)
{
	struct rq *rq = task_rq(p);

	smp_mb(); /* spin-unlock-wait is not a full memory barrier */
	spin_unlock_wait(&rq->lock);
}

static void __task_rq_unlock(struct rq *rq)
	__releases(rq->lock)
{
	spin_unlock(&rq->lock);
}

static inline void task_rq_unlock(struct rq *rq, unsigned long *flags)
	__releases(rq->lock)
{
	spin_unlock_irqrestore(&rq->lock, *flags);
}

/*
 * this_rq_lock - lock this runqueue and disable interrupts.
 */
static struct rq *this_rq_lock(void)
	__acquires(rq->lock)
{
	struct rq *rq;

	local_irq_disable();
	rq = this_rq();
	spin_lock(&rq->lock);

	return rq;
}

#ifdef CONFIG_SCHED_HRTICK
/*
 * Use HR-timers to deliver accurate preemption points.
 *
 * Its all a bit involved since we cannot program an hrt while holding the
 * rq->lock. So what we do is store a state in in rq->hrtick_* and ask for a
 * reschedule event.
 *
 * When we get rescheduled we reprogram the hrtick_timer outside of the
 * rq->lock.
 */

/*
 * Use hrtick when:
 *  - enabled by features
 *  - hrtimer is actually high res
 */
static inline int hrtick_enabled(struct rq *rq)
{
	if (!sched_feat(HRTICK))
		return 0;
	if (!cpu_active(cpu_of(rq)))
		return 0;
	return hrtimer_is_hres_active(&rq->hrtick_timer);
}

static void hrtick_clear(struct rq *rq)
{
	if (hrtimer_active(&rq->hrtick_timer))
		hrtimer_cancel(&rq->hrtick_timer);
}

/*
 * High-resolution timer tick.
 * Runs from hardirq context with interrupts disabled.
 */
static enum hrtimer_restart hrtick(struct hrtimer *timer)
{
	struct rq *rq = container_of(timer, struct rq, hrtick_timer);

	WARN_ON_ONCE(cpu_of(rq) != smp_processor_id());

	spin_lock(&rq->lock);
	update_rq_clock(rq);
	rq->curr->sched_class->task_tick(rq, rq->curr, 1);
	spin_unlock(&rq->lock);

	return HRTIMER_NORESTART;
}

#ifdef CONFIG_SMP
/*
 * called from hardirq (IPI) context
 */
static void __hrtick_start(void *arg)
{
	struct rq *rq = arg;

	spin_lock(&rq->lock);
	hrtimer_restart(&rq->hrtick_timer);
	rq->hrtick_csd_pending = 0;
	spin_unlock(&rq->lock);
}

/*
 * Called to set the hrtick timer state.
 *
 * called with rq->lock held and irqs disabled
 */
static void hrtick_start(struct rq *rq, u64 delay)
{
	struct hrtimer *timer = &rq->hrtick_timer;
	ktime_t time = ktime_add_ns(timer->base->get_time(), delay);

	hrtimer_set_expires(timer, time);

	if (rq == this_rq()) {
		hrtimer_restart(timer);
	} else if (!rq->hrtick_csd_pending) {
		__smp_call_function_single(cpu_of(rq), &rq->hrtick_csd, 0);
		rq->hrtick_csd_pending = 1;
	}
}

static int
hotplug_hrtick(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int cpu = (int)(long)hcpu;

	switch (action) {
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		hrtick_clear(cpu_rq(cpu));
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static __init void init_hrtick(void)
{
	hotcpu_notifier(hotplug_hrtick, 0);
}
#else
/*
 * Called to set the hrtick timer state.
 *
 * called with rq->lock held and irqs disabled
 */
static void hrtick_start(struct rq *rq, u64 delay)
{
	__hrtimer_start_range_ns(&rq->hrtick_timer, ns_to_ktime(delay), 0,
			HRTIMER_MODE_REL, 0);
}

static inline void init_hrtick(void)
{
}
#endif /* CONFIG_SMP */

static void init_rq_hrtick(struct rq *rq)
{
#ifdef CONFIG_SMP
	rq->hrtick_csd_pending = 0;

	rq->hrtick_csd.flags = 0;
	rq->hrtick_csd.func = __hrtick_start;
	rq->hrtick_csd.info = rq;
#endif

	hrtimer_init(&rq->hrtick_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rq->hrtick_timer.function = hrtick;
}
#else	/* CONFIG_SCHED_HRTICK */
static inline void hrtick_clear(struct rq *rq)
{
}

static inline void init_rq_hrtick(struct rq *rq)
{
}

static inline void init_hrtick(void)
{
}
#endif	/* CONFIG_SCHED_HRTICK */

/*
 * resched_task - mark a task 'to be rescheduled now'.
 *
 * On UP this means the setting of the need_resched flag, on SMP it
 * might also involve a cross-CPU call to trigger the scheduler on
 * the target CPU.
 */
#ifdef CONFIG_SMP

#ifndef tsk_is_polling
#define tsk_is_polling(t) test_tsk_thread_flag(t, TIF_POLLING_NRFLAG)
#endif

static void resched_task(struct task_struct *p)
{
	int cpu;

	assert_spin_locked(&task_rq(p)->lock);

	if (test_tsk_need_resched(p))
		return;

	set_tsk_need_resched(p);

	cpu = task_cpu(p);
	if (cpu == smp_processor_id())
		return;

	/* NEED_RESCHED must be visible before we test polling */
	smp_mb();
	if (!tsk_is_polling(p))
		smp_send_reschedule(cpu);
}

static void resched_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	if (!spin_trylock_irqsave(&rq->lock, flags))
		return;
	resched_task(cpu_curr(cpu));
	spin_unlock_irqrestore(&rq->lock, flags);
}

#ifdef CONFIG_NO_HZ
/*
 * When add_timer_on() enqueues a timer into the timer wheel of an
 * idle CPU then this timer might expire before the next timer event
 * which is scheduled to wake up that CPU. In case of a completely
 * idle system the next event might even be infinite time into the
 * future. wake_up_idle_cpu() ensures that the CPU is woken up and
 * leaves the inner idle loop so the newly added timer is taken into
 * account when the CPU goes back to idle and evaluates the timer
 * wheel for the next timer event.
 */
void wake_up_idle_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);

	if (cpu == smp_processor_id())
		return;

	/*
	 * This is safe, as this function is called with the timer
	 * wheel base lock of (cpu) held. When the CPU is on the way
	 * to idle and has not yet set rq->curr to idle then it will
	 * be serialized on the timer wheel base lock and take the new
	 * timer into account automatically.
	 */
	if (rq->curr != rq->idle)
		return;

	/*
	 * We can set TIF_RESCHED on the idle task of the other CPU
	 * lockless. The worst case is that the other CPU runs the
	 * idle task through an additional NOOP schedule()
	 */
	set_tsk_need_resched(rq->idle);

	/* NEED_RESCHED must be visible before we test polling */
	smp_mb();
	if (!tsk_is_polling(rq->idle))
		smp_send_reschedule(cpu);
}
#endif /* CONFIG_NO_HZ */

#else /* !CONFIG_SMP */
static void resched_task(struct task_struct *p)
{
	assert_spin_locked(&task_rq(p)->lock);
	set_tsk_need_resched(p);
}
#endif /* CONFIG_SMP */

#if BITS_PER_LONG == 32
# define WMULT_CONST	(~0UL)
#else
# define WMULT_CONST	(1UL << 32)
#endif

#define WMULT_SHIFT	32

/*
 * Shift right and round:
 */
#define SRR(x, y) (((x) + (1UL << ((y) - 1))) >> (y))

/*
 * delta *= weight / lw
 */
static unsigned long
calc_delta_mine(unsigned long delta_exec, unsigned long weight,
		struct load_weight *lw)
{
	u64 tmp;

	if (!lw->inv_weight) {
		if (BITS_PER_LONG > 32 && unlikely(lw->weight >= WMULT_CONST))
			lw->inv_weight = 1;
		else
			lw->inv_weight = 1 + (WMULT_CONST-lw->weight/2)
				/ (lw->weight+1);
	}

	tmp = (u64)delta_exec * weight;
	/*
	 * Check whether we'd overflow the 64-bit multiplication:
	 */
	if (unlikely(tmp > WMULT_CONST))
		tmp = SRR(SRR(tmp, WMULT_SHIFT/2) * lw->inv_weight,
			WMULT_SHIFT/2);
	else
		tmp = SRR(tmp * lw->inv_weight, WMULT_SHIFT);

	return (unsigned long)min(tmp, (u64)(unsigned long)LONG_MAX);
}

static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}

static inline void update_load_sub(struct load_weight *lw, unsigned long dec)
{
	lw->weight -= dec;
	lw->inv_weight = 0;
}

/*
 * To aid in avoiding the subversion of "niceness" due to uneven distribution
 * of tasks with abnormal "nice" values across CPUs the contribution that
 * each task makes to its run queue's load is weighted according to its
 * scheduling class and "nice" value. For SCHED_NORMAL tasks this is just a
 * scaled version of the new time slice allocation that they receive on time
 * slice expiry etc.
 */

#define WEIGHT_IDLEPRIO                3
#define WMULT_IDLEPRIO         1431655765

/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage. (to achieve that we use a multiplier of 1.25.
 * If a task goes up by ~10% and another task goes down by ~10% then
 * the relative distance between them is ~25%.)
 */
static const int prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};

/*
 * Inverse (2^32/x) values of the prio_to_weight[] array, precalculated.
 *
 * In cases where the weight does not change often, we can use the
 * precalculated inverse to speed up arithmetics by turning divisions
 * into multiplications:
 */
static const u32 prio_to_wmult[40] = {
 /* -20 */     48388,     59856,     76040,     92818,    118348,
 /* -15 */    147320,    184698,    229616,    287308,    360437,
 /* -10 */    449829,    563644,    704093,    875809,   1099582,
 /*  -5 */   1376151,   1717300,   2157191,   2708050,   3363326,
 /*   0 */   4194304,   5237765,   6557202,   8165337,  10153587,
 /*   5 */  12820798,  15790321,  19976592,  24970740,  31350126,
 /*  10 */  39045157,  49367440,  61356676,  76695844,  95443717,
 /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};

static void activate_task(struct rq *rq, struct task_struct *p, int wakeup);

/*
 * runqueue iterator, to support SMP load-balancing between different
 * scheduling classes, without having to expose their internal data
 * structures to the load-balancing proper:
 */
struct rq_iterator {
	void *arg;
	struct task_struct *(*start)(void *);
	struct task_struct *(*next)(void *);
};

#ifdef CONFIG_SMP
static unsigned long
balance_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
	      unsigned long max_load_move, struct sched_domain *sd,
	      enum cpu_idle_type idle, int *all_pinned,
	      int *this_best_prio, struct rq_iterator *iterator);

static int
iter_move_one_task(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle,
		   struct rq_iterator *iterator);
#endif

/* Time spent by the tasks of the cpu accounting group executing in ... */
enum cpuacct_stat_index {
	CPUACCT_STAT_USER,	/* ... user mode */
	CPUACCT_STAT_SYSTEM,	/* ... kernel mode */

	CPUACCT_STAT_NSTATS,
};

#ifdef CONFIG_CGROUP_CPUACCT
static void cpuacct_charge(struct task_struct *tsk, u64 cputime);
static void cpuacct_update_stats(struct task_struct *tsk,
		enum cpuacct_stat_index idx, cputime_t val);
#else
static inline void cpuacct_charge(struct task_struct *tsk, u64 cputime) {}
static inline void cpuacct_update_stats(struct task_struct *tsk,
		enum cpuacct_stat_index idx, cputime_t val) {}
#endif

static inline void inc_cpu_load(struct rq *rq, unsigned long load)
{
	update_load_add(&rq->load, load);
}

static inline void dec_cpu_load(struct rq *rq, unsigned long load)
{
	update_load_sub(&rq->load, load);
}

#if (defined(CONFIG_SMP) && defined(CONFIG_FAIR_GROUP_SCHED)) || defined(CONFIG_RT_GROUP_SCHED)
typedef int (*tg_visitor)(struct task_group *, void *);

/*
 * Iterate the full tree, calling @down when first entering a node and @up when
 * leaving it for the final time.
 */
static int walk_tg_tree(tg_visitor down, tg_visitor up, void *data)
{
	struct task_group *parent, *child;
	int ret;

	rcu_read_lock();
	parent = &root_task_group;
down:
	ret = (*down)(parent, data);
	if (ret)
		goto out_unlock;
	list_for_each_entry_rcu(child, &parent->children, siblings) {
		parent = child;
		goto down;

up:
		continue;
	}
	ret = (*up)(parent, data);
	if (ret)
		goto out_unlock;

	child = parent;
	parent = parent->parent;
	if (parent)
		goto up;
out_unlock:
	rcu_read_unlock();

	return ret;
}

static int tg_nop(struct task_group *tg, void *data)
{
	return 0;
}
#endif

#ifdef CONFIG_SMP
static unsigned long source_load(int cpu, int type);
static unsigned long target_load(int cpu, int type);
static int task_hot(struct task_struct *p, u64 now, struct sched_domain *sd);

static unsigned long cpu_avg_load_per_task(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long nr_running = ACCESS_ONCE(rq->nr_running);

	if (nr_running)
		rq->avg_load_per_task = rq->load.weight / nr_running;
	else
		rq->avg_load_per_task = 0;

	return rq->avg_load_per_task;
}

#ifdef CONFIG_FAIR_GROUP_SCHED

static void __set_se_shares(struct sched_entity *se, unsigned long shares);

/*
 * Calculate and set the cpu's group shares.
 */
static void
update_group_shares_cpu(struct task_group *tg, int cpu,
			unsigned long sd_shares, unsigned long sd_rq_weight)
{
	unsigned long shares;
	unsigned long rq_weight;

	if (!tg->se[cpu])
		return;

	rq_weight = tg->cfs_rq[cpu]->rq_weight;

	/*
	 *           \Sum shares * rq_weight
	 * shares =  -----------------------
	 *               \Sum rq_weight
	 *
	 */
	shares = (sd_shares * rq_weight) / sd_rq_weight;
	shares = clamp_t(unsigned long, shares, MIN_SHARES, MAX_SHARES);

	if (abs(shares - tg->se[cpu]->load.weight) >
			sysctl_sched_shares_thresh) {
		struct rq *rq = cpu_rq(cpu);
		unsigned long flags;

		spin_lock_irqsave(&rq->lock, flags);
		tg->cfs_rq[cpu]->shares = shares;

		__set_se_shares(tg->se[cpu], shares);
		spin_unlock_irqrestore(&rq->lock, flags);
	}
}

/*
 * Re-compute the task group their per cpu shares over the given domain.
 * This needs to be done in a bottom-up fashion because the rq weight of a
 * parent group depends on the shares of its child groups.
 */
static int tg_shares_up(struct task_group *tg, void *data)
{
	unsigned long weight, rq_weight = 0;
	unsigned long shares = 0;
	struct sched_domain *sd = data;
	int i;

	for_each_cpu(i, sched_domain_span(sd)) {
		/*
		 * If there are currently no tasks on the cpu pretend there
		 * is one of average load so that when a new task gets to
		 * run here it will not get delayed by group starvation.
		 */
		weight = tg->cfs_rq[i]->load.weight;
		if (!weight)
			weight = NICE_0_LOAD;

		tg->cfs_rq[i]->rq_weight = weight;
		rq_weight += weight;
		shares += tg->cfs_rq[i]->shares;
	}

	if ((!shares && rq_weight) || shares > tg->shares)
		shares = tg->shares;

	if (!sd->parent || !(sd->parent->flags & SD_LOAD_BALANCE))
		shares = tg->shares;

	for_each_cpu(i, sched_domain_span(sd))
		update_group_shares_cpu(tg, i, shares, rq_weight);

	return 0;
}

/*
 * Compute the cpu's hierarchical load factor for each task group.
 * This needs to be done in a top-down fashion because the load of a child
 * group is a fraction of its parents load.
 */
static int tg_load_down(struct task_group *tg, void *data)
{
	unsigned long load;
	long cpu = (long)data;

	if (!tg->parent) {
		load = cpu_rq(cpu)->load.weight;
	} else {
		load = tg->parent->cfs_rq[cpu]->h_load;
		load *= tg->cfs_rq[cpu]->shares;
		load /= tg->parent->cfs_rq[cpu]->load.weight + 1;
	}

	tg->cfs_rq[cpu]->h_load = load;

	return 0;
}

static void update_shares(struct sched_domain *sd)
{
	u64 now = cpu_clock(raw_smp_processor_id());
	s64 elapsed = now - sd->last_update;

	if (elapsed >= (s64)(u64)sysctl_sched_shares_ratelimit) {
		sd->last_update = now;
		walk_tg_tree(tg_nop, tg_shares_up, sd);
	}
}

static void update_shares_locked(struct rq *rq, struct sched_domain *sd)
{
	spin_unlock(&rq->lock);
	update_shares(sd);
	spin_lock(&rq->lock);
}

static void update_h_load(long cpu)
{
	walk_tg_tree(tg_load_down, tg_nop, (void *)cpu);
}

#else

static inline void update_shares(struct sched_domain *sd)
{
}

static inline void update_shares_locked(struct rq *rq, struct sched_domain *sd)
{
}

#endif

#ifdef CONFIG_PREEMPT

/*
 * fair double_lock_balance: Safely acquires both rq->locks in a fair
 * way at the expense of forcing extra atomic operations in all
 * invocations.  This assures that the double_lock is acquired using the
 * same underlying policy as the spinlock_t on this architecture, which
 * reduces latency compared to the unfair variant below.  However, it
 * also adds more overhead and therefore may reduce throughput.
 */
static inline int _double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	spin_unlock(&this_rq->lock);
	double_rq_lock(this_rq, busiest);

	return 1;
}

#else
/*
 * Unfair double_lock_balance: Optimizes throughput at the expense of
 * latency by eliminating extra atomic operations when the locks are
 * already in proper order on entry.  This favors lower cpu-ids and will
 * grant the double lock to lower cpus over higher ids under contention,
 * regardless of entry order into the function.
 */
static int _double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	int ret = 0;

	if (unlikely(!spin_trylock(&busiest->lock))) {
		if (busiest < this_rq) {
			spin_unlock(&this_rq->lock);
			spin_lock(&busiest->lock);
			spin_lock_nested(&this_rq->lock, SINGLE_DEPTH_NESTING);
			ret = 1;
		} else
			spin_lock_nested(&busiest->lock, SINGLE_DEPTH_NESTING);
	}
	return ret;
}

#endif /* CONFIG_PREEMPT */

/*
 * double_lock_balance - lock the busiest runqueue, this_rq is locked already.
 */
static int double_lock_balance(struct rq *this_rq, struct rq *busiest)
{
	if (unlikely(!irqs_disabled())) {
		/* printk() doesn't work good under rq->lock */
		spin_unlock(&this_rq->lock);
		BUG_ON(1);
	}

	return _double_lock_balance(this_rq, busiest);
}

static inline void double_unlock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(busiest->lock)
{
	spin_unlock(&busiest->lock);
	lock_set_subclass(&this_rq->lock.dep_map, 0, _RET_IP_);
}
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
static void cfs_rq_set_shares(struct cfs_rq *cfs_rq, unsigned long shares)
{
#ifdef CONFIG_SMP
	cfs_rq->shares = shares;
#endif
}
#endif

#include "sched_stats.h"
#include "sched_idletask.c"
#include "sched_fair.c"
#include "sched_rt.c"
#ifdef CONFIG_SCHED_DEBUG
# include "sched_debug.c"
#endif

#define sched_class_highest (&rt_sched_class)
#define for_each_class(class) \
   for (class = sched_class_highest; class; class = class->next)

static void inc_nr_running(struct rq *rq)
{
	rq->nr_running++;
}

static void dec_nr_running(struct rq *rq)
{
	rq->nr_running--;
}

static void set_load_weight(struct task_struct *p)
{
	if (task_has_rt_policy(p)) {
		p->se.load.weight = prio_to_weight[0] * 2;
		p->se.load.inv_weight = prio_to_wmult[0] >> 1;
		return;
	}

	/*
	 * SCHED_IDLE tasks get minimal weight:
	 */
	if (p->policy == SCHED_IDLE) {
		p->se.load.weight = WEIGHT_IDLEPRIO;
		p->se.load.inv_weight = WMULT_IDLEPRIO;
		return;
	}

	p->se.load.weight = prio_to_weight[p->static_prio - MAX_RT_PRIO];
	p->se.load.inv_weight = prio_to_wmult[p->static_prio - MAX_RT_PRIO];
}

static void update_avg(u64 *avg, u64 sample)
{
	s64 diff = sample - *avg;
	*avg += diff >> 3;
}

static void enqueue_task(struct rq *rq, struct task_struct *p, int wakeup)
{
	if (wakeup)
		p->se.start_runtime = p->se.sum_exec_runtime;

	sched_info_queued(p);
	p->sched_class->enqueue_task(rq, p, wakeup);
	p->se.on_rq = 1;
}

static void dequeue_task(struct rq *rq, struct task_struct *p, int sleep)
{
	if (sleep) {
		if (p->se.last_wakeup) {
			update_avg(&p->se.avg_overlap,
				p->se.sum_exec_runtime - p->se.last_wakeup);
			p->se.last_wakeup = 0;
		} else {
			update_avg(&p->se.avg_wakeup,
				sysctl_sched_wakeup_granularity);
		}
	}

	sched_info_dequeued(p);
	p->sched_class->dequeue_task(rq, p, sleep);
	p->se.on_rq = 0;
}

/*
 * __normal_prio - return the priority that is based on the static prio
 */
static inline int __normal_prio(struct task_struct *p)
{
	return p->static_prio;
}

/*
 * Calculate the expected normal priority: i.e. priority
 * without taking RT-inheritance into account. Might be
 * boosted by interactivity modifiers. Changes upon fork,
 * setprio syscalls, and whenever the interactivity
 * estimator recalculates.
 */
static inline int normal_prio(struct task_struct *p)
{
	int prio;

	if (task_has_rt_policy(p))
		prio = MAX_RT_PRIO-1 - p->rt_priority;
	else
		prio = __normal_prio(p);
	return prio;
}

/*
 * Calculate the current priority, i.e. the priority
 * taken into account by the scheduler. This value might
 * be boosted by RT tasks, or might be boosted by
 * interactivity modifiers. Will be RT if the task got
 * RT-boosted. If not then it returns p->normal_prio.
 */
static int effective_prio(struct task_struct *p)
{
	p->normal_prio = normal_prio(p);
	/*
	 * If we are RT tasks or we were boosted to RT priority,
	 * keep the priority unchanged. Otherwise, update priority
	 * to the normal priority:
	 */
	if (!rt_prio(p->prio))
		return p->normal_prio;
	return p->prio;
}

/*
 * activate_task - move a task to the runqueue.
 */
static void activate_task(struct rq *rq, struct task_struct *p, int wakeup)
{
	if (task_contributes_to_load(p))
		rq->nr_uninterruptible--;

	enqueue_task(rq, p, wakeup);
	inc_nr_running(rq);
}

/*
 * deactivate_task - remove a task from the runqueue.
 */
static void deactivate_task(struct rq *rq, struct task_struct *p, int sleep)
{
	if (task_contributes_to_load(p))
		rq->nr_uninterruptible++;

	dequeue_task(rq, p, sleep);
	dec_nr_running(rq);
}

/**
 * task_curr - is this task currently executing on a CPU?
 * @p: the task in question.
 */
inline int task_curr(const struct task_struct *p)
{
	return cpu_curr(task_cpu(p)) == p;
}

static inline void __set_task_cpu(struct task_struct *p, unsigned int cpu)
{
	set_task_rq(p, cpu);
#ifdef CONFIG_SMP
	/*
	 * After ->cpu is set up to a new value, task_rq_lock(p, ...) can be
	 * successfuly executed on another CPU. We must ensure that updates of
	 * per-task data have been completed by this moment.
	 */
	smp_wmb();
	task_thread_info(p)->cpu = cpu;
#endif
}

static inline void check_class_changed(struct rq *rq, struct task_struct *p,
				       const struct sched_class *prev_class,
				       int oldprio, int running)
{
	if (prev_class != p->sched_class) {
		if (prev_class->switched_from)
			prev_class->switched_from(rq, p, running);
		p->sched_class->switched_to(rq, p, running);
	} else
		p->sched_class->prio_changed(rq, p, oldprio, running);
}

#ifdef CONFIG_SMP

/* Used instead of source_load when we know the type == 0 */
static unsigned long weighted_cpuload(const int cpu)
{
	return cpu_rq(cpu)->load.weight;
}

/*
 * Is this task likely cache-hot:
 */
static int
task_hot(struct task_struct *p, u64 now, struct sched_domain *sd)
{
	s64 delta;

	/*
	 * Buddy candidates are cache hot:
	 */
	if (sched_feat(CACHE_HOT_BUDDY) &&
			(&p->se == cfs_rq_of(&p->se)->next ||
			 &p->se == cfs_rq_of(&p->se)->last))
		return 1;

	if (p->sched_class != &fair_sched_class)
		return 0;

	if (sysctl_sched_migration_cost == -1)
		return 1;
	if (sysctl_sched_migration_cost == 0)
		return 0;

	delta = now - p->se.exec_start;

	return delta < (s64)sysctl_sched_migration_cost;
}


void set_task_cpu(struct task_struct *p, unsigned int new_cpu)
{
	int old_cpu = task_cpu(p);
	struct rq *old_rq = cpu_rq(old_cpu), *new_rq = cpu_rq(new_cpu);
	struct cfs_rq *old_cfsrq = task_cfs_rq(p),
		      *new_cfsrq = cpu_cfs_rq(old_cfsrq, new_cpu);
	u64 clock_offset;

	clock_offset = old_rq->clock - new_rq->clock;

	trace_sched_migrate_task(p, task_cpu(p), new_cpu);

#ifdef CONFIG_SCHEDSTATS
	if (p->se.wait_start)
		p->se.wait_start -= clock_offset;
	if (p->se.sleep_start)
		p->se.sleep_start -= clock_offset;
	if (p->se.block_start)
		p->se.block_start -= clock_offset;
	if (old_cpu != new_cpu) {
		schedstat_inc(p, se.nr_migrations);
		if (task_hot(p, old_rq->clock, NULL))
			schedstat_inc(p, se.nr_forced2_migrations);
	}
#endif
	p->se.vruntime -= old_cfsrq->min_vruntime -
					 new_cfsrq->min_vruntime;

	__set_task_cpu(p, new_cpu);
}

struct migration_req {
	struct list_head list;

	struct task_struct *task;
	int dest_cpu;

	struct completion done;
};

/*
 * The task's runqueue lock must be held.
 * Returns true if you have to wait for migration thread.
 */
static int
migrate_task(struct task_struct *p, int dest_cpu, struct migration_req *req)
{
	struct rq *rq = task_rq(p);

	/*
	 * If the task is not on a runqueue (and not running), then
	 * it is sufficient to simply update the task's cpu field.
	 */
	if (!p->se.on_rq && !task_running(rq, p)) {
		set_task_cpu(p, dest_cpu);
		return 0;
	}

	init_completion(&req->done);
	req->task = p;
	req->dest_cpu = dest_cpu;
	list_add(&req->list, &rq->migration_queue);

	return 1;
}

/*
 * wait_task_inactive - wait for a thread to unschedule.
 *
 * If @match_state is nonzero, it's the @p->state value just checked and
 * not expected to change.  If it changes, i.e. @p might have woken up,
 * then return zero.  When we succeed in waiting for @p to be off its CPU,
 * we return a positive number (its total switch count).  If a second call
 * a short while later returns the same number, the caller can be sure that
 * @p has remained unscheduled the whole time.
 *
 * The caller must ensure that the task *will* unschedule sometime soon,
 * else this function might spin for a *long* time. This function can't
 * be called with interrupts off, or it may introduce deadlock with
 * smp_call_function() if an IPI is sent by the same process we are
 * waiting to become inactive.
 */
unsigned long wait_task_inactive(struct task_struct *p, long match_state)
{
	unsigned long flags;
	int running, on_rq;
	unsigned long ncsw;
	struct rq *rq;

	for (;;) {
		/*
		 * We do the initial early heuristics without holding
		 * any task-queue locks at all. We'll only try to get
		 * the runqueue lock when things look like they will
		 * work out!
		 */
		rq = task_rq(p);

		/*
		 * If the task is actively running on another CPU
		 * still, just relax and busy-wait without holding
		 * any locks.
		 *
		 * NOTE! Since we don't hold any locks, it's not
		 * even sure that "rq" stays as the right runqueue!
		 * But we don't care, since "task_running()" will
		 * return false if the runqueue has changed and p
		 * is actually now running somewhere else!
		 */
		while (task_running(rq, p)) {
			if (match_state && unlikely(p->state != match_state))
				return 0;
			cpu_relax();
		}

		/*
		 * Ok, time to look more closely! We need the rq
		 * lock now, to be *sure*. If we're wrong, we'll
		 * just go back and repeat.
		 */
		rq = task_rq_lock(p, &flags);
		trace_sched_wait_task(rq, p);
		running = task_running(rq, p);
		on_rq = p->se.on_rq;
		ncsw = 0;
		if (!match_state || p->state == match_state)
			ncsw = p->nvcsw | LONG_MIN; /* sets MSB */
		task_rq_unlock(rq, &flags);

		/*
		 * If it changed from the expected state, bail out now.
		 */
		if (unlikely(!ncsw))
			break;

		/*
		 * Was it really running after all now that we
		 * checked with the proper locks actually held?
		 *
		 * Oops. Go back and try again..
		 */
		if (unlikely(running)) {
			cpu_relax();
			continue;
		}

		/*
		 * It's not enough that it's not actively running,
		 * it must be off the runqueue _entirely_, and not
		 * preempted!
		 *
		 * So if it was still runnable (but just not actively
		 * running right now), it's preempted, and we should
		 * yield - it could be a while.
		 */
		if (unlikely(on_rq)) {
			schedule_timeout_uninterruptible(1);
			continue;
		}

		/*
		 * Ahh, all good. It wasn't running, and it wasn't
		 * runnable, which means that it will never become
		 * running in the future either. We're all done!
		 */
		break;
	}

	return ncsw;
}

/***
 * kick_process - kick a running thread to enter/exit the kernel
 * @p: the to-be-kicked thread
 *
 * Cause a process which is running on another CPU to enter
 * kernel-mode, without any delay. (to get signals handled.)
 *
 * NOTE: this function doesnt have to take the runqueue lock,
 * because all it wants to ensure is that the remote task enters
 * the kernel. If the IPI races and the task has been migrated
 * to another CPU then no harm is done and the purpose has been
 * achieved as well.
 */
void kick_process(struct task_struct *p)
{
	int cpu;

	preempt_disable();
	cpu = task_cpu(p);
	if ((cpu != smp_processor_id()) && task_curr(p))
		smp_send_reschedule(cpu);
	preempt_enable();
}

/*
 * Return a low guess at the load of a migration-source cpu weighted
 * according to the scheduling class and "nice" value.
 *
 * We want to under-estimate the load of migration sources, to
 * balance conservatively.
 */
static unsigned long source_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return min(rq->cpu_load[type-1], total);
}

/*
 * Return a high guess at the load of a migration-target cpu weighted
 * according to the scheduling class and "nice" value.
 */
static unsigned long target_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return max(rq->cpu_load[type-1], total);
}

/*
 * find_idlest_group finds and returns the least busy CPU group within the
 * domain.
 */
static struct sched_group *
find_idlest_group(struct sched_domain *sd, struct task_struct *p, int this_cpu)
{
	struct sched_group *idlest = NULL, *this = NULL, *group = sd->groups;
	unsigned long min_load = ULONG_MAX, this_load = 0;
	int load_idx = sd->forkexec_idx;
	int imbalance = 100 + (sd->imbalance_pct-100)/2;

	do {
		unsigned long load, avg_load;
		int local_group;
		int i;

		/* Skip over this group if it has no CPUs allowed */
		if (!cpumask_intersects(sched_group_cpus(group),
					&p->cpus_allowed))
			continue;

		local_group = cpumask_test_cpu(this_cpu,
					       sched_group_cpus(group));

		/* Tally up the load of all CPUs in the group */
		avg_load = 0;

		for_each_cpu(i, sched_group_cpus(group)) {
			/* Bias balancing toward cpus of our domain */
			if (local_group)
				load = source_load(i, load_idx);
			else
				load = target_load(i, load_idx);

			avg_load += load;
		}

		/* Adjust by relative CPU power of the group */
		avg_load = sg_div_cpu_power(group,
				avg_load * SCHED_LOAD_SCALE);

		if (local_group) {
			this_load = avg_load;
			this = group;
		} else if (avg_load < min_load) {
			min_load = avg_load;
			idlest = group;
		}
	} while (group = group->next, group != sd->groups);

	if (!idlest || 100*this_load < imbalance*min_load)
		return NULL;
	return idlest;
}

/*
 * find_idlest_cpu - find the idlest cpu among the cpus in group.
 */
static int
find_idlest_cpu(struct sched_group *group, struct task_struct *p, int this_cpu)
{
	unsigned long load, min_load = ULONG_MAX;
	int idlest = -1;
	int i;

	/* Traverse only the allowed CPUs */
	for_each_cpu_and(i, sched_group_cpus(group), &p->cpus_allowed) {
		load = weighted_cpuload(i);

		if (load < min_load || (load == min_load && i == this_cpu)) {
			min_load = load;
			idlest = i;
		}
	}

	return idlest;
}

/*
 * sched_balance_self: balance the current task (running on cpu) in domains
 * that have the 'flag' flag set. In practice, this is SD_BALANCE_FORK and
 * SD_BALANCE_EXEC.
 *
 * Balance, ie. select the least loaded group.
 *
 * Returns the target CPU number, or the same CPU if no balancing is needed.
 *
 * preempt must be disabled.
 */
static int sched_balance_self(int cpu, int flag)
{
	struct task_struct *t = current;
	struct sched_domain *tmp, *sd = NULL;

	for_each_domain(cpu, tmp) {
		/*
		 * If power savings logic is enabled for a domain, stop there.
		 */
		if (tmp->flags & SD_POWERSAVINGS_BALANCE)
			break;
		if (tmp->flags & flag)
			sd = tmp;
	}

	if (sd)
		update_shares(sd);

	while (sd) {
		struct sched_group *group;
		int new_cpu, weight;

		if (!(sd->flags & flag)) {
			sd = sd->child;
			continue;
		}

		group = find_idlest_group(sd, t, cpu);
		if (!group) {
			sd = sd->child;
			continue;
		}

		new_cpu = find_idlest_cpu(group, t, cpu);
		if (new_cpu == -1 || new_cpu == cpu) {
			/* Now try balancing at a lower domain level of cpu */
			sd = sd->child;
			continue;
		}

		/* Now try balancing at a lower domain level of new_cpu */
		cpu = new_cpu;
		weight = cpumask_weight(sched_domain_span(sd));
		sd = NULL;
		for_each_domain(cpu, tmp) {
			if (weight <= cpumask_weight(sched_domain_span(tmp)))
				break;
			if (tmp->flags & flag)
				sd = tmp;
		}
		/* while loop will break here if sd == NULL */
	}

	return cpu;
}

#endif /* CONFIG_SMP */

/***
 * try_to_wake_up - wake up a thread
 * @p: the to-be-woken-up thread
 * @state: the mask of task states that can be woken
 * @sync: do a synchronous wakeup?
 *
 * Put it on the run-queue if it's not already there. The "current"
 * thread is always on the run-queue (except when the actual
 * re-schedule is in progress), and as such you're allowed to do
 * the simpler "current->state = TASK_RUNNING" to mark yourself
 * runnable without the overhead of this.
 *
 * returns failure only if the task is already active.
 */
static int try_to_wake_up(struct task_struct *p, unsigned int state, int sync)
{
	int cpu, orig_cpu, this_cpu, success = 0;
	unsigned long flags;
	long old_state;
	struct rq *rq;

	if (!sched_feat(SYNC_WAKEUPS))
		sync = 0;

#ifdef CONFIG_SMP
	if (sched_feat(LB_WAKEUP_UPDATE) && !root_task_group_empty()) {
		struct sched_domain *sd;

		this_cpu = raw_smp_processor_id();
		cpu = task_cpu(p);

		for_each_domain(this_cpu, sd) {
			if (cpumask_test_cpu(cpu, sched_domain_span(sd))) {
				update_shares(sd);
				break;
			}
		}
	}
#endif

	smp_wmb();
	rq = task_rq_lock(p, &flags);
	update_rq_clock(rq);
	old_state = p->state;
	if (!(old_state & state))
		goto out;

	if (p->se.on_rq)
		goto out_running;

	cpu = task_cpu(p);
	orig_cpu = cpu;
	this_cpu = smp_processor_id();

#ifdef CONFIG_SMP
	if (unlikely(task_running(rq, p)))
		goto out_activate;

	cpu = p->sched_class->select_task_rq(p, sync);
	if (cpu != orig_cpu) {
		set_task_cpu(p, cpu);
		task_rq_unlock(rq, &flags);
		/* might preempt at this point */
		rq = task_rq_lock(p, &flags);
		old_state = p->state;
		if (!(old_state & state))
			goto out;
		if (p->se.on_rq)
			goto out_running;

		this_cpu = smp_processor_id();
		cpu = task_cpu(p);
	}

#ifdef CONFIG_SCHEDSTATS
	schedstat_inc(rq, ttwu_count);
	if (cpu == this_cpu)
		schedstat_inc(rq, ttwu_local);
	else {
		struct sched_domain *sd;
		for_each_domain(this_cpu, sd) {
			if (cpumask_test_cpu(cpu, sched_domain_span(sd))) {
				schedstat_inc(sd, ttwu_wake_remote);
				break;
			}
		}
	}
#endif /* CONFIG_SCHEDSTATS */

out_activate:
#endif /* CONFIG_SMP */
	schedstat_inc(p, se.nr_wakeups);
	if (sync)
		schedstat_inc(p, se.nr_wakeups_sync);
	if (orig_cpu != cpu)
		schedstat_inc(p, se.nr_wakeups_migrate);
	if (cpu == this_cpu)
		schedstat_inc(p, se.nr_wakeups_local);
	else
		schedstat_inc(p, se.nr_wakeups_remote);
	activate_task(rq, p, 1);
	success = 1;

	/*
	 * Only attribute actual wakeups done by this task.
	 */
	if (!in_interrupt()) {
		struct sched_entity *se = &current->se;
		u64 sample = se->sum_exec_runtime;

		if (se->last_wakeup)
			sample -= se->last_wakeup;
		else
			sample -= se->start_runtime;
		update_avg(&se->avg_wakeup, sample);

		se->last_wakeup = se->sum_exec_runtime;
	}

out_running:
	trace_sched_wakeup(rq, p, success);
	check_preempt_curr(rq, p, sync);

	p->state = TASK_RUNNING;
#ifdef CONFIG_SMP
	if (p->sched_class->task_wake_up)
		p->sched_class->task_wake_up(rq, p);
#endif
out:
	task_rq_unlock(rq, &flags);

	return success;
}

int wake_up_process(struct task_struct *p)
{
	return try_to_wake_up(p, TASK_ALL, 0);
}
EXPORT_SYMBOL(wake_up_process);

int wake_up_state(struct task_struct *p, unsigned int state)
{
	return try_to_wake_up(p, state, 0);
}

/*
 * Perform scheduler related setup for a newly forked process p.
 * p is forked by current.
 *
 * __sched_fork() is basic setup used by init_idle() too:
 */
static void __sched_fork(struct task_struct *p)
{
	p->se.exec_start		= 0;
	p->se.sum_exec_runtime		= 0;
	p->se.prev_sum_exec_runtime	= 0;
	p->se.last_wakeup		= 0;
	p->se.avg_overlap		= 0;
	p->se.start_runtime		= 0;
	p->se.avg_wakeup		= sysctl_sched_wakeup_granularity;

#ifdef CONFIG_SCHEDSTATS
	p->se.wait_start		= 0;
	p->se.sum_sleep_runtime		= 0;
	p->se.sleep_start		= 0;
	p->se.block_start		= 0;
	p->se.sleep_max			= 0;
	p->se.block_max			= 0;
	p->se.exec_max			= 0;
	p->se.slice_max			= 0;
	p->se.wait_max			= 0;
#endif

	INIT_LIST_HEAD(&p->rt.run_list);
	p->se.on_rq = 0;
	INIT_LIST_HEAD(&p->se.group_node);

#ifdef CONFIG_PREEMPT_NOTIFIERS
	INIT_HLIST_HEAD(&p->preempt_notifiers);
#endif

	/*
	 * We mark the process as running here, but have not actually
	 * inserted it onto the runqueue yet. This guarantees that
	 * nobody will actually run it, and a signal or other external
	 * event cannot wake it up and insert it on the runqueue either.
	 */
	p->state = TASK_RUNNING;
}

/*
 * fork()/clone()-time setup:
 */
void sched_fork(struct task_struct *p, int clone_flags)
{
	int cpu = get_cpu();

	__sched_fork(p);

#ifdef CONFIG_SMP
	cpu = sched_balance_self(cpu, SD_BALANCE_FORK);
#endif
	set_task_cpu(p, cpu);

	/*
	 * Make sure we do not leak PI boosting priority to the child:
	 */
	p->prio = current->normal_prio;
	if (!rt_prio(p->prio))
		p->sched_class = &fair_sched_class;

#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
	if (likely(sched_info_on()))
		memset(&p->sched_info, 0, sizeof(p->sched_info));
#endif
#if defined(CONFIG_SMP) && defined(__ARCH_WANT_UNLOCKED_CTXSW)
	p->oncpu = 0;
#endif
#ifdef CONFIG_PREEMPT
	/* Want to start with kernel preemption disabled. */
	task_thread_info(p)->preempt_count = 1;
#endif
	plist_node_init(&p->pushable_tasks, MAX_PRIO);

	put_cpu();
}

/*
 * wake_up_new_task - wake up a newly created task for the first time.
 *
 * This function will do some initial scheduler statistics housekeeping
 * that must be done for every newly created context, then puts the task
 * on the runqueue and wakes it.
 */
void wake_up_new_task(struct task_struct *p, unsigned long clone_flags)
{
	unsigned long flags;
	struct rq *rq;

	rq = task_rq_lock(p, &flags);
	BUG_ON(p->state != TASK_RUNNING);
	update_rq_clock(rq);

	p->prio = effective_prio(p);

	if (!p->sched_class->task_new || !current->se.on_rq) {
		activate_task(rq, p, 0);
	} else {
		/*
		 * Let the scheduling class do new task startup
		 * management (if any):
		 */
		p->sched_class->task_new(rq, p);
		inc_nr_running(rq);
	}
	trace_sched_wakeup_new(rq, p, 1);
	check_preempt_curr(rq, p, 0);
#ifdef CONFIG_SMP
	if (p->sched_class->task_wake_up)
		p->sched_class->task_wake_up(rq, p);
#endif
	task_rq_unlock(rq, &flags);
}

#ifdef CONFIG_PREEMPT_NOTIFIERS

/**
 * preempt_notifier_register - tell me when current is being preempted & rescheduled
 * @notifier: notifier struct to register
 */
void preempt_notifier_register(struct preempt_notifier *notifier)
{
	hlist_add_head(&notifier->link, &current->preempt_notifiers);
}
EXPORT_SYMBOL_GPL(preempt_notifier_register);

/**
 * preempt_notifier_unregister - no longer interested in preemption notifications
 * @notifier: notifier struct to unregister
 *
 * This is safe to call from within a preemption notifier.
 */
void preempt_notifier_unregister(struct preempt_notifier *notifier)
{
	hlist_del(&notifier->link);
}
EXPORT_SYMBOL_GPL(preempt_notifier_unregister);

static void fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
	struct preempt_notifier *notifier;
	struct hlist_node *node;

	hlist_for_each_entry(notifier, node, &curr->preempt_notifiers, link)
		notifier->ops->sched_in(notifier, raw_smp_processor_id());
}

static void
fire_sched_out_preempt_notifiers(struct task_struct *curr,
				 struct task_struct *next)
{
	struct preempt_notifier *notifier;
	struct hlist_node *node;

	hlist_for_each_entry(notifier, node, &curr->preempt_notifiers, link)
		notifier->ops->sched_out(notifier, next);
}

#else /* !CONFIG_PREEMPT_NOTIFIERS */

static void fire_sched_in_preempt_notifiers(struct task_struct *curr)
{
}

static void
fire_sched_out_preempt_notifiers(struct task_struct *curr,
				 struct task_struct *next)
{
}

#endif /* CONFIG_PREEMPT_NOTIFIERS */

/**
 * prepare_task_switch - prepare to switch tasks
 * @rq: the runqueue preparing to switch
 * @prev: the current task that is being switched out
 * @next: the task we are going to switch to.
 *
 * This is called with the rq lock held and interrupts off. It must
 * be paired with a subsequent finish_task_switch after the context
 * switch.
 *
 * prepare_task_switch sets up locking and calls architecture specific
 * hooks.
 */
static inline void
prepare_task_switch(struct rq *rq, struct task_struct *prev,
		    struct task_struct *next)
{
	fire_sched_out_preempt_notifiers(prev, next);
	prepare_lock_switch(rq, next);
	prepare_arch_switch(next);
}

/**
 * finish_task_switch - clean up after a task-switch
 * @rq: runqueue associated with task-switch
 * @prev: the thread we just switched away from.
 *
 * finish_task_switch must be called after the context switch, paired
 * with a prepare_task_switch call before the context switch.
 * finish_task_switch will reconcile locking set up by prepare_task_switch,
 * and do any other architecture-specific cleanup actions.
 *
 * Note that we may have delayed dropping an mm in context_switch(). If
 * so, we finish that here outside of the runqueue lock. (Doing it
 * with the lock held can cause deadlocks; see schedule() for
 * details.)
 */
static void finish_task_switch(struct rq *rq, struct task_struct *prev)
	__releases(rq->lock)
{
	struct mm_struct *mm = rq->prev_mm;
	long prev_state;
#ifdef CONFIG_SMP
	int post_schedule = 0;

	if (current->sched_class->needs_post_schedule)
		post_schedule = current->sched_class->needs_post_schedule(rq);
#endif

	rq->prev_mm = NULL;

	/*
	 * A task struct has one reference for the use as "current".
	 * If a task dies, then it sets TASK_DEAD in tsk->state and calls
	 * schedule one last time. The schedule call will never return, and
	 * the scheduled task must drop that reference.
	 * The test for TASK_DEAD must occur while the runqueue locks are
	 * still held, otherwise prev could be scheduled on another cpu, die
	 * there before we look at prev->state, and then the reference would
	 * be dropped twice.
	 *		Manfred Spraul <manfred@colorfullife.com>
	 */
	prev_state = prev->state;
	finish_arch_switch(prev);
	finish_lock_switch(rq, prev);
#ifdef CONFIG_SMP
	if (post_schedule)
		current->sched_class->post_schedule(rq);
#endif

	fire_sched_in_preempt_notifiers(current);
	if (mm)
		mmdrop(mm);
	if (unlikely(prev_state == TASK_DEAD)) {
		/*
		 * Remove function-return probe instances associated with this
		 * task and put them back on the free list.
		 */
		kprobe_flush_task(prev);
		put_task_struct(prev);
	}
}

/**
 * schedule_tail - first thing a freshly forked thread must call.
 * @prev: the thread we just switched away from.
 */
asmlinkage void schedule_tail(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();

	finish_task_switch(rq, prev);
#ifdef __ARCH_WANT_UNLOCKED_CTXSW
	/* In this case, finish_task_switch does not reenable preemption */
	preempt_enable();
#endif
	if (current->set_child_tid)
		put_user(task_pid_vnr(current), current->set_child_tid);
}

/*
 * context_switch - switch to the new MM and the new
 * thread's register state.
 */
static inline void
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next)
{
	struct mm_struct *mm, *oldmm;

	prepare_task_switch(rq, prev, next);
	trace_sched_switch(rq, prev, next);
	mm = next->mm;
	oldmm = prev->active_mm;
	/*
	 * For paravirt, this is coupled with an exit in switch_to to
	 * combine the page table reload and the switch backend into
	 * one hypercall.
	 */
	arch_enter_lazy_cpu_mode();

	if (unlikely(!mm)) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next);
	} else
		switch_mm(oldmm, mm, next);

	if (unlikely(!prev->mm)) {
		prev->active_mm = NULL;
		rq->prev_mm = oldmm;
	}
	/*
	 * Since the runqueue lock will be released by the next
	 * task (which is an invalid locking op but in the case
	 * of the scheduler it's an obvious special-case), so we
	 * do an early lockdep release here:
	 */
#ifndef __ARCH_WANT_UNLOCKED_CTXSW
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);
#endif

	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);

	barrier();
	/*
	 * this_rq must be evaluated again because prev may have moved
	 * CPUs since it called schedule(), thus the 'rq' on its stack
	 * frame will be invalid.
	 */
	finish_task_switch(this_rq(), prev);
}

/*
 * nr_running, nr_uninterruptible and nr_context_switches:
 *
 * externally visible scheduler statistics: current number of runnable
 * threads, current number of uninterruptible-sleeping threads, total
 * number of context switches performed since bootup.
 */
unsigned long nr_running(void)
{
	unsigned long i, sum = 0;

	for_each_online_cpu(i)
		sum += cpu_rq(i)->nr_running;

	return sum;
}

unsigned long nr_uninterruptible(void)
{
	unsigned long i, sum = 0;

	for_each_possible_cpu(i)
		sum += cpu_rq(i)->nr_uninterruptible;

	/*
	 * Since we read the counters lockless, it might be slightly
	 * inaccurate. Do not allow it to go below zero though:
	 */
	if (unlikely((long)sum < 0))
		sum = 0;

	return sum;
}

unsigned long long nr_context_switches(void)
{
	int i;
	unsigned long long sum = 0;

	for_each_possible_cpu(i)
		sum += cpu_rq(i)->nr_switches;

	return sum;
}

unsigned long nr_iowait(void)
{
	unsigned long i, sum = 0;

	for_each_possible_cpu(i)
		sum += atomic_read(&cpu_rq(i)->nr_iowait);

	return sum;
}

unsigned long nr_active(void)
{
	unsigned long i, running = 0, uninterruptible = 0;

	for_each_online_cpu(i) {
		running += cpu_rq(i)->nr_running;
		uninterruptible += cpu_rq(i)->nr_uninterruptible;
	}

	if (unlikely((long)uninterruptible < 0))
		uninterruptible = 0;

	return running + uninterruptible;
}

/*
 * Update rq->cpu_load[] statistics. This function is usually called every
 * scheduler tick (TICK_NSEC).
 */
static void update_cpu_load(struct rq *this_rq)
{
	unsigned long this_load = this_rq->load.weight;
	int i, scale;

	this_rq->nr_load_updates++;

	/* Update our load: */
	for (i = 0, scale = 1; i < CPU_LOAD_IDX_MAX; i++, scale += scale) {
		unsigned long old_load, new_load;

		/* scale is effectively 1 << i now, and >> i divides by scale */

		old_load = this_rq->cpu_load[i];
		new_load = this_load;
		/*
		 * Round up the averaging division if load is increasing. This
		 * prevents us from getting stuck on 9 if the load is 10, for
		 * example.
		 */
		if (new_load > old_load)
			new_load += scale-1;
		this_rq->cpu_load[i] = (old_load*(scale-1) + new_load) >> i;
	}
}

#ifdef CONFIG_SMP

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	if (rq1 == rq2) {
		spin_lock(&rq1->lock);
		__acquire(rq2->lock);	/* Fake it out ;) */
	} else {
		if (rq1 < rq2) {
			spin_lock(&rq1->lock);
			spin_lock_nested(&rq2->lock, SINGLE_DEPTH_NESTING);
		} else {
			spin_lock(&rq2->lock);
			spin_lock_nested(&rq1->lock, SINGLE_DEPTH_NESTING);
		}
	}
	update_rq_clock(rq1);
	update_rq_clock(rq2);
}

/*
 * double_rq_unlock - safely unlock two runqueues
 *
 * Note this does not restore interrupts like task_rq_unlock,
 * you need to do so manually after calling.
 */
static void double_rq_unlock(struct rq *rq1, struct rq *rq2)
	__releases(rq1->lock)
	__releases(rq2->lock)
{
	spin_unlock(&rq1->lock);
	if (rq1 != rq2)
		spin_unlock(&rq2->lock);
	else
		__release(rq2->lock);
}

/*
 * If dest_cpu is allowed for this process, migrate the task to it.
 * This is accomplished by forcing the cpu_allowed mask to only
 * allow dest_cpu, which will force the cpu onto dest_cpu. Then
 * the cpu_allowed mask is restored.
 */
static void sched_migrate_task(struct task_struct *p, int dest_cpu)
{
	struct migration_req req;
	unsigned long flags;
	struct rq *rq;

	rq = task_rq_lock(p, &flags);
	if (!cpumask_test_cpu(dest_cpu, &p->cpus_allowed)
	    || unlikely(!cpu_active(dest_cpu)))
		goto out;

	/* force the process onto the specified CPU */
	if (migrate_task(p, dest_cpu, &req)) {
		/* Need to wait for migration thread (might exit: take ref). */
		struct task_struct *mt = rq->migration_thread;

		get_task_struct(mt);
		task_rq_unlock(rq, &flags);
		wake_up_process(mt);
		put_task_struct(mt);
		wait_for_completion(&req.done);

		return;
	}
out:
	task_rq_unlock(rq, &flags);
}

/*
 * sched_exec - execve() is a valuable balancing opportunity, because at
 * this point the task has the smallest effective memory and cache footprint.
 */
void sched_exec(void)
{
	int new_cpu, this_cpu = get_cpu();
	new_cpu = sched_balance_self(this_cpu, SD_BALANCE_EXEC);
	put_cpu();
	if (new_cpu != this_cpu)
		sched_migrate_task(current, new_cpu);
}

/*
 * pull_task - move a task from a remote runqueue to the local runqueue.
 * Both runqueues must be locked.
 */
static void pull_task(struct rq *src_rq, struct task_struct *p,
		      struct rq *this_rq, int this_cpu)
{
	deactivate_task(src_rq, p, 0);
	set_task_cpu(p, this_cpu);
	activate_task(this_rq, p, 0);
	/*
	 * Note that idle threads have a prio of MAX_PRIO, for this test
	 * to be always true for them.
	 */
	check_preempt_curr(this_rq, p, 0);
}

/*
 * can_migrate_task - may task p from runqueue rq be migrated to this_cpu?
 */
static
int can_migrate_task(struct task_struct *p, struct rq *rq, int this_cpu,
		     struct sched_domain *sd, enum cpu_idle_type idle,
		     int *all_pinned)
{
	int tsk_cache_hot = 0;
	/*
	 * We do not migrate tasks that are:
	 * 1) running (obviously), or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) are cache-hot on their current CPU.
	 */
	if (!cpumask_test_cpu(this_cpu, &p->cpus_allowed)) {
		schedstat_inc(p, se.nr_failed_migrations_affine);
		return 0;
	}
	*all_pinned = 0;

	if (task_running(rq, p)) {
		schedstat_inc(p, se.nr_failed_migrations_running);
		return 0;
	}

	/*
	 * Aggressive migration if:
	 * 1) task is cache cold, or
	 * 2) too many balance attempts have failed.
	 */

	tsk_cache_hot = task_hot(p, rq->clock, sd);
	if (!tsk_cache_hot ||
		sd->nr_balance_failed > sd->cache_nice_tries) {
#ifdef CONFIG_SCHEDSTATS
		if (tsk_cache_hot) {
			schedstat_inc(sd, lb_hot_gained[idle]);
			schedstat_inc(p, se.nr_forced_migrations);
		}
#endif
		return 1;
	}

	if (tsk_cache_hot) {
		schedstat_inc(p, se.nr_failed_migrations_hot);
		return 0;
	}
	return 1;
}

static unsigned long
balance_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
	      unsigned long max_load_move, struct sched_domain *sd,
	      enum cpu_idle_type idle, int *all_pinned,
	      int *this_best_prio, struct rq_iterator *iterator)
{
	int loops = 0, pulled = 0, pinned = 0;
	struct task_struct *p;
	long rem_load_move = max_load_move;

	if (max_load_move == 0)
		goto out;

	pinned = 1;

	/*
	 * Start the load-balancing iterator:
	 */
	p = iterator->start(iterator->arg);
next:
	if (!p || loops++ > sysctl_sched_nr_migrate)
		goto out;

	if ((p->se.load.weight >> 1) > rem_load_move ||
	    !can_migrate_task(p, busiest, this_cpu, sd, idle, &pinned)) {
		p = iterator->next(iterator->arg);
		goto next;
	}

	pull_task(busiest, p, this_rq, this_cpu);
	pulled++;
	rem_load_move -= p->se.load.weight;

#ifdef CONFIG_PREEMPT
	/*
	 * NEWIDLE balancing is a source of latency, so preemptible kernels
	 * will stop after the first task is pulled to minimize the critical
	 * section.
	 */
	if (idle == CPU_NEWLY_IDLE)
		goto out;
#endif

	/*
	 * We only want to steal up to the prescribed amount of weighted load.
	 */
	if (rem_load_move > 0) {
		if (p->prio < *this_best_prio)
			*this_best_prio = p->prio;
		p = iterator->next(iterator->arg);
		goto next;
	}
out:
	/*
	 * Right now, this is one of only two places pull_task() is called,
	 * so we can safely collect pull_task() stats here rather than
	 * inside pull_task().
	 */
	schedstat_add(sd, lb_gained[idle], pulled);

	if (all_pinned)
		*all_pinned = pinned;

	return max_load_move - rem_load_move;
}

/*
 * move_tasks tries to move up to max_load_move weighted load from busiest to
 * this_rq, as part of a balancing operation within domain "sd".
 * Returns 1 if successful and 0 otherwise.
 *
 * Called with both runqueues locked.
 */
static int move_tasks(struct rq *this_rq, int this_cpu, struct rq *busiest,
		      unsigned long max_load_move,
		      struct sched_domain *sd, enum cpu_idle_type idle,
		      int *all_pinned)
{
	const struct sched_class *class = sched_class_highest;
	unsigned long total_load_moved = 0;
	int this_best_prio = this_rq->curr->prio;

	do {
		total_load_moved +=
			class->load_balance(this_rq, this_cpu, busiest,
				max_load_move - total_load_moved,
				sd, idle, all_pinned, &this_best_prio);
		class = class->next;

#ifdef CONFIG_PREEMPT
		/*
		 * NEWIDLE balancing is a source of latency, so preemptible
		 * kernels will stop after the first task is pulled to minimize
		 * the critical section.
		 */
		if (idle == CPU_NEWLY_IDLE && this_rq->nr_running)
			break;
#endif
	} while (class && max_load_move > total_load_moved);

	return total_load_moved > 0;
}

static int
iter_move_one_task(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle,
		   struct rq_iterator *iterator)
{
	struct task_struct *p = iterator->start(iterator->arg);
	int pinned = 0;

	while (p) {
		if (can_migrate_task(p, busiest, this_cpu, sd, idle, &pinned)) {
			pull_task(busiest, p, this_rq, this_cpu);
			/*
			 * Right now, this is only the second place pull_task()
			 * is called, so we can safely collect pull_task()
			 * stats here rather than inside pull_task().
			 */
			schedstat_inc(sd, lb_gained[idle]);

			return 1;
		}
		p = iterator->next(iterator->arg);
	}

	return 0;
}

/*
 * move_one_task tries to move exactly one task from busiest to this_rq, as
 * part of active balancing operations within "domain".
 * Returns 1 if successful and 0 otherwise.
 *
 * Called with both runqueues locked.
 */
static int move_one_task(struct rq *this_rq, int this_cpu, struct rq *busiest,
			 struct sched_domain *sd, enum cpu_idle_type idle)
{
	const struct sched_class *class;

	for (class = sched_class_highest; class; class = class->next)
		if (class->move_one_task(this_rq, this_cpu, busiest, sd, idle))
			return 1;

	return 0;
}
/********** Helpers for find_busiest_group ************************/
/*
 * sd_lb_stats - Structure to store the statistics of a sched_domain
 * 		during load balancing.
 */
struct sd_lb_stats {
	struct sched_group *busiest; /* Busiest group in this sd */
	struct sched_group *this;  /* Local group in this sd */
	unsigned long total_load;  /* Total load of all groups in sd */
	unsigned long total_pwr;   /*	Total power of all groups in sd */
	unsigned long avg_load;	   /* Average load across all groups in sd */

	/** Statistics of this group */
	unsigned long this_load;
	unsigned long this_load_per_task;
	unsigned long this_nr_running;

	/* Statistics of the busiest group */
	unsigned long max_load;
	unsigned long busiest_load_per_task;
	unsigned long busiest_nr_running;

	int group_imb; /* Is there imbalance in this sd */
#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	int power_savings_balance; /* Is powersave balance needed for this sd */
	struct sched_group *group_min; /* Least loaded group in sd */
	struct sched_group *group_leader; /* Group which relieves group_min */
	unsigned long min_load_per_task; /* load_per_task in group_min */
	unsigned long leader_nr_running; /* Nr running of group_leader */
	unsigned long min_nr_running; /* Nr running of group_min */
#endif
};

/*
 * sg_lb_stats - stats of a sched_group required for load_balancing
 */
struct sg_lb_stats {
	unsigned long avg_load; /*Avg load across the CPUs of the group */
	unsigned long group_load; /* Total load over the CPUs of the group */
	unsigned long sum_nr_running; /* Nr tasks running in the group */
	unsigned long sum_weighted_load; /* Weighted load of group's tasks */
	unsigned long group_capacity;
	int group_imb; /* Is there an imbalance in the group ? */
};

/**
 * group_first_cpu - Returns the first cpu in the cpumask of a sched_group.
 * @group: The group whose first cpu is to be returned.
 */
static inline unsigned int group_first_cpu(struct sched_group *group)
{
	return cpumask_first(sched_group_cpus(group));
}

/**
 * get_sd_load_idx - Obtain the load index for a given sched domain.
 * @sd: The sched_domain whose load_idx is to be obtained.
 * @idle: The Idle status of the CPU for whose sd load_icx is obtained.
 */
static inline int get_sd_load_idx(struct sched_domain *sd,
					enum cpu_idle_type idle)
{
	int load_idx;

	switch (idle) {
	case CPU_NOT_IDLE:
		load_idx = sd->busy_idx;
		break;

	case CPU_NEWLY_IDLE:
		load_idx = sd->newidle_idx;
		break;
	default:
		load_idx = sd->idle_idx;
		break;
	}

	return load_idx;
}


#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
/**
 * init_sd_power_savings_stats - Initialize power savings statistics for
 * the given sched_domain, during load balancing.
 *
 * @sd: Sched domain whose power-savings statistics are to be initialized.
 * @sds: Variable containing the statistics for sd.
 * @idle: Idle status of the CPU at which we're performing load-balancing.
 */
static inline void init_sd_power_savings_stats(struct sched_domain *sd,
	struct sd_lb_stats *sds, enum cpu_idle_type idle)
{
	/*
	 * Busy processors will not participate in power savings
	 * balance.
	 */
	if (idle == CPU_NOT_IDLE || !(sd->flags & SD_POWERSAVINGS_BALANCE))
		sds->power_savings_balance = 0;
	else {
		sds->power_savings_balance = 1;
		sds->min_nr_running = ULONG_MAX;
		sds->leader_nr_running = 0;
	}
}

/**
 * update_sd_power_savings_stats - Update the power saving stats for a
 * sched_domain while performing load balancing.
 *
 * @group: sched_group belonging to the sched_domain under consideration.
 * @sds: Variable containing the statistics of the sched_domain
 * @local_group: Does group contain the CPU for which we're performing
 * 		load balancing ?
 * @sgs: Variable containing the statistics of the group.
 */
static inline void update_sd_power_savings_stats(struct sched_group *group,
	struct sd_lb_stats *sds, int local_group, struct sg_lb_stats *sgs)
{

	if (!sds->power_savings_balance)
		return;

	/*
	 * If the local group is idle or completely loaded
	 * no need to do power savings balance at this domain
	 */
	if (local_group && (sds->this_nr_running >= sgs->group_capacity ||
				!sds->this_nr_running))
		sds->power_savings_balance = 0;

	/*
	 * If a group is already running at full capacity or idle,
	 * don't include that group in power savings calculations
	 */
	if (!sds->power_savings_balance ||
		sgs->sum_nr_running >= sgs->group_capacity ||
		!sgs->sum_nr_running)
		return;

	/*
	 * Calculate the group which has the least non-idle load.
	 * This is the group from where we need to pick up the load
	 * for saving power
	 */
	if ((sgs->sum_nr_running < sds->min_nr_running) ||
	    (sgs->sum_nr_running == sds->min_nr_running &&
	     group_first_cpu(group) > group_first_cpu(sds->group_min))) {
		sds->group_min = group;
		sds->min_nr_running = sgs->sum_nr_running;
		sds->min_load_per_task = sgs->sum_weighted_load /
						sgs->sum_nr_running;
	}

	/*
	 * Calculate the group which is almost near its
	 * capacity but still has some space to pick up some load
	 * from other group and save more power
	 */
	if (sgs->sum_nr_running > sgs->group_capacity - 1)
		return;

	if (sgs->sum_nr_running > sds->leader_nr_running ||
	    (sgs->sum_nr_running == sds->leader_nr_running &&
	     group_first_cpu(group) < group_first_cpu(sds->group_leader))) {
		sds->group_leader = group;
		sds->leader_nr_running = sgs->sum_nr_running;
	}
}

/**
 * check_power_save_busiest_group - see if there is potential for some power-savings balance
 * @sds: Variable containing the statistics of the sched_domain
 *	under consideration.
 * @this_cpu: Cpu at which we're currently performing load-balancing.
 * @imbalance: Variable to store the imbalance.
 *
 * Description:
 * Check if we have potential to perform some power-savings balance.
 * If yes, set the busiest group to be the least loaded group in the
 * sched_domain, so that it's CPUs can be put to idle.
 *
 * Returns 1 if there is potential to perform power-savings balance.
 * Else returns 0.
 */
static inline int check_power_save_busiest_group(struct sd_lb_stats *sds,
					int this_cpu, unsigned long *imbalance)
{
	if (!sds->power_savings_balance)
		return 0;

	if (sds->this != sds->group_leader ||
			sds->group_leader == sds->group_min)
		return 0;

	*imbalance = sds->min_load_per_task;
	sds->busiest = sds->group_min;

	if (sched_mc_power_savings >= POWERSAVINGS_BALANCE_WAKEUP) {
		cpu_rq(this_cpu)->rd->sched_mc_preferred_wakeup_cpu =
			group_first_cpu(sds->group_leader);
	}

	return 1;

}
#else /* CONFIG_SCHED_MC || CONFIG_SCHED_SMT */
static inline void init_sd_power_savings_stats(struct sched_domain *sd,
	struct sd_lb_stats *sds, enum cpu_idle_type idle)
{
	return;
}

static inline void update_sd_power_savings_stats(struct sched_group *group,
	struct sd_lb_stats *sds, int local_group, struct sg_lb_stats *sgs)
{
	return;
}

static inline int check_power_save_busiest_group(struct sd_lb_stats *sds,
					int this_cpu, unsigned long *imbalance)
{
	return 0;
}
#endif /* CONFIG_SCHED_MC || CONFIG_SCHED_SMT */


/**
 * update_sg_lb_stats - Update sched_group's statistics for load balancing.
 * @group: sched_group whose statistics are to be updated.
 * @this_cpu: Cpu for which load balance is currently performed.
 * @idle: Idle status of this_cpu
 * @load_idx: Load index of sched_domain of this_cpu for load calc.
 * @sd_idle: Idle status of the sched_domain containing group.
 * @local_group: Does group contain this_cpu.
 * @cpus: Set of cpus considered for load balancing.
 * @balance: Should we balance.
 * @sgs: variable to hold the statistics for this group.
 */
static inline void update_sg_lb_stats(struct sched_group *group, int this_cpu,
			enum cpu_idle_type idle, int load_idx, int *sd_idle,
			int local_group, const struct cpumask *cpus,
			int *balance, struct sg_lb_stats *sgs)
{
	unsigned long load, max_cpu_load, min_cpu_load;
	int i;
	unsigned int balance_cpu = -1, first_idle_cpu = 0;
	unsigned long sum_avg_load_per_task;
	unsigned long avg_load_per_task;

	if (local_group)
		balance_cpu = group_first_cpu(group);

	/* Tally up the load of all CPUs in the group */
	sum_avg_load_per_task = avg_load_per_task = 0;
	max_cpu_load = 0;
	min_cpu_load = ~0UL;

	for_each_cpu_and(i, sched_group_cpus(group), cpus) {
		struct rq *rq = cpu_rq(i);

		if (*sd_idle && rq->nr_running)
			*sd_idle = 0;

		/* Bias balancing toward cpus of our domain */
		if (local_group) {
			if (idle_cpu(i) && !first_idle_cpu) {
				first_idle_cpu = 1;
				balance_cpu = i;
			}

			load = target_load(i, load_idx);
		} else {
			load = source_load(i, load_idx);
			if (load > max_cpu_load)
				max_cpu_load = load;
			if (min_cpu_load > load)
				min_cpu_load = load;
		}

		sgs->group_load += load;
		sgs->sum_nr_running += rq->nr_running;
		sgs->sum_weighted_load += weighted_cpuload(i);

		sum_avg_load_per_task += cpu_avg_load_per_task(i);
	}

	/*
	 * First idle cpu or the first cpu(busiest) in this sched group
	 * is eligible for doing load balancing at this and above
	 * domains. In the newly idle case, we will allow all the cpu's
	 * to do the newly idle load balance.
	 */
	if (idle != CPU_NEWLY_IDLE && local_group &&
	    balance_cpu != this_cpu && balance) {
		*balance = 0;
		return;
	}

	/* Adjust by relative CPU power of the group */
	sgs->avg_load = sg_div_cpu_power(group,
			sgs->group_load * SCHED_LOAD_SCALE);


	/*
	 * Consider the group unbalanced when the imbalance is larger
	 * than the average weight of two tasks.
	 *
	 * APZ: with cgroup the avg task weight can vary wildly and
	 *      might not be a suitable number - should we keep a
	 *      normalized nr_running number somewhere that negates
	 *      the hierarchy?
	 */
	avg_load_per_task = sg_div_cpu_power(group,
			sum_avg_load_per_task * SCHED_LOAD_SCALE);

	if ((max_cpu_load - min_cpu_load) > 2*avg_load_per_task)
		sgs->group_imb = 1;

	sgs->group_capacity = group->__cpu_power / SCHED_LOAD_SCALE;

}

/**
 * update_sd_lb_stats - Update sched_group's statistics for load balancing.
 * @sd: sched_domain whose statistics are to be updated.
 * @this_cpu: Cpu for which load balance is currently performed.
 * @idle: Idle status of this_cpu
 * @sd_idle: Idle status of the sched_domain containing group.
 * @cpus: Set of cpus considered for load balancing.
 * @balance: Should we balance.
 * @sds: variable to hold the statistics for this sched_domain.
 */
static inline void update_sd_lb_stats(struct sched_domain *sd, int this_cpu,
			enum cpu_idle_type idle, int *sd_idle,
			const struct cpumask *cpus, int *balance,
			struct sd_lb_stats *sds)
{
	struct sched_group *group = sd->groups;
	struct sg_lb_stats sgs;
	int load_idx;

	init_sd_power_savings_stats(sd, sds, idle);
	load_idx = get_sd_load_idx(sd, idle);

	do {
		int local_group;

		local_group = cpumask_test_cpu(this_cpu,
					       sched_group_cpus(group));
		memset(&sgs, 0, sizeof(sgs));
		update_sg_lb_stats(group, this_cpu, idle, load_idx, sd_idle,
				local_group, cpus, balance, &sgs);

		if (local_group && balance && !(*balance))
			return;

		sds->total_load += sgs.group_load;
		sds->total_pwr += group->__cpu_power;

		if (local_group) {
			sds->this_load = sgs.avg_load;
			sds->this = group;
			sds->this_nr_running = sgs.sum_nr_running;
			sds->this_load_per_task = sgs.sum_weighted_load;
		} else if (sgs.avg_load > sds->max_load &&
			   (sgs.sum_nr_running > sgs.group_capacity ||
				sgs.group_imb)) {
			sds->max_load = sgs.avg_load;
			sds->busiest = group;
			sds->busiest_nr_running = sgs.sum_nr_running;
			sds->busiest_load_per_task = sgs.sum_weighted_load;
			sds->group_imb = sgs.group_imb;
		}

		update_sd_power_savings_stats(group, sds, local_group, &sgs);
		group = group->next;
	} while (group != sd->groups);

}

/**
 * fix_small_imbalance - Calculate the minor imbalance that exists
 *			amongst the groups of a sched_domain, during
 *			load balancing.
 * @sds: Statistics of the sched_domain whose imbalance is to be calculated.
 * @this_cpu: The cpu at whose sched_domain we're performing load-balance.
 * @imbalance: Variable to store the imbalance.
 */
static inline void fix_small_imbalance(struct sd_lb_stats *sds,
				int this_cpu, unsigned long *imbalance)
{
	unsigned long tmp, pwr_now = 0, pwr_move = 0;
	unsigned int imbn = 2;

	if (sds->this_nr_running) {
		sds->this_load_per_task /= sds->this_nr_running;
		if (sds->busiest_load_per_task >
				sds->this_load_per_task)
			imbn = 1;
	} else
		sds->this_load_per_task =
			cpu_avg_load_per_task(this_cpu);

	if (sds->max_load - sds->this_load + sds->busiest_load_per_task >=
			sds->busiest_load_per_task * imbn) {
		*imbalance = sds->busiest_load_per_task;
		return;
	}

	/*
	 * OK, we don't have enough imbalance to justify moving tasks,
	 * however we may be able to increase total CPU power used by
	 * moving them.
	 */

	pwr_now += sds->busiest->__cpu_power *
			min(sds->busiest_load_per_task, sds->max_load);
	pwr_now += sds->this->__cpu_power *
			min(sds->this_load_per_task, sds->this_load);
	pwr_now /= SCHED_LOAD_SCALE;

	/* Amount of load we'd subtract */
	tmp = sg_div_cpu_power(sds->busiest,
			sds->busiest_load_per_task * SCHED_LOAD_SCALE);
	if (sds->max_load > tmp)
		pwr_move += sds->busiest->__cpu_power *
			min(sds->busiest_load_per_task, sds->max_load - tmp);

	/* Amount of load we'd add */
	if (sds->max_load * sds->busiest->__cpu_power <
		sds->busiest_load_per_task * SCHED_LOAD_SCALE)
		tmp = sg_div_cpu_power(sds->this,
			sds->max_load * sds->busiest->__cpu_power);
	else
		tmp = sg_div_cpu_power(sds->this,
			sds->busiest_load_per_task * SCHED_LOAD_SCALE);
	pwr_move += sds->this->__cpu_power *
			min(sds->this_load_per_task, sds->this_load + tmp);
	pwr_move /= SCHED_LOAD_SCALE;

	/* Move if we gain throughput */
	if (pwr_move > pwr_now)
		*imbalance = sds->busiest_load_per_task;
}

/**
 * calculate_imbalance - Calculate the amount of imbalance present within the
 *			 groups of a given sched_domain during load balance.
 * @sds: statistics of the sched_domain whose imbalance is to be calculated.
 * @this_cpu: Cpu for which currently load balance is being performed.
 * @imbalance: The variable to store the imbalance.
 */
static inline void calculate_imbalance(struct sd_lb_stats *sds, int this_cpu,
		unsigned long *imbalance)
{
	unsigned long max_pull;
	/*
	 * In the presence of smp nice balancing, certain scenarios can have
	 * max load less than avg load(as we skip the groups at or below
	 * its cpu_power, while calculating max_load..)
	 */
	if (sds->max_load < sds->avg_load) {
		*imbalance = 0;
		return fix_small_imbalance(sds, this_cpu, imbalance);
	}

	/* Don't want to pull so many tasks that a group would go idle */
	max_pull = min(sds->max_load - sds->avg_load,
			sds->max_load - sds->busiest_load_per_task);

	/* How much load to actually move to equalise the imbalance */
	*imbalance = min(max_pull * sds->busiest->__cpu_power,
		(sds->avg_load - sds->this_load) * sds->this->__cpu_power)
			/ SCHED_LOAD_SCALE;

	/*
	 * if *imbalance is less than the average load per runnable task
	 * there is no gaurantee that any tasks will be moved so we'll have
	 * a think about bumping its value to force at least one task to be
	 * moved
	 */
	if (*imbalance < sds->busiest_load_per_task)
		return fix_small_imbalance(sds, this_cpu, imbalance);

}
/******* find_busiest_group() helpers end here *********************/

/**
 * find_busiest_group - Returns the busiest group within the sched_domain
 * if there is an imbalance. If there isn't an imbalance, and
 * the user has opted for power-savings, it returns a group whose
 * CPUs can be put to idle by rebalancing those tasks elsewhere, if
 * such a group exists.
 *
 * Also calculates the amount of weighted load which should be moved
 * to restore balance.
 *
 * @sd: The sched_domain whose busiest group is to be returned.
 * @this_cpu: The cpu for which load balancing is currently being performed.
 * @imbalance: Variable which stores amount of weighted load which should
 *		be moved to restore balance/put a group to idle.
 * @idle: The idle status of this_cpu.
 * @sd_idle: The idleness of sd
 * @cpus: The set of CPUs under consideration for load-balancing.
 * @balance: Pointer to a variable indicating if this_cpu
 *	is the appropriate cpu to perform load balancing at this_level.
 *
 * Returns:	- the busiest group if imbalance exists.
 *		- If no imbalance and user has opted for power-savings balance,
 *		   return the least loaded group whose CPUs can be
 *		   put to idle by rebalancing its tasks onto our group.
 */
static struct sched_group *
find_busiest_group(struct sched_domain *sd, int this_cpu,
		   unsigned long *imbalance, enum cpu_idle_type idle,
		   int *sd_idle, const struct cpumask *cpus, int *balance)
{
	struct sd_lb_stats sds;

	memset(&sds, 0, sizeof(sds));

	/*
	 * Compute the various statistics relavent for load balancing at
	 * this level.
	 */
	update_sd_lb_stats(sd, this_cpu, idle, sd_idle, cpus,
					balance, &sds);

	/* Cases where imbalance does not exist from POV of this_cpu */
	/* 1) this_cpu is not the appropriate cpu to perform load balancing
	 *    at this level.
	 * 2) There is no busy sibling group to pull from.
	 * 3) This group is the busiest group.
	 * 4) This group is more busy than the avg busieness at this
	 *    sched_domain.
	 * 5) The imbalance is within the specified limit.
	 * 6) Any rebalance would lead to ping-pong
	 */
	if (balance && !(*balance))
		goto ret;

	if (!sds.busiest || sds.busiest_nr_running == 0)
		goto out_balanced;

	if (sds.this_load >= sds.max_load)
		goto out_balanced;

	sds.avg_load = (SCHED_LOAD_SCALE * sds.total_load) / sds.total_pwr;

	if (sds.this_load >= sds.avg_load)
		goto out_balanced;

	if (100 * sds.max_load <= sd->imbalance_pct * sds.this_load)
		goto out_balanced;

	sds.busiest_load_per_task /= sds.busiest_nr_running;
	if (sds.group_imb)
		sds.busiest_load_per_task =
			min(sds.busiest_load_per_task, sds.avg_load);

	/*
	 * We're trying to get all the cpus to the average_load, so we don't
	 * want to push ourselves above the average load, nor do we wish to
	 * reduce the max loaded cpu below the average load, as either of these
	 * actions would just result in more rebalancing later, and ping-pong
	 * tasks around. Thus we look for the minimum possible imbalance.
	 * Negative imbalances (*we* are more loaded than anyone else) will
	 * be counted as no imbalance for these purposes -- we can't fix that
	 * by pulling tasks to us. Be careful of negative numbers as they'll
	 * appear as very large values with unsigned longs.
	 */
	if (sds.max_load <= sds.busiest_load_per_task)
		goto out_balanced;

	/* Looks like there is an imbalance. Compute it */
	calculate_imbalance(&sds, this_cpu, imbalance);
	return sds.busiest;

out_balanced:
	/*
	 * There is no obvious imbalance. But check if we can do some balancing
	 * to save power.
	 */
	if (check_power_save_busiest_group(&sds, this_cpu, imbalance))
		return sds.busiest;
ret:
	*imbalance = 0;
	return NULL;
}

/*
 * find_busiest_queue - find the busiest runqueue among the cpus in group.
 */
static struct rq *
find_busiest_queue(struct sched_group *group, enum cpu_idle_type idle,
		   unsigned long imbalance, const struct cpumask *cpus)
{
	struct rq *busiest = NULL, *rq;
	unsigned long max_load = 0;
	int i;

	for_each_cpu(i, sched_group_cpus(group)) {
		unsigned long wl;

		if (!cpumask_test_cpu(i, cpus))
			continue;

		rq = cpu_rq(i);
		wl = weighted_cpuload(i);

		if (rq->nr_running == 1 && wl > imbalance)
			continue;

		if (wl > max_load) {
			max_load = wl;
			busiest = rq;
		}
	}

	return busiest;
}

/*
 * Max backoff if we encounter pinned tasks. Pretty arbitrary value, but
 * so long as it is large enough.
 */
#define MAX_PINNED_INTERVAL	512

/* Working cpumask for load_balance and load_balance_newidle. */
static DEFINE_PER_CPU(cpumask_var_t, load_balance_tmpmask);

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 */
static int load_balance(int this_cpu, struct rq *this_rq,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *balance)
{
	int ld_moved, all_pinned = 0, active_balance = 0, sd_idle = 0;
	struct sched_group *group;
	unsigned long imbalance;
	struct rq *busiest;
	unsigned long flags;
	struct cpumask *cpus = __get_cpu_var(load_balance_tmpmask);

	cpumask_setall(cpus);

	/*
	 * When power savings policy is enabled for the parent domain, idle
	 * sibling can pick up load irrespective of busy siblings. In this case,
	 * let the state of idle sibling percolate up as CPU_IDLE, instead of
	 * portraying it as CPU_NOT_IDLE.
	 */
	if (idle != CPU_NOT_IDLE && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		sd_idle = 1;

	schedstat_inc(sd, lb_count[idle]);

redo:
	update_shares(sd);
	group = find_busiest_group(sd, this_cpu, &imbalance, idle, &sd_idle,
				   cpus, balance);

	if (*balance == 0)
		goto out_balanced;

	if (!group) {
		schedstat_inc(sd, lb_nobusyg[idle]);
		goto out_balanced;
	}

	busiest = find_busiest_queue(group, idle, imbalance, cpus);
	if (!busiest) {
		schedstat_inc(sd, lb_nobusyq[idle]);
		goto out_balanced;
	}

	BUG_ON(busiest == this_rq);

	schedstat_add(sd, lb_imbalance[idle], imbalance);

	ld_moved = 0;
	if (busiest->nr_running > 1) {
		/*
		 * Attempt to move tasks. If find_busiest_group has found
		 * an imbalance but busiest->nr_running <= 1, the group is
		 * still unbalanced. ld_moved simply stays zero, so it is
		 * correctly treated as an imbalance.
		 */
		local_irq_save(flags);
		double_rq_lock(this_rq, busiest);
		ld_moved = move_tasks(this_rq, this_cpu, busiest,
				      imbalance, sd, idle, &all_pinned);
		double_rq_unlock(this_rq, busiest);
		local_irq_restore(flags);

		/*
		 * some other cpu did the load balance for us.
		 */
		if (ld_moved && this_cpu != smp_processor_id())
			resched_cpu(this_cpu);

		/* All tasks on this runqueue were pinned by CPU affinity */
		if (unlikely(all_pinned)) {
			cpumask_clear_cpu(cpu_of(busiest), cpus);
			if (!cpumask_empty(cpus))
				goto redo;
			goto out_balanced;
		}
	}

	if (!ld_moved) {
		schedstat_inc(sd, lb_failed[idle]);
		sd->nr_balance_failed++;

		if (unlikely(sd->nr_balance_failed > sd->cache_nice_tries+2)) {

			spin_lock_irqsave(&busiest->lock, flags);

			/* don't kick the migration_thread, if the curr
			 * task on busiest cpu can't be moved to this_cpu
			 */
			if (!cpumask_test_cpu(this_cpu,
					      &busiest->curr->cpus_allowed)) {
				spin_unlock_irqrestore(&busiest->lock, flags);
				all_pinned = 1;
				goto out_one_pinned;
			}

			if (!busiest->active_balance) {
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				active_balance = 1;
			}
			spin_unlock_irqrestore(&busiest->lock, flags);
			if (active_balance)
				wake_up_process(busiest->migration_thread);

			/*
			 * We've kicked active balancing, reset the failure
			 * counter.
			 */
			sd->nr_balance_failed = sd->cache_nice_tries+1;
		}
	} else
		sd->nr_balance_failed = 0;

	if (likely(!active_balance)) {
		/* We were unbalanced, so reset the balancing interval */
		sd->balance_interval = sd->min_interval;
	} else {
		/*
		 * If we've begun active balancing, start to back off. This
		 * case may not be covered by the all_pinned logic if there
		 * is only 1 task on the busy runqueue (because we don't call
		 * move_tasks).
		 */
		if (sd->balance_interval < sd->max_interval)
			sd->balance_interval *= 2;
	}

	if (!ld_moved && !sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		ld_moved = -1;

	goto out;

out_balanced:
	schedstat_inc(sd, lb_balanced[idle]);

	sd->nr_balance_failed = 0;

out_one_pinned:
	/* tune up the balancing interval */
	if ((all_pinned && sd->balance_interval < MAX_PINNED_INTERVAL) ||
			(sd->balance_interval < sd->max_interval))
		sd->balance_interval *= 2;

	if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		ld_moved = -1;
	else
		ld_moved = 0;
out:
	if (ld_moved)
		update_shares(sd);
	return ld_moved;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 *
 * Called from schedule when this_rq is about to become idle (CPU_NEWLY_IDLE).
 * this_rq is locked.
 */
static int
load_balance_newidle(int this_cpu, struct rq *this_rq, struct sched_domain *sd)
{
	struct sched_group *group;
	struct rq *busiest = NULL;
	unsigned long imbalance;
	int ld_moved = 0;
	int sd_idle = 0;
	int all_pinned = 0;
	struct cpumask *cpus = __get_cpu_var(load_balance_tmpmask);

	cpumask_setall(cpus);

	/*
	 * When power savings policy is enabled for the parent domain, idle
	 * sibling can pick up load irrespective of busy siblings. In this case,
	 * let the state of idle sibling percolate up as IDLE, instead of
	 * portraying it as CPU_NOT_IDLE.
	 */
	if (sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		sd_idle = 1;

	schedstat_inc(sd, lb_count[CPU_NEWLY_IDLE]);
redo:
	update_shares_locked(this_rq, sd);
	group = find_busiest_group(sd, this_cpu, &imbalance, CPU_NEWLY_IDLE,
				   &sd_idle, cpus, NULL);
	if (!group) {
		schedstat_inc(sd, lb_nobusyg[CPU_NEWLY_IDLE]);
		goto out_balanced;
	}

	busiest = find_busiest_queue(group, CPU_NEWLY_IDLE, imbalance, cpus);
	if (!busiest) {
		schedstat_inc(sd, lb_nobusyq[CPU_NEWLY_IDLE]);
		goto out_balanced;
	}

	BUG_ON(busiest == this_rq);

	schedstat_add(sd, lb_imbalance[CPU_NEWLY_IDLE], imbalance);

	ld_moved = 0;
	if (busiest->nr_running > 1) {
		/* Attempt to move tasks */
		double_lock_balance(this_rq, busiest);
		/* this_rq->clock is already updated */
		update_rq_clock(busiest);
		ld_moved = move_tasks(this_rq, this_cpu, busiest,
					imbalance, sd, CPU_NEWLY_IDLE,
					&all_pinned);
		double_unlock_balance(this_rq, busiest);

		if (unlikely(all_pinned)) {
			cpumask_clear_cpu(cpu_of(busiest), cpus);
			if (!cpumask_empty(cpus))
				goto redo;
		}
	}

	if (!ld_moved) {
		int active_balance = 0;

		schedstat_inc(sd, lb_failed[CPU_NEWLY_IDLE]);
		if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
		    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
			return -1;

		if (sched_mc_power_savings < POWERSAVINGS_BALANCE_WAKEUP)
			return -1;

		if (sd->nr_balance_failed++ < 2)
			return -1;

		/*
		 * The only task running in a non-idle cpu can be moved to this
		 * cpu in an attempt to completely freeup the other CPU
		 * package. The same method used to move task in load_balance()
		 * have been extended for load_balance_newidle() to speedup
		 * consolidation at sched_mc=POWERSAVINGS_BALANCE_WAKEUP (2)
		 *
		 * The package power saving logic comes from
		 * find_busiest_group().  If there are no imbalance, then
		 * f_b_g() will return NULL.  However when sched_mc={1,2} then
		 * f_b_g() will select a group from which a running task may be
		 * pulled to this cpu in order to make the other package idle.
		 * If there is no opportunity to make a package idle and if
		 * there are no imbalance, then f_b_g() will return NULL and no
		 * action will be taken in load_balance_newidle().
		 *
		 * Under normal task pull operation due to imbalance, there
		 * will be more than one task in the source run queue and
		 * move_tasks() will succeed.  ld_moved will be true and this
		 * active balance code will not be triggered.
		 */

		/* Lock busiest in correct order while this_rq is held */
		double_lock_balance(this_rq, busiest);

		/*
		 * don't kick the migration_thread, if the curr
		 * task on busiest cpu can't be moved to this_cpu
		 */
		if (!cpumask_test_cpu(this_cpu, &busiest->curr->cpus_allowed)) {
			double_unlock_balance(this_rq, busiest);
			all_pinned = 1;
			return ld_moved;
		}

		if (!busiest->active_balance) {
			busiest->active_balance = 1;
			busiest->push_cpu = this_cpu;
			active_balance = 1;
		}

		double_unlock_balance(this_rq, busiest);
		/*
		 * Should not call ttwu while holding a rq->lock
		 */
		spin_unlock(&this_rq->lock);
		if (active_balance)
			wake_up_process(busiest->migration_thread);
		spin_lock(&this_rq->lock);

	} else
		sd->nr_balance_failed = 0;

	update_shares_locked(this_rq, sd);
	return ld_moved;

out_balanced:
	schedstat_inc(sd, lb_balanced[CPU_NEWLY_IDLE]);
	if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&
	    !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		return -1;
	sd->nr_balance_failed = 0;

	return 0;
}

/*
 * idle_balance is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
static void idle_balance(int this_cpu, struct rq *this_rq)
{
	struct sched_domain *sd;
	int pulled_task = 0;
	unsigned long next_balance = jiffies + HZ;

	for_each_domain(this_cpu, sd) {
		unsigned long interval;

		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		if (sd->flags & SD_BALANCE_NEWIDLE)
			/* If we've pulled tasks over stop searching: */
			pulled_task = load_balance_newidle(this_cpu, this_rq,
							   sd);

		interval = msecs_to_jiffies(sd->balance_interval);
		if (time_after(next_balance, sd->last_balance + interval))
			next_balance = sd->last_balance + interval;
		if (pulled_task)
			break;
	}
	if (pulled_task || time_after(jiffies, this_rq->next_balance)) {
		/*
		 * We are going idle. next_balance may be set based on
		 * a busy processor. So reset next_balance.
		 */
		this_rq->next_balance = next_balance;
	}
}

/*
 * active_load_balance is run by migration threads. It pushes running tasks
 * off the busiest CPU onto idle CPUs. It requires at least 1 task to be
 * running on each physical CPU where possible, and avoids physical /
 * logical imbalances.
 *
 * Called with busiest_rq locked.
 */
static void active_load_balance(struct rq *busiest_rq, int busiest_cpu)
{
	int target_cpu = busiest_rq->push_cpu;
	struct sched_domain *sd;
	struct rq *target_rq;

	/* Is there any task to move? */
	if (busiest_rq->nr_running <= 1)
		return;

	target_rq = cpu_rq(target_cpu);

	/*
	 * This condition is "impossible", if it occurs
	 * we need to fix it. Originally reported by
	 * Bjorn Helgaas on a 128-cpu setup.
	 */
	BUG_ON(busiest_rq == target_rq);

	/* move a task from busiest_rq to target_rq */
	double_lock_balance(busiest_rq, target_rq);
	update_rq_clock(busiest_rq);
	update_rq_clock(target_rq);

	/* Search for an sd spanning us and the target CPU. */
	for_each_domain(target_cpu, sd) {
		if ((sd->flags & SD_LOAD_BALANCE) &&
		    cpumask_test_cpu(busiest_cpu, sched_domain_span(sd)))
				break;
	}

	if (likely(sd)) {
		schedstat_inc(sd, alb_count);

		if (move_one_task(target_rq, target_cpu, busiest_rq,
				  sd, CPU_IDLE))
			schedstat_inc(sd, alb_pushed);
		else
			schedstat_inc(sd, alb_failed);
	}
	double_unlock_balance(busiest_rq, target_rq);
}

#ifdef CONFIG_NO_HZ
static struct {
	atomic_t load_balancer;
	cpumask_var_t cpu_mask;
	cpumask_var_t ilb_grp_nohz_mask;
} nohz ____cacheline_aligned = {
	.load_balancer = ATOMIC_INIT(-1),
};

#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
/**
 * lowest_flag_domain - Return lowest sched_domain containing flag.
 * @cpu:	The cpu whose lowest level of sched domain is to
 *		be returned.
 * @flag:	The flag to check for the lowest sched_domain
 *		for the given cpu.
 *
 * Returns the lowest sched_domain of a cpu which contains the given flag.
 */
static inline struct sched_domain *lowest_flag_domain(int cpu, int flag)
{
	struct sched_domain *sd;

	for_each_domain(cpu, sd)
		if (sd && (sd->flags & flag))
			break;

	return sd;
}

/**
 * for_each_flag_domain - Iterates over sched_domains containing the flag.
 * @cpu:	The cpu whose domains we're iterating over.
 * @sd:		variable holding the value of the power_savings_sd
 *		for cpu.
 * @flag:	The flag to filter the sched_domains to be iterated.
 *
 * Iterates over all the scheduler domains for a given cpu that has the 'flag'
 * set, starting from the lowest sched_domain to the highest.
 */
#define for_each_flag_domain(cpu, sd, flag) \
	for (sd = lowest_flag_domain(cpu, flag); \
		(sd && (sd->flags & flag)); sd = sd->parent)

/**
 * is_semi_idle_group - Checks if the given sched_group is semi-idle.
 * @ilb_group:	group to be checked for semi-idleness
 *
 * Returns:	1 if the group is semi-idle. 0 otherwise.
 *
 * We define a sched_group to be semi idle if it has atleast one idle-CPU
 * and atleast one non-idle CPU. This helper function checks if the given
 * sched_group is semi-idle or not.
 */
static inline int is_semi_idle_group(struct sched_group *ilb_group)
{
	cpumask_and(nohz.ilb_grp_nohz_mask, nohz.cpu_mask,
					sched_group_cpus(ilb_group));

	/*
	 * A sched_group is semi-idle when it has atleast one busy cpu
	 * and atleast one idle cpu.
	 */
	if (cpumask_empty(nohz.ilb_grp_nohz_mask))
		return 0;

	if (cpumask_equal(nohz.ilb_grp_nohz_mask, sched_group_cpus(ilb_group)))
		return 0;

	return 1;
}
/**
 * find_new_ilb - Finds the optimum idle load balancer for nomination.
 * @cpu:	The cpu which is nominating a new idle_load_balancer.
 *
 * Returns:	Returns the id of the idle load balancer if it exists,
 *		Else, returns >= nr_cpu_ids.
 *
 * This algorithm picks the idle load balancer such that it belongs to a
 * semi-idle powersavings sched_domain. The idea is to try and avoid
 * completely idle packages/cores just for the purpose of idle load balancing
 * when there are other idle cpu's which are better suited for that job.
 */
static int find_new_ilb(int cpu)
{
	struct sched_domain *sd;
	struct sched_group *ilb_group;

	/*
	 * Have idle load balancer selection from semi-idle packages only
	 * when power-aware load balancing is enabled
	 */
	if (!(sched_smt_power_savings || sched_mc_power_savings))
		goto out_done;

	/*
	 * Optimize for the case when we have no idle CPUs or only one
	 * idle CPU. Don't walk the sched_domain hierarchy in such cases
	 */
	if (cpumask_weight(nohz.cpu_mask) < 2)
		goto out_done;

	for_each_flag_domain(cpu, sd, SD_POWERSAVINGS_BALANCE) {
		ilb_group = sd->groups;

		do {
			if (is_semi_idle_group(ilb_group))
				return cpumask_first(nohz.ilb_grp_nohz_mask);

			ilb_group = ilb_group->next;

		} while (ilb_group != sd->groups);
	}

out_done:
	return cpumask_first(nohz.cpu_mask);
}
#else /*  (CONFIG_SCHED_MC || CONFIG_SCHED_SMT) */
static inline int find_new_ilb(int call_cpu)
{
	return first_cpu(nohz.cpu_mask);
}
#endif

/*
 * This routine will try to nominate the ilb (idle load balancing)
 * owner among the cpus whose ticks are stopped. ilb owner will do the idle
 * load balancing on behalf of all those cpus. If all the cpus in the system
 * go into this tickless mode, then there will be no ilb owner (as there is
 * no need for one) and all the cpus will sleep till the next wakeup event
 * arrives...
 *
 * For the ilb owner, tick is not stopped. And this tick will be used
 * for idle load balancing. ilb owner will still be part of
 * nohz.cpu_mask..
 *
 * While stopping the tick, this cpu will become the ilb owner if there
 * is no other owner. And will be the owner till that cpu becomes busy
 * or if all cpus in the system stop their ticks at which point
 * there is no need for ilb owner.
 *
 * When the ilb owner becomes busy, it nominates another owner, during the
 * next busy scheduler_tick()
 */
int select_nohz_load_balancer(int stop_tick)
{
	int cpu = smp_processor_id();

	if (stop_tick) {
		cpu_rq(cpu)->in_nohz_recently = 1;

		if (!cpu_active(cpu)) {
			if (atomic_read(&nohz.load_balancer) != cpu)
				return 0;

			/*
			 * If we are going offline and still the leader,
			 * give up!
			 */
			if (atomic_cmpxchg(&nohz.load_balancer, cpu, -1) != cpu)
				BUG();

			return 0;
		}

		cpumask_set_cpu(cpu, nohz.cpu_mask);

		/* time for ilb owner also to sleep */
		if (cpumask_weight(nohz.cpu_mask) == num_online_cpus()) {
			if (atomic_read(&nohz.load_balancer) == cpu)
				atomic_set(&nohz.load_balancer, -1);
			return 0;
		}

		if (atomic_read(&nohz.load_balancer) == -1) {
			/* make me the ilb owner */
			if (atomic_cmpxchg(&nohz.load_balancer, -1, cpu) == -1)
				return 1;
		} else if (atomic_read(&nohz.load_balancer) == cpu)
			return 1;
	} else {
		if (!cpumask_test_cpu(cpu, nohz.cpu_mask))
			return 0;

		cpumask_clear_cpu(cpu, nohz.cpu_mask);

		if (atomic_read(&nohz.load_balancer) == cpu)
			if (atomic_cmpxchg(&nohz.load_balancer, cpu, -1) != cpu)
				BUG();
	}
	return 0;
}
#endif

static DEFINE_SPINLOCK(balancing);

/*
 * It checks each scheduling domain to see if it is due to be balanced,
 * and initiates a balancing operation if so.
 *
 * Balancing parameters are set up in arch_init_sched_domains.
 */
static void rebalance_domains(int cpu, enum cpu_idle_type idle)
{
	int balance = 1;
	struct rq *rq = cpu_rq(cpu);
	unsigned long interval;
	struct sched_domain *sd;
	/* Earliest time when we have to do rebalance again */
	unsigned long next_balance = jiffies + 60*HZ;
	int update_next_balance = 0;
	int need_serialize;

	for_each_domain(cpu, sd) {
		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		interval = sd->balance_interval;
		if (idle != CPU_IDLE)
			interval *= sd->busy_factor;

		/* scale ms to jiffies */
		interval = msecs_to_jiffies(interval);
		if (unlikely(!interval))
			interval = 1;
		if (interval > HZ*NR_CPUS/10)
			interval = HZ*NR_CPUS/10;

		need_serialize = sd->flags & SD_SERIALIZE;

		if (need_serialize) {
			if (!spin_trylock(&balancing))
				goto out;
		}

		if (time_after_eq(jiffies, sd->last_balance + interval)) {
			if (load_balance(cpu, rq, sd, idle, &balance)) {
				/*
				 * We've pulled tasks over so either we're no
				 * longer idle, or one of our SMT siblings is
				 * not idle.
				 */
				idle = CPU_NOT_IDLE;
			}
			sd->last_balance = jiffies;
		}
		if (need_serialize)
			spin_unlock(&balancing);
out:
		if (time_after(next_balance, sd->last_balance + interval)) {
			next_balance = sd->last_balance + interval;
			update_next_balance = 1;
		}

		/*
		 * Stop the load balance at this level. There is another
		 * CPU in our sched group which is doing load balancing more
		 * actively.
		 */
		if (!balance)
			break;
	}

	/*
	 * next_balance will be updated only when there is a need.
	 * When the cpu is attached to null domain for ex, it will not be
	 * updated.
	 */
	if (likely(update_next_balance))
		rq->next_balance = next_balance;
}

/*
 * run_rebalance_domains is triggered when needed from the scheduler tick.
 * In CONFIG_NO_HZ case, the idle load balance owner will do the
 * rebalancing for all the cpus for whom scheduler ticks are stopped.
 */
static void run_rebalance_domains(struct softirq_action *h)
{
	int this_cpu = smp_processor_id();
	struct rq *this_rq = cpu_rq(this_cpu);
	enum cpu_idle_type idle = this_rq->idle_at_tick ?
						CPU_IDLE : CPU_NOT_IDLE;

	rebalance_domains(this_cpu, idle);

#ifdef CONFIG_NO_HZ
	/*
	 * If this cpu is the owner for idle load balancing, then do the
	 * balancing on behalf of the other idle cpus whose ticks are
	 * stopped.
	 */
	if (this_rq->idle_at_tick &&
	    atomic_read(&nohz.load_balancer) == this_cpu) {
		struct rq *rq;
		int balance_cpu;

		for_each_cpu(balance_cpu, nohz.cpu_mask) {
			if (balance_cpu == this_cpu)
				continue;

			/*
			 * If this cpu gets work to do, stop the load balancing
			 * work being done for other cpus. Next load
			 * balancing owner will pick it up.
			 */
			if (need_resched())
				break;

			rebalance_domains(balance_cpu, CPU_IDLE);

			rq = cpu_rq(balance_cpu);
			if (time_after(this_rq->next_balance, rq->next_balance))
				this_rq->next_balance = rq->next_balance;
		}
	}
#endif
}

static inline int on_null_domain(int cpu)
{
	return !rcu_dereference(cpu_rq(cpu)->sd);
}

/*
 * Trigger the SCHED_SOFTIRQ if it is time to do periodic load balancing.
 *
 * In case of CONFIG_NO_HZ, this is the place where we nominate a new
 * idle load balancing owner or decide to stop the periodic load balancing,
 * if the whole system is idle.
 */
static inline void trigger_load_balance(struct rq *rq, int cpu)
{
#ifdef CONFIG_NO_HZ
	/*
	 * If we were in the nohz mode recently and busy at the current
	 * scheduler tick, then check if we need to nominate new idle
	 * load balancer.
	 */
	if (rq->in_nohz_recently && !rq->idle_at_tick) {
		rq->in_nohz_recently = 0;

		if (atomic_read(&nohz.load_balancer) == cpu) {
			cpumask_clear_cpu(cpu, nohz.cpu_mask);
			atomic_set(&nohz.load_balancer, -1);
		}

		if (atomic_read(&nohz.load_balancer) == -1) {
			int ilb = find_new_ilb(cpu);

			if (ilb < nr_cpu_ids)
				resched_cpu(ilb);
		}
	}

	/*
	 * If this cpu is idle and doing idle load balancing for all the
	 * cpus with ticks stopped, is it time for that to stop?
	 */
	if (rq->idle_at_tick && atomic_read(&nohz.load_balancer) == cpu &&
	    cpumask_weight(nohz.cpu_mask) == num_online_cpus()) {
		resched_cpu(cpu);
		return;
	}

	/*
	 * If this cpu is idle and the idle load balancing is done by
	 * someone else, then no need raise the SCHED_SOFTIRQ
	 */
	if (rq->idle_at_tick && atomic_read(&nohz.load_balancer) != cpu &&
	    cpumask_test_cpu(cpu, nohz.cpu_mask))
		return;
#endif
	/* Don't need to rebalance while attached to NULL domain */
	if (time_after_eq(jiffies, rq->next_balance) &&
	    likely(!on_null_domain(cpu)))
		raise_softirq(SCHED_SOFTIRQ);
}

#else	/* CONFIG_SMP */

/*
 * on UP we do not need to balance between CPUs:
 */
static inline void idle_balance(int cpu, struct rq *rq)
{
}

#endif

DEFINE_PER_CPU(struct kernel_stat, kstat);

EXPORT_PER_CPU_SYMBOL(kstat);

/*
 * Return any ns on the sched_clock that have not yet been accounted in
 * @p in case that task is currently running.
 *
 * Called with task_rq_lock() held on @rq.
 */
static u64 do_task_delta_exec(struct task_struct *p, struct rq *rq)
{
	u64 ns = 0;

	if (task_current(rq, p)) {
		update_rq_clock(rq);
		ns = rq->clock - p->se.exec_start;
		if ((s64)ns < 0)
			ns = 0;
	}

	return ns;
}

unsigned long long task_delta_exec(struct task_struct *p)
{
	unsigned long flags;
	struct rq *rq;
	u64 ns = 0;

	rq = task_rq_lock(p, &flags);
	ns = do_task_delta_exec(p, rq);
	task_rq_unlock(rq, &flags);

	return ns;
}

/*
 * Return accounted runtime for the task.
 * In case the task is currently running, return the runtime plus current's
 * pending runtime that have not been accounted yet.
 */
unsigned long long task_sched_runtime(struct task_struct *p)
{
	unsigned long flags;
	struct rq *rq;
	u64 ns = 0;

	rq = task_rq_lock(p, &flags);
	ns = p->se.sum_exec_runtime + do_task_delta_exec(p, rq);
	task_rq_unlock(rq, &flags);

	return ns;
}

/*
 * Return sum_exec_runtime for the thread group.
 * In case the task is currently running, return the sum plus current's
 * pending runtime that have not been accounted yet.
 *
 * Note that the thread group might have other running tasks as well,
 * so the return value not includes other pending runtime that other
 * running tasks might have.
 */
unsigned long long thread_group_sched_runtime(struct task_struct *p)
{
	struct task_cputime totals;
	unsigned long flags;
	struct rq *rq;
	u64 ns;

	rq = task_rq_lock(p, &flags);
	thread_group_cputime(p, &totals);
	ns = totals.sum_exec_runtime + do_task_delta_exec(p, rq);
	task_rq_unlock(rq, &flags);

	return ns;
}

/*
 * Account user cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in user space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_user_time(struct task_struct *p, cputime_t cputime,
		       cputime_t cputime_scaled)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t tmp;

	/* Add user time to process. */
	p->utime = cputime_add(p->utime, cputime);
	p->utimescaled = cputime_add(p->utimescaled, cputime_scaled);
	account_group_user_time(p, cputime);

	/* Add user time to cpustat. */
	tmp = cputime_to_cputime64(cputime);
	if (TASK_NICE(p) > 0)
		cpustat->nice = cputime64_add(cpustat->nice, tmp);
	else
		cpustat->user = cputime64_add(cpustat->user, tmp);

	cpuacct_update_stats(p, CPUACCT_STAT_USER, cputime);
	/* Account for user time used */
	acct_update_integrals(p);
}

/*
 * Account guest cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in virtual machine since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
static void account_guest_time(struct task_struct *p, cputime_t cputime,
			       cputime_t cputime_scaled)
{
	cputime64_t tmp;
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;

	tmp = cputime_to_cputime64(cputime);

	/* Add guest time to process. */
	p->utime = cputime_add(p->utime, cputime);
	p->utimescaled = cputime_add(p->utimescaled, cputime_scaled);
	account_group_user_time(p, cputime);
	p->gtime = cputime_add(p->gtime, cputime);

	/* Add guest time to cpustat. */
	cpustat->user = cputime64_add(cpustat->user, tmp);
	cpustat->guest = cputime64_add(cpustat->guest, tmp);
}

/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_system_time(struct task_struct *p, int hardirq_offset,
			 cputime_t cputime, cputime_t cputime_scaled)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t tmp;

	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0)) {
		account_guest_time(p, cputime, cputime_scaled);
		return;
	}

	/* Add system time to process. */
	p->stime = cputime_add(p->stime, cputime);
	p->stimescaled = cputime_add(p->stimescaled, cputime_scaled);
	account_group_system_time(p, cputime);

	/* Add system time to cpustat. */
	tmp = cputime_to_cputime64(cputime);
	if (hardirq_count() - hardirq_offset)
		cpustat->irq = cputime64_add(cpustat->irq, tmp);
	else if (softirq_count())
		cpustat->softirq = cputime64_add(cpustat->softirq, tmp);
	else
		cpustat->system = cputime64_add(cpustat->system, tmp);

	cpuacct_update_stats(p, CPUACCT_STAT_SYSTEM, cputime);

	/* Account for system time used */
	acct_update_integrals(p);
}

/*
 * Account for involuntary wait time.
 * @steal: the cpu time spent in involuntary wait
 */
void account_steal_time(cputime_t cputime)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t cputime64 = cputime_to_cputime64(cputime);

	cpustat->steal = cputime64_add(cpustat->steal, cputime64);
}

/*
 * Account for idle time.
 * @cputime: the cpu time spent in idle wait
 */
void account_idle_time(cputime_t cputime)
{
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	cputime64_t cputime64 = cputime_to_cputime64(cputime);
	struct rq *rq = this_rq();

	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat->iowait = cputime64_add(cpustat->iowait, cputime64);
	else
		cpustat->idle = cputime64_add(cpustat->idle, cputime64);
}

#ifndef CONFIG_VIRT_CPU_ACCOUNTING

/*
 * Account a single tick of cpu time.
 * @p: the process that the cpu time gets accounted to
 * @user_tick: indicates if the tick is a user or a system tick
 */
void account_process_tick(struct task_struct *p, int user_tick)
{
	cputime_t one_jiffy = jiffies_to_cputime(1);
	cputime_t one_jiffy_scaled = cputime_to_scaled(one_jiffy);
	struct rq *rq = this_rq();

	if (user_tick)
		account_user_time(p, one_jiffy, one_jiffy_scaled);
	else if (p != rq->idle)
		account_system_time(p, HARDIRQ_OFFSET, one_jiffy,
				    one_jiffy_scaled);
	else
		account_idle_time(one_jiffy);
}

/*
 * Account multiple ticks of steal time.
 * @p: the process from which the cpu time has been stolen
 * @ticks: number of stolen ticks
 */
void account_steal_ticks(unsigned long ticks)
{
	account_steal_time(jiffies_to_cputime(ticks));
}

/*
 * Account multiple ticks of idle time.
 * @ticks: number of stolen ticks
 */
void account_idle_ticks(unsigned long ticks)
{
	account_idle_time(jiffies_to_cputime(ticks));
}

#endif

/*
 * Use precise platform statistics if available:
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
cputime_t task_utime(struct task_struct *p)
{
	return p->utime;
}

cputime_t task_stime(struct task_struct *p)
{
	return p->stime;
}
#else
cputime_t task_utime(struct task_struct *p)
{
	clock_t utime = cputime_to_clock_t(p->utime),
		total = utime + cputime_to_clock_t(p->stime);
	u64 temp;

	/*
	 * Use CFS's precise accounting:
	 */
	temp = (u64)nsec_to_clock_t(p->se.sum_exec_runtime);

	if (total) {
		temp *= utime;
		do_div(temp, total);
	}
	utime = (clock_t)temp;

	p->prev_utime = max(p->prev_utime, clock_t_to_cputime(utime));
	return p->prev_utime;
}

cputime_t task_stime(struct task_struct *p)
{
	clock_t stime;

	/*
	 * Use CFS's precise accounting. (we subtract utime from
	 * the total, to make sure the total observed by userspace
	 * grows monotonically - apps rely on that):
	 */
	stime = nsec_to_clock_t(p->se.sum_exec_runtime) -
			cputime_to_clock_t(task_utime(p));

	if (stime >= 0)
		p->prev_stime = max(p->prev_stime, clock_t_to_cputime(stime));

	return p->prev_stime;
}
#endif

inline cputime_t task_gtime(struct task_struct *p)
{
	return p->gtime;
}

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 *
 * It also gets called by the fork code, when changing the parent's
 * timeslices.
 */
void scheduler_tick(void)
{
	int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct task_struct *curr = rq->curr;

	sched_clock_tick();

	spin_lock(&rq->lock);
	update_rq_clock(rq);
	update_cpu_load(rq);
	curr->sched_class->task_tick(rq, curr, 0);
	spin_unlock(&rq->lock);

#ifdef CONFIG_SMP
	rq->idle_at_tick = idle_cpu(cpu);
	trigger_load_balance(rq, cpu);
#endif
}

unsigned long get_parent_ip(unsigned long addr)
{
	if (in_lock_functions(addr)) {
		addr = CALLER_ADDR2;
		if (in_lock_functions(addr))
			addr = CALLER_ADDR3;
	}
	return addr;
}

#if defined(CONFIG_PREEMPT) && (defined(CONFIG_DEBUG_PREEMPT) || \
				defined(CONFIG_PREEMPT_TRACER))

void __kprobes add_preempt_count(int val)
{
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Underflow?
	 */
	if (DEBUG_LOCKS_WARN_ON((preempt_count() < 0)))
		return;
#endif
	preempt_count() += val;
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Spinlock count overflowing soon?
	 */
	DEBUG_LOCKS_WARN_ON((preempt_count() & PREEMPT_MASK) >=
				PREEMPT_MASK - 10);
#endif
	if (preempt_count() == val)
		trace_preempt_off(CALLER_ADDR0, get_parent_ip(CALLER_ADDR1));
}
EXPORT_SYMBOL(add_preempt_count);

void __kprobes sub_preempt_count(int val)
{
#ifdef CONFIG_DEBUG_PREEMPT
	/*
	 * Underflow?
	 */
	if (DEBUG_LOCKS_WARN_ON(val > preempt_count()))
		return;
	/*
	 * Is the spinlock portion underflowing?
	 */
	if (DEBUG_LOCKS_WARN_ON((val < PREEMPT_MASK) &&
			!(preempt_count() & PREEMPT_MASK)))
		return;
#endif

	if (preempt_count() == val)
		trace_preempt_on(CALLER_ADDR0, get_parent_ip(CALLER_ADDR1));
	preempt_count() -= val;
}
EXPORT_SYMBOL(sub_preempt_count);

#endif

/*
 * Print scheduling while atomic bug:
 */
static noinline void __schedule_bug(struct task_struct *prev)
{
	struct pt_regs *regs = get_irq_regs();

	printk(KERN_ERR "BUG: scheduling while atomic: %s/%d/0x%08x\n",
		prev->comm, prev->pid, preempt_count());

	debug_show_held_locks(prev);
	print_modules();
	if (irqs_disabled())
		print_irqtrace_events(prev);

	if (regs)
		show_regs(regs);
	else
		dump_stack();
}

/*
 * Various schedule()-time debugging checks and statistics:
 */
static inline void schedule_debug(struct task_struct *prev)
{
	/*
	 * Test if we are atomic. Since do_exit() needs to call into
	 * schedule() atomically, we ignore that path for now.
	 * Otherwise, whine if we are scheduling when we should not be.
	 */
	if (unlikely(in_atomic_preempt_off() && !prev->exit_state))
		__schedule_bug(prev);

	profile_hit(SCHED_PROFILING, __builtin_return_address(0));

	schedstat_inc(this_rq(), sched_count);
#ifdef CONFIG_SCHEDSTATS
	if (unlikely(prev->lock_depth >= 0)) {
		schedstat_inc(this_rq(), bkl_count);
		schedstat_inc(prev, sched_info.bkl_count);
	}
#endif
}

static void put_prev_task(struct rq *rq, struct task_struct *prev)
{
	if (prev->state == TASK_RUNNING) {
		u64 runtime = prev->se.sum_exec_runtime;

		runtime -= prev->se.prev_sum_exec_runtime;
		runtime = min_t(u64, runtime, 2*sysctl_sched_migration_cost);

		/*
		 * In order to avoid avg_overlap growing stale when we are
		 * indeed overlapping and hence not getting put to sleep, grow
		 * the avg_overlap on preemption.
		 *
		 * We use the average preemption runtime because that
		 * correlates to the amount of cache footprint a task can
		 * build up.
		 */
		update_avg(&prev->se.avg_overlap, runtime);
	}
	prev->sched_class->put_prev_task(rq, prev);
}

/*
 * Pick up the highest-prio task:
 */
static inline struct task_struct *
pick_next_task(struct rq *rq)
{
	const struct sched_class *class;
	struct task_struct *p;

	/*
	 * Optimization: we know that if all tasks are in
	 * the fair class we can call that function directly:
	 */
	if (likely(rq->nr_running == rq->cfs.nr_running)) {
		p = fair_sched_class.pick_next_task(rq);
		if (likely(p))
			return p;
	}

	class = sched_class_highest;
	for ( ; ; ) {
		p = class->pick_next_task(rq);
		if (p)
			return p;
		/*
		 * Will never be NULL as the idle class always
		 * returns a non-NULL p:
		 */
		class = class->next;
	}
}

/*
 * schedule() is the main scheduler function.
 */
asmlinkage void __sched __schedule(void)
{
	struct task_struct *prev, *next;
	unsigned long *switch_count;
	struct rq *rq;
	int cpu;

	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	rcu_qsctr_inc(cpu);
	prev = rq->curr;
	switch_count = &prev->nivcsw;

	release_kernel_lock(prev);
need_resched_nonpreemptible:

	schedule_debug(prev);

	if (sched_feat(HRTICK))
		hrtick_clear(rq);

	spin_lock_irq(&rq->lock);
	update_rq_clock(rq);
	clear_tsk_need_resched(prev);

	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
		if (unlikely(signal_pending_state(prev->state, prev)))
			prev->state = TASK_RUNNING;
		else
			deactivate_task(rq, prev, 1);
		switch_count = &prev->nvcsw;
	}

#ifdef CONFIG_SMP
	if (prev->sched_class->pre_schedule)
		prev->sched_class->pre_schedule(rq, prev);
#endif

	if (unlikely(!rq->nr_running))
		idle_balance(cpu, rq);

	put_prev_task(rq, prev);
	next = pick_next_task(rq);

	if (likely(prev != next)) {
		sched_info_switch(prev, next);

		rq->nr_switches++;
		rq->curr = next;
		++*switch_count;

		context_switch(rq, prev, next); /* unlocks the rq */
		/*
		 * the context switch might have flipped the stack from under
		 * us, hence refresh the local variables.
		 */
		cpu = smp_processor_id();
		rq = cpu_rq(cpu);
	} else
		spin_unlock_irq(&rq->lock);

	if (unlikely(reacquire_kernel_lock(current) < 0))
		goto need_resched_nonpreemptible;
}

asmlinkage void __sched schedule(void)
{
need_resched:
	preempt_disable();
	__schedule();
	preempt_enable_no_resched();
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED)))
		goto need_resched;
}
EXPORT_SYMBOL(schedule);

#ifdef CONFIG_SMP
/*
 * Look out! "owner" is an entirely speculative pointer
 * access and not reliable.
 */
int mutex_spin_on_owner(struct mutex *lock, struct thread_info *owner)
{
	unsigned int cpu;
	struct rq *rq;

	if (!sched_feat(OWNER_SPIN))
		return 0;

#ifdef CONFIG_DEBUG_PAGEALLOC
	/*
	 * Need to access the cpu field knowing that
	 * DEBUG_PAGEALLOC could have unmapped it if
	 * the mutex owner just released it and exited.
	 */
	if (probe_kernel_address(&owner->cpu, cpu))
		goto out;
#else
	cpu = owner->cpu;
#endif

	/*
	 * Even if the access succeeded (likely case),
	 * the cpu field may no longer be valid.
	 */
	if (cpu >= nr_cpumask_bits)
		goto out;

	/*
	 * We need to validate that we can do a
	 * get_cpu() and that we have the percpu area.
	 */
	if (!cpu_online(cpu))
		goto out;

	rq = cpu_rq(cpu);

	for (;;) {
		/*
		 * Owner changed, break to re-assess state.
		 */
		if (lock->owner != owner)
			break;

		/*
		 * Is that owner really running on that cpu?
		 */
		if (task_thread_info(rq->curr) != owner || need_resched())
			return 0;

		cpu_relax();
	}
out:
	return 1;
}
#endif

#ifdef CONFIG_PREEMPT
/*
 * this is the entry point to schedule() from in-kernel preemption
 * off of preempt_enable. Kernel preemptions off return from interrupt
 * occur there and call schedule directly.
 */
asmlinkage void __sched preempt_schedule(void)
{
	struct thread_info *ti = current_thread_info();

	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task. Just return..
	 */
	if (likely(ti->preempt_count || irqs_disabled()))
		return;

	do {
		add_preempt_count(PREEMPT_ACTIVE);
		schedule();
		sub_preempt_count(PREEMPT_ACTIVE);

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
		barrier();
	} while (need_resched());
}
EXPORT_SYMBOL(preempt_schedule);

/*
 * this is the entry point to schedule() from kernel preemption
 * off of irq context.
 * Note, that this is called and return with irqs disabled. This will
 * protect us against recursive calling from irq.
 */
asmlinkage void __sched preempt_schedule_irq(void)
{
	struct thread_info *ti = current_thread_info();

	/* Catch callers which need to be fixed */
	BUG_ON(ti->preempt_count || !irqs_disabled());

	do {
		add_preempt_count(PREEMPT_ACTIVE);
		local_irq_enable();
		schedule();
		local_irq_disable();
		sub_preempt_count(PREEMPT_ACTIVE);

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
		barrier();
	} while (need_resched());
}

#endif /* CONFIG_PREEMPT */

int default_wake_function(wait_queue_t *curr, unsigned mode, int sync,
			  void *key)
{
	return try_to_wake_up(curr->private, mode, sync);
}
EXPORT_SYMBOL(default_wake_function);

/*
 * The core wakeup function. Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up. If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING. try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, int sync, void *key)
{
	wait_queue_t *curr, *next;

	list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
		unsigned flags = curr->flags;

		if (curr->func(curr, mode, sync, key) &&
				(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}

/**
 * __wake_up - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: is directly passed to the wakeup function
 */
void __wake_up(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, 0, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(__wake_up);

/*
 * Same as __wake_up but called with the spinlock in wait_queue_head_t held.
 */
void __wake_up_locked(wait_queue_head_t *q, unsigned int mode)
{
	__wake_up_common(q, mode, 1, 0, NULL);
}

void __wake_up_locked_key(wait_queue_head_t *q, unsigned int mode, void *key)
{
	__wake_up_common(q, mode, 1, 0, key);
}

/**
 * __wake_up_sync_key - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: opaque value to be passed to wakeup targets
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronized'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 */
void __wake_up_sync_key(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;
	int sync = 1;

	if (unlikely(!q))
		return;

	if (unlikely(!nr_exclusive))
		sync = 0;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, sync, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(__wake_up_sync_key);

/*
 * __wake_up_sync - see __wake_up_sync_key()
 */
void __wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	__wake_up_sync_key(q, mode, nr_exclusive, NULL);
}
EXPORT_SYMBOL_GPL(__wake_up_sync);	/* For internal use only */

/**
 * complete: - signals a single thread waiting on this completion
 * @x:  holds the state of this particular completion
 *
 * This will wake up a single thread waiting on this completion. Threads will be
 * awakened in the same order in which they were queued.
 *
 * See also complete_all(), wait_for_completion() and related routines.
 */
void complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_common(&x->wait, TASK_NORMAL, 1, 0, NULL);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete);

/**
 * complete_all: - signals all threads waiting on this completion
 * @x:  holds the state of this particular completion
 *
 * This will wake up all threads waiting on this particular completion event.
 */
void complete_all(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done += UINT_MAX/2;
	__wake_up_common(&x->wait, TASK_NORMAL, 0, 0, NULL);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete_all);

static inline long __sched
do_wait_for_common(struct completion *x, long timeout, int state)
{
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			if (signal_pending_state(state, current)) {
				timeout = -ERESTARTSYS;
				break;
			}
			__set_current_state(state);
			spin_unlock_irq(&x->wait.lock);
			timeout = schedule_timeout(timeout);
			spin_lock_irq(&x->wait.lock);
		} while (!x->done && timeout);
		__remove_wait_queue(&x->wait, &wait);
		if (!x->done)
			return timeout;
	}
	x->done--;
	return timeout ?: 1;
}

static long __sched
wait_for_common(struct completion *x, long timeout, int state)
{
	might_sleep();

	spin_lock_irq(&x->wait.lock);
	timeout = do_wait_for_common(x, timeout, state);
	spin_unlock_irq(&x->wait.lock);
	return timeout;
}

/**
 * wait_for_completion: - waits for completion of a task
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It is NOT
 * interruptible and there is no timeout.
 *
 * See also similar routines (i.e. wait_for_completion_timeout()) with timeout
 * and interrupt capability. Also see complete().
 */
void __sched wait_for_completion(struct completion *x)
{
	wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion);

/**
 * wait_for_completion_timeout: - waits for completion of a task (w/timeout)
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. The timeout is in jiffies. It is not
 * interruptible.
 */
unsigned long __sched
wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_timeout);

/**
 * wait_for_completion_interruptible: - waits for completion of a task (w/intr)
 * @x:  holds the state of this particular completion
 *
 * This waits for completion of a specific task to be signaled. It is
 * interruptible.
 */
int __sched wait_for_completion_interruptible(struct completion *x)
{
	long t = wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_INTERRUPTIBLE);
	if (t == -ERESTARTSYS)
		return t;
	return 0;
}
EXPORT_SYMBOL(wait_for_completion_interruptible);

/**
 * wait_for_completion_interruptible_timeout: - waits for completion (w/(to,intr))
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. It is interruptible. The timeout is in jiffies.
 */
unsigned long __sched
wait_for_completion_interruptible_timeout(struct completion *x,
					  unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_INTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_interruptible_timeout);

/**
 * wait_for_completion_killable: - waits for completion of a task (killable)
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It can be
 * interrupted by a kill signal.
 */
int __sched wait_for_completion_killable(struct completion *x)
{
	long t = wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_KILLABLE);
	if (t == -ERESTARTSYS)
		return t;
	return 0;
}
EXPORT_SYMBOL(wait_for_completion_killable);

/**
 *	try_wait_for_completion - try to decrement a completion without blocking
 *	@x:	completion structure
 *
 *	Returns: 0 if a decrement cannot be done without blocking
 *		 1 if a decrement succeeded.
 *
 *	If a completion is being used as a counting completion,
 *	attempt to decrement the counter without blocking. This
 *	enables us to avoid waiting if the resource the completion
 *	is protecting is not available.
 */
bool try_wait_for_completion(struct completion *x)
{
	int ret = 1;

	spin_lock_irq(&x->wait.lock);
	if (!x->done)
		ret = 0;
	else
		x->done--;
	spin_unlock_irq(&x->wait.lock);
	return ret;
}
EXPORT_SYMBOL(try_wait_for_completion);

/**
 *	completion_done - Test to see if a completion has any waiters
 *	@x:	completion structure
 *
 *	Returns: 0 if there are waiters (wait_for_completion() in progress)
 *		 1 if there are no waiters.
 *
 */
bool completion_done(struct completion *x)
{
	int ret = 1;

	spin_lock_irq(&x->wait.lock);
	if (!x->done)
		ret = 0;
	spin_unlock_irq(&x->wait.lock);
	return ret;
}
EXPORT_SYMBOL(completion_done);

static long __sched
sleep_on_common(wait_queue_head_t *q, int state, long timeout)
{
	unsigned long flags;
	wait_queue_t wait;

	init_waitqueue_entry(&wait, current);

	__set_current_state(state);

	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue(q, &wait);
	spin_unlock(&q->lock);
	timeout = schedule_timeout(timeout);
	spin_lock_irq(&q->lock);
	__remove_wait_queue(q, &wait);
	spin_unlock_irqrestore(&q->lock, flags);

	return timeout;
}

void __sched interruptible_sleep_on(wait_queue_head_t *q)
{
	sleep_on_common(q, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}
EXPORT_SYMBOL(interruptible_sleep_on);

long __sched
interruptible_sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	return sleep_on_common(q, TASK_INTERRUPTIBLE, timeout);
}
EXPORT_SYMBOL(interruptible_sleep_on_timeout);

void __sched sleep_on(wait_queue_head_t *q)
{
	sleep_on_common(q, TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}
EXPORT_SYMBOL(sleep_on);

long __sched sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	return sleep_on_common(q, TASK_UNINTERRUPTIBLE, timeout);
}
EXPORT_SYMBOL(sleep_on_timeout);

#ifdef CONFIG_RT_MUTEXES

/*
 * rt_mutex_setprio - set the current priority of a task
 * @p: task
 * @prio: prio value (kernel-internal form)
 *
 * This function changes the 'effective' priority of a task. It does
 * not touch ->normal_prio like __setscheduler().
 *
 * Used by the rt_mutex code to implement priority inheritance logic.
 */
void rt_mutex_setprio(struct task_struct *p, int prio)
{
	unsigned long flags;
	int oldprio, on_rq, running;
	struct rq *rq;
	const struct sched_class *prev_class = p->sched_class;

	BUG_ON(prio < 0 || prio > MAX_PRIO);

	rq = task_rq_lock(p, &flags);
	update_rq_clock(rq);

	oldprio = p->prio;
	on_rq = p->se.on_rq;
	running = task_current(rq, p);
	if (on_rq)
		dequeue_task(rq, p, 0);
	if (running)
		p->sched_class->put_prev_task(rq, p);

	if (rt_prio(prio))
		p->sched_class = &rt_sched_class;
	else
		p->sched_class = &fair_sched_class;

	p->prio = prio;

	if (running)
		p->sched_class->set_curr_task(rq);
	if (on_rq) {
		enqueue_task(rq, p, 0);

		check_class_changed(rq, p, prev_class, oldprio, running);
	}
	task_rq_unlock(rq, &flags);
}

#endif

void set_user_nice(struct task_struct *p, long nice)
{
	int old_prio, delta, on_rq;
	unsigned long flags;
	struct rq *rq;

	if (TASK_NICE(p) == nice || nice < -20 || nice > 19)
		return;
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	rq = task_rq_lock(p, &flags);
	update_rq_clock(rq);
	/*
	 * The RT priorities are set via sched_setscheduler(), but we still
	 * allow the 'normal' nice value to be set - but as expected
	 * it wont have any effect on scheduling until the task is
	 * SCHED_FIFO/SCHED_RR:
	 */
	if (task_has_rt_policy(p)) {
		p->static_prio = NICE_TO_PRIO(nice);
		goto out_unlock;
	}
	on_rq = p->se.on_rq;
	if (on_rq)
		dequeue_task(rq, p, 0);

	p->static_prio = NICE_TO_PRIO(nice);
	set_load_weight(p);
	old_prio = p->prio;
	p->prio = effective_prio(p);
	delta = p->prio - old_prio;

	if (on_rq) {
		enqueue_task(rq, p, 0);
		/*
		 * If the task increased its priority or is running and
		 * lowered its priority, then reschedule its CPU:
		 */
		if (delta < 0 || (delta > 0 && task_running(rq, p)))
			resched_task(rq->curr);
	}
out_unlock:
	task_rq_unlock(rq, &flags);
}
EXPORT_SYMBOL(set_user_nice);

/*
 * can_nice - check if a task can reduce its nice value
 * @p: task
 * @nice: nice value
 */
int can_nice(const struct task_struct *p, const int nice)
{
	/* convert nice value [19,-20] to rlimit style value [1,40] */
	int nice_rlim = 20 - nice;

	return (nice_rlim <= p->signal->rlim[RLIMIT_NICE].rlim_cur ||
		capable(CAP_SYS_NICE));
}

#ifdef __ARCH_WANT_SYS_NICE

/*
 * sys_nice - change the priority of the current process.
 * @increment: priority increment
 *
 * sys_setpriority is a more generic, but much slower function that
 * does similar things.
 */
SYSCALL_DEFINE1(nice, int, increment)
{
	long nice, retval;

	/*
	 * Setpriority might change our priority at the same moment.
	 * We don't have to worry. Conceptually one call occurs first
	 * and we have a single winner.
	 */
	if (increment < -40)
		increment = -40;
	if (increment > 40)
		increment = 40;

	nice = TASK_NICE(current) + increment;
	if (nice < -20)
		nice = -20;
	if (nice > 19)
		nice = 19;

	if (increment < 0 && !can_nice(current, nice))
		return -EPERM;

	retval = security_task_setnice(current, nice);
	if (retval)
		return retval;

	set_user_nice(current, nice);
	return 0;
}

#endif

/**
 * task_prio - return the priority value of a given task.
 * @p: the task in question.
 *
 * This is the priority value as seen by users in /proc.
 * RT tasks are offset by -200. Normal tasks are centered
 * around 0, value goes from -16 to +15.
 */
int task_prio(const struct task_struct *p)
{
	return p->prio - MAX_RT_PRIO;
}

/**
 * task_nice - return the nice value of a given task.
 * @p: the task in question.
 */
int task_nice(const struct task_struct *p)
{
	return TASK_NICE(p);
}
EXPORT_SYMBOL(task_nice);

/**
 * idle_cpu - is a given cpu idle currently?
 * @cpu: the processor in question.
 */
int idle_cpu(int cpu)
{
	return cpu_curr(cpu) == cpu_rq(cpu)->idle;
}

/**
 * idle_task - return the idle task for a given cpu.
 * @cpu: the processor in question.
 */
struct task_struct *idle_task(int cpu)
{
	return cpu_rq(cpu)->idle;
}

/**
 * find_process_by_pid - find a process with a matching PID value.
 * @pid: the pid in question.
 */
static struct task_struct *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_vpid(pid) : current;
}

/* Actually do priority change: must hold rq lock. */
static void
__setscheduler(struct rq *rq, struct task_struct *p, int policy, int prio)
{
	BUG_ON(p->se.on_rq);

	p->policy = policy;
	switch (p->policy) {
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_IDLE:
		p->sched_class = &fair_sched_class;
		break;
	case SCHED_FIFO:
	case SCHED_RR:
		p->sched_class = &rt_sched_class;
		break;
	}

	p->rt_priority = prio;
	p->normal_prio = normal_prio(p);
	/* we are holding p->pi_lock already */
	p->prio = rt_mutex_getprio(p);
	set_load_weight(p);
}

/*
 * check the target process has a UID that matches the current process's
 */
static bool check_same_owner(struct task_struct *p)
{
	const struct cred *cred = current_cred(), *pcred;
	bool match;

	rcu_read_lock();
	pcred = __task_cred(p);
	match = (cred->euid == pcred->euid ||
		 cred->euid == pcred->uid);
	rcu_read_unlock();
	return match;
}

static int __sched_setscheduler(struct task_struct *p, int policy,
				struct sched_param *param, bool user)
{
	int retval, oldprio, oldpolicy = -1, on_rq, running;
	unsigned long flags;
	const struct sched_class *prev_class = p->sched_class;
	struct rq *rq;

	/* may grab non-irq protected spin_locks */
	BUG_ON(in_interrupt());
recheck:
	/* double check policy once rq lock held */
	if (policy < 0)
		policy = oldpolicy = p->policy;
	else if (policy != SCHED_FIFO && policy != SCHED_RR &&
			policy != SCHED_NORMAL && policy != SCHED_BATCH &&
			policy != SCHED_IDLE)
		return -EINVAL;
	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are
	 * 1..MAX_USER_RT_PRIO-1, valid priority for SCHED_NORMAL,
	 * SCHED_BATCH and SCHED_IDLE is 0.
	 */
	if (param->sched_priority < 0 ||
	    (p->mm && param->sched_priority > MAX_USER_RT_PRIO-1) ||
	    (!p->mm && param->sched_priority > MAX_RT_PRIO-1))
		return -EINVAL;
	if (rt_policy(policy) != (param->sched_priority != 0))
		return -EINVAL;

	/*
	 * Allow unprivileged RT tasks to decrease priority:
	 */
	if (user && !capable(CAP_SYS_NICE)) {
		if (rt_policy(policy)) {
			unsigned long rlim_rtprio;

			if (!lock_task_sighand(p, &flags))
				return -ESRCH;
			rlim_rtprio = p->signal->rlim[RLIMIT_RTPRIO].rlim_cur;
			unlock_task_sighand(p, &flags);

			/* can't set/change the rt policy */
			if (policy != p->policy && !rlim_rtprio)
				return -EPERM;

			/* can't increase priority */
			if (param->sched_priority > p->rt_priority &&
			    param->sched_priority > rlim_rtprio)
				return -EPERM;
		}
		/*
		 * Like positive nice levels, dont allow tasks to
		 * move out of SCHED_IDLE either:
		 */
		if (p->policy == SCHED_IDLE && policy != SCHED_IDLE)
			return -EPERM;

		/* can't change other user's priorities */
		if (!check_same_owner(p))
			return -EPERM;
	}

	if (user) {
#ifdef CONFIG_RT_GROUP_SCHED
		/*
		 * Do not allow realtime tasks into groups that have no runtime
		 * assigned.
		 */
		if (rt_bandwidth_enabled() && rt_policy(policy) &&
				task_group(p)->rt_bandwidth.rt_runtime == 0)
			return -EPERM;
#endif

		retval = security_task_setscheduler(p, policy, param);
		if (retval)
			return retval;
	}

	/*
	 * make sure no PI-waiters arrive (or leave) while we are
	 * changing the priority of the task:
	 */
	spin_lock_irqsave(&p->pi_lock, flags);
	/*
	 * To be able to change p->policy safely, the apropriate
	 * runqueue lock must be held.
	 */
	rq = __task_rq_lock(p);
	/* recheck policy now with rq lock held */
	if (unlikely(oldpolicy != -1 && oldpolicy != p->policy)) {
		policy = oldpolicy = -1;
		__task_rq_unlock(rq);
		spin_unlock_irqrestore(&p->pi_lock, flags);
		goto recheck;
	}
	update_rq_clock(rq);
	on_rq = p->se.on_rq;
	running = task_current(rq, p);
	if (on_rq)
		deactivate_task(rq, p, 0);
	if (running)
		p->sched_class->put_prev_task(rq, p);

	oldprio = p->prio;
	__setscheduler(rq, p, policy, param->sched_priority);

	if (running)
		p->sched_class->set_curr_task(rq);
	if (on_rq) {
		activate_task(rq, p, 0);

		check_class_changed(rq, p, prev_class, oldprio, running);
	}
	__task_rq_unlock(rq);
	spin_unlock_irqrestore(&p->pi_lock, flags);

	rt_mutex_adjust_pi(p);

	return 0;
}

/**
 * sched_setscheduler - change the scheduling policy and/or RT priority of a thread.
 * @p: the task in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 *
 * NOTE that the task may be already dead.
 */
int sched_setscheduler(struct task_struct *p, int policy,
		       struct sched_param *param)
{
	return __sched_setscheduler(p, policy, param, true);
}
EXPORT_SYMBOL_GPL(sched_setscheduler);

/**
 * sched_setscheduler_nocheck - change the scheduling policy and/or RT priority of a thread from kernelspace.
 * @p: the task in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 *
 * Just like sched_setscheduler, only don't bother checking if the
 * current context has permission.  For example, this is needed in
 * stop_machine(): we create temporary high priority worker threads,
 * but our caller might not have that capability.
 */
int sched_setscheduler_nocheck(struct task_struct *p, int policy,
			       struct sched_param *param)
{
	return __sched_setscheduler(p, policy, param, false);
}

static int
do_sched_setscheduler(pid_t pid, int policy, struct sched_param __user *param)
{
	struct sched_param lparam;
	struct task_struct *p;
	int retval;

	if (!param || pid < 0)
		return -EINVAL;
	if (copy_from_user(&lparam, param, sizeof(struct sched_param)))
		return -EFAULT;

	rcu_read_lock();
	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (p != NULL)
		retval = sched_setscheduler(p, policy, &lparam);
	rcu_read_unlock();

	return retval;
}

/**
 * sys_sched_setscheduler - set/change the scheduler policy and RT priority
 * @pid: the pid in question.
 * @policy: new policy.
 * @param: structure containing the new RT priority.
 */
SYSCALL_DEFINE3(sched_setscheduler, pid_t, pid, int, policy,
		struct sched_param __user *, param)
{
	/* negative values for policy are not valid */
	if (policy < 0)
		return -EINVAL;

	return do_sched_setscheduler(pid, policy, param);
}

/**
 * sys_sched_setparam - set/change the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the new RT priority.
 */
SYSCALL_DEFINE2(sched_setparam, pid_t, pid, struct sched_param __user *, param)
{
	return do_sched_setscheduler(pid, -1, param);
}

/**
 * sys_sched_getscheduler - get the policy (scheduling class) of a thread
 * @pid: the pid in question.
 */
SYSCALL_DEFINE1(sched_getscheduler, pid_t, pid)
{
	struct task_struct *p;
	int retval;

	if (pid < 0)
		return -EINVAL;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (p) {
		retval = security_task_getscheduler(p);
		if (!retval)
			retval = p->policy;
	}
	read_unlock(&tasklist_lock);
	return retval;
}

/**
 * sys_sched_getscheduler - get the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the RT priority.
 */
SYSCALL_DEFINE2(sched_getparam, pid_t, pid, struct sched_param __user *, param)
{
	struct sched_param lp;
	struct task_struct *p;
	int retval;

	if (!param || pid < 0)
		return -EINVAL;

	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	lp.sched_priority = p->rt_priority;
	read_unlock(&tasklist_lock);

	/*
	 * This one might sleep, we cannot do it with a spinlock held ...
	 */
	retval = copy_to_user(param, &lp, sizeof(*param)) ? -EFAULT : 0;

	return retval;

out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

long sched_setaffinity(pid_t pid, const struct cpumask *in_mask)
{
	cpumask_var_t cpus_allowed, new_mask;
	struct task_struct *p;
	int retval;

	get_online_cpus();
	read_lock(&tasklist_lock);

	p = find_process_by_pid(pid);
	if (!p) {
		read_unlock(&tasklist_lock);
		put_online_cpus();
		return -ESRCH;
	}

	/*
	 * It is not safe to call set_cpus_allowed with the
	 * tasklist_lock held. We will bump the task_struct's
	 * usage count and then drop tasklist_lock.
	 */
	get_task_struct(p);
	read_unlock(&tasklist_lock);

	if (!alloc_cpumask_var(&cpus_allowed, GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_put_task;
	}
	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_free_cpus_allowed;
	}
	retval = -EPERM;
	if (!check_same_owner(p) && !capable(CAP_SYS_NICE))
		goto out_unlock;

	retval = security_task_setscheduler(p, 0, NULL);
	if (retval)
		goto out_unlock;

	cpuset_cpus_allowed(p, cpus_allowed);
	cpumask_and(new_mask, in_mask, cpus_allowed);
 again:
	retval = set_cpus_allowed_ptr(p, new_mask);

	if (!retval) {
		cpuset_cpus_allowed(p, cpus_allowed);
		if (!cpumask_subset(new_mask, cpus_allowed)) {
			/*
			 * We must have raced with a concurrent cpuset
			 * update. Just reset the cpus_allowed to the
			 * cpuset's cpus_allowed
			 */
			cpumask_copy(new_mask, cpus_allowed);
			goto again;
		}
	}
out_unlock:
	free_cpumask_var(new_mask);
out_free_cpus_allowed:
	free_cpumask_var(cpus_allowed);
out_put_task:
	put_task_struct(p);
	put_online_cpus();
	return retval;
}

static int get_user_cpu_mask(unsigned long __user *user_mask_ptr, unsigned len,
			     struct cpumask *new_mask)
{
	if (len < cpumask_size())
		cpumask_clear(new_mask);
	else if (len > cpumask_size())
		len = cpumask_size();

	return copy_from_user(new_mask, user_mask_ptr, len) ? -EFAULT : 0;
}

/**
 * sys_sched_setaffinity - set the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to the new cpu mask
 */
SYSCALL_DEFINE3(sched_setaffinity, pid_t, pid, unsigned int, len,
		unsigned long __user *, user_mask_ptr)
{
	cpumask_var_t new_mask;
	int retval;

	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL))
		return -ENOMEM;

	retval = get_user_cpu_mask(user_mask_ptr, len, new_mask);
	if (retval == 0)
		retval = sched_setaffinity(pid, new_mask);
	free_cpumask_var(new_mask);
	return retval;
}

long sched_getaffinity(pid_t pid, struct cpumask *mask)
{
	struct task_struct *p;
	int retval;

	get_online_cpus();
	read_lock(&tasklist_lock);

	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	cpumask_and(mask, &p->cpus_allowed, cpu_online_mask);

out_unlock:
	read_unlock(&tasklist_lock);
	put_online_cpus();

	return retval;
}

/**
 * sys_sched_getaffinity - get the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to hold the current cpu mask
 */
SYSCALL_DEFINE3(sched_getaffinity, pid_t, pid, unsigned int, len,
		unsigned long __user *, user_mask_ptr)
{
	int ret;
	cpumask_var_t mask;

	if (len < cpumask_size())
		return -EINVAL;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	ret = sched_getaffinity(pid, mask);
	if (ret == 0) {
		if (copy_to_user(user_mask_ptr, mask, cpumask_size()))
			ret = -EFAULT;
		else
			ret = cpumask_size();
	}
	free_cpumask_var(mask);

	return ret;
}

/**
 * sys_sched_yield - yield the current processor to other threads.
 *
 * This function yields the current CPU to other tasks. If there are no
 * other threads running on this CPU then this function will return.
 */
SYSCALL_DEFINE0(sched_yield)
{
	struct rq *rq = this_rq_lock();

	schedstat_inc(rq, yld_count);
	current->sched_class->yield_task(rq);

	/*
	 * Since we are going to call schedule() anyway, there's
	 * no need to preempt or enable interrupts:
	 */
	__release(rq->lock);
	spin_release(&rq->lock.dep_map, 1, _THIS_IP_);
	_raw_spin_unlock(&rq->lock);
	preempt_enable_no_resched();

	schedule();

	return 0;
}

static void __cond_resched(void)
{
#ifdef CONFIG_DEBUG_SPINLOCK_SLEEP
	__might_sleep(__FILE__, __LINE__);
#endif
	/*
	 * The BKS might be reacquired before we have dropped
	 * PREEMPT_ACTIVE, which could trigger a second
	 * cond_resched() call.
	 */
	do {
		add_preempt_count(PREEMPT_ACTIVE);
		schedule();
		sub_preempt_count(PREEMPT_ACTIVE);
	} while (need_resched());
}

int __sched _cond_resched(void)
{
	if (need_resched() && !(preempt_count() & PREEMPT_ACTIVE) &&
					system_state == SYSTEM_RUNNING) {
		__cond_resched();
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_cond_resched);

/*
 * cond_resched_lock() - if a reschedule is pending, drop the given lock,
 * call schedule, and on return reacquire the lock.
 *
 * This works OK both with and without CONFIG_PREEMPT. We do strange low-level
 * operations here to prevent schedule() from being called twice (once via
 * spin_unlock(), once by hand).
 */
int cond_resched_lock(spinlock_t *lock)
{
	int resched = need_resched() && system_state == SYSTEM_RUNNING;
	int ret = 0;

	if (spin_needbreak(lock) || resched) {
		spin_unlock(lock);
		if (resched && need_resched())
			__cond_resched();
		else
			cpu_relax();
		ret = 1;
		spin_lock(lock);
	}
	return ret;
}
EXPORT_SYMBOL(cond_resched_lock);

int __sched cond_resched_softirq(void)
{
	BUG_ON(!in_softirq());

	if (need_resched() && system_state == SYSTEM_RUNNING) {
		local_bh_enable();
		__cond_resched();
		local_bh_disable();
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(cond_resched_softirq);

/**
 * yield - yield the current processor to other threads.
 *
 * This is a shortcut for kernel-space yielding - it marks the
 * thread runnable and calls sys_sched_yield().
 */
void __sched yield(void)
{
	set_current_state(TASK_RUNNING);
	sys_sched_yield();
}
EXPORT_SYMBOL(yield);

/*
 * This task is about to go to sleep on IO. Increment rq->nr_iowait so
 * that process accounting knows that this is a task in IO wait state.
 *
 * But don't do that if it is a deliberate, throttling IO wait (this task
 * has set its backing_dev_info: the queue against which it should throttle)
 */
void __sched io_schedule(void)
{
	struct rq *rq = &__raw_get_cpu_var(runqueues);

	delayacct_blkio_start();
	atomic_inc(&rq->nr_iowait);
	schedule();
	atomic_dec(&rq->nr_iowait);
	delayacct_blkio_end();
}
EXPORT_SYMBOL(io_schedule);

long __sched io_schedule_timeout(long timeout)
{
	struct rq *rq = &__raw_get_cpu_var(runqueues);
	long ret;

	delayacct_blkio_start();
	atomic_inc(&rq->nr_iowait);
	ret = schedule_timeout(timeout);
	atomic_dec(&rq->nr_iowait);
	delayacct_blkio_end();
	return ret;
}

/**
 * sys_sched_get_priority_max - return maximum RT priority.
 * @policy: scheduling class.
 *
 * this syscall returns the maximum rt_priority that can be used
 * by a given scheduling class.
 */
SYSCALL_DEFINE1(sched_get_priority_max, int, policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = MAX_USER_RT_PRIO-1;
		break;
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_IDLE:
		ret = 0;
		break;
	}
	return ret;
}

/**
 * sys_sched_get_priority_min - return minimum RT priority.
 * @policy: scheduling class.
 *
 * this syscall returns the minimum rt_priority that can be used
 * by a given scheduling class.
 */
SYSCALL_DEFINE1(sched_get_priority_min, int, policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_NORMAL:
	case SCHED_BATCH:
	case SCHED_IDLE:
		ret = 0;
	}
	return ret;
}

/**
 * sys_sched_rr_get_interval - return the default timeslice of a process.
 * @pid: pid of the process.
 * @interval: userspace pointer to the timeslice value.
 *
 * this syscall writes the default timeslice value of a given process
 * into the user-space timespec buffer. A value of '0' means infinity.
 */
SYSCALL_DEFINE2(sched_rr_get_interval, pid_t, pid,
		struct timespec __user *, interval)
{
	struct task_struct *p;
	unsigned int time_slice;
	int retval;
	struct timespec t;

	if (pid < 0)
		return -EINVAL;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	/*
	 * Time slice is 0 for SCHED_FIFO tasks and for SCHED_OTHER
	 * tasks that are on an otherwise idle runqueue:
	 */
	time_slice = 0;
	if (p->policy == SCHED_RR) {
		time_slice = DEF_TIMESLICE;
	} else if (p->policy != SCHED_FIFO) {
		struct sched_entity *se = &p->se;
		unsigned long flags;
		struct rq *rq;

		rq = task_rq_lock(p, &flags);
		if (rq->cfs.load.weight)
			time_slice = NS_TO_JIFFIES(sched_slice(&rq->cfs, se));
		task_rq_unlock(rq, &flags);
	}
	read_unlock(&tasklist_lock);
	jiffies_to_timespec(time_slice, &t);
	retval = copy_to_user(interval, &t, sizeof(t)) ? -EFAULT : 0;
	return retval;

out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

static const char stat_nam[] = TASK_STATE_TO_CHAR_STR;

void sched_show_task(struct task_struct *p)
{
	unsigned long free = 0;
	unsigned state;

	state = p->state ? __ffs(p->state) + 1 : 0;
	printk(KERN_INFO "%-13.13s %c", p->comm,
		state < sizeof(stat_nam) - 1 ? stat_nam[state] : '?');
#if BITS_PER_LONG == 32
	if (state == TASK_RUNNING)
		printk(KERN_CONT " running  ");
	else
		printk(KERN_CONT " %08lx ", thread_saved_pc(p));
#else
	if (state == TASK_RUNNING)
		printk(KERN_CONT "  running task    ");
	else
		printk(KERN_CONT " %016lx ", thread_saved_pc(p));
#endif
#ifdef CONFIG_DEBUG_STACK_USAGE
	free = stack_not_used(p);
#endif
	printk(KERN_CONT "%5lu %5d %6d\n", free,
		task_pid_nr(p), task_pid_nr(p->real_parent));

	show_stack(p, NULL);
}

void show_state_filter(unsigned long state_filter)
{
	struct task_struct *g, *p;

#if BITS_PER_LONG == 32
	printk(KERN_INFO
		"  task                PC stack   pid father\n");
#else
	printk(KERN_INFO
		"  task                        PC stack   pid father\n");
#endif
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		/*
		 * reset the NMI-timeout, listing all files on a slow
		 * console might take alot of time:
		 */
		touch_nmi_watchdog();
		if (!state_filter || (p->state & state_filter))
			sched_show_task(p);
	} while_each_thread(g, p);

	touch_all_softlockup_watchdogs();

#ifdef CONFIG_SCHED_DEBUG
	sysrq_sched_debug_show();
#endif
	read_unlock(&tasklist_lock);
	/*
	 * Only show locks if all tasks are dumped:
	 */
	if (state_filter == -1)
		debug_show_all_locks();
}

void __cpuinit init_idle_bootup_task(struct task_struct *idle)
{
	idle->sched_class = &idle_sched_class;
}

/**
 * init_idle - set up an idle thread for a given CPU
 * @idle: task in question
 * @cpu: cpu the idle task belongs to
 *
 * NOTE: this function does not set the idle thread's NEED_RESCHED
 * flag, to make booting more robust.
 */
void __cpuinit init_idle(struct task_struct *idle, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	spin_lock_irqsave(&rq->lock, flags);

	__sched_fork(idle);
	idle->se.exec_start = sched_clock();

	idle->prio = idle->normal_prio = MAX_PRIO;
	cpumask_copy(&idle->cpus_allowed, cpumask_of(cpu));
	__set_task_cpu(idle, cpu);

	rq->curr = rq->idle = idle;
#if defined(CONFIG_SMP) && defined(__ARCH_WANT_UNLOCKED_CTXSW)
	idle->oncpu = 1;
#endif
	spin_unlock_irqrestore(&rq->lock, flags);

	/* Set the preempt count _outside_ the spinlocks! */
#if defined(CONFIG_PREEMPT)
	task_thread_info(idle)->preempt_count = (idle->lock_depth >= 0);
#else
	task_thread_info(idle)->preempt_count = 0;
#endif
	/*
	 * The idle tasks have their own, simple scheduling class:
	 */
	idle->sched_class = &idle_sched_class;
	ftrace_graph_init_task(idle);
}

/*
 * In a system that switches off the HZ timer nohz_cpu_mask
 * indicates which cpus entered this state. This is used
 * in the rcu update to wait only for active cpus. For system
 * which do not switch off the HZ timer nohz_cpu_mask should
 * always be CPU_BITS_NONE.
 */
cpumask_var_t nohz_cpu_mask;

/*
 * Increase the granularity value when there are more CPUs,
 * because with more CPUs the 'effective latency' as visible
 * to users decreases. But the relationship is not linear,
 * so pick a second-best guess by going with the log2 of the
 * number of CPUs.
 *
 * This idea comes from the SD scheduler of Con Kolivas:
 */
static inline void sched_init_granularity(void)
{
	unsigned int factor = 1 + ilog2(num_online_cpus());
	const unsigned long limit = 200000000;

	sysctl_sched_min_granularity *= factor;
	if (sysctl_sched_min_granularity > limit)
		sysctl_sched_min_granularity = limit;

	sysctl_sched_latency *= factor;
	if (sysctl_sched_latency > limit)
		sysctl_sched_latency = limit;

	sysctl_sched_wakeup_granularity *= factor;

	sysctl_sched_shares_ratelimit *= factor;
}

#ifdef CONFIG_SMP
/*
 * This is how migration works:
 *
 * 1) we queue a struct migration_req structure in the source CPU's
 *    runqueue and wake up that CPU's migration thread.
 * 2) we down() the locked semaphore => thread blocks.
 * 3) migration thread wakes up (implicitly it forces the migrated
 *    thread off the CPU)
 * 4) it gets the migration request and checks whether the migrated
 *    task is still in the wrong runqueue.
 * 5) if it's in the wrong runqueue then the migration thread removes
 *    it and puts it into the right queue.
 * 6) migration thread up()s the semaphore.
 * 7) we wake up and the migration is done.
 */

/*
 * Change a given task's CPU affinity. Migrate the thread to a
 * proper CPU and schedule it away if the CPU it's executing on
 * is removed from the allowed bitmask.
 *
 * NOTE: the caller must have a valid reference to the task, the
 * task must not exit() & deallocate itself prematurely. The
 * call is not atomic; no spinlocks may be held.
 */
int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	struct migration_req req;
	unsigned long flags;
	struct rq *rq;
	int ret = 0;

	rq = task_rq_lock(p, &flags);
	if (!cpumask_intersects(new_mask, cpu_online_mask)) {
		ret = -EINVAL;
		goto out;
	}

	if (unlikely((p->flags & PF_THREAD_BOUND) && p != current &&
		     !cpumask_equal(&p->cpus_allowed, new_mask))) {
		ret = -EINVAL;
		goto out;
	}

	if (p->sched_class->set_cpus_allowed)
		p->sched_class->set_cpus_allowed(p, new_mask);
	else {
		cpumask_copy(&p->cpus_allowed, new_mask);
		p->rt.nr_cpus_allowed = cpumask_weight(new_mask);
	}

	/* Can the task run on the task's current CPU? If so, we're done */
	if (cpumask_test_cpu(task_cpu(p), new_mask))
		goto out;

	if (migrate_task(p, cpumask_any_and(cpu_online_mask, new_mask), &req)) {
		/* Need help from migration thread: drop lock and wait. */
		task_rq_unlock(rq, &flags);
		wake_up_process(rq->migration_thread);
		wait_for_completion(&req.done);
		tlb_migrate_finish(p->mm);
		return 0;
	}
out:
	task_rq_unlock(rq, &flags);

	return ret;
}
EXPORT_SYMBOL_GPL(set_cpus_allowed_ptr);

/*
 * Move (not current) task off this cpu, onto dest cpu. We're doing
 * this because either it can't run here any more (set_cpus_allowed()
 * away from this CPU, or CPU going down), or because we're
 * attempting to rebalance this task on exec (sched_exec).
 *
 * So we race with normal scheduler movements, but that's OK, as long
 * as the task is no longer on this CPU.
 *
 * Returns non-zero if task was successfully migrated.
 */
static int __migrate_task(struct task_struct *p, int src_cpu, int dest_cpu)
{
	struct rq *rq_dest, *rq_src;
	int ret = 0, on_rq;

	if (unlikely(!cpu_active(dest_cpu)))
		return ret;

	rq_src = cpu_rq(src_cpu);
	rq_dest = cpu_rq(dest_cpu);

	double_rq_lock(rq_src, rq_dest);
	/* Already moved. */
	if (task_cpu(p) != src_cpu)
		goto done;
	/* Affinity changed (again). */
	if (!cpumask_test_cpu(dest_cpu, &p->cpus_allowed))
		goto fail;

	on_rq = p->se.on_rq;
	if (on_rq)
		deactivate_task(rq_src, p, 0);

	set_task_cpu(p, dest_cpu);
	if (on_rq) {
		activate_task(rq_dest, p, 0);
		check_preempt_curr(rq_dest, p, 0);
	}
done:
	ret = 1;
fail:
	double_rq_unlock(rq_src, rq_dest);
	return ret;
}

/*
 * migration_thread - this is a highprio system thread that performs
 * thread migration by bumping thread off CPU then 'pushing' onto
 * another runqueue.
 */
static int migration_thread(void *data)
{
	int cpu = (long)data;
	struct rq *rq;

	rq = cpu_rq(cpu);
	BUG_ON(rq->migration_thread != current);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		struct migration_req *req;
		struct list_head *head;

		spin_lock_irq(&rq->lock);

		if (cpu_is_offline(cpu)) {
			spin_unlock_irq(&rq->lock);
			goto wait_to_die;
		}

		if (rq->active_balance) {
			active_load_balance(rq, cpu);
			rq->active_balance = 0;
		}

		head = &rq->migration_queue;

		if (list_empty(head)) {
			spin_unlock_irq(&rq->lock);
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			continue;
		}
		req = list_entry(head->next, struct migration_req, list);
		list_del_init(head->next);

		spin_unlock(&rq->lock);
		__migrate_task(req->task, cpu, req->dest_cpu);
		local_irq_enable();

		complete(&req->done);
	}
	__set_current_state(TASK_RUNNING);
	return 0;

wait_to_die:
	/* Wait for kthread_stop */
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU

static int __migrate_task_irq(struct task_struct *p, int src_cpu, int dest_cpu)
{
	int ret;

	local_irq_disable();
	ret = __migrate_task(p, src_cpu, dest_cpu);
	local_irq_enable();
	return ret;
}

/*
 * Figure out where task on dead CPU should go, use force if necessary.
 */
static void move_task_off_dead_cpu(int dead_cpu, struct task_struct *p)
{
	int dest_cpu;
	const struct cpumask *nodemask = cpumask_of_node(cpu_to_node(dead_cpu));

again:
	/* Look for allowed, online CPU in same node. */
	for_each_cpu_and(dest_cpu, nodemask, cpu_online_mask)
		if (cpumask_test_cpu(dest_cpu, &p->cpus_allowed))
			goto move;

	/* Any allowed, online CPU? */
	dest_cpu = cpumask_any_and(&p->cpus_allowed, cpu_online_mask);
	if (dest_cpu < nr_cpu_ids)
		goto move;

	/* No more Mr. Nice Guy. */
	if (dest_cpu >= nr_cpu_ids) {
		cpuset_cpus_allowed_locked(p, &p->cpus_allowed);
		dest_cpu = cpumask_any_and(cpu_online_mask, &p->cpus_allowed);

		/*
		 * Don't tell them about moving exiting tasks or
		 * kernel threads (both mm NULL), since they never
		 * leave kernel.
		 */
		if (p->mm && printk_ratelimit()) {
			printk(KERN_INFO "process %d (%s) no "
			       "longer affine to cpu%d\n",
			       task_pid_nr(p), p->comm, dead_cpu);
		}
	}

move:
	/* It can have affinity changed while we were choosing. */
	if (unlikely(!__migrate_task_irq(p, dead_cpu, dest_cpu)))
		goto again;
}

/*
 * While a dead CPU has no uninterruptible tasks queued at this point,
 * it might still have a nonzero ->nr_uninterruptible counter, because
 * for performance reasons the counter is not stricly tracking tasks to
 * their home CPUs. So we just add the counter to another CPU's counter,
 * to keep the global sum constant after CPU-down:
 */
static void migrate_nr_uninterruptible(struct rq *rq_src)
{
	struct rq *rq_dest = cpu_rq(cpumask_any(cpu_online_mask));
	unsigned long flags;

	local_irq_save(flags);
	double_rq_lock(rq_src, rq_dest);
	rq_dest->nr_uninterruptible += rq_src->nr_uninterruptible;
	rq_src->nr_uninterruptible = 0;
	double_rq_unlock(rq_src, rq_dest);
	local_irq_restore(flags);
}

/* Run through task list and migrate tasks from the dead cpu. */
static void migrate_live_tasks(int src_cpu)
{
	struct task_struct *p, *t;

	read_lock(&tasklist_lock);

	do_each_thread(t, p) {
		if (p == current)
			continue;

		if (task_cpu(p) == src_cpu)
			move_task_off_dead_cpu(src_cpu, p);
	} while_each_thread(t, p);

	read_unlock(&tasklist_lock);
}

/*
 * Schedules idle task to be the next runnable task on current CPU.
 * It does so by boosting its priority to highest possible.
 * Used by CPU offline code.
 */
void sched_idle_next(void)
{
	int this_cpu = smp_processor_id();
	struct rq *rq = cpu_rq(this_cpu);
	struct task_struct *p = rq->idle;
	unsigned long flags;

	/* cpu has to be offline */
	BUG_ON(cpu_online(this_cpu));

	/*
	 * Strictly not necessary since rest of the CPUs are stopped by now
	 * and interrupts disabled on the current cpu.
	 */
	spin_lock_irqsave(&rq->lock, flags);

	__setscheduler(rq, p, SCHED_FIFO, MAX_RT_PRIO-1);

	update_rq_clock(rq);
	activate_task(rq, p, 0);

	spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Ensures that the idle task is using init_mm right before its cpu goes
 * offline.
 */
void idle_task_exit(void)
{
	struct mm_struct *mm = current->active_mm;

	BUG_ON(cpu_online(smp_processor_id()));

	if (mm != &init_mm)
		switch_mm(mm, &init_mm, current);
	mmdrop(mm);
}

/* called under rq->lock with disabled interrupts */
static void migrate_dead(unsigned int dead_cpu, struct task_struct *p)
{
	struct rq *rq = cpu_rq(dead_cpu);

	/* Must be exiting, otherwise would be on tasklist. */
	BUG_ON(!p->exit_state);

	/* Cannot have done final schedule yet: would have vanished. */
	BUG_ON(p->state == TASK_DEAD);

	get_task_struct(p);

	/*
	 * Drop lock around migration; if someone else moves it,
	 * that's OK. No task can be added to this CPU, so iteration is
	 * fine.
	 */
	spin_unlock_irq(&rq->lock);
	move_task_off_dead_cpu(dead_cpu, p);
	spin_lock_irq(&rq->lock);

	put_task_struct(p);
}

/* release_task() removes task from tasklist, so we won't find dead tasks. */
static void migrate_dead_tasks(unsigned int dead_cpu)
{
	struct rq *rq = cpu_rq(dead_cpu);
	struct task_struct *next;

	for ( ; ; ) {
		if (!rq->nr_running)
			break;
		update_rq_clock(rq);
		next = pick_next_task(rq);
		if (!next)
			break;
		next->sched_class->put_prev_task(rq, next);
		migrate_dead(dead_cpu, next);

	}
}
#endif /* CONFIG_HOTPLUG_CPU */

#if defined(CONFIG_SCHED_DEBUG) && defined(CONFIG_SYSCTL)

static struct ctl_table sd_ctl_dir[] = {
	{
		.procname	= "sched_domain",
		.mode		= 0555,
	},
	{0, },
};

static struct ctl_table sd_ctl_root[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.mode		= 0555,
		.child		= sd_ctl_dir,
	},
	{0, },
};

static struct ctl_table *sd_alloc_ctl_entry(int n)
{
	struct ctl_table *entry =
		kcalloc(n, sizeof(struct ctl_table), GFP_KERNEL);

	return entry;
}

static void sd_free_ctl_entry(struct ctl_table **tablep)
{
	struct ctl_table *entry;

	/*
	 * In the intermediate directories, both the child directory and
	 * procname are dynamically allocated and could fail but the mode
	 * will always be set. In the lowest directory the names are
	 * static strings and all have proc handlers.
	 */
	for (entry = *tablep; entry->mode; entry++) {
		if (entry->child)
			sd_free_ctl_entry(&entry->child);
		if (entry->proc_handler == NULL)
			kfree(entry->procname);
	}

	kfree(*tablep);
	*tablep = NULL;
}

static void
set_table_entry(struct ctl_table *entry,
		const char *procname, void *data, int maxlen,
		mode_t mode, proc_handler *proc_handler)
{
	entry->procname = procname;
	entry->data = data;
	entry->maxlen = maxlen;
	entry->mode = mode;
	entry->proc_handler = proc_handler;
}

static struct ctl_table *
sd_alloc_ctl_domain_table(struct sched_domain *sd)
{
	struct ctl_table *table = sd_alloc_ctl_entry(13);

	if (table == NULL)
		return NULL;

	set_table_entry(&table[0], "min_interval", &sd->min_interval,
		sizeof(long), 0644, proc_doulongvec_minmax);
	set_table_entry(&table[1], "max_interval", &sd->max_interval,
		sizeof(long), 0644, proc_doulongvec_minmax);
	set_table_entry(&table[2], "busy_idx", &sd->busy_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[3], "idle_idx", &sd->idle_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[4], "newidle_idx", &sd->newidle_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[5], "wake_idx", &sd->wake_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[6], "forkexec_idx", &sd->forkexec_idx,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[7], "busy_factor", &sd->busy_factor,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[8], "imbalance_pct", &sd->imbalance_pct,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[9], "cache_nice_tries",
		&sd->cache_nice_tries,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[10], "flags", &sd->flags,
		sizeof(int), 0644, proc_dointvec_minmax);
	set_table_entry(&table[11], "name", sd->name,
		CORENAME_MAX_SIZE, 0444, proc_dostring);
	/* &table[12] is terminator */

	return table;
}

static ctl_table *sd_alloc_ctl_cpu_table(int cpu)
{
	struct ctl_table *entry, *table;
	struct sched_domain *sd;
	int domain_num = 0, i;
	char buf[32];

	for_each_domain(cpu, sd)
		domain_num++;
	entry = table = sd_alloc_ctl_entry(domain_num + 1);
	if (table == NULL)
		return NULL;

	i = 0;
	for_each_domain(cpu, sd) {
		snprintf(buf, 32, "domain%d", i);
		entry->procname = kstrdup(buf, GFP_KERNEL);
		entry->mode = 0555;
		entry->child = sd_alloc_ctl_domain_table(sd);
		entry++;
		i++;
	}
	return table;
}

static struct ctl_table_header *sd_sysctl_header;
static void register_sched_domain_sysctl(void)
{
	int i, cpu_num = num_online_cpus();
	struct ctl_table *entry = sd_alloc_ctl_entry(cpu_num + 1);
	char buf[32];

	WARN_ON(sd_ctl_dir[0].child);
	sd_ctl_dir[0].child = entry;

	if (entry == NULL)
		return;

	for_each_online_cpu(i) {
		snprintf(buf, 32, "cpu%d", i);
		entry->procname = kstrdup(buf, GFP_KERNEL);
		entry->mode = 0555;
		entry->child = sd_alloc_ctl_cpu_table(i);
		entry++;
	}

	WARN_ON(sd_sysctl_header);
	sd_sysctl_header = register_sysctl_table(sd_ctl_root);
}

/* may be called multiple times per register */
static void unregister_sched_domain_sysctl(void)
{
	if (sd_sysctl_header)
		unregister_sysctl_table(sd_sysctl_header);
	sd_sysctl_header = NULL;
	if (sd_ctl_dir[0].child)
		sd_free_ctl_entry(&sd_ctl_dir[0].child);
}
#else
static void register_sched_domain_sysctl(void)
{
}
static void unregister_sched_domain_sysctl(void)
{
}
#endif

static void set_rq_online(struct rq *rq)
{
	if (!rq->online) {
		const struct sched_class *class;

		cpumask_set_cpu(rq->cpu, rq->rd->online);
		rq->online = 1;

		for_each_class(class) {
			if (class->rq_online)
				class->rq_online(rq);
		}
	}
}

static void set_rq_offline(struct rq *rq)
{
	if (rq->online) {
		const struct sched_class *class;

		for_each_class(class) {
			if (class->rq_offline)
				class->rq_offline(rq);
		}

		cpumask_clear_cpu(rq->cpu, rq->rd->online);
		rq->online = 0;
	}
}

/*
 * migration_call - callback that gets triggered when a CPU is added.
 * Here we can start up the necessary migration thread for the new CPU.
 */
static int __cpuinit
migration_call(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	struct task_struct *p;
	int cpu = (long)hcpu;
	unsigned long flags;
	struct rq *rq;

	switch (action) {

	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		p = kthread_create(migration_thread, hcpu, "migration/%d", cpu);
		if (IS_ERR(p))
			return NOTIFY_BAD;
		kthread_bind(p, cpu);
		/* Must be high prio: stop_machine expects to yield to it. */
		rq = task_rq_lock(p, &flags);
		__setscheduler(rq, p, SCHED_FIFO, MAX_RT_PRIO-1);
		task_rq_unlock(rq, &flags);
		cpu_rq(cpu)->migration_thread = p;
		break;

	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		/* Strictly unnecessary, as first user will wake it. */
		wake_up_process(cpu_rq(cpu)->migration_thread);

		/* Update our root-domain */
		rq = cpu_rq(cpu);
		spin_lock_irqsave(&rq->lock, flags);
		if (rq->rd) {
			BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));

			set_rq_online(rq);
		}
		spin_unlock_irqrestore(&rq->lock, flags);
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		if (!cpu_rq(cpu)->migration_thread)
			break;
		/* Unbind it from offline cpu so it can run. Fall thru. */
		kthread_bind(cpu_rq(cpu)->migration_thread,
			     cpumask_any(cpu_online_mask));
		kthread_stop(cpu_rq(cpu)->migration_thread);
		cpu_rq(cpu)->migration_thread = NULL;
		break;

	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		cpuset_lock(); /* around calls to cpuset_cpus_allowed_lock() */
		migrate_live_tasks(cpu);
		rq = cpu_rq(cpu);
		kthread_stop(rq->migration_thread);
		rq->migration_thread = NULL;
		/* Idle task back to normal (off runqueue, low prio) */
		spin_lock_irq(&rq->lock);
		update_rq_clock(rq);
		deactivate_task(rq, rq->idle, 0);
		rq->idle->static_prio = MAX_PRIO;
		__setscheduler(rq, rq->idle, SCHED_NORMAL, 0);
		rq->idle->sched_class = &idle_sched_class;
		migrate_dead_tasks(cpu);
		spin_unlock_irq(&rq->lock);
		cpuset_unlock();
		migrate_nr_uninterruptible(rq);
		BUG_ON(rq->nr_running != 0);

		/*
		 * No need to migrate the tasks: it was best-effort if
		 * they didn't take sched_hotcpu_mutex. Just wake up
		 * the requestors.
		 */
		spin_lock_irq(&rq->lock);
		while (!list_empty(&rq->migration_queue)) {
			struct migration_req *req;

			req = list_entry(rq->migration_queue.next,
					 struct migration_req, list);
			list_del_init(&req->list);
			spin_unlock_irq(&rq->lock);
			complete(&req->done);
			spin_lock_irq(&rq->lock);
		}
		spin_unlock_irq(&rq->lock);
		break;

	case CPU_DYING:
	case CPU_DYING_FROZEN:
		/* Update our root-domain */
		rq = cpu_rq(cpu);
		spin_lock_irqsave(&rq->lock, flags);
		if (rq->rd) {
			BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));
			set_rq_offline(rq);
		}
		spin_unlock_irqrestore(&rq->lock, flags);
		break;
#endif
	}
	return NOTIFY_OK;
}

/* Register at highest priority so that task migration (migrate_all_tasks)
 * happens before everything else.
 */
static struct notifier_block __cpuinitdata migration_notifier = {
	.notifier_call = migration_call,
	.priority = 10
};

static int __init migration_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	/* Start one for the boot CPU: */
	err = migration_call(&migration_notifier, CPU_UP_PREPARE, cpu);
	BUG_ON(err == NOTIFY_BAD);
	migration_call(&migration_notifier, CPU_ONLINE, cpu);
	register_cpu_notifier(&migration_notifier);

	return err;
}
early_initcall(migration_init);
#endif

#ifdef CONFIG_SMP

#ifdef CONFIG_SCHED_DEBUG

static int sched_domain_debug_one(struct sched_domain *sd, int cpu, int level,
				  struct cpumask *groupmask)
{
	struct sched_group *group = sd->groups;
	char str[256];

	cpulist_scnprintf(str, sizeof(str), sched_domain_span(sd));
	cpumask_clear(groupmask);

	printk(KERN_DEBUG "%*s domain %d: ", level, "", level);

	if (!(sd->flags & SD_LOAD_BALANCE)) {
		printk("does not load-balance\n");
		if (sd->parent)
			printk(KERN_ERR "ERROR: !SD_LOAD_BALANCE domain"
					" has parent");
		return -1;
	}

	printk(KERN_CONT "span %s level %s\n", str, sd->name);

	if (!cpumask_test_cpu(cpu, sched_domain_span(sd))) {
		printk(KERN_ERR "ERROR: domain->span does not contain "
				"CPU%d\n", cpu);
	}
	if (!cpumask_test_cpu(cpu, sched_group_cpus(group))) {
		printk(KERN_ERR "ERROR: domain->groups does not contain"
				" CPU%d\n", cpu);
	}

	printk(KERN_DEBUG "%*s groups:", level + 1, "");
	do {
		if (!group) {
			printk("\n");
			printk(KERN_ERR "ERROR: group is NULL\n");
			break;
		}

		if (!group->__cpu_power) {
			printk(KERN_CONT "\n");
			printk(KERN_ERR "ERROR: domain->cpu_power not "
					"set\n");
			break;
		}

		if (!cpumask_weight(sched_group_cpus(group))) {
			printk(KERN_CONT "\n");
			printk(KERN_ERR "ERROR: empty group\n");
			break;
		}

		if (cpumask_intersects(groupmask, sched_group_cpus(group))) {
			printk(KERN_CONT "\n");
			printk(KERN_ERR "ERROR: repeated CPUs\n");
			break;
		}

		cpumask_or(groupmask, groupmask, sched_group_cpus(group));

		cpulist_scnprintf(str, sizeof(str), sched_group_cpus(group));
		printk(KERN_CONT " %s (__cpu_power = %d)", str,
						group->__cpu_power);

		group = group->next;
	} while (group != sd->groups);
	printk(KERN_CONT "\n");

	if (!cpumask_equal(sched_domain_span(sd), groupmask))
		printk(KERN_ERR "ERROR: groups don't span domain->span\n");

	if (sd->parent &&
	    !cpumask_subset(groupmask, sched_domain_span(sd->parent)))
		printk(KERN_ERR "ERROR: parent span is not a superset "
			"of domain->span\n");
	return 0;
}

static void sched_domain_debug(struct sched_domain *sd, int cpu)
{
	cpumask_var_t groupmask;
	int level = 0;

	if (!sd) {
		printk(KERN_DEBUG "CPU%d attaching NULL sched-domain.\n", cpu);
		return;
	}

	printk(KERN_DEBUG "CPU%d attaching sched-domain:\n", cpu);

	if (!alloc_cpumask_var(&groupmask, GFP_KERNEL)) {
		printk(KERN_DEBUG "Cannot load-balance (out of memory)\n");
		return;
	}

	for (;;) {
		if (sched_domain_debug_one(sd, cpu, level, groupmask))
			break;
		level++;
		sd = sd->parent;
		if (!sd)
			break;
	}
	free_cpumask_var(groupmask);
}
#else /* !CONFIG_SCHED_DEBUG */
# define sched_domain_debug(sd, cpu) do { } while (0)
#endif /* CONFIG_SCHED_DEBUG */

static int sd_degenerate(struct sched_domain *sd)
{
	if (cpumask_weight(sched_domain_span(sd)) == 1)
		return 1;

	/* Following flags need at least 2 groups */
	if (sd->flags & (SD_LOAD_BALANCE |
			 SD_BALANCE_NEWIDLE |
			 SD_BALANCE_FORK |
			 SD_BALANCE_EXEC |
			 SD_SHARE_CPUPOWER |
			 SD_SHARE_PKG_RESOURCES)) {
		if (sd->groups != sd->groups->next)
			return 0;
	}

	/* Following flags don't use groups */
	if (sd->flags & (SD_WAKE_IDLE |
			 SD_WAKE_AFFINE |
			 SD_WAKE_BALANCE))
		return 0;

	return 1;
}

static int
sd_parent_degenerate(struct sched_domain *sd, struct sched_domain *parent)
{
	unsigned long cflags = sd->flags, pflags = parent->flags;

	if (sd_degenerate(parent))
		return 1;

	if (!cpumask_equal(sched_domain_span(sd), sched_domain_span(parent)))
		return 0;

	/* Does parent contain flags not in child? */
	/* WAKE_BALANCE is a subset of WAKE_AFFINE */
	if (cflags & SD_WAKE_AFFINE)
		pflags &= ~SD_WAKE_BALANCE;
	/* Flags needing groups don't count if only 1 group in parent */
	if (parent->groups == parent->groups->next) {
		pflags &= ~(SD_LOAD_BALANCE |
				SD_BALANCE_NEWIDLE |
				SD_BALANCE_FORK |
				SD_BALANCE_EXEC |
				SD_SHARE_CPUPOWER |
				SD_SHARE_PKG_RESOURCES);
		if (nr_node_ids == 1)
			pflags &= ~SD_SERIALIZE;
	}
	if (~cflags & pflags)
		return 0;

	return 1;
}

static void free_rootdomain(struct root_domain *rd)
{
	cpupri_cleanup(&rd->cpupri);

	free_cpumask_var(rd->rto_mask);
	free_cpumask_var(rd->online);
	free_cpumask_var(rd->span);
	kfree(rd);
}

static void rq_attach_root(struct rq *rq, struct root_domain *rd)
{
	struct root_domain *old_rd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&rq->lock, flags);

	if (rq->rd) {
		old_rd = rq->rd;

		if (cpumask_test_cpu(rq->cpu, old_rd->online))
			set_rq_offline(rq);

		cpumask_clear_cpu(rq->cpu, old_rd->span);

		/*
		 * If we dont want to free the old_rt yet then
		 * set old_rd to NULL to skip the freeing later
		 * in this function:
		 */
		if (!atomic_dec_and_test(&old_rd->refcount))
			old_rd = NULL;
	}

	atomic_inc(&rd->refcount);
	rq->rd = rd;

	cpumask_set_cpu(rq->cpu, rd->span);
	if (cpumask_test_cpu(rq->cpu, cpu_online_mask))
		set_rq_online(rq);

	spin_unlock_irqrestore(&rq->lock, flags);

	if (old_rd)
		free_rootdomain(old_rd);
}

static int __init_refok init_rootdomain(struct root_domain *rd, bool bootmem)
{
	memset(rd, 0, sizeof(*rd));

	if (bootmem) {
		alloc_bootmem_cpumask_var(&def_root_domain.span);
		alloc_bootmem_cpumask_var(&def_root_domain.online);
		alloc_bootmem_cpumask_var(&def_root_domain.rto_mask);
		cpupri_init(&rd->cpupri, true);
		return 0;
	}

	if (!alloc_cpumask_var(&rd->span, GFP_KERNEL))
		goto out;
	if (!alloc_cpumask_var(&rd->online, GFP_KERNEL))
		goto free_span;
	if (!alloc_cpumask_var(&rd->rto_mask, GFP_KERNEL))
		goto free_online;

	if (cpupri_init(&rd->cpupri, false) != 0)
		goto free_rto_mask;
	return 0;

free_rto_mask:
	free_cpumask_var(rd->rto_mask);
free_online:
	free_cpumask_var(rd->online);
free_span:
	free_cpumask_var(rd->span);
out:
	return -ENOMEM;
}

static void init_defrootdomain(void)
{
	init_rootdomain(&def_root_domain, true);

	atomic_set(&def_root_domain.refcount, 1);
}

static struct root_domain *alloc_rootdomain(void)
{
	struct root_domain *rd;

	rd = kmalloc(sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return NULL;

	if (init_rootdomain(rd, false) != 0) {
		kfree(rd);
		return NULL;
	}

	return rd;
}

/*
 * Attach the domain 'sd' to 'cpu' as its base domain. Callers must
 * hold the hotplug lock.
 */
static void
cpu_attach_domain(struct sched_domain *sd, struct root_domain *rd, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct sched_domain *tmp;

	/* Remove the sched domains which do not contribute to scheduling. */
	for (tmp = sd; tmp; ) {
		struct sched_domain *parent = tmp->parent;
		if (!parent)
			break;

		if (sd_parent_degenerate(tmp, parent)) {
			tmp->parent = parent->parent;
			if (parent->parent)
				parent->parent->child = tmp;
		} else
			tmp = tmp->parent;
	}

	if (sd && sd_degenerate(sd)) {
		sd = sd->parent;
		if (sd)
			sd->child = NULL;
	}

	sched_domain_debug(sd, cpu);

	rq_attach_root(rq, rd);
	rcu_assign_pointer(rq->sd, sd);
}

/* cpus with isolated domains */
static cpumask_var_t cpu_isolated_map;

/* Setup the mask of cpus configured for isolated domains */
static int __init isolated_cpu_setup(char *str)
{
	cpulist_parse(str, cpu_isolated_map);
	return 1;
}

__setup("isolcpus=", isolated_cpu_setup);

/*
 * init_sched_build_groups takes the cpumask we wish to span, and a pointer
 * to a function which identifies what group(along with sched group) a CPU
 * belongs to. The return value of group_fn must be a >= 0 and < nr_cpu_ids
 * (due to the fact that we keep track of groups covered with a struct cpumask).
 *
 * init_sched_build_groups will build a circular linked list of the groups
 * covered by the given span, and will set each group's ->cpumask correctly,
 * and ->cpu_power to 0.
 */
static void
init_sched_build_groups(const struct cpumask *span,
			const struct cpumask *cpu_map,
			int (*group_fn)(int cpu, const struct cpumask *cpu_map,
					struct sched_group **sg,
					struct cpumask *tmpmask),
			struct cpumask *covered, struct cpumask *tmpmask)
{
	struct sched_group *first = NULL, *last = NULL;
	int i;

	cpumask_clear(covered);

	for_each_cpu(i, span) {
		struct sched_group *sg;
		int group = group_fn(i, cpu_map, &sg, tmpmask);
		int j;

		if (cpumask_test_cpu(i, covered))
			continue;

		cpumask_clear(sched_group_cpus(sg));
		sg->__cpu_power = 0;

		for_each_cpu(j, span) {
			if (group_fn(j, cpu_map, NULL, tmpmask) != group)
				continue;

			cpumask_set_cpu(j, covered);
			cpumask_set_cpu(j, sched_group_cpus(sg));
		}
		if (!first)
			first = sg;
		if (last)
			last->next = sg;
		last = sg;
	}
	last->next = first;
}

#define SD_NODES_PER_DOMAIN 16

#ifdef CONFIG_NUMA

/**
 * find_next_best_node - find the next node to include in a sched_domain
 * @node: node whose sched_domain we're building
 * @used_nodes: nodes already in the sched_domain
 *
 * Find the next node to include in a given scheduling domain. Simply
 * finds the closest node not already in the @used_nodes map.
 *
 * Should use nodemask_t.
 */
static int find_next_best_node(int node, nodemask_t *used_nodes)
{
	int i, n, val, min_val, best_node = 0;

	min_val = INT_MAX;

	for (i = 0; i < nr_node_ids; i++) {
		/* Start at @node */
		n = (node + i) % nr_node_ids;

		if (!nr_cpus_node(n))
			continue;

		/* Skip already used nodes */
		if (node_isset(n, *used_nodes))
			continue;

		/* Simple min distance search */
		val = node_distance(node, n);

		if (val < min_val) {
			min_val = val;
			best_node = n;
		}
	}

	node_set(best_node, *used_nodes);
	return best_node;
}

/**
 * sched_domain_node_span - get a cpumask for a node's sched_domain
 * @node: node whose cpumask we're constructing
 * @span: resulting cpumask
 *
 * Given a node, construct a good cpumask for its sched_domain to span. It
 * should be one that prevents unnecessary balancing, but also spreads tasks
 * out optimally.
 */
static void sched_domain_node_span(int node, struct cpumask *span)
{
	nodemask_t used_nodes;
	int i;

	cpumask_clear(span);
	nodes_clear(used_nodes);

	cpumask_or(span, span, cpumask_of_node(node));
	node_set(node, used_nodes);

	for (i = 1; i < SD_NODES_PER_DOMAIN; i++) {
		int next_node = find_next_best_node(node, &used_nodes);

		cpumask_or(span, span, cpumask_of_node(next_node));
	}
}
#endif /* CONFIG_NUMA */

int sched_smt_power_savings = 0, sched_mc_power_savings = 0;

/*
 * The cpus mask in sched_group and sched_domain hangs off the end.
 * FIXME: use cpumask_var_t or dynamic percpu alloc to avoid wasting space
 * for nr_cpu_ids < CONFIG_NR_CPUS.
 */
struct static_sched_group {
	struct sched_group sg;
	DECLARE_BITMAP(cpus, CONFIG_NR_CPUS);
};

struct static_sched_domain {
	struct sched_domain sd;
	DECLARE_BITMAP(span, CONFIG_NR_CPUS);
};

/*
 * SMT sched-domains:
 */
#ifdef CONFIG_SCHED_SMT
static DEFINE_PER_CPU(struct static_sched_domain, cpu_domains);
static DEFINE_PER_CPU(struct static_sched_group, sched_group_cpus);

static int
cpu_to_cpu_group(int cpu, const struct cpumask *cpu_map,
		 struct sched_group **sg, struct cpumask *unused)
{
	if (sg)
		*sg = &per_cpu(sched_group_cpus, cpu).sg;
	return cpu;
}
#endif /* CONFIG_SCHED_SMT */

/*
 * multi-core sched-domains:
 */
#ifdef CONFIG_SCHED_MC
static DEFINE_PER_CPU(struct static_sched_domain, core_domains);
static DEFINE_PER_CPU(struct static_sched_group, sched_group_core);
#endif /* CONFIG_SCHED_MC */

#if defined(CONFIG_SCHED_MC) && defined(CONFIG_SCHED_SMT)
static int
cpu_to_core_group(int cpu, const struct cpumask *cpu_map,
		  struct sched_group **sg, struct cpumask *mask)
{
	int group;

	cpumask_and(mask, topology_thread_cpumask(cpu), cpu_map);
	group = cpumask_first(mask);
	if (sg)
		*sg = &per_cpu(sched_group_core, group).sg;
	return group;
}
#elif defined(CONFIG_SCHED_MC)
static int
cpu_to_core_group(int cpu, const struct cpumask *cpu_map,
		  struct sched_group **sg, struct cpumask *unused)
{
	if (sg)
		*sg = &per_cpu(sched_group_core, cpu).sg;
	return cpu;
}
#endif

static DEFINE_PER_CPU(struct static_sched_domain, phys_domains);
static DEFINE_PER_CPU(struct static_sched_group, sched_group_phys);

static int
cpu_to_phys_group(int cpu, const struct cpumask *cpu_map,
		  struct sched_group **sg, struct cpumask *mask)
{
	int group;
#ifdef CONFIG_SCHED_MC
	cpumask_and(mask, cpu_coregroup_mask(cpu), cpu_map);
	group = cpumask_first(mask);
#elif defined(CONFIG_SCHED_SMT)
	cpumask_and(mask, topology_thread_cpumask(cpu), cpu_map);
	group = cpumask_first(mask);
#else
	group = cpu;
#endif
	if (sg)
		*sg = &per_cpu(sched_group_phys, group).sg;
	return group;
}

#ifdef CONFIG_NUMA
/*
 * The init_sched_build_groups can't handle what we want to do with node
 * groups, so roll our own. Now each node has its own list of groups which
 * gets dynamically allocated.
 */
static DEFINE_PER_CPU(struct static_sched_domain, node_domains);
static struct sched_group ***sched_group_nodes_bycpu;

static DEFINE_PER_CPU(struct static_sched_domain, allnodes_domains);
static DEFINE_PER_CPU(struct static_sched_group, sched_group_allnodes);

static int cpu_to_allnodes_group(int cpu, const struct cpumask *cpu_map,
				 struct sched_group **sg,
				 struct cpumask *nodemask)
{
	int group;

	cpumask_and(nodemask, cpumask_of_node(cpu_to_node(cpu)), cpu_map);
	group = cpumask_first(nodemask);

	if (sg)
		*sg = &per_cpu(sched_group_allnodes, group).sg;
	return group;
}

static void init_numa_sched_groups_power(struct sched_group *group_head)
{
	struct sched_group *sg = group_head;
	int j;

	if (!sg)
		return;
	do {
		for_each_cpu(j, sched_group_cpus(sg)) {
			struct sched_domain *sd;

			sd = &per_cpu(phys_domains, j).sd;
			if (j != cpumask_first(sched_group_cpus(sd->groups))) {
				/*
				 * Only add "power" once for each
				 * physical package.
				 */
				continue;
			}

			sg_inc_cpu_power(sg, sd->groups->__cpu_power);
		}
		sg = sg->next;
	} while (sg != group_head);
}
#endif /* CONFIG_NUMA */

#ifdef CONFIG_NUMA
/* Free memory allocated for various sched_group structures */
static void free_sched_groups(const struct cpumask *cpu_map,
			      struct cpumask *nodemask)
{
	int cpu, i;

	for_each_cpu(cpu, cpu_map) {
		struct sched_group **sched_group_nodes
			= sched_group_nodes_bycpu[cpu];

		if (!sched_group_nodes)
			continue;

		for (i = 0; i < nr_node_ids; i++) {
			struct sched_group *oldsg, *sg = sched_group_nodes[i];

			cpumask_and(nodemask, cpumask_of_node(i), cpu_map);
			if (cpumask_empty(nodemask))
				continue;

			if (sg == NULL)
				continue;
			sg = sg->next;
next_sg:
			oldsg = sg;
			sg = sg->next;
			kfree(oldsg);
			if (oldsg != sched_group_nodes[i])
				goto next_sg;
		}
		kfree(sched_group_nodes);
		sched_group_nodes_bycpu[cpu] = NULL;
	}
}
#else /* !CONFIG_NUMA */
static void free_sched_groups(const struct cpumask *cpu_map,
			      struct cpumask *nodemask)
{
}
#endif /* CONFIG_NUMA */

/*
 * Initialize sched groups cpu_power.
 *
 * cpu_power indicates the capacity of sched group, which is used while
 * distributing the load between different sched groups in a sched domain.
 * Typically cpu_power for all the groups in a sched domain will be same unless
 * there are asymmetries in the topology. If there are asymmetries, group
 * having more cpu_power will pickup more load compared to the group having
 * less cpu_power.
 *
 * cpu_power will be a multiple of SCHED_LOAD_SCALE. This multiple represents
 * the maximum number of tasks a group can handle in the presence of other idle
 * or lightly loaded groups in the same sched domain.
 */
static void init_sched_groups_power(int cpu, struct sched_domain *sd)
{
	struct sched_domain *child;
	struct sched_group *group;

	WARN_ON(!sd || !sd->groups);

	if (cpu != cpumask_first(sched_group_cpus(sd->groups)))
		return;

	child = sd->child;

	sd->groups->__cpu_power = 0;

	/*
	 * For perf policy, if the groups in child domain share resources
	 * (for example cores sharing some portions of the cache hierarchy
	 * or SMT), then set this domain groups cpu_power such that each group
	 * can handle only one task, when there are other idle groups in the
	 * same sched domain.
	 */
	if (!child || (!(sd->flags & SD_POWERSAVINGS_BALANCE) &&
		       (child->flags &
			(SD_SHARE_CPUPOWER | SD_SHARE_PKG_RESOURCES)))) {
		sg_inc_cpu_power(sd->groups, SCHED_LOAD_SCALE);
		return;
	}

	/*
	 * add cpu_power of each child group to this groups cpu_power
	 */
	group = child->groups;
	do {
		sg_inc_cpu_power(sd->groups, group->__cpu_power);
		group = group->next;
	} while (group != child->groups);
}

/*
 * Initializers for schedule domains
 * Non-inlined to reduce accumulated stack pressure in build_sched_domains()
 */

#ifdef CONFIG_SCHED_DEBUG
# define SD_INIT_NAME(sd, type)		sd->name = #type
#else
# define SD_INIT_NAME(sd, type)		do { } while (0)
#endif

#define	SD_INIT(sd, type)	sd_init_##type(sd)

#define SD_INIT_FUNC(type)	\
static noinline void sd_init_##type(struct sched_domain *sd)	\
{								\
	memset(sd, 0, sizeof(*sd));				\
	*sd = SD_##type##_INIT;					\
	sd->level = SD_LV_##type;				\
	SD_INIT_NAME(sd, type);					\
}

SD_INIT_FUNC(CPU)
#ifdef CONFIG_NUMA
 SD_INIT_FUNC(ALLNODES)
 SD_INIT_FUNC(NODE)
#endif
#ifdef CONFIG_SCHED_SMT
 SD_INIT_FUNC(SIBLING)
#endif
#ifdef CONFIG_SCHED_MC
 SD_INIT_FUNC(MC)
#endif

static int default_relax_domain_level = -1;

static int __init setup_relax_domain_level(char *str)
{
	unsigned long val;

	val = simple_strtoul(str, NULL, 0);
	if (val < SD_LV_MAX)
		default_relax_domain_level = val;

	return 1;
}
__setup("relax_domain_level=", setup_relax_domain_level);

static void set_domain_attribute(struct sched_domain *sd,
				 struct sched_domain_attr *attr)
{
	int request;

	if (!attr || attr->relax_domain_level < 0) {
		if (default_relax_domain_level < 0)
			return;
		else
			request = default_relax_domain_level;
	} else
		request = attr->relax_domain_level;
	if (request < sd->level) {
		/* turn off idle balance on this domain */
		sd->flags &= ~(SD_WAKE_IDLE|SD_BALANCE_NEWIDLE);
	} else {
		/* turn on idle balance on this domain */
		sd->flags |= (SD_WAKE_IDLE_FAR|SD_BALANCE_NEWIDLE);
	}
}

/*
 * Build sched domains for a given set of cpus and attach the sched domains
 * to the individual cpus
 */
static int __build_sched_domains(const struct cpumask *cpu_map,
				 struct sched_domain_attr *attr)
{
	int i, err = -ENOMEM;
	struct root_domain *rd;
	cpumask_var_t nodemask, this_sibling_map, this_core_map, send_covered,
		tmpmask;
#ifdef CONFIG_NUMA
	cpumask_var_t domainspan, covered, notcovered;
	struct sched_group **sched_group_nodes = NULL;
	int sd_allnodes = 0;

	if (!alloc_cpumask_var(&domainspan, GFP_KERNEL))
		goto out;
	if (!alloc_cpumask_var(&covered, GFP_KERNEL))
		goto free_domainspan;
	if (!alloc_cpumask_var(&notcovered, GFP_KERNEL))
		goto free_covered;
#endif

	if (!alloc_cpumask_var(&nodemask, GFP_KERNEL))
		goto free_notcovered;
	if (!alloc_cpumask_var(&this_sibling_map, GFP_KERNEL))
		goto free_nodemask;
	if (!alloc_cpumask_var(&this_core_map, GFP_KERNEL))
		goto free_this_sibling_map;
	if (!alloc_cpumask_var(&send_covered, GFP_KERNEL))
		goto free_this_core_map;
	if (!alloc_cpumask_var(&tmpmask, GFP_KERNEL))
		goto free_send_covered;

#ifdef CONFIG_NUMA
	/*
	 * Allocate the per-node list of sched groups
	 */
	sched_group_nodes = kcalloc(nr_node_ids, sizeof(struct sched_group *),
				    GFP_KERNEL);
	if (!sched_group_nodes) {
		printk(KERN_WARNING "Can not alloc sched group node list\n");
		goto free_tmpmask;
	}
#endif

	rd = alloc_rootdomain();
	if (!rd) {
		printk(KERN_WARNING "Cannot alloc root domain\n");
		goto free_sched_groups;
	}

#ifdef CONFIG_NUMA
	sched_group_nodes_bycpu[cpumask_first(cpu_map)] = sched_group_nodes;
#endif

	/*
	 * Set up domains for cpus specified by the cpu_map.
	 */
	for_each_cpu(i, cpu_map) {
		struct sched_domain *sd = NULL, *p;

		cpumask_and(nodemask, cpumask_of_node(cpu_to_node(i)), cpu_map);

#ifdef CONFIG_NUMA
		if (cpumask_weight(cpu_map) >
				SD_NODES_PER_DOMAIN*cpumask_weight(nodemask)) {
			sd = &per_cpu(allnodes_domains, i).sd;
			SD_INIT(sd, ALLNODES);
			set_domain_attribute(sd, attr);
			cpumask_copy(sched_domain_span(sd), cpu_map);
			cpu_to_allnodes_group(i, cpu_map, &sd->groups, tmpmask);
			p = sd;
			sd_allnodes = 1;
		} else
			p = NULL;

		sd = &per_cpu(node_domains, i).sd;
		SD_INIT(sd, NODE);
		set_domain_attribute(sd, attr);
		sched_domain_node_span(cpu_to_node(i), sched_domain_span(sd));
		sd->parent = p;
		if (p)
			p->child = sd;
		cpumask_and(sched_domain_span(sd),
			    sched_domain_span(sd), cpu_map);
#endif

		p = sd;
		sd = &per_cpu(phys_domains, i).sd;
		SD_INIT(sd, CPU);
		set_domain_attribute(sd, attr);
		cpumask_copy(sched_domain_span(sd), nodemask);
		sd->parent = p;
		if (p)
			p->child = sd;
		cpu_to_phys_group(i, cpu_map, &sd->groups, tmpmask);

#ifdef CONFIG_SCHED_MC
		p = sd;
		sd = &per_cpu(core_domains, i).sd;
		SD_INIT(sd, MC);
		set_domain_attribute(sd, attr);
		cpumask_and(sched_domain_span(sd), cpu_map,
						   cpu_coregroup_mask(i));
		sd->parent = p;
		p->child = sd;
		cpu_to_core_group(i, cpu_map, &sd->groups, tmpmask);
#endif

#ifdef CONFIG_SCHED_SMT
		p = sd;
		sd = &per_cpu(cpu_domains, i).sd;
		SD_INIT(sd, SIBLING);
		set_domain_attribute(sd, attr);
		cpumask_and(sched_domain_span(sd),
			    topology_thread_cpumask(i), cpu_map);
		sd->parent = p;
		p->child = sd;
		cpu_to_cpu_group(i, cpu_map, &sd->groups, tmpmask);
#endif
	}

#ifdef CONFIG_SCHED_SMT
	/* Set up CPU (sibling) groups */
	for_each_cpu(i, cpu_map) {
		cpumask_and(this_sibling_map,
			    topology_thread_cpumask(i), cpu_map);
		if (i != cpumask_first(this_sibling_map))
			continue;

		init_sched_build_groups(this_sibling_map, cpu_map,
					&cpu_to_cpu_group,
					send_covered, tmpmask);
	}
#endif

#ifdef CONFIG_SCHED_MC
	/* Set up multi-core groups */
	for_each_cpu(i, cpu_map) {
		cpumask_and(this_core_map, cpu_coregroup_mask(i), cpu_map);
		if (i != cpumask_first(this_core_map))
			continue;

		init_sched_build_groups(this_core_map, cpu_map,
					&cpu_to_core_group,
					send_covered, tmpmask);
	}
#endif

	/* Set up physical groups */
	for (i = 0; i < nr_node_ids; i++) {
		cpumask_and(nodemask, cpumask_of_node(i), cpu_map);
		if (cpumask_empty(nodemask))
			continue;

		init_sched_build_groups(nodemask, cpu_map,
					&cpu_to_phys_group,
					send_covered, tmpmask);
	}

#ifdef CONFIG_NUMA
	/* Set up node groups */
	if (sd_allnodes) {
		init_sched_build_groups(cpu_map, cpu_map,
					&cpu_to_allnodes_group,
					send_covered, tmpmask);
	}

	for (i = 0; i < nr_node_ids; i++) {
		/* Set up node groups */
		struct sched_group *sg, *prev;
		int j;

		cpumask_clear(covered);
		cpumask_and(nodemask, cpumask_of_node(i), cpu_map);
		if (cpumask_empty(nodemask)) {
			sched_group_nodes[i] = NULL;
			continue;
		}

		sched_domain_node_span(i, domainspan);
		cpumask_and(domainspan, domainspan, cpu_map);

		sg = kmalloc_node(sizeof(struct sched_group) + cpumask_size(),
				  GFP_KERNEL, i);
		if (!sg) {
			printk(KERN_WARNING "Can not alloc domain group for "
				"node %d\n", i);
			goto error;
		}
		sched_group_nodes[i] = sg;
		for_each_cpu(j, nodemask) {
			struct sched_domain *sd;

			sd = &per_cpu(node_domains, j).sd;
			sd->groups = sg;
		}
		sg->__cpu_power = 0;
		cpumask_copy(sched_group_cpus(sg), nodemask);
		sg->next = sg;
		cpumask_or(covered, covered, nodemask);
		prev = sg;

		for (j = 0; j < nr_node_ids; j++) {
			int n = (i + j) % nr_node_ids;

			cpumask_complement(notcovered, covered);
			cpumask_and(tmpmask, notcovered, cpu_map);
			cpumask_and(tmpmask, tmpmask, domainspan);
			if (cpumask_empty(tmpmask))
				break;

			cpumask_and(tmpmask, tmpmask, cpumask_of_node(n));
			if (cpumask_empty(tmpmask))
				continue;

			sg = kmalloc_node(sizeof(struct sched_group) +
					  cpumask_size(),
					  GFP_KERNEL, i);
			if (!sg) {
				printk(KERN_WARNING
				"Can not alloc domain group for node %d\n", j);
				goto error;
			}
			sg->__cpu_power = 0;
			cpumask_copy(sched_group_cpus(sg), tmpmask);
			sg->next = prev->next;
			cpumask_or(covered, covered, tmpmask);
			prev->next = sg;
			prev = sg;
		}
	}
#endif

	/* Calculate CPU power for physical packages and nodes */
#ifdef CONFIG_SCHED_SMT
	for_each_cpu(i, cpu_map) {
		struct sched_domain *sd = &per_cpu(cpu_domains, i).sd;

		init_sched_groups_power(i, sd);
	}
#endif
#ifdef CONFIG_SCHED_MC
	for_each_cpu(i, cpu_map) {
		struct sched_domain *sd = &per_cpu(core_domains, i).sd;

		init_sched_groups_power(i, sd);
	}
#endif

	for_each_cpu(i, cpu_map) {
		struct sched_domain *sd = &per_cpu(phys_domains, i).sd;

		init_sched_groups_power(i, sd);
	}

#ifdef CONFIG_NUMA
	for (i = 0; i < nr_node_ids; i++)
		init_numa_sched_groups_power(sched_group_nodes[i]);

	if (sd_allnodes) {
		struct sched_group *sg;

		cpu_to_allnodes_group(cpumask_first(cpu_map), cpu_map, &sg,
								tmpmask);
		init_numa_sched_groups_power(sg);
	}
#endif

	/* Attach the domains */
	for_each_cpu(i, cpu_map) {
		struct sched_domain *sd;
#ifdef CONFIG_SCHED_SMT
		sd = &per_cpu(cpu_domains, i).sd;
#elif defined(CONFIG_SCHED_MC)
		sd = &per_cpu(core_domains, i).sd;
#else
		sd = &per_cpu(phys_domains, i).sd;
#endif
		cpu_attach_domain(sd, rd, i);
	}

	err = 0;

free_tmpmask:
	free_cpumask_var(tmpmask);
free_send_covered:
	free_cpumask_var(send_covered);
free_this_core_map:
	free_cpumask_var(this_core_map);
free_this_sibling_map:
	free_cpumask_var(this_sibling_map);
free_nodemask:
	free_cpumask_var(nodemask);
free_notcovered:
#ifdef CONFIG_NUMA
	free_cpumask_var(notcovered);
free_covered:
	free_cpumask_var(covered);
free_domainspan:
	free_cpumask_var(domainspan);
out:
#endif
	return err;

free_sched_groups:
#ifdef CONFIG_NUMA
	kfree(sched_group_nodes);
#endif
	goto free_tmpmask;

#ifdef CONFIG_NUMA
error:
	free_sched_groups(cpu_map, tmpmask);
	free_rootdomain(rd);
	goto free_tmpmask;
#endif
}

static int build_sched_domains(const struct cpumask *cpu_map)
{
	return __build_sched_domains(cpu_map, NULL);
}

static struct cpumask *doms_cur;	/* current sched domains */
static int ndoms_cur;		/* number of sched domains in 'doms_cur' */
static struct sched_domain_attr *dattr_cur;
				/* attribues of custom domains in 'doms_cur' */

/*
 * Special case: If a kmalloc of a doms_cur partition (array of
 * cpumask) fails, then fallback to a single sched domain,
 * as determined by the single cpumask fallback_doms.
 */
static cpumask_var_t fallback_doms;

/*
 * arch_update_cpu_topology lets virtualized architectures update the
 * cpu core maps. It is supposed to return 1 if the topology changed
 * or 0 if it stayed the same.
 */
int __attribute__((weak)) arch_update_cpu_topology(void)
{
	return 0;
}

/*
 * Set up scheduler domains and groups. Callers must hold the hotplug lock.
 * For now this just excludes isolated cpus, but could be used to
 * exclude other special cases in the future.
 */
static int arch_init_sched_domains(const struct cpumask *cpu_map)
{
	int err;

	arch_update_cpu_topology();
	ndoms_cur = 1;
	doms_cur = kmalloc(cpumask_size(), GFP_KERNEL);
	if (!doms_cur)
		doms_cur = fallback_doms;
	cpumask_andnot(doms_cur, cpu_map, cpu_isolated_map);
	dattr_cur = NULL;
	err = build_sched_domains(doms_cur);
	register_sched_domain_sysctl();

	return err;
}

static void arch_destroy_sched_domains(const struct cpumask *cpu_map,
				       struct cpumask *tmpmask)
{
	free_sched_groups(cpu_map, tmpmask);
}

/*
 * Detach sched domains from a group of cpus specified in cpu_map
 * These cpus will now be attached to the NULL domain
 */
static void detach_destroy_domains(const struct cpumask *cpu_map)
{
	/* Save because hotplug lock held. */
	static DECLARE_BITMAP(tmpmask, CONFIG_NR_CPUS);
	int i;

	for_each_cpu(i, cpu_map)
		cpu_attach_domain(NULL, &def_root_domain, i);
	synchronize_sched();
	arch_destroy_sched_domains(cpu_map, to_cpumask(tmpmask));
}

/* handle null as "default" */
static int dattrs_equal(struct sched_domain_attr *cur, int idx_cur,
			struct sched_domain_attr *new, int idx_new)
{
	struct sched_domain_attr tmp;

	/* fast path */
	if (!new && !cur)
		return 1;

	tmp = SD_ATTR_INIT;
	return !memcmp(cur ? (cur + idx_cur) : &tmp,
			new ? (new + idx_new) : &tmp,
			sizeof(struct sched_domain_attr));
}

/*
 * Partition sched domains as specified by the 'ndoms_new'
 * cpumasks in the array doms_new[] of cpumasks. This compares
 * doms_new[] to the current sched domain partitioning, doms_cur[].
 * It destroys each deleted domain and builds each new domain.
 *
 * 'doms_new' is an array of cpumask's of length 'ndoms_new'.
 * The masks don't intersect (don't overlap.) We should setup one
 * sched domain for each mask. CPUs not in any of the cpumasks will
 * not be load balanced. If the same cpumask appears both in the
 * current 'doms_cur' domains and in the new 'doms_new', we can leave
 * it as it is.
 *
 * The passed in 'doms_new' should be kmalloc'd. This routine takes
 * ownership of it and will kfree it when done with it. If the caller
 * failed the kmalloc call, then it can pass in doms_new == NULL &&
 * ndoms_new == 1, and partition_sched_domains() will fallback to
 * the single partition 'fallback_doms', it also forces the domains
 * to be rebuilt.
 *
 * If doms_new == NULL it will be replaced with cpu_online_mask.
 * ndoms_new == 0 is a special case for destroying existing domains,
 * and it will not create the default domain.
 *
 * Call with hotplug lock held
 */
/* FIXME: Change to struct cpumask *doms_new[] */
void partition_sched_domains(int ndoms_new, struct cpumask *doms_new,
			     struct sched_domain_attr *dattr_new)
{
	int i, j, n;
	int new_topology;

	mutex_lock(&sched_domains_mutex);

	/* always unregister in case we don't destroy any domains */
	unregister_sched_domain_sysctl();

	/* Let architecture update cpu core mappings. */
	new_topology = arch_update_cpu_topology();

	n = doms_new ? ndoms_new : 0;

	/* Destroy deleted domains */
	for (i = 0; i < ndoms_cur; i++) {
		for (j = 0; j < n && !new_topology; j++) {
			if (cpumask_equal(&doms_cur[i], &doms_new[j])
			    && dattrs_equal(dattr_cur, i, dattr_new, j))
				goto match1;
		}
		/* no match - a current sched domain not in new doms_new[] */
		detach_destroy_domains(doms_cur + i);
match1:
		;
	}

	if (doms_new == NULL) {
		ndoms_cur = 0;
		doms_new = fallback_doms;
		cpumask_andnot(&doms_new[0], cpu_online_mask, cpu_isolated_map);
		WARN_ON_ONCE(dattr_new);
	}

	/* Build new domains */
	for (i = 0; i < ndoms_new; i++) {
		for (j = 0; j < ndoms_cur && !new_topology; j++) {
			if (cpumask_equal(&doms_new[i], &doms_cur[j])
			    && dattrs_equal(dattr_new, i, dattr_cur, j))
				goto match2;
		}
		/* no match - add a new doms_new */
		__build_sched_domains(doms_new + i,
					dattr_new ? dattr_new + i : NULL);
match2:
		;
	}

	/* Remember the new sched domains */
	if (doms_cur != fallback_doms)
		kfree(doms_cur);
	kfree(dattr_cur);	/* kfree(NULL) is safe */
	doms_cur = doms_new;
	dattr_cur = dattr_new;
	ndoms_cur = ndoms_new;

	register_sched_domain_sysctl();

	mutex_unlock(&sched_domains_mutex);
}

#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
static void arch_reinit_sched_domains(void)
{
	get_online_cpus();

	/* Destroy domains first to force the rebuild */
	partition_sched_domains(0, NULL, NULL);

	rebuild_sched_domains();
	put_online_cpus();
}

static ssize_t sched_power_savings_store(const char *buf, size_t count, int smt)
{
	unsigned int level = 0;

	if (sscanf(buf, "%u", &level) != 1)
		return -EINVAL;

	/*
	 * level is always be positive so don't check for
	 * level < POWERSAVINGS_BALANCE_NONE which is 0
	 * What happens on 0 or 1 byte write,
	 * need to check for count as well?
	 */

	if (level >= MAX_POWERSAVINGS_BALANCE_LEVELS)
		return -EINVAL;

	if (smt)
		sched_smt_power_savings = level;
	else
		sched_mc_power_savings = level;

	arch_reinit_sched_domains();

	return count;
}

#ifdef CONFIG_SCHED_MC
static ssize_t sched_mc_power_savings_show(struct sysdev_class *class,
					   char *page)
{
	return sprintf(page, "%u\n", sched_mc_power_savings);
}
static ssize_t sched_mc_power_savings_store(struct sysdev_class *class,
					    const char *buf, size_t count)
{
	return sched_power_savings_store(buf, count, 0);
}
static SYSDEV_CLASS_ATTR(sched_mc_power_savings, 0644,
			 sched_mc_power_savings_show,
			 sched_mc_power_savings_store);
#endif

#ifdef CONFIG_SCHED_SMT
static ssize_t sched_smt_power_savings_show(struct sysdev_class *dev,
					    char *page)
{
	return sprintf(page, "%u\n", sched_smt_power_savings);
}
static ssize_t sched_smt_power_savings_store(struct sysdev_class *dev,
					     const char *buf, size_t count)
{
	return sched_power_savings_store(buf, count, 1);
}
static SYSDEV_CLASS_ATTR(sched_smt_power_savings, 0644,
		   sched_smt_power_savings_show,
		   sched_smt_power_savings_store);
#endif

int __init sched_create_sysfs_power_savings_entries(struct sysdev_class *cls)
{
	int err = 0;

#ifdef CONFIG_SCHED_SMT
	if (smt_capable())
		err = sysfs_create_file(&cls->kset.kobj,
					&attr_sched_smt_power_savings.attr);
#endif
#ifdef CONFIG_SCHED_MC
	if (!err && mc_capable())
		err = sysfs_create_file(&cls->kset.kobj,
					&attr_sched_mc_power_savings.attr);
#endif
	return err;
}
#endif /* CONFIG_SCHED_MC || CONFIG_SCHED_SMT */

#ifndef CONFIG_CPUSETS
/*
 * Add online and remove offline CPUs from the scheduler domains.
 * When cpusets are enabled they take over this function.
 */
static int update_sched_domains(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		partition_sched_domains(1, NULL, NULL);
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}
#endif

static int update_runtime(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	int cpu = (int)(long)hcpu;

	switch (action) {
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		disable_runtime(cpu_rq(cpu));
		return NOTIFY_OK;

	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		enable_runtime(cpu_rq(cpu));
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

void __init sched_init_smp(void)
{
	cpumask_var_t non_isolated_cpus;

	alloc_cpumask_var(&non_isolated_cpus, GFP_KERNEL);

#if defined(CONFIG_NUMA)
	sched_group_nodes_bycpu = kzalloc(nr_cpu_ids * sizeof(void **),
								GFP_KERNEL);
	BUG_ON(sched_group_nodes_bycpu == NULL);
#endif
	get_online_cpus();
	mutex_lock(&sched_domains_mutex);
	arch_init_sched_domains(cpu_online_mask);
	cpumask_andnot(non_isolated_cpus, cpu_possible_mask, cpu_isolated_map);
	if (cpumask_empty(non_isolated_cpus))
		cpumask_set_cpu(smp_processor_id(), non_isolated_cpus);
	mutex_unlock(&sched_domains_mutex);
	put_online_cpus();

#ifndef CONFIG_CPUSETS
	/* XXX: Theoretical race here - CPU may be hotplugged now */
	hotcpu_notifier(update_sched_domains, 0);
#endif

	/* RT runtime code needs to handle some hotplug events */
	hotcpu_notifier(update_runtime, 0);

	init_hrtick();

	/* Move init over to a non-isolated CPU */
	if (set_cpus_allowed_ptr(current, non_isolated_cpus) < 0)
		BUG();
	sched_init_granularity();
	free_cpumask_var(non_isolated_cpus);

	alloc_cpumask_var(&fallback_doms, GFP_KERNEL);
	init_sched_rt_class();
}
#else
void __init sched_init_smp(void)
{
	sched_init_granularity();
}
#endif /* CONFIG_SMP */

int in_sched_functions(unsigned long addr)
{
	return in_lock_functions(addr) ||
		(addr >= (unsigned long)__sched_text_start
		&& addr < (unsigned long)__sched_text_end);
}

static void init_cfs_rq(struct cfs_rq *cfs_rq, struct rq *rq)
{
	cfs_rq->tasks_timeline = RB_ROOT;
	INIT_LIST_HEAD(&cfs_rq->tasks);
#ifdef CONFIG_FAIR_GROUP_SCHED
	cfs_rq->rq = rq;
#endif
	cfs_rq->min_vruntime = (u64)(-(1LL << 20));
}

static void init_rt_rq(struct rt_rq *rt_rq, struct rq *rq)
{
	struct rt_prio_array *array;
	int i;

	array = &rt_rq->active;
	for (i = 0; i < MAX_RT_PRIO; i++) {
		INIT_LIST_HEAD(array->queue + i);
		__clear_bit(i, array->bitmap);
	}
	/* delimiter for bitsearch: */
	__set_bit(MAX_RT_PRIO, array->bitmap);

#if defined CONFIG_SMP || defined CONFIG_RT_GROUP_SCHED
	rt_rq->highest_prio.curr = MAX_RT_PRIO;
#ifdef CONFIG_SMP
	rt_rq->highest_prio.next = MAX_RT_PRIO;
#endif
#endif
#ifdef CONFIG_SMP
	rt_rq->rt_nr_migratory = 0;
	rt_rq->overloaded = 0;
	plist_head_init(&rq->rt.pushable_tasks, &rq->lock);
#endif

	rt_rq->rt_time = 0;
	rt_rq->rt_throttled = 0;
	rt_rq->rt_runtime = 0;
	spin_lock_init(&rt_rq->rt_runtime_lock);

#ifdef CONFIG_RT_GROUP_SCHED
	rt_rq->rt_nr_boosted = 0;
	rt_rq->rq = rq;
#endif
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static void init_tg_cfs_entry(struct task_group *tg, struct cfs_rq *cfs_rq,
				struct sched_entity *se, int cpu, int add,
				struct sched_entity *parent)
{
	struct rq *rq = cpu_rq(cpu);
	tg->cfs_rq[cpu] = cfs_rq;
	init_cfs_rq(cfs_rq, rq);
	cfs_rq->tg = tg;
	if (add)
		list_add(&cfs_rq->leaf_cfs_rq_list, &rq->leaf_cfs_rq_list);

	tg->se[cpu] = se;
	/* se could be NULL for init_task_group */
	if (!se)
		return;

	if (!parent)
		se->cfs_rq = &rq->cfs;
	else
		se->cfs_rq = parent->my_q;

	se->my_q = cfs_rq;
	se->load.weight = tg->shares;
	se->load.inv_weight = 0;
	se->parent = parent;
}
#endif

#ifdef CONFIG_RT_GROUP_SCHED
static void init_tg_rt_entry(struct task_group *tg, struct rt_rq *rt_rq,
		struct sched_rt_entity *rt_se, int cpu, int add,
		struct sched_rt_entity *parent)
{
	struct rq *rq = cpu_rq(cpu);

	tg->rt_rq[cpu] = rt_rq;
	init_rt_rq(rt_rq, rq);
	rt_rq->tg = tg;
	rt_rq->rt_se = rt_se;
	rt_rq->rt_runtime = tg->rt_bandwidth.rt_runtime;
	if (add)
		list_add(&rt_rq->leaf_rt_rq_list, &rq->leaf_rt_rq_list);

	tg->rt_se[cpu] = rt_se;
	if (!rt_se)
		return;

	if (!parent)
		rt_se->rt_rq = &rq->rt;
	else
		rt_se->rt_rq = parent->my_q;

	rt_se->my_q = rt_rq;
	rt_se->parent = parent;
	INIT_LIST_HEAD(&rt_se->run_list);
}
#endif

void __init sched_init(void)
{
	int i, j;
	unsigned long alloc_size = 0, ptr;

#ifdef CONFIG_FAIR_GROUP_SCHED
	alloc_size += 2 * nr_cpu_ids * sizeof(void **);
#endif
#ifdef CONFIG_RT_GROUP_SCHED
	alloc_size += 2 * nr_cpu_ids * sizeof(void **);
#endif
#ifdef CONFIG_USER_SCHED
	alloc_size *= 2;
#endif
#ifdef CONFIG_CPUMASK_OFFSTACK
	alloc_size += num_possible_cpus() * cpumask_size();
#endif
	/*
	 * As sched_init() is called before page_alloc is setup,
	 * we use alloc_bootmem().
	 */
	if (alloc_size) {
		ptr = (unsigned long)alloc_bootmem(alloc_size);

#ifdef CONFIG_FAIR_GROUP_SCHED
		init_task_group.se = (struct sched_entity **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);

		init_task_group.cfs_rq = (struct cfs_rq **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);

#ifdef CONFIG_USER_SCHED
		root_task_group.se = (struct sched_entity **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);

		root_task_group.cfs_rq = (struct cfs_rq **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);
#endif /* CONFIG_USER_SCHED */
#endif /* CONFIG_FAIR_GROUP_SCHED */
#ifdef CONFIG_RT_GROUP_SCHED
		init_task_group.rt_se = (struct sched_rt_entity **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);

		init_task_group.rt_rq = (struct rt_rq **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);

#ifdef CONFIG_USER_SCHED
		root_task_group.rt_se = (struct sched_rt_entity **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);

		root_task_group.rt_rq = (struct rt_rq **)ptr;
		ptr += nr_cpu_ids * sizeof(void **);
#endif /* CONFIG_USER_SCHED */
#endif /* CONFIG_RT_GROUP_SCHED */
#ifdef CONFIG_CPUMASK_OFFSTACK
		for_each_possible_cpu(i) {
			per_cpu(load_balance_tmpmask, i) = (void *)ptr;
			ptr += cpumask_size();
		}
#endif /* CONFIG_CPUMASK_OFFSTACK */
	}

#ifdef CONFIG_SMP
	init_defrootdomain();
#endif

	init_rt_bandwidth(&def_rt_bandwidth,
			global_rt_period(), global_rt_runtime());

#ifdef CONFIG_RT_GROUP_SCHED
	init_rt_bandwidth(&init_task_group.rt_bandwidth,
			global_rt_period(), global_rt_runtime());
#ifdef CONFIG_USER_SCHED
	init_rt_bandwidth(&root_task_group.rt_bandwidth,
			global_rt_period(), RUNTIME_INF);
#endif /* CONFIG_USER_SCHED */
#endif /* CONFIG_RT_GROUP_SCHED */

#ifdef CONFIG_GROUP_SCHED
	list_add(&init_task_group.list, &task_groups);
	INIT_LIST_HEAD(&init_task_group.children);

#ifdef CONFIG_USER_SCHED
	INIT_LIST_HEAD(&root_task_group.children);
	init_task_group.parent = &root_task_group;
	list_add(&init_task_group.siblings, &root_task_group.children);
#endif /* CONFIG_USER_SCHED */
#endif /* CONFIG_GROUP_SCHED */

	for_each_possible_cpu(i) {
		struct rq *rq;

		rq = cpu_rq(i);
		spin_lock_init(&rq->lock);
		rq->nr_running = 0;
		init_cfs_rq(&rq->cfs, rq);
		init_rt_rq(&rq->rt, rq);
#ifdef CONFIG_FAIR_GROUP_SCHED
		init_task_group.shares = init_task_group_load;
		INIT_LIST_HEAD(&rq->leaf_cfs_rq_list);
#ifdef CONFIG_CGROUP_SCHED
		/*
		 * How much cpu bandwidth does init_task_group get?
		 *
		 * In case of task-groups formed thr' the cgroup filesystem, it
		 * gets 100% of the cpu resources in the system. This overall
		 * system cpu resource is divided among the tasks of
		 * init_task_group and its child task-groups in a fair manner,
		 * based on each entity's (task or task-group's) weight
		 * (se->load.weight).
		 *
		 * In other words, if init_task_group has 10 tasks of weight
		 * 1024) and two child groups A0 and A1 (of weight 1024 each),
		 * then A0's share of the cpu resource is:
		 *
		 * 	A0's bandwidth = 1024 / (10*1024 + 1024 + 1024) = 8.33%
		 *
		 * We achieve this by letting init_task_group's tasks sit
		 * directly in rq->cfs (i.e init_task_group->se[] = NULL).
		 */
		init_tg_cfs_entry(&init_task_group, &rq->cfs, NULL, i, 1, NULL);
#elif defined CONFIG_USER_SCHED
		root_task_group.shares = NICE_0_LOAD;
		init_tg_cfs_entry(&root_task_group, &rq->cfs, NULL, i, 0, NULL);
		/*
		 * In case of task-groups formed thr' the user id of tasks,
		 * init_task_group represents tasks belonging to root user.
		 * Hence it forms a sibling of all subsequent groups formed.
		 * In this case, init_task_group gets only a fraction of overall
		 * system cpu resource, based on the weight assigned to root
		 * user's cpu share (INIT_TASK_GROUP_LOAD). This is accomplished
		 * by letting tasks of init_task_group sit in a separate cfs_rq
		 * (init_cfs_rq) and having one entity represent this group of
		 * tasks in rq->cfs (i.e init_task_group->se[] != NULL).
		 */
		init_tg_cfs_entry(&init_task_group,
				&per_cpu(init_cfs_rq, i),
				&per_cpu(init_sched_entity, i), i, 1,
				root_task_group.se[i]);

#endif
#endif /* CONFIG_FAIR_GROUP_SCHED */

		rq->rt.rt_runtime = def_rt_bandwidth.rt_runtime;
#ifdef CONFIG_RT_GROUP_SCHED
		INIT_LIST_HEAD(&rq->leaf_rt_rq_list);
#ifdef CONFIG_CGROUP_SCHED
		init_tg_rt_entry(&init_task_group, &rq->rt, NULL, i, 1, NULL);
#elif defined CONFIG_USER_SCHED
		init_tg_rt_entry(&root_task_group, &rq->rt, NULL, i, 0, NULL);
		init_tg_rt_entry(&init_task_group,
				&per_cpu(init_rt_rq, i),
				&per_cpu(init_sched_rt_entity, i), i, 1,
				root_task_group.rt_se[i]);
#endif
#endif

		for (j = 0; j < CPU_LOAD_IDX_MAX; j++)
			rq->cpu_load[j] = 0;
#ifdef CONFIG_SMP
		rq->sd = NULL;
		rq->rd = NULL;
		rq->active_balance = 0;
		rq->next_balance = jiffies;
		rq->push_cpu = 0;
		rq->cpu = i;
		rq->online = 0;
		rq->migration_thread = NULL;
		INIT_LIST_HEAD(&rq->migration_queue);
		rq_attach_root(rq, &def_root_domain);
#endif
		init_rq_hrtick(rq);
		atomic_set(&rq->nr_iowait, 0);
	}

	set_load_weight(&init_task);

#ifdef CONFIG_PREEMPT_NOTIFIERS
	INIT_HLIST_HEAD(&init_task.preempt_notifiers);
#endif

#ifdef CONFIG_SMP
	open_softirq(SCHED_SOFTIRQ, run_rebalance_domains);
#endif

#ifdef CONFIG_RT_MUTEXES
	plist_head_init(&init_task.pi_waiters, &init_task.pi_lock);
#endif

	/*
	 * The boot idle thread does lazy MMU switching as well:
	 */
	atomic_inc(&init_mm.mm_count);
	enter_lazy_tlb(&init_mm, current);

	/*
	 * Make us the idle thread. Technically, schedule() should not be
	 * called from this thread, however somewhere below it might be,
	 * but because we are the idle thread, we just pick up running again
	 * when this runqueue becomes "idle".
	 */
	init_idle(current, smp_processor_id());
	/*
	 * During early bootup we pretend to be a normal task:
	 */
	current->sched_class = &fair_sched_class;

	/* Allocate the nohz_cpu_mask if CONFIG_CPUMASK_OFFSTACK */
	alloc_bootmem_cpumask_var(&nohz_cpu_mask);
#ifdef CONFIG_SMP
#ifdef CONFIG_NO_HZ
	alloc_bootmem_cpumask_var(&nohz.cpu_mask);
	alloc_bootmem_cpumask_var(&nohz.ilb_grp_nohz_mask);
#endif
	alloc_bootmem_cpumask_var(&cpu_isolated_map);
#endif /* SMP */

	scheduler_running = 1;
}

#ifdef CONFIG_DEBUG_SPINLOCK_SLEEP
void __might_sleep(char *file, int line)
{
#ifdef in_atomic
	static unsigned long prev_jiffy;	/* ratelimiting */

	if ((!in_atomic() && !irqs_disabled()) ||
		    system_state != SYSTEM_RUNNING || oops_in_progress)
		return;
	if (time_before(jiffies, prev_jiffy + HZ) && prev_jiffy)
		return;
	prev_jiffy = jiffies;

	printk(KERN_ERR
		"BUG: sleeping function called from invalid context at %s:%d\n",
			file, line);
	printk(KERN_ERR
		"in_atomic(): %d, irqs_disabled(): %d, pid: %d, name: %s\n",
			in_atomic(), irqs_disabled(),
			current->pid, current->comm);

	debug_show_held_locks(current);
	if (irqs_disabled())
		print_irqtrace_events(current);
	dump_stack();
#endif
}
EXPORT_SYMBOL(__might_sleep);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
static void normalize_task(struct rq *rq, struct task_struct *p)
{
	int on_rq;

	update_rq_clock(rq);
	on_rq = p->se.on_rq;
	if (on_rq)
		deactivate_task(rq, p, 0);
	__setscheduler(rq, p, SCHED_NORMAL, 0);
	if (on_rq) {
		activate_task(rq, p, 0);
		resched_task(rq->curr);
	}
}

void normalize_rt_tasks(void)
{
	struct task_struct *g, *p;
	unsigned long flags;
	struct rq *rq;

	read_lock_irqsave(&tasklist_lock, flags);
	do_each_thread(g, p) {
		/*
		 * Only normalize user tasks:
		 */
		if (!p->mm)
			continue;

		p->se.exec_start		= 0;
#ifdef CONFIG_SCHEDSTATS
		p->se.wait_start		= 0;
		p->se.sleep_start		= 0;
		p->se.block_start		= 0;
#endif

		if (!rt_task(p)) {
			/*
			 * Renice negative nice level userspace
			 * tasks back to 0:
			 */
			if (TASK_NICE(p) < 0 && p->mm)
				set_user_nice(p, 0);
			continue;
		}

		spin_lock(&p->pi_lock);
		rq = __task_rq_lock(p);

		normalize_task(rq, p);

		__task_rq_unlock(rq);
		spin_unlock(&p->pi_lock);
	} while_each_thread(g, p);

	read_unlock_irqrestore(&tasklist_lock, flags);
}

#endif /* CONFIG_MAGIC_SYSRQ */

#ifdef CONFIG_IA64
/*
 * These functions are only useful for the IA64 MCA handling.
 *
 * They can only be called when the whole system has been
 * stopped - every CPU needs to be quiescent, and no scheduling
 * activity can take place. Using them for anything else would
 * be a serious bug, and as a result, they aren't even visible
 * under any other configuration.
 */

/**
 * curr_task - return the current task for a given cpu.
 * @cpu: the processor in question.
 *
 * ONLY VALID WHEN THE WHOLE SYSTEM IS STOPPED!
 */
struct task_struct *curr_task(int cpu)
{
	return cpu_curr(cpu);
}

/**
 * set_curr_task - set the current task for a given cpu.
 * @cpu: the processor in question.
 * @p: the task pointer to set.
 *
 * Description: This function must only be used when non-maskable interrupts
 * are serviced on a separate stack. It allows the architecture to switch the
 * notion of the current task on a cpu in a non-blocking manner. This function
 * must be called with all CPU's synchronized, and interrupts disabled, the
 * and caller must save the original value of the current task (see
 * curr_task() above) and restore that value before reenabling interrupts and
 * re-starting the system.
 *
 * ONLY VALID WHEN THE WHOLE SYSTEM IS STOPPED!
 */
void set_curr_task(int cpu, struct task_struct *p)
{
	cpu_curr(cpu) = p;
}

#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
static void free_fair_sched_group(struct task_group *tg)
{
	int i;

	for_each_possible_cpu(i) {
		if (tg->cfs_rq)
			kfree(tg->cfs_rq[i]);
		if (tg->se)
			kfree(tg->se[i]);
	}

	kfree(tg->cfs_rq);
	kfree(tg->se);
}

static
int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se;
	struct rq *rq;
	int i;

	tg->cfs_rq = kzalloc(sizeof(cfs_rq) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->cfs_rq)
		goto err;
	tg->se = kzalloc(sizeof(se) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->se)
		goto err;

	tg->shares = NICE_0_LOAD;

	for_each_possible_cpu(i) {
		rq = cpu_rq(i);

		cfs_rq = kzalloc_node(sizeof(struct cfs_rq),
				      GFP_KERNEL, cpu_to_node(i));
		if (!cfs_rq)
			goto err;

		se = kzalloc_node(sizeof(struct sched_entity),
				  GFP_KERNEL, cpu_to_node(i));
		if (!se)
			goto err;

		init_tg_cfs_entry(tg, cfs_rq, se, i, 0, parent->se[i]);
	}

	return 1;

 err:
	return 0;
}

static inline void register_fair_sched_group(struct task_group *tg, int cpu)
{
	list_add_rcu(&tg->cfs_rq[cpu]->leaf_cfs_rq_list,
			&cpu_rq(cpu)->leaf_cfs_rq_list);
}

static inline void unregister_fair_sched_group(struct task_group *tg, int cpu)
{
	list_del_rcu(&tg->cfs_rq[cpu]->leaf_cfs_rq_list);
}
#else /* !CONFG_FAIR_GROUP_SCHED */
static inline void free_fair_sched_group(struct task_group *tg)
{
}

static inline
int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	return 1;
}

static inline void register_fair_sched_group(struct task_group *tg, int cpu)
{
}

static inline void unregister_fair_sched_group(struct task_group *tg, int cpu)
{
}
#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_RT_GROUP_SCHED
static void free_rt_sched_group(struct task_group *tg)
{
	int i;

	destroy_rt_bandwidth(&tg->rt_bandwidth);

	for_each_possible_cpu(i) {
		if (tg->rt_rq)
			kfree(tg->rt_rq[i]);
		if (tg->rt_se)
			kfree(tg->rt_se[i]);
	}

	kfree(tg->rt_rq);
	kfree(tg->rt_se);
}

static
int alloc_rt_sched_group(struct task_group *tg, struct task_group *parent)
{
	struct rt_rq *rt_rq;
	struct sched_rt_entity *rt_se;
	struct rq *rq;
	int i;

	tg->rt_rq = kzalloc(sizeof(rt_rq) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->rt_rq)
		goto err;
	tg->rt_se = kzalloc(sizeof(rt_se) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->rt_se)
		goto err;

	init_rt_bandwidth(&tg->rt_bandwidth,
			ktime_to_ns(def_rt_bandwidth.rt_period), 0);

	for_each_possible_cpu(i) {
		rq = cpu_rq(i);

		rt_rq = kzalloc_node(sizeof(struct rt_rq),
				     GFP_KERNEL, cpu_to_node(i));
		if (!rt_rq)
			goto err;

		rt_se = kzalloc_node(sizeof(struct sched_rt_entity),
				     GFP_KERNEL, cpu_to_node(i));
		if (!rt_se)
			goto err;

		init_tg_rt_entry(tg, rt_rq, rt_se, i, 0, parent->rt_se[i]);
	}

	return 1;

 err:
	return 0;
}

static inline void register_rt_sched_group(struct task_group *tg, int cpu)
{
	list_add_rcu(&tg->rt_rq[cpu]->leaf_rt_rq_list,
			&cpu_rq(cpu)->leaf_rt_rq_list);
}

static inline void unregister_rt_sched_group(struct task_group *tg, int cpu)
{
	list_del_rcu(&tg->rt_rq[cpu]->leaf_rt_rq_list);
}
#else /* !CONFIG_RT_GROUP_SCHED */
static inline void free_rt_sched_group(struct task_group *tg)
{
}

static inline
int alloc_rt_sched_group(struct task_group *tg, struct task_group *parent)
{
	return 1;
}

static inline void register_rt_sched_group(struct task_group *tg, int cpu)
{
}

static inline void unregister_rt_sched_group(struct task_group *tg, int cpu)
{
}
#endif /* CONFIG_RT_GROUP_SCHED */

#ifdef CONFIG_GROUP_SCHED
static void free_sched_group(struct task_group *tg)
{
	free_fair_sched_group(tg);
	free_rt_sched_group(tg);
	kfree(tg);
}

/* allocate runqueue etc for a new task group */
struct task_group *sched_create_group(struct task_group *parent)
{
	struct task_group *tg;
	unsigned long flags;
	int i;

	tg = kzalloc(sizeof(*tg), GFP_KERNEL);
	if (!tg)
		return ERR_PTR(-ENOMEM);

	if (!alloc_fair_sched_group(tg, parent))
		goto err;

	if (!alloc_rt_sched_group(tg, parent))
		goto err;

	spin_lock_irqsave(&task_group_lock, flags);
	for_each_possible_cpu(i) {
		register_fair_sched_group(tg, i);
		register_rt_sched_group(tg, i);
	}
	list_add_rcu(&tg->list, &task_groups);

	WARN_ON(!parent); /* root should already exist */

	tg->parent = parent;
	INIT_LIST_HEAD(&tg->children);
	list_add_rcu(&tg->siblings, &parent->children);
	spin_unlock_irqrestore(&task_group_lock, flags);

	return tg;

err:
	free_sched_group(tg);
	return ERR_PTR(-ENOMEM);
}

/* rcu callback to free various structures associated with a task group */
static void free_sched_group_rcu(struct rcu_head *rhp)
{
	/* now it should be safe to free those cfs_rqs */
	free_sched_group(container_of(rhp, struct task_group, rcu));
}

/* Destroy runqueue etc associated with a task group */
void sched_destroy_group(struct task_group *tg)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&task_group_lock, flags);
	for_each_possible_cpu(i) {
		unregister_fair_sched_group(tg, i);
		unregister_rt_sched_group(tg, i);
	}
	list_del_rcu(&tg->list);
	list_del_rcu(&tg->siblings);
	spin_unlock_irqrestore(&task_group_lock, flags);

	/* wait for possible concurrent references to cfs_rqs complete */
	call_rcu(&tg->rcu, free_sched_group_rcu);
}

/* change task's runqueue when it moves between groups.
 *	The caller of this function should have put the task in its new group
 *	by now. This function just updates tsk->se.cfs_rq and tsk->se.parent to
 *	reflect its new group.
 */
void sched_move_task(struct task_struct *tsk)
{
	int on_rq, running;
	unsigned long flags;
	struct rq *rq;

	rq = task_rq_lock(tsk, &flags);

	update_rq_clock(rq);

	running = task_current(rq, tsk);
	on_rq = tsk->se.on_rq;

	if (on_rq)
		dequeue_task(rq, tsk, 0);
	if (unlikely(running))
		tsk->sched_class->put_prev_task(rq, tsk);

	set_task_rq(tsk, task_cpu(tsk));

#ifdef CONFIG_FAIR_GROUP_SCHED
	if (tsk->sched_class->moved_group)
		tsk->sched_class->moved_group(tsk);
#endif

	if (unlikely(running))
		tsk->sched_class->set_curr_task(rq);
	if (on_rq)
		enqueue_task(rq, tsk, 0);

	task_rq_unlock(rq, &flags);
}
#endif /* CONFIG_GROUP_SCHED */

#ifdef CONFIG_FAIR_GROUP_SCHED
static void __set_se_shares(struct sched_entity *se, unsigned long shares)
{
	struct cfs_rq *cfs_rq = se->cfs_rq;
	int on_rq;

	on_rq = se->on_rq;
	if (on_rq)
		dequeue_entity(cfs_rq, se, 0);

	se->load.weight = shares;
	se->load.inv_weight = 0;

	if (on_rq)
		enqueue_entity(cfs_rq, se, 0);
}

static void set_se_shares(struct sched_entity *se, unsigned long shares)
{
	struct cfs_rq *cfs_rq = se->cfs_rq;
	struct rq *rq = cfs_rq->rq;
	unsigned long flags;

	spin_lock_irqsave(&rq->lock, flags);
	__set_se_shares(se, shares);
	spin_unlock_irqrestore(&rq->lock, flags);
}

static DEFINE_MUTEX(shares_mutex);

int sched_group_set_shares(struct task_group *tg, unsigned long shares)
{
	int i;
	unsigned long flags;

	/*
	 * We can't change the weight of the root cgroup.
	 */
	if (!tg->se[0])
		return -EINVAL;

	if (shares < MIN_SHARES)
		shares = MIN_SHARES;
	else if (shares > MAX_SHARES)
		shares = MAX_SHARES;

	mutex_lock(&shares_mutex);
	if (tg->shares == shares)
		goto done;

	spin_lock_irqsave(&task_group_lock, flags);
	for_each_possible_cpu(i)
		unregister_fair_sched_group(tg, i);
	list_del_rcu(&tg->siblings);
	spin_unlock_irqrestore(&task_group_lock, flags);

	/* wait for any ongoing reference to this group to finish */
	synchronize_sched();

	/*
	 * Now we are free to modify the group's share on each cpu
	 * w/o tripping rebalance_share or load_balance_fair.
	 */
	tg->shares = shares;
	for_each_possible_cpu(i) {
		/*
		 * force a rebalance
		 */
		cfs_rq_set_shares(tg->cfs_rq[i], 0);
		set_se_shares(tg->se[i], shares);
	}

	/*
	 * Enable load balance activity on this group, by inserting it back on
	 * each cpu's rq->leaf_cfs_rq_list.
	 */
	spin_lock_irqsave(&task_group_lock, flags);
	for_each_possible_cpu(i)
		register_fair_sched_group(tg, i);
	list_add_rcu(&tg->siblings, &tg->parent->children);
	spin_unlock_irqrestore(&task_group_lock, flags);
done:
	mutex_unlock(&shares_mutex);
	return 0;
}

unsigned long sched_group_shares(struct task_group *tg)
{
	return tg->shares;
}
#endif

#ifdef CONFIG_RT_GROUP_SCHED
/*
 * Ensure that the real time constraints are schedulable.
 */
static DEFINE_MUTEX(rt_constraints_mutex);

static unsigned long to_ratio(u64 period, u64 runtime)
{
	if (runtime == RUNTIME_INF)
		return 1ULL << 20;

	return div64_u64(runtime << 20, period);
}

/* Must be called with tasklist_lock held */
static inline int tg_has_rt_tasks(struct task_group *tg)
{
	struct task_struct *g, *p;

	do_each_thread(g, p) {
		if (rt_task(p) && rt_rq_of_se(&p->rt)->tg == tg)
			return 1;
	} while_each_thread(g, p);

	return 0;
}

struct rt_schedulable_data {
	struct task_group *tg;
	u64 rt_period;
	u64 rt_runtime;
};

static int tg_schedulable(struct task_group *tg, void *data)
{
	struct rt_schedulable_data *d = data;
	struct task_group *child;
	unsigned long total, sum = 0;
	u64 period, runtime;

	period = ktime_to_ns(tg->rt_bandwidth.rt_period);
	runtime = tg->rt_bandwidth.rt_runtime;

	if (tg == d->tg) {
		period = d->rt_period;
		runtime = d->rt_runtime;
	}

#ifdef CONFIG_USER_SCHED
	if (tg == &root_task_group) {
		period = global_rt_period();
		runtime = global_rt_runtime();
	}
#endif

	/*
	 * Cannot have more runtime than the period.
	 */
	if (runtime > period && runtime != RUNTIME_INF)
		return -EINVAL;

	/*
	 * Ensure we don't starve existing RT tasks.
	 */
	if (rt_bandwidth_enabled() && !runtime && tg_has_rt_tasks(tg))
		return -EBUSY;

	total = to_ratio(period, runtime);

	/*
	 * Nobody can have more than the global setting allows.
	 */
	if (total > to_ratio(global_rt_period(), global_rt_runtime()))
		return -EINVAL;

	/*
	 * The sum of our children's runtime should not exceed our own.
	 */
	list_for_each_entry_rcu(child, &tg->children, siblings) {
		period = ktime_to_ns(child->rt_bandwidth.rt_period);
		runtime = child->rt_bandwidth.rt_runtime;

		if (child == d->tg) {
			period = d->rt_period;
			runtime = d->rt_runtime;
		}

		sum += to_ratio(period, runtime);
	}

	if (sum > total)
		return -EINVAL;

	return 0;
}

static int __rt_schedulable(struct task_group *tg, u64 period, u64 runtime)
{
	struct rt_schedulable_data data = {
		.tg = tg,
		.rt_period = period,
		.rt_runtime = runtime,
	};

	return walk_tg_tree(tg_schedulable, tg_nop, &data);
}

static int tg_set_bandwidth(struct task_group *tg,
		u64 rt_period, u64 rt_runtime)
{
	int i, err = 0;

	mutex_lock(&rt_constraints_mutex);
	read_lock(&tasklist_lock);
	err = __rt_schedulable(tg, rt_period, rt_runtime);
	if (err)
		goto unlock;

	spin_lock_irq(&tg->rt_bandwidth.rt_runtime_lock);
	tg->rt_bandwidth.rt_period = ns_to_ktime(rt_period);
	tg->rt_bandwidth.rt_runtime = rt_runtime;

	for_each_possible_cpu(i) {
		struct rt_rq *rt_rq = tg->rt_rq[i];

		spin_lock(&rt_rq->rt_runtime_lock);
		rt_rq->rt_runtime = rt_runtime;
		spin_unlock(&rt_rq->rt_runtime_lock);
	}
	spin_unlock_irq(&tg->rt_bandwidth.rt_runtime_lock);
 unlock:
	read_unlock(&tasklist_lock);
	mutex_unlock(&rt_constraints_mutex);

	return err;
}

int sched_group_set_rt_runtime(struct task_group *tg, long rt_runtime_us)
{
	u64 rt_runtime, rt_period;

	rt_period = ktime_to_ns(tg->rt_bandwidth.rt_period);
	rt_runtime = (u64)rt_runtime_us * NSEC_PER_USEC;
	if (rt_runtime_us < 0)
		rt_runtime = RUNTIME_INF;

	return tg_set_bandwidth(tg, rt_period, rt_runtime);
}

long sched_group_rt_runtime(struct task_group *tg)
{
	u64 rt_runtime_us;

	if (tg->rt_bandwidth.rt_runtime == RUNTIME_INF)
		return -1;

	rt_runtime_us = tg->rt_bandwidth.rt_runtime;
	do_div(rt_runtime_us, NSEC_PER_USEC);
	return rt_runtime_us;
}

int sched_group_set_rt_period(struct task_group *tg, long rt_period_us)
{
	u64 rt_runtime, rt_period;

	rt_period = (u64)rt_period_us * NSEC_PER_USEC;
	rt_runtime = tg->rt_bandwidth.rt_runtime;

	if (rt_period == 0)
		return -EINVAL;

	return tg_set_bandwidth(tg, rt_period, rt_runtime);
}

long sched_group_rt_period(struct task_group *tg)
{
	u64 rt_period_us;

	rt_period_us = ktime_to_ns(tg->rt_bandwidth.rt_period);
	do_div(rt_period_us, NSEC_PER_USEC);
	return rt_period_us;
}

static int sched_rt_global_constraints(void)
{
	u64 runtime, period;
	int ret = 0;

	if (sysctl_sched_rt_period <= 0)
		return -EINVAL;

	runtime = global_rt_runtime();
	period = global_rt_period();

	/*
	 * Sanity check on the sysctl variables.
	 */
	if (runtime > period && runtime != RUNTIME_INF)
		return -EINVAL;

	mutex_lock(&rt_constraints_mutex);
	read_lock(&tasklist_lock);
	ret = __rt_schedulable(NULL, 0, 0);
	read_unlock(&tasklist_lock);
	mutex_unlock(&rt_constraints_mutex);

	return ret;
}

int sched_rt_can_attach(struct task_group *tg, struct task_struct *tsk)
{
	/* Don't accept realtime tasks when there is no way for them to run */
	if (rt_task(tsk) && tg->rt_bandwidth.rt_runtime == 0)
		return 0;

	return 1;
}

#else /* !CONFIG_RT_GROUP_SCHED */
static int sched_rt_global_constraints(void)
{
	unsigned long flags;
	int i;

	if (sysctl_sched_rt_period <= 0)
		return -EINVAL;

	spin_lock_irqsave(&def_rt_bandwidth.rt_runtime_lock, flags);
	for_each_possible_cpu(i) {
		struct rt_rq *rt_rq = &cpu_rq(i)->rt;

		spin_lock(&rt_rq->rt_runtime_lock);
		rt_rq->rt_runtime = global_rt_runtime();
		spin_unlock(&rt_rq->rt_runtime_lock);
	}
	spin_unlock_irqrestore(&def_rt_bandwidth.rt_runtime_lock, flags);

	return 0;
}
#endif /* CONFIG_RT_GROUP_SCHED */

int sched_rt_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;
	int old_period, old_runtime;
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);
	old_period = sysctl_sched_rt_period;
	old_runtime = sysctl_sched_rt_runtime;

	ret = proc_dointvec(table, write, filp, buffer, lenp, ppos);

	if (!ret && write) {
		ret = sched_rt_global_constraints();
		if (ret) {
			sysctl_sched_rt_period = old_period;
			sysctl_sched_rt_runtime = old_runtime;
		} else {
			def_rt_bandwidth.rt_runtime = global_rt_runtime();
			def_rt_bandwidth.rt_period =
				ns_to_ktime(global_rt_period());
		}
	}
	mutex_unlock(&mutex);

	return ret;
}

#ifdef CONFIG_CGROUP_SCHED

/* return corresponding task_group object of a cgroup */
static inline struct task_group *cgroup_tg(struct cgroup *cgrp)
{
	return container_of(cgroup_subsys_state(cgrp, cpu_cgroup_subsys_id),
			    struct task_group, css);
}

static struct cgroup_subsys_state *
cpu_cgroup_create(struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	struct task_group *tg, *parent;

	if (!cgrp->parent) {
		/* This is early initialization for the top cgroup */
		return &init_task_group.css;
	}

	parent = cgroup_tg(cgrp->parent);
	tg = sched_create_group(parent);
	if (IS_ERR(tg))
		return ERR_PTR(-ENOMEM);

	return &tg->css;
}

static void
cpu_cgroup_destroy(struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	struct task_group *tg = cgroup_tg(cgrp);

	sched_destroy_group(tg);
}

static int
cpu_cgroup_can_attach(struct cgroup_subsys *ss, struct cgroup *cgrp,
		      struct task_struct *tsk)
{
#ifdef CONFIG_RT_GROUP_SCHED
	if (!sched_rt_can_attach(cgroup_tg(cgrp), tsk))
		return -EINVAL;
#else
	/* We don't support RT-tasks being in separate groups */
	if (tsk->sched_class != &fair_sched_class)
		return -EINVAL;
#endif

	return 0;
}

static void
cpu_cgroup_attach(struct cgroup_subsys *ss, struct cgroup *cgrp,
			struct cgroup *old_cont, struct task_struct *tsk)
{
	sched_move_task(tsk);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static int cpu_shares_write_u64(struct cgroup *cgrp, struct cftype *cftype,
				u64 shareval)
{
	return sched_group_set_shares(cgroup_tg(cgrp), shareval);
}

static u64 cpu_shares_read_u64(struct cgroup *cgrp, struct cftype *cft)
{
	struct task_group *tg = cgroup_tg(cgrp);

	return (u64) tg->shares;
}
#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_RT_GROUP_SCHED
static int cpu_rt_runtime_write(struct cgroup *cgrp, struct cftype *cft,
				s64 val)
{
	return sched_group_set_rt_runtime(cgroup_tg(cgrp), val);
}

static s64 cpu_rt_runtime_read(struct cgroup *cgrp, struct cftype *cft)
{
	return sched_group_rt_runtime(cgroup_tg(cgrp));
}

static int cpu_rt_period_write_uint(struct cgroup *cgrp, struct cftype *cftype,
		u64 rt_period_us)
{
	return sched_group_set_rt_period(cgroup_tg(cgrp), rt_period_us);
}

static u64 cpu_rt_period_read_uint(struct cgroup *cgrp, struct cftype *cft)
{
	return sched_group_rt_period(cgroup_tg(cgrp));
}
#endif /* CONFIG_RT_GROUP_SCHED */

static struct cftype cpu_files[] = {
#ifdef CONFIG_FAIR_GROUP_SCHED
	{
		.name = "shares",
		.read_u64 = cpu_shares_read_u64,
		.write_u64 = cpu_shares_write_u64,
	},
#endif
#ifdef CONFIG_RT_GROUP_SCHED
	{
		.name = "rt_runtime_us",
		.read_s64 = cpu_rt_runtime_read,
		.write_s64 = cpu_rt_runtime_write,
	},
	{
		.name = "rt_period_us",
		.read_u64 = cpu_rt_period_read_uint,
		.write_u64 = cpu_rt_period_write_uint,
	},
#endif
};

static int cpu_cgroup_populate(struct cgroup_subsys *ss, struct cgroup *cont)
{
	return cgroup_add_files(cont, ss, cpu_files, ARRAY_SIZE(cpu_files));
}

struct cgroup_subsys cpu_cgroup_subsys = {
	.name		= "cpu",
	.create		= cpu_cgroup_create,
	.destroy	= cpu_cgroup_destroy,
	.can_attach	= cpu_cgroup_can_attach,
	.attach		= cpu_cgroup_attach,
	.populate	= cpu_cgroup_populate,
	.subsys_id	= cpu_cgroup_subsys_id,
	.early_init	= 1,
};

#endif	/* CONFIG_CGROUP_SCHED */

#ifdef CONFIG_CGROUP_CPUACCT

/*
 * CPU accounting code for task groups.
 *
 * Based on the work by Paul Menage (menage@google.com) and Balbir Singh
 * (balbir@in.ibm.com).
 */

/* track cpu usage of a group of tasks and its child groups */
struct cpuacct {
	struct cgroup_subsys_state css;
	/* cpuusage holds pointer to a u64-type object on every cpu */
	u64 *cpuusage;
	struct percpu_counter cpustat[CPUACCT_STAT_NSTATS];
	struct cpuacct *parent;
};

struct cgroup_subsys cpuacct_subsys;

/* return cpu accounting group corresponding to this container */
static inline struct cpuacct *cgroup_ca(struct cgroup *cgrp)
{
	return container_of(cgroup_subsys_state(cgrp, cpuacct_subsys_id),
			    struct cpuacct, css);
}

/* return cpu accounting group to which this task belongs */
static inline struct cpuacct *task_ca(struct task_struct *tsk)
{
	return container_of(task_subsys_state(tsk, cpuacct_subsys_id),
			    struct cpuacct, css);
}

/* create a new cpu accounting group */
static struct cgroup_subsys_state *cpuacct_create(
	struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	struct cpuacct *ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	int i;

	if (!ca)
		goto out;

	ca->cpuusage = alloc_percpu(u64);
	if (!ca->cpuusage)
		goto out_free_ca;

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++)
		if (percpu_counter_init(&ca->cpustat[i], 0))
			goto out_free_counters;

	if (cgrp->parent)
		ca->parent = cgroup_ca(cgrp->parent);

	return &ca->css;

out_free_counters:
	while (--i >= 0)
		percpu_counter_destroy(&ca->cpustat[i]);
	free_percpu(ca->cpuusage);
out_free_ca:
	kfree(ca);
out:
	return ERR_PTR(-ENOMEM);
}

/* destroy an existing cpu accounting group */
static void
cpuacct_destroy(struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	int i;

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++)
		percpu_counter_destroy(&ca->cpustat[i]);
	free_percpu(ca->cpuusage);
	kfree(ca);
}

static u64 cpuacct_cpuusage_read(struct cpuacct *ca, int cpu)
{
	u64 *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
	u64 data;

#ifndef CONFIG_64BIT
	/*
	 * Take rq->lock to make 64-bit read safe on 32-bit platforms.
	 */
	spin_lock_irq(&cpu_rq(cpu)->lock);
	data = *cpuusage;
	spin_unlock_irq(&cpu_rq(cpu)->lock);
#else
	data = *cpuusage;
#endif

	return data;
}

static void cpuacct_cpuusage_write(struct cpuacct *ca, int cpu, u64 val)
{
	u64 *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);

#ifndef CONFIG_64BIT
	/*
	 * Take rq->lock to make 64-bit write safe on 32-bit platforms.
	 */
	spin_lock_irq(&cpu_rq(cpu)->lock);
	*cpuusage = val;
	spin_unlock_irq(&cpu_rq(cpu)->lock);
#else
	*cpuusage = val;
#endif
}

/* return total cpu usage (in nanoseconds) of a group */
static u64 cpuusage_read(struct cgroup *cgrp, struct cftype *cft)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	u64 totalcpuusage = 0;
	int i;

	for_each_present_cpu(i)
		totalcpuusage += cpuacct_cpuusage_read(ca, i);

	return totalcpuusage;
}

static int cpuusage_write(struct cgroup *cgrp, struct cftype *cftype,
								u64 reset)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	int err = 0;
	int i;

	if (reset) {
		err = -EINVAL;
		goto out;
	}

	for_each_present_cpu(i)
		cpuacct_cpuusage_write(ca, i, 0);

out:
	return err;
}

static int cpuacct_percpu_seq_read(struct cgroup *cgroup, struct cftype *cft,
				   struct seq_file *m)
{
	struct cpuacct *ca = cgroup_ca(cgroup);
	u64 percpu;
	int i;

	for_each_present_cpu(i) {
		percpu = cpuacct_cpuusage_read(ca, i);
		seq_printf(m, "%llu ", (unsigned long long) percpu);
	}
	seq_printf(m, "\n");
	return 0;
}

static const char *cpuacct_stat_desc[] = {
	[CPUACCT_STAT_USER] = "user",
	[CPUACCT_STAT_SYSTEM] = "system",
};

static int cpuacct_stats_show(struct cgroup *cgrp, struct cftype *cft,
		struct cgroup_map_cb *cb)
{
	struct cpuacct *ca = cgroup_ca(cgrp);
	int i;

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++) {
		s64 val = percpu_counter_read(&ca->cpustat[i]);
		val = cputime64_to_clock_t(val);
		cb->fill(cb, cpuacct_stat_desc[i], val);
	}
	return 0;
}

static struct cftype files[] = {
	{
		.name = "usage",
		.read_u64 = cpuusage_read,
		.write_u64 = cpuusage_write,
	},
	{
		.name = "usage_percpu",
		.read_seq_string = cpuacct_percpu_seq_read,
	},
	{
		.name = "stat",
		.read_map = cpuacct_stats_show,
	},
};

static int cpuacct_populate(struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	return cgroup_add_files(cgrp, ss, files, ARRAY_SIZE(files));
}

/*
 * charge this task's execution time to its accounting group.
 *
 * called with rq->lock held.
 */
static void cpuacct_charge(struct task_struct *tsk, u64 cputime)
{
	struct cpuacct *ca;
	int cpu;

	if (unlikely(!cpuacct_subsys.active))
		return;

	cpu = task_cpu(tsk);

	rcu_read_lock();

	ca = task_ca(tsk);

	for (; ca; ca = ca->parent) {
		u64 *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
		*cpuusage += cputime;
	}

	rcu_read_unlock();
}

/*
 * Charge the system/user time to the task's accounting group.
 */
static void cpuacct_update_stats(struct task_struct *tsk,
		enum cpuacct_stat_index idx, cputime_t val)
{
	struct cpuacct *ca;

	if (unlikely(!cpuacct_subsys.active))
		return;

	rcu_read_lock();
	ca = task_ca(tsk);

	do {
		percpu_counter_add(&ca->cpustat[idx], val);
		ca = ca->parent;
	} while (ca);
	rcu_read_unlock();
}

struct cgroup_subsys cpuacct_subsys = {
	.name = "cpuacct",
	.create = cpuacct_create,
	.destroy = cpuacct_destroy,
	.populate = cpuacct_populate,
	.subsys_id = cpuacct_subsys_id,
};
#endif	/* CONFIG_CGROUP_CPUACCT */
