// SPDX-License-Identifier: GPL-2.0
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Built-in idle CPU tracking policy.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 * Copyright (c) 2024 Andrea Righi <arighi@nvidia.com>
 */
#include "ext_idle.h"

/* Enable/disable built-in idle CPU selection policy */
DEFINE_STATIC_KEY_FALSE(scx_builtin_idle_enabled);

#ifdef CONFIG_SMP
#ifdef CONFIG_CPUMASK_OFFSTACK
#define CL_ALIGNED_IF_ONSTACK
#else
#define CL_ALIGNED_IF_ONSTACK __cacheline_aligned_in_smp
#endif

/* Enable/disable LLC aware optimizations */
DEFINE_STATIC_KEY_FALSE(scx_selcpu_topo_llc);

/* Enable/disable NUMA aware optimizations */
DEFINE_STATIC_KEY_FALSE(scx_selcpu_topo_numa);

static struct {
	cpumask_var_t cpu;
	cpumask_var_t smt;
} idle_masks CL_ALIGNED_IF_ONSTACK;

bool scx_idle_test_and_clear_cpu(int cpu)
{
#ifdef CONFIG_SCHED_SMT
	/*
	 * SMT mask should be cleared whether we can claim @cpu or not. The SMT
	 * cluster is not wholly idle either way. This also prevents
	 * scx_pick_idle_cpu() from getting caught in an infinite loop.
	 */
	if (sched_smt_active()) {
		const struct cpumask *smt = cpu_smt_mask(cpu);

		/*
		 * If offline, @cpu is not its own sibling and
		 * scx_pick_idle_cpu() can get caught in an infinite loop as
		 * @cpu is never cleared from idle_masks.smt. Ensure that @cpu
		 * is eventually cleared.
		 *
		 * NOTE: Use cpumask_intersects() and cpumask_test_cpu() to
		 * reduce memory writes, which may help alleviate cache
		 * coherence pressure.
		 */
		if (cpumask_intersects(smt, idle_masks.smt))
			cpumask_andnot(idle_masks.smt, idle_masks.smt, smt);
		else if (cpumask_test_cpu(cpu, idle_masks.smt))
			__cpumask_clear_cpu(cpu, idle_masks.smt);
	}
#endif
	return cpumask_test_and_clear_cpu(cpu, idle_masks.cpu);
}

s32 scx_pick_idle_cpu(const struct cpumask *cpus_allowed, u64 flags)
{
	int cpu;

retry:
	if (sched_smt_active()) {
		cpu = cpumask_any_and_distribute(idle_masks.smt, cpus_allowed);
		if (cpu < nr_cpu_ids)
			goto found;

		if (flags & SCX_PICK_IDLE_CORE)
			return -EBUSY;
	}

	cpu = cpumask_any_and_distribute(idle_masks.cpu, cpus_allowed);
	if (cpu >= nr_cpu_ids)
		return -EBUSY;

found:
	if (scx_idle_test_and_clear_cpu(cpu))
		return cpu;
	else
		goto retry;
}

/*
 * Return the amount of CPUs in the same LLC domain of @cpu (or zero if the LLC
 * domain is not defined).
 */
static unsigned int llc_weight(s32 cpu)
{
	struct sched_domain *sd;

	sd = rcu_dereference(per_cpu(sd_llc, cpu));
	if (!sd)
		return 0;

	return sd->span_weight;
}

/*
 * Return the cpumask representing the LLC domain of @cpu (or NULL if the LLC
 * domain is not defined).
 */
static struct cpumask *llc_span(s32 cpu)
{
	struct sched_domain *sd;

	sd = rcu_dereference(per_cpu(sd_llc, cpu));
	if (!sd)
		return 0;

	return sched_domain_span(sd);
}

/*
 * Return the amount of CPUs in the same NUMA domain of @cpu (or zero if the
 * NUMA domain is not defined).
 */
static unsigned int numa_weight(s32 cpu)
{
	struct sched_domain *sd;
	struct sched_group *sg;

	sd = rcu_dereference(per_cpu(sd_numa, cpu));
	if (!sd)
		return 0;
	sg = sd->groups;
	if (!sg)
		return 0;

	return sg->group_weight;
}

/*
 * Return the cpumask representing the NUMA domain of @cpu (or NULL if the NUMA
 * domain is not defined).
 */
static struct cpumask *numa_span(s32 cpu)
{
	struct sched_domain *sd;
	struct sched_group *sg;

	sd = rcu_dereference(per_cpu(sd_numa, cpu));
	if (!sd)
		return NULL;
	sg = sd->groups;
	if (!sg)
		return NULL;

	return sched_group_span(sg);
}

/*
 * Return true if the LLC domains do not perfectly overlap with the NUMA
 * domains, false otherwise.
 */
static bool llc_numa_mismatch(void)
{
	int cpu;

	/*
	 * We need to scan all online CPUs to verify whether their scheduling
	 * domains overlap.
	 *
	 * While it is rare to encounter architectures with asymmetric NUMA
	 * topologies, CPU hotplugging or virtualized environments can result
	 * in asymmetric configurations.
	 *
	 * For example:
	 *
	 *  NUMA 0:
	 *    - LLC 0: cpu0..cpu7
	 *    - LLC 1: cpu8..cpu15 [offline]
	 *
	 *  NUMA 1:
	 *    - LLC 0: cpu16..cpu23
	 *    - LLC 1: cpu24..cpu31
	 *
	 * In this case, if we only check the first online CPU (cpu0), we might
	 * incorrectly assume that the LLC and NUMA domains are fully
	 * overlapping, which is incorrect (as NUMA 1 has two distinct LLC
	 * domains).
	 */
	for_each_online_cpu(cpu)
		if (llc_weight(cpu) != numa_weight(cpu))
			return true;

	return false;
}

/*
 * Initialize topology-aware scheduling.
 *
 * Detect if the system has multiple LLC or multiple NUMA domains and enable
 * cache-aware / NUMA-aware scheduling optimizations in the default CPU idle
 * selection policy.
 *
 * Assumption: the kernel's internal topology representation assumes that each
 * CPU belongs to a single LLC domain, and that each LLC domain is entirely
 * contained within a single NUMA node.
 */
void scx_idle_update_selcpu_topology(void)
{
	bool enable_llc = false, enable_numa = false;
	unsigned int nr_cpus;
	s32 cpu = cpumask_first(cpu_online_mask);

	/*
	 * Enable LLC domain optimization only when there are multiple LLC
	 * domains among the online CPUs. If all online CPUs are part of a
	 * single LLC domain, the idle CPU selection logic can choose any
	 * online CPU without bias.
	 *
	 * Note that it is sufficient to check the LLC domain of the first
	 * online CPU to determine whether a single LLC domain includes all
	 * CPUs.
	 */
	rcu_read_lock();
	nr_cpus = llc_weight(cpu);
	if (nr_cpus > 0) {
		if (nr_cpus < num_online_cpus())
			enable_llc = true;
		pr_debug("sched_ext: LLC=%*pb weight=%u\n",
			 cpumask_pr_args(llc_span(cpu)), llc_weight(cpu));
	}

	/*
	 * Enable NUMA optimization only when there are multiple NUMA domains
	 * among the online CPUs and the NUMA domains don't perfectly overlaps
	 * with the LLC domains.
	 *
	 * If all CPUs belong to the same NUMA node and the same LLC domain,
	 * enabling both NUMA and LLC optimizations is unnecessary, as checking
	 * for an idle CPU in the same domain twice is redundant.
	 */
	nr_cpus = numa_weight(cpu);
	if (nr_cpus > 0) {
		if (nr_cpus < num_online_cpus() && llc_numa_mismatch())
			enable_numa = true;
		pr_debug("sched_ext: NUMA=%*pb weight=%u\n",
			 cpumask_pr_args(numa_span(cpu)), numa_weight(cpu));
	}
	rcu_read_unlock();

	pr_debug("sched_ext: LLC idle selection %s\n",
		 str_enabled_disabled(enable_llc));
	pr_debug("sched_ext: NUMA idle selection %s\n",
		 str_enabled_disabled(enable_numa));

	if (enable_llc)
		static_branch_enable_cpuslocked(&scx_selcpu_topo_llc);
	else
		static_branch_disable_cpuslocked(&scx_selcpu_topo_llc);
	if (enable_numa)
		static_branch_enable_cpuslocked(&scx_selcpu_topo_numa);
	else
		static_branch_disable_cpuslocked(&scx_selcpu_topo_numa);
}

/*
 * Built-in CPU idle selection policy:
 *
 * 1. Prioritize full-idle cores:
 *   - always prioritize CPUs from fully idle cores (both logical CPUs are
 *     idle) to avoid interference caused by SMT.
 *
 * 2. Reuse the same CPU:
 *   - prefer the last used CPU to take advantage of cached data (L1, L2) and
 *     branch prediction optimizations.
 *
 * 3. Pick a CPU within the same LLC (Last-Level Cache):
 *   - if the above conditions aren't met, pick a CPU that shares the same LLC
 *     to maintain cache locality.
 *
 * 4. Pick a CPU within the same NUMA node, if enabled:
 *   - choose a CPU from the same NUMA node to reduce memory access latency.
 *
 * 5. Pick any idle CPU usable by the task.
 *
 * Step 3 and 4 are performed only if the system has, respectively, multiple
 * LLC domains / multiple NUMA nodes (see scx_selcpu_topo_llc and
 * scx_selcpu_topo_numa).
 *
 * NOTE: tasks that can only run on 1 CPU are excluded by this logic, because
 * we never call ops.select_cpu() for them, see select_task_rq().
 */
s32 scx_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags, bool *found)
{
	const struct cpumask *llc_cpus = NULL;
	const struct cpumask *numa_cpus = NULL;
	s32 cpu;

	*found = false;

	/*
	 * This is necessary to protect llc_cpus.
	 */
	rcu_read_lock();

	/*
	 * Determine the scheduling domain only if the task is allowed to run
	 * on all CPUs.
	 *
	 * This is done primarily for efficiency, as it avoids the overhead of
	 * updating a cpumask every time we need to select an idle CPU (which
	 * can be costly in large SMP systems), but it also aligns logically:
	 * if a task's scheduling domain is restricted by user-space (through
	 * CPU affinity), the task will simply use the flat scheduling domain
	 * defined by user-space.
	 */
	if (p->nr_cpus_allowed >= num_possible_cpus()) {
		if (static_branch_maybe(CONFIG_NUMA, &scx_selcpu_topo_numa))
			numa_cpus = numa_span(prev_cpu);

		if (static_branch_maybe(CONFIG_SCHED_MC, &scx_selcpu_topo_llc))
			llc_cpus = llc_span(prev_cpu);
	}

	/*
	 * If WAKE_SYNC, try to migrate the wakee to the waker's CPU.
	 */
	if (wake_flags & SCX_WAKE_SYNC) {
		cpu = smp_processor_id();

		/*
		 * If the waker's CPU is cache affine and prev_cpu is idle,
		 * then avoid a migration.
		 */
		if (cpus_share_cache(cpu, prev_cpu) &&
		    scx_idle_test_and_clear_cpu(prev_cpu)) {
			cpu = prev_cpu;
			goto cpu_found;
		}

		/*
		 * If the waker's local DSQ is empty, and the system is under
		 * utilized, try to wake up @p to the local DSQ of the waker.
		 *
		 * Checking only for an empty local DSQ is insufficient as it
		 * could give the wakee an unfair advantage when the system is
		 * oversaturated.
		 *
		 * Checking only for the presence of idle CPUs is also
		 * insufficient as the local DSQ of the waker could have tasks
		 * piled up on it even if there is an idle core elsewhere on
		 * the system.
		 */
		if (!cpumask_empty(idle_masks.cpu) &&
		    !(current->flags & PF_EXITING) &&
		    cpu_rq(cpu)->scx.local_dsq.nr == 0) {
			if (cpumask_test_cpu(cpu, p->cpus_ptr))
				goto cpu_found;
		}
	}

	/*
	 * If CPU has SMT, any wholly idle CPU is likely a better pick than
	 * partially idle @prev_cpu.
	 */
	if (sched_smt_active()) {
		/*
		 * Keep using @prev_cpu if it's part of a fully idle core.
		 */
		if (cpumask_test_cpu(prev_cpu, idle_masks.smt) &&
		    scx_idle_test_and_clear_cpu(prev_cpu)) {
			cpu = prev_cpu;
			goto cpu_found;
		}

		/*
		 * Search for any fully idle core in the same LLC domain.
		 */
		if (llc_cpus) {
			cpu = scx_pick_idle_cpu(llc_cpus, SCX_PICK_IDLE_CORE);
			if (cpu >= 0)
				goto cpu_found;
		}

		/*
		 * Search for any fully idle core in the same NUMA node.
		 */
		if (numa_cpus) {
			cpu = scx_pick_idle_cpu(numa_cpus, SCX_PICK_IDLE_CORE);
			if (cpu >= 0)
				goto cpu_found;
		}

		/*
		 * Search for any full idle core usable by the task.
		 */
		cpu = scx_pick_idle_cpu(p->cpus_ptr, SCX_PICK_IDLE_CORE);
		if (cpu >= 0)
			goto cpu_found;
	}

	/*
	 * Use @prev_cpu if it's idle.
	 */
	if (scx_idle_test_and_clear_cpu(prev_cpu)) {
		cpu = prev_cpu;
		goto cpu_found;
	}

	/*
	 * Search for any idle CPU in the same LLC domain.
	 */
	if (llc_cpus) {
		cpu = scx_pick_idle_cpu(llc_cpus, 0);
		if (cpu >= 0)
			goto cpu_found;
	}

	/*
	 * Search for any idle CPU in the same NUMA node.
	 */
	if (numa_cpus) {
		cpu = scx_pick_idle_cpu(numa_cpus, 0);
		if (cpu >= 0)
			goto cpu_found;
	}

	/*
	 * Search for any idle CPU usable by the task.
	 */
	cpu = scx_pick_idle_cpu(p->cpus_ptr, 0);
	if (cpu >= 0)
		goto cpu_found;

	rcu_read_unlock();
	return prev_cpu;

cpu_found:
	rcu_read_unlock();

	*found = true;
	return cpu;
}

void scx_idle_reset_masks(void)
{
	/*
	 * Consider all online cpus idle. Should converge to the actual state
	 * quickly.
	 */
	cpumask_copy(idle_masks.cpu, cpu_online_mask);
	cpumask_copy(idle_masks.smt, cpu_online_mask);
}

void scx_idle_init_masks(void)
{
	BUG_ON(!alloc_cpumask_var(&idle_masks.cpu, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&idle_masks.smt, GFP_KERNEL));
}

static void update_builtin_idle(int cpu, bool idle)
{
	assign_cpu(cpu, idle_masks.cpu, idle);

#ifdef CONFIG_SCHED_SMT
	if (sched_smt_active()) {
		const struct cpumask *smt = cpu_smt_mask(cpu);

		if (idle) {
			/*
			 * idle_masks.smt handling is racy but that's fine as
			 * it's only for optimization and self-correcting.
			 */
			if (!cpumask_subset(smt, idle_masks.cpu))
				return;
			cpumask_or(idle_masks.smt, idle_masks.smt, smt);
		} else {
			cpumask_andnot(idle_masks.smt, idle_masks.smt, smt);
		}
	}
#endif
}

/*
 * Update the idle state of a CPU to @idle.
 *
 * If @do_notify is true, ops.update_idle() is invoked to notify the scx
 * scheduler of an actual idle state transition (idle to busy or vice
 * versa). If @do_notify is false, only the idle state in the idle masks is
 * refreshed without invoking ops.update_idle().
 *
 * This distinction is necessary, because an idle CPU can be "reserved" and
 * awakened via scx_bpf_pick_idle_cpu() + scx_bpf_kick_cpu(), marking it as
 * busy even if no tasks are dispatched. In this case, the CPU may return
 * to idle without a true state transition. Refreshing the idle masks
 * without invoking ops.update_idle() ensures accurate idle state tracking
 * while avoiding unnecessary updates and maintaining balanced state
 * transitions.
 */
void __scx_update_idle(struct rq *rq, bool idle, bool do_notify)
{
	int cpu = cpu_of(rq);

	lockdep_assert_rq_held(rq);

	/*
	 * Trigger ops.update_idle() only when transitioning from a task to
	 * the idle thread and vice versa.
	 *
	 * Idle transitions are indicated by do_notify being set to true,
	 * managed by put_prev_task_idle()/set_next_task_idle().
	 */
	if (SCX_HAS_OP(update_idle) && do_notify && !scx_rq_bypassing(rq))
		SCX_CALL_OP(SCX_KF_REST, update_idle, cpu_of(rq), idle);

	/*
	 * Update the idle masks:
	 * - for real idle transitions (do_notify == true)
	 * - for idle-to-idle transitions (indicated by the previous task
	 *   being the idle thread, managed by pick_task_idle())
	 *
	 * Skip updating idle masks if the previous task is not the idle
	 * thread, since set_next_task_idle() has already handled it when
	 * transitioning from a task to the idle thread (calling this
	 * function with do_notify == true).
	 *
	 * In this way we can avoid updating the idle masks twice,
	 * unnecessarily.
	 */
	if (static_branch_likely(&scx_builtin_idle_enabled))
		if (do_notify || is_idle_task(rq->curr))
			update_builtin_idle(cpu, idle);
}
#endif	/* CONFIG_SMP */

/********************************************************************************
 * Helpers that can be called from the BPF scheduler.
 */
__bpf_kfunc_start_defs();

static bool check_builtin_idle_enabled(void)
{
	if (static_branch_likely(&scx_builtin_idle_enabled))
		return true;

	scx_ops_error("built-in idle tracking is disabled");
	return false;
}

/**
 * scx_bpf_select_cpu_dfl - The default implementation of ops.select_cpu()
 * @p: task_struct to select a CPU for
 * @prev_cpu: CPU @p was on previously
 * @wake_flags: %SCX_WAKE_* flags
 * @is_idle: out parameter indicating whether the returned CPU is idle
 *
 * Can only be called from ops.select_cpu() if the built-in CPU selection is
 * enabled - ops.update_idle() is missing or %SCX_OPS_KEEP_BUILTIN_IDLE is set.
 * @p, @prev_cpu and @wake_flags match ops.select_cpu().
 *
 * Returns the picked CPU with *@is_idle indicating whether the picked CPU is
 * currently idle and thus a good candidate for direct dispatching.
 */
__bpf_kfunc s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu,
				       u64 wake_flags, bool *is_idle)
{
	if (!check_builtin_idle_enabled())
		goto prev_cpu;

	if (!scx_kf_allowed(SCX_KF_SELECT_CPU))
		goto prev_cpu;

#ifdef CONFIG_SMP
	return scx_select_cpu_dfl(p, prev_cpu, wake_flags, is_idle);
#endif

prev_cpu:
	*is_idle = false;
	return prev_cpu;
}

/**
 * scx_bpf_get_idle_cpumask - Get a referenced kptr to the idle-tracking
 * per-CPU cpumask.
 *
 * Returns NULL if idle tracking is not enabled, or running on a UP kernel.
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_cpumask(void)
{
	if (!check_builtin_idle_enabled())
		return cpu_none_mask;

#ifdef CONFIG_SMP
	return idle_masks.cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_get_idle_smtmask - Get a referenced kptr to the idle-tracking,
 * per-physical-core cpumask. Can be used to determine if an entire physical
 * core is free.
 *
 * Returns NULL if idle tracking is not enabled, or running on a UP kernel.
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_smtmask(void)
{
	if (!check_builtin_idle_enabled())
		return cpu_none_mask;

#ifdef CONFIG_SMP
	if (sched_smt_active())
		return idle_masks.smt;
	else
		return idle_masks.cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_put_idle_cpumask - Release a previously acquired referenced kptr to
 * either the percpu, or SMT idle-tracking cpumask.
 * @idle_mask: &cpumask to use
 */
__bpf_kfunc void scx_bpf_put_idle_cpumask(const struct cpumask *idle_mask)
{
	/*
	 * Empty function body because we aren't actually acquiring or releasing
	 * a reference to a global idle cpumask, which is read-only in the
	 * caller and is never released. The acquire / release semantics here
	 * are just used to make the cpumask a trusted pointer in the caller.
	 */
}

/**
 * scx_bpf_test_and_clear_cpu_idle - Test and clear @cpu's idle state
 * @cpu: cpu to test and clear idle for
 *
 * Returns %true if @cpu was idle and its idle state was successfully cleared.
 * %false otherwise.
 *
 * Unavailable if ops.update_idle() is implemented and
 * %SCX_OPS_KEEP_BUILTIN_IDLE is not set.
 */
__bpf_kfunc bool scx_bpf_test_and_clear_cpu_idle(s32 cpu)
{
	if (!check_builtin_idle_enabled())
		return false;

	if (ops_cpu_valid(cpu, NULL))
		return scx_idle_test_and_clear_cpu(cpu);
	else
		return false;
}

/**
 * scx_bpf_pick_idle_cpu - Pick and claim an idle cpu
 * @cpus_allowed: Allowed cpumask
 * @flags: %SCX_PICK_IDLE_CPU_* flags
 *
 * Pick and claim an idle cpu in @cpus_allowed. Returns the picked idle cpu
 * number on success. -%EBUSY if no matching cpu was found.
 *
 * Idle CPU tracking may race against CPU scheduling state transitions. For
 * example, this function may return -%EBUSY as CPUs are transitioning into the
 * idle state. If the caller then assumes that there will be dispatch events on
 * the CPUs as they were all busy, the scheduler may end up stalling with CPUs
 * idling while there are pending tasks. Use scx_bpf_pick_any_cpu() and
 * scx_bpf_kick_cpu() to guarantee that there will be at least one dispatch
 * event in the near future.
 *
 * Unavailable if ops.update_idle() is implemented and
 * %SCX_OPS_KEEP_BUILTIN_IDLE is not set.
 */
__bpf_kfunc s32 scx_bpf_pick_idle_cpu(const struct cpumask *cpus_allowed,
				      u64 flags)
{
	if (!check_builtin_idle_enabled())
		return -EBUSY;

	return scx_pick_idle_cpu(cpus_allowed, flags);
}

/**
 * scx_bpf_pick_any_cpu - Pick and claim an idle cpu if available or pick any CPU
 * @cpus_allowed: Allowed cpumask
 * @flags: %SCX_PICK_IDLE_CPU_* flags
 *
 * Pick and claim an idle cpu in @cpus_allowed. If none is available, pick any
 * CPU in @cpus_allowed. Guaranteed to succeed and returns the picked idle cpu
 * number if @cpus_allowed is not empty. -%EBUSY is returned if @cpus_allowed is
 * empty.
 *
 * If ops.update_idle() is implemented and %SCX_OPS_KEEP_BUILTIN_IDLE is not
 * set, this function can't tell which CPUs are idle and will always pick any
 * CPU.
 */
__bpf_kfunc s32 scx_bpf_pick_any_cpu(const struct cpumask *cpus_allowed,
				     u64 flags)
{
	s32 cpu;

	if (static_branch_likely(&scx_builtin_idle_enabled)) {
		cpu = scx_pick_idle_cpu(cpus_allowed, flags);
		if (cpu >= 0)
			return cpu;
	}

	cpu = cpumask_any_distribute(cpus_allowed);
	if (cpu < nr_cpu_ids)
		return cpu;
	else
		return -EBUSY;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_idle)
BTF_ID_FLAGS(func, scx_bpf_get_idle_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_idle_smtmask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_put_idle_cpumask, KF_RELEASE)
BTF_ID_FLAGS(func, scx_bpf_test_and_clear_cpu_idle)
BTF_ID_FLAGS(func, scx_bpf_pick_idle_cpu, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_pick_any_cpu, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_idle)

static const struct btf_kfunc_id_set scx_kfunc_set_idle = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_idle,
};

BTF_KFUNCS_START(scx_kfunc_ids_select_cpu)
BTF_ID_FLAGS(func, scx_bpf_select_cpu_dfl, KF_RCU)
BTF_KFUNCS_END(scx_kfunc_ids_select_cpu)

static const struct btf_kfunc_id_set scx_kfunc_set_select_cpu = {
	.owner			= THIS_MODULE,
	.set			= &scx_kfunc_ids_select_cpu,
};

int scx_idle_init(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &scx_kfunc_set_select_cpu) ||
	      register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &scx_kfunc_set_idle) ||
	      register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &scx_kfunc_set_idle) ||
	      register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &scx_kfunc_set_idle);

	return ret;
}
