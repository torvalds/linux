#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/sched.h>

#include <asm/atomic.h>
#include <asm/page.h>

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

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
#ifndef CONFIG_MIGRATION
#define MAX_SWAPFILES		(1 << MAX_SWAPFILES_SHIFT)
#else
/* Use last two entries for page migration swap entries */
#define MAX_SWAPFILES		((1 << MAX_SWAPFILES_SHIFT)-2)
#define SWP_MIGRATION_READ	MAX_SWAPFILES
#define SWP_MIGRATION_WRITE	(MAX_SWAPFILES + 1)
#endif

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
	SWP_ACTIVE	= (SWP_USED | SWP_WRITEOK),
					/* add others here before... */
	SWP_SCANNING	= (1 << 8),	/* refcount in scan_swap_map */
};

#define SWAP_CLUSTER_MAX 32

#define SWAP_MAP_MAX	0x7fff
#define SWAP_MAP_BAD	0x8000

/*
 * The in-memory structure used to track swap areas.
 */
struct swap_info_struct {
	unsigned int flags;
	int prio;			/* swap priority */
	struct file *swap_file;
	struct block_device *bdev;
	struct list_head extent_list;
	struct swap_extent *curr_swap_extent;
	unsigned old_block_size;
	unsigned short * swap_map;
	unsigned int lowest_bit;
	unsigned int highest_bit;
	unsigned int cluster_next;
	unsigned int cluster_nr;
	unsigned int pages;
	unsigned int max;
	unsigned int inuse_pages;
	int next;			/* next entry on swap list */
};

struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};

/* Swap 50% full? Release swapcache more aggressively.. */
#define vm_swap_full() (nr_swap_pages*2 < total_swap_pages)

/* linux/mm/oom_kill.c */
extern void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask, int order);

/* linux/mm/memory.c */
extern void swapin_readahead(swp_entry_t, unsigned long, struct vm_area_struct *);

/* linux/mm/page_alloc.c */
extern unsigned long totalram_pages;
extern unsigned long totalhigh_pages;
extern unsigned long totalreserve_pages;
extern long nr_swap_pages;
extern unsigned int nr_free_pages(void);
extern unsigned int nr_free_pages_pgdat(pg_data_t *pgdat);
extern unsigned int nr_free_buffer_pages(void);
extern unsigned int nr_free_pagecache_pages(void);

/* linux/mm/swap.c */
extern void FASTCALL(lru_cache_add(struct page *));
extern void FASTCALL(lru_cache_add_active(struct page *));
extern void FASTCALL(activate_page(struct page *));
extern void FASTCALL(mark_page_accessed(struct page *));
extern void lru_add_drain(void);
extern int lru_add_drain_all(void);
extern int rotate_reclaimable_page(struct page *page);
extern void swap_setup(void);

/* linux/mm/vmscan.c */
extern unsigned long try_to_free_pages(struct zone **, gfp_t);
extern unsigned long shrink_all_memory(unsigned long nr_pages);
extern int vm_swappiness;
extern int remove_mapping(struct address_space *mapping, struct page *page);
extern long vm_total_pages;

#ifdef CONFIG_NUMA
extern int zone_reclaim_mode;
extern int sysctl_min_unmapped_ratio;
extern int zone_reclaim(struct zone *, gfp_t, unsigned int);
#else
#define zone_reclaim_mode 0
static inline int zone_reclaim(struct zone *z, gfp_t mask, unsigned int order)
{
	return 0;
}
#endif

extern int kswapd_run(int nid);

#ifdef CONFIG_MMU
/* linux/mm/shmem.c */
extern int shmem_unuse(swp_entry_t entry, struct page *page);
#endif /* CONFIG_MMU */

extern void swap_unplug_io_fn(struct backing_dev_info *, struct page *);

#ifdef CONFIG_SWAP
/* linux/mm/page_io.c */
extern int swap_readpage(struct file *, struct page *);
extern int swap_writepage(struct page *page, struct writeback_control *wbc);
extern int rw_swap_page_sync(int, swp_entry_t, struct page *);

/* linux/mm/swap_state.c */
extern struct address_space swapper_space;
#define total_swapcache_pages  swapper_space.nrpages
extern void show_swap_cache_info(void);
extern int add_to_swap(struct page *, gfp_t);
extern void __delete_from_swap_cache(struct page *);
extern void delete_from_swap_cache(struct page *);
extern int move_to_swap_cache(struct page *, swp_entry_t);
extern int move_from_swap_cache(struct page *, unsigned long,
		struct address_space *);
extern void free_page_and_swap_cache(struct page *);
extern void free_pages_and_swap_cache(struct page **, int);
extern struct page * lookup_swap_cache(swp_entry_t);
extern struct page * read_swap_cache_async(swp_entry_t, struct vm_area_struct *vma,
					   unsigned long addr);
/* linux/mm/swapfile.c */
extern long total_swap_pages;
extern unsigned int nr_swapfiles;
extern void si_swapinfo(struct sysinfo *);
extern swp_entry_t get_swap_page(void);
extern swp_entry_t get_swap_page_of_type(int);
extern int swap_duplicate(swp_entry_t);
extern int valid_swaphandles(swp_entry_t, unsigned long *);
extern void swap_free(swp_entry_t);
extern void free_swap_and_cache(swp_entry_t);
extern int swap_type_of(dev_t);
extern unsigned int count_swap_pages(int, int);
extern sector_t map_swap_page(struct swap_info_struct *, pgoff_t);
extern struct swap_info_struct *get_swap_info_struct(unsigned);
extern int can_share_swap_page(struct page *);
extern int remove_exclusive_swap_page(struct page *);
struct backing_dev_info;

extern spinlock_t swap_lock;

/* linux/mm/thrash.c */
extern struct mm_struct * swap_token_mm;
extern unsigned long swap_token_default_timeout;
extern void grab_swap_token(void);
extern void __put_swap_token(struct mm_struct *);

static inline int has_swap_token(struct mm_struct *mm)
{
	return (mm == swap_token_mm);
}

static inline void put_swap_token(struct mm_struct *mm)
{
	if (has_swap_token(mm))
		__put_swap_token(mm);
}

static inline void disable_swap_token(void)
{
	put_swap_token(swap_token_mm);
}

#else /* CONFIG_SWAP */

#define total_swap_pages			0
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

static inline void free_swap_and_cache(swp_entry_t swp)
{
}

static inline int swap_duplicate(swp_entry_t swp)
{
	return 0;
}

static inline void swap_free(swp_entry_t swp)
{
}

static inline struct page *read_swap_cache_async(swp_entry_t swp,
			struct vm_area_struct *vma, unsigned long addr)
{
	return NULL;
}

static inline struct page *lookup_swap_cache(swp_entry_t swp)
{
	return NULL;
}

static inline int valid_swaphandles(swp_entry_t entry, unsigned long *offset)
{
	return 0;
}

#define can_share_swap_page(p)			(page_mapcount(p) == 1)

static inline int move_to_swap_cache(struct page *page, swp_entry_t entry)
{
	return 1;
}

static inline int move_from_swap_cache(struct page *page, unsigned long index,
					struct address_space *mapping)
{
	return 1;
}

static inline void __delete_from_swap_cache(struct page *page)
{
}

static inline void delete_from_swap_cache(struct page *page)
{
}

#define swap_token_default_timeout		0

static inline int remove_exclusive_swap_page(struct page *p)
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
#define put_swap_token(x) do { } while(0)
#define grab_swap_token()  do { } while(0)
#define has_swap_token(x) 0
#define disable_swap_token() do { } while(0)

#endif /* CONFIG_SWAP */
#endif /* __KERNEL__*/
#endif /* _LINUX_SWAP_H */
