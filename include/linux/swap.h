#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/node.h>

#include <linux/atomic.h>
#include <asm/page.h>

struct notifier_block;

struct bio;

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0
#define SWAP_FLAG_DISCARD	0x10000 /* discard swap cluster after use */

static inline int current_is_kswapd(void)
{
	return current->flags & PF_KSWAPD;
}

/*
 * MAX_SWAPFILES defines the maximum number of swaptypes: things which can
 * be swapped to.  The swap type and the offset into that swap type are
 * encoded into pte's and into pgoff_t's in the swapcache.  Using five bits
 * for the type means that the maximum number of swapcache pages is 27 bits
 * on 32-bit-pgoff_t architectures.  And that assumes that the architecture packs
 * the type/offset into the pte as 5/27 as well.
 */
#define MAX_SWAPFILES_SHIFT	5

/*
 * Use some of the swap files numbers for other purposes. This
 * is a convenient way to hook into the VM to trigger special
 * actions on faults.
 */

/*
 * NUMA node memory migration support
 */
#ifdef CONFIG_MIGRATION
#define SWP_MIGRATION_NUM 2
#define SWP_MIGRATION_READ	(MAX_SWAPFILES + SWP_HWPOISON_NUM)
#define SWP_MIGRATION_WRITE	(MAX_SWAPFILES + SWP_HWPOISON_NUM + 1)
#else
#define SWP_MIGRATION_NUM 0
#endif

/*
 * Handling of hardware poisoned pages with memory corruption.
 */
#ifdef CONFIG_MEMORY_FAILURE
#define SWP_HWPOISON_NUM 1
#define SWP_HWPOISON		MAX_SWAPFILES
#else
#define SWP_HWPOISON_NUM 0
#endif

#define MAX_SWAPFILES \
	((1 << MAX_SWAPFILES_SHIFT) - SWP_MIGRATION_NUM - SWP_HWPOISON_NUM)

/*
 * Magic header for a swap area. The first part of the union is
 * what the swap magic looks like for the old (limited to 128MB)
 * swap area format, the second part of the union adds - in the
 * old reserved area - some extra information. Note that the first
 * kilobyte is reserved for boot loader or disk label stuff...
 *
 * Having the magic at the end of the PAGE_SIZE makes detecting swap
 * areas somewhat tricky on machines that support multiple page sizes.
 * For 2.5 we'll probably want to move the magic to just beyond the
 * bootbits...
 */
union swap_header {
	struct {
		char reserved[PAGE_SIZE - 10];
		char magic[10];			/* SWAP-SPACE or SWAPSPACE2 */
	} magic;
	struct {
		char		bootbits[1024];	/* Space for disklabel etc. */
		__u32		version;
		__u32		last_page;
		__u32		nr_badpages;
		unsigned char	sws_uuid[16];
		unsigned char	sws_volume[16];
		__u32		padding[117];
		__u32		badpages[1];
	} info;
};

 /* A swap entry has to fit into a "unsigned long", as
  * the entry is hidden in the "index" field of the
  * swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

/*
 * current->reclaim_state points to one of these when a task is running
 * memory reclaim
 */
struct reclaim_state {
	unsigned long reclaimed_slab;
};

#ifdef __KERNEL__

struct address_space;
struct sysinfo;
struct writeback_control;
struct zone;

/*
 * A swap extent maps a range of a swapfile's PAGE_SIZE pages onto a range of
 * disk blocks.  A list of swap extents maps the entire swapfile.  (Where the
 * term `swapfile' refers to either a blockdevice or an IS_REG file.  Apart
 * from setup, they're handled identically.
 *
 * We always assume that blocks are of size PAGE_SIZE.
 */
struct swap_extent {
	struct list_head list;
	pgoff_t start_page;
	pgoff_t nr_pages;
	sector_t start_block;
};

/*
 * Max bad pages in the new format..
 */
#define __swapoffset(x) ((unsigned long)&((union swap_header *)0)->x)
#define MAX_SWAP_BADPAGES \
	((__swapoffset(magic.magic) - __swapoffset(info.badpages)) / sizeof(int))

enum {
	SWP_USED	= (1 << 0),	/* is slot in swap_info[] used? */
	SWP_WRITEOK	= (1 << 1),	/* ok to write to this swap?	*/
	SWP_DISCARDABLE = (1 << 2),	/* swapon+blkdev support discard */
	SWP_DISCARDING	= (1 << 3),	/* now discarding a free cluster */
	SWP_SOLIDSTATE	= (1 << 4),	/* blkdev seeks are cheap */
	SWP_CONTINUED	= (1 << 5),	/* swap_map has count continuation */
	SWP_BLKDEV	= (1 << 6),	/* its a block device */
					/* add others here before... */
	SWP_SCANNING	= (1 << 8),	/* refcount in scan_swap_map */
};

#define SWAP_CLUSTER_MAX 32
#define COMPACT_CLUSTER_MAX SWAP_CLUSTER_MAX

/*
 * Ratio between the present memory in the zone and the "gap" that
 * we're allowing kswapd to shrink in addition to the per-zone high
 * wmark, even for zones that already have the high wmark satisfied,
 * in order to provide better per-zone lru behavior. We are ok to
 * spend not more than 1% of the memory for this zone balancing "gap".
 */
#define KSWAPD_ZONE_BALANCE_GAP_RATIO 100

#define SWAP_MAP_MAX	0x3e	/* Max duplication count, in first swap_map */
#define SWAP_MAP_BAD	0x3f	/* Note pageblock is bad, in first swap_map */
#define SWAP_HAS_CACHE	0x40	/* Flag page is cached, in first swap_map */
#define SWAP_CONT_MAX	0x7f	/* Max count, in each swap_map continuation */
#define COUNT_CONTINUED	0x80	/* See swap_map continuation for full count */
#define SWAP_MAP_SHMEM	0xbf	/* Owned by shmem/tmpfs, in first swap_map */

/*
 * The in-memory structure used to track swap areas.
 */
struct swap_info_struct {
	unsigned long	flags;		/* SWP_USED etc: see above */
	signed short	prio;		/* swap priority of this type */
	signed char	type;		/* strange name for an index */
	signed char	next;		/* next type on the swap list */
	unsigned int	max;		/* extent of the swap_map */
	unsigned char *swap_map;	/* vmalloc'ed array of usage counts */
	unsigned int lowest_bit;	/* index of first free in swap_map */
	unsigned int highest_bit;	/* index of last free in swap_map */
	unsigned int pages;		/* total of usable pages of swap */
	unsigned int inuse_pages;	/* number of those currently in use */
	unsigned int cluster_next;	/* likely index for next allocation */
	unsigned int cluster_nr;	/* countdown to next cluster search */
	unsigned int lowest_alloc;	/* while preparing discard cluster */
	unsigned int highest_alloc;	/* while preparing discard cluster */
	struct swap_extent *curr_swap_extent;
	struct swap_extent first_swap_extent;
	struct block_device *bdev;	/* swap device or bdev of swap file */
	struct file *swap_file;		/* seldom referenced */
	unsigned int old_block_size;	/* seldom referenced */
};

struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};

/* Swap 50% full? Release swapcache more aggressively.. */
#define vm_swap_full() (nr_swap_pages*2 < total_swap_pages)

/* linux/mm/page_alloc.c */
extern unsigned long totalram_pages;
extern unsigned long totalreserve_pages;
extern unsigned int nr_free_buffer_pages(void);
extern unsigned int nr_free_pagecache_pages(void);

/* Definition of global_page_state not available yet */
#define nr_free_pages() global_page_state(NR_FREE_PAGES)


/* linux/mm/swap.c */
extern void __lru_cache_add(struct page *, enum lru_list lru);
extern void lru_cache_add_lru(struct page *, enum lru_list lru);
extern void lru_add_page_tail(struct zone* zone,
			      struct page *page, struct page *page_tail);
extern void activate_page(struct page *);
extern void mark_page_accessed(struct page *);
extern void lru_add_drain(void);
extern int lru_add_drain_all(void);
extern void rotate_reclaimable_page(struct page *page);
extern void deactivate_page(struct page *page);
extern void swap_setup(void);

extern void add_page_to_unevictable_list(struct page *page);

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
static inline void lru_cache_add_anon(struct page *page)
{
	__lru_cache_add(page, LRU_INACTIVE_ANON);
}

static inline void lru_cache_add_file(struct page *page)
{
	__lru_cache_add(page, LRU_INACTIVE_FILE);
}

/* LRU Isolation modes. */
#define ISOLATE_INACTIVE 0	/* Isolate inactive pages. */
#define ISOLATE_ACTIVE 1	/* Isolate active pages. */
#define ISOLATE_BOTH 2		/* Isolate both active and inactive pages. */

/* linux/mm/vmscan.c */
extern unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
					gfp_t gfp_mask, nodemask_t *mask);
extern int __isolate_lru_page(struct page *page, int mode, int file);
extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *mem,
						  gfp_t gfp_mask, bool noswap);
extern unsigned long mem_cgroup_shrink_node_zone(struct mem_cgroup *mem,
						gfp_t gfp_mask, bool noswap,
						struct zone *zone,
						unsigned long *nr_scanned);
extern unsigned long shrink_all_memory(unsigned long nr_pages);
extern int vm_swappiness;
extern int remove_mapping(struct address_space *mapping, struct page *page);
extern long vm_total_pages;

#ifdef CONFIG_NUMA
extern int zone_reclaim_mode;
extern int sysctl_min_unmapped_ratio;
extern int sysctl_min_slab_ratio;
extern int zone_reclaim(struct zone *, gfp_t, unsigned int);
#else
#define zone_reclaim_mode 0
static inline int zone_reclaim(struct zone *z, gfp_t mask, unsigned int order)
{
	return 0;
}
#endif

extern int page_evictable(struct page *page, struct vm_area_struct *vma);
extern void scan_mapping_unevictable_pages(struct address_space *);

extern unsigned long scan_unevictable_pages;
extern int scan_unevictable_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
#ifdef CONFIG_NUMA
extern int scan_unevictable_register_node(struct node *node);
extern void scan_unevictable_unregister_node(struct node *node);
#else
static inline int scan_unevictable_register_node(struct node *node)
{
	return 0;
}
static inline void scan_unevictable_unregister_node(struct node *node)
{
}
#endif

extern int kswapd_run(int nid);
extern void kswapd_stop(int nid);
#ifdef CONFIG_CGROUP_MEM_RES_CTLR
extern int mem_cgroup_swappiness(struct mem_cgroup *mem);
#else
static inline int mem_cgroup_swappiness(struct mem_cgroup *mem)
{
	return vm_swappiness;
}
#endif
#ifdef CONFIG_SWAP
/* linux/mm/page_io.c */
extern int swap_readpage(struct page *);
extern int swap_writepage(struct page *page, struct writeback_control *wbc);
extern void end_swap_bio_read(struct bio *bio, int err);

/* linux/mm/swap_state.c */
extern struct address_space swapper_space;
#define total_swapcache_pages  swapper_space.nrpages
extern void show_swap_cache_info(void);
extern int add_to_swap(struct page *);
extern int add_to_swap_cache(struct page *, swp_entry_t, gfp_t);
extern void __delete_from_swap_cache(struct page *);
extern void delete_from_swap_cache(struct page *);
extern void free_page_and_swap_cache(struct page *);
extern void free_pages_and_swap_cache(struct page **, int);
extern struct page *lookup_swap_cache(swp_entry_t);
extern struct page *read_swap_cache_async(swp_entry_t, gfp_t,
			struct vm_area_struct *vma, unsigned long addr);
extern struct page *swapin_readahead(swp_entry_t, gfp_t,
			struct vm_area_struct *vma, unsigned long addr);

/* linux/mm/swapfile.c */
extern long nr_swap_pages;
extern long total_swap_pages;
extern void si_swapinfo(struct sysinfo *);
extern swp_entry_t get_swap_page(void);
extern swp_entry_t get_swap_page_of_type(int);
extern int valid_swaphandles(swp_entry_t, unsigned long *);
extern int add_swap_count_continuation(swp_entry_t, gfp_t);
extern void swap_shmem_alloc(swp_entry_t);
extern int swap_duplicate(swp_entry_t);
extern int swapcache_prepare(swp_entry_t);
extern void swap_free(swp_entry_t);
extern void swapcache_free(swp_entry_t, struct page *page);
extern int free_swap_and_cache(swp_entry_t);
extern int swap_type_of(dev_t, sector_t, struct block_device **);
extern unsigned int count_swap_pages(int, int);
extern sector_t map_swap_page(struct page *, struct block_device **);
extern sector_t swapdev_block(int, pgoff_t);
extern int reuse_swap_page(struct page *);
extern int try_to_free_swap(struct page *);
struct backing_dev_info;

/* linux/mm/thrash.c */
extern struct mm_struct *swap_token_mm;
extern void grab_swap_token(struct mm_struct *);
extern void __put_swap_token(struct mm_struct *);
extern void disable_swap_token(struct mem_cgroup *memcg);

static inline int has_swap_token(struct mm_struct *mm)
{
	return (mm == swap_token_mm);
}

static inline void put_swap_token(struct mm_struct *mm)
{
	if (has_swap_token(mm))
		__put_swap_token(mm);
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR
extern void
mem_cgroup_uncharge_swapcache(struct page *page, swp_entry_t ent, bool swapout);
extern int mem_cgroup_count_swap_user(swp_entry_t ent, struct page **pagep);
#else
static inline void
mem_cgroup_uncharge_swapcache(struct page *page, swp_entry_t ent, bool swapout)
{
}
#endif
#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
extern void mem_cgroup_uncharge_swap(swp_entry_t ent);
#else
static inline void mem_cgroup_uncharge_swap(swp_entry_t ent)
{
}
#endif

#else /* CONFIG_SWAP */

#define nr_swap_pages				0L
#define total_swap_pages			0L
#define total_swapcache_pages			0UL

#define si_swapinfo(val) \
	do { (val)->freeswap = (val)->totalswap = 0; } while (0)
/* only sparc can not include linux/pagemap.h in this file
 * so leave page_cache_release and release_pages undeclared... */
#define free_page_and_swap_cache(page) \
	page_cache_release(page)
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr), 0);

static inline void show_swap_cache_info(void)
{
}

#define free_swap_and_cache(swp)	is_migration_entry(swp)
#define swapcache_prepare(swp)		is_migration_entry(swp)

static inline int add_swap_count_continuation(swp_entry_t swp, gfp_t gfp_mask)
{
	return 0;
}

static inline void swap_shmem_alloc(swp_entry_t swp)
{
}

static inline int swap_duplicate(swp_entry_t swp)
{
	return 0;
}

static inline void swap_free(swp_entry_t swp)
{
}

static inline void swapcache_free(swp_entry_t swp, struct page *page)
{
}

static inline struct page *swapin_readahead(swp_entry_t swp, gfp_t gfp_mask,
			struct vm_area_struct *vma, unsigned long addr)
{
	return NULL;
}

static inline int swap_writepage(struct page *p, struct writeback_control *wbc)
{
	return 0;
}

static inline struct page *lookup_swap_cache(swp_entry_t swp)
{
	return NULL;
}

static inline int add_to_swap(struct page *page)
{
	return 0;
}

static inline int add_to_swap_cache(struct page *page, swp_entry_t entry,
							gfp_t gfp_mask)
{
	return -1;
}

static inline void __delete_from_swap_cache(struct page *page)
{
}

static inline void delete_from_swap_cache(struct page *page)
{
}

#define reuse_swap_page(page)	(page_mapcount(page) == 1)

static inline int try_to_free_swap(struct page *page)
{
	return 0;
}

static inline swp_entry_t get_swap_page(void)
{
	swp_entry_t entry;
	entry.val = 0;
	return entry;
}

/* linux/mm/thrash.c */
static inline void put_swap_token(struct mm_struct *mm)
{
}

static inline void grab_swap_token(struct mm_struct *mm)
{
}

static inline int has_swap_token(struct mm_struct *mm)
{
	return 0;
}

static inline void disable_swap_token(struct mem_cgroup *memcg)
{
}

static inline void
mem_cgroup_uncharge_swapcache(struct page *page, swp_entry_t ent)
{
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR
static inline int
mem_cgroup_count_swap_user(swp_entry_t ent, struct page **pagep)
{
	return 0;
}
#endif

#endif /* CONFIG_SWAP */
#endif /* __KERNEL__*/
#endif /* _LINUX_SWAP_H */
