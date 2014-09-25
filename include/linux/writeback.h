/*
 * include/linux/writeback.h
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/fs.h>

DECLARE_PER_CPU(int, dirty_throttle_leaks);

/*
 * The 1/4 region under the global dirty thresh is for smooth dirty throttling:
 *
 *	(thresh - thresh/DIRTY_FULL_SCOPE, thresh)
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

struct backing_dev_info;

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	WB_SYNC_NONE,	/* Don't wait on anything */
	WB_SYNC_ALL,	/* Wait on every mapping */
};

/*
 * why some writeback work was initiated
 */
enum wb_reason {
	WB_REASON_BACKGROUND,
	WB_REASON_TRY_TO_FREE_PAGES,
	WB_REASON_SYNC,
	WB_REASON_PERIODIC,
	WB_REASON_LAPTOP_TIMER,
	WB_REASON_FREE_MORE_MEM,
	WB_REASON_FS_FREE_SPACE,
	/*
	 * There is no bdi forker thread any more and works are done
	 * by emergency worker, however, this is TPs userland visible
	 * and we'll be exposing exactly the same information,
	 * so it has a mismatch name.
	 */
	WB_REASON_FORKER_THREAD,

	WB_REASON_MAX,
};

/*
 * A control structure which tells the writeback code what to do.  These are
 * always on the stack, and hence need no locking.  They are always initialised
 * in a manner such that unspecified fields are set to zero.
 */
struct writeback_control {
	long nr_to_write;		/* Write this many pages, and decrement
					   this for each page written */
	long pages_skipped;		/* Pages which were not written */

	/*
	 * For a_ops->writepages(): if start or end are non-zero then this is
	 * a hint that the filesystem need only write out the pages inside that
	 * byterange.  The byte at `end' is included in the writeout request.
	 */
	loff_t range_start;
	loff_t range_end;

	enum writeback_sync_modes sync_mode;

	unsigned for_kupdate:1;		/* A kupdate writeback */
	unsigned for_background:1;	/* A background writeback */
	unsigned tagged_writepages:1;	/* tag-and-write to avoid livelock */
	unsigned for_reclaim:1;		/* Invoked from the page allocator */
	unsigned range_cyclic:1;	/* range_start is cyclic */
	unsigned for_sync:1;		/* sync(2) WB_SYNC_ALL writeback */
};

/*
 * fs/fs-writeback.c
 */	
struct bdi_writeback;
void writeback_inodes_sb(struct super_block *, enum wb_reason reason);
void writeback_inodes_sb_nr(struct super_block *, unsigned long nr,
							enum wb_reason reason);
int try_to_writeback_inodes_sb(struct super_block *, enum wb_reason reason);
int try_to_writeback_inodes_sb_nr(struct super_block *, unsigned long nr,
				  enum wb_reason reason);
void sync_inodes_sb(struct super_block *);
void wakeup_flusher_threads(long nr_pages, enum wb_reason reason);
void inode_wait_for_writeback(struct inode *inode);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_NEW, TASK_UNINTERRUPTIBLE);
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
bool zone_dirty_ok(struct zone *zone);

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
			    unsigned long bg_thresh,
			    unsigned long dirty,
			    unsigned long bdi_thresh,
			    unsigned long bdi_dirty,
			    unsigned long start_time);

void page_writeback_init(void);
void balance_dirty_pages_ratelimited(struct address_space *mapping);

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
void set_page_dirty_balance(struct page *page);
void writeback_set_ratelimit(void);
void tag_pages_for_writeback(struct address_space *mapping,
			     pgoff_t start, pgoff_t end);

void account_page_redirty(struct page *page);

#endif		/* WRITEBACK_H */
