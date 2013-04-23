/*
 * (C) 1997 Linus Torvalds
 * (C) 1999 Andrea Arcangeli <andrea@suse.de> (dynamic inode allocation)
 */
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/hash.h>
#include <linux/swap.h>
#include <linux/security.h>
#include <linux/cdev.h>
#include <linux/bootmem.h>
#include <linux/fsnotify.h>
#include <linux/mount.h>
#include <linux/posix_acl.h>
#include <linux/prefetch.h>
#include <linux/buffer_head.h> /* for inode_has_buffers */
#include <linux/ratelimit.h>
#include "internal.h"

/*
 * Inode locking rules:
 *
 * inode->i_lock protects:
 *   inode->i_state, inode->i_hash, __iget()
 * inode->i_sb->s_inode_lru_lock protects:
 *   inode->i_sb->s_inode_lru, inode->i_lru
 * inode_sb_list_lock protects:
 *   sb->s_inodes, inode->i_sb_list
 * bdi->wb.list_lock protects:
 *   bdi->wb.b_{dirty,io,more_io}, inode->i_wb_list
 * inode_hash_lock protects:
 *   inode_hashtable, inode->i_hash
 *
 * Lock ordering:
 *
 * inode_sb_list_lock
 *   inode->i_lock
 *     inode->i_sb->s_inode_lru_lock
 *
 * bdi->wb.list_lock
 *   inode->i_lock
 *
 * inode_hash_lock
 *   inode_sb_list_lock
 *   inode->i_lock
 *
 * iunique_lock
 *   inode_hash_lock
 */

static unsigned int i_hash_mask __read_mostly;
static unsigned int i_hash_shift __read_mostly;
static struct hlist_head *inode_hashtable __read_mostly;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(inode_hash_lock);

__cacheline_aligned_in_smp DEFINE_SPINLOCK(inode_sb_list_lock);

/*
 * Empty aops. Can be used for the cases where the user does not
 * define any of the address_space operations.
 */
const struct address_space_operations empty_aops = {
};
EXPORT_SYMBOL(empty_aops);

/*
 * Statistics gathering..
 */
struct inodes_stat_t inodes_stat;

static DEFINE_PER_CPU(unsigned int, nr_inodes);
static DEFINE_PER_CPU(unsigned int, nr_unused);

static struct kmem_cache *inode_cachep __read_mostly;

static int get_nr_inodes(void)
{
	int i;
	int sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_inodes, i);
	return sum < 0 ? 0 : sum;
}

static inline int get_nr_inodes_unused(void)
{
	int i;
	int sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_unused, i);
	return sum < 0 ? 0 : sum;
}

int get_nr_dirty_inodes(void)
{
	/* not actually dirty inodes, but a wild approximation */
	int nr_dirty = get_nr_inodes() - get_nr_inodes_unused();
	return nr_dirty > 0 ? nr_dirty : 0;
}

/*
 * Handle nr_inode sysctl
 */
#ifdef CONFIG_SYSCTL
int proc_nr_inodes(ctl_table *table, int write,
		   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	inodes_stat.nr_inodes = get_nr_inodes();
	inodes_stat.nr_unused = get_nr_inodes_unused();
	return proc_dointvec(table, write, buffer, lenp, ppos);
}
#endif

/**
 * inode_init_always - perform inode structure intialisation
 * @sb: superblock inode belongs to
 * @inode: inode to initialise
 *
 * These are initializations that need to be done on every inode
 * allocation as the fields are not initialised by slab allocation.
 */
int inode_init_always(struct super_block *sb, struct inode *inode)
{
	static const struct inode_operations empty_iops;
	static const struct file_operations empty_fops;
	struct address_space *const mapping = &inode->i_data;

	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags = 0;
	atomic_set(&inode->i_count, 1);
	inode->i_op = &empty_iops;
	inode->i_fop = &empty_fops;
	inode->__i_nlink = 1;
	inode->i_opflags = 0;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode->i_bytes = 0;
	inode->i_generation = 0;
#ifdef CONFIG_QUOTA
	memset(&inode->i_dquot, 0, sizeof(inode->i_dquot));
#endif
	inode->i_pipe = NULL;
	inode->i_bdev = NULL;
	inode->i_cdev = NULL;
	inode->i_rdev = 0;
	inode->dirtied_when = 0;

	if (security_inode_alloc(inode))
		goto out;
	spin_lock_init(&inode->i_lock);
	lockdep_set_class(&inode->i_lock, &sb->s_type->i_lock_key);

	mutex_init(&inode->i_mutex);
	lockdep_set_class(&inode->i_mutex, &sb->s_type->i_mutex_key);

	atomic_set(&inode->i_dio_count, 0);

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
	mapping->flags = 0;
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->private_data = NULL;
	mapping->backing_dev_info = &default_backing_dev_info;
	mapping->writeback_index = 0;

	/*
	 * If the block_device provides a backing_dev_info for client
	 * inodes then use that.  Otherwise the inode share the bdev's
	 * backing_dev_info.
	 */
	if (sb->s_bdev) {
		struct backing_dev_info *bdi;

		bdi = sb->s_bdev->bd_inode->i_mapping->backing_dev_info;
		mapping->backing_dev_info = bdi;
	}
	inode->i_private = NULL;
	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);	/* buggered by rcu freeing */
#ifdef CONFIG_FS_POSIX_ACL
	inode->i_acl = inode->i_default_acl = ACL_NOT_CACHED;
#endif

#ifdef CONFIG_FSNOTIFY
	inode->i_fsnotify_mask = 0;
#endif

	this_cpu_inc(nr_inodes);

	return 0;
out:
	return -ENOMEM;
}
EXPORT_SYMBOL(inode_init_always);

static struct inode *alloc_inode(struct super_block *sb)
{
	struct inode *inode;

	if (sb->s_op->alloc_inode)
		inode = sb->s_op->alloc_inode(sb);
	else
		inode = kmem_cache_alloc(inode_cachep, GFP_KERNEL);

	if (!inode)
		return NULL;

	if (unlikely(inode_init_always(sb, inode))) {
		if (inode->i_sb->s_op->destroy_inode)
			inode->i_sb->s_op->destroy_inode(inode);
		else
			kmem_cache_free(inode_cachep, inode);
		return NULL;
	}

	return inode;
}

void free_inode_nonrcu(struct inode *inode)
{
	kmem_cache_free(inode_cachep, inode);
}
EXPORT_SYMBOL(free_inode_nonrcu);

void __destroy_inode(struct inode *inode)
{
	BUG_ON(inode_has_buffers(inode));
	security_inode_free(inode);
	fsnotify_inode_delete(inode);
	if (!inode->i_nlink) {
		WARN_ON(atomic_long_read(&inode->i_sb->s_remove_count) == 0);
		atomic_long_dec(&inode->i_sb->s_remove_count);
	}

#ifdef CONFIG_FS_POSIX_ACL
	if (inode->i_acl && inode->i_acl != ACL_NOT_CACHED)
		posix_acl_release(inode->i_acl);
	if (inode->i_default_acl && inode->i_default_acl != ACL_NOT_CACHED)
		posix_acl_release(inode->i_default_acl);
#endif
	this_cpu_dec(nr_inodes);
}
EXPORT_SYMBOL(__destroy_inode);

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(inode_cachep, inode);
}

static void destroy_inode(struct inode *inode)
{
	BUG_ON(!list_empty(&inode->i_lru));
	__destroy_inode(inode);
	if (inode->i_sb->s_op->destroy_inode)
		inode->i_sb->s_op->destroy_inode(inode);
	else
		call_rcu(&inode->i_rcu, i_callback);
}

/**
 * drop_nlink - directly drop an inode's link count
 * @inode: inode
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  In cases
 * where we are attempting to track writes to the
 * filesystem, a decrement to zero means an imminent
 * write when the file is truncated and actually unlinked
 * on the filesystem.
 */
void drop_nlink(struct inode *inode)
{
	WARN_ON(inode->i_nlink == 0);
	inode->__i_nlink--;
	if (!inode->i_nlink)
		atomic_long_inc(&inode->i_sb->s_remove_count);
}
EXPORT_SYMBOL(drop_nlink);

/**
 * clear_nlink - directly zero an inode's link count
 * @inode: inode
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  See
 * drop_nlink() for why we care about i_nlink hitting zero.
 */
void clear_nlink(struct inode *inode)
{
	if (inode->i_nlink) {
		inode->__i_nlink = 0;
		atomic_long_inc(&inode->i_sb->s_remove_count);
	}
}
EXPORT_SYMBOL(clear_nlink);

/**
 * set_nlink - directly set an inode's link count
 * @inode: inode
 * @nlink: new nlink (should be non-zero)
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.
 */
void set_nlink(struct inode *inode, unsigned int nlink)
{
	if (!nlink) {
		clear_nlink(inode);
	} else {
		/* Yes, some filesystems do change nlink from zero to one */
		if (inode->i_nlink == 0)
			atomic_long_dec(&inode->i_sb->s_remove_count);

		inode->__i_nlink = nlink;
	}
}
EXPORT_SYMBOL(set_nlink);

/**
 * inc_nlink - directly increment an inode's link count
 * @inode: inode
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  Currently,
 * it is only here for parity with dec_nlink().
 */
void inc_nlink(struct inode *inode)
{
	if (WARN_ON(inode->i_nlink == 0))
		atomic_long_dec(&inode->i_sb->s_remove_count);

	inode->__i_nlink++;
}
EXPORT_SYMBOL(inc_nlink);

void address_space_init_once(struct address_space *mapping)
{
	memset(mapping, 0, sizeof(*mapping));
	INIT_RADIX_TREE(&mapping->page_tree, GFP_ATOMIC);
	spin_lock_init(&mapping->tree_lock);
	mutex_init(&mapping->i_mmap_mutex);
	INIT_LIST_HEAD(&mapping->private_list);
	spin_lock_init(&mapping->private_lock);
	mapping->i_mmap = RB_ROOT;
	INIT_LIST_HEAD(&mapping->i_mmap_nonlinear);
}
EXPORT_SYMBOL(address_space_init_once);

/*
 * These are initializations that only need to be done
 * once, because the fields are idempotent across use
 * of the inode, so let the slab aware of that.
 */
void inode_init_once(struct inode *inode)
{
	memset(inode, 0, sizeof(*inode));
	INIT_HLIST_NODE(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_devices);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	address_space_init_once(&inode->i_data);
	i_size_ordered_init(inode);
#ifdef CONFIG_FSNOTIFY
	INIT_HLIST_HEAD(&inode->i_fsnotify_marks);
#endif
}
EXPORT_SYMBOL(inode_init_once);

static void init_once(void *foo)
{
	struct inode *inode = (struct inode *) foo;

	inode_init_once(inode);
}

/*
 * inode->i_lock must be held
 */
void __iget(struct inode *inode)
{
	atomic_inc(&inode->i_count);
}

/*
 * get additional reference to inode; caller must already hold one.
 */
void ihold(struct inode *inode)
{
	WARN_ON(atomic_inc_return(&inode->i_count) < 2);
}
EXPORT_SYMBOL(ihold);

static void inode_lru_list_add(struct inode *inode)
{
	spin_lock(&inode->i_sb->s_inode_lru_lock);
	if (list_empty(&inode->i_lru)) {
		list_add(&inode->i_lru, &inode->i_sb->s_inode_lru);
		inode->i_sb->s_nr_inodes_unused++;
		this_cpu_inc(nr_unused);
	}
	spin_unlock(&inode->i_sb->s_inode_lru_lock);
}

/*
 * Add inode to LRU if needed (inode is unused and clean).
 *
 * Needs inode->i_lock held.
 */
void inode_add_lru(struct inode *inode)
{
	if (!(inode->i_state & (I_DIRTY | I_SYNC | I_FREEING | I_WILL_FREE)) &&
	    !atomic_read(&inode->i_count) && inode->i_sb->s_flags & MS_ACTIVE)
		inode_lru_list_add(inode);
}


static void inode_lru_list_del(struct inode *inode)
{
	spin_lock(&inode->i_sb->s_inode_lru_lock);
	if (!list_empty(&inode->i_lru)) {
		list_del_init(&inode->i_lru);
		inode->i_sb->s_nr_inodes_unused--;
		this_cpu_dec(nr_unused);
	}
	spin_unlock(&inode->i_sb->s_inode_lru_lock);
}

/**
 * inode_sb_list_add - add inode to the superblock list of inodes
 * @inode: inode to add
 */
void inode_sb_list_add(struct inode *inode)
{
	spin_lock(&inode_sb_list_lock);
	list_add(&inode->i_sb_list, &inode->i_sb->s_inodes);
	spin_unlock(&inode_sb_list_lock);
}
EXPORT_SYMBOL_GPL(inode_sb_list_add);

static inline void inode_sb_list_del(struct inode *inode)
{
	if (!list_empty(&inode->i_sb_list)) {
		spin_lock(&inode_sb_list_lock);
		list_del_init(&inode->i_sb_list);
		spin_unlock(&inode_sb_list_lock);
	}
}

static unsigned long hash(struct super_block *sb, unsigned long hashval)
{
	unsigned long tmp;

	tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_PRIME + hashval) /
			L1_CACHE_BYTES;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> i_hash_shift);
	return tmp & i_hash_mask;
}

/**
 *	__insert_inode_hash - hash an inode
 *	@inode: unhashed inode
 *	@hashval: unsigned long value used to locate this object in the
 *		inode_hashtable.
 *
 *	Add an inode to the inode hash for this superblock.
 */
void __insert_inode_hash(struct inode *inode, unsigned long hashval)
{
	struct hlist_head *b = inode_hashtable + hash(inode->i_sb, hashval);

	spin_lock(&inode_hash_lock);
	spin_lock(&inode->i_lock);
	hlist_add_head(&inode->i_hash, b);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
}
EXPORT_SYMBOL(__insert_inode_hash);

/**
 *	__remove_inode_hash - remove an inode from the hash
 *	@inode: inode to unhash
 *
 *	Remove an inode from the superblock.
 */
void __remove_inode_hash(struct inode *inode)
{
	spin_lock(&inode_hash_lock);
	spin_lock(&inode->i_lock);
	hlist_del_init(&inode->i_hash);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
}
EXPORT_SYMBOL(__remove_inode_hash);

void clear_inode(struct inode *inode)
{
	might_sleep();
	/*
	 * We have to cycle tree_lock here because reclaim can be still in the
	 * process of removing the last page (in __delete_from_page_cache())
	 * and we must not free mapping under it.
	 */
	spin_lock_irq(&inode->i_data.tree_lock);
	BUG_ON(inode->i_data.nrpages);
	spin_unlock_irq(&inode->i_data.tree_lock);
	BUG_ON(!list_empty(&inode->i_data.private_list));
	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(inode->i_state & I_CLEAR);
	/* don't need i_lock here, no concurrent mods to i_state */
	inode->i_state = I_FREEING | I_CLEAR;
}
EXPORT_SYMBOL(clear_inode);

/*
 * Free the inode passed in, removing it from the lists it is still connected
 * to. We remove any pages still attached to the inode and wait for any IO that
 * is still in progress before finally destroying the inode.
 *
 * An inode must already be marked I_FREEING so that we avoid the inode being
 * moved back onto lists if we race with other code that manipulates the lists
 * (e.g. writeback_single_inode). The caller is responsible for setting this.
 *
 * An inode must already be removed from the LRU list before being evicted from
 * the cache. This should occur atomically with setting the I_FREEING state
 * flag, so no inodes here should ever be on the LRU when being evicted.
 */
static void evict(struct inode *inode)
{
	const struct super_operations *op = inode->i_sb->s_op;

	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(!list_empty(&inode->i_lru));

	if (!list_empty(&inode->i_wb_list))
		inode_wb_list_del(inode);

	inode_sb_list_del(inode);

	/*
	 * Wait for flusher thread to be done with the inode so that filesystem
	 * does not start destroying it while writeback is still running. Since
	 * the inode has I_FREEING set, flusher thread won't start new work on
	 * the inode.  We just have to wait for running writeback to finish.
	 */
	inode_wait_for_writeback(inode);

	if (op->evict_inode) {
		op->evict_inode(inode);
	} else {
		if (inode->i_data.nrpages)
			truncate_inode_pages(&inode->i_data, 0);
		clear_inode(inode);
	}
	if (S_ISBLK(inode->i_mode) && inode->i_bdev)
		bd_forget(inode);
	if (S_ISCHR(inode->i_mode) && inode->i_cdev)
		cd_forget(inode);

	remove_inode_hash(inode);

	spin_lock(&inode->i_lock);
	wake_up_bit(&inode->i_state, __I_NEW);
	BUG_ON(inode->i_state != (I_FREEING | I_CLEAR));
	spin_unlock(&inode->i_lock);

	destroy_inode(inode);
}

/*
 * dispose_list - dispose of the contents of a local list
 * @head: the head of the list to free
 *
 * Dispose-list gets a local list with local inodes in it, so it doesn't
 * need to worry about list corruption and SMP locks.
 */
static void dispose_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct inode *inode;

		inode = list_first_entry(head, struct inode, i_lru);
		list_del_init(&inode->i_lru);

		evict(inode);
	}
}

/**
 * evict_inodes	- evict all evictable inodes for a superblock
 * @sb:		superblock to operate on
 *
 * Make sure that no inodes with zero refcount are retained.  This is
 * called by superblock shutdown after having MS_ACTIVE flag removed,
 * so any inode reaching zero refcount during or after that call will
 * be immediately evicted.
 */
void evict_inodes(struct super_block *sb)
{
	struct inode *inode, *next;
	LIST_HEAD(dispose);

	spin_lock(&inode_sb_list_lock);
	list_for_each_entry_safe(inode, next, &sb->s_inodes, i_sb_list) {
		if (atomic_read(&inode->i_count))
			continue;

		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		inode->i_state |= I_FREEING;
		inode_lru_list_del(inode);
		spin_unlock(&inode->i_lock);
		list_add(&inode->i_lru, &dispose);
	}
	spin_unlock(&inode_sb_list_lock);

	dispose_list(&dispose);
}

/**
 * invalidate_inodes	- attempt to free all inodes on a superblock
 * @sb:		superblock to operate on
 * @kill_dirty: flag to guide handling of dirty inodes
 *
 * Attempts to free all inodes for a given superblock.  If there were any
 * busy inodes return a non-zero value, else zero.
 * If @kill_dirty is set, discard dirty inodes too, otherwise treat
 * them as busy.
 */
int invalidate_inodes(struct super_block *sb, bool kill_dirty)
{
	int busy = 0;
	struct inode *inode, *next;
	LIST_HEAD(dispose);

	spin_lock(&inode_sb_list_lock);
	list_for_each_entry_safe(inode, next, &sb->s_inodes, i_sb_list) {
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (inode->i_state & I_DIRTY && !kill_dirty) {
			spin_unlock(&inode->i_lock);
			busy = 1;
			continue;
		}
		if (atomic_read(&inode->i_count)) {
			spin_unlock(&inode->i_lock);
			busy = 1;
			continue;
		}

		inode->i_state |= I_FREEING;
		inode_lru_list_del(inode);
		spin_unlock(&inode->i_lock);
		list_add(&inode->i_lru, &dispose);
	}
	spin_unlock(&inode_sb_list_lock);

	dispose_list(&dispose);

	return busy;
}

static int can_unuse(struct inode *inode)
{
	if (inode->i_state & ~I_REFERENCED)
		return 0;
	if (inode_has_buffers(inode))
		return 0;
	if (atomic_read(&inode->i_count))
		return 0;
	if (inode->i_data.nrpages)
		return 0;
	return 1;
}

/*
 * Walk the superblock inode LRU for freeable inodes and attempt to free them.
 * This is called from the superblock shrinker function with a number of inodes
 * to trim from the LRU. Inodes to be freed are moved to a temporary list and
 * then are freed outside inode_lock by dispose_list().
 *
 * Any inodes which are pinned purely because of attached pagecache have their
 * pagecache removed.  If the inode has metadata buffers attached to
 * mapping->private_list then try to remove them.
 *
 * If the inode has the I_REFERENCED flag set, then it means that it has been
 * used recently - the flag is set in iput_final(). When we encounter such an
 * inode, clear the flag and move it to the back of the LRU so it gets another
 * pass through the LRU before it gets reclaimed. This is necessary because of
 * the fact we are doing lazy LRU updates to minimise lock contention so the
 * LRU does not have strict ordering. Hence we don't want to reclaim inodes
 * with this flag set because they are the inodes that are out of order.
 */
void prune_icache_sb(struct super_block *sb, int nr_to_scan)
{
	LIST_HEAD(freeable);
	int nr_scanned;
	unsigned long reap = 0;

	spin_lock(&sb->s_inode_lru_lock);
	for (nr_scanned = nr_to_scan; nr_scanned >= 0; nr_scanned--) {
		struct inode *inode;

		if (list_empty(&sb->s_inode_lru))
			break;

		inode = list_entry(sb->s_inode_lru.prev, struct inode, i_lru);

		/*
		 * we are inverting the sb->s_inode_lru_lock/inode->i_lock here,
		 * so use a trylock. If we fail to get the lock, just move the
		 * inode to the back of the list so we don't spin on it.
		 */
		if (!spin_trylock(&inode->i_lock)) {
			list_move(&inode->i_lru, &sb->s_inode_lru);
			continue;
		}

		/*
		 * Referenced or dirty inodes are still in use. Give them
		 * another pass through the LRU as we canot reclaim them now.
		 */
		if (atomic_read(&inode->i_count) ||
		    (inode->i_state & ~I_REFERENCED)) {
			list_del_init(&inode->i_lru);
			spin_unlock(&inode->i_lock);
			sb->s_nr_inodes_unused--;
			this_cpu_dec(nr_unused);
			continue;
		}

		/* recently referenced inodes get one more pass */
		if (inode->i_state & I_REFERENCED) {
			inode->i_state &= ~I_REFERENCED;
			list_move(&inode->i_lru, &sb->s_inode_lru);
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (inode_has_buffers(inode) || inode->i_data.nrpages) {
			__iget(inode);
			spin_unlock(&inode->i_lock);
			spin_unlock(&sb->s_inode_lru_lock);
			if (remove_inode_buffers(inode))
				reap += invalidate_mapping_pages(&inode->i_data,
								0, -1);
			iput(inode);
			spin_lock(&sb->s_inode_lru_lock);

			if (inode != list_entry(sb->s_inode_lru.next,
						struct inode, i_lru))
				continue;	/* wrong inode or list_empty */
			/* avoid lock inversions with trylock */
			if (!spin_trylock(&inode->i_lock))
				continue;
			if (!can_unuse(inode)) {
				spin_unlock(&inode->i_lock);
				continue;
			}
		}
		WARN_ON(inode->i_state & I_NEW);
		inode->i_state |= I_FREEING;
		spin_unlock(&inode->i_lock);

		list_move(&inode->i_lru, &freeable);
		sb->s_nr_inodes_unused--;
		this_cpu_dec(nr_unused);
	}
	if (current_is_kswapd())
		__count_vm_events(KSWAPD_INODESTEAL, reap);
	else
		__count_vm_events(PGINODESTEAL, reap);
	spin_unlock(&sb->s_inode_lru_lock);
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += reap;

	dispose_list(&freeable);
}

static void __wait_on_freeing_inode(struct inode *inode);
/*
 * Called with the inode lock held.
 */
static struct inode *find_inode(struct super_block *sb,
				struct hlist_head *head,
				int (*test)(struct inode *, void *),
				void *data)
{
	struct inode *inode = NULL;

repeat:
	hlist_for_each_entry(inode, head, i_hash) {
		spin_lock(&inode->i_lock);
		if (inode->i_sb != sb) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (!test(inode, data)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (inode->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_inode(inode);
			goto repeat;
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		return inode;
	}
	return NULL;
}

/*
 * find_inode_fast is the fast path version of find_inode, see the comment at
 * iget_locked for details.
 */
static struct inode *find_inode_fast(struct super_block *sb,
				struct hlist_head *head, unsigned long ino)
{
	struct inode *inode = NULL;

repeat:
	hlist_for_each_entry(inode, head, i_hash) {
		spin_lock(&inode->i_lock);
		if (inode->i_ino != ino) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (inode->i_sb != sb) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (inode->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_inode(inode);
			goto repeat;
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		return inode;
	}
	return NULL;
}

/*
 * Each cpu owns a range of LAST_INO_BATCH numbers.
 * 'shared_last_ino' is dirtied only once out of LAST_INO_BATCH allocations,
 * to renew the exhausted range.
 *
 * This does not significantly increase overflow rate because every CPU can
 * consume at most LAST_INO_BATCH-1 unused inode numbers. So there is
 * NR_CPUS*(LAST_INO_BATCH-1) wastage. At 4096 and 1024, this is ~0.1% of the
 * 2^32 range, and is a worst-case. Even a 50% wastage would only increase
 * overflow rate by 2x, which does not seem too significant.
 *
 * On a 32bit, non LFS stat() call, glibc will generate an EOVERFLOW
 * error if st_ino won't fit in target struct field. Use 32bit counter
 * here to attempt to avoid that.
 */
#define LAST_INO_BATCH 1024
static DEFINE_PER_CPU(unsigned int, last_ino);

unsigned int get_next_ino(void)
{
	unsigned int *p = &get_cpu_var(last_ino);
	unsigned int res = *p;

#ifdef CONFIG_SMP
	if (unlikely((res & (LAST_INO_BATCH-1)) == 0)) {
		static atomic_t shared_last_ino;
		int next = atomic_add_return(LAST_INO_BATCH, &shared_last_ino);

		res = next - LAST_INO_BATCH;
	}
#endif

	*p = ++res;
	put_cpu_var(last_ino);
	return res;
}
EXPORT_SYMBOL(get_next_ino);

/**
 *	new_inode_pseudo 	- obtain an inode
 *	@sb: superblock
 *
 *	Allocates a new inode for given superblock.
 *	Inode wont be chained in superblock s_inodes list
 *	This means :
 *	- fs can't be unmount
 *	- quotas, fsnotify, writeback can't work
 */
struct inode *new_inode_pseudo(struct super_block *sb)
{
	struct inode *inode = alloc_inode(sb);

	if (inode) {
		spin_lock(&inode->i_lock);
		inode->i_state = 0;
		spin_unlock(&inode->i_lock);
		INIT_LIST_HEAD(&inode->i_sb_list);
	}
	return inode;
}

/**
 *	new_inode 	- obtain an inode
 *	@sb: superblock
 *
 *	Allocates a new inode for given superblock. The default gfp_mask
 *	for allocations related to inode->i_mapping is GFP_HIGHUSER_MOVABLE.
 *	If HIGHMEM pages are unsuitable or it is known that pages allocated
 *	for the page cache are not reclaimable or migratable,
 *	mapping_set_gfp_mask() must be called with suitable flags on the
 *	newly created inode's mapping
 *
 */
struct inode *new_inode(struct super_block *sb)
{
	struct inode *inode;

	spin_lock_prefetch(&inode_sb_list_lock);

	inode = new_inode_pseudo(sb);
	if (inode)
		inode_sb_list_add(inode);
	return inode;
}
EXPORT_SYMBOL(new_inode);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void lockdep_annotate_inode_mutex_key(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode)) {
		struct file_system_type *type = inode->i_sb->s_type;

		/* Set new key only if filesystem hasn't already changed it */
		if (lockdep_match_class(&inode->i_mutex, &type->i_mutex_key)) {
			/*
			 * ensure nobody is actually holding i_mutex
			 */
			mutex_destroy(&inode->i_mutex);
			mutex_init(&inode->i_mutex);
			lockdep_set_class(&inode->i_mutex,
					  &type->i_mutex_dir_key);
		}
	}
}
EXPORT_SYMBOL(lockdep_annotate_inode_mutex_key);
#endif

/**
 * unlock_new_inode - clear the I_NEW state and wake up any waiters
 * @inode:	new inode to unlock
 *
 * Called when the inode is fully initialised to clear the new state of the
 * inode and wake up anyone waiting for the inode to finish initialisation.
 */
void unlock_new_inode(struct inode *inode)
{
	lockdep_annotate_inode_mutex_key(inode);
	spin_lock(&inode->i_lock);
	WARN_ON(!(inode->i_state & I_NEW));
	inode->i_state &= ~I_NEW;
	smp_mb();
	wake_up_bit(&inode->i_state, __I_NEW);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(unlock_new_inode);

/**
 * iget5_locked - obtain an inode from a mounted file system
 * @sb:		super block of file system
 * @hashval:	hash value (usually inode number) to get
 * @test:	callback used for comparisons between inodes
 * @set:	callback used to initialize a new struct inode
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the inode specified by @hashval and @data in the inode cache,
 * and if present it is return it with an increased reference count. This is
 * a generalized version of iget_locked() for file systems where the inode
 * number is not sufficient for unique identification of an inode.
 *
 * If the inode is not in cache, allocate a new inode and return it locked,
 * hashed, and with the I_NEW flag set. The file system gets to fill it in
 * before unlocking it via unlock_new_inode().
 *
 * Note both @test and @set are called with the inode_hash_lock held, so can't
 * sleep.
 */
struct inode *iget5_locked(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *),
		int (*set)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);
	struct inode *inode;

	spin_lock(&inode_hash_lock);
	inode = find_inode(sb, head, test, data);
	spin_unlock(&inode_hash_lock);

	if (inode) {
		wait_on_inode(inode);
		return inode;
	}

	inode = alloc_inode(sb);
	if (inode) {
		struct inode *old;

		spin_lock(&inode_hash_lock);
		/* We released the lock, so.. */
		old = find_inode(sb, head, test, data);
		if (!old) {
			if (set(inode, data))
				goto set_failed;

			spin_lock(&inode->i_lock);
			inode->i_state = I_NEW;
			hlist_add_head(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			inode_sb_list_add(inode);
			spin_unlock(&inode_hash_lock);

			/* Return the locked inode with I_NEW set, the
			 * caller is responsible for filling in the contents
			 */
			return inode;
		}

		/*
		 * Uhhuh, somebody else created the same inode under
		 * us. Use the old inode instead of the one we just
		 * allocated.
		 */
		spin_unlock(&inode_hash_lock);
		destroy_inode(inode);
		inode = old;
		wait_on_inode(inode);
	}
	return inode;

set_failed:
	spin_unlock(&inode_hash_lock);
	destroy_inode(inode);
	return NULL;
}
EXPORT_SYMBOL(iget5_locked);

/**
 * iget_locked - obtain an inode from a mounted file system
 * @sb:		super block of file system
 * @ino:	inode number to get
 *
 * Search for the inode specified by @ino in the inode cache and if present
 * return it with an increased reference count. This is for file systems
 * where the inode number is sufficient for unique identification of an inode.
 *
 * If the inode is not in cache, allocate a new inode and return it locked,
 * hashed, and with the I_NEW flag set.  The file system gets to fill it in
 * before unlocking it via unlock_new_inode().
 */
struct inode *iget_locked(struct super_block *sb, unsigned long ino)
{
	struct hlist_head *head = inode_hashtable + hash(sb, ino);
	struct inode *inode;

	spin_lock(&inode_hash_lock);
	inode = find_inode_fast(sb, head, ino);
	spin_unlock(&inode_hash_lock);
	if (inode) {
		wait_on_inode(inode);
		return inode;
	}

	inode = alloc_inode(sb);
	if (inode) {
		struct inode *old;

		spin_lock(&inode_hash_lock);
		/* We released the lock, so.. */
		old = find_inode_fast(sb, head, ino);
		if (!old) {
			inode->i_ino = ino;
			spin_lock(&inode->i_lock);
			inode->i_state = I_NEW;
			hlist_add_head(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			inode_sb_list_add(inode);
			spin_unlock(&inode_hash_lock);

			/* Return the locked inode with I_NEW set, the
			 * caller is responsible for filling in the contents
			 */
			return inode;
		}

		/*
		 * Uhhuh, somebody else created the same inode under
		 * us. Use the old inode instead of the one we just
		 * allocated.
		 */
		spin_unlock(&inode_hash_lock);
		destroy_inode(inode);
		inode = old;
		wait_on_inode(inode);
	}
	return inode;
}
EXPORT_SYMBOL(iget_locked);

/*
 * search the inode cache for a matching inode number.
 * If we find one, then the inode number we are trying to
 * allocate is not unique and so we should not use it.
 *
 * Returns 1 if the inode number is unique, 0 if it is not.
 */
static int test_inode_iunique(struct super_block *sb, unsigned long ino)
{
	struct hlist_head *b = inode_hashtable + hash(sb, ino);
	struct inode *inode;

	spin_lock(&inode_hash_lock);
	hlist_for_each_entry(inode, b, i_hash) {
		if (inode->i_ino == ino && inode->i_sb == sb) {
			spin_unlock(&inode_hash_lock);
			return 0;
		}
	}
	spin_unlock(&inode_hash_lock);

	return 1;
}

/**
 *	iunique - get a unique inode number
 *	@sb: superblock
 *	@max_reserved: highest reserved inode number
 *
 *	Obtain an inode number that is unique on the system for a given
 *	superblock. This is used by file systems that have no natural
 *	permanent inode numbering system. An inode number is returned that
 *	is higher than the reserved limit but unique.
 *
 *	BUGS:
 *	With a large number of inodes live on the file system this function
 *	currently becomes quite slow.
 */
ino_t iunique(struct super_block *sb, ino_t max_reserved)
{
	/*
	 * On a 32bit, non LFS stat() call, glibc will generate an EOVERFLOW
	 * error if st_ino won't fit in target struct field. Use 32bit counter
	 * here to attempt to avoid that.
	 */
	static DEFINE_SPINLOCK(iunique_lock);
	static unsigned int counter;
	ino_t res;

	spin_lock(&iunique_lock);
	do {
		if (counter <= max_reserved)
			counter = max_reserved + 1;
		res = counter++;
	} while (!test_inode_iunique(sb, res));
	spin_unlock(&iunique_lock);

	return res;
}
EXPORT_SYMBOL(iunique);

struct inode *igrab(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	if (!(inode->i_state & (I_FREEING|I_WILL_FREE))) {
		__iget(inode);
		spin_unlock(&inode->i_lock);
	} else {
		spin_unlock(&inode->i_lock);
		/*
		 * Handle the case where s_op->clear_inode is not been
		 * called yet, and somebody is calling igrab
		 * while the inode is getting freed.
		 */
		inode = NULL;
	}
	return inode;
}
EXPORT_SYMBOL(igrab);

/**
 * ilookup5_nowait - search for an inode in the inode cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually inode number) to search for
 * @test:	callback used for comparisons between inodes
 * @data:	opaque data pointer to pass to @test
 *
 * Search for the inode specified by @hashval and @data in the inode cache.
 * If the inode is in the cache, the inode is returned with an incremented
 * reference count.
 *
 * Note: I_NEW is not waited upon so you have to be very careful what you do
 * with the returned inode.  You probably should be using ilookup5() instead.
 *
 * Note2: @test is called with the inode_hash_lock held, so can't sleep.
 */
struct inode *ilookup5_nowait(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);
	struct inode *inode;

	spin_lock(&inode_hash_lock);
	inode = find_inode(sb, head, test, data);
	spin_unlock(&inode_hash_lock);

	return inode;
}
EXPORT_SYMBOL(ilookup5_nowait);

/**
 * ilookup5 - search for an inode in the inode cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually inode number) to search for
 * @test:	callback used for comparisons between inodes
 * @data:	opaque data pointer to pass to @test
 *
 * Search for the inode specified by @hashval and @data in the inode cache,
 * and if the inode is in the cache, return the inode with an incremented
 * reference count.  Waits on I_NEW before returning the inode.
 * returned with an incremented reference count.
 *
 * This is a generalized version of ilookup() for file systems where the
 * inode number is not sufficient for unique identification of an inode.
 *
 * Note: @test is called with the inode_hash_lock held, so can't sleep.
 */
struct inode *ilookup5(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data)
{
	struct inode *inode = ilookup5_nowait(sb, hashval, test, data);

	if (inode)
		wait_on_inode(inode);
	return inode;
}
EXPORT_SYMBOL(ilookup5);

/**
 * ilookup - search for an inode in the inode cache
 * @sb:		super block of file system to search
 * @ino:	inode number to search for
 *
 * Search for the inode @ino in the inode cache, and if the inode is in the
 * cache, the inode is returned with an incremented reference count.
 */
struct inode *ilookup(struct super_block *sb, unsigned long ino)
{
	struct hlist_head *head = inode_hashtable + hash(sb, ino);
	struct inode *inode;

	spin_lock(&inode_hash_lock);
	inode = find_inode_fast(sb, head, ino);
	spin_unlock(&inode_hash_lock);

	if (inode)
		wait_on_inode(inode);
	return inode;
}
EXPORT_SYMBOL(ilookup);

int insert_inode_locked(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	ino_t ino = inode->i_ino;
	struct hlist_head *head = inode_hashtable + hash(sb, ino);

	while (1) {
		struct inode *old = NULL;
		spin_lock(&inode_hash_lock);
		hlist_for_each_entry(old, head, i_hash) {
			if (old->i_ino != ino)
				continue;
			if (old->i_sb != sb)
				continue;
			spin_lock(&old->i_lock);
			if (old->i_state & (I_FREEING|I_WILL_FREE)) {
				spin_unlock(&old->i_lock);
				continue;
			}
			break;
		}
		if (likely(!old)) {
			spin_lock(&inode->i_lock);
			inode->i_state |= I_NEW;
			hlist_add_head(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			spin_unlock(&inode_hash_lock);
			return 0;
		}
		__iget(old);
		spin_unlock(&old->i_lock);
		spin_unlock(&inode_hash_lock);
		wait_on_inode(old);
		if (unlikely(!inode_unhashed(old))) {
			iput(old);
			return -EBUSY;
		}
		iput(old);
	}
}
EXPORT_SYMBOL(insert_inode_locked);

int insert_inode_locked4(struct inode *inode, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data)
{
	struct super_block *sb = inode->i_sb;
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);

	while (1) {
		struct inode *old = NULL;

		spin_lock(&inode_hash_lock);
		hlist_for_each_entry(old, head, i_hash) {
			if (old->i_sb != sb)
				continue;
			if (!test(old, data))
				continue;
			spin_lock(&old->i_lock);
			if (old->i_state & (I_FREEING|I_WILL_FREE)) {
				spin_unlock(&old->i_lock);
				continue;
			}
			break;
		}
		if (likely(!old)) {
			spin_lock(&inode->i_lock);
			inode->i_state |= I_NEW;
			hlist_add_head(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			spin_unlock(&inode_hash_lock);
			return 0;
		}
		__iget(old);
		spin_unlock(&old->i_lock);
		spin_unlock(&inode_hash_lock);
		wait_on_inode(old);
		if (unlikely(!inode_unhashed(old))) {
			iput(old);
			return -EBUSY;
		}
		iput(old);
	}
}
EXPORT_SYMBOL(insert_inode_locked4);


int generic_delete_inode(struct inode *inode)
{
	return 1;
}
EXPORT_SYMBOL(generic_delete_inode);

/*
 * Called when we're dropping the last reference
 * to an inode.
 *
 * Call the FS "drop_inode()" function, defaulting to
 * the legacy UNIX filesystem behaviour.  If it tells
 * us to evict inode, do so.  Otherwise, retain inode
 * in cache if fs is alive, sync and evict if fs is
 * shutting down.
 */
static void iput_final(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	const struct super_operations *op = inode->i_sb->s_op;
	int drop;

	WARN_ON(inode->i_state & I_NEW);

	if (op->drop_inode)
		drop = op->drop_inode(inode);
	else
		drop = generic_drop_inode(inode);

	if (!drop && (sb->s_flags & MS_ACTIVE)) {
		inode->i_state |= I_REFERENCED;
		inode_add_lru(inode);
		spin_unlock(&inode->i_lock);
		return;
	}

	if (!drop) {
		inode->i_state |= I_WILL_FREE;
		spin_unlock(&inode->i_lock);
		write_inode_now(inode, 1);
		spin_lock(&inode->i_lock);
		WARN_ON(inode->i_state & I_NEW);
		inode->i_state &= ~I_WILL_FREE;
	}

	inode->i_state |= I_FREEING;
	if (!list_empty(&inode->i_lru))
		inode_lru_list_del(inode);
	spin_unlock(&inode->i_lock);

	evict(inode);
}

/**
 *	iput	- put an inode
 *	@inode: inode to put
 *
 *	Puts an inode, dropping its usage count. If the inode use count hits
 *	zero, the inode is then freed and may also be destroyed.
 *
 *	Consequently, iput() can sleep.
 */
void iput(struct inode *inode)
{
	if (inode) {
		BUG_ON(inode->i_state & I_CLEAR);

		if (atomic_dec_and_lock(&inode->i_count, &inode->i_lock))
			iput_final(inode);
	}
}
EXPORT_SYMBOL(iput);

/**
 *	bmap	- find a block number in a file
 *	@inode: inode of file
 *	@block: block to find
 *
 *	Returns the block number on the device holding the inode that
 *	is the disk block number for the block of the file requested.
 *	That is, asked for block 4 of inode 1 the function will return the
 *	disk block relative to the disk start that holds that block of the
 *	file.
 */
sector_t bmap(struct inode *inode, sector_t block)
{
	sector_t res = 0;
	if (inode->i_mapping->a_ops->bmap)
		res = inode->i_mapping->a_ops->bmap(inode->i_mapping, block);
	return res;
}
EXPORT_SYMBOL(bmap);

/*
 * With relative atime, only update atime if the previous atime is
 * earlier than either the ctime or mtime or if at least a day has
 * passed since the last atime update.
 */
static int relatime_need_update(struct vfsmount *mnt, struct inode *inode,
			     struct timespec now)
{

	if (!(mnt->mnt_flags & MNT_RELATIME))
		return 1;
	/*
	 * Is mtime younger than atime? If yes, update atime:
	 */
	if (timespec_compare(&inode->i_mtime, &inode->i_atime) >= 0)
		return 1;
	/*
	 * Is ctime younger than atime? If yes, update atime:
	 */
	if (timespec_compare(&inode->i_ctime, &inode->i_atime) >= 0)
		return 1;

	/*
	 * Is the previous atime value older than a day? If yes,
	 * update atime:
	 */
	if ((long)(now.tv_sec - inode->i_atime.tv_sec) >= 24*60*60)
		return 1;
	/*
	 * Good, we can skip the atime update:
	 */
	return 0;
}

/*
 * This does the actual work of updating an inodes time or version.  Must have
 * had called mnt_want_write() before calling this.
 */
static int update_time(struct inode *inode, struct timespec *time, int flags)
{
	if (inode->i_op->update_time)
		return inode->i_op->update_time(inode, time, flags);

	if (flags & S_ATIME)
		inode->i_atime = *time;
	if (flags & S_VERSION)
		inode_inc_iversion(inode);
	if (flags & S_CTIME)
		inode->i_ctime = *time;
	if (flags & S_MTIME)
		inode->i_mtime = *time;
	mark_inode_dirty_sync(inode);
	return 0;
}

/**
 *	touch_atime	-	update the access time
 *	@path: the &struct path to update
 *
 *	Update the accessed time on an inode and mark it for writeback.
 *	This function automatically handles read only file systems and media,
 *	as well as the "noatime" flag and inode specific "noatime" markers.
 */
void touch_atime(struct path *path)
{
	struct vfsmount *mnt = path->mnt;
	struct inode *inode = path->dentry->d_inode;
	struct timespec now;

	if (inode->i_flags & S_NOATIME)
		return;
	if (IS_NOATIME(inode))
		return;
	if ((inode->i_sb->s_flags & MS_NODIRATIME) && S_ISDIR(inode->i_mode))
		return;

	if (mnt->mnt_flags & MNT_NOATIME)
		return;
	if ((mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode))
		return;

	now = current_fs_time(inode->i_sb);

	if (!relatime_need_update(mnt, inode, now))
		return;

	if (timespec_equal(&inode->i_atime, &now))
		return;

	if (!sb_start_write_trylock(inode->i_sb))
		return;

	if (__mnt_want_write(mnt))
		goto skip_update;
	/*
	 * File systems can error out when updating inodes if they need to
	 * allocate new space to modify an inode (such is the case for
	 * Btrfs), but since we touch atime while walking down the path we
	 * really don't care if we failed to update the atime of the file,
	 * so just ignore the return value.
	 * We may also fail on filesystems that have the ability to make parts
	 * of the fs read only, e.g. subvolumes in Btrfs.
	 */
	update_time(inode, &now, S_ATIME);
	__mnt_drop_write(mnt);
skip_update:
	sb_end_write(inode->i_sb);
}
EXPORT_SYMBOL(touch_atime);

/*
 * The logic we want is
 *
 *	if suid or (sgid and xgrp)
 *		remove privs
 */
int should_remove_suid(struct dentry *dentry)
{
	umode_t mode = dentry->d_inode->i_mode;
	int kill = 0;

	/* suid always must be killed */
	if (unlikely(mode & S_ISUID))
		kill = ATTR_KILL_SUID;

	/*
	 * sgid without any exec bits is just a mandatory locking mark; leave
	 * it alone.  If some exec bits are set, it's a real sgid; kill it.
	 */
	if (unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
		kill |= ATTR_KILL_SGID;

	if (unlikely(kill && !capable(CAP_FSETID) && S_ISREG(mode)))
		return kill;

	return 0;
}
EXPORT_SYMBOL(should_remove_suid);

static int __remove_suid(struct dentry *dentry, int kill)
{
	struct iattr newattrs;

	newattrs.ia_valid = ATTR_FORCE | kill;
	return notify_change(dentry, &newattrs);
}

int file_remove_suid(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	int killsuid;
	int killpriv;
	int error = 0;

	/* Fast path for nothing security related */
	if (IS_NOSEC(inode))
		return 0;

	killsuid = should_remove_suid(dentry);
	killpriv = security_inode_need_killpriv(dentry);

	if (killpriv < 0)
		return killpriv;
	if (killpriv)
		error = security_inode_killpriv(dentry);
	if (!error && killsuid)
		error = __remove_suid(dentry, killsuid);
	if (!error && (inode->i_sb->s_flags & MS_NOSEC))
		inode->i_flags |= S_NOSEC;

	return error;
}
EXPORT_SYMBOL(file_remove_suid);

/**
 *	file_update_time	-	update mtime and ctime time
 *	@file: file accessed
 *
 *	Update the mtime and ctime members of an inode and mark the inode
 *	for writeback.  Note that this function is meant exclusively for
 *	usage in the file write path of filesystems, and filesystems may
 *	choose to explicitly ignore update via this function with the
 *	S_NOCMTIME inode flag, e.g. for network filesystem where these
 *	timestamps are handled by the server.  This can return an error for
 *	file systems who need to allocate space in order to update an inode.
 */

int file_update_time(struct file *file)
{
	struct inode *inode = file_inode(file);
	struct timespec now;
	int sync_it = 0;
	int ret;

	/* First try to exhaust all avenues to not sync */
	if (IS_NOCMTIME(inode))
		return 0;

	now = current_fs_time(inode->i_sb);
	if (!timespec_equal(&inode->i_mtime, &now))
		sync_it = S_MTIME;

	if (!timespec_equal(&inode->i_ctime, &now))
		sync_it |= S_CTIME;

	if (IS_I_VERSION(inode))
		sync_it |= S_VERSION;

	if (!sync_it)
		return 0;

	/* Finally allowed to write? Takes lock. */
	if (__mnt_want_write_file(file))
		return 0;

	ret = update_time(inode, &now, sync_it);
	__mnt_drop_write_file(file);

	return ret;
}
EXPORT_SYMBOL(file_update_time);

int inode_needs_sync(struct inode *inode)
{
	if (IS_SYNC(inode))
		return 1;
	if (S_ISDIR(inode->i_mode) && IS_DIRSYNC(inode))
		return 1;
	return 0;
}
EXPORT_SYMBOL(inode_needs_sync);

int inode_wait(void *word)
{
	schedule();
	return 0;
}
EXPORT_SYMBOL(inode_wait);

/*
 * If we try to find an inode in the inode hash while it is being
 * deleted, we have to wait until the filesystem completes its
 * deletion before reporting that it isn't found.  This function waits
 * until the deletion _might_ have completed.  Callers are responsible
 * to recheck inode state.
 *
 * It doesn't matter if I_NEW is not set initially, a call to
 * wake_up_bit(&inode->i_state, __I_NEW) after removing from the hash list
 * will DTRT.
 */
static void __wait_on_freeing_inode(struct inode *inode)
{
	wait_queue_head_t *wq;
	DEFINE_WAIT_BIT(wait, &inode->i_state, __I_NEW);
	wq = bit_waitqueue(&inode->i_state, __I_NEW);
	prepare_to_wait(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
	schedule();
	finish_wait(wq, &wait.wait);
	spin_lock(&inode_hash_lock);
}

static __initdata unsigned long ihash_entries;
static int __init set_ihash_entries(char *str)
{
	if (!str)
		return 0;
	ihash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("ihash_entries=", set_ihash_entries);

/*
 * Initialize the waitqueues and inode hash table.
 */
void __init inode_init_early(void)
{
	unsigned int loop;

	/* If hashes are distributed across NUMA nodes, defer
	 * hash allocation until vmalloc space is available.
	 */
	if (hashdist)
		return;

	inode_hashtable =
		alloc_large_system_hash("Inode-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_EARLY,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);

	for (loop = 0; loop < (1U << i_hash_shift); loop++)
		INIT_HLIST_HEAD(&inode_hashtable[loop]);
}

void __init inode_init(void)
{
	unsigned int loop;

	/* inode slab cache */
	inode_cachep = kmem_cache_create("inode_cache",
					 sizeof(struct inode),
					 0,
					 (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					 SLAB_MEM_SPREAD),
					 init_once);

	/* Hash may have been set up in inode_init_early */
	if (!hashdist)
		return;

	inode_hashtable =
		alloc_large_system_hash("Inode-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					0,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);

	for (loop = 0; loop < (1U << i_hash_shift); loop++)
		INIT_HLIST_HEAD(&inode_hashtable[loop]);
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_mode = mode;
	if (S_ISCHR(mode)) {
		inode->i_fop = &def_chr_fops;
		inode->i_rdev = rdev;
	} else if (S_ISBLK(mode)) {
		inode->i_fop = &def_blk_fops;
		inode->i_rdev = rdev;
	} else if (S_ISFIFO(mode))
		inode->i_fop = &def_fifo_fops;
	else if (S_ISSOCK(mode))
		inode->i_fop = &bad_sock_fops;
	else
		printk(KERN_DEBUG "init_special_inode: bogus i_mode (%o) for"
				  " inode %s:%lu\n", mode, inode->i_sb->s_id,
				  inode->i_ino);
}
EXPORT_SYMBOL(init_special_inode);

/**
 * inode_init_owner - Init uid,gid,mode for new inode according to posix standards
 * @inode: New inode
 * @dir: Directory inode
 * @mode: mode of the new inode
 */
void inode_init_owner(struct inode *inode, const struct inode *dir,
			umode_t mode)
{
	inode->i_uid = current_fsuid();
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current_fsgid();
	inode->i_mode = mode;
}
EXPORT_SYMBOL(inode_init_owner);

/**
 * inode_owner_or_capable - check current task permissions to inode
 * @inode: inode being checked
 *
 * Return true if current either has CAP_FOWNER to the inode, or
 * owns the file.
 */
bool inode_owner_or_capable(const struct inode *inode)
{
	if (uid_eq(current_fsuid(), inode->i_uid))
		return true;
	if (inode_capable(inode, CAP_FOWNER))
		return true;
	return false;
}
EXPORT_SYMBOL(inode_owner_or_capable);

/*
 * Direct i/o helper functions
 */
static void __inode_dio_wait(struct inode *inode)
{
	wait_queue_head_t *wq = bit_waitqueue(&inode->i_state, __I_DIO_WAKEUP);
	DEFINE_WAIT_BIT(q, &inode->i_state, __I_DIO_WAKEUP);

	do {
		prepare_to_wait(wq, &q.wait, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&inode->i_dio_count))
			schedule();
	} while (atomic_read(&inode->i_dio_count));
	finish_wait(wq, &q.wait);
}

/**
 * inode_dio_wait - wait for outstanding DIO requests to finish
 * @inode: inode to wait for
 *
 * Waits for all pending direct I/O requests to finish so that we can
 * proceed with a truncate or equivalent operation.
 *
 * Must be called under a lock that serializes taking new references
 * to i_dio_count, usually by inode->i_mutex.
 */
void inode_dio_wait(struct inode *inode)
{
	if (atomic_read(&inode->i_dio_count))
		__inode_dio_wait(inode);
}
EXPORT_SYMBOL(inode_dio_wait);

/*
 * inode_dio_done - signal finish of a direct I/O requests
 * @inode: inode the direct I/O happens on
 *
 * This is called once we've finished processing a direct I/O request,
 * and is used to wake up callers waiting for direct I/O to be quiesced.
 */
void inode_dio_done(struct inode *inode)
{
	if (atomic_dec_and_test(&inode->i_dio_count))
		wake_up_bit(&inode->i_state, __I_DIO_WAKEUP);
}
EXPORT_SYMBOL(inode_dio_done);
