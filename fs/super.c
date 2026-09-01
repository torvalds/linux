// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  super.c contains code to handle: - mount structures
 *                                   - super-block tables
 *                                   - filesystem drivers list
 *                                   - mount system call
 *                                   - umount system call
 *                                   - ustat system call
 *
 * GK 2/5/95  -  Changed to support mounting the root fs via NFS
 *
 *  Added kerneld support: Jacques Gelinas and Bjorn Ekwall
 *  Added change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Added options to /proc/mounts:
 *    Torbjörn Lindh (torbjorn.lindh@gopta.se), April 14, 1996.
 *  Added devfs support: Richard Gooch <rgooch@atnf.csiro.au>, 13-JAN-1998
 *  Heavily rewritten for 'one fs - one tree' dcache architecture. AV, Mar 2000
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/memcontrol.h>
#include <linux/rhashtable.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/writeback.h>		/* for the emergency remount stuff */
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/backing-dev.h>
#include <linux/rculist_bl.h>
#include <linux/fscrypt.h>
#include <linux/fsnotify.h>
#include <linux/lockdep.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include <linux/fserror.h>
#include <uapi/linux/mount.h>
#include "internal.h"

static int thaw_super_locked(struct super_block *sb, enum freeze_holder who,
			     const void *freeze_owner);

static LIST_HEAD(super_blocks);
static DEFINE_SPINLOCK(sb_lock);

static char *sb_writers_name[SB_FREEZE_LEVELS] = {
	"sb_writers",
	"sb_pagefaults",
	"sb_internal",
};

static inline void __super_lock(struct super_block *sb, bool excl)
{
	if (excl)
		down_write(&sb->s_umount);
	else
		down_read(&sb->s_umount);
}

static inline void super_unlock(struct super_block *sb, bool excl)
{
	if (excl)
		up_write(&sb->s_umount);
	else
		up_read(&sb->s_umount);
}

static inline void __super_lock_excl(struct super_block *sb)
{
	__super_lock(sb, true);
}

static inline void super_unlock_excl(struct super_block *sb)
{
	super_unlock(sb, true);
}

static inline void super_unlock_shared(struct super_block *sb)
{
	super_unlock(sb, false);
}

static bool super_flags(const struct super_block *sb, unsigned int flags)
{
	/*
	 * Pairs with smp_store_release() in super_wake() and ensures
	 * that we see @flags after we're woken.
	 */
	return smp_load_acquire(&sb->s_flags) & flags;
}

/**
 * super_lock - wait for superblock to become ready and lock it
 * @sb: superblock to wait for
 * @excl: whether exclusive access is required
 *
 * If the superblock has neither passed through vfs_get_tree() or
 * generic_shutdown_super() yet wait for it to happen. Either superblock
 * creation will succeed and SB_BORN is set by vfs_get_tree() or we're
 * woken and we'll see SB_DYING.
 *
 * The caller must have acquired a temporary reference on @sb->s_passive.
 *
 * Return: The function returns true if SB_BORN was set and with
 *         s_umount held. The function returns false if SB_DYING was
 *         set and without s_umount held.
 */
static __must_check bool super_lock(struct super_block *sb, bool excl)
{
	lockdep_assert_not_held(&sb->s_umount);

	/* wait until the superblock is ready or dying */
	wait_var_event(&sb->s_flags, super_flags(sb, SB_BORN | SB_DYING));

	/* Don't pointlessly acquire s_umount. */
	if (super_flags(sb, SB_DYING))
		return false;

	__super_lock(sb, excl);

	/*
	 * Has gone through generic_shutdown_super() in the meantime.
	 * @sb->s_root is NULL and @sb->s_active is 0. No one needs to
	 * grab a reference to this. Tell them so.
	 */
	if (sb->s_flags & SB_DYING) {
		super_unlock(sb, excl);
		return false;
	}

	WARN_ON_ONCE(!(sb->s_flags & SB_BORN));
	return true;
}

/* wait and try to acquire read-side of @sb->s_umount */
static inline bool super_lock_shared(struct super_block *sb)
{
	return super_lock(sb, false);
}

/* wait and try to acquire write-side of @sb->s_umount */
static inline bool super_lock_excl(struct super_block *sb)
{
	return super_lock(sb, true);
}

/* wake waiters */
#define SUPER_WAKE_FLAGS (SB_BORN | SB_DYING | SB_DEAD)
static void super_wake(struct super_block *sb, unsigned int flag)
{
	WARN_ON_ONCE((flag & ~SUPER_WAKE_FLAGS));
	WARN_ON_ONCE(hweight32(flag & SUPER_WAKE_FLAGS) > 1);

	/*
	 * Pairs with smp_load_acquire() in super_lock() to make sure
	 * all initializations in the superblock are seen by the user
	 * seeing SB_BORN sent.
	 */
	smp_store_release(&sb->s_flags, sb->s_flags | flag);
	/*
	 * Pairs with the barrier in prepare_to_wait_event() to make sure
	 * ___wait_var_event() either sees SB_BORN set or
	 * waitqueue_active() check in wake_up_var() sees the waiter.
	 */
	smp_mb();
	wake_up_var(&sb->s_flags);
}

/*
 * The s_op->nr_cached_objects hooks (used for example by btrfs and xfs)
 * operate on filesystem-global state and ignore sc->memcg. Driving them
 * from per-memcg shrink_slab_memcg() invocations only burns CPU walking
 * per-cpu counters and queueing duplicate work: the actual reclaim happens on
 * the global path (kswapd or root direct reclaim) regardless. Restrict them
 * to that path.
 */
static inline bool super_fs_objects_eligible(struct shrink_control *sc)
{
	return !sc->memcg || mem_cgroup_is_root(sc->memcg);
}

/*
 * One thing we have to be careful of with a per-sb shrinker is that we don't
 * drop the last active reference to the superblock from within the shrinker.
 * If that happens we could trigger unregistering the shrinker from within the
 * shrinker path and that leads to deadlock on the shrinker_mutex. Hence we
 * take a passive reference to the superblock to avoid this from occurring.
 */
static unsigned long super_cache_scan(struct shrinker *shrink,
				      struct shrink_control *sc)
{
	struct super_block *sb;
	long	fs_objects = 0;
	long	total_objects;
	long	freed = 0;
	long	dentries;
	long	inodes;

	sb = shrink->private_data;

	/*
	 * Deadlock avoidance.  We may hold various FS locks, and we don't want
	 * to recurse into the FS that called us in clear_inode() and friends..
	 */
	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;

	if (!super_trylock_shared(sb))
		return SHRINK_STOP;

	if (sb->s_op->nr_cached_objects && super_fs_objects_eligible(sc))
		fs_objects = sb->s_op->nr_cached_objects(sb, sc);

	inodes = list_lru_shrink_count(&sb->s_inode_lru, sc);
	dentries = list_lru_shrink_count(&sb->s_dentry_lru, sc);
	total_objects = dentries + inodes + fs_objects;
	if (!total_objects)
		total_objects = 1;

	/* proportion the scan between the caches */
	dentries = mult_frac(sc->nr_to_scan, dentries, total_objects);
	inodes = mult_frac(sc->nr_to_scan, inodes, total_objects);
	fs_objects = mult_frac(sc->nr_to_scan, fs_objects, total_objects);

	/*
	 * prune the dcache first as the icache is pinned by it, then
	 * prune the icache, followed by the filesystem specific caches
	 *
	 * Ensure that we always scan at least one object - memcg kmem
	 * accounting uses this to fully empty the caches.
	 */
	sc->nr_to_scan = dentries + 1;
	freed = prune_dcache_sb(sb, sc);
	sc->nr_to_scan = inodes + 1;
	freed += prune_icache_sb(sb, sc);

	if (fs_objects) {
		sc->nr_to_scan = fs_objects + 1;
		freed += sb->s_op->free_cached_objects(sb, sc);
	}

	super_unlock_shared(sb);
	return freed;
}

static unsigned long super_cache_count(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	struct super_block *sb;
	long	total_objects = 0;

	sb = shrink->private_data;

	/*
	 * We don't call super_trylock_shared() here as it is a scalability
	 * bottleneck, so we're exposed to partial setup state. The shrinker
	 * rwsem does not protect filesystem operations backing
	 * list_lru_shrink_count() or s_op->nr_cached_objects(). Counts can
	 * change between super_cache_count and super_cache_scan, so we really
	 * don't need locks here.
	 *
	 * However, if we are currently mounting the superblock, the underlying
	 * filesystem might be in a state of partial construction and hence it
	 * is dangerous to access it.  super_trylock_shared() uses a SB_BORN check
	 * to avoid this situation, so do the same here. The memory barrier is
	 * matched with the one in mount_fs() as we don't hold locks here.
	 */
	if (!(sb->s_flags & SB_BORN))
		return 0;
	smp_rmb();

	if (sb->s_op && sb->s_op->nr_cached_objects &&
	    super_fs_objects_eligible(sc))
		total_objects = sb->s_op->nr_cached_objects(sb, sc);

	total_objects += list_lru_shrink_count(&sb->s_dentry_lru, sc);
	total_objects += list_lru_shrink_count(&sb->s_inode_lru, sc);

	if (!total_objects)
		return SHRINK_EMPTY;

	total_objects = vfs_pressure_ratio(total_objects);
	return total_objects;
}

static struct super_dev *super_dev_alloc(dev_t dev, struct super_block *sb);

static void destroy_super_work(struct work_struct *work)
{
	struct super_block *s = container_of(work, struct super_block,
							destroy_work);
	fsnotify_sb_free(s);
	security_sb_free(s);
	put_user_ns(s->s_user_ns);
	/* Only an unregistered entry is still owned by the superblock. */
	kfree(s->s_super_dev);
	kfree(s->s_subtype);
	for (int i = 0; i < SB_FREEZE_LEVELS; i++)
		percpu_free_rwsem(&s->s_writers.rw_sem[i]);
	kfree(s);
}

static void destroy_super_rcu(struct rcu_head *head)
{
	struct super_block *s = container_of(head, struct super_block, rcu);
	INIT_WORK(&s->destroy_work, destroy_super_work);
	schedule_work(&s->destroy_work);
}

/* Free a superblock that has never been seen by anyone */
static void destroy_unused_super(struct super_block *s)
{
	if (!s)
		return;
	super_unlock_excl(s);
	list_lru_destroy(&s->s_dentry_lru);
	list_lru_destroy(&s->s_inode_lru);
	shrinker_free(s->s_shrink);
	/* no delays needed */
	destroy_super_work(&s->destroy_work);
}

/**
 *	alloc_super	-	create new superblock
 *	@type:	filesystem type superblock should belong to
 *	@flags: the mount flags
 *	@user_ns: User namespace for the super_block
 *
 *	Allocates and initializes a new &struct super_block.  alloc_super()
 *	returns a pointer new superblock or %NULL if allocation had failed.
 */
static struct super_block *alloc_super(struct file_system_type *type, int flags,
				       struct user_namespace *user_ns)
{
	struct super_block *s = kzalloc_obj(struct super_block);
	static const struct super_operations default_op;
	int i;

	if (!s)
		return NULL;

	s->s_user_ns = get_user_ns(user_ns);
	init_rwsem(&s->s_umount);
	lockdep_set_class(&s->s_umount, &type->s_umount_key);
	/*
	 * sget_fc() can have s_umount recursion.
	 *
	 * When it cannot find a suitable sb, it allocates a new
	 * one (this one), and tries again to find a suitable old
	 * one.
	 *
	 * In case that succeeds, it will acquire the s_umount
	 * lock of the old one. Since these are clearly distrinct
	 * locks, and this object isn't exposed yet, there's no
	 * risk of deadlocks.
	 *
	 * Annotate this by putting this lock in a different
	 * subclass.
	 */
	down_write_nested(&s->s_umount, SINGLE_DEPTH_NESTING);

	if (security_sb_alloc(s))
		goto fail;

	for (i = 0; i < SB_FREEZE_LEVELS; i++) {
		if (__percpu_init_rwsem(&s->s_writers.rw_sem[i],
					sb_writers_name[i],
					&type->s_writers_key[i]))
			goto fail;
	}
	s->s_bdi = &noop_backing_dev_info;
	s->s_flags = flags;
	if (s->s_user_ns != &init_user_ns)
		s->s_iflags |= SB_I_NODEV;
	INIT_HLIST_NODE(&s->s_instances);
	INIT_HLIST_BL_HEAD(&s->s_roots);
	spin_lock_init(&s->s_roots_lock);
	mutex_init(&s->s_sync_lock);
	INIT_LIST_HEAD(&s->s_inodes);
	spin_lock_init(&s->s_inode_list_lock);
	INIT_LIST_HEAD(&s->s_inodes_wb);
	spin_lock_init(&s->s_inode_wblist_lock);
	fserror_mount(s);

	refcount_set(&s->s_passive, 1);
	atomic_set(&s->s_active, 1);
	mutex_init(&s->s_vfs_rename_mutex);
	lockdep_set_class(&s->s_vfs_rename_mutex, &type->s_vfs_rename_key);
	init_rwsem(&s->s_dquot.dqio_sem);
	s->s_maxbytes = MAX_NON_LFS;
	s->s_op = &default_op;
	s->s_time_gran = 1000000000;
	s->s_time_min = TIME64_MIN;
	s->s_time_max = TIME64_MAX;

	s->s_shrink = shrinker_alloc(SHRINKER_NUMA_AWARE | SHRINKER_MEMCG_AWARE,
				     "sb-%s", type->name);
	if (!s->s_shrink)
		goto fail;

	s->s_shrink->scan_objects = super_cache_scan;
	s->s_shrink->count_objects = super_cache_count;
	s->s_shrink->batch = 1024;
	s->s_shrink->private_data = s;

	if (list_lru_init_memcg(&s->s_dentry_lru, s->s_shrink))
		goto fail;
	if (list_lru_init_memcg(&s->s_inode_lru, s->s_shrink))
		goto fail;
	s->s_super_dev = super_dev_alloc(0, s);
	if (!s->s_super_dev)
		goto fail;

	s->s_min_writeback_pages = MIN_WRITEBACK_PAGES;
	return s;

fail:
	destroy_unused_super(s);
	return NULL;
}

/* Superblock refcounting  */

/*
 * Drop a superblock's passive reference.  Must be called WITHOUT sb_lock held;
 * put_super() acquires sb_lock itself when the final reference is dropped.
 */
void put_super(struct super_block *s)
{
	if (refcount_dec_and_test(&s->s_passive)) {

		spin_lock(&sb_lock);
		list_del_init(&s->s_list);
		spin_unlock(&sb_lock);

		WARN_ON(s->s_dentry_lru.node);
		WARN_ON(s->s_inode_lru.node);
		WARN_ON(s->s_mounts);
		call_rcu(&s->rcu, destroy_super_rcu);
	}
}

struct super_dev {
	dev_t			sd_dev;
	struct super_block	*sd_sb;
	refcount_t		sd_ref;
	struct rhlist_head	sd_node;
	struct rcu_head		sd_rcu;
};

static struct rhltable super_dev_table;
static const struct rhashtable_params super_dev_params = {
	.key_len	= sizeof(dev_t),
	.key_offset	= offsetof(struct super_dev, sd_dev),
	.head_offset	= offsetof(struct super_dev, sd_node),
};

static struct super_dev *super_dev_alloc(dev_t dev, struct super_block *sb)
{
	struct super_dev *fsd;

	fsd = kzalloc_obj(*fsd);
	if (!fsd)
		return NULL;
	fsd->sd_dev = dev;
	fsd->sd_sb = sb;
	refcount_set(&fsd->sd_ref, 1);
	return fsd;
}

static void super_dev_put(struct super_dev *fsd)
{
	/* Unlink only once unpinned, so a cursor never resumes from a removed node. */
	if (fsd && refcount_dec_and_test(&fsd->sd_ref)) {
		rhltable_remove(&super_dev_table, &fsd->sd_node, super_dev_params);
		put_super(fsd->sd_sb);
		kfree_rcu(fsd, sd_rcu);
	}
}

void __init super_dev_init(void)
{
	if (rhltable_init(&super_dev_table, &super_dev_params))
		panic("VFS: Cannot initialise super_dev_table\n");
}

static int super_dev_insert(struct super_dev *fsd)
{
	int err;

	err = rhltable_insert(&super_dev_table, &fsd->sd_node, super_dev_params);
	if (!err)
		refcount_inc(&fsd->sd_sb->s_passive);
	return err;
}

/* Register @sb under @sb->s_dev as the final fallible act of a set callback. */
static int super_dev_register(struct super_block *sb)
{
	struct super_dev *fsd = sb->s_super_dev;
	int err;

	lockdep_assert_held(&sb_lock);
	VFS_WARN_ON_ONCE(!sb->s_dev);
	VFS_WARN_ON_ONCE(!fsd || fsd->sd_dev);

	fsd->sd_dev = sb->s_dev;
	err = super_dev_insert(fsd);
	if (err)
		fsd->sd_dev = 0;
	return err;
}

static struct super_dev *super_dev_get(struct rhlist_head *pos)
{
	struct super_dev *sb_dev;

	for (; pos; pos = rcu_dereference_all(pos->next)) {
		sb_dev = container_of(pos, struct super_dev, sd_node);
		if (refcount_inc_not_zero(&sb_dev->sd_ref))
			return sb_dev;
	}
	return NULL;
}

static struct super_dev *super_dev_first(dev_t dev)
{
	struct super_dev *sb_dev;

	rcu_read_lock();
	sb_dev = super_dev_get(rhltable_lookup(&super_dev_table, &dev, super_dev_params));
	rcu_read_unlock();
	return sb_dev;
}

static struct super_dev *super_dev_next(struct super_dev *prev)
{
	struct super_dev *sb_dev;

	rcu_read_lock();
	sb_dev = super_dev_get(rcu_dereference_all(prev->sd_node.next));
	rcu_read_unlock();

	super_dev_put(prev);
	return sb_dev;
}

static void kill_super_notify(struct super_block *sb)
{
	lockdep_assert_not_held(&sb->s_umount);

	/* already notified earlier */
	if (sb->s_flags & SB_DEAD)
		return;

	/*
	 * Remove it from @fs_supers so it isn't found by new
	 * sget_fc() walkers anymore. Any concurrent mounter still
	 * managing to grab a temporary reference is guaranteed to
	 * already see SB_DYING and will wait until we notify them about
	 * SB_DEAD.
	 */
	spin_lock(&sb_lock);
	hlist_del_init(&sb->s_instances);
	spin_unlock(&sb_lock);

	/* Drop sget_fc()'s claim; a never-registered entry stays with the sb. */
	if (sb->s_super_dev->sd_dev) {
		super_dev_put(sb->s_super_dev);
		sb->s_super_dev = NULL;
	}

	/*
	 * Let concurrent mounts know that this thing is really dead.
	 * We don't need @sb->s_umount here as every concurrent caller
	 * will see SB_DYING and either discard the superblock or wait
	 * for SB_DEAD.
	 */
	super_wake(sb, SB_DEAD);
}

/**
 *	deactivate_locked_super	-	drop an active reference to superblock
 *	@s: superblock to deactivate
 *
 *	Drops an active reference to superblock, converting it into a temporary
 *	one if there is no other active references left.  In that case we
 *	tell fs driver to shut it down and drop the temporary reference we
 *	had just acquired.
 *
 *	Caller holds exclusive lock on superblock; that lock is released.
 */
void deactivate_locked_super(struct super_block *s)
{
	struct file_system_type *fs = s->s_type;
	if (atomic_dec_and_test(&s->s_active)) {
		shrinker_free(s->s_shrink);
		fs->kill_sb(s);

		kill_super_notify(s);

		/* list_lru_destroy() may sleep; put_super() callers may not. */
		list_lru_destroy(&s->s_dentry_lru);
		list_lru_destroy(&s->s_inode_lru);

		put_filesystem(fs);
		put_super(s);
	} else {
		super_unlock_excl(s);
	}
}

EXPORT_SYMBOL(deactivate_locked_super);

/**
 *	deactivate_super	-	drop an active reference to superblock
 *	@s: superblock to deactivate
 *
 *	Variant of deactivate_locked_super(), except that superblock is *not*
 *	locked by caller.  If we are going to drop the final active reference,
 *	lock will be acquired prior to that.
 */
void deactivate_super(struct super_block *s)
{
	if (!atomic_add_unless(&s->s_active, -1, 1)) {
		__super_lock_excl(s);
		deactivate_locked_super(s);
	}
}

EXPORT_SYMBOL(deactivate_super);

/**
 * grab_super - acquire an active reference to a superblock
 * @sb: superblock to acquire
 *
 * Acquire a temporary reference on a superblock and try to trade it for
 * an active reference. This is used in sget_fc() to wait for a
 * superblock to either become SB_BORN or for it to pass through
 * sb->kill() and be marked as SB_DEAD.
 *
 * Return: This returns true if an active reference could be acquired,
 *         false if not.
 */
static bool grab_super(struct super_block *sb)
{
	bool locked;

	refcount_inc(&sb->s_passive);
	spin_unlock(&sb_lock);
	locked = super_lock_excl(sb);
	if (locked) {
		if (atomic_inc_not_zero(&sb->s_active)) {
			put_super(sb);
			return true;
		}
		super_unlock_excl(sb);
	}
	wait_var_event(&sb->s_flags, super_flags(sb, SB_DEAD));
	put_super(sb);
	return false;
}

/*
 *	super_trylock_shared - try to grab ->s_umount shared
 *	@sb: reference we are trying to grab
 *
 *	Try to prevent fs shutdown.  This is used in places where we
 *	cannot take an active reference but we need to ensure that the
 *	filesystem is not shut down while we are working on it. It returns
 *	false if we cannot acquire s_umount or if we lose the race and
 *	filesystem already got into shutdown, and returns true with the s_umount
 *	lock held in read mode in case of success. On successful return,
 *	the caller must drop the s_umount lock when done.
 *
 *	Note that unlike get_super() et.al. this one does *not* bump ->s_passive.
 *	The reason why it's safe is that we are OK with doing trylock instead
 *	of down_read().  There's a couple of places that are OK with that, but
 *	it's very much not a general-purpose interface.
 */
bool super_trylock_shared(struct super_block *sb)
{
	if (down_read_trylock(&sb->s_umount)) {
		if (!(sb->s_flags & SB_DYING) && sb->s_root &&
		    (sb->s_flags & SB_BORN))
			return true;
		super_unlock_shared(sb);
	}

	return false;
}

/**
 *	retire_super	-	prevents superblock from being reused
 *	@sb: superblock to retire
 *
 *	The function marks superblock to be ignored in superblock test, which
 *	prevents it from being reused for any new mounts.  If the superblock has
 *	a private bdi, it also unregisters it, but doesn't reduce the refcount
 *	of the superblock to prevent potential races.  The refcount is reduced
 *	by generic_shutdown_super().  The function can not be called
 *	concurrently with generic_shutdown_super().  It is safe to call the
 *	function multiple times, subsequent calls have no effect.
 *
 *	The marker will affect the re-use only for block-device-based
 *	superblocks.  Other superblocks will still get marked if this function
 *	is used, but that will not affect their reusability.
 */
void retire_super(struct super_block *sb)
{
	WARN_ON(!sb->s_bdev);
	__super_lock_excl(sb);
	if (sb->s_iflags & SB_I_PERSB_BDI) {
		bdi_unregister(sb->s_bdi);
		sb->s_iflags &= ~SB_I_PERSB_BDI;
	}
	sb->s_iflags |= SB_I_RETIRED;
	super_unlock_excl(sb);
}
EXPORT_SYMBOL(retire_super);

/**
 *	generic_shutdown_super	-	common helper for ->kill_sb()
 *	@sb: superblock to kill
 *
 *	generic_shutdown_super() does all fs-independent work on superblock
 *	shutdown.  Typical ->kill_sb() should pick all fs-specific objects
 *	that need destruction out of superblock, call generic_shutdown_super()
 *	and release aforementioned objects.  Note: dentries and inodes _are_
 *	taken care of and do not need specific handling.
 *
 *	Upon calling this function, the filesystem may no longer alter or
 *	rearrange the set of dentries belonging to this super_block, nor may it
 *	change the attachments of dentries to inodes.
 */
void generic_shutdown_super(struct super_block *sb)
{
	const struct super_operations *sop = sb->s_op;

	if (sb->s_root) {
		fsnotify_sb_delete(sb);
		shrink_dcache_for_umount(sb);
		sync_filesystem(sb);
		sb->s_flags &= ~SB_ACTIVE;

		fserror_unmount(sb);
		cgroup_writeback_umount(sb);

		/* Evict all inodes with zero refcount. */
		evict_inodes(sb);

		/*
		 * Clean up and evict any inodes that still have references due
		 * to the security policy.
		 */
		security_sb_delete(sb);

		if (sb->s_dio_done_wq) {
			destroy_workqueue(sb->s_dio_done_wq);
			sb->s_dio_done_wq = NULL;
		}

		if (sop->put_super)
			sop->put_super(sb);

		/*
		 * Now that all potentially-encrypted inodes have been evicted,
		 * the fscrypt keyring can be destroyed.
		 */
		fscrypt_destroy_keyring(sb);

		if (CHECK_DATA_CORRUPTION(!list_empty(&sb->s_inodes), NULL,
				"VFS: Busy inodes after unmount of %s (%s)",
				sb->s_id, sb->s_type->name)) {
			/*
			 * Adding a proper bailout path here would be hard, but
			 * we can at least make it more likely that a later
			 * iput_final() or such crashes cleanly.
			 */
			struct inode *inode;

			spin_lock(&sb->s_inode_list_lock);
			list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
				inode->i_op = VFS_PTR_POISON;
				inode->i_sb = VFS_PTR_POISON;
				inode->i_mapping = VFS_PTR_POISON;
			}
			spin_unlock(&sb->s_inode_list_lock);
		}
	}
	/*
	 * Broadcast to everyone that grabbed a temporary reference to this
	 * superblock before we removed it from @fs_supers that the superblock
	 * is dying. Every walker of @fs_supers outside of sget_fc() will now
	 * discard this superblock and treat it as dead.
	 *
	 * We leave the superblock on @fs_supers so it can be found by
	 * sget_fc() until we passed sb->kill_sb().
	 */
	super_wake(sb, SB_DYING);
	super_unlock_excl(sb);
	if (sb->s_bdi != &noop_backing_dev_info) {
		if (sb->s_iflags & SB_I_PERSB_BDI)
			bdi_unregister(sb->s_bdi);
		bdi_put(sb->s_bdi);
		sb->s_bdi = &noop_backing_dev_info;
	}
}

EXPORT_SYMBOL(generic_shutdown_super);

bool mount_capable(struct fs_context *fc)
{
	if (!(fc->fs_type->fs_flags & FS_USERNS_MOUNT))
		return capable(CAP_SYS_ADMIN);
	else
		return ns_capable(fc->user_ns, CAP_SYS_ADMIN);
}

/**
 * sget_fc - Find or create a superblock
 * @fc:	Filesystem context.
 * @test: Comparison callback
 * @set: Setup callback
 *
 * Create a new superblock or find an existing one.
 *
 * The @test callback is used to find a matching existing superblock.
 * Whether or not the requested parameters in @fc are taken into account
 * is specific to the @test callback that is used. They may even be
 * completely ignored.
 *
 * If an extant superblock is matched, it will be returned unless:
 *
 * (1) the namespace the filesystem context @fc and the extant
 *     superblock's namespace differ
 *
 * (2) the filesystem context @fc has requested that reusing an extant
 *     superblock is not allowed
 *
 * In both cases EBUSY will be returned.
 *
 * If no match is made, a new superblock will be allocated and basic
 * initialisation will be performed (s_type, s_fs_info and s_id will be
 * set and the @set callback will be invoked), the superblock will be
 * published and it will be returned in a partially constructed state
 * with SB_BORN and SB_ACTIVE as yet unset.
 *
 * Return: On success, an extant or newly created superblock is
 *         returned. On failure an error pointer is returned.
 */
struct super_block *sget_fc(struct fs_context *fc,
			    int (*test)(struct super_block *, struct fs_context *),
			    int (*set)(struct super_block *, struct fs_context *))
{
	struct super_block *s = NULL;
	struct super_block *old;
	struct user_namespace *user_ns = fc->global ? &init_user_ns : fc->user_ns;
	int err;

	/*
	 * Never allow s_user_ns != &init_user_ns when FS_USERNS_MOUNT or
	 * FS_USERNS_DELEGATABLE is not set, as the filesystem is likely
	 * unprepared to handle it. This can happen when fsconfig() is called
	 * from init_user_ns with an fs_fd opened in another user namespace.
	 */
	if (user_ns != &init_user_ns &&
	    !(fc->fs_type->fs_flags & (FS_USERNS_MOUNT | FS_USERNS_DELEGATABLE))) {
		errorfc(fc, "VFS: Mounting from non-initial user namespace is not allowed");
		return ERR_PTR(-EPERM);
	}

retry:
	spin_lock(&sb_lock);
	if (test) {
		hlist_for_each_entry(old, &fc->fs_type->fs_supers, s_instances) {
			if (test(old, fc))
				goto share_extant_sb;
		}
	}
	if (!s) {
		spin_unlock(&sb_lock);

		s = alloc_super(fc->fs_type, fc->sb_flags, user_ns);
		if (!s)
			return ERR_PTR(-ENOMEM);
		goto retry;
	}

	s->s_fs_info = fc->s_fs_info;
	err = set(s, fc);
	if (err) {
		VFS_WARN_ON_ONCE(s->s_super_dev->sd_dev);
		s->s_fs_info = NULL;
		spin_unlock(&sb_lock);
		destroy_unused_super(s);
		return ERR_PTR(err);
	}
	VFS_WARN_ON_ONCE(!s->s_super_dev->sd_dev);
	fc->s_fs_info = NULL;
	s->s_type = fc->fs_type;
	s->s_iflags |= fc->s_iflags;
	strscpy(s->s_id, s->s_type->name, sizeof(s->s_id));
	/*
	 * Make the superblock visible on @super_blocks and @fs_supers.
	 * It's in a nascent state and users should wait on SB_BORN or
	 * SB_DYING to be set.
	 */
	list_add_tail(&s->s_list, &super_blocks);
	hlist_add_head(&s->s_instances, &s->s_type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(s->s_type);
	shrinker_register(s->s_shrink);
	return s;

share_extant_sb:
	if (user_ns != old->s_user_ns || fc->exclusive) {
		spin_unlock(&sb_lock);
		destroy_unused_super(s);
		if (fc->exclusive)
			warnfc(fc, "reusing existing filesystem not allowed");
		else
			warnfc(fc, "reusing existing filesystem in another namespace not allowed");
		return ERR_PTR(-EBUSY);
	}
	if (!grab_super(old))
		goto retry;
	destroy_unused_super(s);
	return old;
}
EXPORT_SYMBOL(sget_fc);

void drop_super(struct super_block *sb)
{
	super_unlock_shared(sb);
	put_super(sb);
}

EXPORT_SYMBOL(drop_super);

void drop_super_exclusive(struct super_block *sb)
{
	super_unlock_excl(sb);
	put_super(sb);
}

enum super_iter_flags_t {
	SUPER_ITER_EXCL		= (1U << 0),
	SUPER_ITER_UNLOCKED	= (1U << 1),
	SUPER_ITER_REVERSE	= (1U << 2),
};

static inline struct super_block *first_super(enum super_iter_flags_t flags)
{
	if (flags & SUPER_ITER_REVERSE)
		return list_last_entry(&super_blocks, struct super_block, s_list);
	return list_first_entry(&super_blocks, struct super_block, s_list);
}

static inline struct super_block *next_super(struct super_block *sb,
					     enum super_iter_flags_t flags)
{
	if (flags & SUPER_ITER_REVERSE)
		return list_prev_entry(sb, s_list);
	return list_next_entry(sb, s_list);
}

static void __iterate_supers(void (*f)(struct super_block *, void *), void *arg,
			     enum super_iter_flags_t flags)
{
	struct super_block *sb, *p = NULL;
	bool excl = flags & SUPER_ITER_EXCL;

	spin_lock(&sb_lock);

	for (sb = first_super(flags);
	     !list_entry_is_head(sb, &super_blocks, s_list);
	     sb = next_super(sb, flags)) {
		if (super_flags(sb, SB_DYING))
			continue;

		if (!refcount_inc_not_zero(&sb->s_passive))
			continue;

		spin_unlock(&sb_lock);

		if (flags & SUPER_ITER_UNLOCKED) {
			f(sb, arg);
		} else if (super_lock(sb, excl)) {
			f(sb, arg);
			super_unlock(sb, excl);
		}

		if (p)
			put_super(p);
		p = sb;
		spin_lock(&sb_lock);
	}
	spin_unlock(&sb_lock);
	if (p)
		put_super(p);
}

void iterate_supers(void (*f)(struct super_block *, void *), void *arg)
{
	__iterate_supers(f, arg, 0);
}

/**
 *	iterate_supers_type - call function for superblocks of given type
 *	@type: fs type
 *	@f: function to call
 *	@arg: argument to pass to it
 *
 *	Scans the superblock list and calls given function, passing it
 *	locked superblock and given argument.
 */
void iterate_supers_type(struct file_system_type *type,
	void (*f)(struct super_block *, void *), void *arg)
{
	struct super_block *sb, *p = NULL;

	spin_lock(&sb_lock);
	hlist_for_each_entry(sb, &type->fs_supers, s_instances) {
		bool locked;

		if (super_flags(sb, SB_DYING))
			continue;

		if (!refcount_inc_not_zero(&sb->s_passive))
			continue;

		spin_unlock(&sb_lock);

		locked = super_lock_shared(sb);
		if (locked) {
			f(sb, arg);
			super_unlock_shared(sb);
		}

		if (p)
			put_super(p);
		p = sb;
		spin_lock(&sb_lock);
	}
	spin_unlock(&sb_lock);
	if (p)
		put_super(p);
}

EXPORT_SYMBOL(iterate_supers_type);

struct super_block *user_get_super(dev_t dev, bool excl)
{
	struct super_dev *sb_dev;

	for (sb_dev = super_dev_first(dev); sb_dev; sb_dev = super_dev_next(sb_dev)) {
		struct super_block *sb = sb_dev->sd_sb;

		if (!super_lock(sb, excl))
			continue;

		/* The pinned entry holds a passive reference, take our own. */
		refcount_inc(&sb->s_passive);
		super_dev_put(sb_dev);
		return sb;
	}
	return NULL;
}

/**
 * reconfigure_super - asks filesystem to change superblock parameters
 * @fc: The superblock and configuration
 *
 * Alters the configuration parameters of a live superblock.
 */
int reconfigure_super(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	int retval;
	bool remount_ro = false;
	bool remount_rw = false;
	bool force = fc->sb_flags & SB_FORCE;

	if (fc->sb_flags_mask & ~MS_RMT_MASK)
		return -EINVAL;
	if (sb->s_writers.frozen != SB_UNFROZEN)
		return -EBUSY;

	retval = security_sb_remount(sb, fc->security);
	if (retval)
		return retval;

	if (fc->sb_flags_mask & SB_RDONLY) {
#ifdef CONFIG_BLOCK
		if (!(fc->sb_flags & SB_RDONLY) && sb->s_bdev &&
		    bdev_read_only(sb->s_bdev))
			return -EACCES;
#endif
		remount_rw = !(fc->sb_flags & SB_RDONLY) && sb_rdonly(sb);
		remount_ro = (fc->sb_flags & SB_RDONLY) && !sb_rdonly(sb);
	}

	if (remount_ro) {
		if (!hlist_empty(&sb->s_pins)) {
			super_unlock_excl(sb);
			group_pin_kill(&sb->s_pins);
			__super_lock_excl(sb);
			if (!sb->s_root)
				return 0;
			if (sb->s_writers.frozen != SB_UNFROZEN)
				return -EBUSY;
			remount_ro = !sb_rdonly(sb);
		}
	}
	shrink_dcache_sb(sb);

	/* If we are reconfiguring to RDONLY and current sb is read/write,
	 * make sure there are no files open for writing.
	 */
	if (remount_ro) {
		if (force) {
			sb_start_ro_state_change(sb);
		} else {
			retval = sb_prepare_remount_readonly(sb);
			if (retval)
				return retval;
		}
	} else if (remount_rw) {
		/*
		 * Protect filesystem's reconfigure code from writes from
		 * userspace until reconfigure finishes.
		 */
		sb_start_ro_state_change(sb);
	}

	if (fc->ops->reconfigure) {
		retval = fc->ops->reconfigure(fc);
		if (retval) {
			if (!force)
				goto cancel_readonly;
			/* If forced remount, go ahead despite any errors */
			WARN(1, "forced remount of a %s fs returned %i\n",
			     sb->s_type->name, retval);
		}
	}

	WRITE_ONCE(sb->s_flags, ((sb->s_flags & ~fc->sb_flags_mask) |
				 (fc->sb_flags & fc->sb_flags_mask)));
	sb_end_ro_state_change(sb);

	/*
	 * Some filesystems modify their metadata via some other path than the
	 * bdev buffer cache (eg. use a private mapping, or directories in
	 * pagecache, etc). Also file data modifications go via their own
	 * mappings. So If we try to mount readonly then copy the filesystem
	 * from bdev, we could get stale data, so invalidate it to give a best
	 * effort at coherency.
	 */
	if (remount_ro && sb->s_bdev)
		invalidate_bdev(sb->s_bdev);
	return 0;

cancel_readonly:
	sb_end_ro_state_change(sb);
	return retval;
}

static void do_emergency_remount_callback(struct super_block *sb, void *unused)
{
	if (sb->s_bdev && !sb_rdonly(sb)) {
		struct fs_context *fc;

		fc = fs_context_for_reconfigure(sb->s_root,
					SB_RDONLY | SB_FORCE, SB_RDONLY);
		if (!IS_ERR(fc)) {
			if (parse_monolithic_mount_data(fc, NULL) == 0)
				(void)reconfigure_super(fc);
			put_fs_context(fc);
		}
	}
}

static void do_emergency_remount(struct work_struct *work)
{
	__iterate_supers(do_emergency_remount_callback, NULL,
			 SUPER_ITER_EXCL | SUPER_ITER_REVERSE);
	kfree(work);
	printk("Emergency Remount complete\n");
}

void emergency_remount(void)
{
	struct work_struct *work;

	work = kmalloc_obj(*work, GFP_ATOMIC);
	if (work) {
		INIT_WORK(work, do_emergency_remount);
		schedule_work(work);
	}
}

static inline bool get_active_super(struct super_block *sb)
{
	bool active = false;

	if (super_lock_excl(sb)) {
		active = atomic_inc_not_zero(&sb->s_active);
		super_unlock_excl(sb);
	}
	return active;
}

static void do_thaw_all_callback(struct super_block *sb, void *unused)
{
	if (!get_active_super(sb))
		return;

	/* fs_bdev_thaw() acquires s_umount so it must not be held here */
	if (IS_ENABLED(CONFIG_BLOCK))
		while (sb->s_bdev && !bdev_thaw(sb->s_bdev))
			pr_warn("Emergency Thaw on %pg\n", sb->s_bdev);

	if (super_lock_excl(sb))
		thaw_super_locked(sb, FREEZE_HOLDER_USERSPACE, NULL);
	deactivate_super(sb);
}

static void do_thaw_all(struct work_struct *work)
{
	__iterate_supers(do_thaw_all_callback, NULL, SUPER_ITER_UNLOCKED);
	kfree(work);
	printk(KERN_WARNING "Emergency Thaw complete\n");
}

/**
 * emergency_thaw_all -- forcibly thaw every frozen filesystem
 *
 * Used for emergency unfreeze of all filesystems via SysRq
 */
void emergency_thaw_all(void)
{
	struct work_struct *work;

	work = kmalloc_obj(*work, GFP_ATOMIC);
	if (work) {
		INIT_WORK(work, do_thaw_all);
		schedule_work(work);
	}
}

static const char *filesystems_freeze_ptr = "filesystems_freeze";

static void filesystems_freeze_callback(struct super_block *sb, void *freeze_all_ptr)
{
	if (!sb->s_op->freeze_fs && !sb->s_op->freeze_super)
		return;

	if (!freeze_all_ptr && !(sb->s_type->fs_flags & FS_POWER_FREEZE))
		return;

	if (!get_active_super(sb))
		return;

	if (sb->s_op->freeze_super)
		sb->s_op->freeze_super(sb, FREEZE_EXCL | FREEZE_HOLDER_KERNEL,
				       filesystems_freeze_ptr);
	else
		freeze_super(sb, FREEZE_EXCL | FREEZE_HOLDER_KERNEL,
			     filesystems_freeze_ptr);

	deactivate_super(sb);
}

void filesystems_freeze(bool freeze_all)
{
	void *freeze_all_ptr = NULL;

	if (freeze_all)
		freeze_all_ptr = &freeze_all;
	__iterate_supers(filesystems_freeze_callback, freeze_all_ptr,
			 SUPER_ITER_UNLOCKED | SUPER_ITER_REVERSE);
}

static void filesystems_thaw_callback(struct super_block *sb, void *unused)
{
	if (!sb->s_op->freeze_fs && !sb->s_op->freeze_super)
		return;

	if (!get_active_super(sb))
		return;

	if (sb->s_op->thaw_super)
		sb->s_op->thaw_super(sb, FREEZE_EXCL | FREEZE_HOLDER_KERNEL,
				     filesystems_freeze_ptr);
	else
		thaw_super(sb, FREEZE_EXCL | FREEZE_HOLDER_KERNEL,
			   filesystems_freeze_ptr);

	deactivate_super(sb);
}

void filesystems_thaw(void)
{
	__iterate_supers(filesystems_thaw_callback, NULL, SUPER_ITER_UNLOCKED);
}

static DEFINE_IDA(unnamed_dev_ida);

/**
 * get_anon_bdev - Allocate a block device for filesystems which don't have one.
 * @p: Pointer to a dev_t.
 *
 * Filesystems which don't use real block devices can call this function
 * to allocate a virtual block device.
 *
 * Context: Any context.  Frequently called while holding sb_lock.
 * Return: 0 on success, -EMFILE if there are no anonymous bdevs left
 * or -ENOMEM if memory allocation failed.
 */
int get_anon_bdev(dev_t *p)
{
	int dev;

	/*
	 * Many userspace utilities consider an FSID of 0 invalid.
	 * Always return at least 1 from get_anon_bdev.
	 */
	dev = ida_alloc_range(&unnamed_dev_ida, 1, (1 << MINORBITS) - 1,
			GFP_ATOMIC);
	if (dev == -ENOSPC)
		dev = -EMFILE;
	if (dev < 0)
		return dev;

	*p = MKDEV(0, dev);
	return 0;
}
EXPORT_SYMBOL(get_anon_bdev);

void free_anon_bdev(dev_t dev)
{
	ida_free(&unnamed_dev_ida, MINOR(dev));
}
EXPORT_SYMBOL(free_anon_bdev);

int set_anon_super(struct super_block *s, void *data)
{
	int error;

	error = get_anon_bdev(&s->s_dev);
	if (error)
		return error;

	error = super_dev_register(s);
	if (error)
		free_anon_bdev(s->s_dev);
	return error;
}
EXPORT_SYMBOL(set_anon_super);

void kill_anon_super(struct super_block *sb)
{
	dev_t dev = sb->s_dev;
	generic_shutdown_super(sb);
	kill_super_notify(sb);
	free_anon_bdev(dev);
}
EXPORT_SYMBOL(kill_anon_super);

int set_anon_super_fc(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super(sb, NULL);
}
EXPORT_SYMBOL(set_anon_super_fc);

static int test_keyed_super(struct super_block *sb, struct fs_context *fc)
{
	return sb->s_fs_info == fc->s_fs_info;
}

static int test_single_super(struct super_block *s, struct fs_context *fc)
{
	return 1;
}

static int vfs_get_super(struct fs_context *fc,
		int (*test)(struct super_block *, struct fs_context *),
		int (*fill_super)(struct super_block *sb,
				  struct fs_context *fc))
{
	struct super_block *sb;
	int err;

	sb = sget_fc(fc, test, set_anon_super_fc);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		err = fill_super(sb, fc);
		if (err)
			goto error;

		sb->s_flags |= SB_ACTIVE;
	}

	fc->root = dget(sb->s_root);
	return 0;

error:
	deactivate_locked_super(sb);
	return err;
}

int get_tree_nodev(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	return vfs_get_super(fc, NULL, fill_super);
}
EXPORT_SYMBOL(get_tree_nodev);

int get_tree_single(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	return vfs_get_super(fc, test_single_super, fill_super);
}
EXPORT_SYMBOL(get_tree_single);

int get_tree_keyed(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc),
		void *key)
{
	fc->s_fs_info = key;
	return vfs_get_super(fc, test_keyed_super, fill_super);
}
EXPORT_SYMBOL(get_tree_keyed);

static int set_bdev_super(struct super_block *s, void *data)
{
	s->s_dev = *(dev_t *)data;
	return super_dev_register(s);
}

static int super_s_dev_set(struct super_block *s, struct fs_context *fc)
{
	return set_bdev_super(s, fc->sget_key);
}

static int super_s_dev_test(struct super_block *s, struct fs_context *fc)
{
	return !(s->s_iflags & SB_I_RETIRED) &&
		s->s_dev == *(dev_t *)fc->sget_key;
}

/**
 * sget_dev - Find or create a superblock by device number
 * @fc: Filesystem context.
 * @dev: device number
 *
 * Find or create a superblock using the provided device number that
 * will be stored in fc->sget_key.
 *
 * If an extant superblock is matched, then that will be returned with
 * an elevated reference count that the caller must transfer or discard.
 *
 * If no match is made, a new superblock will be allocated and basic
 * initialisation will be performed (s_type, s_fs_info, s_id, s_dev will
 * be set). The superblock will be published and it will be returned in
 * a partially constructed state with SB_BORN and SB_ACTIVE as yet
 * unset.
 *
 * Return: an existing or newly created superblock on success, an error
 *         pointer on failure.
 */
struct super_block *sget_dev(struct fs_context *fc, dev_t dev)
{
	fc->sget_key = &dev;
	return sget_fc(fc, super_s_dev_test, super_s_dev_set);
}
EXPORT_SYMBOL(sget_dev);

#ifdef CONFIG_BLOCK
static int fs_super_freeze(struct super_block *sb)
{
	if (sb->s_op->freeze_super)
		return sb->s_op->freeze_super(sb,
				FREEZE_MAY_NEST | FREEZE_HOLDER_USERSPACE, NULL);
	return freeze_super(sb, FREEZE_MAY_NEST | FREEZE_HOLDER_USERSPACE, NULL);
}

static int fs_super_thaw(struct super_block *sb)
{
	if (sb->s_op->thaw_super)
		return sb->s_op->thaw_super(sb,
				FREEZE_MAY_NEST | FREEZE_HOLDER_USERSPACE, NULL);
	return thaw_super(sb, FREEZE_MAY_NEST | FREEZE_HOLDER_USERSPACE, NULL);
}

static void fs_bdev_mark_dead(struct block_device *bdev, bool surprise)
{
	struct super_dev *sb_dev;
	dev_t dev = bdev->bd_dev;

	mutex_unlock(&bdev->bd_holder_lock);

	for (sb_dev = super_dev_first(dev); sb_dev; sb_dev = super_dev_next(sb_dev)) {
		struct super_block *sb = sb_dev->sd_sb;

		if (!super_lock_shared(sb))
			continue;
		if (sb->s_root && (sb->s_flags & SB_ACTIVE)) {
			if (!sb->s_op->remove_bdev ||
			    sb->s_op->remove_bdev(sb, bdev)) {
				if (!surprise)
					sync_filesystem(sb);
				shrink_dcache_sb(sb);
				evict_inodes(sb);
				if (sb->s_op->shutdown)
					sb->s_op->shutdown(sb);
			}
		}
		super_unlock_shared(sb);
	}
}

static void fs_bdev_sync(struct block_device *bdev)
{
	struct super_dev *sb_dev;
	dev_t dev = bdev->bd_dev;

	mutex_unlock(&bdev->bd_holder_lock);

	for (sb_dev = super_dev_first(dev); sb_dev; sb_dev = super_dev_next(sb_dev)) {
		struct super_block *sb = sb_dev->sd_sb;

		if (!super_lock_shared(sb))
			continue;
		if (sb->s_root && (sb->s_flags & SB_ACTIVE))
			sync_filesystem(sb);
		super_unlock_shared(sb);
	}
}

/**
 * fs_bdev_freeze - freeze every superblock using a block device
 * @bdev: block device
 *
 * Freeze each live superblock using @bdev.  A superblock owning several block
 * devices is frozen once per device and stays frozen until all are thawed; the
 * block layer nests these freezes so the count stays balanced.
 *
 * Return: 0, or the error from the one superblock on a single-fs device.  When
 *         several superblocks share @bdev a per-superblock failure is swallowed
 *         (see below), but a sync_blockdev() failure is always reported.
 */
static int fs_bdev_freeze(struct block_device *bdev)
{
	dev_t dev = bdev->bd_dev;
	struct super_dev *sb_dev;
	unsigned int count = 0;
	int error = 0, err;

	lockdep_assert_held(&bdev->bd_fsfreeze_mutex);

	mutex_unlock(&bdev->bd_holder_lock);

	for (sb_dev = super_dev_first(dev); sb_dev; sb_dev = super_dev_next(sb_dev)) {
		if (!get_active_super(sb_dev->sd_sb))
			continue;
		err = fs_super_freeze(sb_dev->sd_sb);
		if (err && !error)
			error = err;
		deactivate_super(sb_dev->sd_sb);
		count++;
	}

	/*
	 * When several superblocks share the device, keep it frozen even if some
	 * of them failed to freeze and swallow the error: rolling the rest back
	 * via thaw_super() can fail too, so neither is a clear win. A single
	 * filesystem (count == 1) still reports its error.
	 */
	if (error && count > 1)
		error = 0;
	if (!error)
		error = sync_blockdev(bdev);
	return error;
}

/**
 * fs_bdev_thaw - thaw every superblock using a block device
 * @bdev: block device
 *
 * The counterpart to fs_bdev_freeze(): thaw each live superblock using @bdev.
 * A zero return does not imply a superblock is fully unfrozen; it may have been
 * frozen more than once (by the kernel or via another device).
 *
 * Return: 0, or the first error on a single-fs device; a shared device swallows
 *         per-superblock errors, as fs_bdev_freeze() does.
 */
static int fs_bdev_thaw(struct block_device *bdev)
{
	dev_t dev = bdev->bd_dev;
	struct super_dev *sb_dev;
	unsigned int count = 0;
	int error = 0, err;

	lockdep_assert_held(&bdev->bd_fsfreeze_mutex);

	mutex_unlock(&bdev->bd_holder_lock);

	for (sb_dev = super_dev_first(dev); sb_dev; sb_dev = super_dev_next(sb_dev)) {
		if (!get_active_super(sb_dev->sd_sb))
			continue;
		err = fs_super_thaw(sb_dev->sd_sb);
		if (err && !error)
			error = err;
		deactivate_super(sb_dev->sd_sb);
		count++;
	}

	/* Shared device: swallow per-superblock errors, like fs_bdev_freeze(). */
	if (error && count > 1)
		error = 0;
	return error;
}

static const struct blk_holder_ops fs_holder_ops = {
	.mark_dead		= fs_bdev_mark_dead,
	.sync			= fs_bdev_sync,
	.freeze			= fs_bdev_freeze,
	.thaw			= fs_bdev_thaw,
};

static struct super_dev *super_dev_lookup(dev_t dev, struct super_block *sb)
{
	struct super_dev *it;
	struct rhlist_head *list, *pos;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "suspicious super_dev_lookup() usage");
	VFS_WARN_ON_ONCE(!dev);
	VFS_WARN_ON_ONCE(!sb);

	list = rhltable_lookup(&super_dev_table, &dev, super_dev_params);
	rhl_for_each_entry_rcu(it, pos, list, sd_node) {
		if (it->sd_sb == sb)
			return it;
	}

	return NULL;
}

static int fs_bdev_register(struct file *bdev_file, struct super_block *sb)
{
	struct super_dev *sb_dev __free(kfree) = NULL;
	dev_t dev = file_bdev(bdev_file)->bd_dev;
	int err;

	scoped_guard(rcu) {
		sb_dev = super_dev_lookup(dev, sb);
		if (sb_dev && refcount_inc_not_zero(&sb_dev->sd_ref)) {
			retain_and_null_ptr(sb_dev);
			return 0;
		}
	}

	sb_dev = super_dev_alloc(dev, sb);
	if (!sb_dev)
		return -ENOMEM;

	err = super_dev_insert(sb_dev);
	if (err)
		return err;

	/* Publish the entry before reading the count; pairs with bdev_freeze(). */
	smp_mb();
	if (atomic_read(&file_bdev(bdev_file)->bd_fsfreeze_count) > 0) {
		err = -EBUSY;
		super_dev_put(sb_dev);
	}

	retain_and_null_ptr(sb_dev);
	return err;
}

/**
 * fs_bdev_file_open_by_dev - claim a block device on behalf of a superblock
 * @dev: block device number
 * @mode: open mode
 * @holder: block-layer exclusivity token (a superblock, or the file_system_type
 *          when the device may be shared by several superblocks of that type)
 * @sb: superblock to drive fs_holder_ops events for
 *
 * Open @dev with &fs_holder_ops and register that @sb uses it, so device
 * removal/sync/freeze/thaw are propagated to @sb (and any other superblock
 * sharing @dev).  Must be paired with fs_bdev_file_release().
 *
 * Return: an opened block-device file or an ERR_PTR().
 */
struct file *fs_bdev_file_open_by_dev(dev_t dev, blk_mode_t mode, void *holder,
				      struct super_block *sb)
{
	struct file *bdev_file;
	int err;

	bdev_file = bdev_file_open_by_dev(dev, mode, holder, &fs_holder_ops);
	if (IS_ERR(bdev_file))
		return bdev_file;

	err = fs_bdev_register(bdev_file, sb);
	if (err) {
		bdev_fput(bdev_file);
		return ERR_PTR(err);
	}
	return bdev_file;
}
EXPORT_SYMBOL_GPL(fs_bdev_file_open_by_dev);

/**
 * fs_bdev_file_open_by_path - claim a block device on behalf of a superblock
 * @path: path to the block device
 * @mode: open mode
 * @holder: block-layer exclusivity token (a superblock, or the file_system_type
 *          when the device may be shared by several superblocks of that type)
 * @sb: superblock to drive fs_holder_ops events for
 *
 * Open the block device at @path with &fs_holder_ops and register that @sb
 * uses it, so device removal/sync/freeze/thaw are propagated to @sb (and any
 * other superblock sharing the device).  Must be paired with
 * fs_bdev_file_release().
 *
 * Return: an opened block-device file or an ERR_PTR().
 */
struct file *fs_bdev_file_open_by_path(const char *path, blk_mode_t mode,
				       void *holder, struct super_block *sb)
{
	struct file *bdev_file;
	int err;

	bdev_file = bdev_file_open_by_path(path, mode, holder, &fs_holder_ops);
	if (IS_ERR(bdev_file))
		return bdev_file;

	err = fs_bdev_register(bdev_file, sb);
	if (err) {
		bdev_fput(bdev_file);
		return ERR_PTR(err);
	}
	return bdev_file;
}
EXPORT_SYMBOL_GPL(fs_bdev_file_open_by_path);

/**
 * fs_bdev_unregister - drop a superblock's claim on a block device
 * @bdev_file: file returned by fs_bdev_file_open_by_{dev,path}()
 * @sb: superblock the device was claimed for
 *
 * The inverse of fs_bdev_register(): drop one claim on the {dev, @sb} entry
 * (the last claim unregisters it; a pinning cursor defers the actual unlink)
 * without closing the device.  A caller that must act on the still-open device
 * between unregistering and closing - e.g. re-allow freezing one denied for a
 * membership change - pairs this with bdev_fput().  fs_bdev_file_release() is
 * the common unregister-and-close.
 */
void fs_bdev_unregister(struct file *bdev_file, struct super_block *sb)
{
	dev_t dev = file_bdev(bdev_file)->bd_dev;
	struct super_dev *sb_dev;

	rcu_read_lock();
	sb_dev = super_dev_lookup(dev, sb);
	rcu_read_unlock();
	super_dev_put(sb_dev);
}
EXPORT_SYMBOL_GPL(fs_bdev_unregister);

/**
 * fs_bdev_file_release - release a block device claimed for a superblock
 * @bdev_file: file returned by fs_bdev_file_open_by_{dev,path}()
 * @sb: superblock the device was claimed for
 *
 * Unregister the {dev, @sb} entry, then close the block device.
 */
void fs_bdev_file_release(struct file *bdev_file, struct super_block *sb)
{
	fs_bdev_unregister(bdev_file, sb);
	bdev_fput(bdev_file);
}
EXPORT_SYMBOL_GPL(fs_bdev_file_release);

int setup_bdev_super(struct super_block *sb, int sb_flags,
		struct fs_context *fc)
{
	blk_mode_t mode = sb_open_mode(sb_flags);
	struct file *bdev_file;
	struct block_device *bdev;

	bdev_file = fs_bdev_file_open_by_dev(sb->s_dev, mode, sb, sb);
	if (IS_ERR(bdev_file)) {
		if (fc)
			errorf(fc, "%s: Can't open blockdev", fc->source);
		return PTR_ERR(bdev_file);
	}
	bdev = file_bdev(bdev_file);

	/*
	 * This really should be in blkdev_get_by_dev, but right now can't due
	 * to legacy issues that require us to allow opening a block device node
	 * writable from userspace even for a read-only block device.
	 */
	if ((mode & BLK_OPEN_WRITE) && bdev_read_only(bdev)) {
		fs_bdev_file_release(bdev_file, sb);
		return -EACCES;
	}

	/* The sget_fc() entry is already published; pairs with bdev_freeze(). */
	smp_mb();
	if (atomic_read(&bdev->bd_fsfreeze_count) > 0) {
		if (fc)
			warnf(fc, "%pg: Can't mount, blockdev is frozen", bdev);
		fs_bdev_file_release(bdev_file, sb);
		return -EBUSY;
	}

	spin_lock(&sb_lock);
	sb->s_bdev_file = bdev_file;
	sb->s_bdev = bdev;
	sb->s_bdi = bdi_get(bdev->bd_disk->bdi);
	if (bdev_stable_writes(bdev))
		sb->s_iflags |= SB_I_STABLE_WRITES;
	spin_unlock(&sb_lock);

	snprintf(sb->s_id, sizeof(sb->s_id), "%pg", bdev);
	shrinker_debugfs_rename(sb->s_shrink, "sb-%s:%s", sb->s_type->name,
				sb->s_id);
	sb_set_blocksize(sb, block_size(bdev));
	return 0;
}
EXPORT_SYMBOL_GPL(setup_bdev_super);

/**
 * get_tree_bdev_flags - Get a superblock based on a single block device
 * @fc: The filesystem context holding the parameters
 * @fill_super: Helper to initialise a new superblock
 * @flags: GET_TREE_BDEV_* flags
 */
int get_tree_bdev_flags(struct fs_context *fc,
		int (*fill_super)(struct super_block *sb,
				  struct fs_context *fc), unsigned int flags)
{
	struct super_block *s;
	int error = 0;
	dev_t dev;

	if (!fc->source)
		return invalf(fc, "No source specified");

	error = lookup_bdev(fc->source, &dev);
	if (error) {
		if (!(flags & GET_TREE_BDEV_QUIET_LOOKUP))
			errorf(fc, "%s: Can't lookup blockdev", fc->source);
		return error;
	}
	fc->sb_flags |= SB_NOSEC;
	s = sget_dev(fc, dev);
	if (IS_ERR(s))
		return PTR_ERR(s);

	if (s->s_root) {
		/* Don't summarily change the RO/RW state. */
		if ((fc->sb_flags ^ s->s_flags) & SB_RDONLY) {
			warnf(fc, "%pg: Can't mount, would change RO state", s->s_bdev);
			deactivate_locked_super(s);
			return -EBUSY;
		}
	} else {
		error = setup_bdev_super(s, fc->sb_flags, fc);
		if (!error)
			error = fill_super(s, fc);
		if (error) {
			deactivate_locked_super(s);
			return error;
		}
		s->s_flags |= SB_ACTIVE;
	}

	BUG_ON(fc->root);
	fc->root = dget(s->s_root);
	return 0;
}
EXPORT_SYMBOL_GPL(get_tree_bdev_flags);

/**
 * get_tree_bdev - Get a superblock based on a single block device
 * @fc: The filesystem context holding the parameters
 * @fill_super: Helper to initialise a new superblock
 */
int get_tree_bdev(struct fs_context *fc,
		int (*fill_super)(struct super_block *,
				  struct fs_context *))
{
	return get_tree_bdev_flags(fc, fill_super, 0);
}
EXPORT_SYMBOL(get_tree_bdev);

void kill_block_super(struct super_block *sb)
{
	struct block_device *bdev = sb->s_bdev;

	generic_shutdown_super(sb);
	if (bdev) {
		sync_blockdev(bdev);
		fs_bdev_file_release(sb->s_bdev_file, sb);
	}
}

EXPORT_SYMBOL(kill_block_super);
#endif

/**
 * vfs_get_tree - Get the mountable root
 * @fc: The superblock configuration context.
 *
 * The filesystem is invoked to get or create a superblock which can then later
 * be used for mounting.  The filesystem places a pointer to the root to be
 * used for mounting in @fc->root.
 */
int vfs_get_tree(struct fs_context *fc)
{
	struct super_block *sb;
	int error;

	if (fc->root)
		return -EBUSY;

	/* Get the mountable root in fc->root, with a ref on the root and a ref
	 * on the superblock.
	 */
	error = fc->ops->get_tree(fc);
	if (error < 0)
		return error;

	if (!fc->root) {
		pr_err("Filesystem %s get_tree() didn't set fc->root, returned %i\n",
		       fc->fs_type->name, error);
		/* We don't know what the locking state of the superblock is -
		 * if there is a superblock.
		 */
		BUG();
	}

	sb = fc->root->d_sb;
	WARN_ON(!sb->s_bdi);

	/*
	 * super_wake() contains a memory barrier which also care of
	 * ordering for super_cache_count(). We place it before setting
	 * SB_BORN as the data dependency between the two functions is
	 * the superblock structure contents that we just set up, not
	 * the SB_BORN flag.
	 */
	super_wake(sb, SB_BORN);

	error = security_sb_set_mnt_opts(sb, fc->security, 0, NULL);
	if (unlikely(error)) {
		fc_drop_locked(fc);
		return error;
	}

	/*
	 * filesystems should never set s_maxbytes larger than MAX_LFS_FILESIZE
	 * but s_maxbytes was an unsigned long long for many releases. Throw
	 * this warning for a little while to try and catch filesystems that
	 * violate this rule.
	 */
	WARN((sb->s_maxbytes < 0), "%s set sb->s_maxbytes to "
		"negative value (%lld)\n", fc->fs_type->name, sb->s_maxbytes);

	return 0;
}
EXPORT_SYMBOL(vfs_get_tree);

/*
 * Setup private BDI for given superblock. It gets automatically cleaned up
 * in generic_shutdown_super().
 */
int super_setup_bdi_name(struct super_block *sb, char *fmt, ...)
{
	struct backing_dev_info *bdi;
	int err;
	va_list args;

	bdi = bdi_alloc(NUMA_NO_NODE);
	if (!bdi)
		return -ENOMEM;

	va_start(args, fmt);
	err = bdi_register_va(bdi, fmt, args);
	va_end(args);
	if (err) {
		bdi_put(bdi);
		return err;
	}
	WARN_ON(sb->s_bdi != &noop_backing_dev_info);
	sb->s_bdi = bdi;
	sb->s_iflags |= SB_I_PERSB_BDI;

	return 0;
}
EXPORT_SYMBOL(super_setup_bdi_name);

/*
 * Setup private BDI for given superblock. I gets automatically cleaned up
 * in generic_shutdown_super().
 */
int super_setup_bdi(struct super_block *sb)
{
	static atomic_long_t bdi_seq = ATOMIC_LONG_INIT(0);

	return super_setup_bdi_name(sb, "%.28s-%ld", sb->s_type->name,
				    atomic_long_inc_return(&bdi_seq));
}
EXPORT_SYMBOL(super_setup_bdi);

/**
 * sb_wait_write - wait until all writers to given file system finish
 * @sb: the super for which we wait
 * @level: type of writers we wait for (normal vs page fault)
 *
 * This function waits until there are no writers of given type to given file
 * system.
 */
static void sb_wait_write(struct super_block *sb, int level)
{
	percpu_down_write(sb->s_writers.rw_sem + level-1);
}

/*
 * We are going to return to userspace and forget about these locks, the
 * ownership goes to the caller of thaw_super() which does unlock().
 */
static void lockdep_sb_freeze_release(struct super_block *sb)
{
	int level;

	for (level = SB_FREEZE_LEVELS - 1; level >= 0; level--)
		percpu_rwsem_release(sb->s_writers.rw_sem + level, _THIS_IP_);
}

/*
 * Tell lockdep we are holding these locks before we call ->unfreeze_fs(sb).
 */
static void lockdep_sb_freeze_acquire(struct super_block *sb)
{
	int level;

	for (level = 0; level < SB_FREEZE_LEVELS; ++level)
		percpu_rwsem_acquire(sb->s_writers.rw_sem + level, 0, _THIS_IP_);
}

static void sb_freeze_unlock(struct super_block *sb, int level)
{
	for (level--; level >= 0; level--)
		percpu_up_write(sb->s_writers.rw_sem + level);
}

static int wait_for_partially_frozen(struct super_block *sb)
{
	int ret = 0;

	do {
		unsigned short old = sb->s_writers.frozen;

		up_write(&sb->s_umount);
		ret = wait_var_event_killable(&sb->s_writers.frozen,
					       sb->s_writers.frozen != old);
		down_write(&sb->s_umount);
	} while (ret == 0 &&
		 sb->s_writers.frozen != SB_UNFROZEN &&
		 sb->s_writers.frozen != SB_FREEZE_COMPLETE);

	return ret;
}

#define FREEZE_HOLDERS (FREEZE_HOLDER_KERNEL | FREEZE_HOLDER_USERSPACE)
#define FREEZE_FLAGS (FREEZE_HOLDERS | FREEZE_MAY_NEST | FREEZE_EXCL)

static inline int freeze_inc(struct super_block *sb, enum freeze_holder who)
{
	WARN_ON_ONCE((who & ~FREEZE_FLAGS));
	WARN_ON_ONCE(hweight32(who & FREEZE_HOLDERS) > 1);

	if (who & FREEZE_HOLDER_KERNEL)
		++sb->s_writers.freeze_kcount;
	if (who & FREEZE_HOLDER_USERSPACE)
		++sb->s_writers.freeze_ucount;
	return sb->s_writers.freeze_kcount + sb->s_writers.freeze_ucount;
}

static inline int freeze_dec(struct super_block *sb, enum freeze_holder who)
{
	WARN_ON_ONCE((who & ~FREEZE_FLAGS));
	WARN_ON_ONCE(hweight32(who & FREEZE_HOLDERS) > 1);

	if ((who & FREEZE_HOLDER_KERNEL) && sb->s_writers.freeze_kcount)
		--sb->s_writers.freeze_kcount;
	if ((who & FREEZE_HOLDER_USERSPACE) && sb->s_writers.freeze_ucount)
		--sb->s_writers.freeze_ucount;
	return sb->s_writers.freeze_kcount + sb->s_writers.freeze_ucount;
}

static inline bool may_freeze(struct super_block *sb, enum freeze_holder who,
			      const void *freeze_owner)
{
	lockdep_assert_held(&sb->s_umount);

	WARN_ON_ONCE((who & ~FREEZE_FLAGS));
	WARN_ON_ONCE(hweight32(who & FREEZE_HOLDERS) > 1);

	if (who & FREEZE_EXCL) {
		if (WARN_ON_ONCE(!(who & FREEZE_HOLDER_KERNEL)))
			return false;
		if (WARN_ON_ONCE(who & ~(FREEZE_EXCL | FREEZE_HOLDER_KERNEL)))
			return false;
		if (WARN_ON_ONCE(!freeze_owner))
			return false;
		/* This freeze already has a specific owner. */
		if (sb->s_writers.freeze_owner)
			return false;
		/*
		 * This is already frozen multiple times so we're just
		 * going to take a reference count and mark the freeze as
		 * being owned by the caller.
		 */
		if (sb->s_writers.freeze_kcount + sb->s_writers.freeze_ucount)
			sb->s_writers.freeze_owner = freeze_owner;
		return true;
	}

	if (who & FREEZE_HOLDER_KERNEL)
		return (who & FREEZE_MAY_NEST) ||
		       sb->s_writers.freeze_kcount == 0;
	if (who & FREEZE_HOLDER_USERSPACE)
		return (who & FREEZE_MAY_NEST) ||
		       sb->s_writers.freeze_ucount == 0;
	return false;
}

static inline bool may_unfreeze(struct super_block *sb, enum freeze_holder who,
				const void *freeze_owner)
{
	lockdep_assert_held(&sb->s_umount);

	WARN_ON_ONCE((who & ~FREEZE_FLAGS));
	WARN_ON_ONCE(hweight32(who & FREEZE_HOLDERS) > 1);

	if (who & FREEZE_EXCL) {
		if (WARN_ON_ONCE(!(who & FREEZE_HOLDER_KERNEL)))
			return false;
		if (WARN_ON_ONCE(who & ~(FREEZE_EXCL | FREEZE_HOLDER_KERNEL)))
			return false;
		if (WARN_ON_ONCE(!freeze_owner))
			return false;
		if (WARN_ON_ONCE(sb->s_writers.freeze_kcount == 0))
			return false;
		/* This isn't exclusively frozen. */
		if (!sb->s_writers.freeze_owner)
			return false;
		/* This isn't exclusively frozen by us. */
		if (sb->s_writers.freeze_owner != freeze_owner)
			return false;
		/*
		 * This is still frozen multiple times so we're just
		 * going to drop our reference count and undo our
		 * exclusive freeze.
		 */
		if ((sb->s_writers.freeze_kcount + sb->s_writers.freeze_ucount) > 1)
			sb->s_writers.freeze_owner = NULL;
		return true;
	}

	if (who & FREEZE_HOLDER_KERNEL) {
		/*
		 * Someone's trying to steal the reference belonging to
		 * @sb->s_writers.freeze_owner.
		 */
		if (sb->s_writers.freeze_kcount == 1 &&
		    sb->s_writers.freeze_owner)
			return false;
		return sb->s_writers.freeze_kcount > 0;
	}

	if (who & FREEZE_HOLDER_USERSPACE)
		return sb->s_writers.freeze_ucount > 0;

	return false;
}

/**
 * freeze_super - lock the filesystem and force it into a consistent state
 * @sb: the super to lock
 * @who: context that wants to freeze
 * @freeze_owner: owner of the freeze
 *
 * Syncs the super to make sure the filesystem is consistent and calls the fs's
 * freeze_fs.  Subsequent calls to this without first thawing the fs may return
 * -EBUSY.
 *
 * @who should be:
 * * %FREEZE_HOLDER_USERSPACE if userspace wants to freeze the fs;
 * * %FREEZE_HOLDER_KERNEL if the kernel wants to freeze the fs.
 * * %FREEZE_MAY_NEST whether nesting freeze and thaw requests is allowed.
 *
 * The @who argument distinguishes between the kernel and userspace trying to
 * freeze the filesystem.  Although there cannot be multiple kernel freezes or
 * multiple userspace freezes in effect at any given time, the kernel and
 * userspace can both hold a filesystem frozen.  The filesystem remains frozen
 * until there are no kernel or userspace freezes in effect.
 *
 * A filesystem may hold multiple devices and thus a filesystems may be
 * frozen through the block layer via multiple block devices. In this
 * case the request is marked as being allowed to nest by passing
 * FREEZE_MAY_NEST. The filesystem remains frozen until all block
 * devices are unfrozen. If multiple freezes are attempted without
 * FREEZE_MAY_NEST -EBUSY will be returned.
 *
 * During this function, sb->s_writers.frozen goes through these values:
 *
 * SB_UNFROZEN: File system is normal, all writes progress as usual.
 *
 * SB_FREEZE_WRITE: The file system is in the process of being frozen.  New
 * writes should be blocked, though page faults are still allowed. We wait for
 * all writes to complete and then proceed to the next stage.
 *
 * SB_FREEZE_PAGEFAULT: Freezing continues. Now also page faults are blocked
 * but internal fs threads can still modify the filesystem (although they
 * should not dirty new pages or inodes), writeback can run etc. After waiting
 * for all running page faults we sync the filesystem which will clean all
 * dirty pages and inodes (no new dirty pages or inodes can be created when
 * sync is running).
 *
 * SB_FREEZE_FS: The file system is frozen. Now all internal sources of fs
 * modification are blocked (e.g. XFS preallocation truncation on inode
 * reclaim). This is usually implemented by blocking new transactions for
 * filesystems that have them and need this additional guard. After all
 * internal writers are finished we call ->freeze_fs() to finish filesystem
 * freezing. Then we transition to SB_FREEZE_COMPLETE state. This state is
 * mostly auxiliary for filesystems to verify they do not modify frozen fs.
 *
 * sb->s_writers.frozen is protected by sb->s_umount.
 *
 * Return: If the freeze was successful zero is returned. If the freeze
 *         failed a negative error code is returned.
 */
int freeze_super(struct super_block *sb, enum freeze_holder who, const void *freeze_owner)
{
	int ret;

	if (!super_lock_excl(sb)) {
		WARN_ONCE(1, "Dying superblock while freezing!");
		return -EINVAL;
	}
	atomic_inc(&sb->s_active);

retry:
	if (sb->s_writers.frozen == SB_FREEZE_COMPLETE) {
		if (may_freeze(sb, who, freeze_owner))
			ret = !!WARN_ON_ONCE(freeze_inc(sb, who) == 1);
		else
			ret = -EBUSY;
		/* All freezers share a single active reference. */
		deactivate_locked_super(sb);
		return ret;
	}

	if (sb->s_writers.frozen != SB_UNFROZEN) {
		ret = wait_for_partially_frozen(sb);
		if (ret) {
			deactivate_locked_super(sb);
			return ret;
		}

		goto retry;
	}

	if (sb_rdonly(sb)) {
		/* Nothing to do really... */
		WARN_ON_ONCE(freeze_inc(sb, who) > 1);
		sb->s_writers.freeze_owner = freeze_owner;
		sb->s_writers.frozen = SB_FREEZE_COMPLETE;
		wake_up_var(&sb->s_writers.frozen);
		super_unlock_excl(sb);
		return 0;
	}

	sb->s_writers.frozen = SB_FREEZE_WRITE;
	/* Release s_umount to preserve sb_start_write -> s_umount ordering */
	super_unlock_excl(sb);
	sb_wait_write(sb, SB_FREEZE_WRITE);
	__super_lock_excl(sb);

	/* Now we go and block page faults... */
	sb->s_writers.frozen = SB_FREEZE_PAGEFAULT;
	sb_wait_write(sb, SB_FREEZE_PAGEFAULT);

	/* All writers are done so after syncing there won't be dirty data */
	ret = sync_filesystem(sb);
	if (ret) {
		sb->s_writers.frozen = SB_UNFROZEN;
		sb_freeze_unlock(sb, SB_FREEZE_PAGEFAULT);
		wake_up_var(&sb->s_writers.frozen);
		deactivate_locked_super(sb);
		return ret;
	}

	/* Now wait for internal filesystem counter */
	sb->s_writers.frozen = SB_FREEZE_FS;
	sb_wait_write(sb, SB_FREEZE_FS);

	if (sb->s_op->freeze_fs) {
		ret = sb->s_op->freeze_fs(sb);
		if (ret) {
			printk(KERN_ERR
				"VFS:Filesystem freeze failed\n");
			sb->s_writers.frozen = SB_UNFROZEN;
			sb_freeze_unlock(sb, SB_FREEZE_FS);
			wake_up_var(&sb->s_writers.frozen);
			deactivate_locked_super(sb);
			return ret;
		}
	}
	/*
	 * For debugging purposes so that fs can warn if it sees write activity
	 * when frozen is set to SB_FREEZE_COMPLETE, and for thaw_super().
	 */
	WARN_ON_ONCE(freeze_inc(sb, who) > 1);
	sb->s_writers.freeze_owner = freeze_owner;
	sb->s_writers.frozen = SB_FREEZE_COMPLETE;
	wake_up_var(&sb->s_writers.frozen);
	lockdep_sb_freeze_release(sb);
	super_unlock_excl(sb);
	return 0;
}
EXPORT_SYMBOL(freeze_super);

/*
 * Undoes the effect of a freeze_super_locked call.  If the filesystem is
 * frozen both by userspace and the kernel, a thaw call from either source
 * removes that state without releasing the other state or unlocking the
 * filesystem.
 */
static int thaw_super_locked(struct super_block *sb, enum freeze_holder who,
			     const void *freeze_owner)
{
	int error = -EINVAL;

	if (sb->s_writers.frozen != SB_FREEZE_COMPLETE)
		goto out_unlock;

	if (!may_unfreeze(sb, who, freeze_owner))
		goto out_unlock;

	/*
	 * All freezers share a single active reference.
	 * So just unlock in case there are any left.
	 */
	if (freeze_dec(sb, who))
		goto out_unlock;

	if (sb_rdonly(sb)) {
		sb->s_writers.frozen = SB_UNFROZEN;
		sb->s_writers.freeze_owner = NULL;
		wake_up_var(&sb->s_writers.frozen);
		goto out_deactivate;
	}

	lockdep_sb_freeze_acquire(sb);

	if (sb->s_op->unfreeze_fs) {
		error = sb->s_op->unfreeze_fs(sb);
		if (error) {
			pr_err("VFS: Filesystem thaw failed\n");
			freeze_inc(sb, who);
			lockdep_sb_freeze_release(sb);
			goto out_unlock;
		}
	}

	sb->s_writers.frozen = SB_UNFROZEN;
	sb->s_writers.freeze_owner = NULL;
	wake_up_var(&sb->s_writers.frozen);
	sb_freeze_unlock(sb, SB_FREEZE_FS);
out_deactivate:
	deactivate_locked_super(sb);
	return 0;

out_unlock:
	super_unlock_excl(sb);
	return error;
}

/**
 * thaw_super -- unlock filesystem
 * @sb: the super to thaw
 * @who: context that wants to freeze
 * @freeze_owner: owner of the freeze
 *
 * Unlocks the filesystem and marks it writeable again after freeze_super()
 * if there are no remaining freezes on the filesystem.
 *
 * @who should be:
 * * %FREEZE_HOLDER_USERSPACE if userspace wants to thaw the fs;
 * * %FREEZE_HOLDER_KERNEL if the kernel wants to thaw the fs.
 * * %FREEZE_MAY_NEST whether nesting freeze and thaw requests is allowed
 *
 * A filesystem may hold multiple devices and thus a filesystems may
 * have been frozen through the block layer via multiple block devices.
 * The filesystem remains frozen until all block devices are unfrozen.
 */
int thaw_super(struct super_block *sb, enum freeze_holder who,
	       const void *freeze_owner)
{
	if (!super_lock_excl(sb)) {
		WARN_ONCE(1, "Dying superblock while thawing!");
		return -EINVAL;
	}
	return thaw_super_locked(sb, who, freeze_owner);
}
EXPORT_SYMBOL(thaw_super);

/*
 * Create workqueue for deferred direct IO completions. We allocate the
 * workqueue when it's first needed. This avoids creating workqueue for
 * filesystems that don't need it and also allows us to create the workqueue
 * late enough so the we can include s_id in the name of the workqueue.
 */
int sb_init_dio_done_wq(struct super_block *sb)
{
	struct workqueue_struct *old;
	struct workqueue_struct *wq = alloc_workqueue("dio/%s",
						      WQ_MEM_RECLAIM | WQ_PERCPU,
						      0,
						      sb->s_id);
	if (!wq)
		return -ENOMEM;

	old = NULL;
	/*
	 * This has to be atomic as more DIOs can race to create the workqueue
	 */
	if (!try_cmpxchg(&sb->s_dio_done_wq, &old, wq)) {
		/* Someone created workqueue before us? Free ours... */
		destroy_workqueue(wq);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sb_init_dio_done_wq);
