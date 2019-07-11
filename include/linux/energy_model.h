/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ENERGY_MODEL_H
#define _LINUX_ENERGY_MODEL_H
#include <linux/cpumask.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/rcupdate.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched/topology.h>
#include <linux/types.h>

#ifdef CONFIG_ENERGY_MODEL
/**
 * em_cap_state - Capacity state of a performance domain
 * @frequency:	The CPU frequency in KHz, for consistency with CPUFreq
 * @power:	The power consumed by 1 CPU at this level, in milli-watts
 * @cost:	The cost coefficient associated with this level, used during
 *		energy calculation. Equal to: power * max_frequency / frequency
 */
struct em_cap_state {
	unsigned long frequency;
	unsigned long power;
	unsigned long cost;
};

/**
 * em_perf_domain - Performance domain
 * @table:		List of capacity states, in ascending order
 * @nr_cap_states:	Number of capacity states
 * @cpus:		Cpumask covering the CPUs of the domain
 *
 * A "performance domain" represents a group of CPUs whose performance is
 * scaled together. All CPUs of a performance domain must have the same
 * micro-architecture. Performance domains often have a 1-to-1 mapping with
 * CPUFreq policies.
 */
struct em_perf_domain {
	struct em_cap_state *table;
	int nr_cap_states;
	unsigned long cpus[0];
};

#define EM_CPU_MAX_POWER 0xFFFF

struct em_data_callback {
	/**
	 * active_power() - Provide power at the next capacity state of a CPU
	 * @power	: Active power at the capacity state in mW (modified)
	 * @freq	: Frequency at the capacity state in kHz (modified)
	 * @cpu		: CPU for which we do this operation
	 *
	 * active_power() must find the lowest capacity state of 'cpu' above
	 * 'freq' and update 'power' and 'freq' to the matching active power
	 * and frequency.
	 *
	 * The power is the one of a single CPU in the domain, expressed in
	 * milli-watts. It is expected to fit in the [0, EM_CPU_MAX_POWER]
	 * range.
	 *
	 * Return 0 on success.
	 */
	int (*active_power)(unsigned long *power, unsigned long *freq, int cpu);
};
#define EM_DATA_CB(_active_power_cb) { .active_power = &_active_power_cb }

struct em_perf_domain *em_cpu_get(int cpu);
int em_register_perf_domain(cpumask_t *span, unsigned int nr_states,
						struct em_data_callback *cb);

/**
 * em_pd_energy() - Estimates the energy consumed by the CPUs of a perf. domain
 * @pd		: performance domain for which energy has to be estimated
 * @max_util	: highest utilization among CPUs of the domain
 * @sum_util	: sum of the utilization of all CPUs in the domain
 *
 * Return: the sum of the energy consumed by the CPUs of the domain assuming
 * a capacity state satisfying the max utilization of the domain.
 */
static inline unsigned long em_pd_energy(struct em_perf_domain *pd,
				unsigned long max_util, unsigned long sum_util)
{
	unsigned long freq, scale_cpu;
	struct em_cap_state *cs;
	int i, cpu;

	/*
	 * In order to predict the capacity state, map the utilization of the
	 * most utilized CPU of the performance domain to a requested frequency,
	 * like schedutil.
	 */
	cpu = cpumask_first(to_cpumask(pd->cpus));
	scale_cpu = arch_scale_cpu_capacity(NULL, cpu);
	cs = &pd->table[pd->nr_cap_states - 1];
	freq = map_util_freq(max_util, cs->frequency, scale_cpu);

	/*
	 * Find the lowest capacity state of the Energy Model above the
	 * requested frequency.
	 */
	for (i = 0; i < pd->nr_cap_states; i++) {
		cs = &pd->table[i];
		if (cs->frequency >= freq)
			break;
	}

	/*
	 * The capacity of a CPU in the domain at that capacity state (cs)
	 * can be computed as:
	 *
	 *             cs->freq * scale_cpu
	 *   cs->cap = --------------------                          (1)
	 *                 cpu_max_freq
	 *
	 * So, ignoring the costs of idle states (which are not available in
	 * the EM), the energy consumed by this CPU at that capacity state is
	 * estimated as:
	 *
	 *             cs->power * cpu_util
	 *   cpu_nrg = --------------------                          (2)
	 *                   cs->cap
	 *
	 * since 'cpu_util / cs->cap' represents its percentage of busy time.
	 *
	 *   NOTE: Although the result of this computation actually is in
	 *         units of power, it can be manipulated as an energy value
	 *         over a scheduling period, since it is assumed to be
	 *         constant during that interval.
	 *
	 * By injecting (1) in (2), 'cpu_nrg' can be re-expressed as a product
	 * of two terms:
	 *
	 *             cs->power * cpu_max_freq   cpu_util
	 *   cpu_nrg = ------------------------ * ---------          (3)
	 *                    cs->freq            scale_cpu
	 *
	 * The first term is static, and is stored in the em_cap_state struct
	 * as 'cs->cost'.
	 *
	 * Since all CPUs of the domain have the same micro-architecture, they
	 * share the same 'cs->cost', and the same CPU capacity. Hence, the
	 * total energy of the domain (which is the simple sum of the energy of
	 * all of its CPUs) can be factorized as:
	 *
	 *            cs->cost * \Sum cpu_util
	 *   pd_nrg = ------------------------                       (4)
	 *                  scale_cpu
	 */
	return cs->cost * sum_util / scale_cpu;
}

/**
 * em_pd_nr_cap_states() - Get the number of capacity states of a perf. domain
 * @pd		: performance domain for which this must be done
 *
 * Return: the number of capacity states in the performance domain table
 */
static inline int em_pd_nr_cap_states(struct em_perf_domain *pd)
{
	return pd->nr_cap_states;
}

#else
struct em_perf_domain {};
struct em_data_callback {};
#define EM_DATA_CB(_active_power_cb) { }

static inline int em_register_perf_domain(cpumask_t *span,
			unsigned int nr_states, struct em_data_callback *cb)
{
	return -EINVAL;
}
static inline struct em_perf_domain *em_cpu_get(int cpu)
{
	return NULL;
}
static inline unsigned long em_pd_energy(struct em_perf_domain *pd,
			unsigned long max_util, unsigned long sum_util)
{
	return 0;
}
static inline int em_pd_nr_cap_states(struct em_perf_domain *pd)
{
	return 0;
}
#endif

#endif
