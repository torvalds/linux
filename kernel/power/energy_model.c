// SPDX-License-Identifier: GPL-2.0
/*
 * Energy Model of devices
 *
 * Copyright (c) 2018-2021, Arm ltd.
 * Written by: Quentin Perret, Arm ltd.
 * Improvements provided by: Lukasz Luba, Arm ltd.
 */

#define pr_fmt(fmt) "energy_model: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/energy_model.h>
#include <linux/sched/topology.h>
#include <linux/slab.h>

/*
 * Mutex serializing the registrations of performance domains and letting
 * callbacks defined by drivers sleep.
 */
static DEFINE_MUTEX(em_pd_mutex);

static void em_cpufreq_update_efficiencies(struct device *dev,
					   struct em_perf_state *table);
static void em_check_capacity_update(void);
static void em_update_workfn(struct work_struct *work);
static DECLARE_DELAYED_WORK(em_update_work, em_update_workfn);

static bool _is_cpu_device(struct device *dev)
{
	return (dev->bus == &cpu_subsys);
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *rootdir;

struct em_dbg_info {
	struct em_perf_domain *pd;
	int ps_id;
};

#define DEFINE_EM_DBG_SHOW(name, fname)					\
static int em_debug_##fname##_show(struct seq_file *s, void *unused)	\
{									\
	struct em_dbg_info *em_dbg = s->private;			\
	struct em_perf_state *table;					\
	unsigned long val;						\
									\
	rcu_read_lock();						\
	table = em_perf_state_from_pd(em_dbg->pd);			\
	val = table[em_dbg->ps_id].name;				\
	rcu_read_unlock();						\
									\
	seq_printf(s, "%lu\n", val);					\
	return 0;							\
}									\
DEFINE_SHOW_ATTRIBUTE(em_debug_##fname)

DEFINE_EM_DBG_SHOW(frequency, frequency);
DEFINE_EM_DBG_SHOW(power, power);
DEFINE_EM_DBG_SHOW(cost, cost);
DEFINE_EM_DBG_SHOW(performance, performance);
DEFINE_EM_DBG_SHOW(flags, inefficiency);

static void em_debug_create_ps(struct em_perf_domain *em_pd,
			       struct em_dbg_info *em_dbg, int i,
			       struct dentry *pd)
{
	struct em_perf_state *table;
	unsigned long freq;
	struct dentry *d;
	char name[24];

	em_dbg[i].pd = em_pd;
	em_dbg[i].ps_id = i;

	rcu_read_lock();
	table = em_perf_state_from_pd(em_pd);
	freq = table[i].frequency;
	rcu_read_unlock();

	snprintf(name, sizeof(name), "ps:%lu", freq);

	/* Create per-ps directory */
	d = debugfs_create_dir(name, pd);
	debugfs_create_file("frequency", 0444, d, &em_dbg[i],
			    &em_debug_frequency_fops);
	debugfs_create_file("power", 0444, d, &em_dbg[i],
			    &em_debug_power_fops);
	debugfs_create_file("cost", 0444, d, &em_dbg[i],
			    &em_debug_cost_fops);
	debugfs_create_file("performance", 0444, d, &em_dbg[i],
			    &em_debug_performance_fops);
	debugfs_create_file("inefficient", 0444, d, &em_dbg[i],
			    &em_debug_inefficiency_fops);
}

static int em_debug_cpus_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "%*pbl\n", cpumask_pr_args(to_cpumask(s->private)));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(em_debug_cpus);

static int em_debug_flags_show(struct seq_file *s, void *unused)
{
	struct em_perf_domain *pd = s->private;

	seq_printf(s, "%#lx\n", pd->flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(em_debug_flags);

static void em_debug_create_pd(struct device *dev)
{
	struct em_dbg_info *em_dbg;
	struct dentry *d;
	int i;

	/* Create the directory of the performance domain */
	d = debugfs_create_dir(dev_name(dev), rootdir);

	if (_is_cpu_device(dev))
		debugfs_create_file("cpus", 0444, d, dev->em_pd->cpus,
				    &em_debug_cpus_fops);

	debugfs_create_file("flags", 0444, d, dev->em_pd,
			    &em_debug_flags_fops);

	em_dbg = devm_kcalloc(dev, dev->em_pd->nr_perf_states,
			      sizeof(*em_dbg), GFP_KERNEL);
	if (!em_dbg)
		return;

	/* Create a sub-directory for each performance state */
	for (i = 0; i < dev->em_pd->nr_perf_states; i++)
		em_debug_create_ps(dev->em_pd, em_dbg, i, d);

}

static void em_debug_remove_pd(struct device *dev)
{
	debugfs_lookup_and_remove(dev_name(dev), rootdir);
}

static int __init em_debug_init(void)
{
	/* Create /sys/kernel/debug/energy_model directory */
	rootdir = debugfs_create_dir("energy_model", NULL);

	return 0;
}
fs_initcall(em_debug_init);
#else /* CONFIG_DEBUG_FS */
static void em_debug_create_pd(struct device *dev) {}
static void em_debug_remove_pd(struct device *dev) {}
#endif

static void em_destroy_table_rcu(struct rcu_head *rp)
{
	struct em_perf_table __rcu *table;

	table = container_of(rp, struct em_perf_table, rcu);
	kfree(table);
}

static void em_release_table_kref(struct kref *kref)
{
	struct em_perf_table __rcu *table;

	/* It was the last owner of this table so we can free */
	table = container_of(kref, struct em_perf_table, kref);

	call_rcu(&table->rcu, em_destroy_table_rcu);
}

/**
 * em_table_free() - Handles safe free of the EM table when needed
 * @table : EM table which is going to be freed
 *
 * No return values.
 */
void em_table_free(struct em_perf_table __rcu *table)
{
	kref_put(&table->kref, em_release_table_kref);
}

/**
 * em_table_alloc() - Allocate a new EM table
 * @pd		: EM performance domain for which this must be done
 *
 * Allocate a new EM table and initialize its kref to indicate that it
 * has a user.
 * Returns allocated table or NULL.
 */
struct em_perf_table __rcu *em_table_alloc(struct em_perf_domain *pd)
{
	struct em_perf_table __rcu *table;
	int table_size;

	table_size = sizeof(struct em_perf_state) * pd->nr_perf_states;

	table = kzalloc(sizeof(*table) + table_size, GFP_KERNEL);
	if (!table)
		return NULL;

	kref_init(&table->kref);

	return table;
}

static void em_init_performance(struct device *dev, struct em_perf_domain *pd,
				struct em_perf_state *table, int nr_states)
{
	u64 fmax, max_cap;
	int i, cpu;

	/* This is needed only for CPUs and EAS skip other devices */
	if (!_is_cpu_device(dev))
		return;

	cpu = cpumask_first(em_span_cpus(pd));

	/*
	 * Calculate the performance value for each frequency with
	 * linear relationship. The final CPU capacity might not be ready at
	 * boot time, but the EM will be updated a bit later with correct one.
	 */
	fmax = (u64) table[nr_states - 1].frequency;
	max_cap = (u64) arch_scale_cpu_capacity(cpu);
	for (i = 0; i < nr_states; i++)
		table[i].performance = div64_u64(max_cap * table[i].frequency,
						 fmax);
}

static int em_compute_costs(struct device *dev, struct em_perf_state *table,
			    struct em_data_callback *cb, int nr_states,
			    unsigned long flags)
{
	unsigned long prev_cost = ULONG_MAX;
	int i, ret;

	/* Compute the cost of each performance state. */
	for (i = nr_states - 1; i >= 0; i--) {
		unsigned long power_res, cost;

		if ((flags & EM_PERF_DOMAIN_ARTIFICIAL) && cb->get_cost) {
			ret = cb->get_cost(dev, table[i].frequency, &cost);
			if (ret || !cost || cost > EM_MAX_POWER) {
				dev_err(dev, "EM: invalid cost %lu %d\n",
					cost, ret);
				return -EINVAL;
			}
		} else {
			/* increase resolution of 'cost' precision */
			power_res = table[i].power * 10;
			cost = power_res / table[i].performance;
		}

		table[i].cost = cost;

		if (table[i].cost >= prev_cost) {
			table[i].flags = EM_PERF_STATE_INEFFICIENT;
			dev_dbg(dev, "EM: OPP:%lu is inefficient\n",
				table[i].frequency);
		} else {
			prev_cost = table[i].cost;
		}
	}

	return 0;
}

/**
 * em_dev_compute_costs() - Calculate cost values for new runtime EM table
 * @dev		: Device for which the EM table is to be updated
 * @table	: The new EM table that is going to get the costs calculated
 * @nr_states	: Number of performance states
 *
 * Calculate the em_perf_state::cost values for new runtime EM table. The
 * values are used for EAS during task placement. It also calculates and sets
 * the efficiency flag for each performance state. When the function finish
 * successfully the EM table is ready to be updated and used by EAS.
 *
 * Return 0 on success or a proper error in case of failure.
 */
int em_dev_compute_costs(struct device *dev, struct em_perf_state *table,
			 int nr_states)
{
	return em_compute_costs(dev, table, NULL, nr_states, 0);
}

/**
 * em_dev_update_perf_domain() - Update runtime EM table for a device
 * @dev		: Device for which the EM is to be updated
 * @new_table	: The new EM table that is going to be used from now
 *
 * Update EM runtime modifiable table for the @dev using the provided @table.
 *
 * This function uses a mutex to serialize writers, so it must not be called
 * from a non-sleeping context.
 *
 * Return 0 on success or an error code on failure.
 */
int em_dev_update_perf_domain(struct device *dev,
			      struct em_perf_table __rcu *new_table)
{
	struct em_perf_table __rcu *old_table;
	struct em_perf_domain *pd;

	if (!dev)
		return -EINVAL;

	/* Serialize update/unregister or concurrent updates */
	mutex_lock(&em_pd_mutex);

	if (!dev->em_pd) {
		mutex_unlock(&em_pd_mutex);
		return -EINVAL;
	}
	pd = dev->em_pd;

	kref_get(&new_table->kref);

	old_table = pd->em_table;
	rcu_assign_pointer(pd->em_table, new_table);

	em_cpufreq_update_efficiencies(dev, new_table->state);

	em_table_free(old_table);

	mutex_unlock(&em_pd_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(em_dev_update_perf_domain);

static int em_create_perf_table(struct device *dev, struct em_perf_domain *pd,
				struct em_perf_state *table,
				struct em_data_callback *cb,
				unsigned long flags)
{
	unsigned long power, freq, prev_freq = 0;
	int nr_states = pd->nr_perf_states;
	int i, ret;

	/* Build the list of performance states for this performance domain */
	for (i = 0, freq = 0; i < nr_states; i++, freq++) {
		/*
		 * active_power() is a driver callback which ceils 'freq' to
		 * lowest performance state of 'dev' above 'freq' and updates
		 * 'power' and 'freq' accordingly.
		 */
		ret = cb->active_power(dev, &power, &freq);
		if (ret) {
			dev_err(dev, "EM: invalid perf. state: %d\n",
				ret);
			return -EINVAL;
		}

		/*
		 * We expect the driver callback to increase the frequency for
		 * higher performance states.
		 */
		if (freq <= prev_freq) {
			dev_err(dev, "EM: non-increasing freq: %lu\n",
				freq);
			return -EINVAL;
		}

		/*
		 * The power returned by active_state() is expected to be
		 * positive and be in range.
		 */
		if (!power || power > EM_MAX_POWER) {
			dev_err(dev, "EM: invalid power: %lu\n",
				power);
			return -EINVAL;
		}

		table[i].power = power;
		table[i].frequency = prev_freq = freq;
	}

	em_init_performance(dev, pd, table, nr_states);

	ret = em_compute_costs(dev, table, cb, nr_states, flags);
	if (ret)
		return -EINVAL;

	return 0;
}

static int em_create_pd(struct device *dev, int nr_states,
			struct em_data_callback *cb, cpumask_t *cpus,
			unsigned long flags)
{
	struct em_perf_table __rcu *em_table;
	struct em_perf_domain *pd;
	struct device *cpu_dev;
	int cpu, ret, num_cpus;

	if (_is_cpu_device(dev)) {
		num_cpus = cpumask_weight(cpus);

		/* Prevent max possible energy calculation to not overflow */
		if (num_cpus > EM_MAX_NUM_CPUS) {
			dev_err(dev, "EM: too many CPUs, overflow possible\n");
			return -EINVAL;
		}

		pd = kzalloc(sizeof(*pd) + cpumask_size(), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;

		cpumask_copy(em_span_cpus(pd), cpus);
	} else {
		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;
	}

	pd->nr_perf_states = nr_states;

	em_table = em_table_alloc(pd);
	if (!em_table)
		goto free_pd;

	ret = em_create_perf_table(dev, pd, em_table->state, cb, flags);
	if (ret)
		goto free_pd_table;

	rcu_assign_pointer(pd->em_table, em_table);

	if (_is_cpu_device(dev))
		for_each_cpu(cpu, cpus) {
			cpu_dev = get_cpu_device(cpu);
			cpu_dev->em_pd = pd;
		}

	dev->em_pd = pd;

	return 0;

free_pd_table:
	kfree(em_table);
free_pd:
	kfree(pd);
	return -EINVAL;
}

static void
em_cpufreq_update_efficiencies(struct device *dev, struct em_perf_state *table)
{
	struct em_perf_domain *pd = dev->em_pd;
	struct cpufreq_policy *policy;
	int found = 0;
	int i, cpu;

	if (!_is_cpu_device(dev))
		return;

	/* Try to get a CPU which is active and in this PD */
	cpu = cpumask_first_and(em_span_cpus(pd), cpu_active_mask);
	if (cpu >= nr_cpu_ids) {
		dev_warn(dev, "EM: No online CPU for CPUFreq policy\n");
		return;
	}

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		dev_warn(dev, "EM: Access to CPUFreq policy failed\n");
		return;
	}

	for (i = 0; i < pd->nr_perf_states; i++) {
		if (!(table[i].flags & EM_PERF_STATE_INEFFICIENT))
			continue;

		if (!cpufreq_table_set_inefficient(policy, table[i].frequency))
			found++;
	}

	cpufreq_cpu_put(policy);

	if (!found)
		return;

	/*
	 * Efficiencies have been installed in CPUFreq, inefficient frequencies
	 * will be skipped. The EM can do the same.
	 */
	pd->flags |= EM_PERF_DOMAIN_SKIP_INEFFICIENCIES;
}

/**
 * em_pd_get() - Return the performance domain for a device
 * @dev : Device to find the performance domain for
 *
 * Returns the performance domain to which @dev belongs, or NULL if it doesn't
 * exist.
 */
struct em_perf_domain *em_pd_get(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev))
		return NULL;

	return dev->em_pd;
}
EXPORT_SYMBOL_GPL(em_pd_get);

/**
 * em_cpu_get() - Return the performance domain for a CPU
 * @cpu : CPU to find the performance domain for
 *
 * Returns the performance domain to which @cpu belongs, or NULL if it doesn't
 * exist.
 */
struct em_perf_domain *em_cpu_get(int cpu)
{
	struct device *cpu_dev;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return NULL;

	return em_pd_get(cpu_dev);
}
EXPORT_SYMBOL_GPL(em_cpu_get);

/**
 * em_dev_register_perf_domain() - Register the Energy Model (EM) for a device
 * @dev		: Device for which the EM is to register
 * @nr_states	: Number of performance states to register
 * @cb		: Callback functions providing the data of the Energy Model
 * @cpus	: Pointer to cpumask_t, which in case of a CPU device is
 *		obligatory. It can be taken from i.e. 'policy->cpus'. For other
 *		type of devices this should be set to NULL.
 * @microwatts	: Flag indicating that the power values are in micro-Watts or
 *		in some other scale. It must be set properly.
 *
 * Create Energy Model tables for a performance domain using the callbacks
 * defined in cb.
 *
 * The @microwatts is important to set with correct value. Some kernel
 * sub-systems might rely on this flag and check if all devices in the EM are
 * using the same scale.
 *
 * If multiple clients register the same performance domain, all but the first
 * registration will be ignored.
 *
 * Return 0 on success
 */
int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
				struct em_data_callback *cb, cpumask_t *cpus,
				bool microwatts)
{
	unsigned long cap, prev_cap = 0;
	unsigned long flags = 0;
	int cpu, ret;

	if (!dev || !nr_states || !cb)
		return -EINVAL;

	/*
	 * Use a mutex to serialize the registration of performance domains and
	 * let the driver-defined callback functions sleep.
	 */
	mutex_lock(&em_pd_mutex);

	if (dev->em_pd) {
		ret = -EEXIST;
		goto unlock;
	}

	if (_is_cpu_device(dev)) {
		if (!cpus) {
			dev_err(dev, "EM: invalid CPU mask\n");
			ret = -EINVAL;
			goto unlock;
		}

		for_each_cpu(cpu, cpus) {
			if (em_cpu_get(cpu)) {
				dev_err(dev, "EM: exists for CPU%d\n", cpu);
				ret = -EEXIST;
				goto unlock;
			}
			/*
			 * All CPUs of a domain must have the same
			 * micro-architecture since they all share the same
			 * table.
			 */
			cap = arch_scale_cpu_capacity(cpu);
			if (prev_cap && prev_cap != cap) {
				dev_err(dev, "EM: CPUs of %*pbl must have the same capacity\n",
					cpumask_pr_args(cpus));

				ret = -EINVAL;
				goto unlock;
			}
			prev_cap = cap;
		}
	}

	if (microwatts)
		flags |= EM_PERF_DOMAIN_MICROWATTS;
	else if (cb->get_cost)
		flags |= EM_PERF_DOMAIN_ARTIFICIAL;

	/*
	 * EM only supports uW (exception is artificial EM).
	 * Therefore, check and force the drivers to provide
	 * power in uW.
	 */
	if (!microwatts && !(flags & EM_PERF_DOMAIN_ARTIFICIAL)) {
		dev_err(dev, "EM: only supports uW power values\n");
		ret = -EINVAL;
		goto unlock;
	}

	ret = em_create_pd(dev, nr_states, cb, cpus, flags);
	if (ret)
		goto unlock;

	dev->em_pd->flags |= flags;

	em_cpufreq_update_efficiencies(dev, dev->em_pd->em_table->state);

	em_debug_create_pd(dev);
	dev_info(dev, "EM: created perf domain\n");

unlock:
	mutex_unlock(&em_pd_mutex);

	if (_is_cpu_device(dev))
		em_check_capacity_update();

	return ret;
}
EXPORT_SYMBOL_GPL(em_dev_register_perf_domain);

/**
 * em_dev_unregister_perf_domain() - Unregister Energy Model (EM) for a device
 * @dev		: Device for which the EM is registered
 *
 * Unregister the EM for the specified @dev (but not a CPU device).
 */
void em_dev_unregister_perf_domain(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev) || !dev->em_pd)
		return;

	if (_is_cpu_device(dev))
		return;

	/*
	 * The mutex separates all register/unregister requests and protects
	 * from potential clean-up/setup issues in the debugfs directories.
	 * The debugfs directory name is the same as device's name.
	 */
	mutex_lock(&em_pd_mutex);
	em_debug_remove_pd(dev);

	em_table_free(dev->em_pd->em_table);

	kfree(dev->em_pd);
	dev->em_pd = NULL;
	mutex_unlock(&em_pd_mutex);
}
EXPORT_SYMBOL_GPL(em_dev_unregister_perf_domain);

/*
 * Adjustment of CPU performance values after boot, when all CPUs capacites
 * are correctly calculated.
 */
static void em_adjust_new_capacity(struct device *dev,
				   struct em_perf_domain *pd,
				   u64 max_cap)
{
	struct em_perf_table __rcu *em_table;
	struct em_perf_state *ps, *new_ps;
	int ret, ps_size;

	em_table = em_table_alloc(pd);
	if (!em_table) {
		dev_warn(dev, "EM: allocation failed\n");
		return;
	}

	new_ps = em_table->state;

	rcu_read_lock();
	ps = em_perf_state_from_pd(pd);
	/* Initialize data based on old table */
	ps_size = sizeof(struct em_perf_state) * pd->nr_perf_states;
	memcpy(new_ps, ps, ps_size);

	rcu_read_unlock();

	em_init_performance(dev, pd, new_ps, pd->nr_perf_states);
	ret = em_compute_costs(dev, new_ps, NULL, pd->nr_perf_states,
			       pd->flags);
	if (ret) {
		dev_warn(dev, "EM: compute costs failed\n");
		return;
	}

	ret = em_dev_update_perf_domain(dev, em_table);
	if (ret)
		dev_warn(dev, "EM: update failed %d\n", ret);

	/*
	 * This is one-time-update, so give up the ownership in this updater.
	 * The EM framework has incremented the usage counter and from now
	 * will keep the reference (then free the memory when needed).
	 */
	em_table_free(em_table);
}

static void em_check_capacity_update(void)
{
	cpumask_var_t cpu_done_mask;
	struct em_perf_state *table;
	struct em_perf_domain *pd;
	unsigned long cpu_capacity;
	int cpu;

	if (!zalloc_cpumask_var(&cpu_done_mask, GFP_KERNEL)) {
		pr_warn("no free memory\n");
		return;
	}

	/* Check if CPUs capacity has changed than update EM */
	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy;
		unsigned long em_max_perf;
		struct device *dev;

		if (cpumask_test_cpu(cpu, cpu_done_mask))
			continue;

		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_debug("Accessing cpu%d policy failed\n", cpu);
			schedule_delayed_work(&em_update_work,
					      msecs_to_jiffies(1000));
			break;
		}
		cpufreq_cpu_put(policy);

		pd = em_cpu_get(cpu);
		if (!pd || em_is_artificial(pd))
			continue;

		cpumask_or(cpu_done_mask, cpu_done_mask,
			   em_span_cpus(pd));

		cpu_capacity = arch_scale_cpu_capacity(cpu);

		rcu_read_lock();
		table = em_perf_state_from_pd(pd);
		em_max_perf = table[pd->nr_perf_states - 1].performance;
		rcu_read_unlock();

		/*
		 * Check if the CPU capacity has been adjusted during boot
		 * and trigger the update for new performance values.
		 */
		if (em_max_perf == cpu_capacity)
			continue;

		pr_debug("updating cpu%d cpu_cap=%lu old capacity=%lu\n",
			 cpu, cpu_capacity, em_max_perf);

		dev = get_cpu_device(cpu);
		em_adjust_new_capacity(dev, pd, cpu_capacity);
	}

	free_cpumask_var(cpu_done_mask);
}

static void em_update_workfn(struct work_struct *work)
{
	em_check_capacity_update();
}
