#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/config.h>
#include <linux/mmzone.h>
#include <asm/atomic.h>

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

#define FOR_ALL_ZONES(x) x##_DMA, x##_DMA32, x##_NORMAL, x##_HIGH

enum vm_event_item { PGPGIN, PGPGOUT, PSWPIN, PSWPOUT,
		FOR_ALL_ZONES(PGALLOC),
		PGFREE, PGACTIVATE, PGDEACTIVATE,
		PGFAULT, PGMAJFAULT,
		FOR_ALL_ZONES(PGREFILL),
		FOR_ALL_ZONES(PGSTEAL),
		FOR_ALL_ZONES(PGSCAN_KSWAPD),
		FOR_ALL_ZONES(PGSCAN_DIRECT),
		PGINODESTEAL, SLABS_SCANNED, KSWAPD_STEAL, KSWAPD_INODESTEAL,
		PAGEOUTRUN, ALLOCSTALL, PGROTATED,
		NR_VM_EVENT_ITEMS
};

struct vm_event_state {
	unsigned long event[NR_VM_EVENT_ITEMS];
};

DECLARE_PER_CPU(struct vm_event_state, vm_event_states);

static inline void __count_vm_event(enum vm_event_item item)
{
	__get_cpu_var(vm_event_states).event[item]++;
}

static inline void count_vm_event(enum vm_event_item item)
{
	get_cpu_var(vm_event_states).event[item]++;
	put_cpu();
}

static inline void __count_vm_events(enum vm_event_item item, long delta)
{
	__get_cpu_var(vm_event_states).event[item] += delta;
}

static inline void count_vm_events(enum vm_event_item item, long delta)
{
	get_cpu_var(vm_event_states).event[item] += delta;
	put_cpu();
}

extern void all_vm_events(unsigned long *);
extern void vm_events_fold_cpu(int cpu);

#else

/* Disable counters */
#define get_cpu_vm_events(e)	0L
#define count_vm_event(e)	do { } while (0)
#define count_vm_events(e,d)	do { } while (0)
#define __count_vm_event(e)	do { } while (0)
#define __count_vm_events(e,d)	do { } while (0)
#define vm_events_fold_cpu(x)	do { } while (0)

#endif /* CONFIG_VM_EVENT_COUNTERS */

#define __count_zone_vm_events(item, zone, delta) \
			__count_vm_events(item##_DMA + zone_idx(zone), delta)

/*
 * Zone based page accounting with per cpu differentials.
 */
extern atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];

static inline void zone_page_state_add(long x, struct zone *zone,
				 enum zone_stat_item item)
{
	atomic_long_add(x, &zone->vm_stat[item]);
	atomic_long_add(x, &vm_stat[item]);
}

static inline unsigned long global_page_state(enum zone_stat_item item)
{
	long x = atomic_long_read(&vm_stat[item]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
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

#ifdef CONFIG_NUMA
/*
 * Determine the per node value of a stat item. This function
 * is called frequently in a NUMA machine, so try to be as
 * frugal as possible.
 */
static inline unsigned long node_page_state(int node,
				 enum zone_stat_item item)
{
	struct zone *zones = NODE_DATA(node)->node_zones;

	return
#ifndef CONFIG_DMA_IS_NORMAL
#if !defined(CONFIG_DMA_IS_DMA32) && BITS_PER_LONG >= 64
		zone_page_state(&zones[ZONE_DMA32], item) +
#endif
		zone_page_state(&zones[ZONE_NORMAL], item) +
#endif
#ifdef CONFIG_HIGHMEM
		zone_page_state(&zones[ZONE_HIGHMEM], item) +
#endif
		zone_page_state(&zones[ZONE_DMA], item);
}

extern void zone_statistics(struct zonelist *, struct zone *);

#else

#define node_page_state(node, item) global_page_state(item)
#define zone_statistics(_zl,_z) do { } while (0)

#endif /* CONFIG_NUMA */

#define __add_zone_page_state(__z, __i, __d)	\
		__mod_zone_page_state(__z, __i, __d)
#define __sub_zone_page_state(__z, __i, __d)	\
		__mod_zone_page_state(__z, __i,-(__d))

#define add_zone_page_state(__z, __i, __d) mod_zone_page_state(__z, __i, __d)
#define sub_zone_page_state(__z, __i, __d) mod_zone_page_state(__z, __i, -(__d))

static inline void zap_zone_vm_stats(struct zone *zone)
{
	memset(zone->vm_stat, 0, sizeof(zone->vm_stat));
}

extern void inc_zone_state(struct zone *, enum zone_stat_item);

#ifdef CONFIG_SMP
void __mod_zone_page_state(struct zone *, enum zone_stat_item item, int);
void __inc_zone_page_state(struct page *, enum zone_stat_item);
void __dec_zone_page_state(struct page *, enum zone_stat_item);

void mod_zone_page_state(struct zone *, enum zone_stat_item, int);
void inc_zone_page_state(struct page *, enum zone_stat_item);
void dec_zone_page_state(struct page *, enum zone_stat_item);

extern void inc_zone_state(struct zone *, enum zone_stat_item);

void refresh_cpu_vm_stats(int);
void refresh_vm_stats(void);

#else /* CONFIG_SMP */

/*
 * We do not maintain differentials in a single processor configuration.
 * The functions directly modify the zone and global counters.
 */
static inline void __mod_zone_page_state(struct zone *zone,
			enum zone_stat_item item, int delta)
{
	zone_page_state_add(delta, zone, item);
}

static inline void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
{
	atomic_long_inc(&zone->vm_stat[item]);
	atomic_long_inc(&vm_stat[item]);
}

static inline void __inc_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	__inc_zone_state(page_zone(page), item);
}

static inline void __dec_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	atomic_long_dec(&page_zone(page)->vm_stat[item]);
	atomic_long_dec(&vm_stat[item]);
}

/*
 * We only use atomic operations to update counters. So there is no need to
 * disable interrupts.
 */
#define inc_zone_page_state __inc_zone_page_state
#define dec_zone_page_state __dec_zone_page_state
#define mod_zone_page_state __mod_zone_page_state

static inline void refresh_cpu_vm_stats(int cpu) { }
static inline void refresh_vm_stats(void) { }
#endif

#endif /* _LINUX_VMSTAT_H */
