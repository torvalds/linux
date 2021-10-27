/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _WALT_H
#define _WALT_H

#include "../../../kernel/sched/sched.h"
#include "../../../fs/proc/internal.h"
#include <linux/sched/core_ctl.h>
#include <linux/jump_label.h>

#include <linux/cgroup.h>

#ifdef CONFIG_HZ_300
/*
 * Tick interval becomes to 3333333 due to
 * rounding error when HZ=300.
 */
#define DEFAULT_SCHED_RAVG_WINDOW (3333333 * 5)
#else
/* Min window size (in ns) = 16ms */
#define DEFAULT_SCHED_RAVG_WINDOW 16000000
#endif

/* Max window size (in ns) = 1s */
#define MAX_SCHED_RAVG_WINDOW 1000000000

#define NR_WINDOWS_PER_SEC (NSEC_PER_SEC / DEFAULT_SCHED_RAVG_WINDOW)

#define SCHED_CPUFREQ_MIGRATION	(1U << 1)
#define SCHED_CPUFREQ_INTERCLUSTER_MIG	(1U << 3)
#define SCHED_CPUFREQ_WALT	(1U << 4)
#define SCHED_CPUFREQ_PL	(1U << 5)
#define SCHED_CPUFREQ_EARLY_DET	(1U << 6)
#define SCHED_CPUFREQ_CONTINUE	(1U << 8)

/* MAX_MARGIN_LEVELS should be one less than MAX_CLUSTERS */
#define MAX_MARGIN_LEVELS (MAX_CLUSTERS - 1)

extern struct static_key_true walt_disabled;

enum task_event {
	PUT_PREV_TASK	= 0,
	PICK_NEXT_TASK	= 1,
	TASK_WAKE	= 2,
	TASK_MIGRATE	= 3,
	TASK_UPDATE	= 4,
	IRQ_UPDATE	= 5,
};

/* Note: this need to be in sync with migrate_type_names array */
enum migrate_types {
	GROUP_TO_RQ,
	RQ_TO_GROUP,
};

enum task_boost_type {
	TASK_BOOST_NONE = 0,
	TASK_BOOST_ON_MID,
	TASK_BOOST_ON_MAX,
	TASK_BOOST_STRICT_MAX,
	TASK_BOOST_END,
};

#define WALT_NR_CPUS 8
#define RAVG_HIST_SIZE_MAX 5
#define NUM_BUSY_BUCKETS 10

#define WALT_LOW_LATENCY_PROCFS	BIT(0)
#define WALT_LOW_LATENCY_BINDER	BIT(1)

struct walt_task_struct {
	/*
	 * 'mark_start' marks the beginning of an event (task waking up, task
	 * starting to execute, task being preempted) within a window
	 *
	 * 'sum' represents how runnable a task has been within current
	 * window. It incorporates both running time and wait time and is
	 * frequency scaled.
	 *
	 * 'sum_history' keeps track of history of 'sum' seen over previous
	 * RAVG_HIST_SIZE windows. Windows where task was entirely sleeping are
	 * ignored.
	 *
	 * 'demand' represents maximum sum seen over previous
	 * sysctl_sched_ravg_hist_size windows. 'demand' could drive frequency
	 * demand for tasks.
	 *
	 * 'curr_window_cpu' represents task's contribution to cpu busy time on
	 * various CPUs in the current window
	 *
	 * 'prev_window_cpu' represents task's contribution to cpu busy time on
	 * various CPUs in the previous window
	 *
	 * 'curr_window' represents the sum of all entries in curr_window_cpu
	 *
	 * 'prev_window' represents the sum of all entries in prev_window_cpu
	 *
	 * 'pred_demand' represents task's current predicted cpu busy time
	 *
	 * 'busy_buckets' groups historical busy time into different buckets
	 * used for prediction
	 *
	 * 'demand_scaled' represents task's demand scaled to 1024
	 */
	u64				mark_start;
	u32				sum, demand;
	u32				coloc_demand;
	u32				sum_history[RAVG_HIST_SIZE_MAX];
	u32				curr_window_cpu[WALT_NR_CPUS];
	u32				prev_window_cpu[WALT_NR_CPUS];
	u32				curr_window, prev_window;
	u32				pred_demand;
	u8				busy_buckets[NUM_BUSY_BUCKETS];
	u16				demand_scaled;
	u16				pred_demand_scaled;
	u64				active_time;
	u64				last_win_size;
	int				boost;
	bool				wake_up_idle;
	bool				misfit;
	bool				rtg_high_prio;
	u8				low_latency;
	u64				boost_period;
	u64				boost_expires;
	u64				last_sleep_ts;
	u32				init_load_pct;
	u32				unfilter;
	u64				last_wake_ts;
	u64				last_enqueued_ts;
	struct walt_related_thread_group __rcu	*grp;
	struct list_head		grp_list;
	u64				cpu_cycles;
	cpumask_t			cpus_requested;
	bool				iowaited;
};

/*End linux/sched.h port */
/*SCHED.H PORT*/
extern __read_mostly bool sched_predl;

struct walt_cpu_load {
	unsigned long	nl;
	unsigned long	pl;
	bool		rtgb_active;
	u64		ws;
};

#define DECLARE_BITMAP_ARRAY(name, nr, bits) \
	unsigned long name[nr][BITS_TO_LONGS(bits)]

struct walt_sched_stats {
	int		nr_big_tasks;
	u64		cumulative_runnable_avg_scaled;
	u64		pred_demands_sum_scaled;
	unsigned int	nr_rtg_high_prio_tasks;
};

#define NUM_TRACKED_WINDOWS 2
#define NUM_LOAD_INDICES 1000

struct group_cpu_time {
	u64			curr_runnable_sum;
	u64			prev_runnable_sum;
	u64			nt_curr_runnable_sum;
	u64			nt_prev_runnable_sum;
};

struct load_subtractions {
	u64			window_start;
	u64			subs;
	u64			new_subs;
};

struct walt_rq {
	struct task_struct	*push_task;
	struct walt_sched_cluster *cluster;
	struct cpumask		freq_domain_cpumask;
	struct walt_sched_stats walt_stats;

	u64			window_start;
	u32			prev_window_size;
	unsigned long		walt_flags;

	u64			avg_irqload;
	u64			last_irq_window;
	u64			prev_irq_time;
	struct task_struct	*ed_task;
	u64			task_exec_scale;
	u64			old_busy_time;
	u64			old_estimated_time;
	u64			curr_runnable_sum;
	u64			prev_runnable_sum;
	u64			nt_curr_runnable_sum;
	u64			nt_prev_runnable_sum;
	u64			cum_window_demand_scaled;
	struct group_cpu_time	grp_time;
	struct load_subtractions load_subs[NUM_TRACKED_WINDOWS];
	DECLARE_BITMAP_ARRAY(top_tasks_bitmap,
			NUM_TRACKED_WINDOWS, NUM_LOAD_INDICES);
	u8			*top_tasks[NUM_TRACKED_WINDOWS];
	u8			curr_table;
	int			prev_top;
	int			curr_top;
	bool			notif_pending;
	bool			high_irqload;
	u64			last_cc_update;
	u64			cycles;
};

struct walt_sched_cluster {
	raw_spinlock_t		load_lock;
	struct list_head	list;
	struct cpumask		cpus;
	int			id;
	/*
	 * max_possible_freq = maximum supported by hardware
	 */
	unsigned int		cur_freq;
	unsigned int		max_possible_freq;
	u64			aggr_grp_load;
};

struct walt_related_thread_group {
	int			id;
	raw_spinlock_t		lock;
	struct list_head	tasks;
	struct list_head	list;
	bool			skip_min;
	struct rcu_head		rcu;
	u64			last_update;
	u64			downmigrate_ts;
	u64			start_ts;
};

extern struct walt_sched_cluster *sched_cluster[WALT_NR_CPUS];

extern struct walt_sched_cluster *rq_cluster(struct rq *rq);

/*END SCHED.H PORT*/

extern int num_sched_clusters;
extern unsigned int sched_capacity_margin_up[WALT_NR_CPUS];
extern unsigned int sched_capacity_margin_down[WALT_NR_CPUS];
extern cpumask_t asym_cap_sibling_cpus;
extern cpumask_t __read_mostly **cpu_array;

extern void sched_update_nr_prod(int cpu, bool enq);
extern unsigned int walt_big_tasks(int cpu);
extern void walt_rotate_work_init(void);
extern void walt_rotation_checkpoint(int nr_big);
extern void walt_fill_ta_data(struct core_ctl_notif_data *data);
extern int sched_set_group_id(struct task_struct *p, unsigned int group_id);
extern unsigned int sched_get_group_id(struct task_struct *p);
extern int sched_set_init_task_load(struct task_struct *p, int init_load_pct);
extern u32 sched_get_init_task_load(struct task_struct *p);
extern void core_ctl_check(u64 wallclock);
extern int sched_set_boost(int enable);
extern int sched_pause_count(const cpumask_t *mask, bool include_offline);
extern void sched_pause_pending(int cpu);
extern void sched_unpause_pending(int cpu);
extern int sched_wake_up_idle_show(struct seq_file *m, void *v);
extern ssize_t sched_wake_up_idle_write(struct file *file,
		const char __user *buf, size_t count, loff_t *offset);
extern int sched_wake_up_idle_open(struct inode *inode,	struct file *filp);
extern int sched_init_task_load_show(struct seq_file *m, void *v);
extern ssize_t sched_init_task_load_write(struct file *file, const char __user *buf,
					size_t count, loff_t *offset);
extern int sched_init_task_load_open(struct inode *inode, struct file *filp);
extern int sched_group_id_show(struct seq_file *m, void *v);
extern ssize_t sched_group_id_write(struct file *file, const char __user *buf,
					size_t count, loff_t *offset);
extern int sched_group_id_open(struct inode *inode, struct file *filp);
extern int sched_pause_cpus(struct cpumask *pause_cpus);
extern int sched_unpause_cpus(struct cpumask *unpause_cpus);

extern unsigned int sched_get_cpu_util(int cpu);
extern void sched_update_hyst_times(void);
extern u64 sched_lpm_disallowed_time(int cpu);
extern int
sched_updown_migrate_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sched_boost_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sched_busy_hyst_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern u64 sched_ktime_clock(void);
extern void clear_walt_request(int cpu);
extern void walt_init_tg(struct task_group *tg);
extern void walt_init_topapp_tg(struct task_group *tg);
extern void walt_init_foreground_tg(struct task_group *tg);
extern int register_walt_callback(void);
extern void set_cpu_array(void);
extern int sched_init_ops(void);
extern int core_ctl_init(void);
extern void acquire_rq_locks_irqsave(const cpumask_t *cpus,
		unsigned long *flags);
extern void release_rq_locks_irqrestore(const cpumask_t *cpus,
		unsigned long *flags);
extern struct list_head cluster_head;
extern int set_sched_ravg_window(char *str);
extern int set_sched_predl(char *str);
extern int input_boost_init(void);
extern int core_ctl_init(void);

extern atomic64_t walt_irq_work_lastq_ws;
extern unsigned int __read_mostly sched_ravg_window;
extern unsigned int min_max_possible_capacity;
extern unsigned int max_possible_capacity;
extern unsigned int __read_mostly sched_init_task_load_windows;
extern unsigned int __read_mostly sched_load_granule;

/* 1ms default for 20ms window size scaled to 1024 */
extern unsigned int sysctl_sched_min_task_util_for_boost;
/* 0.68ms default for 20ms window size scaled to 1024 */
extern unsigned int sysctl_sched_min_task_util_for_colocation;
extern unsigned int sysctl_sched_busy_hyst_enable_cpus;
extern unsigned int sysctl_sched_busy_hyst;
extern unsigned int sysctl_sched_coloc_busy_hyst_enable_cpus;
extern unsigned int sysctl_sched_coloc_busy_hyst_cpu[WALT_NR_CPUS];
extern unsigned int sysctl_sched_coloc_busy_hyst_max_ms;
extern unsigned int sysctl_sched_coloc_busy_hyst_cpu_busy_pct[WALT_NR_CPUS];
extern unsigned int sysctl_sched_boost; /* To/from userspace */
extern unsigned int sysctl_sched_capacity_margin_up[MAX_MARGIN_LEVELS];
extern unsigned int sysctl_sched_capacity_margin_down[MAX_MARGIN_LEVELS];
extern unsigned int sched_boost_type; /* currently activated sched boost */
extern enum sched_boost_policy boost_policy;
extern unsigned int sysctl_input_boost_ms;
extern unsigned int sysctl_input_boost_freq[8];
extern unsigned int sysctl_sched_boost_on_input;
extern unsigned int sysctl_sched_load_boost[WALT_NR_CPUS];
extern unsigned int sysctl_sched_user_hint;
extern unsigned int sysctl_sched_conservative_pl;
#define WALT_MANY_WAKEUP_DEFAULT 1000
extern unsigned int sysctl_sched_many_wakeup_threshold;
extern unsigned int sysctl_walt_rtg_cfs_boost_prio;
extern __read_mostly unsigned int sysctl_sched_force_lb_enable;
extern const int sched_user_hint_max;
extern unsigned int sysctl_sched_prefer_spread;

#define for_each_sched_cluster(cluster) \
	list_for_each_entry_rcu(cluster, &cluster_head, list)

static inline u32 cpu_cycles_to_freq(u64 cycles, u64 period)
{
	return div64_u64(cycles, period);
}

static inline unsigned int sched_cpu_legacy_freq(int cpu)
{
	unsigned long curr_cap = arch_scale_freq_capacity(cpu);
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;

	return (curr_cap * (u64) wrq->cluster->max_possible_freq) >>
		SCHED_CAPACITY_SHIFT;
}

extern __read_mostly bool sched_freq_aggr_en;
static inline void walt_enable_frequency_aggregation(bool enable)
{
	sched_freq_aggr_en = enable;
}

#ifndef CONFIG_IRQ_TIME_ACCOUNTING
static inline u64 irq_time_read(int cpu) { return 0; }
#endif

/*Sysctl related interface*/
#define WINDOW_STATS_RECENT		0
#define WINDOW_STATS_MAX		1
#define WINDOW_STATS_MAX_RECENT_AVG	2
#define WINDOW_STATS_AVG		3
#define WINDOW_STATS_INVALID_POLICY	4

extern unsigned int __read_mostly sysctl_sched_coloc_downmigrate_ns;
extern unsigned int __read_mostly sysctl_sched_group_downmigrate_pct;
extern unsigned int __read_mostly sysctl_sched_group_upmigrate_pct;
extern unsigned int __read_mostly sysctl_sched_window_stats_policy;
extern unsigned int sysctl_sched_ravg_window_nr_ticks;
extern unsigned int sysctl_sched_dynamic_ravg_window_enable;
extern unsigned int sysctl_sched_walt_rotate_big_tasks;
extern unsigned int sysctl_sched_task_unfilter_period;
extern unsigned int __read_mostly sysctl_sched_asym_cap_sibling_freq_match_pct;
extern unsigned int sysctl_walt_low_latency_task_threshold; /* disabled by default */
extern struct ctl_table walt_table[];
extern struct ctl_table walt_base_table[];
extern void walt_tunables(void);
extern void walt_update_group_thresholds(void);
extern void sched_window_nr_ticks_change(void);
extern unsigned long sched_user_hint_reset_time;
extern struct irq_work walt_migration_irq_work;
extern __read_mostly unsigned int new_sched_ravg_window;
extern struct task_group *task_group_topapp;
extern struct task_group *task_group_foreground;

#define LIB_PATH_LENGTH 512
extern unsigned int cpuinfo_max_freq_cached;
extern char sched_lib_name[LIB_PATH_LENGTH];
extern unsigned int sched_lib_mask_force;
extern bool is_sched_lib_based_app(pid_t pid);
void android_vh_show_max_freq(void *unused, struct cpufreq_policy *policy,
				unsigned int *max_freq);

/* WALT cpufreq interface */
#define WALT_CPUFREQ_ROLLOVER		(1U << 0)
#define WALT_CPUFREQ_CONTINUE		(1U << 1)
#define WALT_CPUFREQ_IC_MIGRATION	(1U << 2)
#define WALT_CPUFREQ_PL			(1U << 3)
#define WALT_CPUFREQ_EARLY_DET		(1U << 4)

#define NO_BOOST 0
#define FULL_THROTTLE_BOOST 1
#define CONSERVATIVE_BOOST 2
#define RESTRAINED_BOOST 3
#define FULL_THROTTLE_BOOST_DISABLE -1
#define CONSERVATIVE_BOOST_DISABLE -2
#define RESTRAINED_BOOST_DISABLE -3
#define MAX_NUM_BOOST_TYPE (RESTRAINED_BOOST+1)

enum sched_boost_policy {
	SCHED_BOOST_NONE,
	SCHED_BOOST_ON_BIG,
	SCHED_BOOST_ON_ALL,
};

struct walt_task_group {
	/*
	 * Controls whether tasks of this cgroup should be colocated with each
	 * other and tasks of other cgroups that have the same flag turned on.
	 */
	bool colocate;
	/*
	 * array indicating whether this task group participates in the
	 * particular boost type
	 */
	bool sched_boost_enable[MAX_NUM_BOOST_TYPE];
};

struct sched_avg_stats {
	int nr;
	int nr_misfit;
	int nr_max;
	int nr_scaled;
};

struct waltgov_callback {
	void (*func)(struct waltgov_callback *cb, u64 time, unsigned int flags);
};

DECLARE_PER_CPU(struct waltgov_callback *, waltgov_cb_data);

static inline void waltgov_add_callback(int cpu, struct waltgov_callback *cb,
			void (*func)(struct waltgov_callback *cb, u64 time,
			unsigned int flags))
{
	if (WARN_ON(!cb || !func))
		return;

	if (WARN_ON(per_cpu(waltgov_cb_data, cpu)))
		return;

	cb->func = func;
	rcu_assign_pointer(per_cpu(waltgov_cb_data, cpu), cb);
}

static inline void waltgov_remove_callback(int cpu)
{
	rcu_assign_pointer(per_cpu(waltgov_cb_data, cpu), NULL);
}

static inline void waltgov_run_callback(struct rq *rq, unsigned int flags)
{
	struct waltgov_callback *cb;

	cb = rcu_dereference_sched(*per_cpu_ptr(&waltgov_cb_data, cpu_of(rq)));
	if (cb)
		cb->func(cb, sched_ktime_clock(), flags);
}

extern unsigned long cpu_util_freq_walt(int cpu, struct walt_cpu_load *walt_load);
int waltgov_register(void);

extern void walt_lb_init(void);
extern unsigned int walt_rotation_enabled;

/*
 * Returns the current capacity of cpu after applying both
 * cpu and freq scaling.
 */
static inline unsigned long capacity_curr_of(int cpu)
{
	unsigned long max_cap = cpu_rq(cpu)->cpu_capacity_orig;
	unsigned long scale_freq = arch_scale_freq_capacity(cpu);

	return cap_scale(max_cap, scale_freq);
}

static inline unsigned long task_util(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->demand_scaled;
}

static inline unsigned long cpu_util(int cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;
	u64 walt_cpu_util = wrq->walt_stats.cumulative_runnable_avg_scaled;

	return min_t(unsigned long, walt_cpu_util, capacity_orig_of(cpu));
}

static inline unsigned long cpu_util_cum(int cpu, int delta)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;
	u64 util = wrq->cum_window_demand_scaled;
	unsigned long capacity = capacity_orig_of(cpu);

	delta += util;
	if (delta < 0)
		return 0;

	return (delta >= capacity) ? capacity : delta;
}

extern unsigned int capacity_margin_freq;

static inline unsigned long
add_capacity_margin(unsigned long cpu_capacity, int cpu)
{
	cpu_capacity = cpu_capacity * capacity_margin_freq *
			(100 + sysctl_sched_load_boost[cpu]);
	cpu_capacity /= 100;
	cpu_capacity /= SCHED_CAPACITY_SCALE;
	return cpu_capacity;
}

static inline enum sched_boost_policy sched_boost_policy(void)
{
	return boost_policy;
}

static inline int sched_boost(void)
{
	return sched_boost_type;
}

static inline bool rt_boost_on_big(void)
{
	return sched_boost() == FULL_THROTTLE_BOOST ?
			(sched_boost_policy() == SCHED_BOOST_ON_BIG) : false;
}

static inline bool is_full_throttle_boost(void)
{
	return sched_boost() == FULL_THROTTLE_BOOST;
}

static inline bool task_sched_boost(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	struct task_group *tg;
	bool sched_boost_enabled;
	struct walt_task_group *wtg;

	/* optimization for FT boost, skip looking at tg */
	if (sched_boost() == FULL_THROTTLE_BOOST)
		return true;

	rcu_read_lock();
	css = task_css(p, cpu_cgrp_id);
	if (!css) {
		rcu_read_unlock();
		return false;
	}
	tg = container_of(css, struct task_group, css);
	wtg = (struct walt_task_group *) tg->android_vendor_data1;
	sched_boost_enabled = wtg->sched_boost_enable[sched_boost()];
	rcu_read_unlock();

	return sched_boost_enabled;
}

static inline bool task_placement_boost_enabled(struct task_struct *p)
{
	if (likely(sched_boost_policy() == SCHED_BOOST_NONE))
		return false;

	return task_sched_boost(p);
}

static inline enum sched_boost_policy task_boost_policy(struct task_struct *p)
{
	enum sched_boost_policy policy;

	if (likely(sched_boost_policy() == SCHED_BOOST_NONE))
		return SCHED_BOOST_NONE;

	policy = task_sched_boost(p) ? sched_boost_policy() : SCHED_BOOST_NONE;
	if (policy == SCHED_BOOST_ON_BIG) {
		/*
		 * Filter out tasks less than min task util threshold
		 * under conservative boost.
		 */
		if (sched_boost() == CONSERVATIVE_BOOST &&
			task_util(p) <= sysctl_sched_min_task_util_for_boost)
			policy = SCHED_BOOST_NONE;
	}

	return policy;
}

static inline unsigned long capacity_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

static inline bool __cpu_overutilized(int cpu, int delta)
{
	return (capacity_orig_of(cpu) * 1024) <
		((cpu_util(cpu) + delta) * sched_capacity_margin_up[cpu]);
}

static inline bool cpu_overutilized(int cpu)
{
	return __cpu_overutilized(cpu, 0);
}

static inline int asym_cap_siblings(int cpu1, int cpu2)
{
	return (cpumask_test_cpu(cpu1, &asym_cap_sibling_cpus) &&
		cpumask_test_cpu(cpu2, &asym_cap_sibling_cpus));
}

static inline bool asym_cap_sibling_group_has_capacity(int dst_cpu, int margin)
{
	int sib1, sib2;
	int nr_running;
	unsigned long total_util, total_capacity;

	if (cpumask_empty(&asym_cap_sibling_cpus) ||
			cpumask_test_cpu(dst_cpu, &asym_cap_sibling_cpus))
		return false;

	sib1 = cpumask_first(&asym_cap_sibling_cpus);
	sib2 = cpumask_last(&asym_cap_sibling_cpus);

	if (!cpu_active(sib1) || !cpu_active(sib2))
		return false;

	nr_running = cpu_rq(sib1)->cfs.h_nr_running +
			cpu_rq(sib2)->cfs.h_nr_running;

	if (nr_running <= 2)
		return true;

	total_capacity = capacity_of(sib1) + capacity_of(sib2);
	total_util = cpu_util(sib1) + cpu_util(sib2);

	return ((total_capacity * 100) > (total_util * margin));
}

/* Is frequency of two cpus synchronized with each other? */
static inline int same_freq_domain(int src_cpu, int dst_cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(src_cpu)->android_vendor_data1;

	if (src_cpu == dst_cpu)
		return 1;

	if (asym_cap_siblings(src_cpu, dst_cpu))
		return 1;

	return cpumask_test_cpu(dst_cpu, &wrq->freq_domain_cpumask);
}

static inline unsigned long task_util_est(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->demand_scaled;
}

#ifdef CONFIG_UCLAMP_TASK
static inline unsigned long uclamp_task_util(struct task_struct *p)
{
	return clamp(task_util_est(p),
		     uclamp_eff_value(p, UCLAMP_MIN),
		     uclamp_eff_value(p, UCLAMP_MAX));
}
#else
static inline unsigned long uclamp_task_util(struct task_struct *p)
{
	return task_util_est(p);
}
#endif

static inline int per_task_boost(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	if (wts->boost_period) {
		if (sched_clock() > wts->boost_expires) {
			wts->boost_period = 0;
			wts->boost_expires = 0;
			wts->boost = 0;
		}
	}
	return wts->boost;
}

static inline int cluster_first_cpu(struct walt_sched_cluster *cluster)
{
	return cpumask_first(&cluster->cpus);
}

static inline bool hmp_capable(void)
{
	return max_possible_capacity != min_max_possible_capacity;
}

static inline bool is_max_capacity_cpu(int cpu)
{
	return arch_scale_cpu_capacity(cpu) == max_possible_capacity;
}

static inline bool is_min_capacity_cpu(int cpu)
{
	return arch_scale_cpu_capacity(cpu) == min_max_possible_capacity;
}

static inline bool is_min_capacity_cluster(struct walt_sched_cluster *cluster)
{
	return is_min_capacity_cpu(cluster_first_cpu(cluster));
}

/*
 * This is only for tracepoints to print the avg irq load. For
 * task placment considerations, use sched_cpu_high_irqload().
 */
#define SCHED_HIGH_IRQ_TIMEOUT 3
static inline u64 sched_irqload(int cpu)
{
	s64 delta;
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;

	delta = wrq->window_start - wrq->last_irq_window;
	if (delta < SCHED_HIGH_IRQ_TIMEOUT)
		return wrq->avg_irqload;
	else
		return 0;
}

static inline int sched_cpu_high_irqload(int cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;

	return wrq->high_irqload;
}

static inline u64
scale_load_to_freq(u64 load, unsigned int src_freq, unsigned int dst_freq)
{
	return div64_u64(load * (u64)src_freq, (u64)dst_freq);
}

static inline unsigned int max_task_load(void)
{
	return sched_ravg_window;
}

static inline int same_cluster(int src_cpu, int dst_cpu)
{
	struct walt_rq *src_wrq = (struct walt_rq *) cpu_rq(src_cpu)->android_vendor_data1;
	struct walt_rq *dest_wrq = (struct walt_rq *) cpu_rq(dst_cpu)->android_vendor_data1;

	return src_wrq->cluster == dest_wrq->cluster;
}

static inline bool is_suh_max(void)
{
	return sysctl_sched_user_hint == sched_user_hint_max;
}

#define DEFAULT_CGROUP_COLOC_ID 1
static inline bool walt_should_kick_upmigrate(struct task_struct *p, int cpu)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	struct walt_related_thread_group *rtg = wts->grp;

	if (is_suh_max() && rtg && rtg->id == DEFAULT_CGROUP_COLOC_ID &&
			    rtg->skip_min && wts->unfilter)
		return is_min_capacity_cpu(cpu);

	return false;
}

extern bool is_rtgb_active(void);
extern u64 get_rtgb_active_time(void);

static inline unsigned int walt_nr_rtg_high_prio(int cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;

	return wrq->walt_stats.nr_rtg_high_prio_tasks;
}

static inline bool task_fits_capacity(struct task_struct *p,
					long capacity,
					int cpu)
{
	unsigned int margin;

	/*
	 * Derive upmigration/downmigrate margin wrt the src/dest CPU.
	 */
	if (capacity_orig_of(task_cpu(p)) > capacity_orig_of(cpu))
		margin = sched_capacity_margin_down[cpu];
	else
		margin = sched_capacity_margin_up[task_cpu(p)];

	return capacity * 1024 > uclamp_task_util(p) * margin;
}

static inline bool task_fits_max(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long max_capacity = max_possible_capacity;
	unsigned long task_boost = per_task_boost(p);

	if (capacity == max_capacity)
		return true;

	if (is_min_capacity_cpu(cpu)) {
		if (task_boost_policy(p) == SCHED_BOOST_ON_BIG ||
				task_boost > 0 ||
				uclamp_boosted(p) ||
				walt_should_kick_upmigrate(p, cpu))
			return false;
	} else { /* mid cap cpu */
		if (task_boost > TASK_BOOST_ON_MID)
			return false;
	}

	return task_fits_capacity(p, capacity, cpu);
}

/* applying the task threshold for all types of low latency tasks. */
static inline bool walt_low_latency_task(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->low_latency &&
		(task_util(p) < sysctl_walt_low_latency_task_threshold);
}

static inline unsigned int walt_get_idle_exit_latency(struct rq *rq)
{
	struct cpuidle_state *idle = idle_get_state(rq);

	if (idle)
		return idle->exit_latency;

	return UINT_MAX;
}

extern void sched_get_nr_running_avg(struct sched_avg_stats *stats);
extern void sched_update_hyst_times(void);

extern enum sched_boost_policy sched_boost_policy(void);
extern void walt_rt_init(void);
extern void walt_cfs_init(void);
extern int walt_find_energy_efficient_cpu(struct task_struct *p, int prev_cpu,
					int sync, int sibling_count_hint);

static inline unsigned int cpu_max_possible_freq(int cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;

	return wrq->cluster->max_possible_freq;
}

static inline unsigned int cpu_max_freq(int cpu)
{
	return mult_frac(cpu_max_possible_freq(cpu), capacity_orig_of(cpu),
			 arch_scale_cpu_capacity(cpu));
}

static inline unsigned int task_load(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->demand;
}

static inline unsigned int task_pl(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->pred_demand;
}

static inline bool task_in_related_thread_group(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return (rcu_access_pointer(wts->grp) != NULL);
}

static inline bool task_rtg_high_prio(struct task_struct *p)
{
	return task_in_related_thread_group(p) &&
		(p->prio <= sysctl_walt_rtg_cfs_boost_prio);
}

static inline struct walt_related_thread_group
*task_related_thread_group(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return rcu_dereference(wts->grp);
}

#define CPU_RESERVED 1
static inline int is_reserved(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	return test_bit(CPU_RESERVED, &wrq->walt_flags);
}

static inline int mark_reserved(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	return test_and_set_bit(CPU_RESERVED, &wrq->walt_flags);
}

static inline void clear_reserved(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	clear_bit(CPU_RESERVED, &wrq->walt_flags);
}

static inline bool
task_in_cum_window_demand(struct rq *rq, struct task_struct *p)
{
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return cpu_of(rq) == task_cpu(p) && (p->on_rq ||
		wts->last_sleep_ts >= wrq->window_start);
}

static inline void walt_fixup_cum_window_demand(struct rq *rq, s64 scaled_delta)
{
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	wrq->cum_window_demand_scaled += scaled_delta;
	if (unlikely((s64)wrq->cum_window_demand_scaled < 0))
		wrq->cum_window_demand_scaled = 0;
}

static inline void walt_irq_work_queue(struct irq_work *work)
{
	if (likely(cpu_online(raw_smp_processor_id())))
		irq_work_queue(work);
	else
		irq_work_queue_on(work, cpumask_any(cpu_online_mask));
}

#define PF_WAKE_UP_IDLE	1
static inline u32 sched_get_wake_up_idle(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->wake_up_idle;
}

static inline int sched_set_wake_up_idle(struct task_struct *p,
						int wake_up_idle)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	wts->wake_up_idle = !!wake_up_idle;
	return 0;
}

static inline void set_wake_up_idle(bool enabled)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) current->android_vendor_data1;

	wts->wake_up_idle = enabled;
}

extern int set_task_boost(int boost, u64 period);

static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

#endif /* _WALT_H */
