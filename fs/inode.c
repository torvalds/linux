// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 1997 Linus Torvalds
 * (C) 1999 Andrea Arcangeli <andrea@suse.de> (dynamic iyesde allocation)
 */
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/hash.h>
#include <linux/swap.h>
#include <linux/security.h>
#include <linux/cdev.h>
#include <linux/memblock.h>
#include <linux/fsyestify.h>
#include <linux/mount.h>
#include <linux/posix_acl.h>
#include <linux/prefetch.h>
#include <linux/buffer_head.h> /* for iyesde_has_buffers */
#include <linux/ratelimit.h>
#include <linux/list_lru.h>
#include <linux/iversion.h>
#include <trace/events/writeback.h>
#include "internal.h"

/*
 * Iyesde locking rules:
 *
 * iyesde->i_lock protects:
 *   iyesde->i_state, iyesde->i_hash, __iget()
 * Iyesde LRU list locks protect:
 *   iyesde->i_sb->s_iyesde_lru, iyesde->i_lru
 * iyesde->i_sb->s_iyesde_list_lock protects:
 *   iyesde->i_sb->s_iyesdes, iyesde->i_sb_list
 * bdi->wb.list_lock protects:
 *   bdi->wb.b_{dirty,io,more_io,dirty_time}, iyesde->i_io_list
 * iyesde_hash_lock protects:
 *   iyesde_hashtable, iyesde->i_hash
 *
 * Lock ordering:
 *
 * iyesde->i_sb->s_iyesde_list_lock
 *   iyesde->i_lock
 *     Iyesde LRU list locks
 *
 * bdi->wb.list_lock
 *   iyesde->i_lock
 *
 * iyesde_hash_lock
 *   iyesde->i_sb->s_iyesde_list_lock
 *   iyesde->i_lock
 *
 * iunique_lock
 *   iyesde_hash_lock
 */

static unsigned int i_hash_mask __read_mostly;
static unsigned int i_hash_shift __read_mostly;
static struct hlist_head *iyesde_hashtable __read_mostly;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(iyesde_hash_lock);

/*
 * Empty aops. Can be used for the cases where the user does yest
 * define any of the address_space operations.
 */
const struct address_space_operations empty_aops = {
};
EXPORT_SYMBOL(empty_aops);

/*
 * Statistics gathering..
 */
struct iyesdes_stat_t iyesdes_stat;

static DEFINE_PER_CPU(unsigned long, nr_iyesdes);
static DEFINE_PER_CPU(unsigned long, nr_unused);

static struct kmem_cache *iyesde_cachep __read_mostly;

static long get_nr_iyesdes(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_iyesdes, i);
	return sum < 0 ? 0 : sum;
}

static inline long get_nr_iyesdes_unused(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_unused, i);
	return sum < 0 ? 0 : sum;
}

long get_nr_dirty_iyesdes(void)
{
	/* yest actually dirty iyesdes, but a wild approximation */
	long nr_dirty = get_nr_iyesdes() - get_nr_iyesdes_unused();
	return nr_dirty > 0 ? nr_dirty : 0;
}

/*
 * Handle nr_iyesde sysctl
 */
#ifdef CONFIG_SYSCTL
int proc_nr_iyesdes(struct ctl_table *table, int write,
		   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	iyesdes_stat.nr_iyesdes = get_nr_iyesdes();
	iyesdes_stat.nr_unused = get_nr_iyesdes_unused();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}
#endif

static int yes_open(struct iyesde *iyesde, struct file *file)
{
	return -ENXIO;
}

/**
 * iyesde_init_always - perform iyesde structure initialisation
 * @sb: superblock iyesde belongs to
 * @iyesde: iyesde to initialise
 *
 * These are initializations that need to be done on every iyesde
 * allocation as the fields are yest initialised by slab allocation.
 */
int iyesde_init_always(struct super_block *sb, struct iyesde *iyesde)
{
	static const struct iyesde_operations empty_iops;
	static const struct file_operations yes_open_fops = {.open = yes_open};
	struct address_space *const mapping = &iyesde->i_data;

	iyesde->i_sb = sb;
	iyesde->i_blkbits = sb->s_blocksize_bits;
	iyesde->i_flags = 0;
	atomic_set(&iyesde->i_count, 1);
	iyesde->i_op = &empty_iops;
	iyesde->i_fop = &yes_open_fops;
	iyesde->__i_nlink = 1;
	iyesde->i_opflags = 0;
	if (sb->s_xattr)
		iyesde->i_opflags |= IOP_XATTR;
	i_uid_write(iyesde, 0);
	i_gid_write(iyesde, 0);
	atomic_set(&iyesde->i_writecount, 0);
	iyesde->i_size = 0;
	iyesde->i_write_hint = WRITE_LIFE_NOT_SET;
	iyesde->i_blocks = 0;
	iyesde->i_bytes = 0;
	iyesde->i_generation = 0;
	iyesde->i_pipe = NULL;
	iyesde->i_bdev = NULL;
	iyesde->i_cdev = NULL;
	iyesde->i_link = NULL;
	iyesde->i_dir_seq = 0;
	iyesde->i_rdev = 0;
	iyesde->dirtied_when = 0;

#ifdef CONFIG_CGROUP_WRITEBACK
	iyesde->i_wb_frn_winner = 0;
	iyesde->i_wb_frn_avg_time = 0;
	iyesde->i_wb_frn_history = 0;
#endif

	if (security_iyesde_alloc(iyesde))
		goto out;
	spin_lock_init(&iyesde->i_lock);
	lockdep_set_class(&iyesde->i_lock, &sb->s_type->i_lock_key);

	init_rwsem(&iyesde->i_rwsem);
	lockdep_set_class(&iyesde->i_rwsem, &sb->s_type->i_mutex_key);

	atomic_set(&iyesde->i_dio_count, 0);

	mapping->a_ops = &empty_aops;
	mapping->host = iyesde;
	mapping->flags = 0;
	mapping->wb_err = 0;
	atomic_set(&mapping->i_mmap_writable, 0);
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	atomic_set(&mapping->nr_thps, 0);
#endif
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->private_data = NULL;
	mapping->writeback_index = 0;
	iyesde->i_private = NULL;
	iyesde->i_mapping = mapping;
	INIT_HLIST_HEAD(&iyesde->i_dentry);	/* buggered by rcu freeing */
#ifdef CONFIG_FS_POSIX_ACL
	iyesde->i_acl = iyesde->i_default_acl = ACL_NOT_CACHED;
#endif

#ifdef CONFIG_FSNOTIFY
	iyesde->i_fsyestify_mask = 0;
#endif
	iyesde->i_flctx = NULL;
	this_cpu_inc(nr_iyesdes);

	return 0;
out:
	return -ENOMEM;
}
EXPORT_SYMBOL(iyesde_init_always);

void free_iyesde_yesnrcu(struct iyesde *iyesde)
{
	kmem_cache_free(iyesde_cachep, iyesde);
}
EXPORT_SYMBOL(free_iyesde_yesnrcu);

static void i_callback(struct rcu_head *head)
{
	struct iyesde *iyesde = container_of(head, struct iyesde, i_rcu);
	if (iyesde->free_iyesde)
		iyesde->free_iyesde(iyesde);
	else
		free_iyesde_yesnrcu(iyesde);
}

static struct iyesde *alloc_iyesde(struct super_block *sb)
{
	const struct super_operations *ops = sb->s_op;
	struct iyesde *iyesde;

	if (ops->alloc_iyesde)
		iyesde = ops->alloc_iyesde(sb);
	else
		iyesde = kmem_cache_alloc(iyesde_cachep, GFP_KERNEL);

	if (!iyesde)
		return NULL;

	if (unlikely(iyesde_init_always(sb, iyesde))) {
		if (ops->destroy_iyesde) {
			ops->destroy_iyesde(iyesde);
			if (!ops->free_iyesde)
				return NULL;
		}
		iyesde->free_iyesde = ops->free_iyesde;
		i_callback(&iyesde->i_rcu);
		return NULL;
	}

	return iyesde;
}

void __destroy_iyesde(struct iyesde *iyesde)
{
	BUG_ON(iyesde_has_buffers(iyesde));
	iyesde_detach_wb(iyesde);
	security_iyesde_free(iyesde);
	fsyestify_iyesde_delete(iyesde);
	locks_free_lock_context(iyesde);
	if (!iyesde->i_nlink) {
		WARN_ON(atomic_long_read(&iyesde->i_sb->s_remove_count) == 0);
		atomic_long_dec(&iyesde->i_sb->s_remove_count);
	}

#ifdef CONFIG_FS_POSIX_ACL
	if (iyesde->i_acl && !is_uncached_acl(iyesde->i_acl))
		posix_acl_release(iyesde->i_acl);
	if (iyesde->i_default_acl && !is_uncached_acl(iyesde->i_default_acl))
		posix_acl_release(iyesde->i_default_acl);
#endif
	this_cpu_dec(nr_iyesdes);
}
EXPORT_SYMBOL(__destroy_iyesde);

static void destroy_iyesde(struct iyesde *iyesde)
{
	const struct super_operations *ops = iyesde->i_sb->s_op;

	BUG_ON(!list_empty(&iyesde->i_lru));
	__destroy_iyesde(iyesde);
	if (ops->destroy_iyesde) {
		ops->destroy_iyesde(iyesde);
		if (!ops->free_iyesde)
			return;
	}
	iyesde->free_iyesde = ops->free_iyesde;
	call_rcu(&iyesde->i_rcu, i_callback);
}

/**
 * drop_nlink - directly drop an iyesde's link count
 * @iyesde: iyesde
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  In cases
 * where we are attempting to track writes to the
 * filesystem, a decrement to zero means an imminent
 * write when the file is truncated and actually unlinked
 * on the filesystem.
 */
void drop_nlink(struct iyesde *iyesde)
{
	WARN_ON(iyesde->i_nlink == 0);
	iyesde->__i_nlink--;
	if (!iyesde->i_nlink)
		atomic_long_inc(&iyesde->i_sb->s_remove_count);
}
EXPORT_SYMBOL(drop_nlink);

/**
 * clear_nlink - directly zero an iyesde's link count
 * @iyesde: iyesde
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  See
 * drop_nlink() for why we care about i_nlink hitting zero.
 */
void clear_nlink(struct iyesde *iyesde)
{
	if (iyesde->i_nlink) {
		iyesde->__i_nlink = 0;
		atomic_long_inc(&iyesde->i_sb->s_remove_count);
	}
}
EXPORT_SYMBOL(clear_nlink);

/**
 * set_nlink - directly set an iyesde's link count
 * @iyesde: iyesde
 * @nlink: new nlink (should be yesn-zero)
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.
 */
void set_nlink(struct iyesde *iyesde, unsigned int nlink)
{
	if (!nlink) {
		clear_nlink(iyesde);
	} else {
		/* Yes, some filesystems do change nlink from zero to one */
		if (iyesde->i_nlink == 0)
			atomic_long_dec(&iyesde->i_sb->s_remove_count);

		iyesde->__i_nlink = nlink;
	}
}
EXPORT_SYMBOL(set_nlink);

/**
 * inc_nlink - directly increment an iyesde's link count
 * @iyesde: iyesde
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  Currently,
 * it is only here for parity with dec_nlink().
 */
void inc_nlink(struct iyesde *iyesde)
{
	if (unlikely(iyesde->i_nlink == 0)) {
		WARN_ON(!(iyesde->i_state & I_LINKABLE));
		atomic_long_dec(&iyesde->i_sb->s_remove_count);
	}

	iyesde->__i_nlink++;
}
EXPORT_SYMBOL(inc_nlink);

static void __address_space_init_once(struct address_space *mapping)
{
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
	init_rwsem(&mapping->i_mmap_rwsem);
	INIT_LIST_HEAD(&mapping->private_list);
	spin_lock_init(&mapping->private_lock);
	mapping->i_mmap = RB_ROOT_CACHED;
}

void address_space_init_once(struct address_space *mapping)
{
	memset(mapping, 0, sizeof(*mapping));
	__address_space_init_once(mapping);
}
EXPORT_SYMBOL(address_space_init_once);

/*
 * These are initializations that only need to be done
 * once, because the fields are idempotent across use
 * of the iyesde, so let the slab aware of that.
 */
void iyesde_init_once(struct iyesde *iyesde)
{
	memset(iyesde, 0, sizeof(*iyesde));
	INIT_HLIST_NODE(&iyesde->i_hash);
	INIT_LIST_HEAD(&iyesde->i_devices);
	INIT_LIST_HEAD(&iyesde->i_io_list);
	INIT_LIST_HEAD(&iyesde->i_wb_list);
	INIT_LIST_HEAD(&iyesde->i_lru);
	__address_space_init_once(&iyesde->i_data);
	i_size_ordered_init(iyesde);
}
EXPORT_SYMBOL(iyesde_init_once);

static void init_once(void *foo)
{
	struct iyesde *iyesde = (struct iyesde *) foo;

	iyesde_init_once(iyesde);
}

/*
 * iyesde->i_lock must be held
 */
void __iget(struct iyesde *iyesde)
{
	atomic_inc(&iyesde->i_count);
}

/*
 * get additional reference to iyesde; caller must already hold one.
 */
void ihold(struct iyesde *iyesde)
{
	WARN_ON(atomic_inc_return(&iyesde->i_count) < 2);
}
EXPORT_SYMBOL(ihold);

static void iyesde_lru_list_add(struct iyesde *iyesde)
{
	if (list_lru_add(&iyesde->i_sb->s_iyesde_lru, &iyesde->i_lru))
		this_cpu_inc(nr_unused);
	else
		iyesde->i_state |= I_REFERENCED;
}

/*
 * Add iyesde to LRU if needed (iyesde is unused and clean).
 *
 * Needs iyesde->i_lock held.
 */
void iyesde_add_lru(struct iyesde *iyesde)
{
	if (!(iyesde->i_state & (I_DIRTY_ALL | I_SYNC |
				I_FREEING | I_WILL_FREE)) &&
	    !atomic_read(&iyesde->i_count) && iyesde->i_sb->s_flags & SB_ACTIVE)
		iyesde_lru_list_add(iyesde);
}


static void iyesde_lru_list_del(struct iyesde *iyesde)
{

	if (list_lru_del(&iyesde->i_sb->s_iyesde_lru, &iyesde->i_lru))
		this_cpu_dec(nr_unused);
}

/**
 * iyesde_sb_list_add - add iyesde to the superblock list of iyesdes
 * @iyesde: iyesde to add
 */
void iyesde_sb_list_add(struct iyesde *iyesde)
{
	spin_lock(&iyesde->i_sb->s_iyesde_list_lock);
	list_add(&iyesde->i_sb_list, &iyesde->i_sb->s_iyesdes);
	spin_unlock(&iyesde->i_sb->s_iyesde_list_lock);
}
EXPORT_SYMBOL_GPL(iyesde_sb_list_add);

static inline void iyesde_sb_list_del(struct iyesde *iyesde)
{
	if (!list_empty(&iyesde->i_sb_list)) {
		spin_lock(&iyesde->i_sb->s_iyesde_list_lock);
		list_del_init(&iyesde->i_sb_list);
		spin_unlock(&iyesde->i_sb->s_iyesde_list_lock);
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
 *	__insert_iyesde_hash - hash an iyesde
 *	@iyesde: unhashed iyesde
 *	@hashval: unsigned long value used to locate this object in the
 *		iyesde_hashtable.
 *
 *	Add an iyesde to the iyesde hash for this superblock.
 */
void __insert_iyesde_hash(struct iyesde *iyesde, unsigned long hashval)
{
	struct hlist_head *b = iyesde_hashtable + hash(iyesde->i_sb, hashval);

	spin_lock(&iyesde_hash_lock);
	spin_lock(&iyesde->i_lock);
	hlist_add_head(&iyesde->i_hash, b);
	spin_unlock(&iyesde->i_lock);
	spin_unlock(&iyesde_hash_lock);
}
EXPORT_SYMBOL(__insert_iyesde_hash);

/**
 *	__remove_iyesde_hash - remove an iyesde from the hash
 *	@iyesde: iyesde to unhash
 *
 *	Remove an iyesde from the superblock.
 */
void __remove_iyesde_hash(struct iyesde *iyesde)
{
	spin_lock(&iyesde_hash_lock);
	spin_lock(&iyesde->i_lock);
	hlist_del_init(&iyesde->i_hash);
	spin_unlock(&iyesde->i_lock);
	spin_unlock(&iyesde_hash_lock);
}
EXPORT_SYMBOL(__remove_iyesde_hash);

void clear_iyesde(struct iyesde *iyesde)
{
	/*
	 * We have to cycle the i_pages lock here because reclaim can be in the
	 * process of removing the last page (in __delete_from_page_cache())
	 * and we must yest free the mapping under it.
	 */
	xa_lock_irq(&iyesde->i_data.i_pages);
	BUG_ON(iyesde->i_data.nrpages);
	BUG_ON(iyesde->i_data.nrexceptional);
	xa_unlock_irq(&iyesde->i_data.i_pages);
	BUG_ON(!list_empty(&iyesde->i_data.private_list));
	BUG_ON(!(iyesde->i_state & I_FREEING));
	BUG_ON(iyesde->i_state & I_CLEAR);
	BUG_ON(!list_empty(&iyesde->i_wb_list));
	/* don't need i_lock here, yes concurrent mods to i_state */
	iyesde->i_state = I_FREEING | I_CLEAR;
}
EXPORT_SYMBOL(clear_iyesde);

/*
 * Free the iyesde passed in, removing it from the lists it is still connected
 * to. We remove any pages still attached to the iyesde and wait for any IO that
 * is still in progress before finally destroying the iyesde.
 *
 * An iyesde must already be marked I_FREEING so that we avoid the iyesde being
 * moved back onto lists if we race with other code that manipulates the lists
 * (e.g. writeback_single_iyesde). The caller is responsible for setting this.
 *
 * An iyesde must already be removed from the LRU list before being evicted from
 * the cache. This should occur atomically with setting the I_FREEING state
 * flag, so yes iyesdes here should ever be on the LRU when being evicted.
 */
static void evict(struct iyesde *iyesde)
{
	const struct super_operations *op = iyesde->i_sb->s_op;

	BUG_ON(!(iyesde->i_state & I_FREEING));
	BUG_ON(!list_empty(&iyesde->i_lru));

	if (!list_empty(&iyesde->i_io_list))
		iyesde_io_list_del(iyesde);

	iyesde_sb_list_del(iyesde);

	/*
	 * Wait for flusher thread to be done with the iyesde so that filesystem
	 * does yest start destroying it while writeback is still running. Since
	 * the iyesde has I_FREEING set, flusher thread won't start new work on
	 * the iyesde.  We just have to wait for running writeback to finish.
	 */
	iyesde_wait_for_writeback(iyesde);

	if (op->evict_iyesde) {
		op->evict_iyesde(iyesde);
	} else {
		truncate_iyesde_pages_final(&iyesde->i_data);
		clear_iyesde(iyesde);
	}
	if (S_ISBLK(iyesde->i_mode) && iyesde->i_bdev)
		bd_forget(iyesde);
	if (S_ISCHR(iyesde->i_mode) && iyesde->i_cdev)
		cd_forget(iyesde);

	remove_iyesde_hash(iyesde);

	spin_lock(&iyesde->i_lock);
	wake_up_bit(&iyesde->i_state, __I_NEW);
	BUG_ON(iyesde->i_state != (I_FREEING | I_CLEAR));
	spin_unlock(&iyesde->i_lock);

	destroy_iyesde(iyesde);
}

/*
 * dispose_list - dispose of the contents of a local list
 * @head: the head of the list to free
 *
 * Dispose-list gets a local list with local iyesdes in it, so it doesn't
 * need to worry about list corruption and SMP locks.
 */
static void dispose_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct iyesde *iyesde;

		iyesde = list_first_entry(head, struct iyesde, i_lru);
		list_del_init(&iyesde->i_lru);

		evict(iyesde);
		cond_resched();
	}
}

/**
 * evict_iyesdes	- evict all evictable iyesdes for a superblock
 * @sb:		superblock to operate on
 *
 * Make sure that yes iyesdes with zero refcount are retained.  This is
 * called by superblock shutdown after having SB_ACTIVE flag removed,
 * so any iyesde reaching zero refcount during or after that call will
 * be immediately evicted.
 */
void evict_iyesdes(struct super_block *sb)
{
	struct iyesde *iyesde, *next;
	LIST_HEAD(dispose);

again:
	spin_lock(&sb->s_iyesde_list_lock);
	list_for_each_entry_safe(iyesde, next, &sb->s_iyesdes, i_sb_list) {
		if (atomic_read(&iyesde->i_count))
			continue;

		spin_lock(&iyesde->i_lock);
		if (iyesde->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&iyesde->i_lock);
			continue;
		}

		iyesde->i_state |= I_FREEING;
		iyesde_lru_list_del(iyesde);
		spin_unlock(&iyesde->i_lock);
		list_add(&iyesde->i_lru, &dispose);

		/*
		 * We can have a ton of iyesdes to evict at unmount time given
		 * eyesugh memory, check to see if we need to go to sleep for a
		 * bit so we don't livelock.
		 */
		if (need_resched()) {
			spin_unlock(&sb->s_iyesde_list_lock);
			cond_resched();
			dispose_list(&dispose);
			goto again;
		}
	}
	spin_unlock(&sb->s_iyesde_list_lock);

	dispose_list(&dispose);
}
EXPORT_SYMBOL_GPL(evict_iyesdes);

/**
 * invalidate_iyesdes	- attempt to free all iyesdes on a superblock
 * @sb:		superblock to operate on
 * @kill_dirty: flag to guide handling of dirty iyesdes
 *
 * Attempts to free all iyesdes for a given superblock.  If there were any
 * busy iyesdes return a yesn-zero value, else zero.
 * If @kill_dirty is set, discard dirty iyesdes too, otherwise treat
 * them as busy.
 */
int invalidate_iyesdes(struct super_block *sb, bool kill_dirty)
{
	int busy = 0;
	struct iyesde *iyesde, *next;
	LIST_HEAD(dispose);

again:
	spin_lock(&sb->s_iyesde_list_lock);
	list_for_each_entry_safe(iyesde, next, &sb->s_iyesdes, i_sb_list) {
		spin_lock(&iyesde->i_lock);
		if (iyesde->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&iyesde->i_lock);
			continue;
		}
		if (iyesde->i_state & I_DIRTY_ALL && !kill_dirty) {
			spin_unlock(&iyesde->i_lock);
			busy = 1;
			continue;
		}
		if (atomic_read(&iyesde->i_count)) {
			spin_unlock(&iyesde->i_lock);
			busy = 1;
			continue;
		}

		iyesde->i_state |= I_FREEING;
		iyesde_lru_list_del(iyesde);
		spin_unlock(&iyesde->i_lock);
		list_add(&iyesde->i_lru, &dispose);
		if (need_resched()) {
			spin_unlock(&sb->s_iyesde_list_lock);
			cond_resched();
			dispose_list(&dispose);
			goto again;
		}
	}
	spin_unlock(&sb->s_iyesde_list_lock);

	dispose_list(&dispose);

	return busy;
}

/*
 * Isolate the iyesde from the LRU in preparation for freeing it.
 *
 * Any iyesdes which are pinned purely because of attached pagecache have their
 * pagecache removed.  If the iyesde has metadata buffers attached to
 * mapping->private_list then try to remove them.
 *
 * If the iyesde has the I_REFERENCED flag set, then it means that it has been
 * used recently - the flag is set in iput_final(). When we encounter such an
 * iyesde, clear the flag and move it to the back of the LRU so it gets ayesther
 * pass through the LRU before it gets reclaimed. This is necessary because of
 * the fact we are doing lazy LRU updates to minimise lock contention so the
 * LRU does yest have strict ordering. Hence we don't want to reclaim iyesdes
 * with this flag set because they are the iyesdes that are out of order.
 */
static enum lru_status iyesde_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *freeable = arg;
	struct iyesde	*iyesde = container_of(item, struct iyesde, i_lru);

	/*
	 * we are inverting the lru lock/iyesde->i_lock here, so use a trylock.
	 * If we fail to get the lock, just skip it.
	 */
	if (!spin_trylock(&iyesde->i_lock))
		return LRU_SKIP;

	/*
	 * Referenced or dirty iyesdes are still in use. Give them ayesther pass
	 * through the LRU as we cayest reclaim them yesw.
	 */
	if (atomic_read(&iyesde->i_count) ||
	    (iyesde->i_state & ~I_REFERENCED)) {
		list_lru_isolate(lru, &iyesde->i_lru);
		spin_unlock(&iyesde->i_lock);
		this_cpu_dec(nr_unused);
		return LRU_REMOVED;
	}

	/* recently referenced iyesdes get one more pass */
	if (iyesde->i_state & I_REFERENCED) {
		iyesde->i_state &= ~I_REFERENCED;
		spin_unlock(&iyesde->i_lock);
		return LRU_ROTATE;
	}

	if (iyesde_has_buffers(iyesde) || iyesde->i_data.nrpages) {
		__iget(iyesde);
		spin_unlock(&iyesde->i_lock);
		spin_unlock(lru_lock);
		if (remove_iyesde_buffers(iyesde)) {
			unsigned long reap;
			reap = invalidate_mapping_pages(&iyesde->i_data, 0, -1);
			if (current_is_kswapd())
				__count_vm_events(KSWAPD_INODESTEAL, reap);
			else
				__count_vm_events(PGINODESTEAL, reap);
			if (current->reclaim_state)
				current->reclaim_state->reclaimed_slab += reap;
		}
		iput(iyesde);
		spin_lock(lru_lock);
		return LRU_RETRY;
	}

	WARN_ON(iyesde->i_state & I_NEW);
	iyesde->i_state |= I_FREEING;
	list_lru_isolate_move(lru, &iyesde->i_lru, freeable);
	spin_unlock(&iyesde->i_lock);

	this_cpu_dec(nr_unused);
	return LRU_REMOVED;
}

/*
 * Walk the superblock iyesde LRU for freeable iyesdes and attempt to free them.
 * This is called from the superblock shrinker function with a number of iyesdes
 * to trim from the LRU. Iyesdes to be freed are moved to a temporary list and
 * then are freed outside iyesde_lock by dispose_list().
 */
long prune_icache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(freeable);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_iyesde_lru, sc,
				     iyesde_lru_isolate, &freeable);
	dispose_list(&freeable);
	return freed;
}

static void __wait_on_freeing_iyesde(struct iyesde *iyesde);
/*
 * Called with the iyesde lock held.
 */
static struct iyesde *find_iyesde(struct super_block *sb,
				struct hlist_head *head,
				int (*test)(struct iyesde *, void *),
				void *data)
{
	struct iyesde *iyesde = NULL;

repeat:
	hlist_for_each_entry(iyesde, head, i_hash) {
		if (iyesde->i_sb != sb)
			continue;
		if (!test(iyesde, data))
			continue;
		spin_lock(&iyesde->i_lock);
		if (iyesde->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_iyesde(iyesde);
			goto repeat;
		}
		if (unlikely(iyesde->i_state & I_CREATING)) {
			spin_unlock(&iyesde->i_lock);
			return ERR_PTR(-ESTALE);
		}
		__iget(iyesde);
		spin_unlock(&iyesde->i_lock);
		return iyesde;
	}
	return NULL;
}

/*
 * find_iyesde_fast is the fast path version of find_iyesde, see the comment at
 * iget_locked for details.
 */
static struct iyesde *find_iyesde_fast(struct super_block *sb,
				struct hlist_head *head, unsigned long iyes)
{
	struct iyesde *iyesde = NULL;

repeat:
	hlist_for_each_entry(iyesde, head, i_hash) {
		if (iyesde->i_iyes != iyes)
			continue;
		if (iyesde->i_sb != sb)
			continue;
		spin_lock(&iyesde->i_lock);
		if (iyesde->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_iyesde(iyesde);
			goto repeat;
		}
		if (unlikely(iyesde->i_state & I_CREATING)) {
			spin_unlock(&iyesde->i_lock);
			return ERR_PTR(-ESTALE);
		}
		__iget(iyesde);
		spin_unlock(&iyesde->i_lock);
		return iyesde;
	}
	return NULL;
}

/*
 * Each cpu owns a range of LAST_INO_BATCH numbers.
 * 'shared_last_iyes' is dirtied only once out of LAST_INO_BATCH allocations,
 * to renew the exhausted range.
 *
 * This does yest significantly increase overflow rate because every CPU can
 * consume at most LAST_INO_BATCH-1 unused iyesde numbers. So there is
 * NR_CPUS*(LAST_INO_BATCH-1) wastage. At 4096 and 1024, this is ~0.1% of the
 * 2^32 range, and is a worst-case. Even a 50% wastage would only increase
 * overflow rate by 2x, which does yest seem too significant.
 *
 * On a 32bit, yesn LFS stat() call, glibc will generate an EOVERFLOW
 * error if st_iyes won't fit in target struct field. Use 32bit counter
 * here to attempt to avoid that.
 */
#define LAST_INO_BATCH 1024
static DEFINE_PER_CPU(unsigned int, last_iyes);

unsigned int get_next_iyes(void)
{
	unsigned int *p = &get_cpu_var(last_iyes);
	unsigned int res = *p;

#ifdef CONFIG_SMP
	if (unlikely((res & (LAST_INO_BATCH-1)) == 0)) {
		static atomic_t shared_last_iyes;
		int next = atomic_add_return(LAST_INO_BATCH, &shared_last_iyes);

		res = next - LAST_INO_BATCH;
	}
#endif

	res++;
	/* get_next_iyes should yest provide a 0 iyesde number */
	if (unlikely(!res))
		res++;
	*p = res;
	put_cpu_var(last_iyes);
	return res;
}
EXPORT_SYMBOL(get_next_iyes);

/**
 *	new_iyesde_pseudo 	- obtain an iyesde
 *	@sb: superblock
 *
 *	Allocates a new iyesde for given superblock.
 *	Iyesde wont be chained in superblock s_iyesdes list
 *	This means :
 *	- fs can't be unmount
 *	- quotas, fsyestify, writeback can't work
 */
struct iyesde *new_iyesde_pseudo(struct super_block *sb)
{
	struct iyesde *iyesde = alloc_iyesde(sb);

	if (iyesde) {
		spin_lock(&iyesde->i_lock);
		iyesde->i_state = 0;
		spin_unlock(&iyesde->i_lock);
		INIT_LIST_HEAD(&iyesde->i_sb_list);
	}
	return iyesde;
}

/**
 *	new_iyesde 	- obtain an iyesde
 *	@sb: superblock
 *
 *	Allocates a new iyesde for given superblock. The default gfp_mask
 *	for allocations related to iyesde->i_mapping is GFP_HIGHUSER_MOVABLE.
 *	If HIGHMEM pages are unsuitable or it is kyeswn that pages allocated
 *	for the page cache are yest reclaimable or migratable,
 *	mapping_set_gfp_mask() must be called with suitable flags on the
 *	newly created iyesde's mapping
 *
 */
struct iyesde *new_iyesde(struct super_block *sb)
{
	struct iyesde *iyesde;

	spin_lock_prefetch(&sb->s_iyesde_list_lock);

	iyesde = new_iyesde_pseudo(sb);
	if (iyesde)
		iyesde_sb_list_add(iyesde);
	return iyesde;
}
EXPORT_SYMBOL(new_iyesde);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void lockdep_anyestate_iyesde_mutex_key(struct iyesde *iyesde)
{
	if (S_ISDIR(iyesde->i_mode)) {
		struct file_system_type *type = iyesde->i_sb->s_type;

		/* Set new key only if filesystem hasn't already changed it */
		if (lockdep_match_class(&iyesde->i_rwsem, &type->i_mutex_key)) {
			/*
			 * ensure yesbody is actually holding i_mutex
			 */
			// mutex_destroy(&iyesde->i_mutex);
			init_rwsem(&iyesde->i_rwsem);
			lockdep_set_class(&iyesde->i_rwsem,
					  &type->i_mutex_dir_key);
		}
	}
}
EXPORT_SYMBOL(lockdep_anyestate_iyesde_mutex_key);
#endif

/**
 * unlock_new_iyesde - clear the I_NEW state and wake up any waiters
 * @iyesde:	new iyesde to unlock
 *
 * Called when the iyesde is fully initialised to clear the new state of the
 * iyesde and wake up anyone waiting for the iyesde to finish initialisation.
 */
void unlock_new_iyesde(struct iyesde *iyesde)
{
	lockdep_anyestate_iyesde_mutex_key(iyesde);
	spin_lock(&iyesde->i_lock);
	WARN_ON(!(iyesde->i_state & I_NEW));
	iyesde->i_state &= ~I_NEW & ~I_CREATING;
	smp_mb();
	wake_up_bit(&iyesde->i_state, __I_NEW);
	spin_unlock(&iyesde->i_lock);
}
EXPORT_SYMBOL(unlock_new_iyesde);

void discard_new_iyesde(struct iyesde *iyesde)
{
	lockdep_anyestate_iyesde_mutex_key(iyesde);
	spin_lock(&iyesde->i_lock);
	WARN_ON(!(iyesde->i_state & I_NEW));
	iyesde->i_state &= ~I_NEW;
	smp_mb();
	wake_up_bit(&iyesde->i_state, __I_NEW);
	spin_unlock(&iyesde->i_lock);
	iput(iyesde);
}
EXPORT_SYMBOL(discard_new_iyesde);

/**
 * lock_two_yesndirectories - take two i_mutexes on yesn-directory objects
 *
 * Lock any yesn-NULL argument that is yest a directory.
 * Zero, one or two objects may be locked by this function.
 *
 * @iyesde1: first iyesde to lock
 * @iyesde2: second iyesde to lock
 */
void lock_two_yesndirectories(struct iyesde *iyesde1, struct iyesde *iyesde2)
{
	if (iyesde1 > iyesde2)
		swap(iyesde1, iyesde2);

	if (iyesde1 && !S_ISDIR(iyesde1->i_mode))
		iyesde_lock(iyesde1);
	if (iyesde2 && !S_ISDIR(iyesde2->i_mode) && iyesde2 != iyesde1)
		iyesde_lock_nested(iyesde2, I_MUTEX_NONDIR2);
}
EXPORT_SYMBOL(lock_two_yesndirectories);

/**
 * unlock_two_yesndirectories - release locks from lock_two_yesndirectories()
 * @iyesde1: first iyesde to unlock
 * @iyesde2: second iyesde to unlock
 */
void unlock_two_yesndirectories(struct iyesde *iyesde1, struct iyesde *iyesde2)
{
	if (iyesde1 && !S_ISDIR(iyesde1->i_mode))
		iyesde_unlock(iyesde1);
	if (iyesde2 && !S_ISDIR(iyesde2->i_mode) && iyesde2 != iyesde1)
		iyesde_unlock(iyesde2);
}
EXPORT_SYMBOL(unlock_two_yesndirectories);

/**
 * iyesde_insert5 - obtain an iyesde from a mounted file system
 * @iyesde:	pre-allocated iyesde to use for insert to cache
 * @hashval:	hash value (usually iyesde number) to get
 * @test:	callback used for comparisons between iyesdes
 * @set:	callback used to initialize a new struct iyesde
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the iyesde specified by @hashval and @data in the iyesde cache,
 * and if present it is return it with an increased reference count. This is
 * a variant of iget5_locked() for callers that don't want to fail on memory
 * allocation of iyesde.
 *
 * If the iyesde is yest in cache, insert the pre-allocated iyesde to cache and
 * return it locked, hashed, and with the I_NEW flag set. The file system gets
 * to fill it in before unlocking it via unlock_new_iyesde().
 *
 * Note both @test and @set are called with the iyesde_hash_lock held, so can't
 * sleep.
 */
struct iyesde *iyesde_insert5(struct iyesde *iyesde, unsigned long hashval,
			    int (*test)(struct iyesde *, void *),
			    int (*set)(struct iyesde *, void *), void *data)
{
	struct hlist_head *head = iyesde_hashtable + hash(iyesde->i_sb, hashval);
	struct iyesde *old;
	bool creating = iyesde->i_state & I_CREATING;

again:
	spin_lock(&iyesde_hash_lock);
	old = find_iyesde(iyesde->i_sb, head, test, data);
	if (unlikely(old)) {
		/*
		 * Uhhuh, somebody else created the same iyesde under us.
		 * Use the old iyesde instead of the preallocated one.
		 */
		spin_unlock(&iyesde_hash_lock);
		if (IS_ERR(old))
			return NULL;
		wait_on_iyesde(old);
		if (unlikely(iyesde_unhashed(old))) {
			iput(old);
			goto again;
		}
		return old;
	}

	if (set && unlikely(set(iyesde, data))) {
		iyesde = NULL;
		goto unlock;
	}

	/*
	 * Return the locked iyesde with I_NEW set, the
	 * caller is responsible for filling in the contents
	 */
	spin_lock(&iyesde->i_lock);
	iyesde->i_state |= I_NEW;
	hlist_add_head(&iyesde->i_hash, head);
	spin_unlock(&iyesde->i_lock);
	if (!creating)
		iyesde_sb_list_add(iyesde);
unlock:
	spin_unlock(&iyesde_hash_lock);

	return iyesde;
}
EXPORT_SYMBOL(iyesde_insert5);

/**
 * iget5_locked - obtain an iyesde from a mounted file system
 * @sb:		super block of file system
 * @hashval:	hash value (usually iyesde number) to get
 * @test:	callback used for comparisons between iyesdes
 * @set:	callback used to initialize a new struct iyesde
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the iyesde specified by @hashval and @data in the iyesde cache,
 * and if present it is return it with an increased reference count. This is
 * a generalized version of iget_locked() for file systems where the iyesde
 * number is yest sufficient for unique identification of an iyesde.
 *
 * If the iyesde is yest in cache, allocate a new iyesde and return it locked,
 * hashed, and with the I_NEW flag set. The file system gets to fill it in
 * before unlocking it via unlock_new_iyesde().
 *
 * Note both @test and @set are called with the iyesde_hash_lock held, so can't
 * sleep.
 */
struct iyesde *iget5_locked(struct super_block *sb, unsigned long hashval,
		int (*test)(struct iyesde *, void *),
		int (*set)(struct iyesde *, void *), void *data)
{
	struct iyesde *iyesde = ilookup5(sb, hashval, test, data);

	if (!iyesde) {
		struct iyesde *new = alloc_iyesde(sb);

		if (new) {
			new->i_state = 0;
			iyesde = iyesde_insert5(new, hashval, test, set, data);
			if (unlikely(iyesde != new))
				destroy_iyesde(new);
		}
	}
	return iyesde;
}
EXPORT_SYMBOL(iget5_locked);

/**
 * iget_locked - obtain an iyesde from a mounted file system
 * @sb:		super block of file system
 * @iyes:	iyesde number to get
 *
 * Search for the iyesde specified by @iyes in the iyesde cache and if present
 * return it with an increased reference count. This is for file systems
 * where the iyesde number is sufficient for unique identification of an iyesde.
 *
 * If the iyesde is yest in cache, allocate a new iyesde and return it locked,
 * hashed, and with the I_NEW flag set.  The file system gets to fill it in
 * before unlocking it via unlock_new_iyesde().
 */
struct iyesde *iget_locked(struct super_block *sb, unsigned long iyes)
{
	struct hlist_head *head = iyesde_hashtable + hash(sb, iyes);
	struct iyesde *iyesde;
again:
	spin_lock(&iyesde_hash_lock);
	iyesde = find_iyesde_fast(sb, head, iyes);
	spin_unlock(&iyesde_hash_lock);
	if (iyesde) {
		if (IS_ERR(iyesde))
			return NULL;
		wait_on_iyesde(iyesde);
		if (unlikely(iyesde_unhashed(iyesde))) {
			iput(iyesde);
			goto again;
		}
		return iyesde;
	}

	iyesde = alloc_iyesde(sb);
	if (iyesde) {
		struct iyesde *old;

		spin_lock(&iyesde_hash_lock);
		/* We released the lock, so.. */
		old = find_iyesde_fast(sb, head, iyes);
		if (!old) {
			iyesde->i_iyes = iyes;
			spin_lock(&iyesde->i_lock);
			iyesde->i_state = I_NEW;
			hlist_add_head(&iyesde->i_hash, head);
			spin_unlock(&iyesde->i_lock);
			iyesde_sb_list_add(iyesde);
			spin_unlock(&iyesde_hash_lock);

			/* Return the locked iyesde with I_NEW set, the
			 * caller is responsible for filling in the contents
			 */
			return iyesde;
		}

		/*
		 * Uhhuh, somebody else created the same iyesde under
		 * us. Use the old iyesde instead of the one we just
		 * allocated.
		 */
		spin_unlock(&iyesde_hash_lock);
		destroy_iyesde(iyesde);
		if (IS_ERR(old))
			return NULL;
		iyesde = old;
		wait_on_iyesde(iyesde);
		if (unlikely(iyesde_unhashed(iyesde))) {
			iput(iyesde);
			goto again;
		}
	}
	return iyesde;
}
EXPORT_SYMBOL(iget_locked);

/*
 * search the iyesde cache for a matching iyesde number.
 * If we find one, then the iyesde number we are trying to
 * allocate is yest unique and so we should yest use it.
 *
 * Returns 1 if the iyesde number is unique, 0 if it is yest.
 */
static int test_iyesde_iunique(struct super_block *sb, unsigned long iyes)
{
	struct hlist_head *b = iyesde_hashtable + hash(sb, iyes);
	struct iyesde *iyesde;

	spin_lock(&iyesde_hash_lock);
	hlist_for_each_entry(iyesde, b, i_hash) {
		if (iyesde->i_iyes == iyes && iyesde->i_sb == sb) {
			spin_unlock(&iyesde_hash_lock);
			return 0;
		}
	}
	spin_unlock(&iyesde_hash_lock);

	return 1;
}

/**
 *	iunique - get a unique iyesde number
 *	@sb: superblock
 *	@max_reserved: highest reserved iyesde number
 *
 *	Obtain an iyesde number that is unique on the system for a given
 *	superblock. This is used by file systems that have yes natural
 *	permanent iyesde numbering system. An iyesde number is returned that
 *	is higher than the reserved limit but unique.
 *
 *	BUGS:
 *	With a large number of iyesdes live on the file system this function
 *	currently becomes quite slow.
 */
iyes_t iunique(struct super_block *sb, iyes_t max_reserved)
{
	/*
	 * On a 32bit, yesn LFS stat() call, glibc will generate an EOVERFLOW
	 * error if st_iyes won't fit in target struct field. Use 32bit counter
	 * here to attempt to avoid that.
	 */
	static DEFINE_SPINLOCK(iunique_lock);
	static unsigned int counter;
	iyes_t res;

	spin_lock(&iunique_lock);
	do {
		if (counter <= max_reserved)
			counter = max_reserved + 1;
		res = counter++;
	} while (!test_iyesde_iunique(sb, res));
	spin_unlock(&iunique_lock);

	return res;
}
EXPORT_SYMBOL(iunique);

struct iyesde *igrab(struct iyesde *iyesde)
{
	spin_lock(&iyesde->i_lock);
	if (!(iyesde->i_state & (I_FREEING|I_WILL_FREE))) {
		__iget(iyesde);
		spin_unlock(&iyesde->i_lock);
	} else {
		spin_unlock(&iyesde->i_lock);
		/*
		 * Handle the case where s_op->clear_iyesde is yest been
		 * called yet, and somebody is calling igrab
		 * while the iyesde is getting freed.
		 */
		iyesde = NULL;
	}
	return iyesde;
}
EXPORT_SYMBOL(igrab);

/**
 * ilookup5_yeswait - search for an iyesde in the iyesde cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually iyesde number) to search for
 * @test:	callback used for comparisons between iyesdes
 * @data:	opaque data pointer to pass to @test
 *
 * Search for the iyesde specified by @hashval and @data in the iyesde cache.
 * If the iyesde is in the cache, the iyesde is returned with an incremented
 * reference count.
 *
 * Note: I_NEW is yest waited upon so you have to be very careful what you do
 * with the returned iyesde.  You probably should be using ilookup5() instead.
 *
 * Note2: @test is called with the iyesde_hash_lock held, so can't sleep.
 */
struct iyesde *ilookup5_yeswait(struct super_block *sb, unsigned long hashval,
		int (*test)(struct iyesde *, void *), void *data)
{
	struct hlist_head *head = iyesde_hashtable + hash(sb, hashval);
	struct iyesde *iyesde;

	spin_lock(&iyesde_hash_lock);
	iyesde = find_iyesde(sb, head, test, data);
	spin_unlock(&iyesde_hash_lock);

	return IS_ERR(iyesde) ? NULL : iyesde;
}
EXPORT_SYMBOL(ilookup5_yeswait);

/**
 * ilookup5 - search for an iyesde in the iyesde cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually iyesde number) to search for
 * @test:	callback used for comparisons between iyesdes
 * @data:	opaque data pointer to pass to @test
 *
 * Search for the iyesde specified by @hashval and @data in the iyesde cache,
 * and if the iyesde is in the cache, return the iyesde with an incremented
 * reference count.  Waits on I_NEW before returning the iyesde.
 * returned with an incremented reference count.
 *
 * This is a generalized version of ilookup() for file systems where the
 * iyesde number is yest sufficient for unique identification of an iyesde.
 *
 * Note: @test is called with the iyesde_hash_lock held, so can't sleep.
 */
struct iyesde *ilookup5(struct super_block *sb, unsigned long hashval,
		int (*test)(struct iyesde *, void *), void *data)
{
	struct iyesde *iyesde;
again:
	iyesde = ilookup5_yeswait(sb, hashval, test, data);
	if (iyesde) {
		wait_on_iyesde(iyesde);
		if (unlikely(iyesde_unhashed(iyesde))) {
			iput(iyesde);
			goto again;
		}
	}
	return iyesde;
}
EXPORT_SYMBOL(ilookup5);

/**
 * ilookup - search for an iyesde in the iyesde cache
 * @sb:		super block of file system to search
 * @iyes:	iyesde number to search for
 *
 * Search for the iyesde @iyes in the iyesde cache, and if the iyesde is in the
 * cache, the iyesde is returned with an incremented reference count.
 */
struct iyesde *ilookup(struct super_block *sb, unsigned long iyes)
{
	struct hlist_head *head = iyesde_hashtable + hash(sb, iyes);
	struct iyesde *iyesde;
again:
	spin_lock(&iyesde_hash_lock);
	iyesde = find_iyesde_fast(sb, head, iyes);
	spin_unlock(&iyesde_hash_lock);

	if (iyesde) {
		if (IS_ERR(iyesde))
			return NULL;
		wait_on_iyesde(iyesde);
		if (unlikely(iyesde_unhashed(iyesde))) {
			iput(iyesde);
			goto again;
		}
	}
	return iyesde;
}
EXPORT_SYMBOL(ilookup);

/**
 * find_iyesde_yeswait - find an iyesde in the iyesde cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually iyesde number) to search for
 * @match:	callback used for comparisons between iyesdes
 * @data:	opaque data pointer to pass to @match
 *
 * Search for the iyesde specified by @hashval and @data in the iyesde
 * cache, where the helper function @match will return 0 if the iyesde
 * does yest match, 1 if the iyesde does match, and -1 if the search
 * should be stopped.  The @match function must be responsible for
 * taking the i_lock spin_lock and checking i_state for an iyesde being
 * freed or being initialized, and incrementing the reference count
 * before returning 1.  It also must yest sleep, since it is called with
 * the iyesde_hash_lock spinlock held.
 *
 * This is a even more generalized version of ilookup5() when the
 * function must never block --- find_iyesde() can block in
 * __wait_on_freeing_iyesde() --- or when the caller can yest increment
 * the reference count because the resulting iput() might cause an
 * iyesde eviction.  The tradeoff is that the @match funtion must be
 * very carefully implemented.
 */
struct iyesde *find_iyesde_yeswait(struct super_block *sb,
				unsigned long hashval,
				int (*match)(struct iyesde *, unsigned long,
					     void *),
				void *data)
{
	struct hlist_head *head = iyesde_hashtable + hash(sb, hashval);
	struct iyesde *iyesde, *ret_iyesde = NULL;
	int mval;

	spin_lock(&iyesde_hash_lock);
	hlist_for_each_entry(iyesde, head, i_hash) {
		if (iyesde->i_sb != sb)
			continue;
		mval = match(iyesde, hashval, data);
		if (mval == 0)
			continue;
		if (mval == 1)
			ret_iyesde = iyesde;
		goto out;
	}
out:
	spin_unlock(&iyesde_hash_lock);
	return ret_iyesde;
}
EXPORT_SYMBOL(find_iyesde_yeswait);

int insert_iyesde_locked(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	iyes_t iyes = iyesde->i_iyes;
	struct hlist_head *head = iyesde_hashtable + hash(sb, iyes);

	while (1) {
		struct iyesde *old = NULL;
		spin_lock(&iyesde_hash_lock);
		hlist_for_each_entry(old, head, i_hash) {
			if (old->i_iyes != iyes)
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
			spin_lock(&iyesde->i_lock);
			iyesde->i_state |= I_NEW | I_CREATING;
			hlist_add_head(&iyesde->i_hash, head);
			spin_unlock(&iyesde->i_lock);
			spin_unlock(&iyesde_hash_lock);
			return 0;
		}
		if (unlikely(old->i_state & I_CREATING)) {
			spin_unlock(&old->i_lock);
			spin_unlock(&iyesde_hash_lock);
			return -EBUSY;
		}
		__iget(old);
		spin_unlock(&old->i_lock);
		spin_unlock(&iyesde_hash_lock);
		wait_on_iyesde(old);
		if (unlikely(!iyesde_unhashed(old))) {
			iput(old);
			return -EBUSY;
		}
		iput(old);
	}
}
EXPORT_SYMBOL(insert_iyesde_locked);

int insert_iyesde_locked4(struct iyesde *iyesde, unsigned long hashval,
		int (*test)(struct iyesde *, void *), void *data)
{
	struct iyesde *old;

	iyesde->i_state |= I_CREATING;
	old = iyesde_insert5(iyesde, hashval, test, NULL, data);

	if (old != iyesde) {
		iput(old);
		return -EBUSY;
	}
	return 0;
}
EXPORT_SYMBOL(insert_iyesde_locked4);


int generic_delete_iyesde(struct iyesde *iyesde)
{
	return 1;
}
EXPORT_SYMBOL(generic_delete_iyesde);

/*
 * Called when we're dropping the last reference
 * to an iyesde.
 *
 * Call the FS "drop_iyesde()" function, defaulting to
 * the legacy UNIX filesystem behaviour.  If it tells
 * us to evict iyesde, do so.  Otherwise, retain iyesde
 * in cache if fs is alive, sync and evict if fs is
 * shutting down.
 */
static void iput_final(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	const struct super_operations *op = iyesde->i_sb->s_op;
	int drop;

	WARN_ON(iyesde->i_state & I_NEW);

	if (op->drop_iyesde)
		drop = op->drop_iyesde(iyesde);
	else
		drop = generic_drop_iyesde(iyesde);

	if (!drop && (sb->s_flags & SB_ACTIVE)) {
		iyesde_add_lru(iyesde);
		spin_unlock(&iyesde->i_lock);
		return;
	}

	if (!drop) {
		iyesde->i_state |= I_WILL_FREE;
		spin_unlock(&iyesde->i_lock);
		write_iyesde_yesw(iyesde, 1);
		spin_lock(&iyesde->i_lock);
		WARN_ON(iyesde->i_state & I_NEW);
		iyesde->i_state &= ~I_WILL_FREE;
	}

	iyesde->i_state |= I_FREEING;
	if (!list_empty(&iyesde->i_lru))
		iyesde_lru_list_del(iyesde);
	spin_unlock(&iyesde->i_lock);

	evict(iyesde);
}

/**
 *	iput	- put an iyesde
 *	@iyesde: iyesde to put
 *
 *	Puts an iyesde, dropping its usage count. If the iyesde use count hits
 *	zero, the iyesde is then freed and may also be destroyed.
 *
 *	Consequently, iput() can sleep.
 */
void iput(struct iyesde *iyesde)
{
	if (!iyesde)
		return;
	BUG_ON(iyesde->i_state & I_CLEAR);
retry:
	if (atomic_dec_and_lock(&iyesde->i_count, &iyesde->i_lock)) {
		if (iyesde->i_nlink && (iyesde->i_state & I_DIRTY_TIME)) {
			atomic_inc(&iyesde->i_count);
			spin_unlock(&iyesde->i_lock);
			trace_writeback_lazytime_iput(iyesde);
			mark_iyesde_dirty_sync(iyesde);
			goto retry;
		}
		iput_final(iyesde);
	}
}
EXPORT_SYMBOL(iput);

/**
 *	bmap	- find a block number in a file
 *	@iyesde: iyesde of file
 *	@block: block to find
 *
 *	Returns the block number on the device holding the iyesde that
 *	is the disk block number for the block of the file requested.
 *	That is, asked for block 4 of iyesde 1 the function will return the
 *	disk block relative to the disk start that holds that block of the
 *	file.
 */
sector_t bmap(struct iyesde *iyesde, sector_t block)
{
	sector_t res = 0;
	if (iyesde->i_mapping->a_ops->bmap)
		res = iyesde->i_mapping->a_ops->bmap(iyesde->i_mapping, block);
	return res;
}
EXPORT_SYMBOL(bmap);

/*
 * With relative atime, only update atime if the previous atime is
 * earlier than either the ctime or mtime or if at least a day has
 * passed since the last atime update.
 */
static int relatime_need_update(struct vfsmount *mnt, struct iyesde *iyesde,
			     struct timespec64 yesw)
{

	if (!(mnt->mnt_flags & MNT_RELATIME))
		return 1;
	/*
	 * Is mtime younger than atime? If no, update atime:
	 */
	if (timespec64_compare(&iyesde->i_mtime, &iyesde->i_atime) >= 0)
		return 1;
	/*
	 * Is ctime younger than atime? If no, update atime:
	 */
	if (timespec64_compare(&iyesde->i_ctime, &iyesde->i_atime) >= 0)
		return 1;

	/*
	 * Is the previous atime value older than a day? If no,
	 * update atime:
	 */
	if ((long)(yesw.tv_sec - iyesde->i_atime.tv_sec) >= 24*60*60)
		return 1;
	/*
	 * Good, we can skip the atime update:
	 */
	return 0;
}

int generic_update_time(struct iyesde *iyesde, struct timespec64 *time, int flags)
{
	int iflags = I_DIRTY_TIME;
	bool dirty = false;

	if (flags & S_ATIME)
		iyesde->i_atime = *time;
	if (flags & S_VERSION)
		dirty = iyesde_maybe_inc_iversion(iyesde, false);
	if (flags & S_CTIME)
		iyesde->i_ctime = *time;
	if (flags & S_MTIME)
		iyesde->i_mtime = *time;
	if ((flags & (S_ATIME | S_CTIME | S_MTIME)) &&
	    !(iyesde->i_sb->s_flags & SB_LAZYTIME))
		dirty = true;

	if (dirty)
		iflags |= I_DIRTY_SYNC;
	__mark_iyesde_dirty(iyesde, iflags);
	return 0;
}
EXPORT_SYMBOL(generic_update_time);

/*
 * This does the actual work of updating an iyesdes time or version.  Must have
 * had called mnt_want_write() before calling this.
 */
static int update_time(struct iyesde *iyesde, struct timespec64 *time, int flags)
{
	int (*update_time)(struct iyesde *, struct timespec64 *, int);

	update_time = iyesde->i_op->update_time ? iyesde->i_op->update_time :
		generic_update_time;

	return update_time(iyesde, time, flags);
}

/**
 *	touch_atime	-	update the access time
 *	@path: the &struct path to update
 *	@iyesde: iyesde to update
 *
 *	Update the accessed time on an iyesde and mark it for writeback.
 *	This function automatically handles read only file systems and media,
 *	as well as the "yesatime" flag and iyesde specific "yesatime" markers.
 */
bool atime_needs_update(const struct path *path, struct iyesde *iyesde)
{
	struct vfsmount *mnt = path->mnt;
	struct timespec64 yesw;

	if (iyesde->i_flags & S_NOATIME)
		return false;

	/* Atime updates will likely cause i_uid and i_gid to be written
	 * back improprely if their true value is unkyeswn to the vfs.
	 */
	if (HAS_UNMAPPED_ID(iyesde))
		return false;

	if (IS_NOATIME(iyesde))
		return false;
	if ((iyesde->i_sb->s_flags & SB_NODIRATIME) && S_ISDIR(iyesde->i_mode))
		return false;

	if (mnt->mnt_flags & MNT_NOATIME)
		return false;
	if ((mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(iyesde->i_mode))
		return false;

	yesw = current_time(iyesde);

	if (!relatime_need_update(mnt, iyesde, yesw))
		return false;

	if (timespec64_equal(&iyesde->i_atime, &yesw))
		return false;

	return true;
}

void touch_atime(const struct path *path)
{
	struct vfsmount *mnt = path->mnt;
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct timespec64 yesw;

	if (!atime_needs_update(path, iyesde))
		return;

	if (!sb_start_write_trylock(iyesde->i_sb))
		return;

	if (__mnt_want_write(mnt) != 0)
		goto skip_update;
	/*
	 * File systems can error out when updating iyesdes if they need to
	 * allocate new space to modify an iyesde (such is the case for
	 * Btrfs), but since we touch atime while walking down the path we
	 * really don't care if we failed to update the atime of the file,
	 * so just igyesre the return value.
	 * We may also fail on filesystems that have the ability to make parts
	 * of the fs read only, e.g. subvolumes in Btrfs.
	 */
	yesw = current_time(iyesde);
	update_time(iyesde, &yesw, S_ATIME);
	__mnt_drop_write(mnt);
skip_update:
	sb_end_write(iyesde->i_sb);
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
	umode_t mode = d_iyesde(dentry)->i_mode;
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

/*
 * Return mask of changes for yestify_change() that need to be done as a
 * response to write or truncate. Return 0 if yesthing has to be changed.
 * Negative value on error (change should be denied).
 */
int dentry_needs_remove_privs(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int mask = 0;
	int ret;

	if (IS_NOSEC(iyesde))
		return 0;

	mask = should_remove_suid(dentry);
	ret = security_iyesde_need_killpriv(dentry);
	if (ret < 0)
		return ret;
	if (ret)
		mask |= ATTR_KILL_PRIV;
	return mask;
}

static int __remove_privs(struct dentry *dentry, int kill)
{
	struct iattr newattrs;

	newattrs.ia_valid = ATTR_FORCE | kill;
	/*
	 * Note we call this on write, so yestify_change will yest
	 * encounter any conflicting delegations:
	 */
	return yestify_change(dentry, &newattrs, NULL);
}

/*
 * Remove special file priviledges (suid, capabilities) when file is written
 * to or truncated.
 */
int file_remove_privs(struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	struct iyesde *iyesde = file_iyesde(file);
	int kill;
	int error = 0;

	/*
	 * Fast path for yesthing security related.
	 * As well for yesn-regular files, e.g. blkdev iyesdes.
	 * For example, blkdev_write_iter() might get here
	 * trying to remove privs which it is yest allowed to.
	 */
	if (IS_NOSEC(iyesde) || !S_ISREG(iyesde->i_mode))
		return 0;

	kill = dentry_needs_remove_privs(dentry);
	if (kill < 0)
		return kill;
	if (kill)
		error = __remove_privs(dentry, kill);
	if (!error)
		iyesde_has_yes_xattr(iyesde);

	return error;
}
EXPORT_SYMBOL(file_remove_privs);

/**
 *	file_update_time	-	update mtime and ctime time
 *	@file: file accessed
 *
 *	Update the mtime and ctime members of an iyesde and mark the iyesde
 *	for writeback.  Note that this function is meant exclusively for
 *	usage in the file write path of filesystems, and filesystems may
 *	choose to explicitly igyesre update via this function with the
 *	S_NOCMTIME iyesde flag, e.g. for network filesystem where these
 *	timestamps are handled by the server.  This can return an error for
 *	file systems who need to allocate space in order to update an iyesde.
 */

int file_update_time(struct file *file)
{
	struct iyesde *iyesde = file_iyesde(file);
	struct timespec64 yesw;
	int sync_it = 0;
	int ret;

	/* First try to exhaust all avenues to yest sync */
	if (IS_NOCMTIME(iyesde))
		return 0;

	yesw = current_time(iyesde);
	if (!timespec64_equal(&iyesde->i_mtime, &yesw))
		sync_it = S_MTIME;

	if (!timespec64_equal(&iyesde->i_ctime, &yesw))
		sync_it |= S_CTIME;

	if (IS_I_VERSION(iyesde) && iyesde_iversion_need_inc(iyesde))
		sync_it |= S_VERSION;

	if (!sync_it)
		return 0;

	/* Finally allowed to write? Takes lock. */
	if (__mnt_want_write_file(file))
		return 0;

	ret = update_time(iyesde, &yesw, sync_it);
	__mnt_drop_write_file(file);

	return ret;
}
EXPORT_SYMBOL(file_update_time);

/* Caller must hold the file's iyesde lock */
int file_modified(struct file *file)
{
	int err;

	/*
	 * Clear the security bits if the process is yest being run by root.
	 * This keeps people from modifying setuid and setgid binaries.
	 */
	err = file_remove_privs(file);
	if (err)
		return err;

	if (unlikely(file->f_mode & FMODE_NOCMTIME))
		return 0;

	return file_update_time(file);
}
EXPORT_SYMBOL(file_modified);

int iyesde_needs_sync(struct iyesde *iyesde)
{
	if (IS_SYNC(iyesde))
		return 1;
	if (S_ISDIR(iyesde->i_mode) && IS_DIRSYNC(iyesde))
		return 1;
	return 0;
}
EXPORT_SYMBOL(iyesde_needs_sync);

/*
 * If we try to find an iyesde in the iyesde hash while it is being
 * deleted, we have to wait until the filesystem completes its
 * deletion before reporting that it isn't found.  This function waits
 * until the deletion _might_ have completed.  Callers are responsible
 * to recheck iyesde state.
 *
 * It doesn't matter if I_NEW is yest set initially, a call to
 * wake_up_bit(&iyesde->i_state, __I_NEW) after removing from the hash list
 * will DTRT.
 */
static void __wait_on_freeing_iyesde(struct iyesde *iyesde)
{
	wait_queue_head_t *wq;
	DEFINE_WAIT_BIT(wait, &iyesde->i_state, __I_NEW);
	wq = bit_waitqueue(&iyesde->i_state, __I_NEW);
	prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
	spin_unlock(&iyesde->i_lock);
	spin_unlock(&iyesde_hash_lock);
	schedule();
	finish_wait(wq, &wait.wq_entry);
	spin_lock(&iyesde_hash_lock);
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
 * Initialize the waitqueues and iyesde hash table.
 */
void __init iyesde_init_early(void)
{
	/* If hashes are distributed across NUMA yesdes, defer
	 * hash allocation until vmalloc space is available.
	 */
	if (hashdist)
		return;

	iyesde_hashtable =
		alloc_large_system_hash("Iyesde-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_EARLY | HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void __init iyesde_init(void)
{
	/* iyesde slab cache */
	iyesde_cachep = kmem_cache_create("iyesde_cache",
					 sizeof(struct iyesde),
					 0,
					 (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					 SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					 init_once);

	/* Hash may have been set up in iyesde_init_early */
	if (!hashdist)
		return;

	iyesde_hashtable =
		alloc_large_system_hash("Iyesde-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void init_special_iyesde(struct iyesde *iyesde, umode_t mode, dev_t rdev)
{
	iyesde->i_mode = mode;
	if (S_ISCHR(mode)) {
		iyesde->i_fop = &def_chr_fops;
		iyesde->i_rdev = rdev;
	} else if (S_ISBLK(mode)) {
		iyesde->i_fop = &def_blk_fops;
		iyesde->i_rdev = rdev;
	} else if (S_ISFIFO(mode))
		iyesde->i_fop = &pipefifo_fops;
	else if (S_ISSOCK(mode))
		;	/* leave it yes_open_fops */
	else
		printk(KERN_DEBUG "init_special_iyesde: bogus i_mode (%o) for"
				  " iyesde %s:%lu\n", mode, iyesde->i_sb->s_id,
				  iyesde->i_iyes);
}
EXPORT_SYMBOL(init_special_iyesde);

/**
 * iyesde_init_owner - Init uid,gid,mode for new iyesde according to posix standards
 * @iyesde: New iyesde
 * @dir: Directory iyesde
 * @mode: mode of the new iyesde
 */
void iyesde_init_owner(struct iyesde *iyesde, const struct iyesde *dir,
			umode_t mode)
{
	iyesde->i_uid = current_fsuid();
	if (dir && dir->i_mode & S_ISGID) {
		iyesde->i_gid = dir->i_gid;

		/* Directories are special, and always inherit S_ISGID */
		if (S_ISDIR(mode))
			mode |= S_ISGID;
		else if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP) &&
			 !in_group_p(iyesde->i_gid) &&
			 !capable_wrt_iyesde_uidgid(dir, CAP_FSETID))
			mode &= ~S_ISGID;
	} else
		iyesde->i_gid = current_fsgid();
	iyesde->i_mode = mode;
}
EXPORT_SYMBOL(iyesde_init_owner);

/**
 * iyesde_owner_or_capable - check current task permissions to iyesde
 * @iyesde: iyesde being checked
 *
 * Return true if current either has CAP_FOWNER in a namespace with the
 * iyesde owner uid mapped, or owns the file.
 */
bool iyesde_owner_or_capable(const struct iyesde *iyesde)
{
	struct user_namespace *ns;

	if (uid_eq(current_fsuid(), iyesde->i_uid))
		return true;

	ns = current_user_ns();
	if (kuid_has_mapping(ns, iyesde->i_uid) && ns_capable(ns, CAP_FOWNER))
		return true;
	return false;
}
EXPORT_SYMBOL(iyesde_owner_or_capable);

/*
 * Direct i/o helper functions
 */
static void __iyesde_dio_wait(struct iyesde *iyesde)
{
	wait_queue_head_t *wq = bit_waitqueue(&iyesde->i_state, __I_DIO_WAKEUP);
	DEFINE_WAIT_BIT(q, &iyesde->i_state, __I_DIO_WAKEUP);

	do {
		prepare_to_wait(wq, &q.wq_entry, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&iyesde->i_dio_count))
			schedule();
	} while (atomic_read(&iyesde->i_dio_count));
	finish_wait(wq, &q.wq_entry);
}

/**
 * iyesde_dio_wait - wait for outstanding DIO requests to finish
 * @iyesde: iyesde to wait for
 *
 * Waits for all pending direct I/O requests to finish so that we can
 * proceed with a truncate or equivalent operation.
 *
 * Must be called under a lock that serializes taking new references
 * to i_dio_count, usually by iyesde->i_mutex.
 */
void iyesde_dio_wait(struct iyesde *iyesde)
{
	if (atomic_read(&iyesde->i_dio_count))
		__iyesde_dio_wait(iyesde);
}
EXPORT_SYMBOL(iyesde_dio_wait);

/*
 * iyesde_set_flags - atomically set some iyesde flags
 *
 * Note: the caller should be holding i_mutex, or else be sure that
 * they have exclusive access to the iyesde structure (i.e., while the
 * iyesde is being instantiated).  The reason for the cmpxchg() loop
 * --- which wouldn't be necessary if all code paths which modify
 * i_flags actually followed this rule, is that there is at least one
 * code path which doesn't today so we use cmpxchg() out of an abundance
 * of caution.
 *
 * In the long run, i_mutex is overkill, and we should probably look
 * at using the i_lock spinlock to protect i_flags, and then make sure
 * it is so documented in include/linux/fs.h and that all code follows
 * the locking convention!!
 */
void iyesde_set_flags(struct iyesde *iyesde, unsigned int flags,
		     unsigned int mask)
{
	WARN_ON_ONCE(flags & ~mask);
	set_mask_bits(&iyesde->i_flags, mask, flags);
}
EXPORT_SYMBOL(iyesde_set_flags);

void iyesde_yeshighmem(struct iyesde *iyesde)
{
	mapping_set_gfp_mask(iyesde->i_mapping, GFP_USER);
}
EXPORT_SYMBOL(iyesde_yeshighmem);

/**
 * timespec64_trunc - Truncate timespec64 to a granularity
 * @t: Timespec64
 * @gran: Granularity in ns.
 *
 * Truncate a timespec64 to a granularity. Always rounds down. gran must
 * yest be 0 yesr greater than a second (NSEC_PER_SEC, or 10^9 ns).
 */
struct timespec64 timespec64_trunc(struct timespec64 t, unsigned gran)
{
	/* Avoid division in the common cases 1 ns and 1 s. */
	if (gran == 1) {
		/* yesthing */
	} else if (gran == NSEC_PER_SEC) {
		t.tv_nsec = 0;
	} else if (gran > 1 && gran < NSEC_PER_SEC) {
		t.tv_nsec -= t.tv_nsec % gran;
	} else {
		WARN(1, "illegal file time granularity: %u", gran);
	}
	return t;
}
EXPORT_SYMBOL(timespec64_trunc);

/**
 * timestamp_truncate - Truncate timespec to a granularity
 * @t: Timespec
 * @iyesde: iyesde being updated
 *
 * Truncate a timespec to the granularity supported by the fs
 * containing the iyesde. Always rounds down. gran must
 * yest be 0 yesr greater than a second (NSEC_PER_SEC, or 10^9 ns).
 */
struct timespec64 timestamp_truncate(struct timespec64 t, struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	unsigned int gran = sb->s_time_gran;

	t.tv_sec = clamp(t.tv_sec, sb->s_time_min, sb->s_time_max);
	if (unlikely(t.tv_sec == sb->s_time_max || t.tv_sec == sb->s_time_min))
		t.tv_nsec = 0;

	/* Avoid division in the common cases 1 ns and 1 s. */
	if (gran == 1)
		; /* yesthing */
	else if (gran == NSEC_PER_SEC)
		t.tv_nsec = 0;
	else if (gran > 1 && gran < NSEC_PER_SEC)
		t.tv_nsec -= t.tv_nsec % gran;
	else
		WARN(1, "invalid file time granularity: %u", gran);
	return t;
}
EXPORT_SYMBOL(timestamp_truncate);

/**
 * current_time - Return FS time
 * @iyesde: iyesde.
 *
 * Return the current time truncated to the time granularity supported by
 * the fs.
 *
 * Note that iyesde and iyesde->sb canyest be NULL.
 * Otherwise, the function warns and returns time without truncation.
 */
struct timespec64 current_time(struct iyesde *iyesde)
{
	struct timespec64 yesw;

	ktime_get_coarse_real_ts64(&yesw);

	if (unlikely(!iyesde->i_sb)) {
		WARN(1, "current_time() called with uninitialized super_block in the iyesde");
		return yesw;
	}

	return timestamp_truncate(yesw, iyesde);
}
EXPORT_SYMBOL(current_time);

/*
 * Generic function to check FS_IOC_SETFLAGS values and reject any invalid
 * configurations.
 *
 * Note: the caller should be holding i_mutex, or else be sure that they have
 * exclusive access to the iyesde structure.
 */
int vfs_ioc_setflags_prepare(struct iyesde *iyesde, unsigned int oldflags,
			     unsigned int flags)
{
	/*
	 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
	 * the relevant capability.
	 *
	 * This test looks nicer. Thanks to Pauline Middelink
	 */
	if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return -EPERM;

	return 0;
}
EXPORT_SYMBOL(vfs_ioc_setflags_prepare);

/*
 * Generic function to check FS_IOC_FSSETXATTR values and reject any invalid
 * configurations.
 *
 * Note: the caller should be holding i_mutex, or else be sure that they have
 * exclusive access to the iyesde structure.
 */
int vfs_ioc_fssetxattr_check(struct iyesde *iyesde, const struct fsxattr *old_fa,
			     struct fsxattr *fa)
{
	/*
	 * Can't modify an immutable/append-only file unless we have
	 * appropriate permission.
	 */
	if ((old_fa->fsx_xflags ^ fa->fsx_xflags) &
			(FS_XFLAG_IMMUTABLE | FS_XFLAG_APPEND) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return -EPERM;

	/*
	 * Project Quota ID state is only allowed to change from within the init
	 * namespace. Enforce that restriction only if we are trying to change
	 * the quota ID state. Everything else is allowed in user namespaces.
	 */
	if (current_user_ns() != &init_user_ns) {
		if (old_fa->fsx_projid != fa->fsx_projid)
			return -EINVAL;
		if ((old_fa->fsx_xflags ^ fa->fsx_xflags) &
				FS_XFLAG_PROJINHERIT)
			return -EINVAL;
	}

	/* Check extent size hints. */
	if ((fa->fsx_xflags & FS_XFLAG_EXTSIZE) && !S_ISREG(iyesde->i_mode))
		return -EINVAL;

	if ((fa->fsx_xflags & FS_XFLAG_EXTSZINHERIT) &&
			!S_ISDIR(iyesde->i_mode))
		return -EINVAL;

	if ((fa->fsx_xflags & FS_XFLAG_COWEXTSIZE) &&
	    !S_ISREG(iyesde->i_mode) && !S_ISDIR(iyesde->i_mode))
		return -EINVAL;

	/*
	 * It is only valid to set the DAX flag on regular files and
	 * directories on filesystems.
	 */
	if ((fa->fsx_xflags & FS_XFLAG_DAX) &&
	    !(S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode)))
		return -EINVAL;

	/* Extent size hints of zero turn off the flags. */
	if (fa->fsx_extsize == 0)
		fa->fsx_xflags &= ~(FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT);
	if (fa->fsx_cowextsize == 0)
		fa->fsx_xflags &= ~FS_XFLAG_COWEXTSIZE;

	return 0;
}
EXPORT_SYMBOL(vfs_ioc_fssetxattr_check);
