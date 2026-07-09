/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mm_init.h:
 *
 * mm/ internal mm_init and memblock declarations
 */

#ifndef __MM_MM_INIT_H
#define __MM_MM_INIT_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mmzone.h>
#include <linux/jump_label.h>
#include <linux/printk.h>

struct page;
struct vmem_altmap;

/* perform sanity checks on struct pages being allocated or freed */
DECLARE_STATIC_KEY_MAYBE(CONFIG_DEBUG_VM, check_pages_enabled);

void set_zone_contiguous(struct zone *zone);
bool pfn_range_intersects_zones(int nid, unsigned long start_pfn,
			   unsigned long nr_pages);

static inline void clear_zone_contiguous(struct zone *zone)
{
	zone->contiguous = false;
}

void memblock_free_pages(unsigned long pfn, unsigned int order);

void *memmap_alloc(phys_addr_t size, phys_addr_t align, phys_addr_t min_addr,
		int nid, bool exact_nid);

void memmap_init_range(unsigned long size, int nid, unsigned long zone,
		unsigned long start_pfn, unsigned long zone_end_pfn,
		enum meminit_context context,
		struct vmem_altmap *altmap, int migratetype,
		bool isolate_pageblock);

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
/* Free whole pageblock and set its migration type to MIGRATE_CMA. */
void init_cma_reserved_pageblock(struct page *page);
#endif

#ifdef CONFIG_CMA
void init_cma_pageblock(struct page *page);
#else
static inline void init_cma_pageblock(struct page *page)
{
}
#endif

/* Memory initialisation debug and verification */
#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
DECLARE_STATIC_KEY_TRUE(deferred_pages);

static inline bool deferred_pages_enabled(void)
{
	return static_branch_unlikely(&deferred_pages);
}

bool __init deferred_grow_zone(struct zone *zone, unsigned int order);
#else
static inline bool deferred_pages_enabled(void)
{
	return false;
}
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

void init_deferred_page(unsigned long pfn, int nid);

enum mminit_level {
	MMINIT_WARNING,
	MMINIT_VERIFY,
	MMINIT_TRACE
};

#ifdef CONFIG_DEBUG_MEMORY_INIT

extern int mminit_loglevel;

#define mminit_dprintk(level, prefix, fmt, arg...) \
do { \
	if (level < mminit_loglevel) { \
		if (level <= MMINIT_WARNING) \
			pr_warn("mminit::" prefix " " fmt, ##arg);	\
		else \
			printk(KERN_DEBUG "mminit::" prefix " " fmt, ##arg); \
	} \
} while (0)

void mminit_verify_pageflags_layout(void);
void mminit_verify_zonelist(void);
#else

static inline void mminit_dprintk(enum mminit_level level,
				const char *prefix, const char *fmt, ...)
{
}

static inline void mminit_verify_pageflags_layout(void)
{
}

static inline void mminit_verify_zonelist(void)
{
}
#endif /* CONFIG_DEBUG_MEMORY_INIT */

extern bool mirrored_kernelcore;
bool memblock_has_mirror(void);
void memblock_free_all(void);

void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid);

#endif /* __MM_MM_INIT_H */
