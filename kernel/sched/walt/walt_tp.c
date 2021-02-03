// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/tracepoint.h>
#include <trace/hooks/sched.h>
#include "trace.h"
#define CREATE_TRACE_POINTS
#include "perf_trace_counters.h"

unsigned int sysctl_sched_dynamic_tp_enable;

#define USE_CPUHP_STATE CPUHP_AP_ONLINE_DYN

DEFINE_PER_CPU(u32, cntenset_val);
DEFINE_PER_CPU(unsigned long, previous_ccnt);
DEFINE_PER_CPU(unsigned long[NUM_L1_CTRS], previous_l1_cnts);
DEFINE_PER_CPU(unsigned long[NUM_AMU_CTRS], previous_amu_cnts);
DEFINE_PER_CPU(u32, old_pid);
DEFINE_PER_CPU(u32, hotplug_flag);
DEFINE_PER_CPU(u64, prev_time);

static int tracectr_cpu_hotplug_coming_up(unsigned int cpu)
{
	per_cpu(hotplug_flag, cpu) = 1;

	return 0;
}

static void setup_prev_cnts(u32 cpu, u32 cnten_val)
{
	int i;

	if (cnten_val & CC)
		per_cpu(previous_ccnt, cpu) =
			read_sysreg(pmccntr_el0);

	for (i = 0; i < NUM_L1_CTRS; i++) {
		if (cnten_val & (1 << i)) {
			/* Select */
			write_sysreg(i, pmselr_el0);
			isb();
			/* Read value */
			per_cpu(previous_l1_cnts[i], cpu) =
				read_sysreg(pmxevcntr_el0);
		}
	}
}

void tracectr_notifier(void *ignore, bool preempt,
			struct task_struct *prev, struct task_struct *next,
			unsigned int prev_state)
{
	u32 cnten_val;
	int current_pid;
	u32 cpu = task_cpu(next);
	u64 now;

	if (!trace_sched_switch_with_ctrs_enabled())
		return;

	current_pid = next->pid;
	if (per_cpu(old_pid, cpu) != -1) {
		cnten_val = read_sysreg(pmcntenset_el0);
		per_cpu(cntenset_val, cpu) = cnten_val;
		/* Disable all the counters that were enabled */
		write_sysreg(cnten_val, pmcntenclr_el0);

		if (per_cpu(hotplug_flag, cpu) == 1) {
			per_cpu(hotplug_flag, cpu) = 0;
			setup_prev_cnts(cpu, cnten_val);
		} else {
			trace_sched_switch_with_ctrs(preempt, prev, next);
			now = sched_clock();
			if ((now - per_cpu(prev_time, cpu)) > NSEC_PER_SEC) {
				trace_sched_switch_ctrs_cfg(cpu);
				per_cpu(prev_time, cpu) = now;
			}
		}

		/* Enable all the counters that were disabled */
		write_sysreg(cnten_val, pmcntenset_el0);
	}
	per_cpu(old_pid, cpu) = current_pid;
}

static void register_sched_switch_ctrs(void)
{
	int cpu, rc;

	for_each_possible_cpu(cpu)
		per_cpu(old_pid, cpu) = -1;

	rc = cpuhp_setup_state_nocalls(USE_CPUHP_STATE, "tracectr_cpu_hotplug",
				tracectr_cpu_hotplug_coming_up,	NULL);
	if (rc >= 0)
		register_trace_sched_switch(tracectr_notifier, NULL);
}

static void unregister_sched_switch_ctrs(void)
{
	unregister_trace_sched_switch(tracectr_notifier, NULL);
	cpuhp_remove_state_nocalls(USE_CPUHP_STATE);
}

const struct cpumask *sched_trace_rd_span(struct root_domain *rd)
{
#ifdef CONFIG_SMP
	return rd ? rd->span : NULL;
#else
	return NULL;
#endif
}

static void sched_overutilized(void *data, struct root_domain *rd,
				 bool overutilized)
{
	if (trace_sched_overutilized_enabled()) {
		char span[SPAN_SIZE];

		cpumap_print_to_pagebuf(false, span, sched_trace_rd_span(rd));
		trace_sched_overutilized(overutilized, span);
	}
}

static void walt_register_dynamic_tp_events(void)
{
	register_trace_sched_overutilized_tp(sched_overutilized, NULL);
	register_sched_switch_ctrs();
}

static void walt_unregister_dynamic_tp_events(void)
{
	unregister_trace_sched_overutilized_tp(sched_overutilized, NULL);
	unregister_sched_switch_ctrs();
}

int sched_dynamic_tp_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	static DEFINE_MUTEX(mutex);
	int ret = 0, *val = (unsigned int *)table->data;
	unsigned int old_val;

	mutex_lock(&mutex);
	old_val = sysctl_sched_dynamic_tp_enable;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret || !write || (old_val == sysctl_sched_dynamic_tp_enable))
		goto done;

	if (*val)
		walt_register_dynamic_tp_events();
	else
		walt_unregister_dynamic_tp_events();
done:
	mutex_unlock(&mutex);
	return ret;
}
