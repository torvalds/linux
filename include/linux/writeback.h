/*
 * include/linux/writeback.h
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/fs.h>

struct backing_dev_info;

extern spinlock_t inode_lock;
extern struct list_head inode_in_use;
extern struct list_head inode_unused;

/*
 * Yes, writeback.h requires sched.h
 * No, sched.h is not included from here.
 */
static inline int task_is_pdflush(struct task_struct *task)
{
	return task->flags & PF_FLUSHER;
}

#define current_is_pdflush()	task_is_pdflush(current)

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
	struct backing_dev_info *bdi;	/* If !NULL, only write back this
					   queue */
	enum writeback_sync_modes sync_mode;
	unsigned long *older_than_this;	/* If !NULL, only write back inodes
					   older than this */
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

	unsigned nonblocking:1;		/* Don't get stuck on request queues */
	unsigned encountered_congestion:1; /* An output: a queue is full */
	unsigned for_kupdate:1;		/* A kupdate writeback */
	unsigned for_reclaim:1;		/* Invoked from the page allocator */
	unsigned for_writepages:1;	/* This is a writepages() call */
	unsigned range_cyclic:1;	/* range_start is cyclic */
	unsigned more_io:1;		/* more io to be dispatched */
	/*
	 * write_cache_pages() won't update wbc->nr_to_write and
	 * mapping->writeback_index if no_nrwrite_index_update
	 * is set.  write_cache_pages() may write more than we
	 * requested and we want to make sure nr_to_write and
	 * writeback_index are updated in a consistent manner
	 * so we use a single control to update them
	 */
	unsigned no_nrwrite_index_update:1;
};

/*
 * fs/fs-writeback.c
 */	
void writeback_inodes(struct writeback_control *wbc);
int inode_wait(void *);
void sync_inodes_sb(struct super_block *, int wait);
void sync_inodes(int wait);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	wait_on_bit(&inode->i_state, __I_LOCK, inode_wait,
							TASK_UNINTERRUPTIBLE);
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
int wakeup_pdflush(long nr_pages);
void laptop_io_completion(void);
void laptop_sync_completion(void);
void throttle_vm_writeout(gfp_t gfp_mask);

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
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_background_bytes_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_ratio_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos);
extern int dirty_bytes_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos);

struct ctl_table;
struct file;
int dirty_writeback_centisecs_handler(struct ctl_table *, int, struct file *,
				      void __user *, size_t *, loff_t *);

void get_dirty_limits(unsigned long *pbackground, unsigned long *pdirty,
		      unsigned long *pbdi_dirty, struct backing_dev_info *bdi);

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

int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0);
int generic_writepages(struct address_space *mapping,
		       struct writeback_control *wbc);
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data);
int do_writepages(struct address_space *mapping, struct writeback_control *wbc);
int sync_page_range(struct inode *inode, struct address_space *mapping,
			loff_t pos, loff_t count);
int sync_page_range_nolock(struct inode *inode, struct address_space *mapping,
			   loff_t pos, loff_t count);
void set_page_dirty_balance(struct page *page, int page_mkwrite);
void writeback_set_ratelimit(void);

/* pdflush.c */
extern int nr_pdflush_threads;	/* Global so it can be exported to sysctl
				   read-only. */
extern int nr_pdflush_threads_max; /* Global so it can be exported to sysctl */
extern int nr_pdflush_threads_min; /* Global so it can be exported to sysctl */


#endif		/* WRITEBACK_H */
