/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 * Copyright (c) 2024 Andrea Righi <arighi@nvidia.com>
 */
#ifndef _KERNEL_SCHED_EXT_IDLE_H
#define _KERNEL_SCHED_EXT_IDLE_H

struct sched_ext_ops;

#ifdef CONFIG_SMP
void scx_idle_update_selcpu_topology(struct sched_ext_ops *ops);
void scx_idle_init_masks(void);
bool scx_idle_test_and_clear_cpu(int cpu);
s32 scx_pick_idle_cpu(const struct cpumask *cpus_allowed, int node, u64 flags);
#else /* !CONFIG_SMP */
static inline void scx_idle_update_selcpu_topology(struct sched_ext_ops *ops) {}
static inline void scx_idle_init_masks(void) {}
static inline bool scx_idle_test_and_clear_cpu(int cpu) { return false; }
static inline s32 scx_pick_idle_cpu(const struct cpumask *cpus_allowed, int node, u64 flags)
{
	return -EBUSY;
}
#endif /* CONFIG_SMP */

s32 scx_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags, u64 flags);
void scx_idle_enable(struct sched_ext_ops *ops);
void scx_idle_disable(void);
int scx_idle_init(void);

#endif /* _KERNEL_SCHED_EXT_IDLE_H */
