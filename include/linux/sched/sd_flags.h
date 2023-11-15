/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sched-domains (multiprocessor balancing) flag declarations.
 */

#ifndef SD_FLAG
# error "Incorrect import of SD flags definitions"
#endif

/*
 * Hierarchical metaflags
 *
 * SHARED_CHILD: These flags are meant to be set from the base domain upwards.
 * If a domain has this flag set, all of its children should have it set. This
 * is usually because the flag describes some shared resource (all CPUs in that
 * domain share the same resource), or because they are tied to a scheduling
 * behaviour that we want to disable at some point in the hierarchy for
 * scalability reasons.
 *
 * In those cases it doesn't make sense to have the flag set for a domain but
 * not have it in (some of) its children: sched domains ALWAYS span their child
 * domains, so operations done with parent domains will cover CPUs in the lower
 * child domains.
 *
 *
 * SHARED_PARENT: These flags are meant to be set from the highest domain
 * downwards. If a domain has this flag set, all of its parents should have it
 * set. This is usually for topology properties that start to appear above a
 * certain level (e.g. domain starts spanning CPUs outside of the base CPU's
 * socket).
 */
#define SDF_SHARED_CHILD       0x1
#define SDF_SHARED_PARENT      0x2

/*
 * Behavioural metaflags
 *
 * NEEDS_GROUPS: These flags are only relevant if the domain they are set on has
 * more than one group. This is usually for balancing flags (load balancing
 * involves equalizing a metric between groups), or for flags describing some
 * shared resource (which would be shared between groups).
 */
#define SDF_NEEDS_GROUPS       0x4

/*
 * Balance when about to become idle
 *
 * SHARED_CHILD: Set from the base domain up to cpuset.sched_relax_domain_level.
 * NEEDS_GROUPS: Load balancing flag.
 */
SD_FLAG(SD_BALANCE_NEWIDLE, SDF_SHARED_CHILD | SDF_NEEDS_GROUPS)

/*
 * Balance on exec
 *
 * SHARED_CHILD: Set from the base domain up to the NUMA reclaim level.
 * NEEDS_GROUPS: Load balancing flag.
 */
SD_FLAG(SD_BALANCE_EXEC, SDF_SHARED_CHILD | SDF_NEEDS_GROUPS)

/*
 * Balance on fork, clone
 *
 * SHARED_CHILD: Set from the base domain up to the NUMA reclaim level.
 * NEEDS_GROUPS: Load balancing flag.
 */
SD_FLAG(SD_BALANCE_FORK, SDF_SHARED_CHILD | SDF_NEEDS_GROUPS)

/*
 * Balance on wakeup
 *
 * SHARED_CHILD: Set from the base domain up to cpuset.sched_relax_domain_level.
 * NEEDS_GROUPS: Load balancing flag.
 */
SD_FLAG(SD_BALANCE_WAKE, SDF_SHARED_CHILD | SDF_NEEDS_GROUPS)

/*
 * Consider waking task on waking CPU.
 *
 * SHARED_CHILD: Set from the base domain up to the NUMA reclaim level.
 */
SD_FLAG(SD_WAKE_AFFINE, SDF_SHARED_CHILD)

/*
 * Domain members have different CPU capacities
 *
 * SHARED_PARENT: Set from the topmost domain down to the first domain where
 *                asymmetry is detected.
 * NEEDS_GROUPS: Per-CPU capacity is asymmetric between groups.
 */
SD_FLAG(SD_ASYM_CPUCAPACITY, SDF_SHARED_PARENT | SDF_NEEDS_GROUPS)

/*
 * Domain members have different CPU capacities spanning all unique CPU
 * capacity values.
 *
 * SHARED_PARENT: Set from the topmost domain down to the first domain where
 *		  all available CPU capacities are visible
 * NEEDS_GROUPS: Per-CPU capacity is asymmetric between groups.
 */
SD_FLAG(SD_ASYM_CPUCAPACITY_FULL, SDF_SHARED_PARENT | SDF_NEEDS_GROUPS)

/*
 * Domain members share CPU capacity (i.e. SMT)
 *
 * SHARED_CHILD: Set from the base domain up until spanned CPUs no longer share
 *               CPU capacity.
 * NEEDS_GROUPS: Capacity is shared between groups.
 */
SD_FLAG(SD_SHARE_CPUCAPACITY, SDF_SHARED_CHILD | SDF_NEEDS_GROUPS)

/*
 * Domain members share CPU cluster (LLC tags or L2 cache)
 *
 * NEEDS_GROUPS: Clusters are shared between groups.
 */
SD_FLAG(SD_CLUSTER, SDF_NEEDS_GROUPS)

/*
 * Domain members share CPU package resources (i.e. caches)
 *
 * SHARED_CHILD: Set from the base domain up until spanned CPUs no longer share
 *               the same cache(s).
 * NEEDS_GROUPS: Caches are shared between groups.
 */
SD_FLAG(SD_SHARE_PKG_RESOURCES, SDF_SHARED_CHILD | SDF_NEEDS_GROUPS)

/*
 * Only a single load balancing instance
 *
 * SHARED_PARENT: Set for all NUMA levels above NODE. Could be set from a
 *                different level upwards, but it doesn't change that if a
 *                domain has this flag set, then all of its parents need to have
 *                it too (otherwise the serialization doesn't make sense).
 * NEEDS_GROUPS: No point in preserving domain if it has a single group.
 */
SD_FLAG(SD_SERIALIZE, SDF_SHARED_PARENT | SDF_NEEDS_GROUPS)

/*
 * Place busy tasks earlier in the domain
 *
 * NEEDS_GROUPS: Load balancing flag.
 */
SD_FLAG(SD_ASYM_PACKING, SDF_NEEDS_GROUPS)

/*
 * Prefer to place tasks in a sibling domain
 *
 * Set up until domains start spanning NUMA nodes. Close to being a SHARED_CHILD
 * flag, but cleared below domains with SD_ASYM_CPUCAPACITY.
 *
 * NEEDS_GROUPS: Load balancing flag.
 */
SD_FLAG(SD_PREFER_SIBLING, SDF_NEEDS_GROUPS)

/*
 * sched_groups of this level overlap
 *
 * SHARED_PARENT: Set for all NUMA levels above NODE.
 * NEEDS_GROUPS: Overlaps can only exist with more than one group.
 */
SD_FLAG(SD_OVERLAP, SDF_SHARED_PARENT | SDF_NEEDS_GROUPS)

/*
 * Cross-node balancing
 *
 * SHARED_PARENT: Set for all NUMA levels above NODE.
 * NEEDS_GROUPS: No point in preserving domain if it has a single group.
 */
SD_FLAG(SD_NUMA, SDF_SHARED_PARENT | SDF_NEEDS_GROUPS)
