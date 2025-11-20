// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 1997 Linus Torvalds
 * (C) 1999 Andrea Arcangeli <andrea@suse.de> (dynamic inode allocation)
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
#include <linux/fsnotify.h>
#include <linux/mount.h>
#include <linux/posix_acl.h>
#include <linux/buffer_head.h> /* for inode_has_buffers */
#include <linux/ratelimit.h>
#include <linux/list_lru.h>
#include <linux/iversion.h>
#include <linux/rw_hint.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <trace/events/writeback.h>
#define CREATE_TRACE_POINTS
#include <trace/events/timestamp.h>

#include "internal.h"

/*
 * Inode locking rules:
 *
 * inode->i_lock protects:
 *   inode->i_state, inode->i_hash, __iget(), inode->i_io_list
 * Inode LRU list locks protect:
 *   inode->i_sb->s_inode_lru, inode->i_lru
 * inode->i_sb->s_inode_list_lock protects:
 *   inode->i_sb->s_inodes, inode->i_sb_list
 * bdi->wb.list_lock protects:
 *   bdi->wb.b_{dirty,io,more_io,dirty_time}, inode->i_io_list
 * inode_hash_lock protects:
 *   inode_hashtable, inode->i_hash
 *
 * Lock ordering:
 *
 * inode->i_sb->s_inode_list_lock
 *   inode->i_lock
 *     Inode LRU list locks
 *
 * bdi->wb.list_lock
 *   inode->i_lock
 *
 * inode_hash_lock
 *   inode->i_sb->s_inode_list_lock
 *   inode->i_lock
 *
 * iunique_lock
 *   inode_hash_lock
 */

static unsigned int i_hash_mask __ro_after_init;
static unsigned int i_hash_shift __ro_after_init;
static struct hlist_head *inode_hashtable __ro_after_init;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(inode_hash_lock);

/*
 * Empty aops. Can be used for the cases where the user does not
 * define any of the address_space operations.
 */
const struct address_space_operations empty_aops = {
};
EXPORT_SYMBOL(empty_aops);

static DEFINE_PER_CPU(unsigned long, nr_inodes);
static DEFINE_PER_CPU(unsigned long, nr_unused);

static struct kmem_cache *inode_cachep __ro_after_init;

static long get_nr_inodes(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_inodes, i);
	return sum < 0 ? 0 : sum;
}

static inline long get_nr_inodes_unused(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_unused, i);
	return sum < 0 ? 0 : sum;
}

long get_nr_dirty_inodes(void)
{
	/* not actually dirty inodes, but a wild approximation */
	long nr_dirty = get_nr_inodes() - get_nr_inodes_unused();
	return nr_dirty > 0 ? nr_dirty : 0;
}

#ifdef CONFIG_DEBUG_FS
static DEFINE_PER_CPU(long, mg_ctime_updates);
static DEFINE_PER_CPU(long, mg_fine_stamps);
static DEFINE_PER_CPU(long, mg_ctime_swaps);

static unsigned long get_mg_ctime_updates(void)
{
	unsigned long sum = 0;
	int i;

	for_each_possible_cpu(i)
		sum += data_race(per_cpu(mg_ctime_updates, i));
	return sum;
}

static unsigned long get_mg_fine_stamps(void)
{
	unsigned long sum = 0;
	int i;

	for_each_possible_cpu(i)
		sum += data_race(per_cpu(mg_fine_stamps, i));
	return sum;
}

static unsigned long get_mg_ctime_swaps(void)
{
	unsigned long sum = 0;
	int i;

	for_each_possible_cpu(i)
		sum += data_race(per_cpu(mg_ctime_swaps, i));
	return sum;
}

#define mgtime_counter_inc(__var)	this_cpu_inc(__var)

static int mgts_show(struct seq_file *s, void *p)
{
	unsigned long ctime_updates = get_mg_ctime_updates();
	unsigned long ctime_swaps = get_mg_ctime_swaps();
	unsigned long fine_stamps = get_mg_fine_stamps();
	unsigned long floor_swaps = timekeeping_get_mg_floor_swaps();

	seq_printf(s, "%lu %lu %lu %lu\n",
		   ctime_updates, ctime_swaps, fine_stamps, floor_swaps);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mgts);

static int __init mg_debugfs_init(void)
{
	debugfs_create_file("multigrain_timestamps", S_IFREG | S_IRUGO, NULL, NULL, &mgts_fops);
	return 0;
}
late_initcall(mg_debugfs_init);

#else /* ! CONFIG_DEBUG_FS */

#define mgtime_counter_inc(__var)	do { } while (0)

#endif /* CONFIG_DEBUG_FS */

/*
 * Handle nr_inode sysctl
 */
#ifdef CONFIG_SYSCTL
/*
 * Statistics gathering..
 */
static struct inodes_stat_t inodes_stat;

static int proc_nr_inodes(const struct ctl_table *table, int write, void *buffer,
			  size_t *lenp, loff_t *ppos)
{
	inodes_stat.nr_inodes = get_nr_inodes();
	inodes_stat.nr_unused = get_nr_inodes_unused();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}

static const struct ctl_table inodes_sysctls[] = {
	{
		.procname	= "inode-nr",
		.data		= &inodes_stat,
		.maxlen		= 2*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_inodes,
	},
	{
		.procname	= "inode-state",
		.data		= &inodes_stat,
		.maxlen		= 7*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_inodes,
	},
};

static int __init init_fs_inode_sysctls(void)
{
	register_sysctl_init("fs", inodes_sysctls);
	return 0;
}
early_initcall(init_fs_inode_sysctls);
#endif

static int no_open(struct inode *inode, struct file *file)
{
	return -ENXIO;
}

/**
 * inode_init_always_gfp - perform inode structure initialisation
 * @sb: superblock inode belongs to
 * @inode: inode to initialise
 * @gfp: allocation flags
 *
 * These are initializations that need to be done on every inode
 * allocation as the fields are not initialised by slab allocation.
 * If there are additional allocations required @gfp is used.
 */
int inode_init_always_gfp(struct super_block *sb, struct inode *inode, gfp_t gfp)
{
	static const struct inode_operations empty_iops;
	static const struct file_operations no_open_fops = {.open = no_open};
	struct address_space *const mapping = &inode->i_data;

	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags = 0;
	inode->i_state = 0;
	atomic64_set(&inode->i_sequence, 0);
	atomic_set(&inode->i_count, 1);
	inode->i_op = &empty_iops;
	inode->i_fop = &no_open_fops;
	inode->i_ino = 0;
	inode->__i_nlink = 1;
	inode->i_opflags = 0;
	if (sb->s_xattr)
		inode->i_opflags |= IOP_XATTR;
	if (sb->s_type->fs_flags & FS_MGTIME)
		inode->i_opflags |= IOP_MGTIME;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_write_hint = WRITE_LIFE_NOT_SET;
	inode->i_blocks = 0;
	inode->i_bytes = 0;
	inode->i_generation = 0;
	inode->i_pipe = NULL;
	inode->i_cdev = NULL;
	inode->i_link = NULL;
	inode->i_dir_seq = 0;
	inode->i_rdev = 0;
	inode->dirtied_when = 0;

#ifdef CONFIG_CGROUP_WRITEBACK
	inode->i_wb_frn_winner = 0;
	inode->i_wb_frn_avg_time = 0;
	inode->i_wb_frn_history = 0;
#endif

	spin_lock_init(&inode->i_lock);
	lockdep_set_class(&inode->i_lock, &sb->s_type->i_lock_key);

	init_rwsem(&inode->i_rwsem);
	lockdep_set_class(&inode->i_rwsem, &sb->s_type->i_mutex_key);

	atomic_set(&inode->i_dio_count, 0);

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
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
	inode->i_private = NULL;
	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);	/* buggered by rcu freeing */
#ifdef CONFIG_FS_POSIX_ACL
	inode->i_acl = inode->i_default_acl = ACL_NOT_CACHED;
#endif

#ifdef CONFIG_FSNOTIFY
	inode->i_fsnotify_mask = 0;
#endif
	inode->i_flctx = NULL;

	if (unlikely(security_inode_alloc(inode, gfp)))
		return -ENOMEM;

	this_cpu_inc(nr_inodes);

	return 0;
}
EXPORT_SYMBOL(inode_init_always_gfp);

void free_inode_nonrcu(struct inode *inode)
{
	kmem_cache_free(inode_cachep, inode);
}
EXPORT_SYMBOL(free_inode_nonrcu);

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	if (inode->free_inode)
		inode->free_inode(inode);
	else
		free_inode_nonrcu(inode);
}

/**
 *	alloc_inode 	- obtain an inode
 *	@sb: superblock
 *
 *	Allocates a new inode for given superblock.
 *	Inode wont be chained in superblock s_inodes list
 *	This means :
 *	- fs can't be unmount
 *	- quotas, fsnotify, writeback can't work
 */
struct inode *alloc_inode(struct super_block *sb)
{
	const struct super_operations *ops = sb->s_op;
	struct inode *inode;

	if (ops->alloc_inode)
		inode = ops->alloc_inode(sb);
	else
		inode = alloc_inode_sb(sb, inode_cachep, GFP_KERNEL);

	if (!inode)
		return NULL;

	if (unlikely(inode_init_always(sb, inode))) {
		if (ops->destroy_inode) {
			ops->destroy_inode(inode);
			if (!ops->free_inode)
				return NULL;
		}
		inode->free_inode = ops->free_inode;
		i_callback(&inode->i_rcu);
		return NULL;
	}

	return inode;
}

void __destroy_inode(struct inode *inode)
{
	BUG_ON(inode_has_buffers(inode));
	inode_detach_wb(inode);
	security_inode_free(inode);
	fsnotify_inode_delete(inode);
	locks_free_lock_context(inode);
	if (!inode->i_nlink) {
		WARN_ON(atomic_long_read(&inode->i_sb->s_remove_count) == 0);
		atomic_long_dec(&inode->i_sb->s_remove_count);
	}

#ifdef CONFIG_FS_POSIX_ACL
	if (inode->i_acl && !is_uncached_acl(inode->i_acl))
		posix_acl_release(inode->i_acl);
	if (inode->i_default_acl && !is_uncached_acl(inode->i_default_acl))
		posix_acl_release(inode->i_default_acl);
#endif
	this_cpu_dec(nr_inodes);
}
EXPORT_SYMBOL(__destroy_inode);

static void destroy_inode(struct inode *inode)
{
	const struct super_operations *ops = inode->i_sb->s_op;

	BUG_ON(!list_empty(&inode->i_lru));
	__destroy_inode(inode);
	if (ops->destroy_inode) {
		ops->destroy_inode(inode);
		if (!ops->free_inode)
			return;
	}
	inode->free_inode = ops->free_inode;
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
	if (unlikely(inode->i_nlink == 0)) {
		WARN_ON(!(inode->i_state & I_LINKABLE));
		atomic_long_dec(&inode->i_sb->s_remove_count);
	}

	inode->__i_nlink++;
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
 * of the inode, so let the slab aware of that.
 */
void inode_init_once(struct inode *inode)
{
	memset(inode, 0, sizeof(*inode));
	INIT_HLIST_NODE(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_devices);
	INIT_LIST_HEAD(&inode->i_io_list);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	INIT_LIST_HEAD(&inode->i_sb_list);
	__address_space_init_once(&inode->i_data);
	i_size_ordered_init(inode);
}
EXPORT_SYMBOL(inode_init_once);

static void init_once(void *foo)
{
	struct inode *inode = (struct inode *) foo;

	inode_init_once(inode);
}

/*
 * get additional reference to inode; caller must already hold one.
 */
void ihold(struct inode *inode)
{
	WARN_ON(atomic_inc_return(&inode->i_count) < 2);
}
EXPORT_SYMBOL(ihold);

static void __inode_add_lru(struct inode *inode, bool rotate)
{
	if (inode->i_state & (I_DIRTY_ALL | I_SYNC | I_FREEING | I_WILL_FREE))
		return;
	if (icount_read(inode))
		return;
	if (!(inode->i_sb->s_flags & SB_ACTIVE))
		return;
	if (!mapping_shrinkable(&inode->i_data))
		return;

	if (list_lru_add_obj(&inode->i_sb->s_inode_lru, &inode->i_lru))
		this_cpu_inc(nr_unused);
	else if (rotate)
		inode->i_state |= I_REFERENCED;
}

struct wait_queue_head *inode_bit_waitqueue(struct wait_bit_queue_entry *wqe,
					    struct inode *inode, u32 bit)
{
	void *bit_address;

	bit_address = inode_state_wait_address(inode, bit);
	init_wait_var_entry(wqe, bit_address, 0);
	return __var_waitqueue(bit_address);
}
EXPORT_SYMBOL(inode_bit_waitqueue);

/*
 * Add inode to LRU if needed (inode is unused and clean).
 *
 * Needs inode->i_lock held.
 */
void inode_add_lru(struct inode *inode)
{
	__inode_add_lru(inode, false);
}

static void inode_lru_list_del(struct inode *inode)
{
	if (list_lru_del_obj(&inode->i_sb->s_inode_lru, &inode->i_lru))
		this_cpu_dec(nr_unused);
}

static void inode_pin_lru_isolating(struct inode *inode)
{
	lockdep_assert_held(&inode->i_lock);
	WARN_ON(inode->i_state & (I_LRU_ISOLATING | I_FREEING | I_WILL_FREE));
	inode->i_state |= I_LRU_ISOLATING;
}

static void inode_unpin_lru_isolating(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	WARN_ON(!(inode->i_state & I_LRU_ISOLATING));
	inode->i_state &= ~I_LRU_ISOLATING;
	/* Called with inode->i_lock which ensures memory ordering. */
	inode_wake_up_bit(inode, __I_LRU_ISOLATING);
	spin_unlock(&inode->i_lock);
}

static void inode_wait_for_lru_isolating(struct inode *inode)
{
	struct wait_bit_queue_entry wqe;
	struct wait_queue_head *wq_head;

	lockdep_assert_held(&inode->i_lock);
	if (!(inode->i_state & I_LRU_ISOLATING))
		return;

	wq_head = inode_bit_waitqueue(&wqe, inode, __I_LRU_ISOLATING);
	for (;;) {
		prepare_to_wait_event(wq_head, &wqe.wq_entry, TASK_UNINTERRUPTIBLE);
		/*
		 * Checking I_LRU_ISOLATING with inode->i_lock guarantees
		 * memory ordering.
		 */
		if (!(inode->i_state & I_LRU_ISOLATING))
			break;
		spin_unlock(&inode->i_lock);
		schedule();
		spin_lock(&inode->i_lock);
	}
	finish_wait(wq_head, &wqe.wq_entry);
	WARN_ON(inode->i_state & I_LRU_ISOLATING);
}

/**
 * inode_sb_list_add - add inode to the superblock list of inodes
 * @inode: inode to add
 */
void inode_sb_list_add(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	spin_lock(&sb->s_inode_list_lock);
	list_add(&inode->i_sb_list, &sb->s_inodes);
	spin_unlock(&sb->s_inode_list_lock);
}
EXPORT_SYMBOL_GPL(inode_sb_list_add);

static inline void inode_sb_list_del(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (!list_empty(&inode->i_sb_list)) {
		spin_lock(&sb->s_inode_list_lock);
		list_del_init(&inode->i_sb_list);
		spin_unlock(&sb->s_inode_list_lock);
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
	hlist_add_head_rcu(&inode->i_hash, b);
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
	hlist_del_init_rcu(&inode->i_hash);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
}
EXPORT_SYMBOL(__remove_inode_hash);

void dump_mapping(const struct address_space *mapping)
{
	struct inode *host;
	const struct address_space_operations *a_ops;
	struct hlist_node *dentry_first;
	struct dentry *dentry_ptr;
	struct dentry dentry;
	char fname[64] = {};
	unsigned long ino;

	/*
	 * If mapping is an invalid pointer, we don't want to crash
	 * accessing it, so probe everything depending on it carefully.
	 */
	if (get_kernel_nofault(host, &mapping->host) ||
	    get_kernel_nofault(a_ops, &mapping->a_ops)) {
		pr_warn("invalid mapping:%px\n", mapping);
		return;
	}

	if (!host) {
		pr_warn("aops:%ps\n", a_ops);
		return;
	}

	if (get_kernel_nofault(dentry_first, &host->i_dentry.first) ||
	    get_kernel_nofault(ino, &host->i_ino)) {
		pr_warn("aops:%ps invalid inode:%px\n", a_ops, host);
		return;
	}

	if (!dentry_first) {
		pr_warn("aops:%ps ino:%lx\n", a_ops, ino);
		return;
	}

	dentry_ptr = container_of(dentry_first, struct dentry, d_u.d_alias);
	if (get_kernel_nofault(dentry, dentry_ptr) ||
	    !dentry.d_parent || !dentry.d_name.name) {
		pr_warn("aops:%ps ino:%lx invalid dentry:%px\n",
				a_ops, ino, dentry_ptr);
		return;
	}

	if (strncpy_from_kernel_nofault(fname, dentry.d_name.name, 63) < 0)
		strscpy(fname, "<invalid>");
	/*
	 * Even if strncpy_from_kernel_nofault() succeeded,
	 * the fname could be unreliable
	 */
	pr_warn("aops:%ps ino:%lx dentry name(?):\"%s\"\n",
		a_ops, ino, fname);
}

void clear_inode(struct inode *inode)
{
	/*
	 * We have to cycle the i_pages lock here because reclaim can be in the
	 * process of removing the last page (in __filemap_remove_folio())
	 * and we must not free the mapping under it.
	 */
	xa_lock_irq(&inode->i_data.i_pages);
	BUG_ON(inode->i_data.nrpages);
	/*
	 * Almost always, mapping_empty(&inode->i_data) here; but there are
	 * two known and long-standing ways in which nodes may get left behind
	 * (when deep radix-tree node allocation failed partway; or when THP
	 * collapse_file() failed). Until those two known cases are cleaned up,
	 * or a cleanup function is called here, do not BUG_ON(!mapping_empty),
	 * nor even WARN_ON(!mapping_empty).
	 */
	xa_unlock_irq(&inode->i_data.i_pages);
	BUG_ON(!list_empty(&inode->i_data.i_private_list));
	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(inode->i_state & I_CLEAR);
	BUG_ON(!list_empty(&inode->i_wb_list));
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

	if (!list_empty(&inode->i_io_list))
		inode_io_list_del(inode);

	inode_sb_list_del(inode);

	spin_lock(&inode->i_lock);
	inode_wait_for_lru_isolating(inode);

	/*
	 * Wait for flusher thread to be done with the inode so that filesystem
	 * does not start destroying it while writeback is still running. Since
	 * the inode has I_FREEING set, flusher thread won't start new work on
	 * the inode.  We just have to wait for running writeback to finish.
	 */
	inode_wait_for_writeback(inode);
	spin_unlock(&inode->i_lock);

	if (op->evict_inode) {
		op->evict_inode(inode);
	} else {
		truncate_inode_pages_final(&inode->i_data);
		clear_inode(inode);
	}
	if (S_ISCHR(inode->i_mode) && inode->i_cdev)
		cd_forget(inode);

	remove_inode_hash(inode);

	/*
	 * Wake up waiters in __wait_on_freeing_inode().
	 *
	 * It is an invariant that any thread we need to wake up is already
	 * accounted for before remove_inode_hash() acquires ->i_lock -- both
	 * sides take the lock and sleep is aborted if the inode is found
	 * unhashed. Thus either the sleeper wins and goes off CPU, or removal
	 * wins and the sleeper aborts after testing with the lock.
	 *
	 * This also means we don't need any fences for the call below.
	 */
	inode_wake_up_bit(inode, __I_NEW);
	BUG_ON(inode->i_state != (I_FREEING | I_CLEAR));

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
		cond_resched();
	}
}

/**
 * evict_inodes	- evict all evictable inodes for a superblock
 * @sb:		superblock to operate on
 *
 * Make sure that no inodes with zero refcount are retained.  This is
 * called by superblock shutdown after having SB_ACTIVE flag removed,
 * so any inode reaching zero refcount during or after that call will
 * be immediately evicted.
 */
void evict_inodes(struct super_block *sb)
{
	struct inode *inode;
	LIST_HEAD(dispose);

again:
	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		if (icount_read(inode))
			continue;

		spin_lock(&inode->i_lock);
		if (icount_read(inode)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (inode->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		inode->i_state |= I_FREEING;
		inode_lru_list_del(inode);
		spin_unlock(&inode->i_lock);
		list_add(&inode->i_lru, &dispose);

		/*
		 * We can have a ton of inodes to evict at unmount time given
		 * enough memory, check to see if we need to go to sleep for a
		 * bit so we don't livelock.
		 */
		if (need_resched()) {
			spin_unlock(&sb->s_inode_list_lock);
			cond_resched();
			dispose_list(&dispose);
			goto again;
		}
	}
	spin_unlock(&sb->s_inode_list_lock);

	dispose_list(&dispose);
}
EXPORT_SYMBOL_GPL(evict_inodes);

/*
 * Isolate the inode from the LRU in preparation for freeing it.
 *
 * If the inode has the I_REFERENCED flag set, then it means that it has been
 * used recently - the flag is set in iput_final(). When we encounter such an
 * inode, clear the flag and move it to the back of the LRU so it gets another
 * pass through the LRU before it gets reclaimed. This is necessary because of
 * the fact we are doing lazy LRU updates to minimise lock contention so the
 * LRU does not have strict ordering. Hence we don't want to reclaim inodes
 * with this flag set because they are the inodes that are out of order.
 */
static enum lru_status inode_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, void *arg)
{
	struct list_head *freeable = arg;
	struct inode	*inode = container_of(item, struct inode, i_lru);

	/*
	 * We are inverting the lru lock/inode->i_lock here, so use a
	 * trylock. If we fail to get the lock, just skip it.
	 */
	if (!spin_trylock(&inode->i_lock))
		return LRU_SKIP;

	/*
	 * Inodes can get referenced, redirtied, or repopulated while
	 * they're already on the LRU, and this can make them
	 * unreclaimable for a while. Remove them lazily here; iput,
	 * sync, or the last page cache deletion will requeue them.
	 */
	if (icount_read(inode) ||
	    (inode->i_state & ~I_REFERENCED) ||
	    !mapping_shrinkable(&inode->i_data)) {
		list_lru_isolate(lru, &inode->i_lru);
		spin_unlock(&inode->i_lock);
		this_cpu_dec(nr_unused);
		return LRU_REMOVED;
	}

	/* Recently referenced inodes get one more pass */
	if (inode->i_state & I_REFERENCED) {
		inode->i_state &= ~I_REFERENCED;
		spin_unlock(&inode->i_lock);
		return LRU_ROTATE;
	}

	/*
	 * On highmem systems, mapping_shrinkable() permits dropping
	 * page cache in order to free up struct inodes: lowmem might
	 * be under pressure before the cache inside the highmem zone.
	 */
	if (inode_has_buffers(inode) || !mapping_empty(&inode->i_data)) {
		inode_pin_lru_isolating(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&lru->lock);
		if (remove_inode_buffers(inode)) {
			unsigned long reap;
			reap = invalidate_mapping_pages(&inode->i_data, 0, -1);
			if (current_is_kswapd())
				__count_vm_events(KSWAPD_INODESTEAL, reap);
			else
				__count_vm_events(PGINODESTEAL, reap);
			mm_account_reclaimed_pages(reap);
		}
		inode_unpin_lru_isolating(inode);
		return LRU_RETRY;
	}

	WARN_ON(inode->i_state & I_NEW);
	inode->i_state |= I_FREEING;
	list_lru_isolate_move(lru, &inode->i_lru, freeable);
	spin_unlock(&inode->i_lock);

	this_cpu_dec(nr_unused);
	return LRU_REMOVED;
}

/*
 * Walk the superblock inode LRU for freeable inodes and attempt to free them.
 * This is called from the superblock shrinker function with a number of inodes
 * to trim from the LRU. Inodes to be freed are moved to a temporary list and
 * then are freed outside inode_lock by dispose_list().
 */
long prune_icache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(freeable);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_inode_lru, sc,
				     inode_lru_isolate, &freeable);
	dispose_list(&freeable);
	return freed;
}

static void __wait_on_freeing_inode(struct inode *inode, bool is_inode_hash_locked);
/*
 * Called with the inode lock held.
 */
static struct inode *find_inode(struct super_block *sb,
				struct hlist_head *head,
				int (*test)(struct inode *, void *),
				void *data, bool is_inode_hash_locked)
{
	struct inode *inode = NULL;

	if (is_inode_hash_locked)
		lockdep_assert_held(&inode_hash_lock);
	else
		lockdep_assert_not_held(&inode_hash_lock);

	rcu_read_lock();
repeat:
	hlist_for_each_entry_rcu(inode, head, i_hash) {
		if (inode->i_sb != sb)
			continue;
		if (!test(inode, data))
			continue;
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_inode(inode, is_inode_hash_locked);
			goto repeat;
		}
		if (unlikely(inode->i_state & I_CREATING)) {
			spin_unlock(&inode->i_lock);
			rcu_read_unlock();
			return ERR_PTR(-ESTALE);
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		rcu_read_unlock();
		return inode;
	}
	rcu_read_unlock();
	return NULL;
}

/*
 * find_inode_fast is the fast path version of find_inode, see the comment at
 * iget_locked for details.
 */
static struct inode *find_inode_fast(struct super_block *sb,
				struct hlist_head *head, unsigned long ino,
				bool is_inode_hash_locked)
{
	struct inode *inode = NULL;

	if (is_inode_hash_locked)
		lockdep_assert_held(&inode_hash_lock);
	else
		lockdep_assert_not_held(&inode_hash_lock);

	rcu_read_lock();
repeat:
	hlist_for_each_entry_rcu(inode, head, i_hash) {
		if (inode->i_ino != ino)
			continue;
		if (inode->i_sb != sb)
			continue;
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_inode(inode, is_inode_hash_locked);
			goto repeat;
		}
		if (unlikely(inode->i_state & I_CREATING)) {
			spin_unlock(&inode->i_lock);
			rcu_read_unlock();
			return ERR_PTR(-ESTALE);
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		rcu_read_unlock();
		return inode;
	}
	rcu_read_unlock();
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

	res++;
	/* get_next_ino should not provide a 0 inode number */
	if (unlikely(!res))
		res++;
	*p = res;
	put_cpu_var(last_ino);
	return res;
}
EXPORT_SYMBOL(get_next_ino);

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

	inode = alloc_inode(sb);
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
		if (lockdep_match_class(&inode->i_rwsem, &type->i_mutex_key)) {
			/*
			 * ensure nobody is actually holding i_rwsem
			 */
			init_rwsem(&inode->i_rwsem);
			lockdep_set_class(&inode->i_rwsem,
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
	inode->i_state &= ~I_NEW & ~I_CREATING;
	/*
	 * Pairs with the barrier in prepare_to_wait_event() to make sure
	 * ___wait_var_event() either sees the bit cleared or
	 * waitqueue_active() check in wake_up_var() sees the waiter.
	 */
	smp_mb();
	inode_wake_up_bit(inode, __I_NEW);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(unlock_new_inode);

void discard_new_inode(struct inode *inode)
{
	lockdep_annotate_inode_mutex_key(inode);
	spin_lock(&inode->i_lock);
	WARN_ON(!(inode->i_state & I_NEW));
	inode->i_state &= ~I_NEW;
	/*
	 * Pairs with the barrier in prepare_to_wait_event() to make sure
	 * ___wait_var_event() either sees the bit cleared or
	 * waitqueue_active() check in wake_up_var() sees the waiter.
	 */
	smp_mb();
	inode_wake_up_bit(inode, __I_NEW);
	spin_unlock(&inode->i_lock);
	iput(inode);
}
EXPORT_SYMBOL(discard_new_inode);

/**
 * lock_two_nondirectories - take two i_mutexes on non-directory objects
 *
 * Lock any non-NULL argument. Passed objects must not be directories.
 * Zero, one or two objects may be locked by this function.
 *
 * @inode1: first inode to lock
 * @inode2: second inode to lock
 */
void lock_two_nondirectories(struct inode *inode1, struct inode *inode2)
{
	if (inode1)
		WARN_ON_ONCE(S_ISDIR(inode1->i_mode));
	if (inode2)
		WARN_ON_ONCE(S_ISDIR(inode2->i_mode));
	if (inode1 > inode2)
		swap(inode1, inode2);
	if (inode1)
		inode_lock(inode1);
	if (inode2 && inode2 != inode1)
		inode_lock_nested(inode2, I_MUTEX_NONDIR2);
}
EXPORT_SYMBOL(lock_two_nondirectories);

/**
 * unlock_two_nondirectories - release locks from lock_two_nondirectories()
 * @inode1: first inode to unlock
 * @inode2: second inode to unlock
 */
void unlock_two_nondirectories(struct inode *inode1, struct inode *inode2)
{
	if (inode1) {
		WARN_ON_ONCE(S_ISDIR(inode1->i_mode));
		inode_unlock(inode1);
	}
	if (inode2 && inode2 != inode1) {
		WARN_ON_ONCE(S_ISDIR(inode2->i_mode));
		inode_unlock(inode2);
	}
}
EXPORT_SYMBOL(unlock_two_nondirectories);

/**
 * inode_insert5 - obtain an inode from a mounted file system
 * @inode:	pre-allocated inode to use for insert to cache
 * @hashval:	hash value (usually inode number) to get
 * @test:	callback used for comparisons between inodes
 * @set:	callback used to initialize a new struct inode
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the inode specified by @hashval and @data in the inode cache,
 * and if present return it with an increased reference count. This is a
 * variant of iget5_locked() that doesn't allocate an inode.
 *
 * If the inode is not present in the cache, insert the pre-allocated inode and
 * return it locked, hashed, and with the I_NEW flag set. The file system gets
 * to fill it in before unlocking it via unlock_new_inode().
 *
 * Note that both @test and @set are called with the inode_hash_lock held, so
 * they can't sleep.
 */
struct inode *inode_insert5(struct inode *inode, unsigned long hashval,
			    int (*test)(struct inode *, void *),
			    int (*set)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(inode->i_sb, hashval);
	struct inode *old;

	might_sleep();

again:
	spin_lock(&inode_hash_lock);
	old = find_inode(inode->i_sb, head, test, data, true);
	if (unlikely(old)) {
		/*
		 * Uhhuh, somebody else created the same inode under us.
		 * Use the old inode instead of the preallocated one.
		 */
		spin_unlock(&inode_hash_lock);
		if (IS_ERR(old))
			return NULL;
		wait_on_inode(old);
		if (unlikely(inode_unhashed(old))) {
			iput(old);
			goto again;
		}
		return old;
	}

	if (set && unlikely(set(inode, data))) {
		spin_unlock(&inode_hash_lock);
		return NULL;
	}

	/*
	 * Return the locked inode with I_NEW set, the
	 * caller is responsible for filling in the contents
	 */
	spin_lock(&inode->i_lock);
	inode->i_state |= I_NEW;
	hlist_add_head_rcu(&inode->i_hash, head);
	spin_unlock(&inode->i_lock);

	spin_unlock(&inode_hash_lock);

	/*
	 * Add inode to the sb list if it's not already. It has I_NEW at this
	 * point, so it should be safe to test i_sb_list locklessly.
	 */
	if (list_empty(&inode->i_sb_list))
		inode_sb_list_add(inode);

	return inode;
}
EXPORT_SYMBOL(inode_insert5);

/**
 * iget5_locked - obtain an inode from a mounted file system
 * @sb:		super block of file system
 * @hashval:	hash value (usually inode number) to get
 * @test:	callback used for comparisons between inodes
 * @set:	callback used to initialize a new struct inode
 * @data:	opaque data pointer to pass to @test and @set
 *
 * Search for the inode specified by @hashval and @data in the inode cache,
 * and if present return it with an increased reference count. This is a
 * generalized version of iget_locked() for file systems where the inode
 * number is not sufficient for unique identification of an inode.
 *
 * If the inode is not present in the cache, allocate and insert a new inode
 * and return it locked, hashed, and with the I_NEW flag set. The file system
 * gets to fill it in before unlocking it via unlock_new_inode().
 *
 * Note that both @test and @set are called with the inode_hash_lock held, so
 * they can't sleep.
 */
struct inode *iget5_locked(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *),
		int (*set)(struct inode *, void *), void *data)
{
	struct inode *inode = ilookup5(sb, hashval, test, data);

	if (!inode) {
		struct inode *new = alloc_inode(sb);

		if (new) {
			inode = inode_insert5(new, hashval, test, set, data);
			if (unlikely(inode != new))
				destroy_inode(new);
		}
	}
	return inode;
}
EXPORT_SYMBOL(iget5_locked);

/**
 * iget5_locked_rcu - obtain an inode from a mounted file system
 * @sb:		super block of file system
 * @hashval:	hash value (usually inode number) to get
 * @test:	callback used for comparisons between inodes
 * @set:	callback used to initialize a new struct inode
 * @data:	opaque data pointer to pass to @test and @set
 *
 * This is equivalent to iget5_locked, except the @test callback must
 * tolerate the inode not being stable, including being mid-teardown.
 */
struct inode *iget5_locked_rcu(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *),
		int (*set)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);
	struct inode *inode, *new;

	might_sleep();

again:
	inode = find_inode(sb, head, test, data, false);
	if (inode) {
		if (IS_ERR(inode))
			return NULL;
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
		return inode;
	}

	new = alloc_inode(sb);
	if (new) {
		inode = inode_insert5(new, hashval, test, set, data);
		if (unlikely(inode != new))
			destroy_inode(new);
	}
	return inode;
}
EXPORT_SYMBOL_GPL(iget5_locked_rcu);

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

	might_sleep();

again:
	inode = find_inode_fast(sb, head, ino, false);
	if (inode) {
		if (IS_ERR(inode))
			return NULL;
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
		return inode;
	}

	inode = alloc_inode(sb);
	if (inode) {
		struct inode *old;

		spin_lock(&inode_hash_lock);
		/* We released the lock, so.. */
		old = find_inode_fast(sb, head, ino, true);
		if (!old) {
			inode->i_ino = ino;
			spin_lock(&inode->i_lock);
			inode->i_state = I_NEW;
			hlist_add_head_rcu(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			spin_unlock(&inode_hash_lock);
			inode_sb_list_add(inode);

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
		if (IS_ERR(old))
			return NULL;
		inode = old;
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
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

	hlist_for_each_entry_rcu(inode, b, i_hash) {
		if (inode->i_ino == ino && inode->i_sb == sb)
			return 0;
	}
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

	rcu_read_lock();
	spin_lock(&iunique_lock);
	do {
		if (counter <= max_reserved)
			counter = max_reserved + 1;
		res = counter++;
	} while (!test_inode_iunique(sb, res));
	spin_unlock(&iunique_lock);
	rcu_read_unlock();

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
	inode = find_inode(sb, head, test, data, true);
	spin_unlock(&inode_hash_lock);

	return IS_ERR(inode) ? NULL : inode;
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
	struct inode *inode;

	might_sleep();

again:
	inode = ilookup5_nowait(sb, hashval, test, data);
	if (inode) {
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
	}
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

	might_sleep();

again:
	inode = find_inode_fast(sb, head, ino, false);

	if (inode) {
		if (IS_ERR(inode))
			return NULL;
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
	}
	return inode;
}
EXPORT_SYMBOL(ilookup);

/**
 * find_inode_nowait - find an inode in the inode cache
 * @sb:		super block of file system to search
 * @hashval:	hash value (usually inode number) to search for
 * @match:	callback used for comparisons between inodes
 * @data:	opaque data pointer to pass to @match
 *
 * Search for the inode specified by @hashval and @data in the inode
 * cache, where the helper function @match will return 0 if the inode
 * does not match, 1 if the inode does match, and -1 if the search
 * should be stopped.  The @match function must be responsible for
 * taking the i_lock spin_lock and checking i_state for an inode being
 * freed or being initialized, and incrementing the reference count
 * before returning 1.  It also must not sleep, since it is called with
 * the inode_hash_lock spinlock held.
 *
 * This is a even more generalized version of ilookup5() when the
 * function must never block --- find_inode() can block in
 * __wait_on_freeing_inode() --- or when the caller can not increment
 * the reference count because the resulting iput() might cause an
 * inode eviction.  The tradeoff is that the @match funtion must be
 * very carefully implemented.
 */
struct inode *find_inode_nowait(struct super_block *sb,
				unsigned long hashval,
				int (*match)(struct inode *, unsigned long,
					     void *),
				void *data)
{
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);
	struct inode *inode, *ret_inode = NULL;
	int mval;

	spin_lock(&inode_hash_lock);
	hlist_for_each_entry(inode, head, i_hash) {
		if (inode->i_sb != sb)
			continue;
		mval = match(inode, hashval, data);
		if (mval == 0)
			continue;
		if (mval == 1)
			ret_inode = inode;
		goto out;
	}
out:
	spin_unlock(&inode_hash_lock);
	return ret_inode;
}
EXPORT_SYMBOL(find_inode_nowait);

/**
 * find_inode_rcu - find an inode in the inode cache
 * @sb:		Super block of file system to search
 * @hashval:	Key to hash
 * @test:	Function to test match on an inode
 * @data:	Data for test function
 *
 * Search for the inode specified by @hashval and @data in the inode cache,
 * where the helper function @test will return 0 if the inode does not match
 * and 1 if it does.  The @test function must be responsible for taking the
 * i_lock spin_lock and checking i_state for an inode being freed or being
 * initialized.
 *
 * If successful, this will return the inode for which the @test function
 * returned 1 and NULL otherwise.
 *
 * The @test function is not permitted to take a ref on any inode presented.
 * It is also not permitted to sleep.
 *
 * The caller must hold the RCU read lock.
 */
struct inode *find_inode_rcu(struct super_block *sb, unsigned long hashval,
			     int (*test)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);
	struct inode *inode;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "suspicious find_inode_rcu() usage");

	hlist_for_each_entry_rcu(inode, head, i_hash) {
		if (inode->i_sb == sb &&
		    !(READ_ONCE(inode->i_state) & (I_FREEING | I_WILL_FREE)) &&
		    test(inode, data))
			return inode;
	}
	return NULL;
}
EXPORT_SYMBOL(find_inode_rcu);

/**
 * find_inode_by_ino_rcu - Find an inode in the inode cache
 * @sb:		Super block of file system to search
 * @ino:	The inode number to match
 *
 * Search for the inode specified by @hashval and @data in the inode cache,
 * where the helper function @test will return 0 if the inode does not match
 * and 1 if it does.  The @test function must be responsible for taking the
 * i_lock spin_lock and checking i_state for an inode being freed or being
 * initialized.
 *
 * If successful, this will return the inode for which the @test function
 * returned 1 and NULL otherwise.
 *
 * The @test function is not permitted to take a ref on any inode presented.
 * It is also not permitted to sleep.
 *
 * The caller must hold the RCU read lock.
 */
struct inode *find_inode_by_ino_rcu(struct super_block *sb,
				    unsigned long ino)
{
	struct hlist_head *head = inode_hashtable + hash(sb, ino);
	struct inode *inode;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "suspicious find_inode_by_ino_rcu() usage");

	hlist_for_each_entry_rcu(inode, head, i_hash) {
		if (inode->i_ino == ino &&
		    inode->i_sb == sb &&
		    !(READ_ONCE(inode->i_state) & (I_FREEING | I_WILL_FREE)))
		    return inode;
	}
	return NULL;
}
EXPORT_SYMBOL(find_inode_by_ino_rcu);

int insert_inode_locked(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	ino_t ino = inode->i_ino;
	struct hlist_head *head = inode_hashtable + hash(sb, ino);

	might_sleep();

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
			inode->i_state |= I_NEW | I_CREATING;
			hlist_add_head_rcu(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			spin_unlock(&inode_hash_lock);
			return 0;
		}
		if (unlikely(old->i_state & I_CREATING)) {
			spin_unlock(&old->i_lock);
			spin_unlock(&inode_hash_lock);
			return -EBUSY;
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
	struct inode *old;

	might_sleep();

	inode->i_state |= I_CREATING;
	old = inode_insert5(inode, hashval, test, NULL, data);

	if (old != inode) {
		iput(old);
		return -EBUSY;
	}
	return 0;
}
EXPORT_SYMBOL(insert_inode_locked4);


int inode_just_drop(struct inode *inode)
{
	return 1;
}
EXPORT_SYMBOL(inode_just_drop);

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
	unsigned long state;
	int drop;

	WARN_ON(inode->i_state & I_NEW);

	if (op->drop_inode)
		drop = op->drop_inode(inode);
	else
		drop = inode_generic_drop(inode);

	if (!drop &&
	    !(inode->i_state & I_DONTCACHE) &&
	    (sb->s_flags & SB_ACTIVE)) {
		__inode_add_lru(inode, true);
		spin_unlock(&inode->i_lock);
		return;
	}

	state = inode->i_state;
	if (!drop) {
		WRITE_ONCE(inode->i_state, state | I_WILL_FREE);
		spin_unlock(&inode->i_lock);

		write_inode_now(inode, 1);

		spin_lock(&inode->i_lock);
		state = inode->i_state;
		WARN_ON(state & I_NEW);
		state &= ~I_WILL_FREE;
	}

	WRITE_ONCE(inode->i_state, state | I_FREEING);
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
	might_sleep();
	if (unlikely(!inode))
		return;

retry:
	lockdep_assert_not_held(&inode->i_lock);
	VFS_BUG_ON_INODE(inode->i_state & I_CLEAR, inode);
	/*
	 * Note this assert is technically racy as if the count is bogusly
	 * equal to one, then two CPUs racing to further drop it can both
	 * conclude it's fine.
	 */
	VFS_BUG_ON_INODE(atomic_read(&inode->i_count) < 1, inode);

	if (atomic_add_unless(&inode->i_count, -1, 1))
		return;

	if ((inode->i_state & I_DIRTY_TIME) && inode->i_nlink) {
		trace_writeback_lazytime_iput(inode);
		mark_inode_dirty_sync(inode);
		goto retry;
	}

	spin_lock(&inode->i_lock);
	if (unlikely((inode->i_state & I_DIRTY_TIME) && inode->i_nlink)) {
		spin_unlock(&inode->i_lock);
		goto retry;
	}

	if (!atomic_dec_and_test(&inode->i_count)) {
		spin_unlock(&inode->i_lock);
		return;
	}

	/*
	 * iput_final() drops ->i_lock, we can't assert on it as the inode may
	 * be deallocated by the time the call returns.
	 */
	iput_final(inode);
}
EXPORT_SYMBOL(iput);

/**
 *	iput_not_last	- put an inode assuming this is not the last reference
 *	@inode: inode to put
 */
void iput_not_last(struct inode *inode)
{
	VFS_BUG_ON_INODE(atomic_read(&inode->i_count) < 2, inode);

	WARN_ON(atomic_sub_return(1, &inode->i_count) == 0);
}
EXPORT_SYMBOL(iput_not_last);

#ifdef CONFIG_BLOCK
/**
 *	bmap	- find a block number in a file
 *	@inode:  inode owning the block number being requested
 *	@block: pointer containing the block to find
 *
 *	Replaces the value in ``*block`` with the block number on the device holding
 *	corresponding to the requested block number in the file.
 *	That is, asked for block 4 of inode 1 the function will replace the
 *	4 in ``*block``, with disk block relative to the disk start that holds that
 *	block of the file.
 *
 *	Returns -EINVAL in case of error, 0 otherwise. If mapping falls into a
 *	hole, returns 0 and ``*block`` is also set to 0.
 */
int bmap(struct inode *inode, sector_t *block)
{
	if (!inode->i_mapping->a_ops->bmap)
		return -EINVAL;

	*block = inode->i_mapping->a_ops->bmap(inode->i_mapping, *block);
	return 0;
}
EXPORT_SYMBOL(bmap);
#endif

/*
 * With relative atime, only update atime if the previous atime is
 * earlier than or equal to either the ctime or mtime,
 * or if at least a day has passed since the last atime update.
 */
static bool relatime_need_update(struct vfsmount *mnt, struct inode *inode,
			     struct timespec64 now)
{
	struct timespec64 atime, mtime, ctime;

	if (!(mnt->mnt_flags & MNT_RELATIME))
		return true;
	/*
	 * Is mtime younger than or equal to atime? If yes, update atime:
	 */
	atime = inode_get_atime(inode);
	mtime = inode_get_mtime(inode);
	if (timespec64_compare(&mtime, &atime) >= 0)
		return true;
	/*
	 * Is ctime younger than or equal to atime? If yes, update atime:
	 */
	ctime = inode_get_ctime(inode);
	if (timespec64_compare(&ctime, &atime) >= 0)
		return true;

	/*
	 * Is the previous atime value older than a day? If yes,
	 * update atime:
	 */
	if ((long)(now.tv_sec - atime.tv_sec) >= 24*60*60)
		return true;
	/*
	 * Good, we can skip the atime update:
	 */
	return false;
}

/**
 * inode_update_timestamps - update the timestamps on the inode
 * @inode: inode to be updated
 * @flags: S_* flags that needed to be updated
 *
 * The update_time function is called when an inode's timestamps need to be
 * updated for a read or write operation. This function handles updating the
 * actual timestamps. It's up to the caller to ensure that the inode is marked
 * dirty appropriately.
 *
 * In the case where any of S_MTIME, S_CTIME, or S_VERSION need to be updated,
 * attempt to update all three of them. S_ATIME updates can be handled
 * independently of the rest.
 *
 * Returns a set of S_* flags indicating which values changed.
 */
int inode_update_timestamps(struct inode *inode, int flags)
{
	int updated = 0;
	struct timespec64 now;

	if (flags & (S_MTIME|S_CTIME|S_VERSION)) {
		struct timespec64 ctime = inode_get_ctime(inode);
		struct timespec64 mtime = inode_get_mtime(inode);

		now = inode_set_ctime_current(inode);
		if (!timespec64_equal(&now, &ctime))
			updated |= S_CTIME;
		if (!timespec64_equal(&now, &mtime)) {
			inode_set_mtime_to_ts(inode, now);
			updated |= S_MTIME;
		}
		if (IS_I_VERSION(inode) && inode_maybe_inc_iversion(inode, updated))
			updated |= S_VERSION;
	} else {
		now = current_time(inode);
	}

	if (flags & S_ATIME) {
		struct timespec64 atime = inode_get_atime(inode);

		if (!timespec64_equal(&now, &atime)) {
			inode_set_atime_to_ts(inode, now);
			updated |= S_ATIME;
		}
	}
	return updated;
}
EXPORT_SYMBOL(inode_update_timestamps);

/**
 * generic_update_time - update the timestamps on the inode
 * @inode: inode to be updated
 * @flags: S_* flags that needed to be updated
 *
 * The update_time function is called when an inode's timestamps need to be
 * updated for a read or write operation. In the case where any of S_MTIME, S_CTIME,
 * or S_VERSION need to be updated we attempt to update all three of them. S_ATIME
 * updates can be handled done independently of the rest.
 *
 * Returns a S_* mask indicating which fields were updated.
 */
int generic_update_time(struct inode *inode, int flags)
{
	int updated = inode_update_timestamps(inode, flags);
	int dirty_flags = 0;

	if (updated & (S_ATIME|S_MTIME|S_CTIME))
		dirty_flags = inode->i_sb->s_flags & SB_LAZYTIME ? I_DIRTY_TIME : I_DIRTY_SYNC;
	if (updated & S_VERSION)
		dirty_flags |= I_DIRTY_SYNC;
	__mark_inode_dirty(inode, dirty_flags);
	return updated;
}
EXPORT_SYMBOL(generic_update_time);

/*
 * This does the actual work of updating an inodes time or version.  Must have
 * had called mnt_want_write() before calling this.
 */
int inode_update_time(struct inode *inode, int flags)
{
	if (inode->i_op->update_time)
		return inode->i_op->update_time(inode, flags);
	generic_update_time(inode, flags);
	return 0;
}
EXPORT_SYMBOL(inode_update_time);

/**
 *	atime_needs_update	-	update the access time
 *	@path: the &struct path to update
 *	@inode: inode to update
 *
 *	Update the accessed time on an inode and mark it for writeback.
 *	This function automatically handles read only file systems and media,
 *	as well as the "noatime" flag and inode specific "noatime" markers.
 */
bool atime_needs_update(const struct path *path, struct inode *inode)
{
	struct vfsmount *mnt = path->mnt;
	struct timespec64 now, atime;

	if (inode->i_flags & S_NOATIME)
		return false;

	/* Atime updates will likely cause i_uid and i_gid to be written
	 * back improprely if their true value is unknown to the vfs.
	 */
	if (HAS_UNMAPPED_ID(mnt_idmap(mnt), inode))
		return false;

	if (IS_NOATIME(inode))
		return false;
	if ((inode->i_sb->s_flags & SB_NODIRATIME) && S_ISDIR(inode->i_mode))
		return false;

	if (mnt->mnt_flags & MNT_NOATIME)
		return false;
	if ((mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode))
		return false;

	now = current_time(inode);

	if (!relatime_need_update(mnt, inode, now))
		return false;

	atime = inode_get_atime(inode);
	if (timespec64_equal(&atime, &now))
		return false;

	return true;
}

void touch_atime(const struct path *path)
{
	struct vfsmount *mnt = path->mnt;
	struct inode *inode = d_inode(path->dentry);

	if (!atime_needs_update(path, inode))
		return;

	if (!sb_start_write_trylock(inode->i_sb))
		return;

	if (mnt_get_write_access(mnt) != 0)
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
	inode_update_time(inode, S_ATIME);
	mnt_put_write_access(mnt);
skip_update:
	sb_end_write(inode->i_sb);
}
EXPORT_SYMBOL(touch_atime);

/*
 * Return mask of changes for notify_change() that need to be done as a
 * response to write or truncate. Return 0 if nothing has to be changed.
 * Negative value on error (change should be denied).
 */
int dentry_needs_remove_privs(struct mnt_idmap *idmap,
			      struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	int mask = 0;
	int ret;

	if (IS_NOSEC(inode))
		return 0;

	mask = setattr_should_drop_suidgid(idmap, inode);
	ret = security_inode_need_killpriv(dentry);
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
	 * Note we call this on write, so notify_change will not
	 * encounter any conflicting delegations:
	 */
	return notify_change(idmap, dentry, &newattrs, NULL);
}

static int file_remove_privs_flags(struct file *file, unsigned int flags)
{
	struct dentry *dentry = file_dentry(file);
	struct inode *inode = file_inode(file);
	int error = 0;
	int kill;

	if (IS_NOSEC(inode) || !S_ISREG(inode->i_mode))
		return 0;

	kill = dentry_needs_remove_privs(file_mnt_idmap(file), dentry);
	if (kill < 0)
		return kill;

	if (kill) {
		if (flags & IOCB_NOWAIT)
			return -EAGAIN;

		error = __remove_privs(file_mnt_idmap(file), dentry, kill);
	}

	if (!error)
		inode_has_no_xattr(inode);
	return error;
}

/**
 * file_remove_privs - remove special file privileges (suid, capabilities)
 * @file: file to remove privileges from
 *
 * When file is modified by a write or truncation ensure that special
 * file privileges are removed.
 *
 * Return: 0 on success, negative errno on failure.
 */
int file_remove_privs(struct file *file)
{
	return file_remove_privs_flags(file, 0);
}
EXPORT_SYMBOL(file_remove_privs);

/**
 * current_time - Return FS time (possibly fine-grained)
 * @inode: inode.
 *
 * Return the current time truncated to the time granularity supported by
 * the fs, as suitable for a ctime/mtime change. If the ctime is flagged
 * as having been QUERIED, get a fine-grained timestamp, but don't update
 * the floor.
 *
 * For a multigrain inode, this is effectively an estimate of the timestamp
 * that a file would receive. An actual update must go through
 * inode_set_ctime_current().
 */
struct timespec64 current_time(struct inode *inode)
{
	struct timespec64 now;
	u32 cns;

	ktime_get_coarse_real_ts64_mg(&now);

	if (!is_mgtime(inode))
		goto out;

	/* If nothing has queried it, then coarse time is fine */
	cns = smp_load_acquire(&inode->i_ctime_nsec);
	if (cns & I_CTIME_QUERIED) {
		/*
		 * If there is no apparent change, then get a fine-grained
		 * timestamp.
		 */
		if (now.tv_nsec == (cns & ~I_CTIME_QUERIED))
			ktime_get_real_ts64(&now);
	}
out:
	return timestamp_truncate(now, inode);
}
EXPORT_SYMBOL(current_time);

static int inode_needs_update_time(struct inode *inode)
{
	struct timespec64 now, ts;
	int sync_it = 0;

	/* First try to exhaust all avenues to not sync */
	if (IS_NOCMTIME(inode))
		return 0;

	now = current_time(inode);

	ts = inode_get_mtime(inode);
	if (!timespec64_equal(&ts, &now))
		sync_it |= S_MTIME;

	ts = inode_get_ctime(inode);
	if (!timespec64_equal(&ts, &now))
		sync_it |= S_CTIME;

	if (IS_I_VERSION(inode) && inode_iversion_need_inc(inode))
		sync_it |= S_VERSION;

	return sync_it;
}

static int __file_update_time(struct file *file, int sync_mode)
{
	int ret = 0;
	struct inode *inode = file_inode(file);

	/* try to update time settings */
	if (!mnt_get_write_access_file(file)) {
		ret = inode_update_time(inode, sync_mode);
		mnt_put_write_access_file(file);
	}

	return ret;
}

/**
 * file_update_time - update mtime and ctime time
 * @file: file accessed
 *
 * Update the mtime and ctime members of an inode and mark the inode for
 * writeback. Note that this function is meant exclusively for usage in
 * the file write path of filesystems, and filesystems may choose to
 * explicitly ignore updates via this function with the _NOCMTIME inode
 * flag, e.g. for network filesystem where these imestamps are handled
 * by the server. This can return an error for file systems who need to
 * allocate space in order to update an inode.
 *
 * Return: 0 on success, negative errno on failure.
 */
int file_update_time(struct file *file)
{
	int ret;
	struct inode *inode = file_inode(file);

	ret = inode_needs_update_time(inode);
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
 * If IOCB_NOWAIT is set, special file privileges will not be removed and
 * time settings will not be updated. It will return -EAGAIN.
 *
 * Context: Caller must hold the file's inode lock.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int file_modified_flags(struct file *file, int flags)
{
	int ret;
	struct inode *inode = file_inode(file);

	/*
	 * Clear the security bits if the process is not being run by root.
	 * This keeps people from modifying setuid and setgid binaries.
	 */
	ret = file_remove_privs_flags(file, flags);
	if (ret)
		return ret;

	if (unlikely(file->f_mode & FMODE_NOCMTIME))
		return 0;

	ret = inode_needs_update_time(inode);
	if (ret <= 0)
		return ret;
	if (flags & IOCB_NOWAIT)
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
 * Context: Caller must hold the file's inode lock.
 *
 * Return: 0 on success, negative errno on failure.
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
 * Context: Caller must hold the file's inode lock.
 *
 * Return: 0 on success, negative errno on failure.
 */
int kiocb_modified(struct kiocb *iocb)
{
	return file_modified_flags(iocb->ki_filp, iocb->ki_flags);
}
EXPORT_SYMBOL_GPL(kiocb_modified);

int inode_needs_sync(struct inode *inode)
{
	if (IS_SYNC(inode))
		return 1;
	if (S_ISDIR(inode->i_mode) && IS_DIRSYNC(inode))
		return 1;
	return 0;
}
EXPORT_SYMBOL(inode_needs_sync);

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
static void __wait_on_freeing_inode(struct inode *inode, bool is_inode_hash_locked)
{
	struct wait_bit_queue_entry wqe;
	struct wait_queue_head *wq_head;

	/*
	 * Handle racing against evict(), see that routine for more details.
	 */
	if (unlikely(inode_unhashed(inode))) {
		WARN_ON(is_inode_hash_locked);
		spin_unlock(&inode->i_lock);
		return;
	}

	wq_head = inode_bit_waitqueue(&wqe, inode, __I_NEW);
	prepare_to_wait_event(wq_head, &wqe.wq_entry, TASK_UNINTERRUPTIBLE);
	spin_unlock(&inode->i_lock);
	rcu_read_unlock();
	if (is_inode_hash_locked)
		spin_unlock(&inode_hash_lock);
	schedule();
	finish_wait(wq_head, &wqe.wq_entry);
	if (is_inode_hash_locked)
		spin_lock(&inode_hash_lock);
	rcu_read_lock();
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
					HASH_EARLY | HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void __init inode_init(void)
{
	/* inode slab cache */
	inode_cachep = kmem_cache_create("inode_cache",
					 sizeof(struct inode),
					 0,
					 (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					 SLAB_ACCOUNT),
					 init_once);

	/* Hash may have been set up in inode_init_early */
	if (!hashdist)
		return;

	inode_hashtable =
		alloc_large_system_hash("Inode-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_mode = mode;
	switch (inode->i_mode & S_IFMT) {
	case S_IFCHR:
		inode->i_fop = &def_chr_fops;
		inode->i_rdev = rdev;
		break;
	case S_IFBLK:
		if (IS_ENABLED(CONFIG_BLOCK))
			inode->i_fop = &def_blk_fops;
		inode->i_rdev = rdev;
		break;
	case S_IFIFO:
		inode->i_fop = &pipefifo_fops;
		break;
	case S_IFSOCK:
		/* leave it no_open_fops */
		break;
	default:
		printk(KERN_DEBUG "init_special_inode: bogus i_mode (%o) for"
				  " inode %s:%lu\n", mode, inode->i_sb->s_id,
				  inode->i_ino);
		break;
	}
}
EXPORT_SYMBOL(init_special_inode);

/**
 * inode_init_owner - Init uid,gid,mode for new inode according to posix standards
 * @idmap: idmap of the mount the inode was created from
 * @inode: New inode
 * @dir: Directory inode
 * @mode: mode of the new inode
 *
 * If the inode has been created through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then take
 * care to map the inode according to @idmap before checking permissions
 * and initializing i_uid and i_gid. On non-idmapped mounts or if permission
 * checking is to be performed on the raw inode simply pass @nop_mnt_idmap.
 */
void inode_init_owner(struct mnt_idmap *idmap, struct inode *inode,
		      const struct inode *dir, umode_t mode)
{
	inode_fsuid_set(inode, idmap);
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;

		/* Directories are special, and always inherit S_ISGID */
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode_fsgid_set(inode, idmap);
	inode->i_mode = mode;
}
EXPORT_SYMBOL(inode_init_owner);

/**
 * inode_owner_or_capable - check current task permissions to inode
 * @idmap: idmap of the mount the inode was found from
 * @inode: inode being checked
 *
 * Return true if current either has CAP_FOWNER in a namespace with the
 * inode owner uid mapped, or owns the file.
 *
 * If the inode has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then take
 * care to map the inode according to @idmap before checking permissions.
 * On non-idmapped mounts or if permission checking is to be performed on the
 * raw inode simply pass @nop_mnt_idmap.
 */
bool inode_owner_or_capable(struct mnt_idmap *idmap,
			    const struct inode *inode)
{
	vfsuid_t vfsuid;
	struct user_namespace *ns;

	vfsuid = i_uid_into_vfsuid(idmap, inode);
	if (vfsuid_eq_kuid(vfsuid, current_fsuid()))
		return true;

	ns = current_user_ns();
	if (vfsuid_has_mapping(ns, vfsuid) && ns_capable(ns, CAP_FOWNER))
		return true;
	return false;
}
EXPORT_SYMBOL(inode_owner_or_capable);

/*
 * Direct i/o helper functions
 */
bool inode_dio_finished(const struct inode *inode)
{
	return atomic_read(&inode->i_dio_count) == 0;
}
EXPORT_SYMBOL(inode_dio_finished);

/**
 * inode_dio_wait - wait for outstanding DIO requests to finish
 * @inode: inode to wait for
 *
 * Waits for all pending direct I/O requests to finish so that we can
 * proceed with a truncate or equivalent operation.
 *
 * Must be called under a lock that serializes taking new references
 * to i_dio_count, usually by inode->i_rwsem.
 */
void inode_dio_wait(struct inode *inode)
{
	wait_var_event(&inode->i_dio_count, inode_dio_finished(inode));
}
EXPORT_SYMBOL(inode_dio_wait);

void inode_dio_wait_interruptible(struct inode *inode)
{
	wait_var_event_interruptible(&inode->i_dio_count,
				     inode_dio_finished(inode));
}
EXPORT_SYMBOL(inode_dio_wait_interruptible);

/*
 * inode_set_flags - atomically set some inode flags
 *
 * Note: the caller should be holding i_rwsem exclusively, or else be sure that
 * they have exclusive access to the inode structure (i.e., while the
 * inode is being instantiated).  The reason for the cmpxchg() loop
 * --- which wouldn't be necessary if all code paths which modify
 * i_flags actually followed this rule, is that there is at least one
 * code path which doesn't today so we use cmpxchg() out of an abundance
 * of caution.
 *
 * In the long run, i_rwsem is overkill, and we should probably look
 * at using the i_lock spinlock to protect i_flags, and then make sure
 * it is so documented in include/linux/fs.h and that all code follows
 * the locking convention!!
 */
void inode_set_flags(struct inode *inode, unsigned int flags,
		     unsigned int mask)
{
	WARN_ON_ONCE(flags & ~mask);
	set_mask_bits(&inode->i_flags, mask, flags);
}
EXPORT_SYMBOL(inode_set_flags);

void inode_nohighmem(struct inode *inode)
{
	mapping_set_gfp_mask(inode->i_mapping, GFP_USER);
}
EXPORT_SYMBOL(inode_nohighmem);

struct timespec64 inode_set_ctime_to_ts(struct inode *inode, struct timespec64 ts)
{
	trace_inode_set_ctime_to_ts(inode, &ts);
	set_normalized_timespec64(&ts, ts.tv_sec, ts.tv_nsec);
	inode->i_ctime_sec = ts.tv_sec;
	inode->i_ctime_nsec = ts.tv_nsec;
	return ts;
}
EXPORT_SYMBOL(inode_set_ctime_to_ts);

/**
 * timestamp_truncate - Truncate timespec to a granularity
 * @t: Timespec
 * @inode: inode being updated
 *
 * Truncate a timespec to the granularity supported by the fs
 * containing the inode. Always rounds down. gran must
 * not be 0 nor greater than a second (NSEC_PER_SEC, or 10^9 ns).
 */
struct timespec64 timestamp_truncate(struct timespec64 t, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	unsigned int gran = sb->s_time_gran;

	t.tv_sec = clamp(t.tv_sec, sb->s_time_min, sb->s_time_max);
	if (unlikely(t.tv_sec == sb->s_time_max || t.tv_sec == sb->s_time_min))
		t.tv_nsec = 0;

	/* Avoid division in the common cases 1 ns and 1 s. */
	if (gran == 1)
		; /* nothing */
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
 * inode_set_ctime_current - set the ctime to current_time
 * @inode: inode
 *
 * Set the inode's ctime to the current value for the inode. Returns the
 * current value that was assigned. If this is not a multigrain inode, then we
 * set it to the later of the coarse time and floor value.
 *
 * If it is multigrain, then we first see if the coarse-grained timestamp is
 * distinct from what is already there. If so, then use that. Otherwise, get a
 * fine-grained timestamp.
 *
 * After that, try to swap the new value into i_ctime_nsec. Accept the
 * resulting ctime, regardless of the outcome of the swap. If it has
 * already been replaced, then that timestamp is later than the earlier
 * unacceptable one, and is thus acceptable.
 */
struct timespec64 inode_set_ctime_current(struct inode *inode)
{
	struct timespec64 now;
	u32 cns, cur;

	ktime_get_coarse_real_ts64_mg(&now);
	now = timestamp_truncate(now, inode);

	/* Just return that if this is not a multigrain fs */
	if (!is_mgtime(inode)) {
		inode_set_ctime_to_ts(inode, now);
		goto out;
	}

	/*
	 * A fine-grained time is only needed if someone has queried
	 * for timestamps, and the current coarse grained time isn't
	 * later than what's already there.
	 */
	cns = smp_load_acquire(&inode->i_ctime_nsec);
	if (cns & I_CTIME_QUERIED) {
		struct timespec64 ctime = { .tv_sec = inode->i_ctime_sec,
					    .tv_nsec = cns & ~I_CTIME_QUERIED };

		if (timespec64_compare(&now, &ctime) <= 0) {
			ktime_get_real_ts64_mg(&now);
			now = timestamp_truncate(now, inode);
			mgtime_counter_inc(mg_fine_stamps);
		}
	}
	mgtime_counter_inc(mg_ctime_updates);

	/* No need to cmpxchg if it's exactly the same */
	if (cns == now.tv_nsec && inode->i_ctime_sec == now.tv_sec) {
		trace_ctime_xchg_skip(inode, &now);
		goto out;
	}
	cur = cns;
retry:
	/* Try to swap the nsec value into place. */
	if (try_cmpxchg(&inode->i_ctime_nsec, &cur, now.tv_nsec)) {
		/* If swap occurred, then we're (mostly) done */
		inode->i_ctime_sec = now.tv_sec;
		trace_ctime_ns_xchg(inode, cns, now.tv_nsec, cur);
		mgtime_counter_inc(mg_ctime_swaps);
	} else {
		/*
		 * Was the change due to someone marking the old ctime QUERIED?
		 * If so then retry the swap. This can only happen once since
		 * the only way to clear I_CTIME_QUERIED is to stamp the inode
		 * with a new ctime.
		 */
		if (!(cns & I_CTIME_QUERIED) && (cns | I_CTIME_QUERIED) == cur) {
			cns = cur;
			goto retry;
		}
		/* Otherwise, keep the existing ctime */
		now.tv_sec = inode->i_ctime_sec;
		now.tv_nsec = cur & ~I_CTIME_QUERIED;
	}
out:
	return now;
}
EXPORT_SYMBOL(inode_set_ctime_current);

/**
 * inode_set_ctime_deleg - try to update the ctime on a delegated inode
 * @inode: inode to update
 * @update: timespec64 to set the ctime
 *
 * Attempt to atomically update the ctime on behalf of a delegation holder.
 *
 * The nfs server can call back the holder of a delegation to get updated
 * inode attributes, including the mtime. When updating the mtime, update
 * the ctime to a value at least equal to that.
 *
 * This can race with concurrent updates to the inode, in which
 * case the update is skipped.
 *
 * Note that this works even when multigrain timestamps are not enabled,
 * so it is used in either case.
 */
struct timespec64 inode_set_ctime_deleg(struct inode *inode, struct timespec64 update)
{
	struct timespec64 now, cur_ts;
	u32 cur, old;

	/* pairs with try_cmpxchg below */
	cur = smp_load_acquire(&inode->i_ctime_nsec);
	cur_ts.tv_nsec = cur & ~I_CTIME_QUERIED;
	cur_ts.tv_sec = inode->i_ctime_sec;

	/* If the update is older than the existing value, skip it. */
	if (timespec64_compare(&update, &cur_ts) <= 0)
		return cur_ts;

	ktime_get_coarse_real_ts64_mg(&now);

	/* Clamp the update to "now" if it's in the future */
	if (timespec64_compare(&update, &now) > 0)
		update = now;

	update = timestamp_truncate(update, inode);

	/* No need to update if the values are already the same */
	if (timespec64_equal(&update, &cur_ts))
		return cur_ts;

	/*
	 * Try to swap the nsec value into place. If it fails, that means
	 * it raced with an update due to a write or similar activity. That
	 * stamp takes precedence, so just skip the update.
	 */
retry:
	old = cur;
	if (try_cmpxchg(&inode->i_ctime_nsec, &cur, update.tv_nsec)) {
		inode->i_ctime_sec = update.tv_sec;
		mgtime_counter_inc(mg_ctime_swaps);
		return update;
	}

	/*
	 * Was the change due to another task marking the old ctime QUERIED?
	 *
	 * If so, then retry the swap. This can only happen once since
	 * the only way to clear I_CTIME_QUERIED is to stamp the inode
	 * with a new ctime.
	 */
	if (!(old & I_CTIME_QUERIED) && (cur == (old | I_CTIME_QUERIED)))
		goto retry;

	/* Otherwise, it was a new timestamp. */
	cur_ts.tv_sec = inode->i_ctime_sec;
	cur_ts.tv_nsec = cur & ~I_CTIME_QUERIED;
	return cur_ts;
}
EXPORT_SYMBOL(inode_set_ctime_deleg);

/**
 * in_group_or_capable - check whether caller is CAP_FSETID privileged
 * @idmap:	idmap of the mount @inode was found from
 * @inode:	inode to check
 * @vfsgid:	the new/current vfsgid of @inode
 *
 * Check whether @vfsgid is in the caller's group list or if the caller is
 * privileged with CAP_FSETID over @inode. This can be used to determine
 * whether the setgid bit can be kept or must be dropped.
 *
 * Return: true if the caller is sufficiently privileged, false if not.
 */
bool in_group_or_capable(struct mnt_idmap *idmap,
			 const struct inode *inode, vfsgid_t vfsgid)
{
	if (vfsgid_in_group_p(vfsgid))
		return true;
	if (capable_wrt_inode_uidgid(idmap, inode, CAP_FSETID))
		return true;
	return false;
}
EXPORT_SYMBOL(in_group_or_capable);

/**
 * mode_strip_sgid - handle the sgid bit for non-directories
 * @idmap: idmap of the mount the inode was created from
 * @dir: parent directory inode
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
			const struct inode *dir, umode_t mode)
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

#ifdef CONFIG_DEBUG_VFS
/*
 * Dump an inode.
 *
 * TODO: add a proper inode dumping routine, this is a stub to get debug off the
 * ground.
 *
 * TODO: handle getting to fs type with get_kernel_nofault()?
 * See dump_mapping() above.
 */
void dump_inode(struct inode *inode, const char *reason)
{
	struct super_block *sb = inode->i_sb;

	pr_warn("%s encountered for inode %px\n"
		"fs %s mode %ho opflags 0x%hx flags 0x%x state 0x%x count %d\n",
		reason, inode, sb->s_type->name, inode->i_mode, inode->i_opflags,
		inode->i_flags, inode->i_state, atomic_read(&inode->i_count));
}

EXPORT_SYMBOL(dump_inode);
#endif
