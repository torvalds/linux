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
static DEFINE_STATIC_KEY_FALSE(scx_builtin_idle_enabled);

/* Enable/disable per-node idle cpumasks */
static DEFINE_STATIC_KEY_FALSE(scx_builtin_idle_per_node);

#ifdef CONFIG_SMP
/* Enable/disable LLC aware optimizations */
static DEFINE_STATIC_KEY_FALSE(scx_selcpu_topo_llc);

/* Enable/disable NUMA aware optimizations */
static DEFINE_STATIC_KEY_FALSE(scx_selcpu_topo_numa);

/*
 * cpumasks to track idle CPUs within each NUMA node.
 *
 * If SCX_OPS_BUILTIN_IDLE_PER_NODE is not enabled, a single global cpumask
 * from is used to track all the idle CPUs in the system.
 */
struct scx_idle_cpus {
	cpumask_var_t cpu;
	cpumask_var_t smt;
};

/*
 * Global host-wide idle cpumasks (used when SCX_OPS_BUILTIN_IDLE_PER_NODE
 * is not enabled).
 */
static struct scx_idle_cpus scx_idle_global_masks;

/*
 * Per-node idle cpumasks.
 */
static struct scx_idle_cpus **scx_idle_node_masks;

/*
 * Return the idle masks associated to a target @node.
 *
 * NUMA_NO_NODE identifies the global idle cpumask.
 */
static struct scx_idle_cpus *idle_cpumask(int node)
{
	return node == NUMA_NO_NODE ? &scx_idle_global_masks : scx_idle_node_masks[node];
}

/*
 * Returns the NUMA node ID associated with a @cpu, or NUMA_NO_NODE if
 * per-node idle cpumasks are disabled.
 */
static int scx_cpu_node_if_enabled(int cpu)
{
	if (!static_branch_maybe(CONFIG_NUMA, &scx_builtin_idle_per_node))
		return NUMA_NO_NODE;

	return cpu_to_node(cpu);
}

bool scx_idle_test_and_clear_cpu(int cpu)
{
	int node = scx_cpu_node_if_enabled(cpu);
	struct cpumask *idle_cpus = idle_cpumask(node)->cpu;

#ifdef CONFIG_SCHED_SMT
	/*
	 * SMT mask should be cleared whether we can claim @cpu or not. The SMT
	 * cluster is not wholly idle either way. This also prevents
	 * scx_pick_idle_cpu() from getting caught in an infinite loop.
	 */
	if (sched_smt_active()) {
		const struct cpumask *smt = cpu_smt_mask(cpu);
		struct cpumask *idle_smts = idle_cpumask(node)->smt;

		/*
		 * If offline, @cpu is not its own sibling and
		 * scx_pick_idle_cpu() can get caught in an infinite loop as
		 * @cpu is never cleared from the idle SMT mask. Ensure that
		 * @cpu is eventually cleared.
		 *
		 * NOTE: Use cpumask_intersects() and cpumask_test_cpu() to
		 * reduce memory writes, which may help alleviate cache
		 * coherence pressure.
		 */
		if (cpumask_intersects(smt, idle_smts))
			cpumask_andnot(idle_smts, idle_smts, smt);
		else if (cpumask_test_cpu(cpu, idle_smts))
			__cpumask_clear_cpu(cpu, idle_smts);
	}
#endif

	return cpumask_test_and_clear_cpu(cpu, idle_cpus);
}

/*
 * Pick an idle CPU in a specific NUMA node.
 */
static s32 pick_idle_cpu_in_node(const struct cpumask *cpus_allowed, int node, u64 flags)
{
	int cpu;

retry:
	if (sched_smt_active()) {
		cpu = cpumask_any_and_distribute(idle_cpumask(node)->smt, cpus_allowed);
		if (cpu < nr_cpu_ids)
			goto found;

		if (flags & SCX_PICK_IDLE_CORE)
			return -EBUSY;
	}

	cpu = cpumask_any_and_distribute(idle_cpumask(node)->cpu, cpus_allowed);
	if (cpu >= nr_cpu_ids)
		return -EBUSY;

found:
	if (scx_idle_test_and_clear_cpu(cpu))
		return cpu;
	else
		goto retry;
}

/*
 * Tracks nodes that have not yet been visited when searching for an idle
 * CPU across all available nodes.
 */
static DEFINE_PER_CPU(nodemask_t, per_cpu_unvisited);

/*
 * Search for an idle CPU across all nodes, excluding @node.
 */
static s32 pick_idle_cpu_from_online_nodes(const struct cpumask *cpus_allowed, int node, u64 flags)
{
	nodemask_t *unvisited;
	s32 cpu = -EBUSY;

	preempt_disable();
	unvisited = this_cpu_ptr(&per_cpu_unvisited);

	/*
	 * Restrict the search to the online nodes (excluding the current
	 * node that has been visited already).
	 */
	nodes_copy(*unvisited, node_states[N_ONLINE]);
	node_clear(node, *unvisited);

	/*
	 * Traverse all nodes in order of increasing distance, starting
	 * from @node.
	 *
	 * This loop is O(N^2), with N being the amount of NUMA nodes,
	 * which might be quite expensive in large NUMA systems. However,
	 * this complexity comes into play only when a scheduler enables
	 * SCX_OPS_BUILTIN_IDLE_PER_NODE and it's requesting an idle CPU
	 * without specifying a target NUMA node, so it shouldn't be a
	 * bottleneck is most cases.
	 *
	 * As a future optimization we may want to cache the list of nodes
	 * in a per-node array, instead of actually traversing them every
	 * time.
	 */
	for_each_node_numadist(node, *unvisited) {
		cpu = pick_idle_cpu_in_node(cpus_allowed, node, flags);
		if (cpu >= 0)
			break;
	}
	preempt_enable();

	return cpu;
}

/*
 * Find an idle CPU in the system, starting from @node.
 */
s32 scx_pick_idle_cpu(const struct cpumask *cpus_allowed, int node, u64 flags)
{
	s32 cpu;

	/*
	 * Always search in the starting node first (this is an
	 * optimization that can save some cycles even when the search is
	 * not limited to a single node).
	 */
	cpu = pick_idle_cpu_in_node(cpus_allowed, node, flags);
	if (cpu >= 0)
		return cpu;

	/*
	 * Stop the search if we are using only a single global cpumask
	 * (NUMA_NO_NODE) or if the search is restricted to the first node
	 * only.
	 */
	if (node == NUMA_NO_NODE || flags & SCX_PICK_IDLE_IN_NODE)
		return -EBUSY;

	/*
	 * Extend the search to the other online nodes.
	 */
	return pick_idle_cpu_from_online_nodes(cpus_allowed, node, flags);
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
void scx_idle_update_selcpu_topology(struct sched_ext_ops *ops)
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
	 *
	 * If SCX_OPS_BUILTIN_IDLE_PER_NODE is enabled ignore the NUMA
	 * optimization, as we would naturally select idle CPUs within
	 * specific NUMA nodes querying the corresponding per-node cpumask.
	 */
	if (!(ops->flags & SCX_OPS_BUILTIN_IDLE_PER_NODE)) {
		nr_cpus = numa_weight(cpu);
		if (nr_cpus > 0) {
			if (nr_cpus < num_online_cpus() && llc_numa_mismatch())
				enable_numa = true;
			pr_debug("sched_ext: NUMA=%*pb weight=%u\n",
				 cpumask_pr_args(numa_span(cpu)), nr_cpus);
		}
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
 * Step 3 and 4 are performed only if the system has, respectively,
 * multiple LLCs / multiple NUMA nodes (see scx_selcpu_topo_llc and
 * scx_selcpu_topo_numa) and they don't contain the same subset of CPUs.
 *
 * If %SCX_OPS_BUILTIN_IDLE_PER_NODE is enabled, the search will always
 * begin in @prev_cpu's node and proceed to other nodes in order of
 * increasing distance.
 *
 * Return the picked CPU if idle, or a negative value otherwise.
 *
 * NOTE: tasks that can only run on 1 CPU are excluded by this logic, because
 * we never call ops.select_cpu() for them, see select_task_rq().
 */
s32 scx_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags, u64 flags)
{
	const struct cpumask *llc_cpus = NULL;
	const struct cpumask *numa_cpus = NULL;
	int node = scx_cpu_node_if_enabled(prev_cpu);
	s32 cpu;

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
		int waker_node;

		/*
		 * If the waker's CPU is cache affine and prev_cpu is idle,
		 * then avoid a migration.
		 */
		cpu = smp_processor_id();
		if (cpus_share_cache(cpu, prev_cpu) &&
		    scx_idle_test_and_clear_cpu(prev_cpu)) {
			cpu = prev_cpu;
			goto out_unlock;
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
		waker_node = cpu_to_node(cpu);
		if (!(current->flags & PF_EXITING) &&
		    cpu_rq(cpu)->scx.local_dsq.nr == 0 &&
		    (!(flags & SCX_PICK_IDLE_IN_NODE) || (waker_node == node)) &&
		    !cpumask_empty(idle_cpumask(waker_node)->cpu)) {
			if (cpumask_test_cpu(cpu, p->cpus_ptr))
				goto out_unlock;
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
		if (cpumask_test_cpu(prev_cpu, idle_cpumask(node)->smt) &&
		    scx_idle_test_and_clear_cpu(prev_cpu)) {
			cpu = prev_cpu;
			goto out_unlock;
		}

		/*
		 * Search for any fully idle core in the same LLC domain.
		 */
		if (llc_cpus) {
			cpu = pick_idle_cpu_in_node(llc_cpus, node, SCX_PICK_IDLE_CORE);
			if (cpu >= 0)
				goto out_unlock;
		}

		/*
		 * Search for any fully idle core in the same NUMA node.
		 */
		if (numa_cpus) {
			cpu = pick_idle_cpu_in_node(numa_cpus, node, SCX_PICK_IDLE_CORE);
			if (cpu >= 0)
				goto out_unlock;
		}

		/*
		 * Search for any full-idle core usable by the task.
		 *
		 * If the node-aware idle CPU selection policy is enabled
		 * (%SCX_OPS_BUILTIN_IDLE_PER_NODE), the search will always
		 * begin in prev_cpu's node and proceed to other nodes in
		 * order of increasing distance.
		 */
		cpu = scx_pick_idle_cpu(p->cpus_ptr, node, flags | SCX_PICK_IDLE_CORE);
		if (cpu >= 0)
			goto out_unlock;

		/*
		 * Give up if we're strictly looking for a full-idle SMT
		 * core.
		 */
		if (flags & SCX_PICK_IDLE_CORE) {
			cpu = prev_cpu;
			goto out_unlock;
		}
	}

	/*
	 * Use @prev_cpu if it's idle.
	 */
	if (scx_idle_test_and_clear_cpu(prev_cpu)) {
		cpu = prev_cpu;
		goto out_unlock;
	}

	/*
	 * Search for any idle CPU in the same LLC domain.
	 */
	if (llc_cpus) {
		cpu = pick_idle_cpu_in_node(llc_cpus, node, 0);
		if (cpu >= 0)
			goto out_unlock;
	}

	/*
	 * Search for any idle CPU in the same NUMA node.
	 */
	if (numa_cpus) {
		cpu = pick_idle_cpu_in_node(numa_cpus, node, 0);
		if (cpu >= 0)
			goto out_unlock;
	}

	/*
	 * Search for any idle CPU usable by the task.
	 *
	 * If the node-aware idle CPU selection policy is enabled
	 * (%SCX_OPS_BUILTIN_IDLE_PER_NODE), the search will always begin
	 * in prev_cpu's node and proceed to other nodes in order of
	 * increasing distance.
	 */
	cpu = scx_pick_idle_cpu(p->cpus_ptr, node, flags);
	if (cpu >= 0)
		goto out_unlock;

out_unlock:
	rcu_read_unlock();

	return cpu;
}

/*
 * Initialize global and per-node idle cpumasks.
 */
void scx_idle_init_masks(void)
{
	int node;

	/* Allocate global idle cpumasks */
	BUG_ON(!alloc_cpumask_var(&scx_idle_global_masks.cpu, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&scx_idle_global_masks.smt, GFP_KERNEL));

	/* Allocate per-node idle cpumasks */
	scx_idle_node_masks = kcalloc(num_possible_nodes(),
				      sizeof(*scx_idle_node_masks), GFP_KERNEL);
	BUG_ON(!scx_idle_node_masks);

	for_each_node(node) {
		scx_idle_node_masks[node] = kzalloc_node(sizeof(**scx_idle_node_masks),
							 GFP_KERNEL, node);
		BUG_ON(!scx_idle_node_masks[node]);

		BUG_ON(!alloc_cpumask_var_node(&scx_idle_node_masks[node]->cpu, GFP_KERNEL, node));
		BUG_ON(!alloc_cpumask_var_node(&scx_idle_node_masks[node]->smt, GFP_KERNEL, node));
	}
}

static void update_builtin_idle(int cpu, bool idle)
{
	int node = scx_cpu_node_if_enabled(cpu);
	struct cpumask *idle_cpus = idle_cpumask(node)->cpu;

	assign_cpu(cpu, idle_cpus, idle);

#ifdef CONFIG_SCHED_SMT
	if (sched_smt_active()) {
		const struct cpumask *smt = cpu_smt_mask(cpu);
		struct cpumask *idle_smts = idle_cpumask(node)->smt;

		if (idle) {
			/*
			 * idle_smt handling is racy but that's fine as it's
			 * only for optimization and self-correcting.
			 */
			if (!cpumask_subset(smt, idle_cpus))
				return;
			cpumask_or(idle_smts, idle_smts, smt);
		} else {
			cpumask_andnot(idle_smts, idle_smts, smt);
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

static void reset_idle_masks(struct sched_ext_ops *ops)
{
	int node;

	/*
	 * Consider all online cpus idle. Should converge to the actual state
	 * quickly.
	 */
	if (!(ops->flags & SCX_OPS_BUILTIN_IDLE_PER_NODE)) {
		cpumask_copy(idle_cpumask(NUMA_NO_NODE)->cpu, cpu_online_mask);
		cpumask_copy(idle_cpumask(NUMA_NO_NODE)->smt, cpu_online_mask);
		return;
	}

	for_each_node(node) {
		const struct cpumask *node_mask = cpumask_of_node(node);

		cpumask_and(idle_cpumask(node)->cpu, cpu_online_mask, node_mask);
		cpumask_and(idle_cpumask(node)->smt, cpu_online_mask, node_mask);
	}
}
#endif	/* CONFIG_SMP */

void scx_idle_enable(struct sched_ext_ops *ops)
{
	if (!ops->update_idle || (ops->flags & SCX_OPS_KEEP_BUILTIN_IDLE))
		static_branch_enable(&scx_builtin_idle_enabled);
	else
		static_branch_disable(&scx_builtin_idle_enabled);

	if (ops->flags & SCX_OPS_BUILTIN_IDLE_PER_NODE)
		static_branch_enable(&scx_builtin_idle_per_node);
	else
		static_branch_disable(&scx_builtin_idle_per_node);

#ifdef CONFIG_SMP
	reset_idle_masks(ops);
#endif
}

void scx_idle_disable(void)
{
	static_branch_disable(&scx_builtin_idle_enabled);
	static_branch_disable(&scx_builtin_idle_per_node);
}

/********************************************************************************
 * Helpers that can be called from the BPF scheduler.
 */

static int validate_node(int node)
{
	if (!static_branch_likely(&scx_builtin_idle_per_node)) {
		scx_ops_error("per-node idle tracking is disabled");
		return -EOPNOTSUPP;
	}

	/* Return no entry for NUMA_NO_NODE (not a critical scx error) */
	if (node == NUMA_NO_NODE)
		return -ENOENT;

	/* Make sure node is in a valid range */
	if (node < 0 || node >= nr_node_ids) {
		scx_ops_error("invalid node %d", node);
		return -EINVAL;
	}

	/* Make sure the node is part of the set of possible nodes */
	if (!node_possible(node)) {
		scx_ops_error("unavailable node %d", node);
		return -EINVAL;
	}

	return node;
}

__bpf_kfunc_start_defs();

static bool check_builtin_idle_enabled(void)
{
	if (static_branch_likely(&scx_builtin_idle_enabled))
		return true;

	scx_ops_error("built-in idle tracking is disabled");
	return false;
}

/**
 * scx_bpf_cpu_node - Return the NUMA node the given @cpu belongs to, or
 *		      trigger an error if @cpu is invalid
 * @cpu: target CPU
 */
__bpf_kfunc int scx_bpf_cpu_node(s32 cpu)
{
#ifdef CONFIG_NUMA
	if (!ops_cpu_valid(cpu, NULL))
		return NUMA_NO_NODE;

	return cpu_to_node(cpu);
#else
	return 0;
#endif
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
#ifdef CONFIG_SMP
	s32 cpu;
#endif
	if (!ops_cpu_valid(prev_cpu, NULL))
		goto prev_cpu;

	if (!check_builtin_idle_enabled())
		goto prev_cpu;

	if (!scx_kf_allowed(SCX_KF_SELECT_CPU))
		goto prev_cpu;

#ifdef CONFIG_SMP
	cpu = scx_select_cpu_dfl(p, prev_cpu, wake_flags, 0);
	if (cpu >= 0) {
		*is_idle = true;
		return cpu;
	}
#endif

prev_cpu:
	*is_idle = false;
	return prev_cpu;
}

/**
 * scx_bpf_get_idle_cpumask_node - Get a referenced kptr to the
 * idle-tracking per-CPU cpumask of a target NUMA node.
 * @node: target NUMA node
 *
 * Returns an empty cpumask if idle tracking is not enabled, if @node is
 * not valid, or running on a UP kernel. In this case the actual error will
 * be reported to the BPF scheduler via scx_ops_error().
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_cpumask_node(int node)
{
	node = validate_node(node);
	if (node < 0)
		return cpu_none_mask;

#ifdef CONFIG_SMP
	return idle_cpumask(node)->cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_get_idle_cpumask - Get a referenced kptr to the idle-tracking
 * per-CPU cpumask.
 *
 * Returns an empty mask if idle tracking is not enabled, or running on a
 * UP kernel.
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_cpumask(void)
{
	if (static_branch_unlikely(&scx_builtin_idle_per_node)) {
		scx_ops_error("SCX_OPS_BUILTIN_IDLE_PER_NODE enabled");
		return cpu_none_mask;
	}

	if (!check_builtin_idle_enabled())
		return cpu_none_mask;

#ifdef CONFIG_SMP
	return idle_cpumask(NUMA_NO_NODE)->cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_get_idle_smtmask_node - Get a referenced kptr to the
 * idle-tracking, per-physical-core cpumask of a target NUMA node. Can be
 * used to determine if an entire physical core is free.
 * @node: target NUMA node
 *
 * Returns an empty cpumask if idle tracking is not enabled, if @node is
 * not valid, or running on a UP kernel. In this case the actual error will
 * be reported to the BPF scheduler via scx_ops_error().
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_smtmask_node(int node)
{
	node = validate_node(node);
	if (node < 0)
		return cpu_none_mask;

#ifdef CONFIG_SMP
	if (sched_smt_active())
		return idle_cpumask(node)->smt;
	else
		return idle_cpumask(node)->cpu;
#else
	return cpu_none_mask;
#endif
}

/**
 * scx_bpf_get_idle_smtmask - Get a referenced kptr to the idle-tracking,
 * per-physical-core cpumask. Can be used to determine if an entire physical
 * core is free.
 *
 * Returns an empty mask if idle tracking is not enabled, or running on a
 * UP kernel.
 */
__bpf_kfunc const struct cpumask *scx_bpf_get_idle_smtmask(void)
{
	if (static_branch_unlikely(&scx_builtin_idle_per_node)) {
		scx_ops_error("SCX_OPS_BUILTIN_IDLE_PER_NODE enabled");
		return cpu_none_mask;
	}

	if (!check_builtin_idle_enabled())
		return cpu_none_mask;

#ifdef CONFIG_SMP
	if (sched_smt_active())
		return idle_cpumask(NUMA_NO_NODE)->smt;
	else
		return idle_cpumask(NUMA_NO_NODE)->cpu;
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
 * scx_bpf_pick_idle_cpu_node - Pick and claim an idle cpu from @node
 * @cpus_allowed: Allowed cpumask
 * @node: target NUMA node
 * @flags: %SCX_PICK_IDLE_* flags
 *
 * Pick and claim an idle cpu in @cpus_allowed from the NUMA node @node.
 *
 * Returns the picked idle cpu number on success, or -%EBUSY if no matching
 * cpu was found.
 *
 * The search starts from @node and proceeds to other online NUMA nodes in
 * order of increasing distance (unless SCX_PICK_IDLE_IN_NODE is specified,
 * in which case the search is limited to the target @node).
 *
 * Always returns an error if ops.update_idle() is implemented and
 * %SCX_OPS_KEEP_BUILTIN_IDLE is not set, or if
 * %SCX_OPS_BUILTIN_IDLE_PER_NODE is not set.
 */
__bpf_kfunc s32 scx_bpf_pick_idle_cpu_node(const struct cpumask *cpus_allowed,
					   int node, u64 flags)
{
	node = validate_node(node);
	if (node < 0)
		return node;

	return scx_pick_idle_cpu(cpus_allowed, node, flags);
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
 *
 * Always returns an error if %SCX_OPS_BUILTIN_IDLE_PER_NODE is set, use
 * scx_bpf_pick_idle_cpu_node() instead.
 */
__bpf_kfunc s32 scx_bpf_pick_idle_cpu(const struct cpumask *cpus_allowed,
				      u64 flags)
{
	if (static_branch_maybe(CONFIG_NUMA, &scx_builtin_idle_per_node)) {
		scx_ops_error("per-node idle tracking is enabled");
		return -EBUSY;
	}

	if (!check_builtin_idle_enabled())
		return -EBUSY;

	return scx_pick_idle_cpu(cpus_allowed, NUMA_NO_NODE, flags);
}

/**
 * scx_bpf_pick_any_cpu_node - Pick and claim an idle cpu if available
 *			       or pick any CPU from @node
 * @cpus_allowed: Allowed cpumask
 * @node: target NUMA node
 * @flags: %SCX_PICK_IDLE_CPU_* flags
 *
 * Pick and claim an idle cpu in @cpus_allowed. If none is available, pick any
 * CPU in @cpus_allowed. Guaranteed to succeed and returns the picked idle cpu
 * number if @cpus_allowed is not empty. -%EBUSY is returned if @cpus_allowed is
 * empty.
 *
 * The search starts from @node and proceeds to other online NUMA nodes in
 * order of increasing distance (unless %SCX_PICK_IDLE_IN_NODE is specified,
 * in which case the search is limited to the target @node, regardless of
 * the CPU idle state).
 *
 * If ops.update_idle() is implemented and %SCX_OPS_KEEP_BUILTIN_IDLE is not
 * set, this function can't tell which CPUs are idle and will always pick any
 * CPU.
 */
__bpf_kfunc s32 scx_bpf_pick_any_cpu_node(const struct cpumask *cpus_allowed,
					  int node, u64 flags)
{
	s32 cpu;

	node = validate_node(node);
	if (node < 0)
		return node;

	cpu = scx_pick_idle_cpu(cpus_allowed, node, flags);
	if (cpu >= 0)
		return cpu;

	if (flags & SCX_PICK_IDLE_IN_NODE)
		cpu = cpumask_any_and_distribute(cpumask_of_node(node), cpus_allowed);
	else
		cpu = cpumask_any_distribute(cpus_allowed);
	if (cpu < nr_cpu_ids)
		return cpu;
	else
		return -EBUSY;
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
 *
 * Always returns an error if %SCX_OPS_BUILTIN_IDLE_PER_NODE is set, use
 * scx_bpf_pick_any_cpu_node() instead.
 */
__bpf_kfunc s32 scx_bpf_pick_any_cpu(const struct cpumask *cpus_allowed,
				     u64 flags)
{
	s32 cpu;

	if (static_branch_maybe(CONFIG_NUMA, &scx_builtin_idle_per_node)) {
		scx_ops_error("per-node idle tracking is enabled");
		return -EBUSY;
	}

	if (static_branch_likely(&scx_builtin_idle_enabled)) {
		cpu = scx_pick_idle_cpu(cpus_allowed, NUMA_NO_NODE, flags);
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
BTF_ID_FLAGS(func, scx_bpf_cpu_node)
BTF_ID_FLAGS(func, scx_bpf_get_idle_cpumask_node, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_idle_cpumask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_idle_smtmask_node, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_get_idle_smtmask, KF_ACQUIRE)
BTF_ID_FLAGS(func, scx_bpf_put_idle_cpumask, KF_RELEASE)
BTF_ID_FLAGS(func, scx_bpf_test_and_clear_cpu_idle)
BTF_ID_FLAGS(func, scx_bpf_pick_idle_cpu_node, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_pick_idle_cpu, KF_RCU)
BTF_ID_FLAGS(func, scx_bpf_pick_any_cpu_node, KF_RCU)
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
