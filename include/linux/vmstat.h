#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/config.h>
#include <linux/mmzone.h>
#include <asm/atomic.h>

/*
 * Global page accounting.  One instance per CPU.  Only unsigned longs are
 * allowed.
 *
 * - Fields can be modified with xxx_page_state and xxx_page_state_zone at
 * any time safely (which protects the instance from modification by
 * interrupt.
 * - The __xxx_page_state variants can be used safely when interrupts are
 * disabled.
 * - The __xxx_page_state variants can be used if the field is only
 * modified from process context and protected from preemption, or only
 * modified from interrupt context.  In this case, the field should be
 * commented here.
 */
struct page_state {
	unsigned long nr_dirty;		/* Dirty writeable pages */
	unsigned long nr_writeback;	/* Pages under writeback */
	unsigned long nr_unstable;	/* NFS unstable pages */
	unsigned long nr_page_table_pages;/* Pages used for pagetables */
#define GET_PAGE_STATE_LAST nr_page_table_pages

	/*
	 * The below are zeroed by get_page_state().  Use get_full_page_state()
	 * to add up all these.
	 */
	unsigned long pgpgin;		/* Disk reads */
	unsigned long pgpgout;		/* Disk writes */
	unsigned long pswpin;		/* swap reads */
	unsigned long pswpout;		/* swap writes */

	unsigned long pgalloc_high;	/* page allocations */
	unsigned long pgalloc_normal;
	unsigned long pgalloc_dma32;
	unsigned long pgalloc_dma;

	unsigned long pgfree;		/* page freeings */
	unsigned long pgactivate;	/* pages moved inactive->active */
	unsigned long pgdeactivate;	/* pages moved active->inactive */

	unsigned long pgfault;		/* faults (major+minor) */
	unsigned long pgmajfault;	/* faults (major only) */

	unsigned long pgrefill_high;	/* inspected in refill_inactive_zone */
	unsigned long pgrefill_normal;
	unsigned long pgrefill_dma32;
	unsigned long pgrefill_dma;

	unsigned long pgsteal_high;	/* total highmem pages reclaimed */
	unsigned long pgsteal_normal;
	unsigned long pgsteal_dma32;
	unsigned long pgsteal_dma;

	unsigned long pgscan_kswapd_high;/* total highmem pages scanned */
	unsigned long pgscan_kswapd_normal;
	unsigned long pgscan_kswapd_dma32;
	unsigned long pgscan_kswapd_dma;

	unsigned long pgscan_direct_high;/* total highmem pages scanned */
	unsigned long pgscan_direct_normal;
	unsigned long pgscan_direct_dma32;
	unsigned long pgscan_direct_dma;

	unsigned long pginodesteal;	/* pages reclaimed via inode freeing */
	unsigned long slabs_scanned;	/* slab objects scanned */
	unsigned long kswapd_steal;	/* pages reclaimed by kswapd */
	unsigned long kswapd_inodesteal;/* reclaimed via kswapd inode freeing */
	unsigned long pageoutrun;	/* kswapd's calls to page reclaim */
	unsigned long allocstall;	/* direct reclaim calls */

	unsigned long pgrotated;	/* pages rotated to tail of the LRU */
	unsigned long nr_bounce;	/* pages for bounce buffers */
};

extern void get_page_state(struct page_state *ret);
extern void get_page_state_node(struct page_state *ret, int node);
extern void get_full_page_state(struct page_state *ret);
extern unsigned long read_page_state_offset(unsigned long offset);
extern void mod_page_state_offset(unsigned long offset, unsigned long delta);
extern void __mod_page_state_offset(unsigned long offset, unsigned long delta);

#define read_page_state(member) \
	read_page_state_offset(offsetof(struct page_state, member))

#define mod_page_state(member, delta)	\
	mod_page_state_offset(offsetof(struct page_state, member), (delta))

#define __mod_page_state(member, delta)	\
	__mod_page_state_offset(offsetof(struct page_state, member), (delta))

#define inc_page_state(member)		mod_page_state(member, 1UL)
#define dec_page_state(member)		mod_page_state(member, 0UL - 1)
#define add_page_state(member,delta)	mod_page_state(member, (delta))
#define sub_page_state(member,delta)	mod_page_state(member, 0UL - (delta))

#define __inc_page_state(member)	__mod_page_state(member, 1UL)
#define __dec_page_state(member)	__mod_page_state(member, 0UL - 1)
#define __add_page_state(member,delta)	__mod_page_state(member, (delta))
#define __sub_page_state(member,delta)	__mod_page_state(member, 0UL - (delta))

#define page_state(member) (*__page_state(offsetof(struct page_state, member)))

#define state_zone_offset(zone, member)					\
({									\
	unsigned offset;						\
	if (is_highmem(zone))						\
		offset = offsetof(struct page_state, member##_high);	\
	else if (is_normal(zone))					\
		offset = offsetof(struct page_state, member##_normal);	\
	else if (is_dma32(zone))					\
		offset = offsetof(struct page_state, member##_dma32);	\
	else								\
		offset = offsetof(struct page_state, member##_dma);	\
	offset;								\
})

#define __mod_page_state_zone(zone, member, delta)			\
 do {									\
	__mod_page_state_offset(state_zone_offset(zone, member), (delta)); \
 } while (0)

#define mod_page_state_zone(zone, member, delta)			\
 do {									\
	mod_page_state_offset(state_zone_offset(zone, member), (delta)); \
 } while (0)

DECLARE_PER_CPU(struct page_state, page_states);

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
#else
#define node_page_state(node, item) global_page_state(item)
#endif

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

static inline void __inc_zone_page_state(struct page *page,
			enum zone_stat_item item)
{
	atomic_long_inc(&page_zone(page)->vm_stat[item]);
	atomic_long_inc(&vm_stat[item]);
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
