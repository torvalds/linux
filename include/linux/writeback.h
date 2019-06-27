/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/writeback.h
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/flex_proportions.h>
#include <linux/backing-dev-defs.h>
#include <linux/blk_types.h>

struct bio;

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
#ifdef CONFIG_CGROUP_WRITEBACK
	struct bdi_writeback *wb;	/* wb this writeback is issued under */
	struct inode *inode;		/* inode being written out */

	/* foreign inode detection, see wbc_detach_inode() */
	int wb_id;			/* current wb id */
	int wb_lcand_id;		/* last foreign candidate wb id */
	int wb_tcand_id;		/* this foreign candidate wb id */
	size_t wb_bytes;		/* bytes written by current wb */
	size_t wb_lcand_bytes;		/* bytes written by last candidate */
	size_t wb_tcand_bytes;		/* bytes written by this candidate */
#endif
};

static inline int wbc_to_write_flags(struct writeback_control *wbc)
{
	if (wbc->sync_mode == WB_SYNC_ALL)
		return REQ_SYNC;
	else if (wbc->for_kupdate || wbc->for_background)
		return REQ_BACKGROUND;

	return 0;
}

/*
 * A wb_domain represents a domain that wb's (bdi_writeback's) belong to
 * and are measured against each other in.  There always is one global
 * domain, global_wb_domain, that every wb in the system is a member of.
 * This allows measuring the relative bandwidth of each wb to distribute
 * dirtyable memory accordingly.
 */
struct wb_domain {
	spinlock_t lock;

	/*
	 * Scale the writeback cache size proportional to the relative
	 * writeout speed.
	 *
	 * We do this by keeping a floating proportion between BDIs, based
	 * on page writeback completions [end_page_writeback()]. Those
	 * devices that write out pages fastest will get the larger share,
	 * while the slower will get a smaller share.
	 *
	 * We use page writeout completions because we are interested in
	 * getting rid of dirty pages. Having them written out is the
	 * primary goal.
	 *
	 * We introduce a concept of time, a period over which we measure
	 * these events, because demand can/will vary over time. The length
	 * of this period itself is measured in page writeback completions.
	 */
	struct fprop_global completions;
	struct timer_list period_timer;	/* timer for aging of completions */
	unsigned long period_time;

	/*
	 * The dirtyable memory and dirty threshold could be suddenly
	 * knocked down by a large amount (eg. on the startup of KVM in a
	 * swapless system). This may throw the system into deep dirty
	 * exceeded state and throttle heavy/light dirtiers alike. To
	 * retain good responsiveness, maintain global_dirty_limit for
	 * tracking slowly down to the knocked down dirty threshold.
	 *
	 * Both fields are protected by ->lock.
	 */
	unsigned long dirty_limit_tstamp;
	unsigned long dirty_limit;
};

/**
 * wb_domain_size_changed - memory available to a wb_domain has changed
 * @dom: wb_domain of interest
 *
 * This function should be called when the amount of memory available to
 * @dom has changed.  It resets @dom's dirty limit parameters to prevent
 * the past values which don't match the current configuration from skewing
 * dirty throttling.  Without this, when memory size of a wb_domain is
 * greatly reduced, the dirty throttling logic may allow too many pages to
 * be dirtied leading to consecutive unnecessary OOMs and may get stuck in
 * that situation.
 */
static inline void wb_domain_size_changed(struct wb_domain *dom)
{
	spin_lock(&dom->lock);
	dom->dirty_limit_tstamp = jiffies;
	dom->dirty_limit = 0;
	spin_unlock(&dom->lock);
}

/*
 * fs/fs-writeback.c
 */	
struct bdi_writeback;
void writeback_inodes_sb(struct super_block *, enum wb_reason reason);
void writeback_inodes_sb_nr(struct super_block *, unsigned long nr,
							enum wb_reason reason);
void try_to_writeback_inodes_sb(struct super_block *sb, enum wb_reason reason);
void sync_inodes_sb(struct super_block *);
void wakeup_flusher_threads(enum wb_reason reason);
void wakeup_flusher_threads_bdi(struct backing_dev_info *bdi,
				enum wb_reason reason);
void inode_wait_for_writeback(struct inode *inode);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_NEW, TASK_UNINTERRUPTIBLE);
}

#ifdef CONFIG_CGROUP_WRITEBACK

#include <linux/cgroup.h>
#include <linux/bio.h>

void __inode_attach_wb(struct inode *inode, struct page *page);
void wbc_attach_and_unlock_inode(struct writeback_control *wbc,
				 struct inode *inode)
	__releases(&inode->i_lock);
void wbc_detach_inode(struct writeback_control *wbc);
void wbc_account_cgroup_owner(struct writeback_control *wbc, struct page *page,
			      size_t bytes);
void cgroup_writeback_umount(void);

/**
 * inode_attach_wb - associate an inode with its wb
 * @inode: inode of interest
 * @page: page being dirtied (may be NULL)
 *
 * If @inode doesn't have its wb, associate it with the wb matching the
 * memcg of @page or, if @page is NULL, %current.  May be called w/ or w/o
 * @inode->i_lock.
 */
static inline void inode_attach_wb(struct inode *inode, struct page *page)
{
	if (!inode->i_wb)
		__inode_attach_wb(inode, page);
}

/**
 * inode_detach_wb - disassociate an inode from its wb
 * @inode: inode of interest
 *
 * @inode is being freed.  Detach from its wb.
 */
static inline void inode_detach_wb(struct inode *inode)
{
	if (inode->i_wb) {
		WARN_ON_ONCE(!(inode->i_state & I_CLEAR));
		wb_put(inode->i_wb);
		inode->i_wb = NULL;
	}
}

/**
 * wbc_attach_fdatawrite_inode - associate wbc and inode for fdatawrite
 * @wbc: writeback_control of interest
 * @inode: target inode
 *
 * This function is to be used by __filemap_fdatawrite_range(), which is an
 * alternative entry point into writeback code, and first ensures @inode is
 * associated with a bdi_writeback and attaches it to @wbc.
 */
static inline void wbc_attach_fdatawrite_inode(struct writeback_control *wbc,
					       struct inode *inode)
{
	spin_lock(&inode->i_lock);
	inode_attach_wb(inode, NULL);
	wbc_attach_and_unlock_inode(wbc, inode);
}

/**
 * wbc_init_bio - writeback specific initializtion of bio
 * @wbc: writeback_control for the writeback in progress
 * @bio: bio to be initialized
 *
 * @bio is a part of the writeback in progress controlled by @wbc.  Perform
 * writeback specific initialization.  This is used to apply the cgroup
 * writeback context.  Must be called after the bio has been associated with
 * a device.
 */
static inline void wbc_init_bio(struct writeback_control *wbc, struct bio *bio)
{
	/*
	 * pageout() path doesn't attach @wbc to the inode being written
	 * out.  This is intentional as we don't want the function to block
	 * behind a slow cgroup.  Ultimately, we want pageout() to kick off
	 * regular writeback instead of writing things out itself.
	 */
	if (wbc->wb)
		bio_associate_blkg_from_css(bio, wbc->wb->blkcg_css);
}

#else	/* CONFIG_CGROUP_WRITEBACK */

static inline void inode_attach_wb(struct inode *inode, struct page *page)
{
}

static inline void inode_detach_wb(struct inode *inode)
{
}

static inline void wbc_attach_and_unlock_inode(struct writeback_control *wbc,
					       struct inode *inode)
	__releases(&inode->i_lock)
{
	spin_unlock(&inode->i_lock);
}

static inline void wbc_attach_fdatawrite_inode(struct writeback_control *wbc,
					       struct inode *inode)
{
}

static inline void wbc_detach_inode(struct writeback_control *wbc)
{
}

static inline void wbc_init_bio(struct writeback_control *wbc, struct bio *bio)
{
}

static inline void wbc_account_cgroup_owner(struct writeback_control *wbc,
					    struct page *page, size_t bytes)
{
}

static inline void cgroup_writeback_umount(void)
{
}

#endif	/* CONFIG_CGROUP_WRITEBACK */

/*
 * mm/page-writeback.c
 */
#ifdef CONFIG_BLOCK
void laptop_io_completion(struct backing_dev_info *info);
void laptop_sync_completion(void);
void laptop_mode_sync(struct work_struct *work);
void laptop_mode_timer_fn(struct timer_list *t);
#else
static inline void laptop_sync_completion(void) { }
#endif
bool node_dirty_ok(struct pglist_data *pgdat);
int wb_domain_init(struct wb_domain *dom, gfp_t gfp);
#ifdef CONFIG_CGROUP_WRITEBACK
void wb_domain_exit(struct wb_domain *dom);
#endif

extern struct wb_domain global_wb_domain;

/* These are exported to sysctl. */
extern int dirty_background_ratio;
extern unsigned long dirty_background_bytes;
extern int vm_dirty_ratio;
extern unsigned long vm_dirty_bytes;
extern unsigned int dirty_writeback_interval;
extern unsigned int dirty_expire_interval;
extern unsigned int dirtytime_expire_interval;
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
int dirtytime_interval_handler(struct ctl_table *table, int write,
			       void __user *buffer, size_t *lenp, loff_t *ppos);

struct ctl_table;
int dirty_writeback_centisecs_handler(struct ctl_table *, int,
				      void __user *, size_t *, loff_t *);

void global_dirty_limits(unsigned long *pbackground, unsigned long *pdirty);
unsigned long wb_calc_thresh(struct bdi_writeback *wb, unsigned long thresh);

void wb_update_bandwidth(struct bdi_writeback *wb, unsigned long start_time);
void balance_dirty_pages_ratelimited(struct address_space *mapping);
bool wb_over_bg_thresh(struct bdi_writeback *wb);

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
void writeback_set_ratelimit(void);
void tag_pages_for_writeback(struct address_space *mapping,
			     pgoff_t start, pgoff_t end);

void account_page_redirty(struct page *page);

void sb_mark_inode_writeback(struct inode *inode);
void sb_clear_inode_writeback(struct inode *inode);

#endif		/* WRITEBACK_H */
