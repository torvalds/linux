#ifndef __LINUX_MEMORY_HOTPLUG_H
#define __LINUX_MEMORY_HOTPLUG_H

#include <linux/mmzone.h>
#include <linux/spinlock.h>
#include <linux/mmzone.h>
#include <linux/notifier.h>

struct page;
struct zone;
struct pglist_data;

#ifdef CONFIG_MEMORY_HOTPLUG
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
/* need some defines for these for archs that don't support it */
extern void online_page(struct page *page);
/* VM interface that may be used by firmware interface */
extern int online_pages(unsigned long, unsigned long);

/* reasonably generic interface to expand the physical pages in a zone  */
extern int __add_pages(struct zone *zone, unsigned long start_pfn,
	unsigned long nr_pages);
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

#endif /* ! CONFIG_MEMORY_HOTPLUG */
static inline int __remove_pages(struct zone *zone, unsigned long start_pfn,
	unsigned long nr_pages)
{
	printk(KERN_WARNING "%s() called, not yet supported\n", __FUNCTION__);
	dump_stack();
	return -ENOSYS;
}

extern int add_memory(u64 start, u64 size);
extern int remove_memory(u64 start, u64 size);

#endif /* __LINUX_MEMORY_HOTPLUG_H */
