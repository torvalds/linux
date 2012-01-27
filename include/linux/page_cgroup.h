#ifndef __LINUX_PAGE_CGROUP_H
#define __LINUX_PAGE_CGROUP_H

enum {
	/* flags for mem_cgroup */
	PCG_LOCK,  /* Lock for pc->mem_cgroup and following bits. */
	PCG_CACHE, /* charged as cache */
	PCG_USED, /* this object is in use. */
	PCG_MIGRATION, /* under page migration */
	/* flags for mem_cgroup and file and I/O status */
	PCG_MOVE_LOCK, /* For race between move_account v.s. following bits */
	PCG_FILE_MAPPED, /* page is accounted as "mapped" */
	__NR_PCG_FLAGS,
};

#ifndef __GENERATING_BOUNDS_H
#include <generated/bounds.h>

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
};

void __meminit pgdat_page_cgroup_init(struct pglist_data *pgdat);

#ifdef CONFIG_SPARSEMEM
static inline void __init page_cgroup_init_flatmem(void)
{
}
extern void __init page_cgroup_init(void);
#else
void __init page_cgroup_init_flatmem(void);
static inline void __init page_cgroup_init(void)
{
}
#endif

struct page_cgroup *lookup_page_cgroup(struct page *page);
struct page *lookup_cgroup_page(struct page_cgroup *pc);

#define TESTPCGFLAG(uname, lname)			\
static inline int PageCgroup##uname(struct page_cgroup *pc)	\
	{ return test_bit(PCG_##lname, &pc->flags); }

#define SETPCGFLAG(uname, lname)			\
static inline void SetPageCgroup##uname(struct page_cgroup *pc)\
	{ set_bit(PCG_##lname, &pc->flags);  }

#define CLEARPCGFLAG(uname, lname)			\
static inline void ClearPageCgroup##uname(struct page_cgroup *pc)	\
	{ clear_bit(PCG_##lname, &pc->flags);  }

#define TESTCLEARPCGFLAG(uname, lname)			\
static inline int TestClearPageCgroup##uname(struct page_cgroup *pc)	\
	{ return test_and_clear_bit(PCG_##lname, &pc->flags);  }

/* Cache flag is set only once (at allocation) */
TESTPCGFLAG(Cache, CACHE)
CLEARPCGFLAG(Cache, CACHE)
SETPCGFLAG(Cache, CACHE)

TESTPCGFLAG(Used, USED)
CLEARPCGFLAG(Used, USED)
SETPCGFLAG(Used, USED)

SETPCGFLAG(FileMapped, FILE_MAPPED)
CLEARPCGFLAG(FileMapped, FILE_MAPPED)
TESTPCGFLAG(FileMapped, FILE_MAPPED)

SETPCGFLAG(Migration, MIGRATION)
CLEARPCGFLAG(Migration, MIGRATION)
TESTPCGFLAG(Migration, MIGRATION)

static inline void lock_page_cgroup(struct page_cgroup *pc)
{
	/*
	 * Don't take this lock in IRQ context.
	 * This lock is for pc->mem_cgroup, USED, CACHE, MIGRATION
	 */
	bit_spin_lock(PCG_LOCK, &pc->flags);
}

static inline void unlock_page_cgroup(struct page_cgroup *pc)
{
	bit_spin_unlock(PCG_LOCK, &pc->flags);
}

static inline void move_lock_page_cgroup(struct page_cgroup *pc,
	unsigned long *flags)
{
	/*
	 * We know updates to pc->flags of page cache's stats are from both of
	 * usual context or IRQ context. Disable IRQ to avoid deadlock.
	 */
	local_irq_save(*flags);
	bit_spin_lock(PCG_MOVE_LOCK, &pc->flags);
}

static inline void move_unlock_page_cgroup(struct page_cgroup *pc,
	unsigned long *flags)
{
	bit_spin_unlock(PCG_MOVE_LOCK, &pc->flags);
	local_irq_restore(*flags);
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

static inline void __init page_cgroup_init_flatmem(void)
{
}

#endif /* CONFIG_CGROUP_MEM_RES_CTLR */

#include <linux/swap.h>

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
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

#endif /* CONFIG_CGROUP_MEM_RES_CTLR_SWAP */

#endif /* !__GENERATING_BOUNDS_H */

#endif /* __LINUX_PAGE_CGROUP_H */
