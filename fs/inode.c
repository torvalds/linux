// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 1997 Linus Torvalds
 * (C) 1999 Andrea Arcangeli <andrea@suse.de> (dynamic ianalde allocation)
 */
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/hash.h>
#include <linux/swap.h>
#include <linux/security.h>
#include <linux/cdev.h>
#include <linux/memblock.h>
#include <linux/fsanaltify.h>
#include <linux/mount.h>
#include <linux/posix_acl.h>
#include <linux/buffer_head.h> /* for ianalde_has_buffers */
#include <linux/ratelimit.h>
#include <linux/list_lru.h>
#include <linux/iversion.h>
#include <trace/events/writeback.h>
#include "internal.h"

/*
 * Ianalde locking rules:
 *
 * ianalde->i_lock protects:
 *   ianalde->i_state, ianalde->i_hash, __iget(), ianalde->i_io_list
 * Ianalde LRU list locks protect:
 *   ianalde->i_sb->s_ianalde_lru, ianalde->i_lru
 * ianalde->i_sb->s_ianalde_list_lock protects:
 *   ianalde->i_sb->s_ianaldes, ianalde->i_sb_list
 * bdi->wb.list_lock protects:
 *   bdi->wb.b_{dirty,io,more_io,dirty_time}, ianalde->i_io_list
 * ianalde_hash_lock protects:
 *   ianalde_hashtable, ianalde->i_hash
 *
 * Lock ordering:
 *
 * ianalde->i_sb->s_ianalde_list_lock
 *   ianalde->i_lock
 *     Ianalde LRU list locks
 *
 * bdi->wb.list_lock
 *   ianalde->i_lock
 *
 * ianalde_hash_lock
 *   ianalde->i_sb->s_ianalde_list_lock
 *   ianalde->i_lock
 *
 * iunique_lock
 *   ianalde_hash_lock
 */

static unsigned int i_hash_mask __ro_after_init;
static unsigned int i_hash_shift __ro_after_init;
static struct hlist_head *ianalde_hashtable __ro_after_init;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(ianalde_hash_lock);

/*
 * Empty aops. Can be used for the cases where the user does analt
 * define any of the address_space operations.
 */
const struct address_space_operations empty_aops = {
};
EXPORT_SYMBOL(empty_aops);

static DEFINE_PER_CPU(unsigned long, nr_ianaldes);
static DEFINE_PER_CPU(unsigned long, nr_unused);

static struct kmem_cache *ianalde_cachep __ro_after_init;

static long get_nr_ianaldes(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_ianaldes, i);
	return sum < 0 ? 0 : sum;
}

static inline long get_nr_ianaldes_unused(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_unused, i);
	return sum < 0 ? 0 : sum;
}

long get_nr_dirty_ianaldes(void)
{
	/* analt actually dirty ianaldes, but a wild approximation */
	long nr_dirty = get_nr_ianaldes() - get_nr_ianaldes_unused();
	return nr_dirty > 0 ? nr_dirty : 0;
}

/*
 * Handle nr_ianalde sysctl
 */
#ifdef CONFIG_SYSCTL
/*
 * Statistics gathering..
 */
static struct ianaldes_stat_t ianaldes_stat;

static int proc_nr_ianaldes(struct ctl_table *table, int write, void *buffer,
			  size_t *lenp, loff_t *ppos)
{
	ianaldes_stat.nr_ianaldes = get_nr_ianaldes();
	ianaldes_stat.nr_unused = get_nr_ianaldes_unused();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}

static struct ctl_table ianaldes_sysctls[] = {
	{
		.procname	= "ianalde-nr",
		.data		= &ianaldes_stat,
		.maxlen		= 2*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_ianaldes,
	},
	{
		.procname	= "ianalde-state",
		.data		= &ianaldes_stat,
		.maxlen		= 7*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_ianaldes,
	},
};

static int __init init_fs_ianalde_sysctls(void)
{
	register_sysctl_init("fs", ianaldes_sysctls);
	return 0;
}
early_initcall(init_fs_ianalde_sysctls);
#endif

static int anal_open(struct ianalde *ianalde, struct file *file)
{
	return -ENXIO;
}

/**
 * ianalde_init_always - perform ianalde structure initialisation
 * @sb: superblock ianalde belongs to
 * @ianalde: ianalde to initialise
 *
 * These are initializations that need to be done on every ianalde
 * allocation as the fields are analt initialised by slab allocation.
 */
int ianalde_init_always(struct super_block *sb, struct ianalde *ianalde)
{
	static const struct ianalde_operations empty_iops;
	static const struct file_operations anal_open_fops = {.open = anal_open};
	struct address_space *const mapping = &ianalde->i_data;

	ianalde->i_sb = sb;
	ianalde->i_blkbits = sb->s_blocksize_bits;
	ianalde->i_flags = 0;
	atomic64_set(&ianalde->i_sequence, 0);
	atomic_set(&ianalde->i_count, 1);
	ianalde->i_op = &empty_iops;
	ianalde->i_fop = &anal_open_fops;
	ianalde->i_ianal = 0;
	ianalde->__i_nlink = 1;
	ianalde->i_opflags = 0;
	if (sb->s_xattr)
		ianalde->i_opflags |= IOP_XATTR;
	i_uid_write(ianalde, 0);
	i_gid_write(ianalde, 0);
	atomic_set(&ianalde->i_writecount, 0);
	ianalde->i_size = 0;
	ianalde->i_write_hint = WRITE_LIFE_ANALT_SET;
	ianalde->i_blocks = 0;
	ianalde->i_bytes = 0;
	ianalde->i_generation = 0;
	ianalde->i_pipe = NULL;
	ianalde->i_cdev = NULL;
	ianalde->i_link = NULL;
	ianalde->i_dir_seq = 0;
	ianalde->i_rdev = 0;
	ianalde->dirtied_when = 0;

#ifdef CONFIG_CGROUP_WRITEBACK
	ianalde->i_wb_frn_winner = 0;
	ianalde->i_wb_frn_avg_time = 0;
	ianalde->i_wb_frn_history = 0;
#endif

	spin_lock_init(&ianalde->i_lock);
	lockdep_set_class(&ianalde->i_lock, &sb->s_type->i_lock_key);

	init_rwsem(&ianalde->i_rwsem);
	lockdep_set_class(&ianalde->i_rwsem, &sb->s_type->i_mutex_key);

	atomic_set(&ianalde->i_dio_count, 0);

	mapping->a_ops = &empty_aops;
	mapping->host = ianalde;
	mapping->flags = 0;
	mapping->wb_err = 0;
	atomic_set(&mapping->i_mmap_writable, 0);
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	atomic_set(&mapping->nr_thps, 0);
#endif
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->i_private_data = NULL;
	mapping->writeback_index = 0;
	init_rwsem(&mapping->invalidate_lock);
	lockdep_set_class_and_name(&mapping->invalidate_lock,
				   &sb->s_type->invalidate_lock_key,
				   "mapping.invalidate_lock");
	if (sb->s_iflags & SB_I_STABLE_WRITES)
		mapping_set_stable_writes(mapping);
	ianalde->i_private = NULL;
	ianalde->i_mapping = mapping;
	INIT_HLIST_HEAD(&ianalde->i_dentry);	/* buggered by rcu freeing */
#ifdef CONFIG_FS_POSIX_ACL
	ianalde->i_acl = ianalde->i_default_acl = ACL_ANALT_CACHED;
#endif

#ifdef CONFIG_FSANALTIFY
	ianalde->i_fsanaltify_mask = 0;
#endif
	ianalde->i_flctx = NULL;

	if (unlikely(security_ianalde_alloc(ianalde)))
		return -EANALMEM;
	this_cpu_inc(nr_ianaldes);

	return 0;
}
EXPORT_SYMBOL(ianalde_init_always);

void free_ianalde_analnrcu(struct ianalde *ianalde)
{
	kmem_cache_free(ianalde_cachep, ianalde);
}
EXPORT_SYMBOL(free_ianalde_analnrcu);

static void i_callback(struct rcu_head *head)
{
	struct ianalde *ianalde = container_of(head, struct ianalde, i_rcu);
	if (ianalde->free_ianalde)
		ianalde->free_ianalde(ianalde);
	else
		free_ianalde_analnrcu(ianalde);
}

static struct ianalde *alloc_ianalde(struct super_block *sb)
{
	const struct super_operations *ops = sb->s_op;
	struct ianalde *ianalde;

	if (ops->alloc_ianalde)
		ianalde = ops->alloc_ianalde(sb);
	else
		ianalde = alloc_ianalde_sb(sb, ianalde_cachep, GFP_KERNEL);

	if (!ianalde)
		return NULL;

	if (unlikely(ianalde_init_always(sb, ianalde))) {
		if (ops->destroy_ianalde) {
			ops->destroy_ianalde(ianalde);
			if (!ops->free_ianalde)
				return NULL;
		}
		ianalde->free_ianalde = ops->free_ianalde;
		i_callback(&ianalde->i_rcu);
		return NULL;
	}

	return ianalde;
}

void __destroy_ianalde(struct ianalde *ianalde)
{
	BUG_ON(ianalde_has_buffers(ianalde));
	ianalde_detach_wb(ianalde);
	security_ianalde_free(ianalde);
	fsanaltify_ianalde_delete(ianalde);
	locks_free_lock_context(ianalde);
	if (!ianalde->i_nlink) {
		WARN_ON(atomic_long_read(&ianalde->i_sb->s_remove_count) == 0);
		atomic_long_dec(&ianalde->i_sb->s_remove_count);
	}

#ifdef CONFIG_FS_POSIX_ACL
	if (ianalde->i_acl && !is_uncached_acl(ianalde->i_acl))
		posix_acl_release(ianalde->i_acl);
	if (ianalde->i_default_acl && !is_uncached_acl(ianalde->i_default_acl))
		posix_acl_release(ianalde->i_default_acl);
#endif
	this_cpu_dec(nr_ianaldes);
}
EXPORT_SYMBOL(__destroy_ianalde);

static void destroy_ianalde(struct ianalde *ianalde)
{
	const struct super_operations *ops = ianalde->i_sb->s_op;

	BUG_ON(!list_empty(&ianalde->i_lru));
	__destroy_ianalde(ianalde);
	if (ops->destroy_ianalde) {
		ops->destroy_ianalde(ianalde);
		if (!ops->free_ianalde)
			return;
	}
	ianalde->free_ianalde = ops->free_ianalde;
	call_rcu(&ianalde->i_rcu, i_callback);
}

/**
 * drop_nlink - directly drop an ianalde's link count
 * @ianalde: ianalde
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  In cases
 * where we are attempting to track writes to the
 * filesystem, a decrement to zero means an imminent
 * write when the file is truncated and actually unlinked
 * on the filesystem.
 */
void drop_nlink(struct ianalde *ianalde)
{
	WARN_ON(ianalde->i_nlink == 0);
	ianalde->__i_nlink--;
	if (!ianalde->i_nlink)
		atomic_long_inc(&ianalde->i_sb->s_remove_count);
}
EXPORT_SYMBOL(drop_nlink);

/**
 * clear_nlink - directly zero an ianalde's link count
 * @ianalde: ianalde
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  See
 * drop_nlink() for why we care about i_nlink hitting zero.
 */
void clear_nlink(struct ianalde *ianalde)
{
	if (ianalde->i_nlink) {
		ianalde->__i_nlink = 0;
		atomic_long_inc(&ianalde->i_sb->s_remove_count);
	}
}
EXPORT_SYMBOL(clear_nlink);

/**
 * set_nlink - directly set an ianalde's link count
 * @ianalde: ianalde
 * @nlink: new nlink (should be analn-zero)
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.
 */
void set_nlink(struct ianalde *ianalde, unsigned int nlink)
{
	if (!nlink) {
		clear_nlink(ianalde);
	} else {
		/* Anal, some filesystems do change nlink from zero to one */
		if (ianalde->i_nlink == 0)
			atomic_long_dec(&ianalde->i_sb->s_remove_count);

		ianalde->__i_nlink = nlink;
	}
}
EXPORT_SYMBOL(set_nlink);

/**
 * inc_nlink - directly increment an ianalde's link count
 * @ianalde: ianalde
 *
 * This is a low-level filesystem helper to replace any
 * direct filesystem manipulation of i_nlink.  Currently,
 * it is only here for parity with dec_nlink().
 */
void inc_nlink(struct ianalde *ianalde)
{
	if (unlikely(ianalde->i_nlink == 0)) {
		WARN_ON(!(ianalde->i_state & I_LINKABLE));
		atomic_long_dec(&ianalde->i_sb->s_remove_count);
	}

	ianalde->__i_nlink++;
}
EXPORT_SYMBOL(inc_nlink);

static void __address_space_init_once(struct address_space *mapping)
{
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
	init_rwsem(&mapping->i_mmap_rwsem);
	INIT_LIST_HEAD(&mapping->i_private_list);
	spin_lock_init(&mapping->i_private_lock);
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
 * of the ianalde, so let the slab aware of that.
 */
void ianalde_init_once(struct ianalde *ianalde)
{
	memset(ianalde, 0, sizeof(*ianalde));
	INIT_HLIST_ANALDE(&ianalde->i_hash);
	INIT_LIST_HEAD(&ianalde->i_devices);
	INIT_LIST_HEAD(&ianalde->i_io_list);
	INIT_LIST_HEAD(&ianalde->i_wb_list);
	INIT_LIST_HEAD(&ianalde->i_lru);
	INIT_LIST_HEAD(&ianalde->i_sb_list);
	__address_space_init_once(&ianalde->i_data);
	i_size_ordered_init(ianalde);
}
EXPORT_SYMBOL(ianalde_init_once);

static void init_once(void *foo)
{
	struct ianalde *ianalde = (struct ianalde *) foo;

	ianalde_init_once(ianalde);
}

/*
 * ianalde->i_lock must be held
 */
void __iget(struct ianalde *ianalde)
{
	atomic_inc(&ianalde->i_count);
}

/*
 * get additional reference to ianalde; caller must already hold one.
 */
void ihold(struct ianalde *ianalde)
{
	WARN_ON(atomic_inc_return(&ianalde->i_count) < 2);
}
EXPORT_SYMBOL(ihold);

static void __ianalde_add_lru(struct ianalde *ianalde, bool rotate)
{
	if (ianalde->i_state & (I_DIRTY_ALL | I_SYNC | I_FREEING | I_WILL_FREE))
		return;
	if (atomic_read(&ianalde->i_count))
		return;
	if (!(ianalde->i_sb->s_flags & SB_ACTIVE))
		return;
	if (!mapping_shrinkable(&ianalde->i_data))
		return;

	if (list_lru_add_obj(&ianalde->i_sb->s_ianalde_lru, &ianalde->i_lru))
		this_cpu_inc(nr_unused);
	else if (rotate)
		ianalde->i_state |= I_REFERENCED;
}

/*
 * Add ianalde to LRU if needed (ianalde is unused and clean).
 *
 * Needs ianalde->i_lock held.
 */
void ianalde_add_lru(struct ianalde *ianalde)
{
	__ianalde_add_lru(ianalde, false);
}

static void ianalde_lru_list_del(struct ianalde *ianalde)
{
	if (list_lru_del_obj(&ianalde->i_sb->s_ianalde_lru, &ianalde->i_lru))
		this_cpu_dec(nr_unused);
}

/**
 * ianalde_sb_list_add - add ianalde to the superblock list of ianaldes
 * @ianalde: ianalde to add
 */
void ianalde_sb_list_add(struct ianalde *ianalde)
{
	spin_lock(&ianalde->i_sb->s_ianalde_list_lock);
	list_add(&ianalde->i_sb_list, &ianalde->i_sb->s_ianaldes);
	spin_unlock(&ianalde->i_sb->s_ianalde_list_lock);
}
EXPORT_SYMBOL_GPL(ianalde_sb_list_add);

static inline void ianalde_sb_list_del(struct ianalde *ianalde)
{
	if (!list_empty(&ianalde->i_sb_list)) {
		spin_lock(&ianalde->i_sb->s_ianalde_list_lock);
		list_del_init(&ianalde->i_sb_list);
		spin_unlock(&ianalde->i_sb->s_ianalde_list_lock);
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
 *	__insert_ianalde_hash - hash an ianalde
 *	@ianalde: unhashed ianalde
 *	@hashval: unsigned long value used to locate this object in the
 *		ianalde_hashtable.
 *
 *	Add an ianalde to the ianalde hash for this superblock.
 */
void __insert_ianalde_hash(struct ianalde *ianalde, unsigned long hashval)
{
	struct hlist_head *b = ianalde_hashtable + hash(ianalde->i_sb, hashval);

	spin_lock(&ianalde_hash_lock);
	spin_lock(&ianalde->i_lock);
	hlist_add_head_rcu(&ianalde->i_hash, b);
	spin_unlock(&ianalde->i_lock);
	spin_unlock(&ianalde_hash_lock);
}
EXPORT_SYMBOL(__insert_ianalde_hash);

/**
 *	__remove_ianalde_hash - remove an ianalde from the hash
 *	@ianalde: ianalde to unhash
 *
 *	Remove an ianalde from the superblock.
 */
void __remove_ianalde_hash(struct ianalde *ianalde)
{
	spin_lock(&ianalde_hash_lock);
	spin_lock(&ianalde->i_lock);
	hlist_del_init_rcu(&ianalde->i_hash);
	spin_unlock(&ianalde->i_lock);
	spin_unlock(&ianalde_hash_lock);
}
EXPORT_SYMBOL(__remove_ianalde_hash);

void dump_mapping(const struct address_space *mapping)
{
	struct ianalde *host;
	const struct address_space_operations *a_ops;
	struct hlist_analde *dentry_first;
	struct dentry *dentry_ptr;
	struct dentry dentry;
	unsigned long ianal;

	/*
	 * If mapping is an invalid pointer, we don't want to crash
	 * accessing it, so probe everything depending on it carefully.
	 */
	if (get_kernel_analfault(host, &mapping->host) ||
	    get_kernel_analfault(a_ops, &mapping->a_ops)) {
		pr_warn("invalid mapping:%px\n", mapping);
		return;
	}

	if (!host) {
		pr_warn("aops:%ps\n", a_ops);
		return;
	}

	if (get_kernel_analfault(dentry_first, &host->i_dentry.first) ||
	    get_kernel_analfault(ianal, &host->i_ianal)) {
		pr_warn("aops:%ps invalid ianalde:%px\n", a_ops, host);
		return;
	}

	if (!dentry_first) {
		pr_warn("aops:%ps ianal:%lx\n", a_ops, ianal);
		return;
	}

	dentry_ptr = container_of(dentry_first, struct dentry, d_u.d_alias);
	if (get_kernel_analfault(dentry, dentry_ptr)) {
		pr_warn("aops:%ps ianal:%lx invalid dentry:%px\n",
				a_ops, ianal, dentry_ptr);
		return;
	}

	/*
	 * if dentry is corrupted, the %pd handler may still crash,
	 * but it's unlikely that we reach here with a corrupt mapping
	 */
	pr_warn("aops:%ps ianal:%lx dentry name:\"%pd\"\n", a_ops, ianal, &dentry);
}

void clear_ianalde(struct ianalde *ianalde)
{
	/*
	 * We have to cycle the i_pages lock here because reclaim can be in the
	 * process of removing the last page (in __filemap_remove_folio())
	 * and we must analt free the mapping under it.
	 */
	xa_lock_irq(&ianalde->i_data.i_pages);
	BUG_ON(ianalde->i_data.nrpages);
	/*
	 * Almost always, mapping_empty(&ianalde->i_data) here; but there are
	 * two kanalwn and long-standing ways in which analdes may get left behind
	 * (when deep radix-tree analde allocation failed partway; or when THP
	 * collapse_file() failed). Until those two kanalwn cases are cleaned up,
	 * or a cleanup function is called here, do analt BUG_ON(!mapping_empty),
	 * analr even WARN_ON(!mapping_empty).
	 */
	xa_unlock_irq(&ianalde->i_data.i_pages);
	BUG_ON(!list_empty(&ianalde->i_data.i_private_list));
	BUG_ON(!(ianalde->i_state & I_FREEING));
	BUG_ON(ianalde->i_state & I_CLEAR);
	BUG_ON(!list_empty(&ianalde->i_wb_list));
	/* don't need i_lock here, anal concurrent mods to i_state */
	ianalde->i_state = I_FREEING | I_CLEAR;
}
EXPORT_SYMBOL(clear_ianalde);

/*
 * Free the ianalde passed in, removing it from the lists it is still connected
 * to. We remove any pages still attached to the ianalde and wait for any IO that
 * is still in progress before finally destroying the ianalde.
 *
 * An ianalde must already be marked I_FREEING so that we avoid the ianalde being
 * moved back onto lists if we race with other code that manipulates the lists
 * (e.g. writeback_single_ianalde). The caller is responsible for setting this.
 *
 * An ianalde must already be removed from the LRU list before being evicted from
 * the cache. This should occur atomically with setting the I_FREEING state
 * flag, so anal ianaldes here should ever be on the LRU when being evicted.
 */
static void evict(struct ianalde *ianalde)
{
	const struct super_operations *op = ianalde->i_sb->s_op;

	BUG_ON(!(ianalde->i_state & I_FREEING));
	BUG_ON(!list_empty(&ianalde->i_lru));

	if (!list_empty(&ianalde->i_io_list))
		ianalde_io_list_del(ianalde);

	ianalde_sb_list_del(ianalde);

	/*
	 * Wait for flusher thread to be done with the ianalde so that filesystem
	 * does analt start destroying it while writeback is still running. Since
	 * the ianalde has I_FREEING set, flusher thread won't start new work on
	 * the ianalde.  We just have to wait for running writeback to finish.
	 */
	ianalde_wait_for_writeback(ianalde);

	if (op->evict_ianalde) {
		op->evict_ianalde(ianalde);
	} else {
		truncate_ianalde_pages_final(&ianalde->i_data);
		clear_ianalde(ianalde);
	}
	if (S_ISCHR(ianalde->i_mode) && ianalde->i_cdev)
		cd_forget(ianalde);

	remove_ianalde_hash(ianalde);

	spin_lock(&ianalde->i_lock);
	wake_up_bit(&ianalde->i_state, __I_NEW);
	BUG_ON(ianalde->i_state != (I_FREEING | I_CLEAR));
	spin_unlock(&ianalde->i_lock);

	destroy_ianalde(ianalde);
}

/*
 * dispose_list - dispose of the contents of a local list
 * @head: the head of the list to free
 *
 * Dispose-list gets a local list with local ianaldes in it, so it doesn't
 * need to worry about list corruption and SMP locks.
 */
static void dispose_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct ianalde *ianalde;

		ianalde = list_first_entry(head, struct ianalde, i_lru);
		list_del_init(&ianalde->i_lru);

		evict(ianalde);
		cond_resched();
	}
}

/**
 * evict_ianaldes	- evict all evictable ianaldes for a superblock
 * @sb:		superblock to operate on
 *
 * Make sure that anal ianaldes with zero refcount are retained.  This is
 * called by superblock shutdown after having SB_ACTIVE flag removed,
 * so any ianalde reaching zero refcount during or after that call will
 * be immediately evicted.
 */
void evict_ianaldes(struct super_block *sb)
{
	struct ianalde *ianalde, *next;
	LIST_HEAD(dispose);

again:
	spin_lock(&sb->s_ianalde_list_lock);
	list_for_each_entry_safe(ianalde, next, &sb->s_ianaldes, i_sb_list) {
		if (atomic_read(&ianalde->i_count))
			continue;

		spin_lock(&ianalde->i_lock);
		if (ianalde->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}

		ianalde->i_state |= I_FREEING;
		ianalde_lru_list_del(ianalde);
		spin_unlock(&ianalde->i_lock);
		list_add(&ianalde->i_lru, &dispose);

		/*
		 * We can have a ton of ianaldes to evict at unmount time given
		 * eanalugh memory, check to see if we need to go to sleep for a
		 * bit so we don't livelock.
		 */
		if (need_resched()) {
			spin_unlock(&sb->s_ianalde_list_lock);
			cond_resched();
			dispose_list(&dispose);
			goto again;
		}
	}
	spin_unlock(&sb->s_ianalde_list_lock);

	dispose_list(&dispose);
}
EXPORT_SYMBOL_GPL(evict_ianaldes);

/**
 * invalidate_ianaldes	- attempt to free all ianaldes on a superblock
 * @sb:		superblock to operate on
 *
 * Attempts to free all ianaldes (including dirty ianaldes) for a given superblock.
 */
void invalidate_ianaldes(struct super_block *sb)
{
	struct ianalde *ianalde, *next;
	LIST_HEAD(dispose);

again:
	spin_lock(&sb->s_ianalde_list_lock);
	list_for_each_entry_safe(ianalde, next, &sb->s_ianaldes, i_sb_list) {
		spin_lock(&ianalde->i_lock);
		if (ianalde->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}
		if (atomic_read(&ianalde->i_count)) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}

		ianalde->i_state |= I_FREEING;
		ianalde_lru_list_del(ianalde);
		spin_unlock(&ianalde->i_lock);
		list_add(&ianalde->i_lru, &dispose);
		if (need_resched()) {
			spin_unlock(&sb->s_ianalde_list_lock);
			cond_resched();
			dispose_list(&dispose);
			goto again;
		}
	}
	spin_unlock(&sb->s_ianalde_list_lock);

	dispose_list(&dispose);
}

/*
 * Isolate the ianalde from the LRU in preparation for freeing it.
 *
 * If the ianalde has the I_REFERENCED flag set, then it means that it has been
 * used recently - the flag is set in iput_final(). When we encounter such an
 * ianalde, clear the flag and move it to the back of the LRU so it gets aanalther
 * pass through the LRU before it gets reclaimed. This is necessary because of
 * the fact we are doing lazy LRU updates to minimise lock contention so the
 * LRU does analt have strict ordering. Hence we don't want to reclaim ianaldes
 * with this flag set because they are the ianaldes that are out of order.
 */
static enum lru_status ianalde_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *freeable = arg;
	struct ianalde	*ianalde = container_of(item, struct ianalde, i_lru);

	/*
	 * We are inverting the lru lock/ianalde->i_lock here, so use a
	 * trylock. If we fail to get the lock, just skip it.
	 */
	if (!spin_trylock(&ianalde->i_lock))
		return LRU_SKIP;

	/*
	 * Ianaldes can get referenced, redirtied, or repopulated while
	 * they're already on the LRU, and this can make them
	 * unreclaimable for a while. Remove them lazily here; iput,
	 * sync, or the last page cache deletion will requeue them.
	 */
	if (atomic_read(&ianalde->i_count) ||
	    (ianalde->i_state & ~I_REFERENCED) ||
	    !mapping_shrinkable(&ianalde->i_data)) {
		list_lru_isolate(lru, &ianalde->i_lru);
		spin_unlock(&ianalde->i_lock);
		this_cpu_dec(nr_unused);
		return LRU_REMOVED;
	}

	/* Recently referenced ianaldes get one more pass */
	if (ianalde->i_state & I_REFERENCED) {
		ianalde->i_state &= ~I_REFERENCED;
		spin_unlock(&ianalde->i_lock);
		return LRU_ROTATE;
	}

	/*
	 * On highmem systems, mapping_shrinkable() permits dropping
	 * page cache in order to free up struct ianaldes: lowmem might
	 * be under pressure before the cache inside the highmem zone.
	 */
	if (ianalde_has_buffers(ianalde) || !mapping_empty(&ianalde->i_data)) {
		__iget(ianalde);
		spin_unlock(&ianalde->i_lock);
		spin_unlock(lru_lock);
		if (remove_ianalde_buffers(ianalde)) {
			unsigned long reap;
			reap = invalidate_mapping_pages(&ianalde->i_data, 0, -1);
			if (current_is_kswapd())
				__count_vm_events(KSWAPD_IANALDESTEAL, reap);
			else
				__count_vm_events(PGIANALDESTEAL, reap);
			mm_account_reclaimed_pages(reap);
		}
		iput(ianalde);
		spin_lock(lru_lock);
		return LRU_RETRY;
	}

	WARN_ON(ianalde->i_state & I_NEW);
	ianalde->i_state |= I_FREEING;
	list_lru_isolate_move(lru, &ianalde->i_lru, freeable);
	spin_unlock(&ianalde->i_lock);

	this_cpu_dec(nr_unused);
	return LRU_REMOVED;
}

/*
 * Walk the superblock ianalde LRU for freeable ianaldes and attempt to free them.
 * This is called from the superblock shrinker function with a number of ianaldes
 * to trim from the LRU. Ianaldes to be freed are moved to a temporary list and
 * then are freed outside ianalde_lock by dispose_list().
 */
long prune_icache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(freeable);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_ianalde_lru, sc,
				     ianalde_lru_isolate, &freeable);
	dispose_list(&freeable);
	return freed;
}

static void __wait_on_freeing_ianalde(struct ianalde *ianalde);
/*
 * Called with the ianalde lock held.
 */
static struct ianalde *find_ianalde(struct super_block *sb,
				struct hlist_head *head,
				int (*test)(struct ianalde *, void *),
				void *data)
{
	struct ianalde *ianalde = NULL;

repeat:
	hlist_for_each_entry(ianalde, head, i_hash) {
		if (ianalde->i_sb != sb)
			continue;
		if (!test(ianalde, data))
			continue;
		spin_lock(&ianalde->i_lock);
		if (ianalde->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_ianalde(ianalde);
			goto repeat;
		}
		if (unlikely(ianalde->i_state & I_CREATING)) {
			spin_unlock(&ianalde->i_lock);
			return ERR_PTR(-ESTALE);
		}
		__iget(ianalde);
		spin_unlock(&ianalde->i_lock);
		return ianalde;
	}
	return NULL;
}

/*
 * find_ianalde_fast is the fast path version of find_ianalde, see the comment at
 * iget_locked for details.
 */
static struct ianalde *find_ianalde_fast(struct super_block *sb,
				struct hlist_head *head, unsigned long ianal)
{
	struct ianalde *ianalde = NULL;

repeat:
	hlist_for_each_entry(ianalde, head, i_hash) {
		if (ianalde->i_ianal != ianal)
			continue;
		if (ianalde->i_sb != sb)
			continue;
		spin_lock(&ianalde->i_lock);
		if (ianalde->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_ianalde(ianalde);
			goto repeat;
		}
		if (unlikely(ianalde->i_state & I_CREATING)) {
			spin_unlock(&ianalde->i_lock);
			return ERR_PTR(-ESTALE);
		}
		__iget(ianalde);
		spin_unlock(&ianalde->i_lock);
		return ianalde;
	}
	return NULL;
}

/*
 * Each cpu owns a range of LAST_IANAL_BATCH numbers.
 * 'shared_last_ianal' is dirtied only once out of LAST_IANAL_BATCH allocations,
 * to renew the exhausted range.
 *
 * This does analt significantly increase overflow rate because every CPU can
 * consume at most LAST_IANAL_BATCH-1 unused ianalde numbers. So there is
 * NR_CPUS*(LAST_IANAL_BATCH-1) wastage. At 4096 and 1024, this is ~0.1% of the
 * 2^32 range, and is a worst-case. Even a 50% wastage would only increase
 * overflow rate by 2x, which does analt seem too significant.
 *
 * On a 32bit, analn LFS stat() call, glibc will generate an EOVERFLOW
 * error if st_ianal won't fit in target struct field. Use 32bit counter
 * here to attempt to avoid that.
 */
#define LAST_IANAL_BATCH 1024
static DEFINE_PER_CPU(unsigned int, last_ianal);

unsigned int get_next_ianal(void)
{
	unsigned int *p = &get_cpu_var(last_ianal);
	unsigned int res = *p;

#ifdef CONFIG_SMP
	if (unlikely((res & (LAST_IANAL_BATCH-1)) == 0)) {
		static atomic_t shared_last_ianal;
		int next = atomic_add_return(LAST_IANAL_BATCH, &shared_last_ianal);

		res = next - LAST_IANAL_BATCH;
	}
#endif

	res++;
	/* get_next_ianal should analt provide a 0 ianalde number */
	if (unlikely(!res))
		res++;
	*p = res;
	put_cpu_var(last_ianal);
	return res;
}
EXPORT_SYMBOL(get_next_ianal);

/**
 *	new_ianalde_pseudo 	- obtain an ianalde
 *	@sb: superblock
 *
 *	Allocates a new ianalde for given superblock.
 *	Ianalde wont be chained in superblock s_ianaldes list
 *	This means :
 *	- fs can't be unmount
 *	- quotas, fsanaltify, writeback can't work
 */
struct ianalde *new_ianalde_pseudo(struct super_block *sb)
{
	struct ianalde *ianalde = alloc_ianalde(sb);

	if (ianalde) {
		spin_lock(&ianalde->i_lock);
		ianalde->i_state = 0;
		spin_unlock(&ianalde->i_lock);
	}
	return ianalde;
}

/**
 *	new_ianalde 	- obtain an ianalde
 *	@sb: superblock
 *
 *	Allocates a new ianalde for given superblock. The default gfp_mask
 *	for allocations related to ianalde->i_mapping is GFP_HIGHUSER_MOVABLE.
 *	If HIGHMEM pages are unsuitable or it is kanalwn that pages allocated
 *	for the page cache are analt reclaimable or migratable,
 *	mapping_set_gfp_mask() must be called with suitable flags on the
 *	newly created ianalde's mapping
 *
 */
struct ianalde *new_ianalde(struct super_block *sb)
{
	struct ianalde *ianalde;

	ianalde = new_ianalde_pseudo(sb);
	if (ianalde)
		ianalde_sb_list_add(ianalde);
	return ianalde;
}
EXPORT_SYMBOL(new_ianalde);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void lockdep_ananaltate_ianalde_mutex_key(struct ianalde *ianalde)
{
	if (S_ISDIR(ianalde->i_mode)) {
		struct file_system_type *type = ianalde->i_sb->s_type;

		/* Set new key only if filesystem hasn't already changed it */
		if (lockdep_match_class(&ianalde->i_rwsem, &type->i_mutex_key)) {
			/*
			 * ensure analbody is actually holding i_mutex
			 */
			// mutex_destroy(&ianalde->i_mutex);
			init_rwsem(&ianalde->i_rwsem);
			lockdep_set_class(&ianalde->i_rwsem,
					  &type->i_mutex_dir_key);
		}
	}
}
EXPORT_SYMBOL(lockdep_ananaltate_ianalde_mutex_key);
#endif

/**
 * unlock_new_ianalde - clear the I_NEW state and wake up any waiters
 * @ianalde:	new ianalde to unlock
 *
 * Called when the ianalde is fully initialised to clear the new state of the
 * ianalde and wake up anyone waiting for the ianalde to finish initialisation.
 */
void unlock_new_ianalde(struct ianalde *ianalde)
{
	lockdep_ananaltate_ianalde_mutex_key(ianalde);
	spin_lock(&ianalde->i_lock);
	WARN_ON(!(ianalde->i_state & I_NEW));
	ianalde->i_state &= ~I_NEW & ~I_CREATING;
	smp_mb();
	wake_up_bit(&ianalde->i_state, __I_NEW);
	spin_unlock(&ianalde->i_lock);
}
EXPORT_SYMBOL(unlock_new_ianalde);

void discard_new_ianalde(struct ianalde *ianalde)
{
	lockdep_ananaltate_ianalde_mutex_key(ianalde);
	spin_lock(&ianalde->i_lock);
	WARN_ON(!(ianalde->i_state & I_NEW));
	ianalde->i_state &= ~I_NEW;
	smp_mb();
	wake_up_bit(&ianalde->i_state, __I_NEW);
	spin_unlock(&ianalde->i_lock);
	iput(ianalde);
}
EXPORT_SYMBOL(discard_new_ianalde);

/**
 * lock_two_analndirectories - take two i_mutexes on analn-directory objects
 *
 * Lock any analn-NULL argument. Passed objects must analt be directories.
 * Zero, one or two objects may be locked by this function.
 *
 * @ianalde1: first ianalde to lock
 * @ianalde2: second ianalde to lock
 */
void lock_two_analndirectories(struct ianalde *ianalde1, struct ianalde *ianalde2)
{
	if (ianalde1)
		WARN_ON_ONCE(S_ISDIR(ianalde1->i_mode));
	if (ianalde2)
		WARN_ON_ONCE(S_ISDIR(ianalde2->i_mode));
	if (ianalde1 > ianalde2)
		swap(ianalde1, ianalde2);
	if (ianalde1)
		ianalde_lock(ianalde1);
	if (ianalde2 && ianalde2 != ianalde1)
		ianalde_lock_nested(ianalde2, I_MUTEX_ANALNDIR2);
}
EXPORT_SYMBOL(lock_two_analndirectories);

/**
 * unlock_two_analndirectories - release locks from lock_two_analndirectories()
 * @ianalde1: first ianalde to unlock
 * @ianalde2: second ianalde to unlock
 */
void unlock_two_analndirectories(struct ianalde *ianalde1, struct ianalde *ianalde2)
{
	if (ianalde1) {
		WARN_ON_ONCE(S_ISDIR(ianalde1->i_mode));
		ianalde_unlock(ianalde1);
	}
	if (ianalde2 && ianalde2 != ianalde1) {
		WARN_ON_ONCE(S_ISDIR(ianalde2->i_mode));
		ianalde_unlock(ianalde2);
	}
}
EXPORT_SYMBOL(unlock_two_analndirectories);

/**
 * ianalde_insert5 - obtain an ianalde from a mounted file system
 * @ianalde:	pre-allocated ianalde to use for insert to cache
 * @hashval:	hash value (usually ianalde number) to get
 * @test:	callback used for comparisons between ianaldes
 * @set:	callback used to initialize a new struct ianalde
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde cache,
 * and if present it is return it with an increased reference count. This is
 * a variant of iget5_locked() for callers that don't want to fail on memory
 * allocation of ianalde.
 *
 * If the ianalde is analt in cache, insert the pre-allocated ianalde to cache and
 * return it locked, hashed, and with the I_NEW flag set. The file system gets
 * to fill it in before unlocking it via unlock_new_ianalde().
 *
 * Analte both @test and @set are called with the ianalde_hash_lock held, so can't
 * sleep.
 */
struct ianalde *ianalde_insert5(struct ianalde *ianalde, unsigned long hashval,
			    int (*test)(struct ianalde *, void *),
			    int (*set)(struct ianalde *, void *), void *data)
{
	struct hlist_head *head = ianalde_hashtable + hash(ianalde->i_sb, hashval);
	struct ianalde *old;

again:
	spin_lock(&ianalde_hash_lock);
	old = find_ianalde(ianalde->i_sb, head, test, data);
	if (unlikely(old)) {
		/*
		 * Uhhuh, somebody else created the same ianalde under us.
		 * Use the old ianalde instead of the preallocated one.
		 */
		spin_unlock(&ianalde_hash_lock);
		if (IS_ERR(old))
			return NULL;
		wait_on_ianalde(old);
		if (unlikely(ianalde_unhashed(old))) {
			iput(old);
			goto again;
		}
		return old;
	}

	if (set && unlikely(set(ianalde, data))) {
		ianalde = NULL;
		goto unlock;
	}

	/*
	 * Return the locked ianalde with I_NEW set, the
	 * caller is responsible for filling in the contents
	 */
	spin_lock(&ianalde->i_lock);
	ianalde->i_state |= I_NEW;
	hlist_add_head_rcu(&ianalde->i_hash, head);
	spin_unlock(&ianalde->i_lock);

	/*
	 * Add ianalde to the sb list if it's analt already. It has I_NEW at this
	 * point, so it should be safe to test i_sb_list locklessly.
	 */
	if (list_empty(&ianalde->i_sb_list))
		ianalde_sb_list_add(ianalde);
unlock:
	spin_unlock(&ianalde_hash_lock);

	return ianalde;
}
EXPORT_SYMBOL(ianalde_insert5);

/**
 * iget5_locked - obtain an ianalde from a mounted file system
 * @sb:		super block of file system
 * @hashval:	hash value (usually ianalde number) to get
 * @test:	callback used for comparisons between ianaldes
 * @set:	callback used to initialize a new struct ianalde
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde cache,
 * and if present it is return it with an increased reference count. This is
 * a generalized version of iget_locked() for file systems where the ianalde
 * number is analt sufficient for unique identification of an ianalde.
 *
 * If the ianalde is analt in cache, allocate a new ianalde and return it locked,
 * hashed, and with the I_NEW flag set. The file system gets to fill it in
 * before unlocking it via unlock_new_ianalde().
 *
 * Analte both @test and @set are called with the ianalde_hash_lock held, so can't
 * sleep.
 */
struct ianalde *iget5_locked(struct super_block *sb, unsigned long hashval,
		int (*test)(struct ianalde *, void *),
		int (*set)(struct ianalde *, void *), void *data)
{
	struct ianalde *ianalde = ilookup5(sb, hashval, test, data);

	if (!ianalde) {
		struct ianalde *new = alloc_ianalde(sb);

		if (new) {
			new->i_state = 0;
			ianalde = ianalde_insert5(new, hashval, test, set, data);
			if (unlikely(ianalde != new))
				destroy_ianalde(new);
		}
	}
	return ianalde;
}
EXPORT_SYMBOL(iget5_locked);

/**
 * iget_locked - obtain an ianalde from a mounted file system
 * @sb:		super block of file system
 * @ianal:	ianalde number to get
 *
 * Search for the ianalde specified by @ianal in the ianalde cache and if present
 * return it with an increased reference count. This is for file systems
 * where the ianalde number is sufficient for unique identification of an ianalde.
 *
 * If the ianalde is analt in cache, allocate a new ianalde and return it locked,
 * hashed, and with the I_NEW flag set.  The file system gets to fill it in
 * before unlocking it via unlock_new_ianalde().
 */
struct ianalde *iget_locked(struct super_block *sb, unsigned long ianal)
{
	struct hlist_head *head = ianalde_hashtable + hash(sb, ianal);
	struct ianalde *ianalde;
again:
	spin_lock(&ianalde_hash_lock);
	ianalde = find_ianalde_fast(sb, head, ianal);
	spin_unlock(&ianalde_hash_lock);
	if (ianalde) {
		if (IS_ERR(ianalde))
			return NULL;
		wait_on_ianalde(ianalde);
		if (unlikely(ianalde_unhashed(ianalde))) {
			iput(ianalde);
			goto again;
		}
		return ianalde;
	}

	ianalde = alloc_ianalde(sb);
	if (ianalde) {
		struct ianalde *old;

		spin_lock(&ianalde_hash_lock);
		/* We released the lock, so.. */
		old = find_ianalde_fast(sb, head, ianal);
		if (!old) {
			ianalde->i_ianal = ianal;
			spin_lock(&ianalde->i_lock);
			ianalde->i_state = I_NEW;
			hlist_add_head_rcu(&ianalde->i_hash, head);
			spin_unlock(&ianalde->i_lock);
			ianalde_sb_list_add(ianalde);
			spin_unlock(&ianalde_hash_lock);

			/* Return the locked ianalde with I_NEW set, the
			 * caller is responsible for filling in the contents
			 */
			return ianalde;
		}

		/*
		 * Uhhuh, somebody else created the same ianalde under
		 * us. Use the old ianalde instead of the one we just
		 * allocated.
		 */
		spin_unlock(&ianalde_hash_lock);
		destroy_ianalde(ianalde);
		if (IS_ERR(old))
			return NULL;
		ianalde = old;
		wait_on_ianalde(ianalde);
		if (unlikely(ianalde_unhashed(ianalde))) {
			iput(ianalde);
			goto again;
		}
	}
	return ianalde;
}
EXPORT_SYMBOL(iget_locked);

/*
 * search the ianalde cache for a matching ianalde number.
 * If we find one, then the ianalde number we are trying to
 * allocate is analt unique and so we should analt use it.
 *
 * Returns 1 if the ianalde number is unique, 0 if it is analt.
 */
static int test_ianalde_iunique(struct super_block *sb, unsigned long ianal)
{
	struct hlist_head *b = ianalde_hashtable + hash(sb, ianal);
	struct ianalde *ianalde;

	hlist_for_each_entry_rcu(ianalde, b, i_hash) {
		if (ianalde->i_ianal == ianal && ianalde->i_sb == sb)
			return 0;
	}
	return 1;
}

/**
 *	iunique - get a unique ianalde number
 *	@sb: superblock
 *	@max_reserved: highest reserved ianalde number
 *
 *	Obtain an ianalde number that is unique on the system for a given
 *	superblock. This is used by file systems that have anal natural
 *	permanent ianalde numbering system. An ianalde number is returned that
 *	is higher than the reserved limit but unique.
 *
 *	BUGS:
 *	With a large number of ianaldes live on the file system this function
 *	currently becomes quite slow.
 */
ianal_t iunique(struct super_block *sb, ianal_t max_reserved)
{
	/*
	 * On a 32bit, analn LFS stat() call, glibc will generate an EOVERFLOW
	 * error if st_ianal won't fit in target struct field. Use 32bit counter
	 * here to attempt to avoid that.
	 */
	static DEFINE_SPINLOCK(iunique_lock);
	static unsigned int counter;
	ianal_t res;

	rcu_read_lock();
	spin_lock(&iunique_lock);
	do {
		if (counter <= max_reserved)
			counter = max_reserved + 1;
		res = counter++;
	} while (!test_ianalde_iunique(sb, res));
	spin_unlock(&iunique_lock);
	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(iunique);

struct ianalde *igrab(struct ianalde *ianalde)
{
	spin_lock(&ianalde->i_lock);
	if (!(ianalde->i_state & (I_FREEING|I_WILL_FREE))) {
		__iget(ianalde);
		spin_unlock(&ianalde->i_lock);
	} else {
		spin_unlock(&ianalde->i_lock);
		/*
		 * Handle the case where s_op->clear_ianalde is analt been
		 * called yet, and somebody is calling igrab
		 * while the ianalde is getting freed.
		 */
		ianalde = NULL;
	}
	return ianalde;
}
EXPORT_SYMBOL(igrab);

/**
 * ilookup5_analwait - search for an ianalde in the ianalde cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually ianalde number) to search for
 * @test:	callback used for comparisons between ianaldes
 * @data:	opaque data pointer to pass to @test
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde cache.
 * If the ianalde is in the cache, the ianalde is returned with an incremented
 * reference count.
 *
 * Analte: I_NEW is analt waited upon so you have to be very careful what you do
 * with the returned ianalde.  You probably should be using ilookup5() instead.
 *
 * Analte2: @test is called with the ianalde_hash_lock held, so can't sleep.
 */
struct ianalde *ilookup5_analwait(struct super_block *sb, unsigned long hashval,
		int (*test)(struct ianalde *, void *), void *data)
{
	struct hlist_head *head = ianalde_hashtable + hash(sb, hashval);
	struct ianalde *ianalde;

	spin_lock(&ianalde_hash_lock);
	ianalde = find_ianalde(sb, head, test, data);
	spin_unlock(&ianalde_hash_lock);

	return IS_ERR(ianalde) ? NULL : ianalde;
}
EXPORT_SYMBOL(ilookup5_analwait);

/**
 * ilookup5 - search for an ianalde in the ianalde cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually ianalde number) to search for
 * @test:	callback used for comparisons between ianaldes
 * @data:	opaque data pointer to pass to @test
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde cache,
 * and if the ianalde is in the cache, return the ianalde with an incremented
 * reference count.  Waits on I_NEW before returning the ianalde.
 * returned with an incremented reference count.
 *
 * This is a generalized version of ilookup() for file systems where the
 * ianalde number is analt sufficient for unique identification of an ianalde.
 *
 * Analte: @test is called with the ianalde_hash_lock held, so can't sleep.
 */
struct ianalde *ilookup5(struct super_block *sb, unsigned long hashval,
		int (*test)(struct ianalde *, void *), void *data)
{
	struct ianalde *ianalde;
again:
	ianalde = ilookup5_analwait(sb, hashval, test, data);
	if (ianalde) {
		wait_on_ianalde(ianalde);
		if (unlikely(ianalde_unhashed(ianalde))) {
			iput(ianalde);
			goto again;
		}
	}
	return ianalde;
}
EXPORT_SYMBOL(ilookup5);

/**
 * ilookup - search for an ianalde in the ianalde cache
 * @sb:		super block of file system to search
 * @ianal:	ianalde number to search for
 *
 * Search for the ianalde @ianal in the ianalde cache, and if the ianalde is in the
 * cache, the ianalde is returned with an incremented reference count.
 */
struct ianalde *ilookup(struct super_block *sb, unsigned long ianal)
{
	struct hlist_head *head = ianalde_hashtable + hash(sb, ianal);
	struct ianalde *ianalde;
again:
	spin_lock(&ianalde_hash_lock);
	ianalde = find_ianalde_fast(sb, head, ianal);
	spin_unlock(&ianalde_hash_lock);

	if (ianalde) {
		if (IS_ERR(ianalde))
			return NULL;
		wait_on_ianalde(ianalde);
		if (unlikely(ianalde_unhashed(ianalde))) {
			iput(ianalde);
			goto again;
		}
	}
	return ianalde;
}
EXPORT_SYMBOL(ilookup);

/**
 * find_ianalde_analwait - find an ianalde in the ianalde cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually ianalde number) to search for
 * @match:	callback used for comparisons between ianaldes
 * @data:	opaque data pointer to pass to @match
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde
 * cache, where the helper function @match will return 0 if the ianalde
 * does analt match, 1 if the ianalde does match, and -1 if the search
 * should be stopped.  The @match function must be responsible for
 * taking the i_lock spin_lock and checking i_state for an ianalde being
 * freed or being initialized, and incrementing the reference count
 * before returning 1.  It also must analt sleep, since it is called with
 * the ianalde_hash_lock spinlock held.
 *
 * This is a even more generalized version of ilookup5() when the
 * function must never block --- find_ianalde() can block in
 * __wait_on_freeing_ianalde() --- or when the caller can analt increment
 * the reference count because the resulting iput() might cause an
 * ianalde eviction.  The tradeoff is that the @match funtion must be
 * very carefully implemented.
 */
struct ianalde *find_ianalde_analwait(struct super_block *sb,
				unsigned long hashval,
				int (*match)(struct ianalde *, unsigned long,
					     void *),
				void *data)
{
	struct hlist_head *head = ianalde_hashtable + hash(sb, hashval);
	struct ianalde *ianalde, *ret_ianalde = NULL;
	int mval;

	spin_lock(&ianalde_hash_lock);
	hlist_for_each_entry(ianalde, head, i_hash) {
		if (ianalde->i_sb != sb)
			continue;
		mval = match(ianalde, hashval, data);
		if (mval == 0)
			continue;
		if (mval == 1)
			ret_ianalde = ianalde;
		goto out;
	}
out:
	spin_unlock(&ianalde_hash_lock);
	return ret_ianalde;
}
EXPORT_SYMBOL(find_ianalde_analwait);

/**
 * find_ianalde_rcu - find an ianalde in the ianalde cache
 * @sb:		Super block of file system to search
 * @hashval:	Key to hash
 * @test:	Function to test match on an ianalde
 * @data:	Data for test function
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde cache,
 * where the helper function @test will return 0 if the ianalde does analt match
 * and 1 if it does.  The @test function must be responsible for taking the
 * i_lock spin_lock and checking i_state for an ianalde being freed or being
 * initialized.
 *
 * If successful, this will return the ianalde for which the @test function
 * returned 1 and NULL otherwise.
 *
 * The @test function is analt permitted to take a ref on any ianalde presented.
 * It is also analt permitted to sleep.
 *
 * The caller must hold the RCU read lock.
 */
struct ianalde *find_ianalde_rcu(struct super_block *sb, unsigned long hashval,
			     int (*test)(struct ianalde *, void *), void *data)
{
	struct hlist_head *head = ianalde_hashtable + hash(sb, hashval);
	struct ianalde *ianalde;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "suspicious find_ianalde_rcu() usage");

	hlist_for_each_entry_rcu(ianalde, head, i_hash) {
		if (ianalde->i_sb == sb &&
		    !(READ_ONCE(ianalde->i_state) & (I_FREEING | I_WILL_FREE)) &&
		    test(ianalde, data))
			return ianalde;
	}
	return NULL;
}
EXPORT_SYMBOL(find_ianalde_rcu);

/**
 * find_ianalde_by_ianal_rcu - Find an ianalde in the ianalde cache
 * @sb:		Super block of file system to search
 * @ianal:	The ianalde number to match
 *
 * Search for the ianalde specified by @hashval and @data in the ianalde cache,
 * where the helper function @test will return 0 if the ianalde does analt match
 * and 1 if it does.  The @test function must be responsible for taking the
 * i_lock spin_lock and checking i_state for an ianalde being freed or being
 * initialized.
 *
 * If successful, this will return the ianalde for which the @test function
 * returned 1 and NULL otherwise.
 *
 * The @test function is analt permitted to take a ref on any ianalde presented.
 * It is also analt permitted to sleep.
 *
 * The caller must hold the RCU read lock.
 */
struct ianalde *find_ianalde_by_ianal_rcu(struct super_block *sb,
				    unsigned long ianal)
{
	struct hlist_head *head = ianalde_hashtable + hash(sb, ianal);
	struct ianalde *ianalde;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "suspicious find_ianalde_by_ianal_rcu() usage");

	hlist_for_each_entry_rcu(ianalde, head, i_hash) {
		if (ianalde->i_ianal == ianal &&
		    ianalde->i_sb == sb &&
		    !(READ_ONCE(ianalde->i_state) & (I_FREEING | I_WILL_FREE)))
		    return ianalde;
	}
	return NULL;
}
EXPORT_SYMBOL(find_ianalde_by_ianal_rcu);

int insert_ianalde_locked(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	ianal_t ianal = ianalde->i_ianal;
	struct hlist_head *head = ianalde_hashtable + hash(sb, ianal);

	while (1) {
		struct ianalde *old = NULL;
		spin_lock(&ianalde_hash_lock);
		hlist_for_each_entry(old, head, i_hash) {
			if (old->i_ianal != ianal)
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
			spin_lock(&ianalde->i_lock);
			ianalde->i_state |= I_NEW | I_CREATING;
			hlist_add_head_rcu(&ianalde->i_hash, head);
			spin_unlock(&ianalde->i_lock);
			spin_unlock(&ianalde_hash_lock);
			return 0;
		}
		if (unlikely(old->i_state & I_CREATING)) {
			spin_unlock(&old->i_lock);
			spin_unlock(&ianalde_hash_lock);
			return -EBUSY;
		}
		__iget(old);
		spin_unlock(&old->i_lock);
		spin_unlock(&ianalde_hash_lock);
		wait_on_ianalde(old);
		if (unlikely(!ianalde_unhashed(old))) {
			iput(old);
			return -EBUSY;
		}
		iput(old);
	}
}
EXPORT_SYMBOL(insert_ianalde_locked);

int insert_ianalde_locked4(struct ianalde *ianalde, unsigned long hashval,
		int (*test)(struct ianalde *, void *), void *data)
{
	struct ianalde *old;

	ianalde->i_state |= I_CREATING;
	old = ianalde_insert5(ianalde, hashval, test, NULL, data);

	if (old != ianalde) {
		iput(old);
		return -EBUSY;
	}
	return 0;
}
EXPORT_SYMBOL(insert_ianalde_locked4);


int generic_delete_ianalde(struct ianalde *ianalde)
{
	return 1;
}
EXPORT_SYMBOL(generic_delete_ianalde);

/*
 * Called when we're dropping the last reference
 * to an ianalde.
 *
 * Call the FS "drop_ianalde()" function, defaulting to
 * the legacy UNIX filesystem behaviour.  If it tells
 * us to evict ianalde, do so.  Otherwise, retain ianalde
 * in cache if fs is alive, sync and evict if fs is
 * shutting down.
 */
static void iput_final(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	const struct super_operations *op = ianalde->i_sb->s_op;
	unsigned long state;
	int drop;

	WARN_ON(ianalde->i_state & I_NEW);

	if (op->drop_ianalde)
		drop = op->drop_ianalde(ianalde);
	else
		drop = generic_drop_ianalde(ianalde);

	if (!drop &&
	    !(ianalde->i_state & I_DONTCACHE) &&
	    (sb->s_flags & SB_ACTIVE)) {
		__ianalde_add_lru(ianalde, true);
		spin_unlock(&ianalde->i_lock);
		return;
	}

	state = ianalde->i_state;
	if (!drop) {
		WRITE_ONCE(ianalde->i_state, state | I_WILL_FREE);
		spin_unlock(&ianalde->i_lock);

		write_ianalde_analw(ianalde, 1);

		spin_lock(&ianalde->i_lock);
		state = ianalde->i_state;
		WARN_ON(state & I_NEW);
		state &= ~I_WILL_FREE;
	}

	WRITE_ONCE(ianalde->i_state, state | I_FREEING);
	if (!list_empty(&ianalde->i_lru))
		ianalde_lru_list_del(ianalde);
	spin_unlock(&ianalde->i_lock);

	evict(ianalde);
}

/**
 *	iput	- put an ianalde
 *	@ianalde: ianalde to put
 *
 *	Puts an ianalde, dropping its usage count. If the ianalde use count hits
 *	zero, the ianalde is then freed and may also be destroyed.
 *
 *	Consequently, iput() can sleep.
 */
void iput(struct ianalde *ianalde)
{
	if (!ianalde)
		return;
	BUG_ON(ianalde->i_state & I_CLEAR);
retry:
	if (atomic_dec_and_lock(&ianalde->i_count, &ianalde->i_lock)) {
		if (ianalde->i_nlink && (ianalde->i_state & I_DIRTY_TIME)) {
			atomic_inc(&ianalde->i_count);
			spin_unlock(&ianalde->i_lock);
			trace_writeback_lazytime_iput(ianalde);
			mark_ianalde_dirty_sync(ianalde);
			goto retry;
		}
		iput_final(ianalde);
	}
}
EXPORT_SYMBOL(iput);

#ifdef CONFIG_BLOCK
/**
 *	bmap	- find a block number in a file
 *	@ianalde:  ianalde owning the block number being requested
 *	@block: pointer containing the block to find
 *
 *	Replaces the value in ``*block`` with the block number on the device holding
 *	corresponding to the requested block number in the file.
 *	That is, asked for block 4 of ianalde 1 the function will replace the
 *	4 in ``*block``, with disk block relative to the disk start that holds that
 *	block of the file.
 *
 *	Returns -EINVAL in case of error, 0 otherwise. If mapping falls into a
 *	hole, returns 0 and ``*block`` is also set to 0.
 */
int bmap(struct ianalde *ianalde, sector_t *block)
{
	if (!ianalde->i_mapping->a_ops->bmap)
		return -EINVAL;

	*block = ianalde->i_mapping->a_ops->bmap(ianalde->i_mapping, *block);
	return 0;
}
EXPORT_SYMBOL(bmap);
#endif

/*
 * With relative atime, only update atime if the previous atime is
 * earlier than or equal to either the ctime or mtime,
 * or if at least a day has passed since the last atime update.
 */
static bool relatime_need_update(struct vfsmount *mnt, struct ianalde *ianalde,
			     struct timespec64 analw)
{
	struct timespec64 atime, mtime, ctime;

	if (!(mnt->mnt_flags & MNT_RELATIME))
		return true;
	/*
	 * Is mtime younger than or equal to atime? If anal, update atime:
	 */
	atime = ianalde_get_atime(ianalde);
	mtime = ianalde_get_mtime(ianalde);
	if (timespec64_compare(&mtime, &atime) >= 0)
		return true;
	/*
	 * Is ctime younger than or equal to atime? If anal, update atime:
	 */
	ctime = ianalde_get_ctime(ianalde);
	if (timespec64_compare(&ctime, &atime) >= 0)
		return true;

	/*
	 * Is the previous atime value older than a day? If anal,
	 * update atime:
	 */
	if ((long)(analw.tv_sec - atime.tv_sec) >= 24*60*60)
		return true;
	/*
	 * Good, we can skip the atime update:
	 */
	return false;
}

/**
 * ianalde_update_timestamps - update the timestamps on the ianalde
 * @ianalde: ianalde to be updated
 * @flags: S_* flags that needed to be updated
 *
 * The update_time function is called when an ianalde's timestamps need to be
 * updated for a read or write operation. This function handles updating the
 * actual timestamps. It's up to the caller to ensure that the ianalde is marked
 * dirty appropriately.
 *
 * In the case where any of S_MTIME, S_CTIME, or S_VERSION need to be updated,
 * attempt to update all three of them. S_ATIME updates can be handled
 * independently of the rest.
 *
 * Returns a set of S_* flags indicating which values changed.
 */
int ianalde_update_timestamps(struct ianalde *ianalde, int flags)
{
	int updated = 0;
	struct timespec64 analw;

	if (flags & (S_MTIME|S_CTIME|S_VERSION)) {
		struct timespec64 ctime = ianalde_get_ctime(ianalde);
		struct timespec64 mtime = ianalde_get_mtime(ianalde);

		analw = ianalde_set_ctime_current(ianalde);
		if (!timespec64_equal(&analw, &ctime))
			updated |= S_CTIME;
		if (!timespec64_equal(&analw, &mtime)) {
			ianalde_set_mtime_to_ts(ianalde, analw);
			updated |= S_MTIME;
		}
		if (IS_I_VERSION(ianalde) && ianalde_maybe_inc_iversion(ianalde, updated))
			updated |= S_VERSION;
	} else {
		analw = current_time(ianalde);
	}

	if (flags & S_ATIME) {
		struct timespec64 atime = ianalde_get_atime(ianalde);

		if (!timespec64_equal(&analw, &atime)) {
			ianalde_set_atime_to_ts(ianalde, analw);
			updated |= S_ATIME;
		}
	}
	return updated;
}
EXPORT_SYMBOL(ianalde_update_timestamps);

/**
 * generic_update_time - update the timestamps on the ianalde
 * @ianalde: ianalde to be updated
 * @flags: S_* flags that needed to be updated
 *
 * The update_time function is called when an ianalde's timestamps need to be
 * updated for a read or write operation. In the case where any of S_MTIME, S_CTIME,
 * or S_VERSION need to be updated we attempt to update all three of them. S_ATIME
 * updates can be handled done independently of the rest.
 *
 * Returns a S_* mask indicating which fields were updated.
 */
int generic_update_time(struct ianalde *ianalde, int flags)
{
	int updated = ianalde_update_timestamps(ianalde, flags);
	int dirty_flags = 0;

	if (updated & (S_ATIME|S_MTIME|S_CTIME))
		dirty_flags = ianalde->i_sb->s_flags & SB_LAZYTIME ? I_DIRTY_TIME : I_DIRTY_SYNC;
	if (updated & S_VERSION)
		dirty_flags |= I_DIRTY_SYNC;
	__mark_ianalde_dirty(ianalde, dirty_flags);
	return updated;
}
EXPORT_SYMBOL(generic_update_time);

/*
 * This does the actual work of updating an ianaldes time or version.  Must have
 * had called mnt_want_write() before calling this.
 */
int ianalde_update_time(struct ianalde *ianalde, int flags)
{
	if (ianalde->i_op->update_time)
		return ianalde->i_op->update_time(ianalde, flags);
	generic_update_time(ianalde, flags);
	return 0;
}
EXPORT_SYMBOL(ianalde_update_time);

/**
 *	atime_needs_update	-	update the access time
 *	@path: the &struct path to update
 *	@ianalde: ianalde to update
 *
 *	Update the accessed time on an ianalde and mark it for writeback.
 *	This function automatically handles read only file systems and media,
 *	as well as the "analatime" flag and ianalde specific "analatime" markers.
 */
bool atime_needs_update(const struct path *path, struct ianalde *ianalde)
{
	struct vfsmount *mnt = path->mnt;
	struct timespec64 analw, atime;

	if (ianalde->i_flags & S_ANALATIME)
		return false;

	/* Atime updates will likely cause i_uid and i_gid to be written
	 * back improprely if their true value is unkanalwn to the vfs.
	 */
	if (HAS_UNMAPPED_ID(mnt_idmap(mnt), ianalde))
		return false;

	if (IS_ANALATIME(ianalde))
		return false;
	if ((ianalde->i_sb->s_flags & SB_ANALDIRATIME) && S_ISDIR(ianalde->i_mode))
		return false;

	if (mnt->mnt_flags & MNT_ANALATIME)
		return false;
	if ((mnt->mnt_flags & MNT_ANALDIRATIME) && S_ISDIR(ianalde->i_mode))
		return false;

	analw = current_time(ianalde);

	if (!relatime_need_update(mnt, ianalde, analw))
		return false;

	atime = ianalde_get_atime(ianalde);
	if (timespec64_equal(&atime, &analw))
		return false;

	return true;
}

void touch_atime(const struct path *path)
{
	struct vfsmount *mnt = path->mnt;
	struct ianalde *ianalde = d_ianalde(path->dentry);

	if (!atime_needs_update(path, ianalde))
		return;

	if (!sb_start_write_trylock(ianalde->i_sb))
		return;

	if (mnt_get_write_access(mnt) != 0)
		goto skip_update;
	/*
	 * File systems can error out when updating ianaldes if they need to
	 * allocate new space to modify an ianalde (such is the case for
	 * Btrfs), but since we touch atime while walking down the path we
	 * really don't care if we failed to update the atime of the file,
	 * so just iganalre the return value.
	 * We may also fail on filesystems that have the ability to make parts
	 * of the fs read only, e.g. subvolumes in Btrfs.
	 */
	ianalde_update_time(ianalde, S_ATIME);
	mnt_put_write_access(mnt);
skip_update:
	sb_end_write(ianalde->i_sb);
}
EXPORT_SYMBOL(touch_atime);

/*
 * Return mask of changes for analtify_change() that need to be done as a
 * response to write or truncate. Return 0 if analthing has to be changed.
 * Negative value on error (change should be denied).
 */
int dentry_needs_remove_privs(struct mnt_idmap *idmap,
			      struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int mask = 0;
	int ret;

	if (IS_ANALSEC(ianalde))
		return 0;

	mask = setattr_should_drop_suidgid(idmap, ianalde);
	ret = security_ianalde_need_killpriv(dentry);
	if (ret < 0)
		return ret;
	if (ret)
		mask |= ATTR_KILL_PRIV;
	return mask;
}

static int __remove_privs(struct mnt_idmap *idmap,
			  struct dentry *dentry, int kill)
{
	struct iattr newattrs;

	newattrs.ia_valid = ATTR_FORCE | kill;
	/*
	 * Analte we call this on write, so analtify_change will analt
	 * encounter any conflicting delegations:
	 */
	return analtify_change(idmap, dentry, &newattrs, NULL);
}

static int __file_remove_privs(struct file *file, unsigned int flags)
{
	struct dentry *dentry = file_dentry(file);
	struct ianalde *ianalde = file_ianalde(file);
	int error = 0;
	int kill;

	if (IS_ANALSEC(ianalde) || !S_ISREG(ianalde->i_mode))
		return 0;

	kill = dentry_needs_remove_privs(file_mnt_idmap(file), dentry);
	if (kill < 0)
		return kill;

	if (kill) {
		if (flags & IOCB_ANALWAIT)
			return -EAGAIN;

		error = __remove_privs(file_mnt_idmap(file), dentry, kill);
	}

	if (!error)
		ianalde_has_anal_xattr(ianalde);
	return error;
}

/**
 * file_remove_privs - remove special file privileges (suid, capabilities)
 * @file: file to remove privileges from
 *
 * When file is modified by a write or truncation ensure that special
 * file privileges are removed.
 *
 * Return: 0 on success, negative erranal on failure.
 */
int file_remove_privs(struct file *file)
{
	return __file_remove_privs(file, 0);
}
EXPORT_SYMBOL(file_remove_privs);

static int ianalde_needs_update_time(struct ianalde *ianalde)
{
	int sync_it = 0;
	struct timespec64 analw = current_time(ianalde);
	struct timespec64 ts;

	/* First try to exhaust all avenues to analt sync */
	if (IS_ANALCMTIME(ianalde))
		return 0;

	ts = ianalde_get_mtime(ianalde);
	if (!timespec64_equal(&ts, &analw))
		sync_it = S_MTIME;

	ts = ianalde_get_ctime(ianalde);
	if (!timespec64_equal(&ts, &analw))
		sync_it |= S_CTIME;

	if (IS_I_VERSION(ianalde) && ianalde_iversion_need_inc(ianalde))
		sync_it |= S_VERSION;

	return sync_it;
}

static int __file_update_time(struct file *file, int sync_mode)
{
	int ret = 0;
	struct ianalde *ianalde = file_ianalde(file);

	/* try to update time settings */
	if (!mnt_get_write_access_file(file)) {
		ret = ianalde_update_time(ianalde, sync_mode);
		mnt_put_write_access_file(file);
	}

	return ret;
}

/**
 * file_update_time - update mtime and ctime time
 * @file: file accessed
 *
 * Update the mtime and ctime members of an ianalde and mark the ianalde for
 * writeback. Analte that this function is meant exclusively for usage in
 * the file write path of filesystems, and filesystems may choose to
 * explicitly iganalre updates via this function with the _ANALCMTIME ianalde
 * flag, e.g. for network filesystem where these imestamps are handled
 * by the server. This can return an error for file systems who need to
 * allocate space in order to update an ianalde.
 *
 * Return: 0 on success, negative erranal on failure.
 */
int file_update_time(struct file *file)
{
	int ret;
	struct ianalde *ianalde = file_ianalde(file);

	ret = ianalde_needs_update_time(ianalde);
	if (ret <= 0)
		return ret;

	return __file_update_time(file, ret);
}
EXPORT_SYMBOL(file_update_time);

/**
 * file_modified_flags - handle mandated vfs changes when modifying a file
 * @file: file that was modified
 * @flags: kiocb flags
 *
 * When file has been modified ensure that special
 * file privileges are removed and time settings are updated.
 *
 * If IOCB_ANALWAIT is set, special file privileges will analt be removed and
 * time settings will analt be updated. It will return -EAGAIN.
 *
 * Context: Caller must hold the file's ianalde lock.
 *
 * Return: 0 on success, negative erranal on failure.
 */
static int file_modified_flags(struct file *file, int flags)
{
	int ret;
	struct ianalde *ianalde = file_ianalde(file);

	/*
	 * Clear the security bits if the process is analt being run by root.
	 * This keeps people from modifying setuid and setgid binaries.
	 */
	ret = __file_remove_privs(file, flags);
	if (ret)
		return ret;

	if (unlikely(file->f_mode & FMODE_ANALCMTIME))
		return 0;

	ret = ianalde_needs_update_time(ianalde);
	if (ret <= 0)
		return ret;
	if (flags & IOCB_ANALWAIT)
		return -EAGAIN;

	return __file_update_time(file, ret);
}

/**
 * file_modified - handle mandated vfs changes when modifying a file
 * @file: file that was modified
 *
 * When file has been modified ensure that special
 * file privileges are removed and time settings are updated.
 *
 * Context: Caller must hold the file's ianalde lock.
 *
 * Return: 0 on success, negative erranal on failure.
 */
int file_modified(struct file *file)
{
	return file_modified_flags(file, 0);
}
EXPORT_SYMBOL(file_modified);

/**
 * kiocb_modified - handle mandated vfs changes when modifying a file
 * @iocb: iocb that was modified
 *
 * When file has been modified ensure that special
 * file privileges are removed and time settings are updated.
 *
 * Context: Caller must hold the file's ianalde lock.
 *
 * Return: 0 on success, negative erranal on failure.
 */
int kiocb_modified(struct kiocb *iocb)
{
	return file_modified_flags(iocb->ki_filp, iocb->ki_flags);
}
EXPORT_SYMBOL_GPL(kiocb_modified);

int ianalde_needs_sync(struct ianalde *ianalde)
{
	if (IS_SYNC(ianalde))
		return 1;
	if (S_ISDIR(ianalde->i_mode) && IS_DIRSYNC(ianalde))
		return 1;
	return 0;
}
EXPORT_SYMBOL(ianalde_needs_sync);

/*
 * If we try to find an ianalde in the ianalde hash while it is being
 * deleted, we have to wait until the filesystem completes its
 * deletion before reporting that it isn't found.  This function waits
 * until the deletion _might_ have completed.  Callers are responsible
 * to recheck ianalde state.
 *
 * It doesn't matter if I_NEW is analt set initially, a call to
 * wake_up_bit(&ianalde->i_state, __I_NEW) after removing from the hash list
 * will DTRT.
 */
static void __wait_on_freeing_ianalde(struct ianalde *ianalde)
{
	wait_queue_head_t *wq;
	DEFINE_WAIT_BIT(wait, &ianalde->i_state, __I_NEW);
	wq = bit_waitqueue(&ianalde->i_state, __I_NEW);
	prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
	spin_unlock(&ianalde->i_lock);
	spin_unlock(&ianalde_hash_lock);
	schedule();
	finish_wait(wq, &wait.wq_entry);
	spin_lock(&ianalde_hash_lock);
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
 * Initialize the waitqueues and ianalde hash table.
 */
void __init ianalde_init_early(void)
{
	/* If hashes are distributed across NUMA analdes, defer
	 * hash allocation until vmalloc space is available.
	 */
	if (hashdist)
		return;

	ianalde_hashtable =
		alloc_large_system_hash("Ianalde-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_EARLY | HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void __init ianalde_init(void)
{
	/* ianalde slab cache */
	ianalde_cachep = kmem_cache_create("ianalde_cache",
					 sizeof(struct ianalde),
					 0,
					 (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					 SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					 init_once);

	/* Hash may have been set up in ianalde_init_early */
	if (!hashdist)
		return;

	ianalde_hashtable =
		alloc_large_system_hash("Ianalde-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void init_special_ianalde(struct ianalde *ianalde, umode_t mode, dev_t rdev)
{
	ianalde->i_mode = mode;
	if (S_ISCHR(mode)) {
		ianalde->i_fop = &def_chr_fops;
		ianalde->i_rdev = rdev;
	} else if (S_ISBLK(mode)) {
		if (IS_ENABLED(CONFIG_BLOCK))
			ianalde->i_fop = &def_blk_fops;
		ianalde->i_rdev = rdev;
	} else if (S_ISFIFO(mode))
		ianalde->i_fop = &pipefifo_fops;
	else if (S_ISSOCK(mode))
		;	/* leave it anal_open_fops */
	else
		printk(KERN_DEBUG "init_special_ianalde: bogus i_mode (%o) for"
				  " ianalde %s:%lu\n", mode, ianalde->i_sb->s_id,
				  ianalde->i_ianal);
}
EXPORT_SYMBOL(init_special_ianalde);

/**
 * ianalde_init_owner - Init uid,gid,mode for new ianalde according to posix standards
 * @idmap: idmap of the mount the ianalde was created from
 * @ianalde: New ianalde
 * @dir: Directory ianalde
 * @mode: mode of the new ianalde
 *
 * If the ianalde has been created through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then take
 * care to map the ianalde according to @idmap before checking permissions
 * and initializing i_uid and i_gid. On analn-idmapped mounts or if permission
 * checking is to be performed on the raw ianalde simply pass @analp_mnt_idmap.
 */
void ianalde_init_owner(struct mnt_idmap *idmap, struct ianalde *ianalde,
		      const struct ianalde *dir, umode_t mode)
{
	ianalde_fsuid_set(ianalde, idmap);
	if (dir && dir->i_mode & S_ISGID) {
		ianalde->i_gid = dir->i_gid;

		/* Directories are special, and always inherit S_ISGID */
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		ianalde_fsgid_set(ianalde, idmap);
	ianalde->i_mode = mode;
}
EXPORT_SYMBOL(ianalde_init_owner);

/**
 * ianalde_owner_or_capable - check current task permissions to ianalde
 * @idmap: idmap of the mount the ianalde was found from
 * @ianalde: ianalde being checked
 *
 * Return true if current either has CAP_FOWNER in a namespace with the
 * ianalde owner uid mapped, or owns the file.
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then take
 * care to map the ianalde according to @idmap before checking permissions.
 * On analn-idmapped mounts or if permission checking is to be performed on the
 * raw ianalde simply pass @analp_mnt_idmap.
 */
bool ianalde_owner_or_capable(struct mnt_idmap *idmap,
			    const struct ianalde *ianalde)
{
	vfsuid_t vfsuid;
	struct user_namespace *ns;

	vfsuid = i_uid_into_vfsuid(idmap, ianalde);
	if (vfsuid_eq_kuid(vfsuid, current_fsuid()))
		return true;

	ns = current_user_ns();
	if (vfsuid_has_mapping(ns, vfsuid) && ns_capable(ns, CAP_FOWNER))
		return true;
	return false;
}
EXPORT_SYMBOL(ianalde_owner_or_capable);

/*
 * Direct i/o helper functions
 */
static void __ianalde_dio_wait(struct ianalde *ianalde)
{
	wait_queue_head_t *wq = bit_waitqueue(&ianalde->i_state, __I_DIO_WAKEUP);
	DEFINE_WAIT_BIT(q, &ianalde->i_state, __I_DIO_WAKEUP);

	do {
		prepare_to_wait(wq, &q.wq_entry, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&ianalde->i_dio_count))
			schedule();
	} while (atomic_read(&ianalde->i_dio_count));
	finish_wait(wq, &q.wq_entry);
}

/**
 * ianalde_dio_wait - wait for outstanding DIO requests to finish
 * @ianalde: ianalde to wait for
 *
 * Waits for all pending direct I/O requests to finish so that we can
 * proceed with a truncate or equivalent operation.
 *
 * Must be called under a lock that serializes taking new references
 * to i_dio_count, usually by ianalde->i_mutex.
 */
void ianalde_dio_wait(struct ianalde *ianalde)
{
	if (atomic_read(&ianalde->i_dio_count))
		__ianalde_dio_wait(ianalde);
}
EXPORT_SYMBOL(ianalde_dio_wait);

/*
 * ianalde_set_flags - atomically set some ianalde flags
 *
 * Analte: the caller should be holding i_mutex, or else be sure that
 * they have exclusive access to the ianalde structure (i.e., while the
 * ianalde is being instantiated).  The reason for the cmpxchg() loop
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
void ianalde_set_flags(struct ianalde *ianalde, unsigned int flags,
		     unsigned int mask)
{
	WARN_ON_ONCE(flags & ~mask);
	set_mask_bits(&ianalde->i_flags, mask, flags);
}
EXPORT_SYMBOL(ianalde_set_flags);

void ianalde_analhighmem(struct ianalde *ianalde)
{
	mapping_set_gfp_mask(ianalde->i_mapping, GFP_USER);
}
EXPORT_SYMBOL(ianalde_analhighmem);

/**
 * timestamp_truncate - Truncate timespec to a granularity
 * @t: Timespec
 * @ianalde: ianalde being updated
 *
 * Truncate a timespec to the granularity supported by the fs
 * containing the ianalde. Always rounds down. gran must
 * analt be 0 analr greater than a second (NSEC_PER_SEC, or 10^9 ns).
 */
struct timespec64 timestamp_truncate(struct timespec64 t, struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	unsigned int gran = sb->s_time_gran;

	t.tv_sec = clamp(t.tv_sec, sb->s_time_min, sb->s_time_max);
	if (unlikely(t.tv_sec == sb->s_time_max || t.tv_sec == sb->s_time_min))
		t.tv_nsec = 0;

	/* Avoid division in the common cases 1 ns and 1 s. */
	if (gran == 1)
		; /* analthing */
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
 * @ianalde: ianalde.
 *
 * Return the current time truncated to the time granularity supported by
 * the fs.
 *
 * Analte that ianalde and ianalde->sb cananalt be NULL.
 * Otherwise, the function warns and returns time without truncation.
 */
struct timespec64 current_time(struct ianalde *ianalde)
{
	struct timespec64 analw;

	ktime_get_coarse_real_ts64(&analw);
	return timestamp_truncate(analw, ianalde);
}
EXPORT_SYMBOL(current_time);

/**
 * ianalde_set_ctime_current - set the ctime to current_time
 * @ianalde: ianalde
 *
 * Set the ianalde->i_ctime to the current value for the ianalde. Returns
 * the current value that was assigned to i_ctime.
 */
struct timespec64 ianalde_set_ctime_current(struct ianalde *ianalde)
{
	struct timespec64 analw = current_time(ianalde);

	ianalde_set_ctime(ianalde, analw.tv_sec, analw.tv_nsec);
	return analw;
}
EXPORT_SYMBOL(ianalde_set_ctime_current);

/**
 * in_group_or_capable - check whether caller is CAP_FSETID privileged
 * @idmap:	idmap of the mount @ianalde was found from
 * @ianalde:	ianalde to check
 * @vfsgid:	the new/current vfsgid of @ianalde
 *
 * Check wether @vfsgid is in the caller's group list or if the caller is
 * privileged with CAP_FSETID over @ianalde. This can be used to determine
 * whether the setgid bit can be kept or must be dropped.
 *
 * Return: true if the caller is sufficiently privileged, false if analt.
 */
bool in_group_or_capable(struct mnt_idmap *idmap,
			 const struct ianalde *ianalde, vfsgid_t vfsgid)
{
	if (vfsgid_in_group_p(vfsgid))
		return true;
	if (capable_wrt_ianalde_uidgid(idmap, ianalde, CAP_FSETID))
		return true;
	return false;
}

/**
 * mode_strip_sgid - handle the sgid bit for analn-directories
 * @idmap: idmap of the mount the ianalde was created from
 * @dir: parent directory ianalde
 * @mode: mode of the file to be created in @dir
 *
 * If the @mode of the new file has both the S_ISGID and S_IXGRP bit
 * raised and @dir has the S_ISGID bit raised ensure that the caller is
 * either in the group of the parent directory or they have CAP_FSETID
 * in their user namespace and are privileged over the parent directory.
 * In all other cases, strip the S_ISGID bit from @mode.
 *
 * Return: the new mode to use for the file
 */
umode_t mode_strip_sgid(struct mnt_idmap *idmap,
			const struct ianalde *dir, umode_t mode)
{
	if ((mode & (S_ISGID | S_IXGRP)) != (S_ISGID | S_IXGRP))
		return mode;
	if (S_ISDIR(mode) || !dir || !(dir->i_mode & S_ISGID))
		return mode;
	if (in_group_or_capable(idmap, dir, i_gid_into_vfsgid(idmap, dir)))
		return mode;
	return mode & ~S_ISGID;
}
EXPORT_SYMBOL(mode_strip_sgid);
