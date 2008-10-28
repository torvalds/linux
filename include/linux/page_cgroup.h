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

void __init pgdat_page_cgroup_init(struct pglist_data *pgdat);
void __init page_cgroup_init(void);
struct page_cgroup *lookup_page_cgroup(struct page *page);

enum {
	/* flags for mem_cgroup */
	PCG_LOCK,  /* page cgroup is locked */
	PCG_CACHE, /* charged as cache */
	PCG_USED, /* this object is in use. */
	/* flags for LRU placement */
	PCG_ACTIVE, /* page is active in this cgroup */
	PCG_FILE, /* page is file system backed */
	PCG_UNEVICTABLE, /* page is unevictableable */
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

/* LRU management flags (from global-lru definition) */
TESTPCGFLAG(File, FILE)
SETPCGFLAG(File, FILE)
CLEARPCGFLAG(File, FILE)

TESTPCGFLAG(Active, ACTIVE)
SETPCGFLAG(Active, ACTIVE)
CLEARPCGFLAG(Active, ACTIVE)

TESTPCGFLAG(Unevictable, UNEVICTABLE)
SETPCGFLAG(Unevictable, UNEVICTABLE)
CLEARPCGFLAG(Unevictable, UNEVICTABLE)

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

#endif
#endif
