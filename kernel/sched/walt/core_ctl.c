// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"core_ctl: " fmt

#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/syscore_ops.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/walt.h>
#include <linux/kstrtox.h>

#include "walt.h"
#include "trace.h"

/* mask of all CPUs with a fully pause claim outstanding */
static cpumask_t cpus_paused_by_us = { CPU_BITS_NONE };

/* mask of all CPUS with a partial pause claim outstanding */
static cpumask_t cpus_part_paused_by_us = { CPU_BITS_NONE };

/* global to indicate which cpus to pause for sbt */
cpumask_t cpus_for_sbt_pause = { CPU_BITS_NONE };

struct cluster_data {
	bool			inited;
	unsigned int		min_cpus;
	unsigned int		min_partial_cpus;
	unsigned int		max_cpus;
	unsigned int		offline_delay_ms;
	unsigned int		busy_up_thres[MAX_CPUS_PER_CLUSTER];
	unsigned int		busy_down_thres[MAX_CPUS_PER_CLUSTER];
	unsigned int		active_cpus;
	unsigned int		num_cpus;
	unsigned int		nr_not_preferred_cpus;
	cpumask_t		cpu_mask;
	unsigned int		need_cpus;
	unsigned int		task_thres;
	unsigned int		max_nr;
	unsigned int		nr_assist;
	unsigned int		nr_busy;
	unsigned int		nr_big;
	s64			need_ts;
	struct list_head	lru;
	bool			enable;
	int			nrrun;
	unsigned int		first_cpu;
	unsigned int		boost;
	struct kobject		kobj;
	unsigned int		strict_nrrun;
	cpumask_t		nrrun_cpu_mask;
	cpumask_t		nrrun_cpu_misfit_mask;
	cpumask_t		assist_cpu_mask;
	cpumask_t		assist_cpu_misfit_mask;
};

struct cpu_data {
	bool			is_busy;
	unsigned int		busy_pct;
	unsigned int		cpu;
	bool			not_preferred;
	struct cluster_data	*cluster;
	struct list_head	sib;
	bool			disabled;
};

static DEFINE_PER_CPU(struct cpu_data, cpu_state);
static struct cluster_data cluster_state[MAX_CLUSTERS];
static unsigned int num_clusters;

#define for_each_cluster(cluster, idx) \
	for (; (idx) < num_clusters && ((cluster) = &cluster_state[idx]);\
		idx++)

/* single core_ctl thread for all pause/unpause core_ctl operations */
struct task_struct *core_ctl_thread;

/* single lock per single thread for core_ctl
 * protects core_ctl_pending flag
 */
spinlock_t core_ctl_pending_lock;
bool core_ctl_pending;

static DEFINE_SPINLOCK(state_lock);
static void sysfs_param_changed(struct cluster_data *state);
static void wake_up_core_ctl_thread(void);
static bool initialized;
static bool assist_params_initialized;

ATOMIC_NOTIFIER_HEAD(core_ctl_notifier);
static unsigned int last_nr_big;

static unsigned int get_active_cpu_count(const struct cluster_data *cluster);
static unsigned int get_assist_active_cpu_count(const struct cluster_data *cluster);
static unsigned int active_cpu_count_from_mask(const cpumask_t *cpus);
static void __ref do_core_ctl(void);

cpumask_t part_haltable_cpus = { CPU_BITS_NONE };
/* ========================= sysfs interface =========================== */

static ssize_t store_min_cpus(struct cluster_data *state,
				const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	state->min_cpus = min(val, state->num_cpus);
	sysfs_param_changed(state);

	return count;
}

static ssize_t show_min_cpus(const struct cluster_data *state, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->min_cpus);
}

static ssize_t store_min_partial_cpus(struct cluster_data *state,
				const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	state->min_partial_cpus = min(val, state->num_cpus);
	sysfs_param_changed(state);

	if (state->min_partial_cpus > 0)
		cpumask_copy(&part_haltable_cpus, &state->cpu_mask);

	return count;
}

static ssize_t show_min_partial_cpus(const struct cluster_data *state, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->min_partial_cpus);
}

static ssize_t store_max_cpus(struct cluster_data *state,
				const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	state->max_cpus = min(val, state->num_cpus);
	sysfs_param_changed(state);

	return count;
}

static ssize_t show_max_cpus(const struct cluster_data *state, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->max_cpus);
}

static ssize_t store_offline_delay_ms(struct cluster_data *state,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	state->offline_delay_ms = val;
	sysfs_param_changed(state);

	return count;
}

static ssize_t show_task_thres(const struct cluster_data *state, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->task_thres);
}

static ssize_t store_task_thres(struct cluster_data *state,
				const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	if (val < state->num_cpus)
		return -EINVAL;

	state->task_thres = val;
	sysfs_param_changed(state);

	return count;
}


static ssize_t show_offline_delay_ms(const struct cluster_data *state,
				     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->offline_delay_ms);
}

static ssize_t store_busy_up_thres(struct cluster_data *state,
					const char *buf, size_t count)
{
	unsigned int val[MAX_CPUS_PER_CLUSTER];
	int ret, i;

	ret = sscanf(buf, "%u %u %u %u %u %u\n",
			&val[0], &val[1], &val[2], &val[3],
			&val[4], &val[5]);
	if (ret != 1 && ret != state->num_cpus)
		return -EINVAL;

	if (ret == 1) {
		for (i = 0; i < state->num_cpus; i++)
			state->busy_up_thres[i] = val[0];
	} else {
		for (i = 0; i < state->num_cpus; i++)
			state->busy_up_thres[i] = val[i];
	}
	sysfs_param_changed(state);
	return count;
}

static ssize_t show_busy_up_thres(const struct cluster_data *state, char *buf)
{
	int i, count = 0;

	for (i = 0; i < state->num_cpus; i++)
		count += scnprintf(buf + count, PAGE_SIZE - count, "%u ",
				  state->busy_up_thres[i]);

	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	return count;
}

static ssize_t store_busy_down_thres(struct cluster_data *state,
					const char *buf, size_t count)
{
	unsigned int val[MAX_CPUS_PER_CLUSTER];
	int ret, i;

	ret = sscanf(buf, "%u %u %u %u %u %u\n",
			&val[0], &val[1], &val[2], &val[3],
			&val[4], &val[5]);
	if (ret != 1 && ret != state->num_cpus)
		return -EINVAL;

	if (ret == 1) {
		for (i = 0; i < state->num_cpus; i++)
			state->busy_down_thres[i] = val[0];
	} else {
		for (i = 0; i < state->num_cpus; i++)
			state->busy_down_thres[i] = val[i];
	}
	sysfs_param_changed(state);
	return count;
}

static ssize_t show_busy_down_thres(const struct cluster_data *state, char *buf)
{
	int i, count = 0;

	for (i = 0; i < state->num_cpus; i++)
		count += scnprintf(buf + count, PAGE_SIZE - count, "%u ",
				  state->busy_down_thres[i]);

	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	return count;
}

static ssize_t store_enable(struct cluster_data *state,
				const char *buf, size_t count)
{
	unsigned int val;
	bool bval;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	bval = !!val;
	if (bval != state->enable) {
		state->enable = bval;
		sysfs_param_changed(state);
	}

	return count;
}

static ssize_t show_enable(const struct cluster_data *state, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->enable);
}

static ssize_t show_need_cpus(const struct cluster_data *state, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", state->need_cpus);
}

static ssize_t show_active_cpus(const struct cluster_data *state, char *buf)
{
	int active_cpus = get_active_cpu_count(state);

	return scnprintf(buf, PAGE_SIZE, "%u\n", active_cpus);
}

static unsigned int cluster_paused_cpus(const struct cluster_data *cluster)
{
	cpumask_t cluster_paused_cpus;

	cpumask_or(&cluster_paused_cpus, &cpus_paused_by_us, &cpus_part_paused_by_us);
	cpumask_and(&cluster_paused_cpus, &cluster->cpu_mask, &cluster_paused_cpus);

	return cpumask_weight(&cluster_paused_cpus);
}

static ssize_t show_global_state(const struct cluster_data *state, char *buf)
{
	struct cpu_data *c;
	struct cluster_data *cluster;
	ssize_t count = 0;
	unsigned int cpu;

	spin_lock_irq(&state_lock);
	for_each_possible_cpu(cpu) {
		c = &per_cpu(cpu_state, cpu);
		cluster = c->cluster;
		if (!cluster || !cluster->inited)
			continue;

		count += scnprintf(buf + count, PAGE_SIZE - count,
					"CPU%u\n", cpu);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tCPU: %u\n", c->cpu);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tOnline: %u\n",
					cpu_online(c->cpu));
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tPaused: %u Partial Paused: %u\n",
					cpu_halted(c->cpu), cpu_partial_halted(cpu));
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tFirst CPU: %u\n",
						cluster->first_cpu);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tBusy%%: %u\n", c->busy_pct);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tIs busy: %u\n", c->is_busy);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tNot preferred: %u\n",
						c->not_preferred);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"\tNr running: %u\n", cluster->nrrun);
		count += scnprintf(buf + count, PAGE_SIZE - count,
			"\tActive CPUs: %u\n", get_active_cpu_count(cluster));
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"\tNeed CPUs: %u\n", cluster->need_cpus);
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"\tCluster paused CPUs: %u\n",
				   cluster_paused_cpus(cluster));
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"\tBoost: %u\n", (unsigned int) cluster->boost);
	}
	spin_unlock_irq(&state_lock);

	return count;
}

static ssize_t store_not_preferred(struct cluster_data *state,
				   const char *buf, size_t count)
{
	struct cpu_data *c;
	unsigned int i;
	unsigned int val[MAX_CPUS_PER_CLUSTER];
	unsigned long flags;
	int ret;
	int not_preferred_count = 0;

	ret = sscanf(buf, "%u %u %u %u %u %u\n",
			&val[0], &val[1], &val[2], &val[3],
			&val[4], &val[5]);
	if (ret != state->num_cpus)
		return -EINVAL;

	spin_lock_irqsave(&state_lock, flags);
	for (i = 0; i < state->num_cpus; i++) {
		c = &per_cpu(cpu_state, i + state->first_cpu);
		c->not_preferred = val[i];
		not_preferred_count += !!val[i];
	}
	state->nr_not_preferred_cpus = not_preferred_count;
	spin_unlock_irqrestore(&state_lock, flags);

	return count;
}

static ssize_t show_not_preferred(const struct cluster_data *state, char *buf)
{
	struct cpu_data *c;
	ssize_t count = 0;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&state_lock, flags);
	for (i = 0; i < state->num_cpus; i++) {
		c = &per_cpu(cpu_state, i + state->first_cpu);
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"%u ", c->not_preferred);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	spin_unlock_irqrestore(&state_lock, flags);

	return count;
}

DECLARE_BITMAP(temp_bitmap, WALT_NR_CPUS);

static ssize_t store_nrrun_cpu_mask(struct cluster_data *state,
				    const char *buf, size_t count)
{
	unsigned long bitmask = 0xFF;
	const unsigned long *bitmaskp = &bitmask;
	unsigned long flags;
	int ret = 0;

	ret = kstrtoul(buf, 0, (unsigned long *)bitmaskp);
	if (ret < 0)
		return ret;

	bitmap_copy(temp_bitmap, bitmaskp, 8);

	spin_lock_irqsave(&state_lock, flags);
	cpumask_copy(&state->nrrun_cpu_mask, to_cpumask(temp_bitmap));
	spin_unlock_irqrestore(&state_lock, flags);

	return count;
}

static ssize_t show_nrrun_cpu_mask(const struct cluster_data *state, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "0x%x\n", (*(cpumask_bits(&state->nrrun_cpu_mask))));

	return ret;
}

static ssize_t store_nrrun_cpu_misfit_mask(struct cluster_data *state,
				    const char *buf, size_t count)
{
	unsigned long bitmask;
	const unsigned long *bitmaskp = &bitmask;
	unsigned long flags;
	int ret = 0;

	ret = kstrtoul(buf, 0, (unsigned long *)bitmaskp);
	if (ret < 0)
		return ret;

	bitmap_copy(temp_bitmap, bitmaskp, 8);

	spin_lock_irqsave(&state_lock, flags);
	cpumask_copy(&state->nrrun_cpu_misfit_mask, to_cpumask(temp_bitmap));
	spin_unlock_irqrestore(&state_lock, flags);

	return count;
}

static ssize_t show_nrrun_cpu_misfit_mask(const struct cluster_data *state, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "0x%x\n", (*(cpumask_bits(&state->nrrun_cpu_misfit_mask))));

	return ret;
}

static ssize_t store_assist_cpu_mask(struct cluster_data *state,
				    const char *buf, size_t count)
{
	unsigned long bitmask;
	const unsigned long *bitmaskp = &bitmask;
	unsigned long flags;
	int ret = 0;

	ret = kstrtoul(buf, 0, (unsigned long *)bitmaskp);
	if (ret < 0)
		return ret;

	bitmap_copy(temp_bitmap, bitmaskp, 8);

	spin_lock_irqsave(&state_lock, flags);
	cpumask_copy(&state->assist_cpu_mask, to_cpumask(temp_bitmap));
	spin_unlock_irqrestore(&state_lock, flags);

	return count;
}

static ssize_t show_assist_cpu_mask(const struct cluster_data *state, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "0x%x\n", (*(cpumask_bits(&state->assist_cpu_mask))));

	return ret;
}

static ssize_t store_assist_cpu_misfit_mask(struct cluster_data *state,
				    const char *buf, size_t count)
{
	unsigned long bitmask;
	const unsigned long *bitmaskp = &bitmask;
	unsigned long flags;
	int ret = 0;

	ret = kstrtoul(buf, 0, (unsigned long *)bitmaskp);
	if (ret < 0)
		return ret;

	bitmap_copy(temp_bitmap, bitmaskp, 8);

	spin_lock_irqsave(&state_lock, flags);
	cpumask_copy(&state->assist_cpu_misfit_mask, to_cpumask(temp_bitmap));
	spin_unlock_irqrestore(&state_lock, flags);

	return count;
}

static ssize_t show_assist_cpu_misfit_mask(const struct cluster_data *state, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "0x%x\n",
			(*(cpumask_bits(&state->assist_cpu_misfit_mask))));

	return ret;
}

struct core_ctl_attr {
	struct attribute	attr;
	ssize_t			(*show)(const struct cluster_data *cd, char *c);
	ssize_t			(*store)(struct cluster_data *cd, const char *c,
							size_t count);
};

#define core_ctl_attr_ro(_name)		\
static struct core_ctl_attr _name =	\
__ATTR(_name, 0444, show_##_name, NULL)

#define core_ctl_attr_rw(_name)			\
static struct core_ctl_attr _name =		\
__ATTR(_name, 0644, show_##_name, store_##_name)

core_ctl_attr_rw(min_cpus);
core_ctl_attr_rw(min_partial_cpus);
core_ctl_attr_rw(max_cpus);
core_ctl_attr_rw(offline_delay_ms);
core_ctl_attr_rw(busy_up_thres);
core_ctl_attr_rw(busy_down_thres);
core_ctl_attr_rw(task_thres);
core_ctl_attr_ro(need_cpus);
core_ctl_attr_ro(active_cpus);
core_ctl_attr_ro(global_state);
core_ctl_attr_rw(not_preferred);
core_ctl_attr_rw(enable);
core_ctl_attr_rw(nrrun_cpu_mask);
core_ctl_attr_rw(nrrun_cpu_misfit_mask);
core_ctl_attr_rw(assist_cpu_mask);
core_ctl_attr_rw(assist_cpu_misfit_mask);

static struct attribute *default_attrs[] = {
	&min_cpus.attr,
	&min_partial_cpus.attr,
	&max_cpus.attr,
	&offline_delay_ms.attr,
	&busy_up_thres.attr,
	&busy_down_thres.attr,
	&task_thres.attr,
	&enable.attr,
	&need_cpus.attr,
	&active_cpus.attr,
	&global_state.attr,
	&not_preferred.attr,
	&nrrun_cpu_mask.attr,
	&nrrun_cpu_misfit_mask.attr,
	&assist_cpu_mask.attr,
	&assist_cpu_misfit_mask.attr,
	NULL
};
ATTRIBUTE_GROUPS(default);

#define to_cluster_data(k) container_of(k, struct cluster_data, kobj)
#define to_attr(a) container_of(a, struct core_ctl_attr, attr)
static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cluster_data *data = to_cluster_data(kobj);
	struct core_ctl_attr *cattr = to_attr(attr);
	ssize_t ret = -EIO;

	if (cattr->show)
		ret = cattr->show(data, buf);

	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct cluster_data *data = to_cluster_data(kobj);
	struct core_ctl_attr *cattr = to_attr(attr);
	ssize_t ret = -EIO;

	if (cattr->store)
		ret = cattr->store(data, buf, count);

	return ret;
}

static const struct sysfs_ops sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct kobj_type ktype_core_ctl = {
	.sysfs_ops	= &sysfs_ops,
	.default_groups = default_groups,
};

/* ==================== runqueue based core count =================== */

static struct sched_avg_stats *nr_stats;

/**
 * compute_cluster_nr_run:
 * @index: cluster index
 *
 *   Number of tasks running on this cluster plus
 *   tasks running on monitored clusters to find
 *   out CPUs needed from this cluster. Typically
 *   the other cpus that are monitored are from
 *   higher capacity clusters, and full clusters
 *   are considered.
 *
 * For example:
 *   On dual cluster system with 4 min capacity
 *   CPUs and 4 max capacity CPUs, if there are
 *   4 small tasks running on min capacity CPUs
 *   and 2 big tasks running on 2 max capacity
 *   CPUs, nr_need has to be 6 for min capacity
 *   cluster and 2 for max capacity cluster.
 *   This is because, min capacity cluster has to
 *   account for tasks running on max capacity
 *   cluster, so that, the min capacity cluster
 *   can be ready to accommodate tasks running on max
 *   capacity CPUs if the demand of tasks goes down.
 */
static int compute_cluster_nr_run(int index)
{
	int cpu;
	struct cluster_data *cluster;
	int nr_need = 0;

	cluster = &cluster_state[index];

	for_each_cpu(cpu, &cluster->nrrun_cpu_mask) {
		if (cpu_partial_halted(cpu))
			continue;
		nr_need += nr_stats[cpu].nr;
	}

	if (active_cpu_count_from_mask(&cluster->nrrun_cpu_mask) <= nr_need)
		nr_need = nr_need - active_cpu_count_from_mask(&cluster->nrrun_cpu_mask);

	return nr_need;
}

/**
 * compute_cluster_nr_misfit:
 * @index: cluster index
 *   Tasks running on cpus which this cluster is monitoring,
 *   that need to be migrated to this cluster. Typically,
 *   lower capacity cpus are monitored, and full clusters
 *   are considered.
 */
static int compute_cluster_nr_misfit(int index)
{
	int cpu;
	struct cluster_data *cluster;
	int prev_misfit_need = 0;

	cluster = &cluster_state[index];

	for_each_cpu(cpu, &cluster->nrrun_cpu_misfit_mask)
		prev_misfit_need += nr_stats[cpu].nr_misfit;

	return prev_misfit_need;
}

/**
 * compute_cluster_max_nr
 * @index: cluster index
 *
 * For each cpu in this cluster, determine the maximum
 * number of tasks running on the cpu, and return the
 * maximum number of tasks seen on a single cpu, for
 * this cluster.
 */
static int compute_cluster_max_nr(int index)
{
	int cpu;
	struct cluster_data *cluster = &cluster_state[index];
	int max_nr = 0;

	for_each_cpu(cpu, &cluster->cpu_mask) {
		if (cpu_partial_halted(cpu))
			continue;
		max_nr = max(max_nr, nr_stats[cpu].nr_max);
	}

	return max_nr;
}

/**
 * cluster->nr_assist (aka prev_nr_need_assist) =
 *   compute_cluster_nr_run_assist() +
 *   compute_cluster_nr_run_misfit_assist() -
 *   get_assist_active_cpu_count()
 *
 *   nr_assist is the number of tasks that are eligible to run on
 *   the monitored cpus, but cannot run because of insufficient
 *   CPUs there. The cpus being monitored yielding this information
 *   used by compute_cluster_nr_run_assist() and
 *   compute_cluster_nr_run_misfit_assist() are assist_cpu_mask and
 *   assist_cpu_misfit_mask, respectively.
 *
 *   cluster->nr_assist is zero if there are no paused cpus in this cluster.
 *
 * For example:
 *
 *   If max capacity cluster masks are defined as
 *   assist_cpu_mask=0x70 and assist_cpu_misfit_mask=0x0F:
 *
 *   On tri-cluster system with 4 min capacity CPUs, 3 intermediate
 *   capacity CPUs and 1 max capacity CPU, if there are 4 small
 *   tasks running on min capacity CPUs, 4 big tasks running on
 *   intermediate capacity CPUs and no tasks running on max capacity
 *   CPU, nr_run_misfit_assist for min & max capacity clusters will be
 *   0, but for intermediate capacity cluster nr_run_assist will be 1
 *   as it has 3 CPUs, but, there are 4 big tasks to be served.
 *
 *   Since the max capacity cluster is monitoring intermediate
 *   clusters for number of tasks, and min capacity clusters
 *   for number of misfits and there are no misfits on min
 *   capacity cpus, this component is 0. Since there are 4 big
 *   tasks on intertmediate cap cpus but only 3 CPUs, nr_run_assist
 *   for the max capacity cluster is 1.
 *
 *   As a further example, if the cluster had one misfit task in addition
 *   the small tasks on silvers, prime would have counted 1 task
 *   as a result of nr_run_misfit_assist CPUs, and 1 task as a result of
 *   nr_run_assist CPUs.
 *
 *   In both cases the max capacity cpu is currently unpaused, nr_assist
 *   will be 0.
 */

/**
 * compute_cluster_nr_run_assist:
 * @index: cluster index
 *   Tasks running on cpus that this cluster
 *   is assisting. Typically the cpus being
 *   monitored are lower capacity cpus, not
 *   including the current cluster.
 */
static int compute_cluster_nr_run_assist(int index)
{
	int cpu;
	struct cluster_data *cluster = &cluster_state[index];
	int nr_assist = 0;

	for_each_cpu(cpu, &cluster->assist_cpu_mask) {
		if (cpu_partial_halted(cpu))
			continue;
		nr_assist += nr_stats[cpu].nr;
	}

	return nr_assist;
}

/**
 * compute_cluster_nr_run_assist:
 * @index: cluster index
 *   Tasks running on cpus that this cluster
 *   is assisting for misfits. Typically the
 *   cpus being monitored are lower capacity
 *   cpus, not including the current cluster.
 *
 *   In a 3 cluster system, this means that prime
 *   would be monitoring golds for assistance with
 *   misfits.
 */
static int compute_cluster_nr_misfit_assist(int index)
{
	int cpu;
	struct cluster_data *cluster = &cluster_state[index];
	int nr_misfit_assist = 0;

	for_each_cpu(cpu, &cluster->assist_cpu_misfit_mask) {
		if (cpu_partial_halted(cpu))
			continue;
		nr_misfit_assist += nr_stats[cpu].nr_misfit;
	}

	return nr_misfit_assist;
}


/**
 * cluster_real_big_tasks
 * @index: cluster index
 *
 * Return the number of misfits on the lowest capacity
 * cluster, or the number of tasks running on a bigger
 * capacity cluster. This means that any task running
 * on a non-min-capacity-cluster is considered a big
 * task.
 *
 * Do not remove partially halted cpus from this calculation
 * as that will impact the number of big tasks counted in
 * the system, and will impact the enablement of big task
 * rotation.
 */
static int cluster_real_big_tasks(int index)
{
	int nr_big = 0;
	int cpu;
	struct cluster_data *cluster = &cluster_state[index];

	if (index == 0) {
		for_each_cpu(cpu, &cluster->cpu_mask)
			nr_big += nr_stats[cpu].nr_misfit;
	} else {
		for_each_cpu(cpu, &cluster->cpu_mask)
			nr_big += nr_stats[cpu].nr;
	}

	return nr_big;
}

/*
 * This is only implemented for min capacity cluster.
 *
 * Bringing a little CPU out of pause and using it
 * more does not hurt power as much as bringing big CPUs.
 *
 * little cluster provides help needed for the other clusters.
 * we take nr_scaled (which gives better resolution) and find
 * the total nr in the system. Then take out the active higher
 * capacity CPUs from the nr and consider the remaining nr as
 * strict and consider that many little CPUs are needed.
 */
static int compute_cluster_nr_strict_need(int index)
{
	int cpu;
	struct cluster_data *cluster;
	int nr_strict_need = 0;

	if (index != 0)
		return 0;

	for_each_cluster(cluster, index) {
		int nr_scaled = 0;
		int active_cpus = cluster->active_cpus;

		for_each_cpu(cpu, &cluster->cpu_mask) {
			if (cpu_partial_halted(cpu))
				continue;
			nr_scaled += nr_stats[cpu].nr_scaled;
		}

		nr_scaled /= 100;

		/*
		 * For little cluster, nr_scaled becomes the nr_strict,
		 * for other cluster, overflow is counted towards
		 * the little cluster need.
		 */
		if (index == 0)
			nr_strict_need += nr_scaled;
		else
			nr_strict_need += max(0, nr_scaled - active_cpus);
	}

	return nr_strict_need;
}

/*
 * Determine the number of cpus that are busy in the cluster.
 *
 * Using the thresholds for indicating that the cpu is busy
 * with hysterysis, and the high-irqload status for the cpu,
 * determine if a single cpu is busy, and include this in the
 * roll-up busy calculation for the entire cluster.
 */
static int compute_cluster_nr_busy(int index)
{
	struct cluster_data *cluster = &cluster_state[index];
	struct cpu_data *c;
	unsigned int thres_idx;
	int nr_busy = 0;

	cluster->active_cpus = get_active_cpu_count(cluster);
	thres_idx = cluster->active_cpus ? cluster->active_cpus - 1 : 0;
	list_for_each_entry(c, &cluster->lru, sib) {
		if (cpu_partial_halted(c->cpu)) {
			c->is_busy = false;
		} else {
			if (c->busy_pct >= cluster->busy_up_thres[thres_idx] ||
			    sched_cpu_high_irqload(c->cpu))
				c->is_busy = true;
			else if (c->busy_pct < cluster->busy_down_thres[thres_idx])
				c->is_busy = false;

			nr_busy += c->is_busy;
		}
	}

	return nr_busy;
}

static void update_running_avg(u64 window_start)
{
	struct cluster_data *cluster;
	unsigned int index = 0;
	unsigned long flags;
	int big_avg = 0;

	nr_stats = sched_get_nr_running_avg();

	spin_lock_irqsave(&state_lock, flags);
	for_each_cluster(cluster, index) {
		int nr_need, nr_misfit_need;
		int nr_assist_need, nr_misfit_assist_need, nr_assist_active;

		if (!cluster->inited)
			continue;

		nr_need = compute_cluster_nr_run(index);
		nr_misfit_need = compute_cluster_nr_misfit(index);

		cluster->nrrun = nr_need + nr_misfit_need;
		cluster->max_nr = compute_cluster_max_nr(index);

		nr_assist_need = compute_cluster_nr_run_assist(index);
		nr_misfit_assist_need = compute_cluster_nr_misfit_assist(index);

		cluster->strict_nrrun = compute_cluster_nr_strict_need(index);
		nr_assist_active = get_assist_active_cpu_count(cluster);

		if (!cpumask_intersects(&cluster->assist_cpu_mask, &cpus_paused_by_us) &&
		    !cpumask_intersects(&cluster->assist_cpu_mask, &cpus_part_paused_by_us) &&
		    nr_assist_need + nr_misfit_assist_need > nr_assist_active)
			cluster->nr_assist = nr_assist_need +
					nr_misfit_assist_need - nr_assist_active;
		else
			cluster->nr_assist = 0;

		cluster->nr_busy = compute_cluster_nr_busy(index);

		trace_core_ctl_update_nr_need(cluster->first_cpu, nr_need,
					nr_misfit_need, cluster->nrrun, cluster->max_nr,
					cluster->strict_nrrun, nr_assist_need,
					nr_misfit_assist_need, cluster->nr_assist,
					cluster->nr_busy);

		cluster->nr_big = cluster_real_big_tasks(index);
		big_avg += cluster->nr_big;
	}
	spin_unlock_irqrestore(&state_lock, flags);

	last_nr_big = big_avg;
	walt_rotation_checkpoint(big_avg);
	fmax_uncap_checkpoint(big_avg, window_start);
}

#define MAX_NR_THRESHOLD	4
/* adjust needed CPUs based on current runqueue information */
static unsigned int apply_task_need(const struct cluster_data *cluster)
{
	int new_need;

	/* resume all cores if there are enough tasks */
	if (cluster->nrrun >= cluster->task_thres)
		return cluster->num_cpus;

	new_need = cluster->nr_busy;

	/*
	 * resume as many cores as the previous cluster
	 * needs assistance with.
	 */
	new_need = new_need + cluster->nr_assist;

	/* only resume more cores if there are tasks to run */
	if (cluster->nrrun > new_need)
		new_need = new_need + 1;

	/*
	 * We don't want tasks to be overcrowded in a cluster.
	 * If any CPU has more than MAX_NR_THRESHOLD in the last
	 * window, bring another CPU to help out.
	 */
	if (cluster->max_nr > MAX_NR_THRESHOLD)
		new_need = new_need + 1;

	/*
	 * For little cluster, we use a bit more relaxed approach
	 * and impose the strict nr condition. Because all tasks can
	 * spill onto little if big cluster is crowded.
	 */
	if (new_need < cluster->strict_nrrun)
		new_need = cluster->strict_nrrun;

	return new_need;
}

/* ======================= load based core count  ====================== */

static unsigned int apply_limits(const struct cluster_data *cluster,
				 unsigned int need_cpus)
{
	if (!cluster->enable)
		return cluster->num_cpus;

	return min(max(cluster->min_cpus, need_cpus), cluster->max_cpus);
}

static unsigned int active_cpu_count_from_mask(const cpumask_t *cpus_to_check)
{
	cpumask_t lcpus;

	cpumask_andnot(&lcpus, cpus_to_check, cpu_halt_mask);
	cpumask_andnot(&lcpus, &lcpus, cpu_partial_halt_mask);

	return cpumask_weight(&lcpus);
}

static unsigned int get_active_cpu_count(const struct cluster_data *cluster)
{
	return active_cpu_count_from_mask(&cluster->cpu_mask);
}

static unsigned int get_assist_active_cpu_count(const struct cluster_data *cluster)
{
	cpumask_t cpus;

	cpumask_andnot(&cpus, &cluster->assist_cpu_mask, cpu_halt_mask);
	cpumask_andnot(&cpus, &cpus, cpu_partial_halt_mask);

	return cpumask_weight(&cpus);
}

static bool is_active(const struct cpu_data *state)
{
	return cpu_active(state->cpu) && !cpu_halted(state->cpu) && !cpu_partial_halted(state->cpu);
}

static bool adjustment_possible(const struct cluster_data *cluster,
							unsigned int need)
{
	return (need < cluster->active_cpus || (need > cluster->active_cpus &&
						cluster_paused_cpus(cluster)));
}

static bool eval_need(struct cluster_data *cluster)
{
	unsigned long flags;
	unsigned int need_cpus = 0, last_need;
	bool adj_now = false;
	bool adj_possible = false;
	unsigned int new_need;
	s64 now, elapsed;

	if (unlikely(!cluster->inited))
		return false;

	spin_lock_irqsave(&state_lock, flags);

	if (cluster->boost || !cluster->enable)
		need_cpus = cluster->max_cpus;
	else
		need_cpus = apply_task_need(cluster);

	new_need = apply_limits(cluster, need_cpus);

	last_need = cluster->need_cpus;
	now = ktime_to_ms(ktime_get());

	if (new_need > cluster->active_cpus) {
		adj_now = true;
	} else {
		/*
		 * When there is no change in need and there are no more
		 * active CPUs than currently needed, just update the
		 * need time stamp and return.
		 */
		if (new_need == last_need && new_need == cluster->active_cpus) {
			cluster->need_ts = now;
			adj_now = false;
			goto unlock;
		}

		elapsed = now - cluster->need_ts;
		adj_now = elapsed >= cluster->offline_delay_ms;
	}

	if (adj_now) {
		adj_possible = adjustment_possible(cluster, new_need);
		cluster->need_ts = now;
		cluster->need_cpus = new_need;
	}

unlock:
	trace_core_ctl_eval_need(cluster->first_cpu, last_need, new_need,
				 cluster->active_cpus, adj_now, adj_possible,
				 adj_now && adj_possible, cluster->need_ts);
	spin_unlock_irqrestore(&state_lock, flags);

	return adj_now && adj_possible;
}

static void sysfs_param_changed(struct cluster_data *cluster)
{
	if (eval_need(cluster))
		wake_up_core_ctl_thread();
}

/* ========================= core count enforcement ==================== */

static void wake_up_core_ctl_thread(void)
{
	unsigned long flags;

	spin_lock_irqsave(&core_ctl_pending_lock, flags);
	core_ctl_pending = true;
	spin_unlock_irqrestore(&core_ctl_pending_lock, flags);

	wake_up_process(core_ctl_thread);
}

static u64 core_ctl_check_timestamp;

int core_ctl_set_boost(bool boost)
{
	unsigned int index = 0;
	struct cluster_data *cluster = NULL;
	unsigned long flags;
	int ret = 0;
	bool boost_state_changed = false;

	if (unlikely(!initialized))
		return 0;

	spin_lock_irqsave(&state_lock, flags);
	for_each_cluster(cluster, index) {
		if (boost) {
			boost_state_changed = !cluster->boost;
			++cluster->boost;
		} else {
			if (!cluster->boost) {
				ret = -EINVAL;
				break;
			}
			--cluster->boost;
			boost_state_changed = !cluster->boost;
		}
	}
	spin_unlock_irqrestore(&state_lock, flags);

	if (boost_state_changed) {
		index = 0;
		for_each_cluster(cluster, index)
			sysfs_param_changed(cluster);
	}

	if (cluster)
		trace_core_ctl_set_boost(cluster->boost, ret);

	return ret;
}
EXPORT_SYMBOL(core_ctl_set_boost);

void core_ctl_notifier_register(struct notifier_block *n)
{
	atomic_notifier_chain_register(&core_ctl_notifier, n);
}
EXPORT_SYMBOL(core_ctl_notifier_register);

void core_ctl_notifier_unregister(struct notifier_block *n)
{
	atomic_notifier_chain_unregister(&core_ctl_notifier, n);
}
EXPORT_SYMBOL(core_ctl_notifier_unregister);

static void core_ctl_call_notifier(void)
{
	struct core_ctl_notif_data ndata = {0};
	struct notifier_block *nb;

	/*
	 * Don't bother querying the stats when the notifier
	 * chain is empty.
	 */
	rcu_read_lock();
	nb = rcu_dereference_raw(core_ctl_notifier.head);
	rcu_read_unlock();

	if (!nb)
		return;

	ndata.nr_big = last_nr_big;
	walt_fill_ta_data(&ndata);
	trace_core_ctl_notif_data(ndata.nr_big, ndata.coloc_load_pct,
			ndata.ta_util_pct, ndata.cur_cap_pct);

	atomic_notifier_call_chain(&core_ctl_notifier, 0, &ndata);
}

/**
 * core_ctl_check_masks_set
 *
 * return true if all clusters have updated values for their appropriate
 * masks.
 */
static bool core_ctl_check_masks_set(void)
{
	int index = 0;
	struct cluster_data *cluster;
	int possible_cpus = cpumask_weight(cpu_possible_mask);
	int all_masks_set = true;

	for_each_cluster(cluster, index) {
		if (cpumask_weight(&cluster->nrrun_cpu_mask) > possible_cpus ||
		    cpumask_weight(&cluster->nrrun_cpu_misfit_mask) > possible_cpus ||
		    cpumask_weight(&cluster->assist_cpu_mask) > possible_cpus ||
		    cpumask_weight(&cluster->assist_cpu_misfit_mask) > possible_cpus) {
			all_masks_set = false;
			break;
		}
	}

	return all_masks_set;
}

/* is the system in a single-big-thread case? */
static inline bool is_sbt(void)
{
	struct cluster_data *cluster = &cluster_state[MAX_CLUSTERS - 1];

	if (last_nr_big == 1 && cluster->nr_big == 1)
		return true;

	return false;
}

/**
 * sbt_ctl_check
 *
 * Determine if the system should enter or
 * exit single-big-thread mode and ensure
 * the cpus are paused when entering.
 *
 * note: depends on update_running_average
 * note: must be called every window rollover
 */
void sbt_ctl_check(void)
{
	static bool prev_is_sbt;
	static int prev_is_sbt_windows;
	bool now_is_sbt = is_sbt();

	/* if there are cpus to adjust */
	if (cpumask_weight(&cpus_for_sbt_pause) != 0) {

		if (prev_is_sbt == now_is_sbt) {
			if (prev_is_sbt_windows < sysctl_sched_sbt_delay_windows)
				prev_is_sbt_windows = sysctl_sched_sbt_delay_windows;
			return;
		}

		if (now_is_sbt && prev_is_sbt_windows-- > 0)
			return;

		if (!prev_is_sbt && now_is_sbt)
			/*sbt entry*/
			walt_pause_cpus(&cpus_for_sbt_pause, PAUSE_SBT);
		else if (prev_is_sbt && !now_is_sbt)
			/* sbt exit */
			walt_resume_cpus(&cpus_for_sbt_pause, PAUSE_SBT);

		prev_is_sbt_windows = sysctl_sched_sbt_delay_windows;
		prev_is_sbt = now_is_sbt;
	}
}

/*
 * sched_get_nr_running_avg will wipe out previous statistics and
 * update it to the values computed since the last call.
 *
 * core_ctl_check assumes that the statistics are stable, hence
 * window based. Therefore core_ctl_check must only be called from
 * window rollover, or walt_irq_work for not migration.
 */
void core_ctl_check(u64 window_start)
{
	int cpu;
	struct cpu_data *c;
	struct cluster_data *cluster;
	unsigned int index = 0;
	unsigned long flags;
	unsigned int wakeup = 0;

	if (unlikely(!initialized))
		return;

	if (unlikely(!assist_params_initialized)) {
		assist_params_initialized = core_ctl_check_masks_set();
		return;
	}

	if (window_start == core_ctl_check_timestamp)
		return;

	core_ctl_check_timestamp = window_start;

	spin_lock_irqsave(&state_lock, flags);
	for_each_possible_cpu(cpu) {

		c = &per_cpu(cpu_state, cpu);
		cluster = c->cluster;

		if (!cluster || !cluster->inited)
			continue;

		c->busy_pct = sched_get_cpu_util_pct(cpu);
	}
	spin_unlock_irqrestore(&state_lock, flags);

	update_running_avg(window_start);

	for_each_cluster(cluster, index)
		wakeup |= eval_need(cluster);

	if (wakeup)
		do_core_ctl();
	core_ctl_call_notifier();

	/* independent check from eval_need */
	sbt_ctl_check();
}

/* must be called with state_lock held */
static void move_cpu_lru(struct cpu_data *cpu_data)
{
	list_del(&cpu_data->sib);
	list_add_tail(&cpu_data->sib, &cpu_data->cluster->lru);
}

/* pick the cpus that shall be partially paused */
static void try_to_partial_pause(struct cluster_data *cluster,
				 struct cpumask *pause_cpus,
				 struct cpumask *part_pause_cpus)
{
	struct cpu_data *c, *tmp;
	int num_cpus = cluster->min_partial_cpus -
		cpumask_weight(&cpus_part_paused_by_us);

	if (num_cpus <= 0)
		return;

	list_for_each_entry_safe(c, tmp, &cluster->lru, sib) {
		if (num_cpus <= 0)
			break;

		if (cpumask_test_cpu(c->cpu, pause_cpus)) {
			if (!cpumask_test_cpu(c->cpu, &cpus_part_paused_by_us)) {
				cpumask_set_cpu(c->cpu, part_pause_cpus);
				cpumask_clear_cpu(c->cpu, pause_cpus);
				num_cpus--;
				move_cpu_lru(c);
			}
		}
	}
}

static void try_to_partial_resume(struct cluster_data *cluster,
				  struct cpumask *unpause_cpus,
				  struct cpumask *part_unpause_cpus)
{
	/* track the partial cpus that need to be unpaused */
	cpumask_and(part_unpause_cpus, unpause_cpus, &cpus_part_paused_by_us);
}

static void try_to_pause(struct cluster_data *cluster, unsigned int need,
			 struct cpumask *pause_cpus, struct cpumask *part_pause_cpus)
{
	struct cpu_data *c, *tmp;
	unsigned long flags;
	unsigned int num_cpus = cluster->num_cpus;
	unsigned int nr_pending = 0, active_cpus = cluster->active_cpus;
	bool first_pass = cluster->nr_not_preferred_cpus;

	/*
	 * Protect against entry being removed (and added at tail) by other
	 * thread (hotplug).
	 */
	spin_lock_irqsave(&state_lock, flags);
	list_for_each_entry_safe(c, tmp, &cluster->lru, sib) {
		if (!num_cpus--)
			break;

		if (c->disabled)
			continue;
		if (!is_active(c))
			continue;
		if (active_cpus - nr_pending == need)
			break;
		/* Don't pause busy CPUs. */
		if (c->is_busy)
			continue;
		/*
		 * We pause only the not_preferred CPUs. If none
		 * of the CPUs are selected as not_preferred, then
		 * all CPUs are eligible for pausing.
		 */
		if (cluster->nr_not_preferred_cpus && !c->not_preferred)
			continue;

		pr_debug("Trying to pause CPU%u\n", c->cpu);
		cpumask_set_cpu(c->cpu, pause_cpus);
		nr_pending++;
		move_cpu_lru(c);
	}

again:
	/*
	 * If the number of active CPUs is within the limits, then
	 * don't force pause of any busy CPUs.
	 */
	if (active_cpus - nr_pending <= cluster->max_cpus)
		goto unlock;

	num_cpus = cluster->num_cpus;
	list_for_each_entry_safe(c, tmp, &cluster->lru, sib) {
		if (!num_cpus--)
			break;

		if (c->disabled)
			continue;
		if (!is_active(c))
			continue;
		if (active_cpus - nr_pending <= cluster->max_cpus)
			break;

		if (first_pass && !c->not_preferred)
			continue;

		cpumask_set_cpu(c->cpu, pause_cpus);
		nr_pending++;
		move_cpu_lru(c);
	}

	if (first_pass && active_cpus - nr_pending > cluster->max_cpus) {
		first_pass = false;
		goto again;
	}
unlock:
	try_to_partial_pause(cluster, pause_cpus, part_pause_cpus);
	spin_unlock_irqrestore(&state_lock, flags);
}

static int __try_to_resume(struct cluster_data *cluster, unsigned int need,
			   bool force, struct cpumask *unpause_cpus,
			   struct cpumask *part_unpause_cpus)
{
	struct cpu_data *c, *tmp;
	unsigned long flags;
	unsigned int num_cpus = cluster->num_cpus;
	unsigned int nr_pending = 0, active_cpus = cluster->active_cpus;

	/*
	 * Protect against entry being removed (and added at tail) by other
	 * thread (hotplug).
	 */
	spin_lock_irqsave(&state_lock, flags);
	list_for_each_entry_safe(c, tmp, &cluster->lru, sib) {
		if (!num_cpus--)
			break;

		if (!cpumask_test_cpu(c->cpu, &cpus_paused_by_us) &&
		    !cpumask_test_cpu(c->cpu, &cpus_part_paused_by_us))
			continue;
		if (is_active(c) ||
			(!force && c->not_preferred))
			continue;
		if (active_cpus + nr_pending == need)
			break;

		pr_debug("Trying to resume CPU%u\n", c->cpu);

		cpumask_set_cpu(c->cpu, unpause_cpus);
		nr_pending++;
		move_cpu_lru(c);
	}

	spin_unlock_irqrestore(&state_lock, flags);

	return nr_pending;
}

static void try_to_resume(struct cluster_data *cluster, unsigned int need,
			  struct cpumask *unpause_cpus,
			  struct cpumask *part_unpause_cpus)
{
	bool force_use_non_preferred = false;
	unsigned int nr_pending;

	/*
	 * __try_to_resume() marks the CPUs to be resumed but active_cpus
	 * won't be reflected yet. So use the nr_pending to adjust active
	 * count.
	 */
	nr_pending = __try_to_resume(cluster, need, force_use_non_preferred,
				     unpause_cpus, part_unpause_cpus);

	if (cluster->active_cpus + nr_pending == need)
		return;

	force_use_non_preferred = true;
	__try_to_resume(cluster, need, force_use_non_preferred, unpause_cpus, part_unpause_cpus);

	try_to_partial_resume(cluster, unpause_cpus, part_unpause_cpus);
}

/*
 * core_ctl_pause_cpus: pause a set of CPUs as requested by core_ctl, handling errors.
 *
 * In order to handle errors properly, and properly track success, the cpus being
 * passed to walt_pause_cpus needs to be saved off. It needs to be saved because
 * walt_pause_cpus will modify the value (through pause_cpus()). Pause_cpus modifies
 * the value because it updates the variable to eliminate CPUs that are already paused.
 * THIS code, however, must be very careful to track what cpus were requested, rather
 * than what cpus actually were paused in this action. Otherwise, the ref-counts in
 * walt_pause.c will get out of sync with this code.
 */
static void core_ctl_pause_cpus(struct cpumask *cpus_to_pause, struct cpumask *cpus_to_part_pause)
{
	cpumask_t saved_cpus;

	cpumask_andnot(cpus_to_part_pause, cpus_to_part_pause, &cpus_part_paused_by_us);
	cpumask_copy(&saved_cpus, cpus_to_part_pause);

	if (cpumask_any(cpus_to_part_pause) < nr_cpu_ids) {
		if (walt_partial_pause_cpus(cpus_to_part_pause, PAUSE_CORE_CTL) < 0)
			pr_debug("core_ctl pause failed cpus=%*pbl part_paused_by_us=%*pbl\n",
				 cpumask_pr_args(cpus_to_part_pause),
				 cpumask_pr_args(&cpus_part_paused_by_us));
		else
			cpumask_or(&cpus_part_paused_by_us, &cpus_part_paused_by_us, &saved_cpus);
	}

	/* only fully pause cpus that are not currently paused by us, including not currently
	 * partially paused (because these were included in the partial pause case).
	 */
	cpumask_andnot(cpus_to_pause, cpus_to_pause, &cpus_paused_by_us);
	cpumask_copy(&saved_cpus, cpus_to_pause);

	if (cpumask_any(cpus_to_pause) < nr_cpu_ids) {
		if (walt_pause_cpus(cpus_to_pause, PAUSE_CORE_CTL) < 0)
			pr_debug("core_ctl pause failed cpus=%*pbl paused_by_us=%*pbl\n",
				 cpumask_pr_args(cpus_to_pause),
				 cpumask_pr_args(&cpus_paused_by_us));
		else
			cpumask_or(&cpus_paused_by_us, &cpus_paused_by_us, &saved_cpus);
	}
}

/*
 * core_ctl_resume_cpus: resume a set of CPUs as requested by core_ctl, handling errors.
 *
 * In order to handle errors properly, and properly track success, the cpus being
 * passed to walt_resume_cpus needs to be saved off. It needs to be saved because
 * walt_resume_cpus will modify the value (through resume_cpus()). Resume_cpus modifies
 * the value because it updates the variable to eliminate CPUs that are already resumed.
 * THIS code, however, must be very careful to track what cpus were requested, rather
 * than what cpus actually were resumed in this action. Otherwise, the ref-counts in
 * walt_pause.c will get out of sync with this code.
 */
static void core_ctl_resume_cpus(struct cpumask *cpus_to_unpause,
				 struct cpumask *cpus_to_part_unpause)
{
	cpumask_t saved_cpus;

	/* only part resume cpus partially paused by us */
	cpumask_and(cpus_to_part_unpause, cpus_to_part_unpause, &cpus_part_paused_by_us);
	cpumask_copy(&saved_cpus, cpus_to_part_unpause);

	if (cpumask_any(cpus_to_part_unpause) < nr_cpu_ids) {
		if (walt_partial_resume_cpus(cpus_to_part_unpause, PAUSE_CORE_CTL) < 0)
			pr_debug("core_ctl resume failed cpus=%*pbl part_paused_by_us=%*pbl\n",
				 cpumask_pr_args(cpus_to_part_unpause),
				 cpumask_pr_args(&cpus_part_paused_by_us));
		else
			cpumask_andnot(&cpus_part_paused_by_us,
				       &cpus_part_paused_by_us, &saved_cpus);
	}

	cpumask_and(cpus_to_unpause, cpus_to_unpause, &cpus_paused_by_us);
	cpumask_copy(&saved_cpus, cpus_to_unpause);

	if (cpumask_any(cpus_to_unpause) < nr_cpu_ids) {
		if (walt_resume_cpus(cpus_to_unpause, PAUSE_CORE_CTL) < 0)
			pr_debug("core_ctl resume failed cpus=%*pbl paused_by_us=%*pbl\n",
				 cpumask_pr_args(cpus_to_unpause),
				 cpumask_pr_args(&cpus_paused_by_us));
		else
			cpumask_andnot(&cpus_paused_by_us, &cpus_paused_by_us, &saved_cpus);
	}
}

static void __ref do_core_ctl(void)
{
	struct cluster_data *cluster;
	unsigned int index = 0;
	unsigned int need;
	cpumask_t cpus_to_pause = { CPU_BITS_NONE };
	cpumask_t cpus_to_unpause = { CPU_BITS_NONE };
	cpumask_t cpus_to_part_pause = { CPU_BITS_NONE };
	cpumask_t cpus_to_part_unpause = { CPU_BITS_NONE };

	for_each_cluster(cluster, index) {

		cluster->active_cpus = get_active_cpu_count(cluster);
		need = apply_limits(cluster, cluster->need_cpus);

		if (adjustment_possible(cluster, need)) {
			pr_debug("Trying to adjust group %u from %u to %u\n",
				 cluster->first_cpu, cluster->active_cpus, need);

			if (cluster->active_cpus > need)
				try_to_pause(cluster, need, &cpus_to_pause,
					     &cpus_to_part_pause);

			else if (cluster->active_cpus < need)
				try_to_resume(cluster, need, &cpus_to_unpause,
					      &cpus_to_part_unpause);
		}
	}

	core_ctl_pause_cpus(&cpus_to_pause, &cpus_to_part_pause);
	core_ctl_resume_cpus(&cpus_to_unpause, &cpus_to_part_unpause);
}

static int __ref try_core_ctl(void *data)
{
	unsigned long flags;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&core_ctl_pending_lock, flags);
		if (!core_ctl_pending) {
			spin_unlock_irqrestore(&core_ctl_pending_lock, flags);
			schedule();
			if (kthread_should_stop())
				break;
			spin_lock_irqsave(&core_ctl_pending_lock, flags);
		}
		set_current_state(TASK_RUNNING);
		core_ctl_pending = false;
		spin_unlock_irqrestore(&core_ctl_pending_lock, flags);

		do_core_ctl();
	}

	return 0;
}

/* ============================ init code ============================== */

static struct cluster_data *find_cluster_by_first_cpu(unsigned int first_cpu)
{
	unsigned int i;

	for (i = 0; i < num_clusters; ++i) {
		if (cluster_state[i].first_cpu == first_cpu)
			return &cluster_state[i];
	}

	return NULL;
}

static int cluster_init(const struct cpumask *mask)
{
	struct device *dev;
	unsigned int first_cpu = cpumask_first(mask);
	struct cluster_data *cluster;
	struct cpu_data *state;
	unsigned int cpu;

	if (find_cluster_by_first_cpu(first_cpu))
		return 0;

	dev = get_cpu_device(first_cpu);
	if (!dev)
		return -ENODEV;

	pr_info("Creating CPU group %d\n", first_cpu);

	if (num_clusters == MAX_CLUSTERS) {
		pr_err("Unsupported number of clusters. Only %u supported\n",
								MAX_CLUSTERS);
		return -EINVAL;
	}
	cluster = &cluster_state[num_clusters];
	++num_clusters;

	cpumask_copy(&cluster->cpu_mask, mask);
	cluster->num_cpus = cpumask_weight(mask);
	if (cluster->num_cpus > MAX_CPUS_PER_CLUSTER) {
		pr_err("HW configuration not supported\n");
		return -EINVAL;
	}
	cluster->first_cpu = first_cpu;
	cluster->min_cpus = 1;
	cluster->min_partial_cpus = 0;
	cluster->max_cpus = cluster->num_cpus;
	cluster->need_cpus = cluster->num_cpus;
	cluster->offline_delay_ms = 100;
	cluster->task_thres = UINT_MAX;
	cluster->nrrun = cluster->num_cpus;
	cluster->enable = false;
	cluster->nr_not_preferred_cpus = 0;
	cluster->strict_nrrun = 0;
	cluster->nr_big = 0;

	/*
	 * set all cpus in the cluster.  this is an invalid state
	 * and core control will not be considered initialized until
	 * this state is no longer true (all masks must be written).
	 */
	cpumask_setall(&cluster->nrrun_cpu_mask);
	cpumask_setall(&cluster->nrrun_cpu_misfit_mask);
	cpumask_setall(&cluster->assist_cpu_mask);
	cpumask_setall(&cluster->assist_cpu_misfit_mask);

	INIT_LIST_HEAD(&cluster->lru);

	for_each_cpu(cpu, mask) {
		pr_info("Init CPU%u state\n", cpu);

		state = &per_cpu(cpu_state, cpu);
		state->cluster = cluster;
		state->cpu = cpu;
		state->disabled = get_cpu_device(cpu) &&
				get_cpu_device(cpu)->offline_disabled;
		list_add_tail(&state->sib, &cluster->lru);
	}
	cluster->active_cpus = get_active_cpu_count(cluster);

	cluster->inited = true;

	kobject_init(&cluster->kobj, &ktype_core_ctl);
	return kobject_add(&cluster->kobj, &dev->kobj, "core_ctl");
}

int core_ctl_init(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };
	struct walt_sched_cluster *cluster;
	int ret;

	spin_lock_init(&core_ctl_pending_lock);

	nr_stats = sched_get_nr_running_avg();

	/* initialize our single kthread, after spin lock init */
	core_ctl_thread = kthread_run(try_core_ctl, NULL, "core_ctl");

	if (IS_ERR(core_ctl_thread))
		return PTR_ERR(core_ctl_thread);

	sched_setscheduler_nocheck(core_ctl_thread, SCHED_FIFO, &param);

	for_each_sched_cluster(cluster) {
		ret = cluster_init(&cluster->cpus);
		if (ret)
			pr_warn("unable to create core ctl group: %d\n", ret);
	}

	initialized = true;

	return 0;
}
