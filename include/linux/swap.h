/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <linux/linkage.h>
#include <linux/mmzone.h>
#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/node.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/page-flags.h>
#include <uapi/linux/mempolicy.h>
#include <asm/page.h>

struct notifier_block;

struct bio;

struct pagevec;

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0
#define SWAP_FLAG_DISCARD	0x10000 /* enable discard for swap */
#define SWAP_FLAG_DISCARD_ONCE	0x20000 /* discard swap area at swapon-time */
#define SWAP_FLAG_DISCARD_PAGES 0x40000 /* discard page-clusters after use */

#define SWAP_FLAGS_VALID	(SWAP_FLAG_PRIO_MASK | SWAP_FLAG_PREFER | \
				 SWAP_FLAG_DISCARD | SWAP_FLAG_DISCARD_ONCE | \
				 SWAP_FLAG_DISCARD_PAGES)
#define SWAP_BATCH 64

int kswapd (void *p);

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

#define SWP_SWAPIN_ERROR_NUM 1
#define SWP_SWAPIN_ERROR     (MAX_SWAPFILES + SWP_HWPOISON_NUM + \
			     SWP_MIGRATION_NUM + SWP_DEVICE_NUM + \
			     SWP_PTE_MARKER_NUM)
/*
 * PTE markers are used to persist information onto PTEs that are mapped with
 * file-backed memories.  As its name "PTE" hints, it should only be applied to
 * the leaves of pgtables.
 */
#ifdef CONFIG_PTE_MARKER
#define SWP_PTE_MARKER_NUM 1
#define SWP_PTE_MARKER     (MAX_SWAPFILES + SWP_HWPOISON_NUM + \
			    SWP_MIGRATION_NUM + SWP_DEVICE_NUM)
#else
#define SWP_PTE_MARKER_NUM 0
#endif

/*
 * Unaddressable device memory support. See include/linux/hmm.h and
 * Documentation/mm/hmm.rst. Short description is we need struct pages for
 * device memory that is unaddressable (inaccessible) by CPU, so that we can
 * migrate part of a process memory to device memory.
 *
 * When a page is migrated from CPU to device, we set the CPU page table entry
 * to a special SWP_DEVICE_{READ|WRITE} entry.
 *
 * When a page is mapped by the device for exclusive access we set the CPU page
 * table entries to special SWP_DEVICE_EXCLUSIVE_* entries.
 */
#ifdef CONFIG_DEVICE_PRIVATE
#define SWP_DEVICE_NUM 4
#define SWP_DEVICE_WRITE (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM)
#define SWP_DEVICE_READ (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM+1)
#define SWP_DEVICE_EXCLUSIVE_WRITE (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM+2)
#define SWP_DEVICE_EXCLUSIVE_READ (MAX_SWAPFILES+SWP_HWPOISON_NUM+SWP_MIGRATION_NUM+3)
#else
#define SWP_DEVICE_NUM 0
#endif

/*
 * Page migration support.
 *
 * SWP_MIGRATION_READ_EXCLUSIVE is only applicable to anonymous pages and
 * indicates that the referenced (part of) an anonymous page is exclusive to
 * a single process. For SWP_MIGRATION_WRITE, that information is implicit:
 * (part of) an anonymous page that are mapped writable are exclusive to a
 * single process.
 */
#ifdef CONFIG_MIGRATION
#define SWP_MIGRATION_NUM 3
#define SWP_MIGRATION_READ (MAX_SWAPFILES + SWP_HWPOISON_NUM)
#define SWP_MIGRATION_READ_EXCLUSIVE (MAX_SWAPFILES + SWP_HWPOISON_NUM + 1)
#define SWP_MIGRATION_WRITE (MAX_SWAPFILES + SWP_HWPOISON_NUM + 2)
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
	((1 << MAX_SWAPFILES_SHIFT) - SWP_DEVICE_NUM - \
	SWP_MIGRATION_NUM - SWP_HWPOISON_NUM - \
	SWP_PTE_MARKER_NUM - SWP_SWAPIN_ERROR_NUM)

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

/*
 * current->reclaim_state points to one of these when a task is running
 * memory reclaim
 */
struct reclaim_state {
	unsigned long reclaimed_slab;
#ifdef CONFIG_LRU_GEN
	/* per-thread mm walk data */
	struct lru_gen_mm_walk *mm_walk;
#endif
};

#ifdef __KERNEL__

struct address_space;
struct sysinfo;
struct writeback_control;
struct zone;

/*
 * A swap extent maps a range of a swapfile's PAGE_SIZE pages onto a range of
 * disk blocks.  A rbtree of swap extents maps the entire swapfile (Where the
 * term `swapfile' refers to either a blockdevice or an IS_REG file). Apart
 * from setup, they're handled identically.
 *
 * We always assume that blocks are of size PAGE_SIZE.
 */
struct swap_extent {
	struct rb_node rb_node;
	pgoff_t start_page;
	pgoff_t nr_pages;
	sector_t start_block;
};

/*
 * Max bad pages in the new format..
 */
#define MAX_SWAP_BADPAGES \
	((offsetof(union swap_header, magic.magic) - \
	  offsetof(union swap_header, info.badpages)) / sizeof(int))

enum {
	SWP_USED	= (1 << 0),	/* is slot in swap_info[] used? */
	SWP_WRITEOK	= (1 << 1),	/* ok to write to this swap?	*/
	SWP_DISCARDABLE = (1 << 2),	/* blkdev support discard */
	SWP_DISCARDING	= (1 << 3),	/* now discarding a free cluster */
	SWP_SOLIDSTATE	= (1 << 4),	/* blkdev seeks are cheap */
	SWP_CONTINUED	= (1 << 5),	/* swap_map has count continuation */
	SWP_BLKDEV	= (1 << 6),	/* its a block device */
	SWP_ACTIVATED	= (1 << 7),	/* set after swap_activate success */
	SWP_FS_OPS	= (1 << 8),	/* swapfile operations go through fs */
	SWP_AREA_DISCARD = (1 << 9),	/* single-time swap area discards */
	SWP_PAGE_DISCARD = (1 << 10),	/* freed swap page-cluster discards */
	SWP_STABLE_WRITES = (1 << 11),	/* no overwrite PG_writeback pages */
	SWP_SYNCHRONOUS_IO = (1 << 12),	/* synchronous IO is efficient */
					/* add others here before... */
	SWP_SCANNING	= (1 << 14),	/* refcount in scan_swap_map */
};

#define SWAP_CLUSTER_MAX 32UL
#define COMPACT_CLUSTER_MAX SWAP_CLUSTER_MAX

/* Bit flag in swap_map */
#define SWAP_HAS_CACHE	0x40	/* Flag page is cached, in first swap_map */
#define COUNT_CONTINUED	0x80	/* Flag swap_map continuation for full count */

/* Special value in first swap_map */
#define SWAP_MAP_MAX	0x3e	/* Max count */
#define SWAP_MAP_BAD	0x3f	/* Note page is bad */
#define SWAP_MAP_SHMEM	0xbf	/* Owned by shmem/tmpfs */

/* Special value in each swap_map continuation */
#define SWAP_CONT_MAX	0x7f	/* Max count */

/*
 * We use this to track usage of a cluster. A cluster is a block of swap disk
 * space with SWAPFILE_CLUSTER pages long and naturally aligns in disk. All
 * free clusters are organized into a list. We fetch an entry from the list to
 * get a free cluster.
 *
 * The data field stores next cluster if the cluster is free or cluster usage
 * counter otherwise. The flags field determines if a cluster is free. This is
 * protected by swap_info_struct.lock.
 */
struct swap_cluster_info {
	spinlock_t lock;	/*
				 * Protect swap_cluster_info fields
				 * and swap_info_struct->swap_map
				 * elements correspond to the swap
				 * cluster
				 */
	unsigned int data:24;
	unsigned int flags:8;
};
#define CLUSTER_FLAG_FREE 1 /* This cluster is free */
#define CLUSTER_FLAG_NEXT_NULL 2 /* This cluster has no next cluster */
#define CLUSTER_FLAG_HUGE 4 /* This cluster is backing a transparent huge page */

/*
 * We assign a cluster to each CPU, so each CPU can allocate swap entry from
 * its own cluster and swapout sequentially. The purpose is to optimize swapout
 * throughput.
 */
struct percpu_cluster {
	struct swap_cluster_info index; /* Current cluster index */
	unsigned int next; /* Likely next allocation offset */
};

struct swap_cluster_list {
	struct swap_cluster_info head;
	struct swap_cluster_info tail;
};

/*
 * The in-memory structure used to track swap areas.
 */
struct swap_info_struct {
	struct percpu_ref users;	/* indicate and keep swap device valid. */
	unsigned long	flags;		/* SWP_USED etc: see above */
	signed short	prio;		/* swap priority of this type */
	struct plist_node list;		/* entry in swap_active_head */
	signed char	type;		/* strange name for an index */
	unsigned int	max;		/* extent of the swap_map */
	unsigned char *swap_map;	/* vmalloc'ed array of usage counts */
	struct swap_cluster_info *cluster_info; /* cluster info. Only for SSD */
	struct swap_cluster_list free_clusters; /* free clusters list */
	unsigned int lowest_bit;	/* index of first free in swap_map */
	unsigned int highest_bit;	/* index of last free in swap_map */
	unsigned int pages;		/* total of usable pages of swap */
	unsigned int inuse_pages;	/* number of those currently in use */
	unsigned int cluster_next;	/* likely index for next allocation */
	unsigned int cluster_nr;	/* countdown to next cluster search */
	unsigned int __percpu *cluster_next_cpu; /*percpu index for next allocation */
	struct percpu_cluster __percpu *percpu_cluster; /* per cpu's swap location */
	struct rb_root swap_extent_root;/* root of the swap extent rbtree */
	struct block_device *bdev;	/* swap device or bdev of swap file */
	struct file *swap_file;		/* seldom referenced */
	unsigned int old_block_size;	/* seldom referenced */
	struct completion comp;		/* seldom referenced */
#ifdef CONFIG_FRONTSWAP
	unsigned long *frontswap_map;	/* frontswap in-use, one bit per page */
	atomic_t frontswap_pages;	/* frontswap pages in-use counter */
#endif
	spinlock_t lock;		/*
					 * protect map scan related fields like
					 * swap_map, lowest_bit, highest_bit,
					 * inuse_pages, cluster_next,
					 * cluster_nr, lowest_alloc,
					 * highest_alloc, free/discard cluster
					 * list. other fields are only changed
					 * at swapon/swapoff, so are protected
					 * by swap_lock. changing flags need
					 * hold this lock and swap_lock. If
					 * both locks need hold, hold swap_lock
					 * first.
					 */
	spinlock_t cont_lock;		/*
					 * protect swap count continuation page
					 * list.
					 */
	struct work_struct discard_work; /* discard worker */
	struct swap_cluster_list discard_clusters; /* discard clusters list */
	struct plist_node avail_lists[]; /*
					   * entries in swap_avail_heads, one
					   * entry per node.
					   * Must be last as the number of the
					   * array is nr_node_ids, which is not
					   * a fixed value so have to allocate
					   * dynamically.
					   * And it has to be an array so that
					   * plist_for_each_* can work.
					   */
};

#ifdef CONFIG_64BIT
#define SWAP_RA_ORDER_CEILING	5
#else
/* Avoid stack overflow, because we need to save part of page table */
#define SWAP_RA_ORDER_CEILING	3
#define SWAP_RA_PTE_CACHE_SIZE	(1 << SWAP_RA_ORDER_CEILING)
#endif

struct vma_swap_readahead {
	unsigned short win;
	unsigned short offset;
	unsigned short nr_pte;
#ifdef CONFIG_64BIT
	pte_t *ptes;
#else
	pte_t ptes[SWAP_RA_PTE_CACHE_SIZE];
#endif
};

static inline swp_entry_t folio_swap_entry(struct folio *folio)
{
	swp_entry_t entry = { .val = page_private(&folio->page) };
	return entry;
}

static inline void folio_set_swap_entry(struct folio *folio, swp_entry_t entry)
{
	folio->private = (void *)entry.val;
}

/* linux/mm/workingset.c */
void workingset_age_nonresident(struct lruvec *lruvec, unsigned long nr_pages);
void *workingset_eviction(struct folio *folio, struct mem_cgroup *target_memcg);
void workingset_refault(struct folio *folio, void *shadow);
void workingset_activation(struct folio *folio);

/* Only track the nodes of mappings with shadow entries */
void workingset_update_node(struct xa_node *node);
extern struct list_lru shadow_nodes;
#define mapping_set_update(xas, mapping) do {				\
	if (!dax_mapping(mapping) && !shmem_mapping(mapping)) {		\
		xas_set_update(xas, workingset_update_node);		\
		xas_set_lru(xas, &shadow_nodes);			\
	}								\
} while (0)

/* linux/mm/page_alloc.c */
extern unsigned long totalreserve_pages;

/* Definition of global_zone_page_state not available yet */
#define nr_free_pages() global_zone_page_state(NR_FREE_PAGES)


/* linux/mm/swap.c */
void lru_note_cost(struct lruvec *lruvec, bool file, unsigned int nr_pages);
void lru_note_cost_folio(struct folio *);
void folio_add_lru(struct folio *);
void folio_add_lru_vma(struct folio *, struct vm_area_struct *);
void lru_cache_add(struct page *);
void mark_page_accessed(struct page *);
void folio_mark_accessed(struct folio *);

extern atomic_t lru_disable_count;

static inline bool lru_cache_disabled(void)
{
	return atomic_read(&lru_disable_count);
}

static inline void lru_cache_enable(void)
{
	atomic_dec(&lru_disable_count);
}

extern void lru_cache_disable(void);
extern void lru_add_drain(void);
extern void lru_add_drain_cpu(int cpu);
extern void lru_add_drain_cpu_zone(struct zone *zone);
extern void lru_add_drain_all(void);
extern void deactivate_page(struct page *page);
extern void mark_page_lazyfree(struct page *page);
extern void swap_setup(void);

extern void lru_cache_add_inactive_or_unevictable(struct page *page,
						struct vm_area_struct *vma);

/* linux/mm/vmscan.c */
extern unsigned long zone_reclaimable_pages(struct zone *zone);
extern unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
					gfp_t gfp_mask, nodemask_t *mask);

#define MEMCG_RECLAIM_MAY_SWAP (1 << 1)
#define MEMCG_RECLAIM_PROACTIVE (1 << 2)
extern unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *memcg,
						  unsigned long nr_pages,
						  gfp_t gfp_mask,
						  unsigned int reclaim_options);
extern unsigned long mem_cgroup_shrink_node(struct mem_cgroup *mem,
						gfp_t gfp_mask, bool noswap,
						pg_data_t *pgdat,
						unsigned long *nr_scanned);
extern unsigned long shrink_all_memory(unsigned long nr_pages);
extern int vm_swappiness;
long remove_mapping(struct address_space *mapping, struct folio *folio);

extern unsigned long reclaim_pages(struct list_head *page_list);
#ifdef CONFIG_NUMA
extern int node_reclaim_mode;
extern int sysctl_min_unmapped_ratio;
extern int sysctl_min_slab_ratio;
#else
#define node_reclaim_mode 0
#endif

static inline bool node_reclaim_enabled(void)
{
	/* Is any node_reclaim_mode bit set? */
	return node_reclaim_mode & (RECLAIM_ZONE|RECLAIM_WRITE|RECLAIM_UNMAP);
}

void check_move_unevictable_folios(struct folio_batch *fbatch);
void check_move_unevictable_pages(struct pagevec *pvec);

extern void kswapd_run(int nid);
extern void kswapd_stop(int nid);

#ifdef CONFIG_SWAP

int add_swap_extent(struct swap_info_struct *sis, unsigned long start_page,
		unsigned long nr_pages, sector_t start_block);
int generic_swapfile_activate(struct swap_info_struct *, struct file *,
		sector_t *);

static inline unsigned long total_swapcache_pages(void)
{
	return global_node_page_state(NR_SWAPCACHE);
}

extern void free_swap_cache(struct page *page);
extern void free_page_and_swap_cache(struct page *);
extern void free_pages_and_swap_cache(struct page **, int);
/* linux/mm/swapfile.c */
extern atomic_long_t nr_swap_pages;
extern long total_swap_pages;
extern atomic_t nr_rotate_swap;
extern bool has_usable_swap(void);

/* Swap 50% full? Release swapcache more aggressively.. */
static inline bool vm_swap_full(void)
{
	return atomic_long_read(&nr_swap_pages) * 2 < total_swap_pages;
}

static inline long get_nr_swap_pages(void)
{
	return atomic_long_read(&nr_swap_pages);
}

extern void si_swapinfo(struct sysinfo *);
swp_entry_t folio_alloc_swap(struct folio *folio);
bool folio_free_swap(struct folio *folio);
void put_swap_folio(struct folio *folio, swp_entry_t entry);
extern swp_entry_t get_swap_page_of_type(int);
extern int get_swap_pages(int n, swp_entry_t swp_entries[], int entry_size);
extern int add_swap_count_continuation(swp_entry_t, gfp_t);
extern void swap_shmem_alloc(swp_entry_t);
extern int swap_duplicate(swp_entry_t);
extern int swapcache_prepare(swp_entry_t);
extern void swap_free(swp_entry_t);
extern void swapcache_free_entries(swp_entry_t *entries, int n);
extern int free_swap_and_cache(swp_entry_t);
int swap_type_of(dev_t device, sector_t offset);
int find_first_swap(dev_t *device);
extern unsigned int count_swap_pages(int, int);
extern sector_t swapdev_block(int, pgoff_t);
extern int __swap_count(swp_entry_t entry);
extern int __swp_swapcount(swp_entry_t entry);
extern int swp_swapcount(swp_entry_t entry);
extern struct swap_info_struct *page_swap_info(struct page *);
extern struct swap_info_struct *swp_swap_info(swp_entry_t entry);
struct backing_dev_info;
extern int init_swap_address_space(unsigned int type, unsigned long nr_pages);
extern void exit_swap_address_space(unsigned int type);
extern struct swap_info_struct *get_swap_device(swp_entry_t entry);
sector_t swap_page_sector(struct page *page);

static inline void put_swap_device(struct swap_info_struct *si)
{
	percpu_ref_put(&si->users);
}

#else /* CONFIG_SWAP */
static inline struct swap_info_struct *swp_swap_info(swp_entry_t entry)
{
	return NULL;
}

static inline struct swap_info_struct *get_swap_device(swp_entry_t entry)
{
	return NULL;
}

static inline void put_swap_device(struct swap_info_struct *si)
{
}

#define get_nr_swap_pages()			0L
#define total_swap_pages			0L
#define total_swapcache_pages()			0UL
#define vm_swap_full()				0

#define si_swapinfo(val) \
	do { (val)->freeswap = (val)->totalswap = 0; } while (0)
/* only sparc can not include linux/pagemap.h in this file
 * so leave put_page and release_pages undeclared... */
#define free_page_and_swap_cache(page) \
	put_page(page)
#define free_pages_and_swap_cache(pages, nr) \
	release_pages((pages), (nr));

/* used to sanity check ptes in zap_pte_range when CONFIG_SWAP=0 */
#define free_swap_and_cache(e) is_pfn_swap_entry(e)

static inline void free_swap_cache(struct page *page)
{
}

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

static inline void put_swap_folio(struct folio *folio, swp_entry_t swp)
{
}

static inline int __swap_count(swp_entry_t entry)
{
	return 0;
}

static inline int __swp_swapcount(swp_entry_t entry)
{
	return 0;
}

static inline int swp_swapcount(swp_entry_t entry)
{
	return 0;
}

static inline swp_entry_t folio_alloc_swap(struct folio *folio)
{
	swp_entry_t entry;
	entry.val = 0;
	return entry;
}

static inline bool folio_free_swap(struct folio *folio)
{
	return false;
}

static inline int add_swap_extent(struct swap_info_struct *sis,
				  unsigned long start_page,
				  unsigned long nr_pages, sector_t start_block)
{
	return -EINVAL;
}
#endif /* CONFIG_SWAP */

#ifdef CONFIG_THP_SWAP
extern int split_swap_cluster(swp_entry_t entry);
#else
static inline int split_swap_cluster(swp_entry_t entry)
{
	return 0;
}
#endif

#ifdef CONFIG_MEMCG
static inline int mem_cgroup_swappiness(struct mem_cgroup *memcg)
{
	/* Cgroup2 doesn't have per-cgroup swappiness */
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return vm_swappiness;

	/* root ? */
	if (mem_cgroup_disabled() || mem_cgroup_is_root(memcg))
		return vm_swappiness;

	return memcg->swappiness;
}
#else
static inline int mem_cgroup_swappiness(struct mem_cgroup *mem)
{
	return vm_swappiness;
}
#endif

#ifdef CONFIG_ZSWAP
extern u64 zswap_pool_total_size;
extern atomic_t zswap_stored_pages;
#endif

#if defined(CONFIG_SWAP) && defined(CONFIG_MEMCG) && defined(CONFIG_BLK_CGROUP)
extern void __cgroup_throttle_swaprate(struct page *page, gfp_t gfp_mask);
static inline  void cgroup_throttle_swaprate(struct page *page, gfp_t gfp_mask)
{
	if (mem_cgroup_disabled())
		return;
	__cgroup_throttle_swaprate(page, gfp_mask);
}
#else
static inline void cgroup_throttle_swaprate(struct page *page, gfp_t gfp_mask)
{
}
#endif
static inline void folio_throttle_swaprate(struct folio *folio, gfp_t gfp)
{
	cgroup_throttle_swaprate(&folio->page, gfp);
}

#if defined(CONFIG_MEMCG) && defined(CONFIG_SWAP)
void mem_cgroup_swapout(struct folio *folio, swp_entry_t entry);
int __mem_cgroup_try_charge_swap(struct folio *folio, swp_entry_t entry);
static inline int mem_cgroup_try_charge_swap(struct folio *folio,
		swp_entry_t entry)
{
	if (mem_cgroup_disabled())
		return 0;
	return __mem_cgroup_try_charge_swap(folio, entry);
}

extern void __mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages);
static inline void mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages)
{
	if (mem_cgroup_disabled())
		return;
	__mem_cgroup_uncharge_swap(entry, nr_pages);
}

extern long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg);
extern bool mem_cgroup_swap_full(struct folio *folio);
#else
static inline void mem_cgroup_swapout(struct folio *folio, swp_entry_t entry)
{
}

static inline int mem_cgroup_try_charge_swap(struct folio *folio,
					     swp_entry_t entry)
{
	return 0;
}

static inline void mem_cgroup_uncharge_swap(swp_entry_t entry,
					    unsigned int nr_pages)
{
}

static inline long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg)
{
	return get_nr_swap_pages();
}

static inline bool mem_cgroup_swap_full(struct folio *folio)
{
	return vm_swap_full();
}
#endif

#endif /* __KERNEL__*/
#endif /* _LINUX_SWAP_H */
