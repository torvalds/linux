/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _WALT_H
#define _WALT_H

#include <linux/pm_qos.h>
#include <linux/sched/cputime.h>
#include "../../../kernel/sched/sched.h"
#include "../../../fs/proc/internal.h"
#include <linux/sched/walt.h>
#include <linux/jump_label.h>

#include <linux/cgroup.h>

#include <uapi/linux/sched/types.h>
#include <linux/cpuidle.h>
#include <linux/sched/clock.h>
#include <trace/hooks/cgroup.h>

#define MSEC_TO_NSEC (1000 * 1000)

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

/* MAX_MARGIN_LEVELS should be one less than MAX_CLUSTERS */
#define MAX_MARGIN_LEVELS (MAX_CLUSTERS - 1)

extern bool walt_disabled;
extern bool waltgov_disabled;

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

enum pipeline_types {
	MANUAL_PIPELINE,
	AUTO_PIPELINE,
	MAX_PIPELINE_TYPES,
};

enum freq_caps {
	PARTIAL_HALT_CAP,
	SMART_FMAX_CAP,
	HIGH_PERF_CAP,
	FREQ_REL_CAP,
	MAX_FREQ_CAP,
};

#define WALT_LOW_LATENCY_PROCFS		BIT(0)
#define WALT_LOW_LATENCY_BINDER		BIT(1)
#define WALT_LOW_LATENCY_PIPELINE	BIT(2)
#define WALT_LOW_LATENCY_HEAVY		BIT(3)

#define WALT_LOW_LATENCY_MASK		(WALT_LOW_LATENCY_PIPELINE|WALT_LOW_LATENCY_HEAVY)

struct walt_cpu_load {
	unsigned long	nl;
	unsigned long	pl;
	bool		rtgb_active;
	u64		ws;
	bool		ed_active;
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
	u64			util;
	/* MVP */
	struct list_head	mvp_tasks;
	int                     num_mvp_tasks;
	u64			mvp_arrival_time; /* ts when 1st mvp task selected on this cpu */
	u64			mvp_throttle_time; /* ts when mvp were throttled */
	bool			skip_mvp;

	u64			latest_clock;
	u32			enqueue_counter;
};

DECLARE_PER_CPU(struct walt_rq, walt_rq);

struct walt_sched_cluster {
	raw_spinlock_t		load_lock;
	struct list_head	list;
	struct cpumask		cpus;
	int			id;
	/*
	 * max_possible_freq = maximum supported by hardware
	 * max_freq = max freq as per cpufreq limits
	 */
	unsigned int		cur_freq;
	unsigned int		max_possible_freq;
	unsigned int		max_freq;
	unsigned int		walt_internal_freq_limit;
	u64			aggr_grp_load;
	unsigned long		util_to_cost[1024];
	u64			found_ts;
	int8_t			sibling_cluster;
};

struct freq_relation_map {
	unsigned int src_freq;
	unsigned int tgt_freq;
	int target_cluster_cpu;
};
#define MAX_FREQ_RELATIONS     10
#define TUPLE_SIZE     3

extern struct walt_sched_cluster *sched_cluster[WALT_NR_CPUS];
extern cpumask_t part_haltable_cpus;
extern cpumask_t cpus_paused_by_us;
extern cpumask_t cpus_part_paused_by_us;
/*END SCHED.H PORT*/

extern int num_sched_clusters;
extern int nr_big_cpus;
extern unsigned int sched_capacity_margin_up[WALT_NR_CPUS];
extern unsigned int sched_capacity_margin_down[WALT_NR_CPUS];
extern cpumask_t asym_cap_sibling_cpus;
extern cpumask_t __read_mostly **cpu_array;
extern int cpu_l2_sibling[WALT_NR_CPUS];
extern void sched_update_nr_prod(int cpu, int enq);
extern unsigned int walt_big_tasks(int cpu);
extern void walt_rotation_checkpoint(int nr_big);
extern void fmax_uncap_checkpoint(int nr_big, u64 window_start, u32 wakeup_ctr_sum);
extern void walt_fill_ta_data(struct core_ctl_notif_data *data);
extern int sched_set_group_id(struct task_struct *p, unsigned int group_id);
extern unsigned int sched_get_group_id(struct task_struct *p);
extern void core_ctl_check(u64 wallclock, u32 wakeup_ctr_sum);
extern int core_ctl_set_cluster_boost(int idx, bool boost);
extern int sched_set_boost(int enable);
extern void walt_boost_init(void);
extern int sched_pause_cpus(struct cpumask *pause_cpus);
extern int sched_unpause_cpus(struct cpumask *unpause_cpus);
extern void walt_kick_cpu(int cpu);
extern void walt_smp_call_newidle_balance(int cpu);
extern bool cpus_halted_by_client(struct cpumask *cpu, enum pause_client client);

extern unsigned int sched_get_cpu_util_pct(int cpu);
extern void sched_update_hyst_times(void);
extern int sched_boost_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern int sched_busy_hyst_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern u64 walt_sched_clock(void);
extern void walt_init_tg(struct task_group *tg);
extern void walt_init_topapp_tg(struct task_group *tg);
extern void walt_init_foreground_tg(struct task_group *tg);
extern int register_walt_callback(void);
extern int input_boost_init(void);
extern int core_ctl_init(void);
extern void rebuild_sched_domains(void);
void update_freq_relation(struct walt_sched_cluster *cluster);

extern atomic64_t walt_irq_work_lastq_ws;
extern unsigned int __read_mostly sched_ravg_window;
extern int min_possible_cluster_id;
extern int max_possible_cluster_id;
extern unsigned int __read_mostly sched_init_task_load_windows;
extern unsigned int __read_mostly sched_load_granule;

#define SCHED_IDLE_ENOUGH_DEFAULT 30
#define SCHED_CLUSTER_UTIL_THRES_PCT_DEFAULT 40

extern unsigned int sysctl_sched_idle_enough_clust[MAX_CLUSTERS];
extern unsigned int sysctl_sched_cluster_util_thres_pct_clust[MAX_CLUSTERS];

/* 1ms default for 20ms window size scaled to 1024 */
extern unsigned int sysctl_sched_min_task_util_for_boost;
extern unsigned int sysctl_sched_min_task_util_for_uclamp;
/* 0.68ms default for 20ms window size scaled to 1024 */
extern unsigned int sysctl_sched_min_task_util_for_colocation;
extern unsigned int sysctl_sched_busy_hyst_enable_cpus;
extern unsigned int sysctl_sched_busy_hyst;
extern unsigned int sysctl_sched_coloc_busy_hyst_enable_cpus;
extern unsigned int sysctl_sched_coloc_busy_hyst_cpu[WALT_NR_CPUS];
extern unsigned int sysctl_sched_coloc_busy_hyst_max_ms;
extern unsigned int sysctl_sched_coloc_busy_hyst_cpu_busy_pct[WALT_NR_CPUS];
extern unsigned int sysctl_sched_util_busy_hyst_enable_cpus;
extern unsigned int sysctl_sched_util_busy_hyst_cpu[WALT_NR_CPUS];
extern unsigned int sysctl_sched_util_busy_hyst_cpu_util[WALT_NR_CPUS];
extern unsigned int sysctl_sched_boost; /* To/from userspace */
extern unsigned int sysctl_sched_capacity_margin_up[MAX_MARGIN_LEVELS];
extern unsigned int sysctl_sched_capacity_margin_down[MAX_MARGIN_LEVELS];
extern unsigned int sched_boost_type; /* currently activated sched boost */
extern enum sched_boost_policy boost_policy;
extern unsigned int sysctl_input_boost_ms;
extern unsigned int sysctl_input_boost_freq[8];
extern unsigned int sysctl_sched_boost_on_input;
extern unsigned int sysctl_sched_user_hint;
extern unsigned int sysctl_sched_conservative_pl;
extern unsigned int sysctl_sched_hyst_min_coloc_ns;
extern unsigned int sysctl_sched_long_running_rt_task_ms;
extern unsigned int sysctl_ed_boost_pct;
extern unsigned int sysctl_em_inflate_pct;
extern unsigned int sysctl_em_inflate_thres;
extern unsigned int sysctl_sched_heavy_nr;

extern int cpufreq_walt_set_adaptive_freq(unsigned int cpu, unsigned int adaptive_low_freq,
					  unsigned int adaptive_high_freq);
extern int cpufreq_walt_get_adaptive_freq(unsigned int cpu, unsigned int *adaptive_low_freq,
					  unsigned int *adaptive_high_freq);
extern int cpufreq_walt_reset_adaptive_freq(unsigned int cpu);

#define WALT_MANY_WAKEUP_DEFAULT 1000
extern unsigned int sysctl_sched_many_wakeup_threshold;
extern unsigned int sysctl_walt_rtg_cfs_boost_prio;
extern __read_mostly unsigned int sysctl_sched_force_lb_enable;
extern const int sched_user_hint_max;
extern unsigned int sysctl_sched_dynamic_tp_enable;
extern unsigned int sysctl_panic_on_walt_bug;
extern unsigned int sysctl_max_freq_partial_halt;
extern unsigned int sysctl_fmax_cap[MAX_CLUSTERS];
extern unsigned int high_perf_cluster_freq_cap[MAX_CLUSTERS];
extern unsigned int fmax_cap[MAX_FREQ_CAP][MAX_CLUSTERS];
extern int sched_dynamic_tp_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos);
extern struct freq_relation_map relation_data[MAX_CLUSTERS][MAX_FREQ_RELATIONS];
extern struct list_head cluster_head;
#define for_each_sched_cluster(cluster) \
	list_for_each_entry_rcu(cluster, &cluster_head, list)

static inline struct walt_sched_cluster *cpu_cluster(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return wrq->cluster;
}

static inline u32 cpu_cycles_to_freq(u64 cycles, u64 period)
{
	return div64_u64(cycles, period);
}

static inline unsigned int sched_cpu_legacy_freq(int cpu)
{
	unsigned long curr_cap = arch_scale_freq_capacity(cpu);
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

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
extern unsigned int sysctl_sched_walt_rotate_big_tasks;
extern unsigned int sysctl_sched_task_unfilter_period;
extern unsigned int sysctl_walt_low_latency_task_threshold; /* disabled by default */
extern unsigned int sysctl_sched_sync_hint_enable;
extern unsigned int sysctl_sched_suppress_region2;
extern unsigned int sysctl_sched_skip_sp_newly_idle_lb;
extern unsigned int sysctl_sched_asymcap_boost;
extern struct ctl_table walt_table[];
extern struct ctl_table walt_base_table[];
extern void walt_tunables(void);
extern void walt_update_group_thresholds(void);
extern void sched_window_nr_ticks_change(void);
extern unsigned long sched_user_hint_reset_time;
extern struct irq_work walt_migration_irq_work;

#define LIB_PATH_LENGTH 512
extern unsigned int cpuinfo_max_freq_cached;
extern char sched_lib_name[LIB_PATH_LENGTH];
extern unsigned int sched_lib_mask_force;

extern cpumask_t cpus_for_sbt_pause;
extern unsigned int sysctl_sched_sbt_enable;
extern unsigned int sysctl_sched_sbt_delay_windows;

extern cpumask_t cpus_for_pipeline;

/* WALT cpufreq interface */
#define WALT_CPUFREQ_ROLLOVER		0x1
#define WALT_CPUFREQ_CONTINUE		0x2
#define WALT_CPUFREQ_IC_MIGRATION	0x4
#define WALT_CPUFREQ_PL			0x8
#define WALT_CPUFREQ_EARLY_DET		0x10
#define WALT_CPUFREQ_BOOST_UPDATE	0x20
#define WALT_CPUFREQ_ASYM_FIXUP		0x40
#define WALT_CPUFREQ_SHARED_RAIL	0x80

#define CPUFREQ_REASON_LOAD		0
#define CPUFREQ_REASON_BTR		0x1
#define CPUFREQ_REASON_PL		0x2
#define CPUFREQ_REASON_EARLY_DET	0x4
#define CPUFREQ_REASON_RTG_BOOST	0x8
#define CPUFREQ_REASON_HISPEED		0x10
#define CPUFREQ_REASON_NWD		0x20
#define CPUFREQ_REASON_FREQ_AGR		0x40
#define CPUFREQ_REASON_KSOFTIRQD	0x80
#define CPUFREQ_REASON_TT_LOAD		0x100
#define CPUFREQ_REASON_SUH		0x200
#define CPUFREQ_REASON_ADAPTIVE_LOW	0x400
#define CPUFREQ_REASON_ADAPTIVE_HIGH	0x800
#define CPUFREQ_REASON_SMART_FMAX_CAP	0x1000
#define CPUFREQ_REASON_HIGH_PERF_CAP	0x2000
#define CPUFREQ_REASON_PARTIAL_HALT_CAP	0x4000
#define CPUFREQ_REASON_FREQ_REL_CAP	0x8000

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
		cb->func(cb, walt_sched_clock(), flags);
}

extern unsigned long cpu_util_freq_walt(int cpu, struct walt_cpu_load *walt_load,
		unsigned int *reason);
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
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);
	u64 walt_cpu_util = wrq->walt_stats.cumulative_runnable_avg_scaled;

	return min_t(unsigned long, walt_cpu_util, capacity_orig_of(cpu));
}

static inline unsigned long cpu_util_cum(int cpu)
{
	return READ_ONCE(cpu_rq(cpu)->cfs.avg.util_avg);
}

/* applying the task threshold for all types of low latency tasks. */
static inline bool walt_low_latency_task(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	if (!wts->low_latency)
		return false;

	if (wts->low_latency & WALT_LOW_LATENCY_MASK)
		return true;

	/* WALT_LOW_LATENCY_BINDER and WALT_LOW_LATENCY_PROCFS remain */
	return (task_util(p) < sysctl_walt_low_latency_task_threshold);
}

static inline bool walt_binder_low_latency_task(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return (wts->low_latency & WALT_LOW_LATENCY_BINDER) &&
		(task_util(p) < sysctl_walt_low_latency_task_threshold);
}

static inline bool walt_procfs_low_latency_task(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return (wts->low_latency & WALT_LOW_LATENCY_PROCFS) &&
		(task_util(p) < sysctl_walt_low_latency_task_threshold);
}

static inline bool walt_pipeline_low_latency_task(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return wts->low_latency & WALT_LOW_LATENCY_MASK;
}

static inline unsigned int walt_get_idle_exit_latency(struct rq *rq)
{
	struct cpuidle_state *idle = idle_get_state(rq);

	if (idle)
		return idle->exit_latency;

	return 0; /* CPU is not idle */
}

static inline bool rt_boost_on_big(void)
{
	return sched_boost_type == FULL_THROTTLE_BOOST ?
			(boost_policy == SCHED_BOOST_ON_BIG) : false;
}

static inline bool is_full_throttle_boost(void)
{
	return sched_boost_type == FULL_THROTTLE_BOOST;
}

static inline bool is_storage_boost(void)
{
	return sched_boost_type == STORAGE_BOOST;
}

static inline bool task_sched_boost(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	struct task_group *tg;
	bool sched_boost_enabled;
	struct walt_task_group *wtg;

	/* optimization for FT boost, skip looking at tg */
	if (sched_boost_type == FULL_THROTTLE_BOOST)
		return true;

	rcu_read_lock();
	css = task_css(p, cpu_cgrp_id);
	if (!css) {
		rcu_read_unlock();
		return false;
	}
	tg = container_of(css, struct task_group, css);
	wtg = (struct walt_task_group *) tg->android_vendor_data1;
	sched_boost_enabled = wtg->sched_boost_enable[sched_boost_type];
	rcu_read_unlock();

	return sched_boost_enabled;
}

static inline bool task_placement_boost_enabled(struct task_struct *p)
{
	if (likely(boost_policy == SCHED_BOOST_NONE))
		return false;

	return task_sched_boost(p);
}

static inline enum sched_boost_policy task_boost_policy(struct task_struct *p)
{
	enum sched_boost_policy policy;

	if (likely(boost_policy == SCHED_BOOST_NONE))
		return SCHED_BOOST_NONE;

	policy = task_sched_boost(p) ? boost_policy : SCHED_BOOST_NONE;
	if (policy == SCHED_BOOST_ON_BIG) {
		/*
		 * Filter out tasks less than min task util threshold
		 * under conservative boost.
		 */
		if (sched_boost_type == CONSERVATIVE_BOOST &&
			task_util(p) <= sysctl_sched_min_task_util_for_boost &&
			!walt_pipeline_low_latency_task(p))
			policy = SCHED_BOOST_NONE;
	}

	return policy;
}

static inline bool walt_uclamp_boosted(struct task_struct *p)
{
	return ((uclamp_eff_value(p, UCLAMP_MIN) > 0) &&
			(task_util(p) > sysctl_sched_min_task_util_for_uclamp));
}

static inline unsigned long capacity_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

static inline bool __cpu_overutilized(int cpu, int delta)
{
	unsigned long cap = capacity_orig_of(cpu);

	return cap ? ((cap * 1024) <
			((cpu_util(cpu) + delta) * sched_capacity_margin_up[cpu])) : true;
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

/* Is frequency of two cpus synchronized with each other? */
static inline int same_freq_domain(int src_cpu, int dst_cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, src_cpu);

	if (src_cpu == dst_cpu)
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
		if (walt_sched_clock() > wts->boost_expires) {
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
	return max_possible_cluster_id != min_possible_cluster_id;
}

static inline bool is_max_possible_cluster_cpu(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return wrq->cluster->id == max_possible_cluster_id;
}

static inline bool is_min_possible_cluster_cpu(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return wrq->cluster->id == min_possible_cluster_id;
}

static inline bool is_min_capacity_cluster(struct walt_sched_cluster *cluster)
{
	return cluster->id == min_possible_cluster_id;
}

/*
 * This is only for tracepoints to print the avg irq load. For
 * task placment considerations, use sched_cpu_high_irqload().
 */
#define SCHED_HIGH_IRQ_TIMEOUT 3
static inline u64 sched_irqload(int cpu)
{
	s64 delta;
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	delta = wrq->window_start - wrq->last_irq_window;
	if (delta < SCHED_HIGH_IRQ_TIMEOUT)
		return wrq->avg_irqload;
	else
		return 0;
}

static inline int sched_cpu_high_irqload(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

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
	struct walt_rq *src_wrq = &per_cpu(walt_rq, src_cpu);
	struct walt_rq *dest_wrq = &per_cpu(walt_rq, dst_cpu);

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
		return is_min_possible_cluster_cpu(cpu);

	return false;
}

extern bool is_rtgb_active(void);
extern u64 get_rtgb_active_time(void);

static inline unsigned int walt_nr_rtg_high_prio(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return wrq->walt_stats.nr_rtg_high_prio_tasks;
}

static inline bool task_in_related_thread_group(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return (rcu_access_pointer(wts->grp) != NULL);
}

bool walt_halt_check_last(int cpu);
extern struct cpumask __cpu_halt_mask;
extern struct cpumask __cpu_partial_halt_mask;

#define cpu_halt_mask ((struct cpumask *)&__cpu_halt_mask)
#define cpu_partial_halt_mask ((struct cpumask *)&__cpu_partial_halt_mask)

/* a halted cpu must NEVER be used for tasks, as this is the thermal indication to avoid a cpu */
#define cpu_halted(cpu) cpumask_test_cpu((cpu), cpu_halt_mask)

/* a partially halted may be used for helping smaller cpus with small tasks */
#define cpu_partial_halted(cpu) cpumask_test_cpu((cpu), cpu_partial_halt_mask)

#define ASYMCAP_BOOST(cpu)	(sysctl_sched_asymcap_boost && \
				!is_min_possible_cluster_cpu(cpu) && \
				!cpu_partial_halted(cpu))

static bool check_for_higher_capacity(int cpu1, int cpu2)
{
	if (cpu_partial_halted(cpu1) && is_min_possible_cluster_cpu(cpu2))
		return false;

	if (is_min_possible_cluster_cpu(cpu1) && cpu_partial_halted(cpu2))
		return false;

	return capacity_orig_of(cpu1) > capacity_orig_of(cpu2);
}

/* Migration margins for topapp */
extern unsigned int sched_capacity_margin_early_up[WALT_NR_CPUS];
extern unsigned int sched_capacity_margin_early_down[WALT_NR_CPUS];
static inline bool task_fits_capacity(struct task_struct *p,
					int dst_cpu)
{
	unsigned int margin;
	unsigned long capacity = capacity_orig_of(dst_cpu);

	/*
	 * Derive upmigration/downmigrate margin wrt the src/dest CPU.
	 */
	if (check_for_higher_capacity(task_cpu(p), dst_cpu)) {
		margin = sched_capacity_margin_down[dst_cpu];
		if (task_in_related_thread_group(p)) {
			margin = max(margin, sched_capacity_margin_early_down[dst_cpu]);
		}
	} else {
		margin = sched_capacity_margin_up[task_cpu(p)];
		if (task_in_related_thread_group(p)) {
			margin = max(margin, sched_capacity_margin_early_up[task_cpu(p)]);
		}
	}

	return capacity * 1024 > uclamp_task_util(p) * margin;
}

static inline bool task_fits_max(struct task_struct *p, int dst_cpu)
{
	unsigned long task_boost = per_task_boost(p);

	if (is_max_possible_cluster_cpu(dst_cpu))
		return true;

	if (is_min_possible_cluster_cpu(dst_cpu)) {
		if (task_boost_policy(p) == SCHED_BOOST_ON_BIG ||
				task_boost > 0 ||
				walt_uclamp_boosted(p) ||
				walt_should_kick_upmigrate(p, dst_cpu))
			return false;
	} else { /* mid cap cpu */
		if (task_boost > TASK_BOOST_ON_MID)
			return false;
		if (!task_in_related_thread_group(p) && p->prio >= 124)
			/* a non topapp low prio task fits on gold */
			return true;
	}

	return task_fits_capacity(p, dst_cpu);
}

extern struct sched_avg_stats *sched_get_nr_running_avg(void);
extern unsigned int sched_get_cluster_util_pct(struct walt_sched_cluster *cluster);
extern void sched_update_hyst_times(void);

extern void walt_rt_init(void);
extern void walt_cfs_init(void);
extern void walt_halt_init(void);
extern void walt_mvp_lock_ordering_init(void);
extern void walt_fixup_init(void);
extern int walt_find_energy_efficient_cpu(struct task_struct *p, int prev_cpu,
					int sync, int sibling_count_hint);
extern int walt_find_cluster_packing_cpu(int start_cpu);
extern bool walt_choose_packing_cpu(int packing_cpu, struct task_struct *p);

static inline unsigned int cpu_max_possible_freq(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

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

static inline bool walt_get_rtg_status(struct task_struct *p)
{
	struct walt_related_thread_group *grp;
	bool ret = false;

	rcu_read_lock();

	grp = task_related_thread_group(p);
	if (grp)
		ret = grp->skip_min;

	rcu_read_unlock();

	return ret;
}

#define CPU_RESERVED 1
static inline int is_reserved(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return test_bit(CPU_RESERVED, &wrq->walt_flags);
}

static inline int mark_reserved(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return test_and_set_bit(CPU_RESERVED, &wrq->walt_flags);
}

static inline void clear_reserved(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	clear_bit(CPU_RESERVED, &wrq->walt_flags);
}

static inline void walt_irq_work_queue(struct irq_work *work)
{
	if (likely(cpu_online(raw_smp_processor_id())))
		irq_work_queue(work);
	else
		irq_work_queue_on(work, cpumask_any(cpu_online_mask));
}

static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

/*
 * The policy of a RT boosted task (via PI mutex) still indicates it is
 * a fair task, so use prio check as well. The prio check alone is not
 * sufficient since idle task also has 120 priority.
 */
static inline bool walt_fair_task(struct task_struct *p)
{
	return p->prio >= MAX_RT_PRIO && !is_idle_task(p);
}

extern int sched_long_running_rt_task_ms_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);

static inline void walt_flag_set(struct task_struct *p, enum walt_flags feature, bool set)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	if (set)
		wts->flags |= 1 << feature;
	else
		wts->flags &= ~(1 << feature);
}

static inline bool walt_flag_test(struct task_struct *p, enum walt_flags feature)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return !!(wts->flags & (1 << feature));
}

#define WALT_MVP_SLICE		3000000U
#define WALT_MVP_LIMIT		(4 * WALT_MVP_SLICE)

/* higher number, better priority */
#define WALT_RTG_MVP		0
#define WALT_BINDER_MVP		1
#define WALT_TASK_BOOST_MVP	2
#define WALT_LL_PIPE_MVP	3

#define WALT_NOT_MVP		-1

#define is_mvp(wts) (wts->mvp_prio != WALT_NOT_MVP)
void walt_cfs_enqueue_task(struct rq *rq, struct task_struct *p);
void walt_cfs_dequeue_task(struct rq *rq, struct task_struct *p);
void walt_cfs_tick(struct rq *rq);
void walt_lb_tick(struct rq *rq);

extern __read_mostly unsigned int walt_scale_demand_divisor;

static inline u64 scale_time_to_util(u64 d)
{
	/*
	 * The denominator at most could be (8 * tick_size) >> SCHED_CAPACITY_SHIFT,
	 * a value that easily fits a 32bit integer.
	 */
	do_div(d, walt_scale_demand_divisor);
	return d;
}

void create_util_to_cost(void);
struct compute_energy_output {
	unsigned long	sum_util[MAX_CLUSTERS];
	unsigned long	max_util[MAX_CLUSTERS];
	unsigned long	cost[MAX_CLUSTERS];
	unsigned int	cluster_first_cpu[MAX_CLUSTERS];
};

static inline bool is_state1(void)
{
	struct cpumask local_mask = { CPU_BITS_NONE };

	if (!cpumask_weight(&part_haltable_cpus))
		return false;

	cpumask_or(&local_mask, cpu_partial_halt_mask, cpu_halt_mask);
	return cpumask_subset(&part_haltable_cpus, &local_mask);
}

/* determine if this task should be allowed to use a partially halted cpu */
static inline bool task_reject_partialhalt_cpu(struct task_struct *p, int cpu)
{
	if (p->prio < MAX_RT_PRIO)
		return false;

	if (cpu_partial_halted(cpu) && !task_fits_capacity(p, 0))
		return true;

	return false;
}

/* walt_find_and_choose_cluster_packing_cpu - Return a packing_cpu choice common for this cluster.
 * @start_cpu:  The cpu from the cluster to choose from
 *
 * If the cluster has a 32bit capable cpu return it regardless
 * of whether it is halted or not.
 *
 * If the cluster does not have a 32 bit capable cpu, find the
 * first unhalted, active cpu in this cluster.
 *
 * Returns -1 if packing_cpu if not found or is unsuitable to be packed on  to
 * Returns a valid cpu number if packing_cpu is found and is usable
 */
static inline int walt_find_and_choose_cluster_packing_cpu(int start_cpu, struct task_struct *p)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, start_cpu);
	struct walt_sched_cluster *cluster = wrq->cluster;
	cpumask_t unhalted_cpus;
	int packing_cpu;

	/* if idle_enough feature is not enabled */
	if (!sysctl_sched_idle_enough_clust[cluster->id])
		return -1;
	if (!sysctl_sched_cluster_util_thres_pct_clust[cluster->id])
		return -1;

	/* find all unhalted active cpus */
	cpumask_andnot(&unhalted_cpus, cpu_active_mask, cpu_halt_mask);

	/* find all unhalted active cpus in this cluster */
	cpumask_and(&unhalted_cpus, &unhalted_cpus, &cluster->cpus);

	if (is_compat_thread(task_thread_info(p)))
		/* try to find a packing cpu within 32 bit subset */
		cpumask_and(&unhalted_cpus, &unhalted_cpus, system_32bit_el0_cpumask());

	/* return the first found unhalted, active cpu, in this cluster */
	packing_cpu = cpumask_first(&unhalted_cpus);

	/* packing cpu must be a valid cpu for runqueue lookup */
	if (packing_cpu >= nr_cpu_ids)
		return -1;

	/* if cpu is not allowed for this task */
	if (!cpumask_test_cpu(packing_cpu, p->cpus_ptr))
		return -1;

	/* if cluster util is high */
	if (sched_get_cluster_util_pct(cluster) >=
	    sysctl_sched_cluster_util_thres_pct_clust[cluster->id])
		return -1;

	/* if cpu utilization is high */
	if (cpu_util(packing_cpu) >= sysctl_sched_idle_enough_clust[cluster->id])
		return -1;

	/* don't pack big tasks */
	if (task_util(p) >= sysctl_sched_idle_enough_clust[cluster->id])
		return -1;

	if (task_reject_partialhalt_cpu(p, packing_cpu))
		return -1;

	/* don't pack if running at a freq higher than 43.9pct of its fmax */
	if (arch_scale_freq_capacity(packing_cpu) > 450)
		return -1;

	/* the packing cpu can be used, so pack! */
	return packing_cpu;
}

extern void update_cpu_capacity_helper(int cpu);

static inline bool has_internal_freq_limit_changed(struct walt_sched_cluster *cluster)
{
	unsigned int internal_freq;
	int i;

	internal_freq = cluster->walt_internal_freq_limit;
	cluster->walt_internal_freq_limit = cluster->max_freq;

	if (likely(!waltgov_disabled)) {
		for (i = 0; i < MAX_FREQ_CAP; i++)
			cluster->walt_internal_freq_limit = min(fmax_cap[i][cluster->id],
					     cluster->walt_internal_freq_limit);
	}

	return cluster->walt_internal_freq_limit != internal_freq;
}

static inline void update_fmax_cap_capacities(void)
{
	struct walt_sched_cluster *cluster;
	int cpu;

	for_each_sched_cluster(cluster) {
		if (has_internal_freq_limit_changed(cluster)) {
			for_each_cpu(cpu, &cluster->cpus)
				update_cpu_capacity_helper(cpu);
		}
	}
}

extern int add_pipeline(struct walt_task_struct *wts);
extern int remove_pipeline(struct walt_task_struct *wts);

extern void walt_task_dump(struct task_struct *p);
extern void walt_rq_dump(int cpu);
extern void walt_dump(void);
extern int in_sched_bug;

extern struct rq *__migrate_task(struct rq *rq, struct rq_flags *rf,
				 struct task_struct *p, int dest_cpu);

DECLARE_PER_CPU(u64, rt_task_arrival_time);
extern int walt_get_mvp_task_prio(struct task_struct *p);
extern void walt_cfs_deactivate_mvp_task(struct rq *rq, struct task_struct *p);

enum WALT_DEBUG_FEAT {
	WALT_BUG_UPSTREAM,
	WALT_BUG_WALT,
	WALT_BUG_UNUSED,

	/* maximum 4 entries allowed */
	WALT_DEBUG_FEAT_NR,
};

#define WALT_PANIC(condition)				\
({							\
	if (unlikely(!!(condition)) && !in_sched_bug) {	\
		in_sched_bug = 1;			\
		walt_dump();				\
		BUG_ON(condition);			\
	}						\
})

/* the least signifcant byte is the bitmask for features and printk */
#define WALT_PANIC_SENTINEL	0x4544DE00

#define walt_debug_bitmask_panic(x) (1UL << x)
#define walt_debug_bitmask_print(x) (1UL << (x + WALT_DEBUG_FEAT_NR))

/* setup initial values, bug and print on upstream and walt, ignore noncritical */
#define walt_debug_initial_values()			\
	(WALT_PANIC_SENTINEL |				\
	 walt_debug_bitmask_panic(WALT_BUG_UPSTREAM) |	\
	 walt_debug_bitmask_print(WALT_BUG_UPSTREAM) |	\
	 walt_debug_bitmask_panic(WALT_BUG_WALT) |	\
	 walt_debug_bitmask_print(WALT_BUG_WALT))

/* least significant nibble is the bug feature itself */
#define walt_debug_feat_panic(x) (!!(sysctl_panic_on_walt_bug & (1UL << x)))

/* 2nd least significant nibble is the print capability */
#define walt_debug_feat_print(x) (!!(sysctl_panic_on_walt_bug & (1UL << (x + WALT_DEBUG_FEAT_NR))))

/* return true if the sentinel is set, regardless of feature set */
static inline bool is_walt_sentinel(void)
{
	if (unlikely((sysctl_panic_on_walt_bug & 0xFFFFFF00) == WALT_PANIC_SENTINEL))
		return true;
	return false;
}

/* if the sentinel is properly set, print and/or panic as configured */
#define WALT_BUG(feat, p, format, args...)					\
({										\
	if (is_walt_sentinel()) {						\
		if (walt_debug_feat_print(feat))				\
			printk_deferred("WALT-BUG " format, args);		\
		if (walt_debug_feat_panic(feat)) {				\
			if (p)							\
				walt_task_dump(p);				\
			WALT_PANIC(1);						\
		}								\
	}									\
})

static inline void walt_lockdep_assert(int cond, int cpu, struct task_struct *p)
{
	if (!cond) {
		pr_err("LOCKDEP: %pS %ps %ps %ps\n",
		       __builtin_return_address(0),
		       __builtin_return_address(1),
		       __builtin_return_address(2),
		       __builtin_return_address(3));
		WALT_BUG(WALT_BUG_WALT, p,
			 "running_cpu=%d cpu_rq=%d cpu_rq lock not held",
			 raw_smp_processor_id(), cpu);
	}
}

#ifdef CONFIG_LOCKDEP
#define walt_lockdep_assert_held(l, cpu, p)				\
	walt_lockdep_assert(lockdep_is_held(l) != LOCK_STATE_NOT_HELD, cpu, p)
#else
#define walt_lockdep_assert_held(l, cpu, p) do { (void)(l); } while (0)
#endif

#define walt_lockdep_assert_rq(rq, p)			\
	walt_lockdep_assert_held(&rq->__lock, cpu_of(rq), p)

#endif /* _WALT_H */
