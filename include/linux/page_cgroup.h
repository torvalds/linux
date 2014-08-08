#ifndef __LINUX_PAGE_CGROUP_H
#define __LINUX_PAGE_CGROUP_H

enum {
	/* flags for mem_cgroup */
	PCG_USED = 0x01,	/* This page is charged to a memcg */
	PCG_MEM = 0x02,		/* This page holds a memory charge */
	PCG_MEMSW = 0x04,	/* This page holds a memory+swap charge */
	__NR_PCG_FLAGS,
};

#ifndef __GENERATING_BOUNDS_H
#include <generated/bounds.h>

struct pglist_data;

#ifdef CONFIG_MEMCG
struct mem_cgroup;

/*
 * Page Cgroup can be considered as an extended mem_map.
 * A page_cgroup page is associated with every page descriptor. The
 * page_cgroup helps us identify information about the cgroup
 * All page cgroups are allocated at boot or memory hotplug event,
 * then the page cgroup for pfn always exists.
 */
struct page_cgroup {
	unsigned long flags;
	struct mem_cgroup *mem_cgroup;
};

extern void pgdat_page_cgroup_init(struct pglist_data *pgdat);

#ifdef CONFIG_SPARSEMEM
static inline void page_cgroup_init_flatmem(void)
{
}
extern void page_cgroup_init(void);
#else
extern void page_cgroup_init_flatmem(void);
static inline void page_cgroup_init(void)
{
}
#endif

struct page_cgroup *lookup_page_cgroup(struct page *page);
struct page *lookup_cgroup_page(struct page_cgroup *pc);

static inline int PageCgroupUsed(struct page_cgroup *pc)
{
	return !!(pc->flags & PCG_USED);
}
#else /* !CONFIG_MEMCG */
struct page_cgroup;

static inline void pgdat_page_cgroup_init(struct pglist_data *pgdat)
{
}

static inline struct page_cgroup *lookup_page_cgroup(struct page *page)
{
	return NULL;
}

static inline void page_cgroup_init(void)
{
}

static inline void page_cgroup_init_flatmem(void)
{
}
#endif /* CONFIG_MEMCG */

#include <linux/swap.h>

#ifdef CONFIG_MEMCG_SWAP
extern unsigned short swap_cgroup_cmpxchg(swp_entry_t ent,
					unsigned short old, unsigned short new);
extern unsigned short swap_cgroup_record(swp_entry_t ent, unsigned short id);
extern unsigned short lookup_swap_cgroup_id(swp_entry_t ent);
extern int swap_cgroup_swapon(int type, unsigned long max_pages);
extern void swap_cgroup_swapoff(int type);
#else

static inline
unsigned short swap_cgroup_record(swp_entry_t ent, unsigned short id)
{
	return 0;
}

static inline
unsigned short lookup_swap_cgroup_id(swp_entry_t ent)
{
	return 0;
}

static inline int
swap_cgroup_swapon(int type, unsigned long max_pages)
{
	return 0;
}

static inline void swap_cgroup_swapoff(int type)
{
	return;
}

#endif /* CONFIG_MEMCG_SWAP */

#endif /* !__GENERATING_BOUNDS_H */

#endif /* __LINUX_PAGE_CGROUP_H */
