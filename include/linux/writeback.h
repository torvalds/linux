/*
 * include/linux/writeback.h
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/fs.h>

/*
 * The 1/4 region under the global dirty thresh is for smooth dirty throttling:
 *
 *	(thresh - thresh/DIRTY_FULL_SCOPE, thresh)
 *
 * The 1/16 region above the global dirty limit will be put to maximum pauses:
 *
 *	(limit, limit + limit/DIRTY_MAXPAUSE_AREA)
 *
 * The 1/16 region above the max-pause region, dirty exceeded bdi's will be put
 * to loops:
 *
 *	(limit + limit/DIRTY_MAXPAUSE_AREA, limit + limit/DIRTY_PASSGOOD_AREA)
 *
 * Further beyond, all dirtier tasks will enter a loop waiting (possibly long
 * time) for the dirty pages to drop, unless written enough pages.
 *
 * The global dirty threshold is normally equal to the global dirty limit,
 * except when the system suddenly allocates a lot of anonymous memory and
 * knocks down the global dirty threshold quickly, in which case the global
 * dirty limit will follow down slowly to prevent livelocking all dirtier tasks.
 */
#define DIRTY_SCOPE		8
#define DIRTY_FULL_SCOPE	(DIRTY_SCOPE / 2)
#define DIRTY_MAXPAUSE_AREA		16
#define DIRTY_PASSGOOD_AREA		8

/*
 * 4MB minimal write chunk size
 */
#define MIN_WRITEBACK_PAGES	(4096UL >> (PAGE_CACHE_SHIFT - 10))

struct backing_dev_info;

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	WB_SYNC_NONE,	/* Don't wait on anything */
	WB_SYNC_ALL,	/* Wait on every mapping */
};

/*
 * A control structure which tells the writeback code what to do.  These are
 * always on the stack, and hence need no locking.  They are always initialised
 * in a manner such that unspecified fields are set to zero.
 */
struct writeback_control {
	enum writeback_sync_modes sync_mode;
	long nr_to_write;		/* Write this many pages, and decrement
					   this for each page written */
	long pages_skipped;		/* Pages which were not written */

	/*
	 * For a_ops->writepages(): is start or end are non-zero then this is
	 * a hint that the filesystem need only write out the pages inside that
	 * byterange.  The byte at `end' is included in the writeout request.
	 */
	loff_t range_start;
	loff_t range_end;

	unsigned for_kupdate:1;		/* A kupdate writeback */
	unsigned for_background:1;	/* A background writeback */
	unsigned tagged_writepages:1;	/* tag-and-write to avoid livelock */
	unsigned for_reclaim:1;		/* Invoked from the page allocator */
	unsigned range_cyclic:1;	/* range_start is cyclic */
};

/*
 * fs/fs-writeback.c
 */	
struct bdi_writeback;
int inode_wait(void *);
void writeback_inodes_sb(struct super_block *);
void writeback_inodes_sb_nr(struct super_block *, unsigned long nr);
int writeback_inodes_sb_if_idle(struct super_block *);
int writeback_inodes_sb_nr_if_idle(struct super_block *, unsigned long nr);
void sync_inodes_sb(struct super_block *);
long writeback_inodes_wb(struct bdi_writeback *wb, long nr_pages);
long wb_do_writeback(struct bdi_writeback *wb, int force_wait);
void wakeup_flusher_threads(long nr_pages);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_NEW, inode_wait, TASK_UNINTERRUPTIBLE);
}
static inline void inode_sync_wait(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_SYNC, inode_wait,
							TASK_UNINTERRUPTIBLE);
}


/*
 * mm/page-writeback.c
 */
#ifdef CONFIG_BLOCK
void laptop_io_completion(struct backing_dev_info *info);
void laptop_sync_completion(void);
void laptop_mode_sync(struct work_struct *work);
void laptop_mode_timer_fn(unsigned long data);
#else
static inline void laptop_sync_completion(void) { }
#endif
void throttle_vm_writeout(gfp_t gfp_mask);

extern unsigned long global_dirty_limit;

/* These are exported to sysctl. */
extern int dirty_background_ratio;
extern unsigned long dirty_background_bytes;
extern int vm_dirty_ratio;
extern unsigned long vm_dirty_bytes;
extern unsigned int dirty_writeback_interval;
extern unsigned int dirty_expire_interval;
extern int vm_highmem_is_dirtyable;
extern int block_dump;
extern int laptop_mode;

extern unsigned long determine_dirtyable_memory(void);

extern int dirty_background_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_background_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);

struct ctl_table;
int dirty_writeback_centisecs_handler(struct ctl_table *, int,
				      void __user *, size_t *, loff_t *);

void global_dirty_limits(unsigned long *pbackground, unsigned long *pdirty);
unsigned long bdi_dirty_limit(struct backing_dev_info *bdi,
			       unsigned long dirty);

void __bdi_update_bandwidth(struct backing_dev_info *bdi,
			    unsigned long thresh,
			    unsigned long dirty,
			    unsigned long bdi_thresh,
			    unsigned long bdi_dirty,
			    unsigned long start_time);

void page_writeback_init(void);
void balance_dirty_pages_ratelimited_nr(struct address_space *mapping,
					unsigned long nr_pages_dirtied);

static inline void
balance_dirty_pages_ratelimited(struct address_space *mapping)
{
	balance_dirty_pages_ratelimited_nr(mapping, 1);
}

typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);

int generic_writepages(struct address_space *mapping,
		       struct writeback_control *wbc);
void tag_pages_for_writeback(struct address_space *mapping,
			     pgoff_t start, pgoff_t end);
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data);
int do_writepages(struct address_space *mapping, struct writeback_control *wbc);
void set_page_dirty_balance(struct page *page, int page_mkwrite);
void writeback_set_ratelimit(void);
void tag_pages_for_writeback(struct address_space *mapping,
			     pgoff_t start, pgoff_t end);

/* pdflush.c */
extern int nr_pdflush_threads;	/* Global so it can be exported to sysctl
				   read-only. */


#endif		/* WRITEBACK_H */
