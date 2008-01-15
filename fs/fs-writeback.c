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
 * 10Apr2002	akpm@zip.com.au
 *		Split out of fs/inode.c
 *		Additions for address_space-based writeback
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include "internal.h"

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

	if (unlikely(block_dump)) {
		struct dentry *dentry = NULL;
		const char *name = "?";

		if (!list_empty(&inode->i_dentry)) {
			dentry = list_entry(inode->i_dentry.next,
					    struct dentry, d_alias);
			if (dentry && dentry->d_name.name)
				name = (const char *) dentry->d_name.name;
		}

		if (inode->i_ino || strcmp(inode->i_sb->s_id, "bdev"))
			printk(KERN_DEBUG
			       "%s(%d): dirtied inode %lu (%s) on %s\n",
			       current->comm, task_pid_nr(current), inode->i_ino,
			       name, inode->i_sb->s_id);
	}

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
		 * If the inode was already on s_dirty/s_io/s_more_io, don't
		 * reposition it (that would break s_dirty time-ordering).
		 */
		if (!was_dirty) {
			inode->dirtied_when = jiffies;
			list_move(&inode->i_list, &sb->s_dirty);
		}
	}
out:
	spin_unlock(&inode_lock);
}

EXPORT_SYMBOL(__mark_inode_dirty);

static int write_inode(struct inode *inode, int sync)
{
	if (inode->i_sb->s_op->write_inode && !is_bad_inode(inode))
		return inode->i_sb->s_op->write_inode(inode, sync);
	return 0;
}

/*
 * Redirty an inode: set its when-it-was dirtied timestamp and move it to the
 * furthest end of its superblock's dirty-inode list.
 *
 * Before stamping the inode's ->dirtied_when, we check to see whether it is
 * already the most-recently-dirtied inode on the s_dirty list.  If that is
 * the case then the inode must have been redirtied while it was being written
 * out and we don't reset its dirtied_when.
 */
static void redirty_tail(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (!list_empty(&sb->s_dirty)) {
		struct inode *tail_inode;

		tail_inode = list_entry(sb->s_dirty.next, struct inode, i_list);
		if (!time_after_eq(inode->dirtied_when,
				tail_inode->dirtied_when))
			inode->dirtied_when = jiffies;
	}
	list_move(&inode->i_list, &sb->s_dirty);
}

/*
 * requeue inode for re-scanning after sb->s_io list is exhausted.
 */
static void requeue_io(struct inode *inode)
{
	list_move(&inode->i_list, &inode->i_sb->s_more_io);
}

static void inode_sync_complete(struct inode *inode)
{
	/*
	 * Prevent speculative execution through spin_unlock(&inode_lock);
	 */
	smp_mb();
	wake_up_bit(&inode->i_state, __I_SYNC);
}

/*
 * Move expired dirty inodes from @delaying_queue to @dispatch_queue.
 */
static void move_expired_inodes(struct list_head *delaying_queue,
			       struct list_head *dispatch_queue,
				unsigned long *older_than_this)
{
	while (!list_empty(delaying_queue)) {
		struct inode *inode = list_entry(delaying_queue->prev,
						struct inode, i_list);
		if (older_than_this &&
			time_after(inode->dirtied_when, *older_than_this))
			break;
		list_move(&inode->i_list, dispatch_queue);
	}
}

/*
 * Queue all expired dirty inodes for io, eldest first.
 */
static void queue_io(struct super_block *sb,
				unsigned long *older_than_this)
{
	list_splice_init(&sb->s_more_io, sb->s_io.prev);
	move_expired_inodes(&sb->s_dirty, &sb->s_io, older_than_this);
}

int sb_has_dirty_inodes(struct super_block *sb)
{
	return !list_empty(&sb->s_dirty) ||
	       !list_empty(&sb->s_io) ||
	       !list_empty(&sb->s_more_io);
}
EXPORT_SYMBOL(sb_has_dirty_inodes);

/*
 * Write a single inode's dirty pages and inode data out to disk.
 * If `wait' is set, wait on the writeout.
 *
 * The whole writeout design is quite complex and fragile.  We want to avoid
 * starvation of particular inodes when others are being redirtied, prevent
 * livelocks, etc.
 *
 * Called under inode_lock.
 */
static int
__sync_single_inode(struct inode *inode, struct writeback_control *wbc)
{
	unsigned dirty;
	struct address_space *mapping = inode->i_mapping;
	int wait = wbc->sync_mode == WB_SYNC_ALL;
	int ret;

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
	if (!(inode->i_state & I_FREEING)) {
		if (!(inode->i_state & I_DIRTY) &&
		    mapping_tagged(mapping, PAGECACHE_TAG_DIRTY)) {
			/*
			 * We didn't write back all the pages.  nfs_writepages()
			 * sometimes bales out without doing anything. Redirty
			 * the inode; Move it from s_io onto s_more_io/s_dirty.
			 */
			/*
			 * akpm: if the caller was the kupdate function we put
			 * this inode at the head of s_dirty so it gets first
			 * consideration.  Otherwise, move it to the tail, for
			 * the reasons described there.  I'm not really sure
			 * how much sense this makes.  Presumably I had a good
			 * reasons for doing it this way, and I'd rather not
			 * muck with it at present.
			 */
			if (wbc->for_kupdate) {
				/*
				 * For the kupdate function we move the inode
				 * to s_more_io so it will get more writeout as
				 * soon as the queue becomes uncongested.
				 */
				inode->i_state |= I_DIRTY_PAGES;
				requeue_io(inode);
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
		} else if (inode->i_state & I_DIRTY) {
			/*
			 * Someone redirtied the inode while were writing back
			 * the pages.
			 */
			redirty_tail(inode);
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

/*
 * Write out an inode's dirty pages.  Called under inode_lock.  Either the
 * caller has ref on the inode (either via __iget or via syscall against an fd)
 * or the inode has I_WILL_FREE set (via generic_forget_inode)
 */
static int
__writeback_single_inode(struct inode *inode, struct writeback_control *wbc)
{
	wait_queue_head_t *wqh;

	if (!atomic_read(&inode->i_count))
		WARN_ON(!(inode->i_state & (I_WILL_FREE|I_FREEING)));
	else
		WARN_ON(inode->i_state & I_WILL_FREE);

	if ((wbc->sync_mode != WB_SYNC_ALL) && (inode->i_state & I_SYNC)) {
		struct address_space *mapping = inode->i_mapping;
		int ret;

		/*
		 * We're skipping this inode because it's locked, and we're not
		 * doing writeback-for-data-integrity.  Move it to s_more_io so
		 * that writeback can proceed with the other inodes on s_io.
		 * We'll have another go at writing back this inode when we
		 * completed a full scan of s_io.
		 */
		requeue_io(inode);

		/*
		 * Even if we don't actually write the inode itself here,
		 * we can at least start some of the data writeout..
		 */
		spin_unlock(&inode_lock);
		ret = do_writepages(mapping, wbc);
		spin_lock(&inode_lock);
		return ret;
	}

	/*
	 * It's a data-integrity sync.  We must wait.
	 */
	if (inode->i_state & I_SYNC) {
		DEFINE_WAIT_BIT(wq, &inode->i_state, __I_SYNC);

		wqh = bit_waitqueue(&inode->i_state, __I_SYNC);
		do {
			spin_unlock(&inode_lock);
			__wait_on_bit(wqh, &wq, inode_wait,
							TASK_UNINTERRUPTIBLE);
			spin_lock(&inode_lock);
		} while (inode->i_state & I_SYNC);
	}
	return __sync_single_inode(inode, wbc);
}

/*
 * Write out a superblock's list of dirty inodes.  A wait will be performed
 * upon no inodes, all inodes or the final one, depending upon sync_mode.
 *
 * If older_than_this is non-NULL, then only write out inodes which
 * had their first dirtying at a time earlier than *older_than_this.
 *
 * If we're a pdlfush thread, then implement pdflush collision avoidance
 * against the entire list.
 *
 * WB_SYNC_HOLD is a hack for sys_sync(): reattach the inode to sb->s_dirty so
 * that it can be located for waiting on in __writeback_single_inode().
 *
 * Called under inode_lock.
 *
 * If `bdi' is non-zero then we're being asked to writeback a specific queue.
 * This function assumes that the blockdev superblock's inodes are backed by
 * a variety of queues, so all inodes are searched.  For other superblocks,
 * assume that all inodes are backed by the same queue.
 *
 * FIXME: this linear search could get expensive with many fileystems.  But
 * how to fix?  We need to go from an address_space to all inodes which share
 * a queue with that address_space.  (Easy: have a global "dirty superblocks"
 * list).
 *
 * The inodes to be written are parked on sb->s_io.  They are moved back onto
 * sb->s_dirty as they are selected for writing.  This way, none can be missed
 * on the writer throttling path, and we get decent balancing between many
 * throttled threads: we don't want them all piling up on inode_sync_wait.
 */
static void
sync_sb_inodes(struct super_block *sb, struct writeback_control *wbc)
{
	const unsigned long start = jiffies;	/* livelock avoidance */

	if (!wbc->for_kupdate || list_empty(&sb->s_io))
		queue_io(sb, wbc->older_than_this);

	while (!list_empty(&sb->s_io)) {
		struct inode *inode = list_entry(sb->s_io.prev,
						struct inode, i_list);
		struct address_space *mapping = inode->i_mapping;
		struct backing_dev_info *bdi = mapping->backing_dev_info;
		long pages_skipped;

		if (!bdi_cap_writeback_dirty(bdi)) {
			redirty_tail(inode);
			if (sb_is_blkdev_sb(sb)) {
				/*
				 * Dirty memory-backed blockdev: the ramdisk
				 * driver does this.  Skip just this inode
				 */
				continue;
			}
			/*
			 * Dirty memory-backed inode against a filesystem other
			 * than the kernel-internal bdev filesystem.  Skip the
			 * entire superblock.
			 */
			break;
		}

		if (wbc->nonblocking && bdi_write_congested(bdi)) {
			wbc->encountered_congestion = 1;
			if (!sb_is_blkdev_sb(sb))
				break;		/* Skip a congested fs */
			requeue_io(inode);
			continue;		/* Skip a congested blockdev */
		}

		if (wbc->bdi && bdi != wbc->bdi) {
			if (!sb_is_blkdev_sb(sb))
				break;		/* fs has the wrong queue */
			requeue_io(inode);
			continue;		/* blockdev has wrong queue */
		}

		/* Was this inode dirtied after sync_sb_inodes was called? */
		if (time_after(inode->dirtied_when, start))
			break;

		/* Is another pdflush already flushing this queue? */
		if (current_is_pdflush() && !writeback_acquire(bdi))
			break;

		BUG_ON(inode->i_state & I_FREEING);
		__iget(inode);
		pages_skipped = wbc->pages_skipped;
		__writeback_single_inode(inode, wbc);
		if (wbc->sync_mode == WB_SYNC_HOLD) {
			inode->dirtied_when = jiffies;
			list_move(&inode->i_list, &sb->s_dirty);
		}
		if (current_is_pdflush())
			writeback_release(bdi);
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
		if (wbc->nr_to_write <= 0)
			break;
	}
	return;		/* Leave any unwritten inodes on s_io */
}

/*
 * Start writeback of dirty pagecache data against all unlocked inodes.
 *
 * Note:
 * We don't need to grab a reference to superblock here. If it has non-empty
 * ->s_dirty it's hadn't been killed yet and kill_super() won't proceed
 * past sync_inodes_sb() until the ->s_dirty/s_io/s_more_io lists are all
 * empty. Since __sync_single_inode() regains inode_lock before it finally moves
 * inode from superblock lists we are OK.
 *
 * If `older_than_this' is non-zero then only flush inodes which have a
 * flushtime older than *older_than_this.
 *
 * If `bdi' is non-zero then we will scan the first inode against each
 * superblock until we find the matching ones.  One group will be the dirty
 * inodes against a filesystem.  Then when we hit the dummy blockdev superblock,
 * sync_sb_inodes will seekout the blockdev which matches `bdi'.  Maybe not
 * super-efficient but we're about to do a ton of I/O...
 */
void
writeback_inodes(struct writeback_control *wbc)
{
	struct super_block *sb;

	might_sleep();
	spin_lock(&sb_lock);
restart:
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		if (sb_has_dirty_inodes(sb)) {
			/* we're making our own get_super here */
			sb->s_count++;
			spin_unlock(&sb_lock);
			/*
			 * If we can't get the readlock, there's no sense in
			 * waiting around, most of the time the FS is going to
			 * be unmounted by the time it is released.
			 */
			if (down_read_trylock(&sb->s_umount)) {
				if (sb->s_root) {
					spin_lock(&inode_lock);
					sync_sb_inodes(sb, wbc);
					spin_unlock(&inode_lock);
				}
				up_read(&sb->s_umount);
			}
			spin_lock(&sb_lock);
			if (__put_super_and_need_restart(sb))
				goto restart;
		}
		if (wbc->nr_to_write <= 0)
			break;
	}
	spin_unlock(&sb_lock);
}

/*
 * writeback and wait upon the filesystem's dirty inodes.  The caller will
 * do this in two passes - one to write, and one to wait.  WB_SYNC_HOLD is
 * used to park the written inodes on sb->s_dirty for the wait pass.
 *
 * A finite limit is set on the number of pages which will be written.
 * To prevent infinite livelock of sys_sync().
 *
 * We add in the number of potentially dirty inodes, because each inode write
 * can dirty pagecache in the underlying blockdev.
 */
void sync_inodes_sb(struct super_block *sb, int wait)
{
	struct writeback_control wbc = {
		.sync_mode	= wait ? WB_SYNC_ALL : WB_SYNC_HOLD,
		.range_start	= 0,
		.range_end	= LLONG_MAX,
	};
	unsigned long nr_dirty = global_page_state(NR_FILE_DIRTY);
	unsigned long nr_unstable = global_page_state(NR_UNSTABLE_NFS);

	wbc.nr_to_write = nr_dirty + nr_unstable +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused) +
			nr_dirty + nr_unstable;
	wbc.nr_to_write += wbc.nr_to_write / 2;		/* Bit more for luck */
	spin_lock(&inode_lock);
	sync_sb_inodes(sb, &wbc);
	spin_unlock(&inode_lock);
}

/*
 * Rather lame livelock avoidance.
 */
static void set_sb_syncing(int val)
{
	struct super_block *sb;
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		sb->s_syncing = val;
	}
	spin_unlock(&sb_lock);
}

/**
 * sync_inodes - writes all inodes to disk
 * @wait: wait for completion
 *
 * sync_inodes() goes through each super block's dirty inode list, writes the
 * inodes out, waits on the writeout and puts the inodes back on the normal
 * list.
 *
 * This is for sys_sync().  fsync_dev() uses the same algorithm.  The subtle
 * part of the sync functions is that the blockdev "superblock" is processed
 * last.  This is because the write_inode() function of a typical fs will
 * perform no I/O, but will mark buffers in the blockdev mapping as dirty.
 * What we want to do is to perform all that dirtying first, and then write
 * back all those inode blocks via the blockdev mapping in one sweep.  So the
 * additional (somewhat redundant) sync_blockdev() calls here are to make
 * sure that really happens.  Because if we call sync_inodes_sb(wait=1) with
 * outstanding dirty inodes, the writeback goes block-at-a-time within the
 * filesystem's write_inode().  This is extremely slow.
 */
static void __sync_inodes(int wait)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (sb->s_syncing)
			continue;
		sb->s_syncing = 1;
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (sb->s_root) {
			sync_inodes_sb(sb, wait);
			sync_blockdev(sb->s_bdev);
		}
		up_read(&sb->s_umount);
		spin_lock(&sb_lock);
		if (__put_super_and_need_restart(sb))
			goto restart;
	}
	spin_unlock(&sb_lock);
}

void sync_inodes(int wait)
{
	set_sb_syncing(0);
	__sync_inodes(0);

	if (wait) {
		set_sb_syncing(0);
		__sync_inodes(1);
	}
}

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
		.sync_mode = WB_SYNC_ALL,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};

	if (!mapping_cap_writeback_dirty(inode->i_mapping))
		wbc.nr_to_write = 0;

	might_sleep();
	spin_lock(&inode_lock);
	ret = __writeback_single_inode(inode, &wbc);
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
	ret = __writeback_single_inode(inode, wbc);
	spin_unlock(&inode_lock);
	return ret;
}
EXPORT_SYMBOL(sync_inode);

/**
 * generic_osync_inode - flush all dirty data for a given inode to disk
 * @inode: inode to write
 * @mapping: the address_space that should be flushed
 * @what:  what to write and wait upon
 *
 * This can be called by file_write functions for files which have the
 * O_SYNC flag set, to flush dirty writes to disk.
 *
 * @what is a bitmask, specifying which part of the inode's data should be
 * written and waited upon.
 *
 *    OSYNC_DATA:     i_mapping's dirty data
 *    OSYNC_METADATA: the buffers at i_mapping->private_list
 *    OSYNC_INODE:    the inode itself
 */

int generic_osync_inode(struct inode *inode, struct address_space *mapping, int what)
{
	int err = 0;
	int need_write_inode_now = 0;
	int err2;

	if (what & OSYNC_DATA)
		err = filemap_fdatawrite(mapping);
	if (what & (OSYNC_METADATA|OSYNC_DATA)) {
		err2 = sync_mapping_buffers(mapping);
		if (!err)
			err = err2;
	}
	if (what & OSYNC_DATA) {
		err2 = filemap_fdatawait(mapping);
		if (!err)
			err = err2;
	}

	spin_lock(&inode_lock);
	if ((inode->i_state & I_DIRTY) &&
	    ((what & OSYNC_INODE) || (inode->i_state & I_DIRTY_DATASYNC)))
		need_write_inode_now = 1;
	spin_unlock(&inode_lock);

	if (need_write_inode_now) {
		err2 = write_inode_now(inode, 1);
		if (!err)
			err = err2;
	}
	else
		inode_sync_wait(inode);

	return err;
}

EXPORT_SYMBOL(generic_osync_inode);

/**
 * writeback_acquire: attempt to get exclusive writeback access to a device
 * @bdi: the device's backing_dev_info structure
 *
 * It is a waste of resources to have more than one pdflush thread blocked on
 * a single request queue.  Exclusion at the request_queue level is obtained
 * via a flag in the request_queue's backing_dev_info.state.
 *
 * Non-request_queue-backed address_spaces will share default_backing_dev_info,
 * unless they implement their own.  Which is somewhat inefficient, as this
 * may prevent concurrent writeback against multiple devices.
 */
int writeback_acquire(struct backing_dev_info *bdi)
{
	return !test_and_set_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_in_progress: determine whether there is writeback in progress
 * @bdi: the device's backing_dev_info structure.
 *
 * Determine whether there is writeback in progress against a backing device.
 */
int writeback_in_progress(struct backing_dev_info *bdi)
{
	return test_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_release: relinquish exclusive writeback access against a device.
 * @bdi: the device's backing_dev_info structure
 */
void writeback_release(struct backing_dev_info *bdi)
{
	BUG_ON(!writeback_in_progress(bdi));
	clear_bit(BDI_pdflush, &bdi->state);
}
