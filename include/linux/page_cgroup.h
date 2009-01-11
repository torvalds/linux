#ifndef __LINUX_PAGE_CGROUP_H
#define __LINUX_PAGE_CGROUP_H

#ifdef CONFIG_CGROUP_MEM_RES_CTLR
#include <linux/bit_spinlock.h>
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
	struct page *page;
	struct list_head lru;		/* per cgroup LRU list */
};

void __meminit pgdat_page_cgroup_init(struct pglist_data *pgdat);
void __init page_cgroup_init(void);
struct page_cgroup *lookup_page_cgroup(struct page *page);

enum {
	/* flags for mem_cgroup */
	PCG_LOCK,  /* page cgroup is locked */
	PCG_CACHE, /* charged as cache */
	PCG_USED, /* this object is in use. */
};

#define TESTPCGFLAG(uname, lname)			\
static inline int PageCgroup##uname(struct page_cgroup *pc)	\
	{ return test_bit(PCG_##lname, &pc->flags); }

#define SETPCGFLAG(uname, lname)			\
static inline void SetPageCgroup##uname(struct page_cgroup *pc)\
	{ set_bit(PCG_##lname, &pc->flags);  }

#define CLEARPCGFLAG(uname, lname)			\
static inline void ClearPageCgroup##uname(struct page_cgroup *pc)	\
	{ clear_bit(PCG_##lname, &pc->flags);  }

/* Cache flag is set only once (at allocation) */
TESTPCGFLAG(Cache, CACHE)

TESTPCGFLAG(Used, USED)
CLEARPCGFLAG(Used, USED)

static inline int page_cgroup_nid(struct page_cgroup *pc)
{
	return page_to_nid(pc->page);
}

static inline enum zone_type page_cgroup_zid(struct page_cgroup *pc)
{
	return page_zonenum(pc->page);
}

static inline void lock_page_cgroup(struct page_cgroup *pc)
{
	bit_spin_lock(PCG_LOCK, &pc->flags);
}

static inline int trylock_page_cgroup(struct page_cgroup *pc)
{
	return bit_spin_trylock(PCG_LOCK, &pc->flags);
}

static inline void unlock_page_cgroup(struct page_cgroup *pc)
{
	bit_spin_unlock(PCG_LOCK, &pc->flags);
}

#else /* CONFIG_CGROUP_MEM_RES_CTLR */
struct page_cgroup;

static inline void __meminit pgdat_page_cgroup_init(struct pglist_data *pgdat)
{
}

static inline struct page_cgroup *lookup_page_cgroup(struct page *page)
{
	return NULL;
}

static inline void page_cgroup_init(void)
{
}

#endif

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
#include <linux/swap.h>
extern struct mem_cgroup *
swap_cgroup_record(swp_entry_t ent, struct mem_cgroup *mem);
extern struct mem_cgroup *lookup_swap_cgroup(swp_entry_t ent);
extern int swap_cgroup_swapon(int type, unsigned long max_pages);
extern void swap_cgroup_swapoff(int type);
#else
#include <linux/swap.h>

static inline
struct mem_cgroup *swap_cgroup_record(swp_entry_t ent, struct mem_cgroup *mem)
{
	return NULL;
}

static inline
struct mem_cgroup *lookup_swap_cgroup(swp_entry_t ent)
{
	return NULL;
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

#endif
#endif
