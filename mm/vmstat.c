// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/vmstat.c
 *
 *  Manages VM statistics
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  zoned VM statistics
 *  Copyright (C) 2006 Silicon Graphics, Inc.,
 *		Christoph Lameter <christoph@lameter.com>
 *  Copyright (C) 2008-2014 Christoph Lameter
 */
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/vmstat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/writeback.h>
#include <linux/compaction.h>
#include <linux/mm_inline.h>
#include <linux/page_ext.h>
#include <linux/page_owner.h>
#include <linux/migrate.h>

#include "internal.h"

#ifdef CONFIG_NUMA
int sysctl_vm_numa_stat = ENABLE_NUMA_STAT;

/* zero numa counters within a zone */
static void zero_zone_numa_counters(struct zone *zone)
{
	int item, cpu;

	for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++) {
		atomic_long_set(&zone->vm_numa_event[item], 0);
		for_each_online_cpu(cpu) {
			per_cpu_ptr(zone->per_cpu_zonestats, cpu)->vm_numa_event[item]
						= 0;
		}
	}
}

/* zero numa counters of all the populated zones */
static void zero_zones_numa_counters(void)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		zero_zone_numa_counters(zone);
}

/* zero global numa counters */
static void zero_global_numa_counters(void)
{
	int item;

	for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++)
		atomic_long_set(&vm_numa_event[item], 0);
}

static void invalid_numa_statistics(void)
{
	zero_zones_numa_counters();
	zero_global_numa_counters();
}

static DEFINE_MUTEX(vm_numa_stat_lock);

int sysctl_vm_numa_stat_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int ret, oldval;

	mutex_lock(&vm_numa_stat_lock);
	if (write)
		oldval = sysctl_vm_numa_stat;
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret || !write)
		goto out;

	if (oldval == sysctl_vm_numa_stat)
		goto out;
	else if (sysctl_vm_numa_stat == ENABLE_NUMA_STAT) {
		static_branch_enable(&vm_numa_stat_key);
		pr_info("enable numa statistics\n");
	} else {
		static_branch_disable(&vm_numa_stat_key);
		invalid_numa_statistics();
		pr_info("disable numa statistics, and clear numa counters\n");
	}

out:
	mutex_unlock(&vm_numa_stat_lock);
	return ret;
}
#endif

#ifdef CONFIG_VM_EVENT_COUNTERS
DEFINE_PER_CPU(struct vm_event_state, vm_event_states) = {{0}};
EXPORT_PER_CPU_SYMBOL(vm_event_states);

static void sum_vm_events(unsigned long *ret)
{
	int cpu;
	int i;

	memset(ret, 0, NR_VM_EVENT_ITEMS * sizeof(unsigned long));

	for_each_online_cpu(cpu) {
		struct vm_event_state *this = &per_cpu(vm_event_states, cpu);

		for (i = 0; i < NR_VM_EVENT_ITEMS; i++)
			ret[i] += this->event[i];
	}
}

/*
 * Accumulate the vm event counters across all CPUs.
 * The result is unavoidably approximate - it can change
 * during and after execution of this function.
*/
void all_vm_events(unsigned long *ret)
{
	cpus_read_lock();
	sum_vm_events(ret);
	cpus_read_unlock();
}
EXPORT_SYMBOL_GPL(all_vm_events);

/*
 * Fold the foreign cpu events into our own.
 *
 * This is adding to the events on one processor
 * but keeps the global counts constant.
 */
void vm_events_fold_cpu(int cpu)
{
	struct vm_event_state *fold_state = &per_cpu(vm_event_states, cpu);
	int i;

	for (i = 0; i < NR_VM_EVENT_ITEMS; i++) {
		count_vm_events(i, fold_state->event[i]);
		fold_state->event[i] = 0;
	}
}

#endif /* CONFIG_VM_EVENT_COUNTERS */

/*
 * Manage combined zone based / global counters
 *
 * vm_stat contains the global counters
 */
atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS] __cacheline_aligned_in_smp;
atomic_long_t vm_numa_event[NR_VM_NUMA_EVENT_ITEMS] __cacheline_aligned_in_smp;
EXPORT_SYMBOL(vm_zone_stat);
EXPORT_SYMBOL(vm_node_stat);

#ifdef CONFIG_NUMA
static void fold_vm_zone_numa_events(struct zone *zone)
{
	unsigned long zone_numa_events[NR_VM_NUMA_EVENT_ITEMS] = { 0, };
	int cpu;
	enum numa_stat_item item;

	for_each_online_cpu(cpu) {
		struct per_cpu_zonestat *pzstats;

		pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);
		for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++)
			zone_numa_events[item] += xchg(&pzstats->vm_numa_event[item], 0);
	}

	for (item = 0; item < NR_VM_NUMA_EVENT_ITEMS; item++)
		zone_numa_event_add(zone_numa_events[item], zone, item);
}

void fold_vm_numa_events(void)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		fold_vm_zone_numa_events(zone);
}
#endif

#ifdef CONFIG_SMP

int calculate_pressure_threshold(struct zone *zone)
{
	int threshold;
	int watermark_distance;

	/*
	 * As vmstats are not up to date, there is drift between the estimated
	 * and real values. For high thresholds and a high number of CPUs, it
	 * is possible for the min watermark to be breached while the estimated
	 * value looks fine. The pressure threshold is a reduced value such
	 * that even the maximum amount of drift will not accidentally breach
	 * the min watermark
	 */
	watermark_distance = low_wmark_pages(zone) - min_wmark_pages(zone);
	threshold = max(1, (int)(watermark_distance / num_online_cpus()));

	/*
	 * Maximum threshold is 125
	 */
	threshold = min(125, threshold);

	return threshold;
}

int calculate_normal_threshold(struct zone *zone)
{
	int threshold;
	int mem;	/* memory in 128 MB units */

	/*
	 * The threshold scales with the number of processors and the amount
	 * of memory per zone. More memory means that we can defer updates for
	 * longer, more processors could lead to more contention.
 	 * fls() is used to have a cheap way of logarithmic scaling.
	 *
	 * Some sample thresholds:
	 *
	 * Threshold	Processors	(fls)	Zonesize	fls(mem)+1
	 * ------------------------------------------------------------------
	 * 8		1		1	0.9-1 GB	4
	 * 16		2		2	0.9-1 GB	4
	 * 20 		2		2	1-2 GB		5
	 * 24		2		2	2-4 GB		6
	 * 28		2		2	4-8 GB		7
	 * 32		2		2	8-16 GB		8
	 * 4		2		2	<128M		1
	 * 30		4		3	2-4 GB		5
	 * 48		4		3	8-16 GB		8
	 * 32		8		4	1-2 GB		4
	 * 32		8		4	0.9-1GB		4
	 * 10		16		5	<128M		1
	 * 40		16		5	900M		4
	 * 70		64		7	2-4 GB		5
	 * 84		64		7	4-8 GB		6
	 * 108		512		9	4-8 GB		6
	 * 125		1024		10	8-16 GB		8
	 * 125		1024		10	16-32 GB	9
	 */

	mem = zone_managed_pages(zone) >> (27 - PAGE_SHIFT);

	threshold = 2 * fls(num_online_cpus()) * (1 + fls(mem));

	/*
	 * Maximum threshold is 125
	 */
	threshold = min(125, threshold);

	return threshold;
}

/*
 * Refresh the thresholds for each zone.
 */
void refresh_zone_stat_thresholds(void)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	int cpu;
	int threshold;

	/* Zero current pgdat thresholds */
	for_each_online_pgdat(pgdat) {
		for_each_online_cpu(cpu) {
			per_cpu_ptr(pgdat->per_cpu_nodestats, cpu)->stat_threshold = 0;
		}
	}

	for_each_populated_zone(zone) {
		struct pglist_data *pgdat = zone->zone_pgdat;
		unsigned long max_drift, tolerate_drift;

		threshold = calculate_normal_threshold(zone);

		for_each_online_cpu(cpu) {
			int pgdat_threshold;

			per_cpu_ptr(zone->per_cpu_zonestats, cpu)->stat_threshold
							= threshold;

			/* Base nodestat threshold on the largest populated zone. */
			pgdat_threshold = per_cpu_ptr(pgdat->per_cpu_nodestats, cpu)->stat_threshold;
			per_cpu_ptr(pgdat->per_cpu_nodestats, cpu)->stat_threshold
				= max(threshold, pgdat_threshold);
		}

		/*
		 * Only set percpu_drift_mark if there is a danger that
		 * NR_FREE_PAGES reports the low watermark is ok when in fact
		 * the min watermark could be breached by an allocation
		 */
		tolerate_drift = low_wmark_pages(zone) - min_wmark_pages(zone);
		max_drift = num_online_cpus() * threshold;
		if (max_drift > tolerate_drift)
			zone->percpu_drift_mark = high_wmark_pages(zone) +
					max_drift;
	}
}

void set_pgdat_percpu_threshold(pg_data_t *pgdat,
				int (*calculate_pressure)(struct zone *))
{
	struct zone *zone;
	int cpu;
	int threshold;
	int i;

	for (i = 0; i < pgdat->nr_zones; i++) {
		zone = &pgdat->node_zones[i];
		if (!zone->percpu_drift_mark)
			continue;

		threshold = (*calculate_pressure)(zone);
		for_each_online_cpu(cpu)
			per_cpu_ptr(zone->per_cpu_zonestats, cpu)->stat_threshold
							= threshold;
	}
}

/*
 * For use when we know that interrupts are disabled,
 * or when we know that preemption is disabled and that
 * particular counter cannot be updated from interrupt context.
 */
void __mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
			   long delta)
{
	struct per_cpu_zonestat __percpu *pcp = zone->per_cpu_zonestats;
	s8 __percpu *p = pcp->vm_stat_diff + item;
	long x;
	long t;

	/*
	 * Accurate vmstat updates require a RMW. On !PREEMPT_RT kernels,
	 * atomicity is provided by IRQs being disabled -- either explicitly
	 * or via local_lock_irq. On PREEMPT_RT, local_lock_irq only disables
	 * CPU migrations and preemption potentially corrupts a counter so
	 * disable preemption.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();

	x = delta + __this_cpu_read(*p);

	t = __this_cpu_read(pcp->stat_threshold);

	if (unlikely(abs(x) > t)) {
		zone_page_state_add(x, zone, item);
		x = 0;
	}
	__this_cpu_write(*p, x);

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}
EXPORT_SYMBOL(__mod_zone_page_state);

void __mod_node_page_state(struct pglist_data *pgdat, enum node_stat_item item,
				long delta)
{
	struct per_cpu_nodestat __percpu *pcp = pgdat->per_cpu_nodestats;
	s8 __percpu *p = pcp->vm_node_stat_diff + item;
	long x;
	long t;

	if (vmstat_item_in_bytes(item)) {
		/*
		 * Only cgroups use subpage accounting right now; at
		 * the global level, these items still change in
		 * multiples of whole pages. Store them as pages
		 * internally to keep the per-cpu counters compact.
		 */
		VM_WARN_ON_ONCE(delta & (PAGE_SIZE - 1));
		delta >>= PAGE_SHIFT;
	}

	/* See __mod_node_page_state */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();

	x = delta + __this_cpu_read(*p);

	t = __this_cpu_read(pcp->stat_threshold);

	if (unlikely(abs(x) > t)) {
		node_page_state_add(x, pgdat, item);
		x = 0;
	}
	__this_cpu_write(*p, x);

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}
EXPORT_SYMBOL(__mod_node_page_state);

/*
 * Optimized increment and decrement functions.
 *
 * These are only for a single page and therefore can take a struct page *
 * argument instead of struct zone *. This allows the inclusion of the code
 * generated for page_zone(page) into the optimized functions.
 *
 * No overflow check is necessary and therefore the differential can be
 * incremented or decremented in place which may allow the compilers to
 * generate better code.
 * The increment or decrement is known and therefore one boundary check can
 * be omitted.
 *
 * NOTE: These functions are very performance sensitive. Change only
 * with care.
 *
 * Some processors have inc/dec instructions that are atomic vs an interrupt.
 * However, the code must first determine the differential location in a zone
 * based on the processor number and then inc/dec the counter. There is no
 * guarantee without disabling preemption that the processor will not change
 * in between and therefore the atomicity vs. interrupt cannot be exploited
 * in a useful way here.
 */
void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
{
	struct per_cpu_zonestat __percpu *pcp = zone->per_cpu_zonestats;
	s8 __percpu *p = pcp->vm_stat_diff + item;
	s8 v, t;

	/* See __mod_node_page_state */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();

	v = __this_cpu_inc_return(*p);
	t = __this_cpu_read(pcp->stat_threshold);
	if (unlikely(v > t)) {
		s8 overstep = t >> 1;

		zone_page_state_add(v + overstep, zone, item);
		__this_cpu_write(*p, -overstep);
	}

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}

void __inc_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	struct per_cpu_nodestat __percpu *pcp = pgdat->per_cpu_nodestats;
	s8 __percpu *p = pcp->vm_node_stat_diff + item;
	s8 v, t;

	VM_WARN_ON_ONCE(vmstat_item_in_bytes(item));

	/* See __mod_node_page_state */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();

	v = __this_cpu_inc_return(*p);
	t = __this_cpu_read(pcp->stat_threshold);
	if (unlikely(v > t)) {
		s8 overstep = t >> 1;

		node_page_state_add(v + overstep, pgdat, item);
		__this_cpu_write(*p, -overstep);
	}

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}

void __inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	__inc_zone_state(page_zone(page), item);
}
EXPORT_SYMBOL(__inc_zone_page_state);

void __inc_node_page_state(struct page *page, enum node_stat_item item)
{
	__inc_node_state(page_pgdat(page), item);
}
EXPORT_SYMBOL(__inc_node_page_state);

void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
{
	struct per_cpu_zonestat __percpu *pcp = zone->per_cpu_zonestats;
	s8 __percpu *p = pcp->vm_stat_diff + item;
	s8 v, t;

	/* See __mod_node_page_state */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();

	v = __this_cpu_dec_return(*p);
	t = __this_cpu_read(pcp->stat_threshold);
	if (unlikely(v < - t)) {
		s8 overstep = t >> 1;

		zone_page_state_add(v - overstep, zone, item);
		__this_cpu_write(*p, overstep);
	}

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}

void __dec_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	struct per_cpu_nodestat __percpu *pcp = pgdat->per_cpu_nodestats;
	s8 __percpu *p = pcp->vm_node_stat_diff + item;
	s8 v, t;

	VM_WARN_ON_ONCE(vmstat_item_in_bytes(item));

	/* See __mod_node_page_state */
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_disable();

	v = __this_cpu_dec_return(*p);
	t = __this_cpu_read(pcp->stat_threshold);
	if (unlikely(v < - t)) {
		s8 overstep = t >> 1;

		node_page_state_add(v - overstep, pgdat, item);
		__this_cpu_write(*p, overstep);
	}

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		preempt_enable();
}

void __dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	__dec_zone_state(page_zone(page), item);
}
EXPORT_SYMBOL(__dec_zone_page_state);

void __dec_node_page_state(struct page *page, enum node_stat_item item)
{
	__dec_node_state(page_pgdat(page), item);
}
EXPORT_SYMBOL(__dec_node_page_state);

#ifdef CONFIG_HAVE_CMPXCHG_LOCAL
/*
 * If we have cmpxchg_local support then we do not need to incur the overhead
 * that comes with local_irq_save/restore if we use this_cpu_cmpxchg.
 *
 * mod_state() modifies the zone counter state through atomic per cpu
 * operations.
 *
 * Overstep mode specifies how overstep should handled:
 *     0       No overstepping
 *     1       Overstepping half of threshold
 *     -1      Overstepping minus half of threshold
*/
static inline void mod_zone_state(struct zone *zone,
       enum zone_stat_item item, long delta, int overstep_mode)
{
	struct per_cpu_zonestat __percpu *pcp = zone->per_cpu_zonestats;
	s8 __percpu *p = pcp->vm_stat_diff + item;
	long o, n, t, z;

	do {
		z = 0;  /* overflow to zone counters */

		/*
		 * The fetching of the stat_threshold is racy. We may apply
		 * a counter threshold to the wrong the cpu if we get
		 * rescheduled while executing here. However, the next
		 * counter update will apply the threshold again and
		 * therefore bring the counter under the threshold again.
		 *
		 * Most of the time the thresholds are the same anyways
		 * for all cpus in a zone.
		 */
		t = this_cpu_read(pcp->stat_threshold);

		o = this_cpu_read(*p);
		n = delta + o;

		if (abs(n) > t) {
			int os = overstep_mode * (t >> 1) ;

			/* Overflow must be added to zone counters */
			z = n + os;
			n = -os;
		}
	} while (this_cpu_cmpxchg(*p, o, n) != o);

	if (z)
		zone_page_state_add(z, zone, item);
}

void mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
			 long delta)
{
	mod_zone_state(zone, item, delta, 0);
}
EXPORT_SYMBOL(mod_zone_page_state);

void inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	mod_zone_state(page_zone(page), item, 1, 1);
}
EXPORT_SYMBOL(inc_zone_page_state);

void dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	mod_zone_state(page_zone(page), item, -1, -1);
}
EXPORT_SYMBOL(dec_zone_page_state);

static inline void mod_node_state(struct pglist_data *pgdat,
       enum node_stat_item item, int delta, int overstep_mode)
{
	struct per_cpu_nodestat __percpu *pcp = pgdat->per_cpu_nodestats;
	s8 __percpu *p = pcp->vm_node_stat_diff + item;
	long o, n, t, z;

	if (vmstat_item_in_bytes(item)) {
		/*
		 * Only cgroups use subpage accounting right now; at
		 * the global level, these items still change in
		 * multiples of whole pages. Store them as pages
		 * internally to keep the per-cpu counters compact.
		 */
		VM_WARN_ON_ONCE(delta & (PAGE_SIZE - 1));
		delta >>= PAGE_SHIFT;
	}

	do {
		z = 0;  /* overflow to node counters */

		/*
		 * The fetching of the stat_threshold is racy. We may apply
		 * a counter threshold to the wrong the cpu if we get
		 * rescheduled while executing here. However, the next
		 * counter update will apply the threshold again and
		 * therefore bring the counter under the threshold again.
		 *
		 * Most of the time the thresholds are the same anyways
		 * for all cpus in a node.
		 */
		t = this_cpu_read(pcp->stat_threshold);

		o = this_cpu_read(*p);
		n = delta + o;

		if (abs(n) > t) {
			int os = overstep_mode * (t >> 1) ;

			/* Overflow must be added to node counters */
			z = n + os;
			n = -os;
		}
	} while (this_cpu_cmpxchg(*p, o, n) != o);

	if (z)
		node_page_state_add(z, pgdat, item);
}

void mod_node_page_state(struct pglist_data *pgdat, enum node_stat_item item,
					long delta)
{
	mod_node_state(pgdat, item, delta, 0);
}
EXPORT_SYMBOL(mod_node_page_state);

void inc_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	mod_node_state(pgdat, item, 1, 1);
}

void inc_node_page_state(struct page *page, enum node_stat_item item)
{
	mod_node_state(page_pgdat(page), item, 1, 1);
}
EXPORT_SYMBOL(inc_node_page_state);

void dec_node_page_state(struct page *page, enum node_stat_item item)
{
	mod_node_state(page_pgdat(page), item, -1, -1);
}
EXPORT_SYMBOL(dec_node_page_state);
#else
/*
 * Use interrupt disable to serialize counter updates
 */
void mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
			 long delta)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_zone_page_state(zone, item, delta);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(mod_zone_page_state);

void inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	unsigned long flags;
	struct zone *zone;

	zone = page_zone(page);
	local_irq_save(flags);
	__inc_zone_state(zone, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(inc_zone_page_state);

void dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__dec_zone_page_state(page, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(dec_zone_page_state);

void inc_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__inc_node_state(pgdat, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(inc_node_state);

void mod_node_page_state(struct pglist_data *pgdat, enum node_stat_item item,
					long delta)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_node_page_state(pgdat, item, delta);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(mod_node_page_state);

void inc_node_page_state(struct page *page, enum node_stat_item item)
{
	unsigned long flags;
	struct pglist_data *pgdat;

	pgdat = page_pgdat(page);
	local_irq_save(flags);
	__inc_node_state(pgdat, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(inc_node_page_state);

void dec_node_page_state(struct page *page, enum node_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__dec_node_page_state(page, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(dec_node_page_state);
#endif

/*
 * Fold a differential into the global counters.
 * Returns the number of counters updated.
 */
static int fold_diff(int *zone_diff, int *node_diff)
{
	int i;
	int changes = 0;

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		if (zone_diff[i]) {
			atomic_long_add(zone_diff[i], &vm_zone_stat[i]);
			changes++;
	}

	for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
		if (node_diff[i]) {
			atomic_long_add(node_diff[i], &vm_node_stat[i]);
			changes++;
	}
	return changes;
}

/*
 * Update the zone counters for the current cpu.
 *
 * Note that refresh_cpu_vm_stats strives to only access
 * node local memory. The per cpu pagesets on remote zones are placed
 * in the memory local to the processor using that pageset. So the
 * loop over all zones will access a series of cachelines local to
 * the processor.
 *
 * The call to zone_page_state_add updates the cachelines with the
 * statistics in the remote zone struct as well as the global cachelines
 * with the global counters. These could cause remote node cache line
 * bouncing and will have to be only done when necessary.
 *
 * The function returns the number of global counters updated.
 */
static int refresh_cpu_vm_stats(bool do_pagesets)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	int i;
	int global_zone_diff[NR_VM_ZONE_STAT_ITEMS] = { 0, };
	int global_node_diff[NR_VM_NODE_STAT_ITEMS] = { 0, };
	int changes = 0;

	for_each_populated_zone(zone) {
		struct per_cpu_zonestat __percpu *pzstats = zone->per_cpu_zonestats;
#ifdef CONFIG_NUMA
		struct per_cpu_pages __percpu *pcp = zone->per_cpu_pageset;
#endif

		for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++) {
			int v;

			v = this_cpu_xchg(pzstats->vm_stat_diff[i], 0);
			if (v) {

				atomic_long_add(v, &zone->vm_stat[i]);
				global_zone_diff[i] += v;
#ifdef CONFIG_NUMA
				/* 3 seconds idle till flush */
				__this_cpu_write(pcp->expire, 3);
#endif
			}
		}
#ifdef CONFIG_NUMA

		if (do_pagesets) {
			cond_resched();
			/*
			 * Deal with draining the remote pageset of this
			 * processor
			 *
			 * Check if there are pages remaining in this pageset
			 * if not then there is nothing to expire.
			 */
			if (!__this_cpu_read(pcp->expire) ||
			       !__this_cpu_read(pcp->count))
				continue;

			/*
			 * We never drain zones local to this processor.
			 */
			if (zone_to_nid(zone) == numa_node_id()) {
				__this_cpu_write(pcp->expire, 0);
				continue;
			}

			if (__this_cpu_dec_return(pcp->expire))
				continue;

			if (__this_cpu_read(pcp->count)) {
				drain_zone_pages(zone, this_cpu_ptr(pcp));
				changes++;
			}
		}
#endif
	}

	for_each_online_pgdat(pgdat) {
		struct per_cpu_nodestat __percpu *p = pgdat->per_cpu_nodestats;

		for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++) {
			int v;

			v = this_cpu_xchg(p->vm_node_stat_diff[i], 0);
			if (v) {
				atomic_long_add(v, &pgdat->vm_stat[i]);
				global_node_diff[i] += v;
			}
		}
	}

	changes += fold_diff(global_zone_diff, global_node_diff);
	return changes;
}

/*
 * Fold the data for an offline cpu into the global array.
 * There cannot be any access by the offline cpu and therefore
 * synchronization is simplified.
 */
void cpu_vm_stats_fold(int cpu)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	int i;
	int global_zone_diff[NR_VM_ZONE_STAT_ITEMS] = { 0, };
	int global_node_diff[NR_VM_NODE_STAT_ITEMS] = { 0, };

	for_each_populated_zone(zone) {
		struct per_cpu_zonestat *pzstats;

		pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);

		for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++) {
			if (pzstats->vm_stat_diff[i]) {
				int v;

				v = pzstats->vm_stat_diff[i];
				pzstats->vm_stat_diff[i] = 0;
				atomic_long_add(v, &zone->vm_stat[i]);
				global_zone_diff[i] += v;
			}
		}
#ifdef CONFIG_NUMA
		for (i = 0; i < NR_VM_NUMA_EVENT_ITEMS; i++) {
			if (pzstats->vm_numa_event[i]) {
				unsigned long v;

				v = pzstats->vm_numa_event[i];
				pzstats->vm_numa_event[i] = 0;
				zone_numa_event_add(v, zone, i);
			}
		}
#endif
	}

	for_each_online_pgdat(pgdat) {
		struct per_cpu_nodestat *p;

		p = per_cpu_ptr(pgdat->per_cpu_nodestats, cpu);

		for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
			if (p->vm_node_stat_diff[i]) {
				int v;

				v = p->vm_node_stat_diff[i];
				p->vm_node_stat_diff[i] = 0;
				atomic_long_add(v, &pgdat->vm_stat[i]);
				global_node_diff[i] += v;
			}
	}

	fold_diff(global_zone_diff, global_node_diff);
}

/*
 * this is only called if !populated_zone(zone), which implies no other users of
 * pset->vm_stat_diff[] exist.
 */
void drain_zonestat(struct zone *zone, struct per_cpu_zonestat *pzstats)
{
	unsigned long v;
	int i;

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++) {
		if (pzstats->vm_stat_diff[i]) {
			v = pzstats->vm_stat_diff[i];
			pzstats->vm_stat_diff[i] = 0;
			zone_page_state_add(v, zone, i);
		}
	}

#ifdef CONFIG_NUMA
	for (i = 0; i < NR_VM_NUMA_EVENT_ITEMS; i++) {
		if (pzstats->vm_numa_event[i]) {
			v = pzstats->vm_numa_event[i];
			pzstats->vm_numa_event[i] = 0;
			zone_numa_event_add(v, zone, i);
		}
	}
#endif
}
#endif

#ifdef CONFIG_NUMA
/*
 * Determine the per node value of a stat item. This function
 * is called frequently in a NUMA machine, so try to be as
 * frugal as possible.
 */
unsigned long sum_zone_node_page_state(int node,
				 enum zone_stat_item item)
{
	struct zone *zones = NODE_DATA(node)->node_zones;
	int i;
	unsigned long count = 0;

	for (i = 0; i < MAX_NR_ZONES; i++)
		count += zone_page_state(zones + i, item);

	return count;
}

/* Determine the per node value of a numa stat item. */
unsigned long sum_zone_numa_event_state(int node,
				 enum numa_stat_item item)
{
	struct zone *zones = NODE_DATA(node)->node_zones;
	unsigned long count = 0;
	int i;

	for (i = 0; i < MAX_NR_ZONES; i++)
		count += zone_numa_event_state(zones + i, item);

	return count;
}

/*
 * Determine the per node value of a stat item.
 */
unsigned long node_page_state_pages(struct pglist_data *pgdat,
				    enum node_stat_item item)
{
	long x = atomic_long_read(&pgdat->vm_stat[item]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

unsigned long node_page_state(struct pglist_data *pgdat,
			      enum node_stat_item item)
{
	VM_WARN_ON_ONCE(vmstat_item_in_bytes(item));

	return node_page_state_pages(pgdat, item);
}
#endif

#ifdef CONFIG_COMPACTION

struct contig_page_info {
	unsigned long free_pages;
	unsigned long free_blocks_total;
	unsigned long free_blocks_suitable;
};

/*
 * Calculate the number of free pages in a zone, how many contiguous
 * pages are free and how many are large enough to satisfy an allocation of
 * the target size. Note that this function makes no attempt to estimate
 * how many suitable free blocks there *might* be if MOVABLE pages were
 * migrated. Calculating that is possible, but expensive and can be
 * figured out from userspace
 */
static void fill_contig_page_info(struct zone *zone,
				unsigned int suitable_order,
				struct contig_page_info *info)
{
	unsigned int order;

	info->free_pages = 0;
	info->free_blocks_total = 0;
	info->free_blocks_suitable = 0;

	for (order = 0; order < MAX_ORDER; order++) {
		unsigned long blocks;

		/*
		 * Count number of free blocks.
		 *
		 * Access to nr_free is lockless as nr_free is used only for
		 * diagnostic purposes. Use data_race to avoid KCSAN warning.
		 */
		blocks = data_race(zone->free_area[order].nr_free);
		info->free_blocks_total += blocks;

		/* Count free base pages */
		info->free_pages += blocks << order;

		/* Count the suitable free blocks */
		if (order >= suitable_order)
			info->free_blocks_suitable += blocks <<
						(order - suitable_order);
	}
}

/*
 * A fragmentation index only makes sense if an allocation of a requested
 * size would fail. If that is true, the fragmentation index indicates
 * whether external fragmentation or a lack of memory was the problem.
 * The value can be used to determine if page reclaim or compaction
 * should be used
 */
static int __fragmentation_index(unsigned int order, struct contig_page_info *info)
{
	unsigned long requested = 1UL << order;

	if (WARN_ON_ONCE(order >= MAX_ORDER))
		return 0;

	if (!info->free_blocks_total)
		return 0;

	/* Fragmentation index only makes sense when a request would fail */
	if (info->free_blocks_suitable)
		return -1000;

	/*
	 * Index is between 0 and 1 so return within 3 decimal places
	 *
	 * 0 => allocation would fail due to lack of memory
	 * 1 => allocation would fail due to fragmentation
	 */
	return 1000 - div_u64( (1000+(div_u64(info->free_pages * 1000ULL, requested))), info->free_blocks_total);
}

/*
 * Calculates external fragmentation within a zone wrt the given order.
 * It is defined as the percentage of pages found in blocks of size
 * less than 1 << order. It returns values in range [0, 100].
 */
unsigned int extfrag_for_order(struct zone *zone, unsigned int order)
{
	struct contig_page_info info;

	fill_contig_page_info(zone, order, &info);
	if (info.free_pages == 0)
		return 0;

	return div_u64((info.free_pages -
			(info.free_blocks_suitable << order)) * 100,
			info.free_pages);
}

/* Same as __fragmentation index but allocs contig_page_info on stack */
int fragmentation_index(struct zone *zone, unsigned int order)
{
	struct contig_page_info info;

	fill_contig_page_info(zone, order, &info);
	return __fragmentation_index(order, &info);
}
#endif

#if defined(CONFIG_PROC_FS) || defined(CONFIG_SYSFS) || \
    defined(CONFIG_NUMA) || defined(CONFIG_MEMCG)
#ifdef CONFIG_ZONE_DMA
#define TEXT_FOR_DMA(xx) xx "_dma",
#else
#define TEXT_FOR_DMA(xx)
#endif

#ifdef CONFIG_ZONE_DMA32
#define TEXT_FOR_DMA32(xx) xx "_dma32",
#else
#define TEXT_FOR_DMA32(xx)
#endif

#ifdef CONFIG_HIGHMEM
#define TEXT_FOR_HIGHMEM(xx) xx "_high",
#else
#define TEXT_FOR_HIGHMEM(xx)
#endif

#define TEXTS_FOR_ZONES(xx) TEXT_FOR_DMA(xx) TEXT_FOR_DMA32(xx) xx "_normal", \
					TEXT_FOR_HIGHMEM(xx) xx "_movable",

const char * const vmstat_text[] = {
	/* enum zone_stat_item counters */
	"nr_free_pages",
	"nr_zone_inactive_anon",
	"nr_zone_active_anon",
	"nr_zone_inactive_file",
	"nr_zone_active_file",
	"nr_zone_unevictable",
	"nr_zone_write_pending",
	"nr_mlock",
	"nr_bounce",
#if IS_ENABLED(CONFIG_ZSMALLOC)
	"nr_zspages",
#endif
	"nr_free_cma",

	/* enum numa_stat_item counters */
#ifdef CONFIG_NUMA
	"numa_hit",
	"numa_miss",
	"numa_foreign",
	"numa_interleave",
	"numa_local",
	"numa_other",
#endif

	/* enum node_stat_item counters */
	"nr_inactive_anon",
	"nr_active_anon",
	"nr_inactive_file",
	"nr_active_file",
	"nr_unevictable",
	"nr_slab_reclaimable",
	"nr_slab_unreclaimable",
	"nr_isolated_anon",
	"nr_isolated_file",
	"workingset_nodes",
	"workingset_refault_anon",
	"workingset_refault_file",
	"workingset_activate_anon",
	"workingset_activate_file",
	"workingset_restore_anon",
	"workingset_restore_file",
	"workingset_nodereclaim",
	"nr_anon_pages",
	"nr_mapped",
	"nr_file_pages",
	"nr_dirty",
	"nr_writeback",
	"nr_writeback_temp",
	"nr_shmem",
	"nr_shmem_hugepages",
	"nr_shmem_pmdmapped",
	"nr_file_hugepages",
	"nr_file_pmdmapped",
	"nr_anon_transparent_hugepages",
	"nr_vmscan_write",
	"nr_vmscan_immediate_reclaim",
	"nr_dirtied",
	"nr_written",
	"nr_throttled_written",
	"nr_kernel_misc_reclaimable",
	"nr_foll_pin_acquired",
	"nr_foll_pin_released",
	"nr_kernel_stack",
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
	"nr_shadow_call_stack",
#endif
	"nr_page_table_pages",
#ifdef CONFIG_SWAP
	"nr_swapcached",
#endif
#ifdef CONFIG_NUMA_BALANCING
	"pgpromote_success",
#endif

	/* enum writeback_stat_item counters */
	"nr_dirty_threshold",
	"nr_dirty_background_threshold",

#if defined(CONFIG_VM_EVENT_COUNTERS) || defined(CONFIG_MEMCG)
	/* enum vm_event_item counters */
	"pgpgin",
	"pgpgout",
	"pswpin",
	"pswpout",

	TEXTS_FOR_ZONES("pgalloc")
	TEXTS_FOR_ZONES("allocstall")
	TEXTS_FOR_ZONES("pgskip")

	"pgfree",
	"pgactivate",
	"pgdeactivate",
	"pglazyfree",

	"pgfault",
	"pgmajfault",
	"pglazyfreed",

	"pgrefill",
	"pgreuse",
	"pgsteal_kswapd",
	"pgsteal_direct",
	"pgdemote_kswapd",
	"pgdemote_direct",
	"pgscan_kswapd",
	"pgscan_direct",
	"pgscan_direct_throttle",
	"pgscan_anon",
	"pgscan_file",
	"pgsteal_anon",
	"pgsteal_file",

#ifdef CONFIG_NUMA
	"zone_reclaim_failed",
#endif
	"pginodesteal",
	"slabs_scanned",
	"kswapd_inodesteal",
	"kswapd_low_wmark_hit_quickly",
	"kswapd_high_wmark_hit_quickly",
	"pageoutrun",

	"pgrotated",

	"drop_pagecache",
	"drop_slab",
	"oom_kill",

#ifdef CONFIG_NUMA_BALANCING
	"numa_pte_updates",
	"numa_huge_pte_updates",
	"numa_hint_faults",
	"numa_hint_faults_local",
	"numa_pages_migrated",
#endif
#ifdef CONFIG_MIGRATION
	"pgmigrate_success",
	"pgmigrate_fail",
	"thp_migration_success",
	"thp_migration_fail",
	"thp_migration_split",
#endif
#ifdef CONFIG_COMPACTION
	"compact_migrate_scanned",
	"compact_free_scanned",
	"compact_isolated",
	"compact_stall",
	"compact_fail",
	"compact_success",
	"compact_daemon_wake",
	"compact_daemon_migrate_scanned",
	"compact_daemon_free_scanned",
#endif

#ifdef CONFIG_HUGETLB_PAGE
	"htlb_buddy_alloc_success",
	"htlb_buddy_alloc_fail",
#endif
#ifdef CONFIG_CMA
	"cma_alloc_success",
	"cma_alloc_fail",
#endif
	"unevictable_pgs_culled",
	"unevictable_pgs_scanned",
	"unevictable_pgs_rescued",
	"unevictable_pgs_mlocked",
	"unevictable_pgs_munlocked",
	"unevictable_pgs_cleared",
	"unevictable_pgs_stranded",

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	"thp_fault_alloc",
	"thp_fault_fallback",
	"thp_fault_fallback_charge",
	"thp_collapse_alloc",
	"thp_collapse_alloc_failed",
	"thp_file_alloc",
	"thp_file_fallback",
	"thp_file_fallback_charge",
	"thp_file_mapped",
	"thp_split_page",
	"thp_split_page_failed",
	"thp_deferred_split_page",
	"thp_split_pmd",
	"thp_scan_exceed_none_pte",
	"thp_scan_exceed_swap_pte",
	"thp_scan_exceed_share_pte",
#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
	"thp_split_pud",
#endif
	"thp_zero_page_alloc",
	"thp_zero_page_alloc_failed",
	"thp_swpout",
	"thp_swpout_fallback",
#endif
#ifdef CONFIG_MEMORY_BALLOON
	"balloon_inflate",
	"balloon_deflate",
#ifdef CONFIG_BALLOON_COMPACTION
	"balloon_migrate",
#endif
#endif /* CONFIG_MEMORY_BALLOON */
#ifdef CONFIG_DEBUG_TLBFLUSH
	"nr_tlb_remote_flush",
	"nr_tlb_remote_flush_received",
	"nr_tlb_local_flush_all",
	"nr_tlb_local_flush_one",
#endif /* CONFIG_DEBUG_TLBFLUSH */

#ifdef CONFIG_DEBUG_VM_VMACACHE
	"vmacache_find_calls",
	"vmacache_find_hits",
#endif
#ifdef CONFIG_SWAP
	"swap_ra",
	"swap_ra_hit",
#ifdef CONFIG_KSM
	"ksm_swpin_copy",
#endif
#endif
#ifdef CONFIG_KSM
	"cow_ksm",
#endif
#ifdef CONFIG_ZSWAP
	"zswpin",
	"zswpout",
#endif
#ifdef CONFIG_X86
	"direct_map_level2_splits",
	"direct_map_level3_splits",
#endif
#endif /* CONFIG_VM_EVENT_COUNTERS || CONFIG_MEMCG */
};
#endif /* CONFIG_PROC_FS || CONFIG_SYSFS || CONFIG_NUMA || CONFIG_MEMCG */

#if (defined(CONFIG_DEBUG_FS) && defined(CONFIG_COMPACTION)) || \
     defined(CONFIG_PROC_FS)
static void *frag_start(struct seq_file *m, loff_t *pos)
{
	pg_data_t *pgdat;
	loff_t node = *pos;

	for (pgdat = first_online_pgdat();
	     pgdat && node;
	     pgdat = next_online_pgdat(pgdat))
		--node;

	return pgdat;
}

static void *frag_next(struct seq_file *m, void *arg, loff_t *pos)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	(*pos)++;
	return next_online_pgdat(pgdat);
}

static void frag_stop(struct seq_file *m, void *arg)
{
}

/*
 * Walk zones in a node and print using a callback.
 * If @assert_populated is true, only use callback for zones that are populated.
 */
static void walk_zones_in_node(struct seq_file *m, pg_data_t *pgdat,
		bool assert_populated, bool nolock,
		void (*print)(struct seq_file *m, pg_data_t *, struct zone *))
{
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (assert_populated && !populated_zone(zone))
			continue;

		if (!nolock)
			spin_lock_irqsave(&zone->lock, flags);
		print(m, pgdat, zone);
		if (!nolock)
			spin_unlock_irqrestore(&zone->lock, flags);
	}
}
#endif

#ifdef CONFIG_PROC_FS
static void frag_show_print(struct seq_file *m, pg_data_t *pgdat,
						struct zone *zone)
{
	int order;

	seq_printf(m, "Node %d, zone %8s ", pgdat->node_id, zone->name);
	for (order = 0; order < MAX_ORDER; ++order)
		/*
		 * Access to nr_free is lockless as nr_free is used only for
		 * printing purposes. Use data_race to avoid KCSAN warning.
		 */
		seq_printf(m, "%6lu ", data_race(zone->free_area[order].nr_free));
	seq_putc(m, '\n');
}

/*
 * This walks the free areas for each zone.
 */
static int frag_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	walk_zones_in_node(m, pgdat, true, false, frag_show_print);
	return 0;
}

static void pagetypeinfo_showfree_print(struct seq_file *m,
					pg_data_t *pgdat, struct zone *zone)
{
	int order, mtype;

	for (mtype = 0; mtype < MIGRATE_TYPES; mtype++) {
		seq_printf(m, "Node %4d, zone %8s, type %12s ",
					pgdat->node_id,
					zone->name,
					migratetype_names[mtype]);
		for (order = 0; order < MAX_ORDER; ++order) {
			unsigned long freecount = 0;
			struct free_area *area;
			struct list_head *curr;
			bool overflow = false;

			area = &(zone->free_area[order]);

			list_for_each(curr, &area->free_list[mtype]) {
				/*
				 * Cap the free_list iteration because it might
				 * be really large and we are under a spinlock
				 * so a long time spent here could trigger a
				 * hard lockup detector. Anyway this is a
				 * debugging tool so knowing there is a handful
				 * of pages of this order should be more than
				 * sufficient.
				 */
				if (++freecount >= 100000) {
					overflow = true;
					break;
				}
			}
			seq_printf(m, "%s%6lu ", overflow ? ">" : "", freecount);
			spin_unlock_irq(&zone->lock);
			cond_resched();
			spin_lock_irq(&zone->lock);
		}
		seq_putc(m, '\n');
	}
}

/* Print out the free pages at each order for each migatetype */
static void pagetypeinfo_showfree(struct seq_file *m, void *arg)
{
	int order;
	pg_data_t *pgdat = (pg_data_t *)arg;

	/* Print header */
	seq_printf(m, "%-43s ", "Free pages count per migrate type at order");
	for (order = 0; order < MAX_ORDER; ++order)
		seq_printf(m, "%6d ", order);
	seq_putc(m, '\n');

	walk_zones_in_node(m, pgdat, true, false, pagetypeinfo_showfree_print);
}

static void pagetypeinfo_showblockcount_print(struct seq_file *m,
					pg_data_t *pgdat, struct zone *zone)
{
	int mtype;
	unsigned long pfn;
	unsigned long start_pfn = zone->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(zone);
	unsigned long count[MIGRATE_TYPES] = { 0, };

	for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages) {
		struct page *page;

		page = pfn_to_online_page(pfn);
		if (!page)
			continue;

		if (page_zone(page) != zone)
			continue;

		mtype = get_pageblock_migratetype(page);

		if (mtype < MIGRATE_TYPES)
			count[mtype]++;
	}

	/* Print counts */
	seq_printf(m, "Node %d, zone %8s ", pgdat->node_id, zone->name);
	for (mtype = 0; mtype < MIGRATE_TYPES; mtype++)
		seq_printf(m, "%12lu ", count[mtype]);
	seq_putc(m, '\n');
}

/* Print out the number of pageblocks for each migratetype */
static void pagetypeinfo_showblockcount(struct seq_file *m, void *arg)
{
	int mtype;
	pg_data_t *pgdat = (pg_data_t *)arg;

	seq_printf(m, "\n%-23s", "Number of blocks type ");
	for (mtype = 0; mtype < MIGRATE_TYPES; mtype++)
		seq_printf(m, "%12s ", migratetype_names[mtype]);
	seq_putc(m, '\n');
	walk_zones_in_node(m, pgdat, true, false,
		pagetypeinfo_showblockcount_print);
}

/*
 * Print out the number of pageblocks for each migratetype that contain pages
 * of other types. This gives an indication of how well fallbacks are being
 * contained by rmqueue_fallback(). It requires information from PAGE_OWNER
 * to determine what is going on
 */
static void pagetypeinfo_showmixedcount(struct seq_file *m, pg_data_t *pgdat)
{
#ifdef CONFIG_PAGE_OWNER
	int mtype;

	if (!static_branch_unlikely(&page_owner_inited))
		return;

	drain_all_pages(NULL);

	seq_printf(m, "\n%-23s", "Number of mixed blocks ");
	for (mtype = 0; mtype < MIGRATE_TYPES; mtype++)
		seq_printf(m, "%12s ", migratetype_names[mtype]);
	seq_putc(m, '\n');

	walk_zones_in_node(m, pgdat, true, true,
		pagetypeinfo_showmixedcount_print);
#endif /* CONFIG_PAGE_OWNER */
}

/*
 * This prints out statistics in relation to grouping pages by mobility.
 * It is expensive to collect so do not constantly read the file.
 */
static int pagetypeinfo_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	/* check memoryless node */
	if (!node_state(pgdat->node_id, N_MEMORY))
		return 0;

	seq_printf(m, "Page block order: %d\n", pageblock_order);
	seq_printf(m, "Pages per block:  %lu\n", pageblock_nr_pages);
	seq_putc(m, '\n');
	pagetypeinfo_showfree(m, pgdat);
	pagetypeinfo_showblockcount(m, pgdat);
	pagetypeinfo_showmixedcount(m, pgdat);

	return 0;
}

static const struct seq_operations fragmentation_op = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= frag_show,
};

static const struct seq_operations pagetypeinfo_op = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= pagetypeinfo_show,
};

static bool is_zone_first_populated(pg_data_t *pgdat, struct zone *zone)
{
	int zid;

	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		struct zone *compare = &pgdat->node_zones[zid];

		if (populated_zone(compare))
			return zone == compare;
	}

	return false;
}

static void zoneinfo_show_print(struct seq_file *m, pg_data_t *pgdat,
							struct zone *zone)
{
	int i;
	seq_printf(m, "Node %d, zone %8s", pgdat->node_id, zone->name);
	if (is_zone_first_populated(pgdat, zone)) {
		seq_printf(m, "\n  per-node stats");
		for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++) {
			unsigned long pages = node_page_state_pages(pgdat, i);

			if (vmstat_item_print_in_thp(i))
				pages /= HPAGE_PMD_NR;
			seq_printf(m, "\n      %-12s %lu", node_stat_name(i),
				   pages);
		}
	}
	seq_printf(m,
		   "\n  pages free     %lu"
		   "\n        boost    %lu"
		   "\n        min      %lu"
		   "\n        low      %lu"
		   "\n        high     %lu"
		   "\n        spanned  %lu"
		   "\n        present  %lu"
		   "\n        managed  %lu"
		   "\n        cma      %lu",
		   zone_page_state(zone, NR_FREE_PAGES),
		   zone->watermark_boost,
		   min_wmark_pages(zone),
		   low_wmark_pages(zone),
		   high_wmark_pages(zone),
		   zone->spanned_pages,
		   zone->present_pages,
		   zone_managed_pages(zone),
		   zone_cma_pages(zone));

	seq_printf(m,
		   "\n        protection: (%ld",
		   zone->lowmem_reserve[0]);
	for (i = 1; i < ARRAY_SIZE(zone->lowmem_reserve); i++)
		seq_printf(m, ", %ld", zone->lowmem_reserve[i]);
	seq_putc(m, ')');

	/* If unpopulated, no other information is useful */
	if (!populated_zone(zone)) {
		seq_putc(m, '\n');
		return;
	}

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		seq_printf(m, "\n      %-12s %lu", zone_stat_name(i),
			   zone_page_state(zone, i));

#ifdef CONFIG_NUMA
	for (i = 0; i < NR_VM_NUMA_EVENT_ITEMS; i++)
		seq_printf(m, "\n      %-12s %lu", numa_stat_name(i),
			   zone_numa_event_state(zone, i));
#endif

	seq_printf(m, "\n  pagesets");
	for_each_online_cpu(i) {
		struct per_cpu_pages *pcp;
		struct per_cpu_zonestat __maybe_unused *pzstats;

		pcp = per_cpu_ptr(zone->per_cpu_pageset, i);
		seq_printf(m,
			   "\n    cpu: %i"
			   "\n              count: %i"
			   "\n              high:  %i"
			   "\n              batch: %i",
			   i,
			   pcp->count,
			   pcp->high,
			   pcp->batch);
#ifdef CONFIG_SMP
		pzstats = per_cpu_ptr(zone->per_cpu_zonestats, i);
		seq_printf(m, "\n  vm stats threshold: %d",
				pzstats->stat_threshold);
#endif
	}
	seq_printf(m,
		   "\n  node_unreclaimable:  %u"
		   "\n  start_pfn:           %lu",
		   pgdat->kswapd_failures >= MAX_RECLAIM_RETRIES,
		   zone->zone_start_pfn);
	seq_putc(m, '\n');
}

/*
 * Output information about zones in @pgdat.  All zones are printed regardless
 * of whether they are populated or not: lowmem_reserve_ratio operates on the
 * set of all zones and userspace would not be aware of such zones if they are
 * suppressed here (zoneinfo displays the effect of lowmem_reserve_ratio).
 */
static int zoneinfo_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	walk_zones_in_node(m, pgdat, false, false, zoneinfo_show_print);
	return 0;
}

static const struct seq_operations zoneinfo_op = {
	.start	= frag_start, /* iterate over all zones. The same as in
			       * fragmentation. */
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= zoneinfo_show,
};

#define NR_VMSTAT_ITEMS (NR_VM_ZONE_STAT_ITEMS + \
			 NR_VM_NUMA_EVENT_ITEMS + \
			 NR_VM_NODE_STAT_ITEMS + \
			 NR_VM_WRITEBACK_STAT_ITEMS + \
			 (IS_ENABLED(CONFIG_VM_EVENT_COUNTERS) ? \
			  NR_VM_EVENT_ITEMS : 0))

static void *vmstat_start(struct seq_file *m, loff_t *pos)
{
	unsigned long *v;
	int i;

	if (*pos >= NR_VMSTAT_ITEMS)
		return NULL;

	BUILD_BUG_ON(ARRAY_SIZE(vmstat_text) < NR_VMSTAT_ITEMS);
	fold_vm_numa_events();
	v = kmalloc_array(NR_VMSTAT_ITEMS, sizeof(unsigned long), GFP_KERNEL);
	m->private = v;
	if (!v)
		return ERR_PTR(-ENOMEM);
	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		v[i] = global_zone_page_state(i);
	v += NR_VM_ZONE_STAT_ITEMS;

#ifdef CONFIG_NUMA
	for (i = 0; i < NR_VM_NUMA_EVENT_ITEMS; i++)
		v[i] = global_numa_event_state(i);
	v += NR_VM_NUMA_EVENT_ITEMS;
#endif

	for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++) {
		v[i] = global_node_page_state_pages(i);
		if (vmstat_item_print_in_thp(i))
			v[i] /= HPAGE_PMD_NR;
	}
	v += NR_VM_NODE_STAT_ITEMS;

	global_dirty_limits(v + NR_DIRTY_BG_THRESHOLD,
			    v + NR_DIRTY_THRESHOLD);
	v += NR_VM_WRITEBACK_STAT_ITEMS;

#ifdef CONFIG_VM_EVENT_COUNTERS
	all_vm_events(v);
	v[PGPGIN] /= 2;		/* sectors -> kbytes */
	v[PGPGOUT] /= 2;
#endif
	return (unsigned long *)m->private + *pos;
}

static void *vmstat_next(struct seq_file *m, void *arg, loff_t *pos)
{
	(*pos)++;
	if (*pos >= NR_VMSTAT_ITEMS)
		return NULL;
	return (unsigned long *)m->private + *pos;
}

static int vmstat_show(struct seq_file *m, void *arg)
{
	unsigned long *l = arg;
	unsigned long off = l - (unsigned long *)m->private;

	seq_puts(m, vmstat_text[off]);
	seq_put_decimal_ull(m, " ", *l);
	seq_putc(m, '\n');

	if (off == NR_VMSTAT_ITEMS - 1) {
		/*
		 * We've come to the end - add any deprecated counters to avoid
		 * breaking userspace which might depend on them being present.
		 */
		seq_puts(m, "nr_unstable 0\n");
	}
	return 0;
}

static void vmstat_stop(struct seq_file *m, void *arg)
{
	kfree(m->private);
	m->private = NULL;
}

static const struct seq_operations vmstat_op = {
	.start	= vmstat_start,
	.next	= vmstat_next,
	.stop	= vmstat_stop,
	.show	= vmstat_show,
};
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct delayed_work, vmstat_work);
int sysctl_stat_interval __read_mostly = HZ;

#ifdef CONFIG_PROC_FS
static void refresh_vm_stats(struct work_struct *work)
{
	refresh_cpu_vm_stats(true);
}

int vmstat_refresh(struct ctl_table *table, int write,
		   void *buffer, size_t *lenp, loff_t *ppos)
{
	long val;
	int err;
	int i;

	/*
	 * The regular update, every sysctl_stat_interval, may come later
	 * than expected: leaving a significant amount in per_cpu buckets.
	 * This is particularly misleading when checking a quantity of HUGE
	 * pages, immediately after running a test.  /proc/sys/vm/stat_refresh,
	 * which can equally be echo'ed to or cat'ted from (by root),
	 * can be used to update the stats just before reading them.
	 *
	 * Oh, and since global_zone_page_state() etc. are so careful to hide
	 * transiently negative values, report an error here if any of
	 * the stats is negative, so we know to go looking for imbalance.
	 */
	err = schedule_on_each_cpu(refresh_vm_stats);
	if (err)
		return err;
	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++) {
		/*
		 * Skip checking stats known to go negative occasionally.
		 */
		switch (i) {
		case NR_ZONE_WRITE_PENDING:
		case NR_FREE_CMA_PAGES:
			continue;
		}
		val = atomic_long_read(&vm_zone_stat[i]);
		if (val < 0) {
			pr_warn("%s: %s %ld\n",
				__func__, zone_stat_name(i), val);
		}
	}
	for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++) {
		/*
		 * Skip checking stats known to go negative occasionally.
		 */
		switch (i) {
		case NR_WRITEBACK:
			continue;
		}
		val = atomic_long_read(&vm_node_stat[i]);
		if (val < 0) {
			pr_warn("%s: %s %ld\n",
				__func__, node_stat_name(i), val);
		}
	}
	if (write)
		*ppos += *lenp;
	else
		*lenp = 0;
	return 0;
}
#endif /* CONFIG_PROC_FS */

static void vmstat_update(struct work_struct *w)
{
	if (refresh_cpu_vm_stats(true)) {
		/*
		 * Counters were updated so we expect more updates
		 * to occur in the future. Keep on running the
		 * update worker thread.
		 */
		queue_delayed_work_on(smp_processor_id(), mm_percpu_wq,
				this_cpu_ptr(&vmstat_work),
				round_jiffies_relative(sysctl_stat_interval));
	}
}

/*
 * Check if the diffs for a certain cpu indicate that
 * an update is needed.
 */
static bool need_update(int cpu)
{
	pg_data_t *last_pgdat = NULL;
	struct zone *zone;

	for_each_populated_zone(zone) {
		struct per_cpu_zonestat *pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);
		struct per_cpu_nodestat *n;

		/*
		 * The fast way of checking if there are any vmstat diffs.
		 */
		if (memchr_inv(pzstats->vm_stat_diff, 0, sizeof(pzstats->vm_stat_diff)))
			return true;

		if (last_pgdat == zone->zone_pgdat)
			continue;
		last_pgdat = zone->zone_pgdat;
		n = per_cpu_ptr(zone->zone_pgdat->per_cpu_nodestats, cpu);
		if (memchr_inv(n->vm_node_stat_diff, 0, sizeof(n->vm_node_stat_diff)))
			return true;
	}
	return false;
}

/*
 * Switch off vmstat processing and then fold all the remaining differentials
 * until the diffs stay at zero. The function is used by NOHZ and can only be
 * invoked when tick processing is not active.
 */
void quiet_vmstat(void)
{
	if (system_state != SYSTEM_RUNNING)
		return;

	if (!delayed_work_pending(this_cpu_ptr(&vmstat_work)))
		return;

	if (!need_update(smp_processor_id()))
		return;

	/*
	 * Just refresh counters and do not care about the pending delayed
	 * vmstat_update. It doesn't fire that often to matter and canceling
	 * it would be too expensive from this path.
	 * vmstat_shepherd will take care about that for us.
	 */
	refresh_cpu_vm_stats(false);
}

/*
 * Shepherd worker thread that checks the
 * differentials of processors that have their worker
 * threads for vm statistics updates disabled because of
 * inactivity.
 */
static void vmstat_shepherd(struct work_struct *w);

static DECLARE_DEFERRABLE_WORK(shepherd, vmstat_shepherd);

static void vmstat_shepherd(struct work_struct *w)
{
	int cpu;

	cpus_read_lock();
	/* Check processors whose vmstat worker threads have been disabled */
	for_each_online_cpu(cpu) {
		struct delayed_work *dw = &per_cpu(vmstat_work, cpu);

		if (!delayed_work_pending(dw) && need_update(cpu))
			queue_delayed_work_on(cpu, mm_percpu_wq, dw, 0);

		cond_resched();
	}
	cpus_read_unlock();

	schedule_delayed_work(&shepherd,
		round_jiffies_relative(sysctl_stat_interval));
}

static void __init start_shepherd_timer(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		INIT_DEFERRABLE_WORK(per_cpu_ptr(&vmstat_work, cpu),
			vmstat_update);

	schedule_delayed_work(&shepherd,
		round_jiffies_relative(sysctl_stat_interval));
}

static void __init init_cpu_node_state(void)
{
	int node;

	for_each_online_node(node) {
		if (!cpumask_empty(cpumask_of_node(node)))
			node_set_state(node, N_CPU);
	}
}

static int vmstat_cpu_online(unsigned int cpu)
{
	refresh_zone_stat_thresholds();

	if (!node_state(cpu_to_node(cpu), N_CPU)) {
		node_set_state(cpu_to_node(cpu), N_CPU);
		set_migration_target_nodes();
	}

	return 0;
}

static int vmstat_cpu_down_prep(unsigned int cpu)
{
	cancel_delayed_work_sync(&per_cpu(vmstat_work, cpu));
	return 0;
}

static int vmstat_cpu_dead(unsigned int cpu)
{
	const struct cpumask *node_cpus;
	int node;

	node = cpu_to_node(cpu);

	refresh_zone_stat_thresholds();
	node_cpus = cpumask_of_node(node);
	if (!cpumask_empty(node_cpus))
		return 0;

	node_clear_state(node, N_CPU);
	set_migration_target_nodes();

	return 0;
}

#endif

struct workqueue_struct *mm_percpu_wq;

void __init init_mm_internals(void)
{
	int ret __maybe_unused;

	mm_percpu_wq = alloc_workqueue("mm_percpu_wq", WQ_MEM_RECLAIM, 0);

#ifdef CONFIG_SMP
	ret = cpuhp_setup_state_nocalls(CPUHP_MM_VMSTAT_DEAD, "mm/vmstat:dead",
					NULL, vmstat_cpu_dead);
	if (ret < 0)
		pr_err("vmstat: failed to register 'dead' hotplug state\n");

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "mm/vmstat:online",
					vmstat_cpu_online,
					vmstat_cpu_down_prep);
	if (ret < 0)
		pr_err("vmstat: failed to register 'online' hotplug state\n");

	cpus_read_lock();
	init_cpu_node_state();
	cpus_read_unlock();

	start_shepherd_timer();
#endif
	migrate_on_reclaim_init();
#ifdef CONFIG_PROC_FS
	proc_create_seq("buddyinfo", 0444, NULL, &fragmentation_op);
	proc_create_seq("pagetypeinfo", 0400, NULL, &pagetypeinfo_op);
	proc_create_seq("vmstat", 0444, NULL, &vmstat_op);
	proc_create_seq("zoneinfo", 0444, NULL, &zoneinfo_op);
#endif
}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_COMPACTION)

/*
 * Return an index indicating how much of the available free memory is
 * unusable for an allocation of the requested size.
 */
static int unusable_free_index(unsigned int order,
				struct contig_page_info *info)
{
	/* No free memory is interpreted as all free memory is unusable */
	if (info->free_pages == 0)
		return 1000;

	/*
	 * Index should be a value between 0 and 1. Return a value to 3
	 * decimal places.
	 *
	 * 0 => no fragmentation
	 * 1 => high fragmentation
	 */
	return div_u64((info->free_pages - (info->free_blocks_suitable << order)) * 1000ULL, info->free_pages);

}

static void unusable_show_print(struct seq_file *m,
					pg_data_t *pgdat, struct zone *zone)
{
	unsigned int order;
	int index;
	struct contig_page_info info;

	seq_printf(m, "Node %d, zone %8s ",
				pgdat->node_id,
				zone->name);
	for (order = 0; order < MAX_ORDER; ++order) {
		fill_contig_page_info(zone, order, &info);
		index = unusable_free_index(order, &info);
		seq_printf(m, "%d.%03d ", index / 1000, index % 1000);
	}

	seq_putc(m, '\n');
}

/*
 * Display unusable free space index
 *
 * The unusable free space index measures how much of the available free
 * memory cannot be used to satisfy an allocation of a given size and is a
 * value between 0 and 1. The higher the value, the more of free memory is
 * unusable and by implication, the worse the external fragmentation is. This
 * can be expressed as a percentage by multiplying by 100.
 */
static int unusable_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	/* check memoryless node */
	if (!node_state(pgdat->node_id, N_MEMORY))
		return 0;

	walk_zones_in_node(m, pgdat, true, false, unusable_show_print);

	return 0;
}

static const struct seq_operations unusable_sops = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= unusable_show,
};

DEFINE_SEQ_ATTRIBUTE(unusable);

static void extfrag_show_print(struct seq_file *m,
					pg_data_t *pgdat, struct zone *zone)
{
	unsigned int order;
	int index;

	/* Alloc on stack as interrupts are disabled for zone walk */
	struct contig_page_info info;

	seq_printf(m, "Node %d, zone %8s ",
				pgdat->node_id,
				zone->name);
	for (order = 0; order < MAX_ORDER; ++order) {
		fill_contig_page_info(zone, order, &info);
		index = __fragmentation_index(order, &info);
		seq_printf(m, "%2d.%03d ", index / 1000, index % 1000);
	}

	seq_putc(m, '\n');
}

/*
 * Display fragmentation index for orders that allocations would fail for
 */
static int extfrag_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	walk_zones_in_node(m, pgdat, true, false, extfrag_show_print);

	return 0;
}

static const struct seq_operations extfrag_sops = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= extfrag_show,
};

DEFINE_SEQ_ATTRIBUTE(extfrag);

static int __init extfrag_debug_init(void)
{
	struct dentry *extfrag_debug_root;

	extfrag_debug_root = debugfs_create_dir("extfrag", NULL);

	debugfs_create_file("unusable_index", 0444, extfrag_debug_root, NULL,
			    &unusable_fops);

	debugfs_create_file("extfrag_index", 0444, extfrag_debug_root, NULL,
			    &extfrag_fops);

	return 0;
}

module_init(extfrag_debug_init);
#endif
