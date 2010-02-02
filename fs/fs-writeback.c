/*
 * fs/fs-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains all the functions related to writing back and waiting
 * upon dirty inodes against superblocks, and writing back dirty
 * pages against inodes.  ie: data writeback.  Writeout of the
 * inode itself is not handled here.
 *
 * 10Apr2002	Andrew Morton
 *		Split out of fs/inode.c
 *		Additions for address_space-based writeback
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include "internal.h"

#define inode_to_bdi(inode)	((inode)->i_mapping->backing_dev_info)

/*
 * We don't actually have pdflush, but this one is exported though /proc...
 */
int nr_pdflush_threads;

/*
 * Passed into wb_writeback(), essentially a subset of writeback_control
 */
struct wb_writeback_args {
	long nr_pages;
	struct super_block *sb;
	enum writeback_sync_modes sync_mode;
	int for_kupdate:1;
	int range_cyclic:1;
	int for_background:1;
};

/*
 * Work items for the bdi_writeback threads
 */
struct bdi_work {
	struct list_head list;		/* pending work list */
	struct rcu_head rcu_head;	/* for RCU free/clear of work */

	unsigned long seen;		/* threads that have seen this work */
	atomic_t pending;		/* number of threads still to do work */

	struct wb_writeback_args args;	/* writeback arguments */

	unsigned long state;		/* flag bits, see WS_* */
};

enum {
	WS_USED_B = 0,
	WS_ONSTACK_B,
};

#define WS_USED (1 << WS_USED_B)
#define WS_ONSTACK (1 << WS_ONSTACK_B)

static inline bool bdi_work_on_stack(struct bdi_work *work)
{
	return test_bit(WS_ONSTACK_B, &work->state);
}

static inline void bdi_work_init(struct bdi_work *work,
				 struct wb_writeback_args *args)
{
	INIT_RCU_HEAD(&work->rcu_head);
	work->args = *args;
	work->state = WS_USED;
}

/**
 * writeback_in_progress - determine whether there is writeback in progress
 * @bdi: the device's backing_dev_info structure.
 *
 * Determine whether there is writeback waiting to be handled against a
 * backing device.
 */
int writeback_in_progress(struct backing_dev_info *bdi)
{
	return !list_empty(&bdi->work_list);
}

static void bdi_work_clear(struct bdi_work *work)
{
	clear_bit(WS_USED_B, &work->state);
	smp_mb__after_clear_bit();
	/*
	 * work can have disappeared at this point. bit waitq functions
	 * should be able to tolerate this, provided bdi_sched_wait does
	 * not dereference it's pointer argument.
	*/
	wake_up_bit(&work->state, WS_USED_B);
}

static void bdi_work_free(struct rcu_head *head)
{
	struct bdi_work *work = container_of(head, struct bdi_work, rcu_head);

	if (!bdi_work_on_stack(work))
		kfree(work);
	else
		bdi_work_clear(work);
}

static void wb_work_complete(struct bdi_work *work)
{
	const enum writeback_sync_modes sync_mode = work->args.sync_mode;
	int onstack = bdi_work_on_stack(work);

	/*
	 * For allocated work, we can clear the done/seen bit right here.
	 * For on-stack work, we need to postpone both the clear and free
	 * to after the RCU grace period, since the stack could be invalidated
	 * as soon as bdi_work_clear() has done the wakeup.
	 */
	if (!onstack)
		bdi_work_clear(work);
	if (sync_mode == WB_SYNC_NONE || onstack)
		call_rcu(&work->rcu_head, bdi_work_free);
}

static void wb_clear_pending(struct bdi_writeback *wb, struct bdi_work *work)
{
	/*
	 * The caller has retrieved the work arguments from this work,
	 * drop our reference. If this is the last ref, delete and free it
	 */
	if (atomic_dec_and_test(&work->pending)) {
		struct backing_dev_info *bdi = wb->bdi;

		spin_lock(&bdi->wb_lock);
		list_del_rcu(&work->list);
		spin_unlock(&bdi->wb_lock);

		wb_work_complete(work);
	}
}

static void bdi_queue_work(struct backing_dev_info *bdi, struct bdi_work *work)
{
	work->seen = bdi->wb_mask;
	BUG_ON(!work->seen);
	atomic_set(&work->pending, bdi->wb_cnt);
	BUG_ON(!bdi->wb_cnt);

	/*
	 * list_add_tail_rcu() contains the necessary barriers to
	 * make sure the above stores are seen before the item is
	 * noticed on the list
	 */
	spin_lock(&bdi->wb_lock);
	list_add_tail_rcu(&work->list, &bdi->work_list);
	spin_unlock(&bdi->wb_lock);

	/*
	 * If the default thread isn't there, make sure we add it. When
	 * it gets created and wakes up, we'll run this work.
	 */
	if (unlikely(list_empty_careful(&bdi->wb_list)))
		wake_up_process(default_backing_dev_info.wb.task);
	else {
		struct bdi_writeback *wb = &bdi->wb;

		if (wb->task)
			wake_up_process(wb->task);
	}
}

/*
 * Used for on-stack allocated work items. The caller needs to wait until
 * the wb threads have acked the work before it's safe to continue.
 */
static void bdi_wait_on_work_clear(struct bdi_work *work)
{
	wait_on_bit(&work->state, WS_USED_B, bdi_sched_wait,
		    TASK_UNINTERRUPTIBLE);
}

static void bdi_alloc_queue_work(struct backing_dev_info *bdi,
				 struct wb_writeback_args *args)
{
	struct bdi_work *work;

	/*
	 * This is WB_SYNC_NONE writeback, so if allocation fails just
	 * wakeup the thread for old dirty data writeback
	 */
	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (work) {
		bdi_work_init(work, args);
		bdi_queue_work(bdi, work);
	} else {
		struct bdi_writeback *wb = &bdi->wb;

		if (wb->task)
			wake_up_process(wb->task);
	}
}

/**
 * bdi_sync_writeback - start and wait for writeback
 * @bdi: the backing device to write from
 * @sb: write inodes from this super_block
 *
 * Description:
 *   This does WB_SYNC_ALL data integrity writeback and waits for the
 *   IO to complete. Callers must hold the sb s_umount semaphore for
 *   reading, to avoid having the super disappear before we are done.
 */
static void bdi_sync_writeback(struct backing_dev_info *bdi,
			       struct super_block *sb)
{
	struct wb_writeback_args args = {
		.sb		= sb,
		.sync_mode	= WB_SYNC_ALL,
		.nr_pages	= LONG_MAX,
		.range_cyclic	= 0,
	};
	struct bdi_work work;

	bdi_work_init(&work, &args);
	work.state |= WS_ONSTACK;

	bdi_queue_work(bdi, &work);
	bdi_wait_on_work_clear(&work);
}

/**
 * bdi_start_writeback - start writeback
 * @bdi: the backing device to write from
 * @sb: write inodes from this super_block
 * @nr_pages: the number of pages to write
 *
 * Description:
 *   This does WB_SYNC_NONE opportunistic writeback. The IO is only
 *   started when this function returns, we make no guarentees on
 *   completion. Caller need not hold sb s_umount semaphore.
 *
 */
void bdi_start_writeback(struct backing_dev_info *bdi, struct super_block *sb,
			 long nr_pages)
{
	struct wb_writeback_args args = {
		.sb		= sb,
		.sync_mode	= WB_SYNC_NONE,
		.nr_pages	= nr_pages,
		.range_cyclic	= 1,
	};

	/*
	 * We treat @nr_pages=0 as the special case to do background writeback,
	 * ie. to sync pages until the background dirty threshold is reached.
	 */
	if (!nr_pages) {
		args.nr_pages = LONG_MAX;
		args.for_background = 1;
	}

	bdi_alloc_queue_work(bdi, &args);
}

/*
 * Redirty an inode: set its when-it-was dirtied timestamp and move it to the
 * furthest end of its superblock's dirty-inode list.
 *
 * Before stamping the inode's ->dirtied_when, we check to see whether it is
 * already the most-recently-dirtied inode on the b_dirty list.  If that is
 * the case then the inode must have been redirtied while it was being written
 * out and we don't reset its dirtied_when.
 */
static void redirty_tail(struct inode *inode)
{
	struct bdi_writeback *wb = &inode_to_bdi(inode)->wb;

	if (!list_empty(&wb->b_dirty)) {
		struct inode *tail;

		tail = list_entry(wb->b_dirty.next, struct inode, i_list);
		if (time_before(inode->dirtied_when, tail->dirtied_when))
			inode->dirtied_when = jiffies;
	}
	list_move(&inode->i_list, &wb->b_dirty);
}

/*
 * requeue inode for re-scanning after bdi->b_io list is exhausted.
 */
static void requeue_io(struct inode *inode)
{
	struct bdi_writeback *wb = &inode_to_bdi(inode)->wb;

	list_move(&inode->i_list, &wb->b_more_io);
}

static void inode_sync_complete(struct inode *inode)
{
	/*
	 * Prevent speculative execution through spin_unlock(&inode_lock);
	 */
	smp_mb();
	wake_up_bit(&inode->i_state, __I_SYNC);
}

static bool inode_dirtied_after(struct inode *inode, unsigned long t)
{
	bool ret = time_after(inode->dirtied_when, t);
#ifndef CONFIG_64BIT
	/*
	 * For inodes being constantly redirtied, dirtied_when can get stuck.
	 * It _appears_ to be in the future, but is actually in distant past.
	 * This test is necessary to prevent such wrapped-around relative times
	 * from permanently stopping the whole bdi writeback.
	 */
	ret = ret && time_before_eq(inode->dirtied_when, jiffies);
#endif
	return ret;
}

/*
 * Move expired dirty inodes from @delaying_queue to @dispatch_queue.
 */
static void move_expired_inodes(struct list_head *delaying_queue,
			       struct list_head *dispatch_queue,
				unsigned long *older_than_this)
{
	LIST_HEAD(tmp);
	struct list_head *pos, *node;
	struct super_block *sb = NULL;
	struct inode *inode;
	int do_sb_sort = 0;

	while (!list_empty(delaying_queue)) {
		inode = list_entry(delaying_queue->prev, struct inode, i_list);
		if (older_than_this &&
		    inode_dirtied_after(inode, *older_than_this))
			break;
		if (sb && sb != inode->i_sb)
			do_sb_sort = 1;
		sb = inode->i_sb;
		list_move(&inode->i_list, &tmp);
	}

	/* just one sb in list, splice to dispatch_queue and we're done */
	if (!do_sb_sort) {
		list_splice(&tmp, dispatch_queue);
		return;
	}

	/* Move inodes from one superblock together */
	while (!list_empty(&tmp)) {
		inode = list_entry(tmp.prev, struct inode, i_list);
		sb = inode->i_sb;
		list_for_each_prev_safe(pos, node, &tmp) {
			inode = list_entry(pos, struct inode, i_list);
			if (inode->i_sb == sb)
				list_move(&inode->i_list, dispatch_queue);
		}
	}
}

/*
 * Queue all expired dirty inodes for io, eldest first.
 */
static void queue_io(struct bdi_writeback *wb, unsigned long *older_than_this)
{
	list_splice_init(&wb->b_more_io, wb->b_io.prev);
	move_expired_inodes(&wb->b_dirty, &wb->b_io, older_than_this);
}

static int write_inode(struct inode *inode, int sync)
{
	if (inode->i_sb->s_op->write_inode && !is_bad_inode(inode))
		return inode->i_sb->s_op->write_inode(inode, sync);
	return 0;
}

/*
 * Wait for writeback on an inode to complete.
 */
static void inode_wait_for_writeback(struct inode *inode)
{
	DEFINE_WAIT_BIT(wq, &inode->i_state, __I_SYNC);
	wait_queue_head_t *wqh;

	wqh = bit_waitqueue(&inode->i_state, __I_SYNC);
	do {
		spin_unlock(&inode_lock);
		__wait_on_bit(wqh, &wq, inode_wait, TASK_UNINTERRUPTIBLE);
		spin_lock(&inode_lock);
	} while (inode->i_state & I_SYNC);
}

/*
 * Write out an inode's dirty pages.  Called under inode_lock.  Either the
 * caller has ref on the inode (either via __iget or via syscall against an fd)
 * or the inode has I_WILL_FREE set (via generic_forget_inode)
 *
 * If `wait' is set, wait on the writeout.
 *
 * The whole writeout design is quite complex and fragile.  We want to avoid
 * starvation of particular inodes when others are being redirtied, prevent
 * livelocks, etc.
 *
 * Called under inode_lock.
 */
static int
writeback_single_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct address_space *mapping = inode->i_mapping;
	int wait = wbc->sync_mode == WB_SYNC_ALL;
	unsigned dirty;
	int ret;

	if (!atomic_read(&inode->i_count))
		WARN_ON(!(inode->i_state & (I_WILL_FREE|I_FREEING)));
	else
		WARN_ON(inode->i_state & I_WILL_FREE);

	if (inode->i_state & I_SYNC) {
		/*
		 * If this inode is locked for writeback and we are not doing
		 * writeback-for-data-integrity, move it to b_more_io so that
		 * writeback can proceed with the other inodes on s_io.
		 *
		 * We'll have another go at writing back this inode when we
		 * completed a full scan of b_io.
		 */
		if (!wait) {
			requeue_io(inode);
			return 0;
		}

		/*
		 * It's a data-integrity sync.  We must wait.
		 */
		inode_wait_for_writeback(inode);
	}

	BUG_ON(inode->i_state & I_SYNC);

	/* Set I_SYNC, reset I_DIRTY */
	dirty = inode->i_state & I_DIRTY;
	inode->i_state |= I_SYNC;
	inode->i_state &= ~I_DIRTY;

	spin_unlock(&inode_lock);

	ret = do_writepages(mapping, wbc);

	/* Don't write the inode if only I_DIRTY_PAGES was set */
	if (dirty & (I_DIRTY_SYNC | I_DIRTY_DATASYNC)) {
		int err = write_inode(inode, wait);
		if (ret == 0)
			ret = err;
	}

	if (wait) {
		int err = filemap_fdatawait(mapping);
		if (ret == 0)
			ret = err;
	}

	spin_lock(&inode_lock);
	inode->i_state &= ~I_SYNC;
	if (!(inode->i_state & (I_FREEING | I_CLEAR))) {
		if ((inode->i_state & I_DIRTY_PAGES) && wbc->for_kupdate) {
			/*
			 * More pages get dirtied by a fast dirtier.
			 */
			goto select_queue;
		} else if (inode->i_state & I_DIRTY) {
			/*
			 * At least XFS will redirty the inode during the
			 * writeback (delalloc) and on io completion (isize).
			 */
			redirty_tail(inode);
		} else if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY)) {
			/*
			 * We didn't write back all the pages.  nfs_writepages()
			 * sometimes bales out without doing anything. Redirty
			 * the inode; Move it from b_io onto b_more_io/b_dirty.
			 */
			/*
			 * akpm: if the caller was the kupdate function we put
			 * this inode at the head of b_dirty so it gets first
			 * consideration.  Otherwise, move it to the tail, for
			 * the reasons described there.  I'm not really sure
			 * how much sense this makes.  Presumably I had a good
			 * reasons for doing it this way, and I'd rather not
			 * muck with it at present.
			 */
			if (wbc->for_kupdate) {
				/*
				 * For the kupdate function we move the inode
				 * to b_more_io so it will get more writeout as
				 * soon as the queue becomes uncongested.
				 */
				inode->i_state |= I_DIRTY_PAGES;
select_queue:
				if (wbc->nr_to_write <= 0) {
					/*
					 * slice used up: queue for next turn
					 */
					requeue_io(inode);
				} else {
					/*
					 * somehow blocked: retry later
					 */
					redirty_tail(inode);
				}
			} else {
				/*
				 * Otherwise fully redirty the inode so that
				 * other inodes on this superblock will get some
				 * writeout.  Otherwise heavy writing to one
				 * file would indefinitely suspend writeout of
				 * all the other files.
				 */
				inode->i_state |= I_DIRTY_PAGES;
				redirty_tail(inode);
			}
		} else if (atomic_read(&inode->i_count)) {
			/*
			 * The inode is clean, inuse
			 */
			list_move(&inode->i_list, &inode_in_use);
		} else {
			/*
			 * The inode is clean, unused
			 */
			list_move(&inode->i_list, &inode_unused);
		}
	}
	inode_sync_complete(inode);
	return ret;
}

static void unpin_sb_for_writeback(struct super_block **psb)
{
	struct super_block *sb = *psb;

	if (sb) {
		up_read(&sb->s_umount);
		put_super(sb);
		*psb = NULL;
	}
}

/*
 * For WB_SYNC_NONE writeback, the caller does not have the sb pinned
 * before calling writeback. So make sure that we do pin it, so it doesn't
 * go away while we are writing inodes from it.
 *
 * Returns 0 if the super was successfully pinned (or pinning wasn't needed),
 * 1 if we failed.
 */
static int pin_sb_for_writeback(struct writeback_control *wbc,
				struct inode *inode, struct super_block **psb)
{
	struct super_block *sb = inode->i_sb;

	/*
	 * If this sb is already pinned, nothing more to do. If not and
	 * *psb is non-NULL, unpin the old one first
	 */
	if (sb == *psb)
		return 0;
	else if (*psb)
		unpin_sb_for_writeback(psb);

	/*
	 * Caller must already hold the ref for this
	 */
	if (wbc->sync_mode == WB_SYNC_ALL) {
		WARN_ON(!rwsem_is_locked(&sb->s_umount));
		return 0;
	}

	spin_lock(&sb_lock);
	sb->s_count++;
	if (down_read_trylock(&sb->s_umount)) {
		if (sb->s_root) {
			spin_unlock(&sb_lock);
			goto pinned;
		}
		/*
		 * umounted, drop rwsem again and fall through to failure
		 */
		up_read(&sb->s_umount);
	}

	sb->s_count--;
	spin_unlock(&sb_lock);
	return 1;
pinned:
	*psb = sb;
	return 0;
}

static void writeback_inodes_wb(struct bdi_writeback *wb,
				struct writeback_control *wbc)
{
	struct super_block *sb = wbc->sb, *pin_sb = NULL;
	const unsigned long start = jiffies;	/* livelock avoidance */

	spin_lock(&inode_lock);

	if (!wbc->for_kupdate || list_empty(&wb->b_io))
		queue_io(wb, wbc->older_than_this);

	while (!list_empty(&wb->b_io)) {
		struct inode *inode = list_entry(wb->b_io.prev,
						struct inode, i_list);
		long pages_skipped;

		/*
		 * super block given and doesn't match, skip this inode
		 */
		if (sb && sb != inode->i_sb) {
			redirty_tail(inode);
			continue;
		}

		if (inode->i_state & (I_NEW | I_WILL_FREE)) {
			requeue_io(inode);
			continue;
		}

		/*
		 * Was this inode dirtied after sync_sb_inodes was called?
		 * This keeps sync from extra jobs and livelock.
		 */
		if (inode_dirtied_after(inode, start))
			break;

		if (pin_sb_for_writeback(wbc, inode, &pin_sb)) {
			requeue_io(inode);
			continue;
		}

		BUG_ON(inode->i_state & (I_FREEING | I_CLEAR));
		__iget(inode);
		pages_skipped = wbc->pages_skipped;
		writeback_single_inode(inode, wbc);
		if (wbc->pages_skipped != pages_skipped) {
			/*
			 * writeback is not making progress due to locked
			 * buffers.  Skip this inode for now.
			 */
			redirty_tail(inode);
		}
		spin_unlock(&inode_lock);
		iput(inode);
		cond_resched();
		spin_lock(&inode_lock);
		if (wbc->nr_to_write <= 0) {
			wbc->more_io = 1;
			break;
		}
		if (!list_empty(&wb->b_more_io))
			wbc->more_io = 1;
	}

	unpin_sb_for_writeback(&pin_sb);

	spin_unlock(&inode_lock);
	/* Leave any unwritten inodes on b_io */
}

void writeback_inodes_wbc(struct writeback_control *wbc)
{
	struct backing_dev_info *bdi = wbc->bdi;

	writeback_inodes_wb(&bdi->wb, wbc);
}

/*
 * The maximum number of pages to writeout in a single bdi flush/kupdate
 * operation.  We do this so we don't hold I_SYNC against an inode for
 * enormous amounts of time, which would block a userspace task which has
 * been forced to throttle against that inode.  Also, the code reevaluates
 * the dirty each time it has written this many pages.
 */
#define MAX_WRITEBACK_PAGES     1024

static inline bool over_bground_thresh(void)
{
	unsigned long background_thresh, dirty_thresh;

	get_dirty_limits(&background_thresh, &dirty_thresh, NULL, NULL);

	return (global_page_state(NR_FILE_DIRTY) +
		global_page_state(NR_UNSTABLE_NFS) >= background_thresh);
}

/*
 * Explicit flushing or periodic writeback of "old" data.
 *
 * Define "old": the first time one of an inode's pages is dirtied, we mark the
 * dirtying-time in the inode's address_space.  So this periodic writeback code
 * just walks the superblock inode list, writing back any inodes which are
 * older than a specific point in time.
 *
 * Try to run once per dirty_writeback_interval.  But if a writeback event
 * takes longer than a dirty_writeback_interval interval, then leave a
 * one-second gap.
 *
 * older_than_this takes precedence over nr_to_write.  So we'll only write back
 * all dirty pages if they are all attached to "old" mappings.
 */
static long wb_writeback(struct bdi_writeback *wb,
			 struct wb_writeback_args *args)
{
	struct writeback_control wbc = {
		.bdi			= wb->bdi,
		.sb			= args->sb,
		.sync_mode		= args->sync_mode,
		.older_than_this	= NULL,
		.for_kupdate		= args->for_kupdate,
		.for_background		= args->for_background,
		.range_cyclic		= args->range_cyclic,
	};
	unsigned long oldest_jif;
	long wrote = 0;
	struct inode *inode;

	if (wbc.for_kupdate) {
		wbc.older_than_this = &oldest_jif;
		oldest_jif = jiffies -
				msecs_to_jiffies(dirty_expire_interval * 10);
	}
	if (!wbc.range_cyclic) {
		wbc.range_start = 0;
		wbc.range_end = LLONG_MAX;
	}

	for (;;) {
		/*
		 * Stop writeback when nr_pages has been consumed
		 */
		if (args->nr_pages <= 0)
			break;

		/*
		 * For background writeout, stop when we are below the
		 * background dirty threshold
		 */
		if (args->for_background && !over_bground_thresh())
			break;

		wbc.more_io = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		wbc.pages_skipped = 0;
		writeback_inodes_wb(wb, &wbc);
		args->nr_pages -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
		wrote += MAX_WRITEBACK_PAGES - wbc.nr_to_write;

		/*
		 * If we consumed everything, see if we have more
		 */
		if (wbc.nr_to_write <= 0)
			continue;
		/*
		 * Didn't write everything and we don't have more IO, bail
		 */
		if (!wbc.more_io)
			break;
		/*
		 * Did we write something? Try for more
		 */
		if (wbc.nr_to_write < MAX_WRITEBACK_PAGES)
			continue;
		/*
		 * Nothing written. Wait for some inode to
		 * become available for writeback. Otherwise
		 * we'll just busyloop.
		 */
		spin_lock(&inode_lock);
		if (!list_empty(&wb->b_more_io))  {
			inode = list_entry(wb->b_more_io.prev,
						struct inode, i_list);
			inode_wait_for_writeback(inode);
		}
		spin_unlock(&inode_lock);
	}

	return wrote;
}

/*
 * Return the next bdi_work struct that hasn't been processed by this
 * wb thread yet. ->seen is initially set for each thread that exists
 * for this device, when a thread first notices a piece of work it
 * clears its bit. Depending on writeback type, the thread will notify
 * completion on either receiving the work (WB_SYNC_NONE) or after
 * it is done (WB_SYNC_ALL).
 */
static struct bdi_work *get_next_work_item(struct backing_dev_info *bdi,
					   struct bdi_writeback *wb)
{
	struct bdi_work *work, *ret = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(work, &bdi->work_list, list) {
		if (!test_bit(wb->nr, &work->seen))
			continue;
		clear_bit(wb->nr, &work->seen);

		ret = work;
		break;
	}

	rcu_read_unlock();
	return ret;
}

static long wb_check_old_data_flush(struct bdi_writeback *wb)
{
	unsigned long expired;
	long nr_pages;

	expired = wb->last_old_flush +
			msecs_to_jiffies(dirty_writeback_interval * 10);
	if (time_before(jiffies, expired))
		return 0;

	wb->last_old_flush = jiffies;
	nr_pages = global_page_state(NR_FILE_DIRTY) +
			global_page_state(NR_UNSTABLE_NFS) +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);

	if (nr_pages) {
		struct wb_writeback_args args = {
			.nr_pages	= nr_pages,
			.sync_mode	= WB_SYNC_NONE,
			.for_kupdate	= 1,
			.range_cyclic	= 1,
		};

		return wb_writeback(wb, &args);
	}

	return 0;
}

/*
 * Retrieve work items and do the writeback they describe
 */
long wb_do_writeback(struct bdi_writeback *wb, int force_wait)
{
	struct backing_dev_info *bdi = wb->bdi;
	struct bdi_work *work;
	long wrote = 0;

	while ((work = get_next_work_item(bdi, wb)) != NULL) {
		struct wb_writeback_args args = work->args;

		/*
		 * Override sync mode, in case we must wait for completion
		 */
		if (force_wait)
			work->args.sync_mode = args.sync_mode = WB_SYNC_ALL;

		/*
		 * If this isn't a data integrity operation, just notify
		 * that we have seen this work and we are now starting it.
		 */
		if (args.sync_mode == WB_SYNC_NONE)
			wb_clear_pending(wb, work);

		wrote += wb_writeback(wb, &args);

		/*
		 * This is a data integrity writeback, so only do the
		 * notification when we have completed the work.
		 */
		if (args.sync_mode == WB_SYNC_ALL)
			wb_clear_pending(wb, work);
	}

	/*
	 * Check for periodic writeback, kupdated() style
	 */
	wrote += wb_check_old_data_flush(wb);

	return wrote;
}

/*
 * Handle writeback of dirty data for the device backed by this bdi. Also
 * wakes up periodically and does kupdated style flushing.
 */
int bdi_writeback_task(struct bdi_writeback *wb)
{
	unsigned long last_active = jiffies;
	unsigned long wait_jiffies = -1UL;
	long pages_written;

	while (!kthread_should_stop()) {
		pages_written = wb_do_writeback(wb, 0);

		if (pages_written)
			last_active = jiffies;
		else if (wait_jiffies != -1UL) {
			unsigned long max_idle;

			/*
			 * Longest period of inactivity that we tolerate. If we
			 * see dirty data again later, the task will get
			 * recreated automatically.
			 */
			max_idle = max(5UL * 60 * HZ, wait_jiffies);
			if (time_after(jiffies, max_idle + last_active))
				break;
		}

		wait_jiffies = msecs_to_jiffies(dirty_writeback_interval * 10);
		schedule_timeout_interruptible(wait_jiffies);
		try_to_freeze();
	}

	return 0;
}

/*
 * Schedule writeback for all backing devices. This does WB_SYNC_NONE
 * writeback, for integrity writeback see bdi_sync_writeback().
 */
static void bdi_writeback_all(struct super_block *sb, long nr_pages)
{
	struct wb_writeback_args args = {
		.sb		= sb,
		.nr_pages	= nr_pages,
		.sync_mode	= WB_SYNC_NONE,
	};
	struct backing_dev_info *bdi;

	rcu_read_lock();

	list_for_each_entry_rcu(bdi, &bdi_list, bdi_list) {
		if (!bdi_has_dirty_io(bdi))
			continue;

		bdi_alloc_queue_work(bdi, &args);
	}

	rcu_read_unlock();
}

/*
 * Start writeback of `nr_pages' pages.  If `nr_pages' is zero, write back
 * the whole world.
 */
void wakeup_flusher_threads(long nr_pages)
{
	if (nr_pages == 0)
		nr_pages = global_page_state(NR_FILE_DIRTY) +
				global_page_state(NR_UNSTABLE_NFS);
	bdi_writeback_all(NULL, nr_pages);
}

static noinline void block_dump___mark_inode_dirty(struct inode *inode)
{
	if (inode->i_ino || strcmp(inode->i_sb->s_id, "bdev")) {
		struct dentry *dentry;
		const char *name = "?";

		dentry = d_find_alias(inode);
		if (dentry) {
			spin_lock(&dentry->d_lock);
			name = (const char *) dentry->d_name.name;
		}
		printk(KERN_DEBUG
		       "%s(%d): dirtied inode %lu (%s) on %s\n",
		       current->comm, task_pid_nr(current), inode->i_ino,
		       name, inode->i_sb->s_id);
		if (dentry) {
			spin_unlock(&dentry->d_lock);
			dput(dentry);
		}
	}
}

/**
 *	__mark_inode_dirty -	internal function
 *	@inode: inode to mark
 *	@flags: what kind of dirty (i.e. I_DIRTY_SYNC)
 *	Mark an inode as dirty. Callers should use mark_inode_dirty or
 *  	mark_inode_dirty_sync.
 *
 * Put the inode on the super block's dirty list.
 *
 * CAREFUL! We mark it dirty unconditionally, but move it onto the
 * dirty list only if it is hashed or if it refers to a blockdev.
 * If it was not hashed, it will never be added to the dirty list
 * even if it is later hashed, as it will have been marked dirty already.
 *
 * In short, make sure you hash any inodes _before_ you start marking
 * them dirty.
 *
 * This function *must* be atomic for the I_DIRTY_PAGES case -
 * set_page_dirty() is called under spinlock in several places.
 *
 * Note that for blockdevs, inode->dirtied_when represents the dirtying time of
 * the block-special inode (/dev/hda1) itself.  And the ->dirtied_when field of
 * the kernel-internal blockdev inode represents the dirtying time of the
 * blockdev's pages.  This is why for I_DIRTY_PAGES we always use
 * page->mapping->host, so the page-dirtying time is recorded in the internal
 * blockdev inode.
 */
void __mark_inode_dirty(struct inode *inode, int flags)
{
	struct super_block *sb = inode->i_sb;

	/*
	 * Don't do this for I_DIRTY_PAGES - that doesn't actually
	 * dirty the inode itself
	 */
	if (flags & (I_DIRTY_SYNC | I_DIRTY_DATASYNC)) {
		if (sb->s_op->dirty_inode)
			sb->s_op->dirty_inode(inode);
	}

	/*
	 * make sure that changes are seen by all cpus before we test i_state
	 * -- mikulas
	 */
	smp_mb();

	/* avoid the locking if we can */
	if ((inode->i_state & flags) == flags)
		return;

	if (unlikely(block_dump))
		block_dump___mark_inode_dirty(inode);

	spin_lock(&inode_lock);
	if ((inode->i_state & flags) != flags) {
		const int was_dirty = inode->i_state & I_DIRTY;

		inode->i_state |= flags;

		/*
		 * If the inode is being synced, just update its dirty state.
		 * The unlocker will place the inode on the appropriate
		 * superblock list, based upon its state.
		 */
		if (inode->i_state & I_SYNC)
			goto out;

		/*
		 * Only add valid (hashed) inodes to the superblock's
		 * dirty list.  Add blockdev inodes as well.
		 */
		if (!S_ISBLK(inode->i_mode)) {
			if (hlist_unhashed(&inode->i_hash))
				goto out;
		}
		if (inode->i_state & (I_FREEING|I_CLEAR))
			goto out;

		/*
		 * If the inode was already on b_dirty/b_io/b_more_io, don't
		 * reposition it (that would break b_dirty time-ordering).
		 */
		if (!was_dirty) {
			struct bdi_writeback *wb = &inode_to_bdi(inode)->wb;
			struct backing_dev_info *bdi = wb->bdi;

			if (bdi_cap_writeback_dirty(bdi) &&
			    !test_bit(BDI_registered, &bdi->state)) {
				WARN_ON(1);
				printk(KERN_ERR "bdi-%s not registered\n",
								bdi->name);
			}

			inode->dirtied_when = jiffies;
			list_move(&inode->i_list, &wb->b_dirty);
		}
	}
out:
	spin_unlock(&inode_lock);
}
EXPORT_SYMBOL(__mark_inode_dirty);

/*
 * Write out a superblock's list of dirty inodes.  A wait will be performed
 * upon no inodes, all inodes or the final one, depending upon sync_mode.
 *
 * If older_than_this is non-NULL, then only write out inodes which
 * had their first dirtying at a time earlier than *older_than_this.
 *
 * If `bdi' is non-zero then we're being asked to writeback a specific queue.
 * This function assumes that the blockdev superblock's inodes are backed by
 * a variety of queues, so all inodes are searched.  For other superblocks,
 * assume that all inodes are backed by the same queue.
 *
 * The inodes to be written are parked on bdi->b_io.  They are moved back onto
 * bdi->b_dirty as they are selected for writing.  This way, none can be missed
 * on the writer throttling path, and we get decent balancing between many
 * throttled threads: we don't want them all piling up on inode_sync_wait.
 */
static void wait_sb_inodes(struct super_block *sb)
{
	struct inode *inode, *old_inode = NULL;

	/*
	 * We need to be protected against the filesystem going from
	 * r/o to r/w or vice versa.
	 */
	WARN_ON(!rwsem_is_locked(&sb->s_umount));

	spin_lock(&inode_lock);

	/*
	 * Data integrity sync. Must wait for all pages under writeback,
	 * because there may have been pages dirtied before our sync
	 * call, but which had writeout started before we write it out.
	 * In which case, the inode may not be on the dirty list, but
	 * we still have to wait for that writeout.
	 */
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		struct address_space *mapping;

		if (inode->i_state & (I_FREEING|I_CLEAR|I_WILL_FREE|I_NEW))
			continue;
		mapping = inode->i_mapping;
		if (mapping->nrpages == 0)
			continue;
		__iget(inode);
		spin_unlock(&inode_lock);
		/*
		 * We hold a reference to 'inode' so it couldn't have
		 * been removed from s_inodes list while we dropped the
		 * inode_lock.  We cannot iput the inode now as we can
		 * be holding the last reference and we cannot iput it
		 * under inode_lock. So we keep the reference and iput
		 * it later.
		 */
		iput(old_inode);
		old_inode = inode;

		filemap_fdatawait(mapping);

		cond_resched();

		spin_lock(&inode_lock);
	}
	spin_unlock(&inode_lock);
	iput(old_inode);
}

/**
 * writeback_inodes_sb	-	writeback dirty inodes from given super_block
 * @sb: the superblock
 *
 * Start writeback on some inodes on this super_block. No guarantees are made
 * on how many (if any) will be written, and this function does not wait
 * for IO completion of submitted IO. The number of pages submitted is
 * returned.
 */
void writeback_inodes_sb(struct super_block *sb)
{
	unsigned long nr_dirty = global_page_state(NR_FILE_DIRTY);
	unsigned long nr_unstable = global_page_state(NR_UNSTABLE_NFS);
	long nr_to_write;

	nr_to_write = nr_dirty + nr_unstable +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);

	bdi_start_writeback(sb->s_bdi, sb, nr_to_write);
}
EXPORT_SYMBOL(writeback_inodes_sb);

/**
 * writeback_inodes_sb_if_idle	-	start writeback if none underway
 * @sb: the superblock
 *
 * Invoke writeback_inodes_sb if no writeback is currently underway.
 * Returns 1 if writeback was started, 0 if not.
 */
int writeback_inodes_sb_if_idle(struct super_block *sb)
{
	if (!writeback_in_progress(sb->s_bdi)) {
		writeback_inodes_sb(sb);
		return 1;
	} else
		return 0;
}
EXPORT_SYMBOL(writeback_inodes_sb_if_idle);

/**
 * sync_inodes_sb	-	sync sb inode pages
 * @sb: the superblock
 *
 * This function writes and waits on any dirty inode belonging to this
 * super_block. The number of pages synced is returned.
 */
void sync_inodes_sb(struct super_block *sb)
{
	bdi_sync_writeback(sb->s_bdi, sb);
	wait_sb_inodes(sb);
}
EXPORT_SYMBOL(sync_inodes_sb);

/**
 * write_inode_now	-	write an inode to disk
 * @inode: inode to write to disk
 * @sync: whether the write should be synchronous or not
 *
 * This function commits an inode to disk immediately if it is dirty. This is
 * primarily needed by knfsd.
 *
 * The caller must either have a ref on the inode or must have set I_WILL_FREE.
 */
int write_inode_now(struct inode *inode, int sync)
{
	int ret;
	struct writeback_control wbc = {
		.nr_to_write = LONG_MAX,
		.sync_mode = sync ? WB_SYNC_ALL : WB_SYNC_NONE,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};

	if (!mapping_cap_writeback_dirty(inode->i_mapping))
		wbc.nr_to_write = 0;

	might_sleep();
	spin_lock(&inode_lock);
	ret = writeback_single_inode(inode, &wbc);
	spin_unlock(&inode_lock);
	if (sync)
		inode_sync_wait(inode);
	return ret;
}
EXPORT_SYMBOL(write_inode_now);

/**
 * sync_inode - write an inode and its pages to disk.
 * @inode: the inode to sync
 * @wbc: controls the writeback mode
 *
 * sync_inode() will write an inode and its pages to disk.  It will also
 * correctly update the inode on its superblock's dirty inode lists and will
 * update inode->i_state.
 *
 * The caller must have a ref on the inode.
 */
int sync_inode(struct inode *inode, struct writeback_control *wbc)
{
	int ret;

	spin_lock(&inode_lock);
	ret = writeback_single_inode(inode, wbc);
	spin_unlock(&inode_lock);
	return ret;
}
EXPORT_SYMBOL(sync_inode);
