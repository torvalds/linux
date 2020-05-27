// SPDX-License-Identifier: GPL-2.0
/*
 * Energy Model of CPUs
 *
 * Copyright (c) 2018, Arm ltd.
 * Written by: Quentin Perret, Arm ltd.
 */

#define pr_fmt(fmt) "energy_model: " fmt

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/energy_model.h>
#include <linux/sched/topology.h>
#include <linux/slab.h>

/* Mapping of each CPU to the performance domain to which it belongs. */
static DEFINE_PER_CPU(struct em_perf_domain *, em_data);

/*
 * Mutex serializing the registrations of performance domains and letting
 * callbacks defined by drivers sleep.
 */
static DEFINE_MUTEX(em_pd_mutex);

#ifdef CONFIG_DEBUG_FS
static struct dentry *rootdir;

static void em_debug_create_ps(struct em_perf_state *ps, struct dentry *pd)
{
	struct dentry *d;
	char name[24];

	snprintf(name, sizeof(name), "ps:%lu", ps->frequency);

	/* Create per-ps directory */
	d = debugfs_create_dir(name, pd);
	debugfs_create_ulong("frequency", 0444, d, &ps->frequency);
	debugfs_create_ulong("power", 0444, d, &ps->power);
	debugfs_create_ulong("cost", 0444, d, &ps->cost);
}

static int em_debug_cpus_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "%*pbl\n", cpumask_pr_args(to_cpumask(s->private)));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(em_debug_cpus);

static void em_debug_create_pd(struct em_perf_domain *pd, int cpu)
{
	struct dentry *d;
	char name[8];
	int i;

	snprintf(name, sizeof(name), "pd%d", cpu);

	/* Create the directory of the performance domain */
	d = debugfs_create_dir(name, rootdir);

	debugfs_create_file("cpus", 0444, d, pd->cpus, &em_debug_cpus_fops);

	/* Create a sub-directory for each performance state */
	for (i = 0; i < pd->nr_perf_states; i++)
		em_debug_create_ps(&pd->table[i], d);
}

static int __init em_debug_init(void)
{
	/* Create /sys/kernel/debug/energy_model directory */
	rootdir = debugfs_create_dir("energy_model", NULL);

	return 0;
}
core_initcall(em_debug_init);
#else /* CONFIG_DEBUG_FS */
static void em_debug_create_pd(struct em_perf_domain *pd, int cpu) {}
#endif
static struct em_perf_domain *
em_create_pd(struct device *dev, int nr_states, struct em_data_callback *cb,
	     cpumask_t *span)
{
	unsigned long opp_eff, prev_opp_eff = ULONG_MAX;
	unsigned long power, freq, prev_freq = 0;
	int i, ret, cpu = cpumask_first(span);
	struct em_perf_state *table;
	struct em_perf_domain *pd;
	u64 fmax;

	if (!cb->active_power)
		return NULL;

	pd = kzalloc(sizeof(*pd) + cpumask_size(), GFP_KERNEL);
	if (!pd)
		return NULL;

	table = kcalloc(nr_states, sizeof(*table), GFP_KERNEL);
	if (!table)
		goto free_pd;

	/* Build the list of performance states for this performance domain */
	for (i = 0, freq = 0; i < nr_states; i++, freq++) {
		/*
		 * active_power() is a driver callback which ceils 'freq' to
		 * lowest performance state of 'cpu' above 'freq' and updates
		 * 'power' and 'freq' accordingly.
		 */
		ret = cb->active_power(&power, &freq, dev);
		if (ret) {
			pr_err("pd%d: invalid perf. state: %d\n", cpu, ret);
			goto free_ps_table;
		}

		/*
		 * We expect the driver callback to increase the frequency for
		 * higher performance states.
		 */
		if (freq <= prev_freq) {
			pr_err("pd%d: non-increasing freq: %lu\n", cpu, freq);
			goto free_ps_table;
		}

		/*
		 * The power returned by active_state() is expected to be
		 * positive, in milli-watts and to fit into 16 bits.
		 */
		if (!power || power > EM_MAX_POWER) {
			pr_err("pd%d: invalid power: %lu\n", cpu, power);
			goto free_ps_table;
		}

		table[i].power = power;
		table[i].frequency = prev_freq = freq;

		/*
		 * The hertz/watts efficiency ratio should decrease as the
		 * frequency grows on sane platforms. But this isn't always
		 * true in practice so warn the user if a higher OPP is more
		 * power efficient than a lower one.
		 */
		opp_eff = freq / power;
		if (opp_eff >= prev_opp_eff)
			pr_warn("pd%d: hertz/watts ratio non-monotonically decreasing: em_perf_state %d >= em_perf_state%d\n",
					cpu, i, i - 1);
		prev_opp_eff = opp_eff;
	}

	/* Compute the cost of each performance state. */
	fmax = (u64) table[nr_states - 1].frequency;
	for (i = 0; i < nr_states; i++) {
		table[i].cost = div64_u64(fmax * table[i].power,
					  table[i].frequency);
	}

	pd->table = table;
	pd->nr_perf_states = nr_states;
	cpumask_copy(to_cpumask(pd->cpus), span);

	em_debug_create_pd(pd, cpu);

	return pd;

free_ps_table:
	kfree(table);
free_pd:
	kfree(pd);

	return NULL;
}

/**
 * em_cpu_get() - Return the performance domain for a CPU
 * @cpu : CPU to find the performance domain for
 *
 * Return: the performance domain to which 'cpu' belongs, or NULL if it doesn't
 * exist.
 */
struct em_perf_domain *em_cpu_get(int cpu)
{
	return READ_ONCE(per_cpu(em_data, cpu));
}
EXPORT_SYMBOL_GPL(em_cpu_get);

/**
 * em_dev_register_perf_domain() - Register the Energy Model (EM) for a device
 * @dev		: Device for which the EM is to register
 * @nr_states	: Number of performance states to register
 * @cb		: Callback functions providing the data of the Energy Model
 * @span	: Pointer to cpumask_t, which in case of a CPU device is
 *		obligatory. It can be taken from i.e. 'policy->cpus'. For other
 *		type of devices this should be set to NULL.
 *
 * Create Energy Model tables for a performance domain using the callbacks
 * defined in cb.
 *
 * If multiple clients register the same performance domain, all but the first
 * registration will be ignored.
 *
 * Return 0 on success
 */
int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
				struct em_data_callback *cb, cpumask_t *span)
{
	unsigned long cap, prev_cap = 0;
	struct em_perf_domain *pd;
	int cpu, ret = 0;

	if (!dev || !span || !nr_states || !cb)
		return -EINVAL;

	/*
	 * Use a mutex to serialize the registration of performance domains and
	 * let the driver-defined callback functions sleep.
	 */
	mutex_lock(&em_pd_mutex);

	for_each_cpu(cpu, span) {
		/* Make sure we don't register again an existing domain. */
		if (READ_ONCE(per_cpu(em_data, cpu))) {
			ret = -EEXIST;
			goto unlock;
		}

		/*
		 * All CPUs of a domain must have the same micro-architecture
		 * since they all share the same table.
		 */
		cap = arch_scale_cpu_capacity(cpu);
		if (prev_cap && prev_cap != cap) {
			pr_err("CPUs of %*pbl must have the same capacity\n",
							cpumask_pr_args(span));
			ret = -EINVAL;
			goto unlock;
		}
		prev_cap = cap;
	}

	/* Create the performance domain and add it to the Energy Model. */
	pd = em_create_pd(dev, nr_states, cb, span);
	if (!pd) {
		ret = -EINVAL;
		goto unlock;
	}

	for_each_cpu(cpu, span) {
		/*
		 * The per-cpu array can be read concurrently from em_cpu_get().
		 * The barrier enforces the ordering needed to make sure readers
		 * can only access well formed em_perf_domain structs.
		 */
		smp_store_release(per_cpu_ptr(&em_data, cpu), pd);
	}

	pr_debug("Created perf domain %*pbl\n", cpumask_pr_args(span));
unlock:
	mutex_unlock(&em_pd_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(em_dev_register_perf_domain);

/**
 * em_register_perf_domain() - Register the Energy Model of a performance domain
 * @span	: Mask of CPUs in the performance domain
 * @nr_states	: Number of capacity states to register
 * @cb		: Callback functions providing the data of the Energy Model
 *
 * Create Energy Model tables for a performance domain using the callbacks
 * defined in cb.
 *
 * If multiple clients register the same performance domain, all but the first
 * registration will be ignored.
 *
 * Return 0 on success
 */
int em_register_perf_domain(cpumask_t *span, unsigned int nr_states,
						struct em_data_callback *cb)
{
	struct device *cpu_dev;

	cpu_dev = get_cpu_device(cpumask_first(span));

	return em_dev_register_perf_domain(cpu_dev, nr_states, cb, span);
}
EXPORT_SYMBOL_GPL(em_register_perf_domain);
