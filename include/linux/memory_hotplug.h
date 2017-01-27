#ifndef __LINUX_MEMORY_HOTPLUG_H
#define __LINUX_MEMORY_HOTPLUG_H

#include <linux/mmzone.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/bug.h>

struct page;
struct zone;
struct pglist_data;
struct mem_section;
struct memory_block;
struct resource;

#ifdef CONFIG_MEMORY_HOTPLUG

/*
 * Types for free bootmem stored in page->lru.next. These have to be in
 * some random range in unsigned long space for debugging purposes.
 */
enum {
	MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE = 12,
	SECTION_INFO = MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE,
	MIX_SECTION_INFO,
	NODE_INFO,
	MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE = NODE_INFO,
};

/* Types for control the zone type of onlined and offlined memory */
enum {
	MMOP_OFFLINE = -1,
	MMOP_ONLINE_KEEP,
	MMOP_ONLINE_KERNEL,
	MMOP_ONLINE_MOVABLE,
};

/*
 * pgdat resizing functions
 */
static inline
void pgdat_resize_lock(struct pglist_data *pgdat, unsigned long *flags)
{
	spin_lock_irqsave(&pgdat->node_size_lock, *flags);
}
static inline
void pgdat_resize_unlock(struct pglist_data *pgdat, unsigned long *flags)
{
	spin_unlock_irqrestore(&pgdat->node_size_lock, *flags);
}
static inline
void pgdat_resize_init(struct pglist_data *pgdat)
{
	spin_lock_init(&pgdat->node_size_lock);
}
/*
 * Zone resizing functions
 *
 * Note: any attempt to resize a zone should has pgdat_resize_lock()
 * zone_span_writelock() both held. This ensure the size of a zone
 * can't be changed while pgdat_resize_lock() held.
 */
static inline unsigned zone_span_seqbegin(struct zone *zone)
{
	return read_seqbegin(&zone->span_seqlock);
}
static inline int zone_span_seqretry(struct zone *zone, unsigned iv)
{
	return read_seqretry(&zone->span_seqlock, iv);
}
static inline void zone_span_writelock(struct zone *zone)
{
	write_seqlock(&zone->span_seqlock);
}
static inline void zone_span_writeunlock(struct zone *zone)
{
	write_sequnlock(&zone->span_seqlock);
}
static inline void zone_seqlock_init(struct zone *zone)
{
	seqlock_init(&zone->span_seqlock);
}
extern int zone_grow_free_lists(struct zone *zone, unsigned long new_nr_pages);
extern int zone_grow_waitqueues(struct zone *zone, unsigned long nr_pages);
extern int add_one_highpage(struct page *page, int pfn, int bad_ppro);
/* VM interface that may be used by firmware interface */
extern int online_pages(unsigned long, unsigned long, int);
extern int test_pages_in_a_zone(unsigned long, unsigned long);
extern void __offline_isolated_pages(unsigned long, unsigned long);

typedef void (*online_page_callback_t)(struct page *page);

extern int set_online_page_callback(online_page_callback_t callback);
extern int restore_online_page_callback(online_page_callback_t callback);

extern void __online_page_set_limits(struct page *page);
extern void __online_page_increment_counters(struct page *page);
extern void __online_page_free(struct page *page);

extern int try_online_node(int nid);

extern bool memhp_auto_online;

#ifdef CONFIG_MEMORY_HOTREMOVE
extern bool is_pageblock_removable_nolock(struct page *page);
extern int arch_remove_memory(u64 start, u64 size);
extern int __remove_pages(struct zone *zone, unsigned long start_pfn,
	unsigned long nr_pages);
#endif /* CONFIG_MEMORY_HOTREMOVE */

/* reasonably generic interface to expand the physical pages in a zone  */
extern int __add_pages(int nid, struct zone *zone, unsigned long start_pfn,
	unsigned long nr_pages);

#ifdef CONFIG_NUMA
extern int memory_add_physaddr_to_nid(u64 start);
#else
static inline int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
#endif

#ifdef CONFIG_HAVE_ARCH_NODEDATA_EXTENSION
/*
 * For supporting node-hotadd, we have to allocate a new pgdat.
 *
 * If an arch has generic style NODE_DATA(),
 * node_data[nid] = kzalloc() works well. But it depends on the architecture.
 *
 * In general, generic_alloc_nodedata() is used.
 * Now, arch_free_nodedata() is just defined for error path of node_hot_add.
 *
 */
extern pg_data_t *arch_alloc_nodedata(int nid);
extern void arch_free_nodedata(pg_data_t *pgdat);
extern void arch_refresh_nodedata(int nid, pg_data_t *pgdat);

#else /* CONFIG_HAVE_ARCH_NODEDATA_EXTENSION */

#define arch_alloc_nodedata(nid)	generic_alloc_nodedata(nid)
#define arch_free_nodedata(pgdat)	generic_free_nodedata(pgdat)

#ifdef CONFIG_NUMA
/*
 * If ARCH_HAS_NODEDATA_EXTENSION=n, this func is used to allocate pgdat.
 * XXX: kmalloc_node() can't work well to get new node's memory at this time.
 *	Because, pgdat for the new node is not allocated/initialized yet itself.
 *	To use new node's memory, more consideration will be necessary.
 */
#define generic_alloc_nodedata(nid)				\
({								\
	kzalloc(sizeof(pg_data_t), GFP_KERNEL);			\
})
/*
 * This definition is just for error path in node hotadd.
 * For node hotremove, we have to replace this.
 */
#define generic_free_nodedata(pgdat)	kfree(pgdat)

extern pg_data_t *node_data[];
static inline void arch_refresh_nodedata(int nid, pg_data_t *pgdat)
{
	node_data[nid] = pgdat;
}

#else /* !CONFIG_NUMA */

/* never called */
static inline pg_data_t *generic_alloc_nodedata(int nid)
{
	BUG();
	return NULL;
}
static inline void generic_free_nodedata(pg_data_t *pgdat)
{
}
static inline void arch_refresh_nodedata(int nid, pg_data_t *pgdat)
{
}
#endif /* CONFIG_NUMA */
#endif /* CONFIG_HAVE_ARCH_NODEDATA_EXTENSION */

#ifdef CONFIG_HAVE_BOOTMEM_INFO_NODE
extern void __init register_page_bootmem_info_node(struct pglist_data *pgdat);
#else
static inline void register_page_bootmem_info_node(struct pglist_data *pgdat)
{
}
#endif
extern void put_page_bootmem(struct page *page);
extern void get_page_bootmem(unsigned long ingo, struct page *page,
			     unsigned long type);

void get_online_mems(void);
void put_online_mems(void);

void mem_hotplug_begin(void);
void mem_hotplug_done(void);

extern void set_zone_contiguous(struct zone *zone);
extern void clear_zone_contiguous(struct zone *zone);

#else /* ! CONFIG_MEMORY_HOTPLUG */
/*
 * Stub functions for when hotplug is off
 */
static inline void pgdat_resize_lock(struct pglist_data *p, unsigned long *f) {}
static inline void pgdat_resize_unlock(struct pglist_data *p, unsigned long *f) {}
static inline void pgdat_resize_init(struct pglist_data *pgdat) {}

static inline unsigned zone_span_seqbegin(struct zone *zone)
{
	return 0;
}
static inline int zone_span_seqretry(struct zone *zone, unsigned iv)
{
	return 0;
}
static inline void zone_span_writelock(struct zone *zone) {}
static inline void zone_span_writeunlock(struct zone *zone) {}
static inline void zone_seqlock_init(struct zone *zone) {}

static inline int mhp_notimplemented(const char *func)
{
	printk(KERN_WARNING "%s() called, with CONFIG_MEMORY_HOTPLUG disabled\n", func);
	dump_stack();
	return -ENOSYS;
}

static inline void register_page_bootmem_info_node(struct pglist_data *pgdat)
{
}

static inline int try_online_node(int nid)
{
	return 0;
}

static inline void get_online_mems(void) {}
static inline void put_online_mems(void) {}

static inline void mem_hotplug_begin(void) {}
static inline void mem_hotplug_done(void) {}

#endif /* ! CONFIG_MEMORY_HOTPLUG */

#ifdef CONFIG_MEMORY_HOTREMOVE

extern bool is_mem_section_removable(unsigned long pfn, unsigned long nr_pages);
extern void try_offline_node(int nid);
extern int offline_pages(unsigned long start_pfn, unsigned long nr_pages);
extern void remove_memory(int nid, u64 start, u64 size);

#else
static inline bool is_mem_section_removable(unsigned long pfn,
					unsigned long nr_pages)
{
	return false;
}

static inline void try_offline_node(int nid) {}

static inline int offline_pages(unsigned long start_pfn, unsigned long nr_pages)
{
	return -EINVAL;
}

static inline void remove_memory(int nid, u64 start, u64 size) {}
#endif /* CONFIG_MEMORY_HOTREMOVE */

extern int walk_memory_range(unsigned long start_pfn, unsigned long end_pfn,
		void *arg, int (*func)(struct memory_block *, void *));
extern int add_memory(int nid, u64 start, u64 size);
extern int add_memory_resource(int nid, struct resource *resource, bool online);
extern int zone_for_memory(int nid, u64 start, u64 size, int zone_default,
		bool for_device);
extern int arch_add_memory(int nid, u64 start, u64 size, bool for_device);
extern int offline_pages(unsigned long start_pfn, unsigned long nr_pages);
extern bool is_memblock_offlined(struct memory_block *mem);
extern void remove_memory(int nid, u64 start, u64 size);
extern int sparse_add_one_section(struct zone *zone, unsigned long start_pfn);
extern void sparse_remove_one_section(struct zone *zone, struct mem_section *ms,
		unsigned long map_offset);
extern struct page *sparse_decode_mem_map(unsigned long coded_mem_map,
					  unsigned long pnum);
extern bool zone_can_shift(unsigned long pfn, unsigned long nr_pages,
			  enum zone_type target, int *zone_shift);

#endif /* __LINUX_MEMORY_HOTPLUG_H */
