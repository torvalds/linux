/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/mmzone.h>
#include <linux/vm_event_item.h>
#include <linux/atomic.h>
#include <linux/static_key.h>
#include <linux/mmdebug.h>

extern int sysctl_stat_interval;

#ifdef CONFIG_NUMA
#define ENABLE_NUMA_STAT   1
#define DISABLE_NUMA_STAT   0
extern int sysctl_vm_numa_stat;
DECLARE_STATIC_KEY_TRUE(vm_numa_stat_key);
int sysctl_vm_numa_stat_handler(const struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos);
#endif

struct reclaim_stat {
	unsigned nr_dirty;
	unsigned nr_unqueued_dirty;
	unsigned nr_congested;
	unsigned nr_writeback;
	unsigned nr_immediate;
	unsigned nr_pageout;
	unsigned nr_activate[ANON_AND_FILE];
	unsigned nr_ref_keep;
	unsigned nr_unmap_fail;
	unsigned nr_lazyfree_fail;
};

/* Stat data for system wide items */
enum vm_stat_item {
	NR_DIRTY_THRESHOLD,
	NR_DIRTY_BG_THRESHOLD,
	NR_MEMMAP_PAGES,	/* page metadata allocated through buddy allocator */
	NR_MEMMAP_BOOT_PAGES,	/* page metadata allocated through boot allocator */
	NR_VM_STAT_ITEMS,
};

#ifdef CONFIG_VM_EVENT_COUNTERS
/*
 * Light weight per cpu counter implementation.
 *
 * Counters should only be incremented and no critical kernel component
 * should rely on the counter values.
 *
 * Counters are handled completely inline. On many platforms the code
 * generated will simply be the increment of a global address.
 */

struct vm_event_state {
	unsigned long event[NR_VM_EVENT_ITEMS];
};

DECLARE_PER_CPU(struct vm_event_state, vm_event_states);

/*
 * vm counters are allowed to be racy. Use raw_cpu_ops to avoid the
 * local_irq_disable overhead.
 */
static inline void __count_vm_event(enum vm_event_item item)
{
	raw_cpu_inc(vm_event_states.event[item]);
}

static inline void count_vm_event(enum vm_event_item item)
{
	this_cpu_inc(vm_event_states.event[item]);
}

static inline void __count_vm_events(enum vm_event_item item, long delta)
{
	raw_cpu_add(vm_event_states.event[item], delta);
}

static inline void count_vm_events(enum vm_event_item item, long delta)
{
	this_cpu_add(vm_event_states.event[item], delta);
}

extern void all_vm_events(unsigned long *);

extern void vm_events_fold_cpu(int cpu);

#else

/* Disable counters */
static inline void count_vm_event(enum vm_event_item item)
{
}
static inline void count_vm_events(enum vm_event_item item, long delta)
{
}
static inline void __count_vm_event(enum vm_event_item item)
{
}
static inline void __count_vm_events(enum vm_event_item item, long delta)
{
}
static inline void all_vm_events(unsigned long *ret)
{
}
static inline void vm_events_fold_cpu(int cpu)
{
}

#endif /* CONFIG_VM_EVENT_COUNTERS */

#ifdef CONFIG_NUMA_BALANCING
#define count_vm_numa_event(x)     count_vm_event(x)
#define count_vm_numa_events(x, y) count_vm_events(x, y)
#else
#define count_vm_numa_event(x) do {} while (0)
#define count_vm_numa_events(x, y) do { (void)(y); } while (0)
#endif /* CONFIG_NUMA_BALANCING */

#ifdef CONFIG_DEBUG_TLBFLUSH
#define count_vm_tlb_event(x)	   count_vm_event(x)
#define count_vm_tlb_events(x, y)  count_vm_events(x, y)
#else
#define count_vm_tlb_event(x)     do {} while (0)
#define count_vm_tlb_events(x, y) do { (void)(y); } while (0)
#endif

#ifdef CONFIG_PER_VMA_LOCK_STATS
#define count_vm_vma_lock_event(x) count_vm_event(x)
#else
#define count_vm_vma_lock_event(x) do {} while (0)
#endif

#define __count_zid_vm_events(item, zid, delta) \
	__count_vm_events(item##_NORMAL - ZONE_NORMAL + zid, delta)

/*
 * Zone and node-based page accounting with per cpu differentials.
 */
extern atomic_long_t vm_zone_stat[NR_VM_ZONE_STAT_ITEMS];
extern atomic_long_t vm_node_stat[NR_VM_NODE_STAT_ITEMS];
extern atomic_long_t vm_numa_event[NR_VM_NUMA_EVENT_ITEMS];

#ifdef CONFIG_NUMA
static inline void zone_numa_event_add(long x, struct zone *zone,
				enum numa_stat_item item)
{
	atomic_long_add(x, &zone->vm_numa_event[item]);
	atomic_long_add(x, &vm_numa_event[item]);
}

static inline unsigned long zone_numa_event_state(struct zone *zone,
					enum numa_stat_item item)
{
	return atomic_long_read(&zone->vm_numa_event[item]);
}

static inline unsigned long
global_numa_event_state(enum numa_stat_item item)
{
	return atomic_long_read(&vm_numa_event[item]);
}
#endif /* CONFIG_NUMA */

static inline void zone_page_state_add(long x, struct zone *zone,
				 enum zone_stat_item item)
{
	atomic_long_add(x, &zone->vm_stat[item]);
	atomic_long_add(x, &vm_zone_stat[item]);
}

static inline void node_page_state_add(long x, struct pglist_data *pgdat,
				 enum node_stat_item item)
{
	atomic_long_add(x, &pgdat->vm_stat[item]);
	atomic_long_add(x, &vm_node_stat[item]);
}

static inline unsigned long global_zone_page_state(enum zone_stat_item item)
{
	long x = atomic_long_read(&vm_zone_stat[item]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

static inline
unsigned long global_node_page_state_pages(enum node_stat_item item)
{
	long x = atomic_long_read(&vm_node_stat[item]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

static inline unsigned long global_node_page_state(enum node_stat_item item)
{
	VM_WARN_ON_ONCE(vmstat_item_in_bytes(item));

	return global_node_page_state_pages(item);
}

static inline unsigned long zone_page_state(struct zone *zone,
					enum zone_stat_item item)
{
	long x = atomic_long_read(&zone->vm_stat[item]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

/*
 * More accurate version that also considers the currently pending
 * deltas. For that we need to loop over all cpus to find the current
 * deltas. There is no synchronization so the result cannot be
 * exactly accurate either.
 */
static inline unsigned long zone_page_state_snapshot(struct zone *zone,
					enum zone_stat_item item)
{
	long x = atomic_long_read(&zone->vm_stat[item]);

#ifdef CONFIG_SMP
	int cpu;
	for_each_online_cpu(cpu)
		x += per_cpu_ptr(zone->per_cpu_zonestats, cpu)->vm_stat_diff[item];

	if (x < 0)
		x = 0;
#endif
	return x;
}

#ifdef CONFIG_NUMA
/* See __count_vm_event comment on why raw_cpu_inc is used. */
static inline void
__count_numa_event(struct zone *zone, enum numa_stat_item item)
{
	struct per_cpu_zonestat __percpu *pzstats = zone->per_cpu_zonestats;

	raw_cpu_inc(pzstats->vm_numa_event[item]);
}

static inline void
__count_numa_events(struct zone *zone, enum numa_stat_item item, long delta)
{
	struct per_cpu_zonestat __percpu *pzstats = zone->per_cpu_zonestats;

	raw_cpu_add(pzstats->vm_numa_event[item], delta);
}

extern unsigned long sum_zone_node_page_state(int node,
					      enum zone_stat_item item);
extern unsigned long sum_zone_numa_event_state(int node, enum numa_stat_item item);
extern unsigned long node_page_state(struct pglist_data *pgdat,
						enum node_stat_item item);
extern unsigned long node_page_state_pages(struct pglist_data *pgdat,
					   enum node_stat_item item);
extern void fold_vm_numa_events(void);
#else
#define sum_zone_node_page_state(node, item) global_zone_page_state(item)
#define node_page_state(node, item) global_node_page_state(item)
#define node_page_state_pages(node, item) global_node_page_state_pages(item)
static inline void fold_vm_numa_events(void)
{
}
#endif /* CONFIG_NUMA */

#ifdef CONFIG_SMP
void __mod_zone_page_state(struct zone *, enum zone_stat_item item, long);
void __inc_zone_page_state(struct page *, enum zone_stat_item);
void __dec_zone_page_state(struct page *, enum zone_stat_item);

void __mod_node_page_state(struct pglist_data *, enum node_stat_item item, long);
void __inc_node_page_state(struct page *, enum node_stat_item);
void __dec_node_page_state(struct page *, enum node_stat_item);

void mod_zone_page_state(struct zone *, enum zone_stat_item, long);
void inc_zone_page_state(struct page *, enum zone_stat_item);
void dec_zone_page_state(struct page *, enum zone_stat_item);

void mod_node_page_state(struct pglist_data *, enum node_stat_item, long);
void inc_node_page_state(struct page *, enum node_stat_item);
void dec_node_page_state(struct page *, enum node_stat_item);

extern void inc_node_state(struct pglist_data *, enum node_stat_item);
extern void __inc_zone_state(struct zone *, enum zone_stat_item);
extern void __inc_node_state(struct pglist_data *, enum node_stat_item);
extern void dec_zone_state(struct zone *, enum zone_stat_item);
extern void __dec_zone_state(struct zone *, enum zone_stat_item);
extern void __dec_node_state(struct pglist_data *, enum node_stat_item);

void quiet_vmstat(void);
void cpu_vm_stats_fold(int cpu);
void refresh_zone_stat_thresholds(void);

struct ctl_table;
int vmstat_refresh(const struct ctl_table *, int write, void *buffer, size_t *lenp,
		loff_t *ppos);

void drain_zonestat(struct zone *zone, struct per_cpu_zonestat *);

int calculate_pressure_threshold(struct zone *zone);
int calculate_normal_threshold(struct zone *zone);
void set_pgdat_percpu_threshold(pg_data_t *pgdat,
				int (*calculate_pressure)(struct zone *));
#else /* CONFIG_SMP */

/*
 * We do not maintain differentials in a single processor configuration.
 * The functions directly modify the zone and global counters.
 */
static inline void __mod_zone_page_state(struct zone *zone,
			enum zone_stat_item item, long delta)
{
	zone_page_state_add(delta, zone, item);
}

static inline void __mod_node_page_state(struct pglist_data *pgdat,
			enum node_stat_item item, int delta)
{
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

	node_page_state_add(delta, pgdat, item);
}

static inline void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
{
	atomic_long_inc(&zone->vm_stat[item]);
	atomic_long_inc(&vm_zone_stat[item]);
}

static inline void __inc_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	atomic_long_inc(&pgdat->vm_stat[item]);
	atomic_long_inc(&vm_node_stat[item]);
}

static inline void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
{
	atomic_long_dec(&zone->vm_stat[item]);
	atomic_long_dec(&vm_zone_stat[item]);
}

static inline void __dec_node_state(struct pglist_data *pgdat, enum node_stat_item item)
{
	atomic_long_dec(&pgdat->vm_stat[item]);
	atomic_long_dec(&vm_node_stat[item]);
}

static inline void __inc_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	__inc_zone_state(page_zone(page), item);
}

static inline void __inc_node_page_state(struct page *page,
			enum node_stat_item item)
{
	__inc_node_state(page_pgdat(page), item);
}


static inline void __dec_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	__dec_zone_state(page_zone(page), item);
}

static inline void __dec_node_page_state(struct page *page,
			enum node_stat_item item)
{
	__dec_node_state(page_pgdat(page), item);
}


/*
 * We only use atomic operations to update counters. So there is no need to
 * disable interrupts.
 */
#define inc_zone_page_state __inc_zone_page_state
#define dec_zone_page_state __dec_zone_page_state
#define mod_zone_page_state __mod_zone_page_state

#define inc_node_page_state __inc_node_page_state
#define dec_node_page_state __dec_node_page_state
#define mod_node_page_state __mod_node_page_state

#define inc_zone_state __inc_zone_state
#define inc_node_state __inc_node_state
#define dec_zone_state __dec_zone_state

#define set_pgdat_percpu_threshold(pgdat, callback) { }

static inline void refresh_zone_stat_thresholds(void) { }
static inline void cpu_vm_stats_fold(int cpu) { }
static inline void quiet_vmstat(void) { }

static inline void drain_zonestat(struct zone *zone,
			struct per_cpu_zonestat *pzstats) { }
#endif		/* CONFIG_SMP */

static inline void __zone_stat_mod_folio(struct folio *folio,
		enum zone_stat_item item, long nr)
{
	__mod_zone_page_state(folio_zone(folio), item, nr);
}

static inline void __zone_stat_add_folio(struct folio *folio,
		enum zone_stat_item item)
{
	__mod_zone_page_state(folio_zone(folio), item, folio_nr_pages(folio));
}

static inline void __zone_stat_sub_folio(struct folio *folio,
		enum zone_stat_item item)
{
	__mod_zone_page_state(folio_zone(folio), item, -folio_nr_pages(folio));
}

static inline void zone_stat_mod_folio(struct folio *folio,
		enum zone_stat_item item, long nr)
{
	mod_zone_page_state(folio_zone(folio), item, nr);
}

static inline void zone_stat_add_folio(struct folio *folio,
		enum zone_stat_item item)
{
	mod_zone_page_state(folio_zone(folio), item, folio_nr_pages(folio));
}

static inline void zone_stat_sub_folio(struct folio *folio,
		enum zone_stat_item item)
{
	mod_zone_page_state(folio_zone(folio), item, -folio_nr_pages(folio));
}

static inline void __node_stat_mod_folio(struct folio *folio,
		enum node_stat_item item, long nr)
{
	__mod_node_page_state(folio_pgdat(folio), item, nr);
}

static inline void __node_stat_add_folio(struct folio *folio,
		enum node_stat_item item)
{
	__mod_node_page_state(folio_pgdat(folio), item, folio_nr_pages(folio));
}

static inline void __node_stat_sub_folio(struct folio *folio,
		enum node_stat_item item)
{
	__mod_node_page_state(folio_pgdat(folio), item, -folio_nr_pages(folio));
}

static inline void node_stat_mod_folio(struct folio *folio,
		enum node_stat_item item, long nr)
{
	mod_node_page_state(folio_pgdat(folio), item, nr);
}

static inline void node_stat_add_folio(struct folio *folio,
		enum node_stat_item item)
{
	mod_node_page_state(folio_pgdat(folio), item, folio_nr_pages(folio));
}

static inline void node_stat_sub_folio(struct folio *folio,
		enum node_stat_item item)
{
	mod_node_page_state(folio_pgdat(folio), item, -folio_nr_pages(folio));
}

extern const char * const vmstat_text[];

static inline const char *zone_stat_name(enum zone_stat_item item)
{
	return vmstat_text[item];
}

#ifdef CONFIG_NUMA
static inline const char *numa_stat_name(enum numa_stat_item item)
{
	return vmstat_text[NR_VM_ZONE_STAT_ITEMS +
			   item];
}
#endif /* CONFIG_NUMA */

static inline const char *node_stat_name(enum node_stat_item item)
{
	return vmstat_text[NR_VM_ZONE_STAT_ITEMS +
			   NR_VM_NUMA_EVENT_ITEMS +
			   item];
}

static inline const char *lru_list_name(enum lru_list lru)
{
	return node_stat_name(NR_LRU_BASE + lru) + 3; // skip "nr_"
}

#if defined(CONFIG_VM_EVENT_COUNTERS) || defined(CONFIG_MEMCG)
static inline const char *vm_event_name(enum vm_event_item item)
{
	return vmstat_text[NR_VM_ZONE_STAT_ITEMS +
			   NR_VM_NUMA_EVENT_ITEMS +
			   NR_VM_NODE_STAT_ITEMS +
			   NR_VM_STAT_ITEMS +
			   item];
}
#endif /* CONFIG_VM_EVENT_COUNTERS || CONFIG_MEMCG */

#ifdef CONFIG_MEMCG

void __mod_lruvec_state(struct lruvec *lruvec, enum node_stat_item idx,
			int val);

static inline void mod_lruvec_state(struct lruvec *lruvec,
				    enum node_stat_item idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_lruvec_state(lruvec, idx, val);
	local_irq_restore(flags);
}

void __lruvec_stat_mod_folio(struct folio *folio,
			     enum node_stat_item idx, int val);

static inline void lruvec_stat_mod_folio(struct folio *folio,
					 enum node_stat_item idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__lruvec_stat_mod_folio(folio, idx, val);
	local_irq_restore(flags);
}

static inline void mod_lruvec_page_state(struct page *page,
					 enum node_stat_item idx, int val)
{
	lruvec_stat_mod_folio(page_folio(page), idx, val);
}

#else

static inline void __mod_lruvec_state(struct lruvec *lruvec,
				      enum node_stat_item idx, int val)
{
	__mod_node_page_state(lruvec_pgdat(lruvec), idx, val);
}

static inline void mod_lruvec_state(struct lruvec *lruvec,
				    enum node_stat_item idx, int val)
{
	mod_node_page_state(lruvec_pgdat(lruvec), idx, val);
}

static inline void __lruvec_stat_mod_folio(struct folio *folio,
					 enum node_stat_item idx, int val)
{
	__mod_node_page_state(folio_pgdat(folio), idx, val);
}

static inline void lruvec_stat_mod_folio(struct folio *folio,
					 enum node_stat_item idx, int val)
{
	mod_node_page_state(folio_pgdat(folio), idx, val);
}

static inline void mod_lruvec_page_state(struct page *page,
					 enum node_stat_item idx, int val)
{
	mod_node_page_state(page_pgdat(page), idx, val);
}

#endif /* CONFIG_MEMCG */

static inline void __lruvec_stat_add_folio(struct folio *folio,
					   enum node_stat_item idx)
{
	__lruvec_stat_mod_folio(folio, idx, folio_nr_pages(folio));
}

static inline void __lruvec_stat_sub_folio(struct folio *folio,
					   enum node_stat_item idx)
{
	__lruvec_stat_mod_folio(folio, idx, -folio_nr_pages(folio));
}

static inline void lruvec_stat_add_folio(struct folio *folio,
					 enum node_stat_item idx)
{
	lruvec_stat_mod_folio(folio, idx, folio_nr_pages(folio));
}

static inline void lruvec_stat_sub_folio(struct folio *folio,
					 enum node_stat_item idx)
{
	lruvec_stat_mod_folio(folio, idx, -folio_nr_pages(folio));
}

void memmap_boot_pages_add(long delta);
void memmap_pages_add(long delta);
#endif /* _LINUX_VMSTAT_H */
