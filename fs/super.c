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
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/writeback.h>		/* for the emergency remount stuff */
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/backing-dev.h>
#include <linux/rculist_bl.h>
#include <linux/cleancache.h>
#include <linux/fsnotify.h>
#include <linux/lockdep.h>
#include <linux/user_namespace.h>
#include "internal.h"

static int thaw_super_locked(struct super_block *sb);

static LIST_HEAD(super_blocks);
static DEFINE_SPINLOCK(sb_lock);

static char *sb_writers_name[SB_FREEZE_LEVELS] = {
	"sb_writers",
	"sb_pagefaults",
	"sb_internal",
};

/*
 * One thing we have to be careful of with a per-sb shrinker is that we don't
 * drop the last active reference to the superblock from within the shrinker.
 * If that happens we could trigger unregistering the shrinker from within the
 * shrinker path and that leads to deadlock on the shrinker_rwsem. Hence we
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

	sb = container_of(shrink, struct super_block, s_shrink);

	/*
	 * Deadlock avoidance.  We may hold various FS locks, and we don't want
	 * to recurse into the FS that called us in clear_inode() and friends..
	 */
	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;

	if (!trylock_super(sb))
		return SHRINK_STOP;

	if (sb->s_op->nr_cached_objects)
		fs_objects = sb->s_op->nr_cached_objects(sb, sc);

	inodes = list_lru_shrink_count(&sb->s_inode_lru, sc);
	dentries = list_lru_shrink_count(&sb->s_dentry_lru, sc);
	total_objects = dentries + inodes + fs_objects + 1;
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

	up_read(&sb->s_umount);
	return freed;
}

static unsigned long super_cache_count(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	struct super_block *sb;
	long	total_objects = 0;

	sb = container_of(shrink, struct super_block, s_shrink);

	/*
	 * Don't call trylock_super as it is a potential
	 * scalability bottleneck. The counts could get updated
	 * between super_cache_count and super_cache_scan anyway.
	 * Call to super_cache_count with shrinker_rwsem held
	 * ensures the safety of call to list_lru_shrink_count() and
	 * s_op->nr_cached_objects().
	 */
	if (sb->s_op && sb->s_op->nr_cached_objects)
		total_objects = sb->s_op->nr_cached_objects(sb, sc);

	total_objects += list_lru_shrink_count(&sb->s_dentry_lru, sc);
	total_objects += list_lru_shrink_count(&sb->s_inode_lru, sc);

	total_objects = vfs_pressure_ratio(total_objects);
	return total_objects;
}

static void destroy_super_work(struct work_struct *work)
{
	struct super_block *s = container_of(work, struct super_block,
							destroy_work);
	int i;

	for (i = 0; i < SB_FREEZE_LEVELS; i++)
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
	up_write(&s->s_umount);
	list_lru_destroy(&s->s_dentry_lru);
	list_lru_destroy(&s->s_inode_lru);
	security_sb_free(s);
	put_user_ns(s->s_user_ns);
	kfree(s->s_subtype);
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
	struct super_block *s = kzalloc(sizeof(struct super_block),  GFP_USER);
	static const struct super_operations default_op;
	int i;

	if (!s)
		return NULL;

	INIT_LIST_HEAD(&s->s_mounts);
	s->s_user_ns = get_user_ns(user_ns);
	init_rwsem(&s->s_umount);
	lockdep_set_class(&s->s_umount, &type->s_umount_key);
	/*
	 * sget() can have s_umount recursion.
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
	init_waitqueue_head(&s->s_writers.wait_unfrozen);
	s->s_bdi = &noop_backing_dev_info;
	s->s_flags = flags;
	if (s->s_user_ns != &init_user_ns)
		s->s_iflags |= SB_I_NODEV;
	INIT_HLIST_NODE(&s->s_instances);
	INIT_HLIST_BL_HEAD(&s->s_roots);
	mutex_init(&s->s_sync_lock);
	INIT_LIST_HEAD(&s->s_inodes);
	spin_lock_init(&s->s_inode_list_lock);
	INIT_LIST_HEAD(&s->s_inodes_wb);
	spin_lock_init(&s->s_inode_wblist_lock);

	if (list_lru_init_memcg(&s->s_dentry_lru))
		goto fail;
	if (list_lru_init_memcg(&s->s_inode_lru))
		goto fail;
	s->s_count = 1;
	atomic_set(&s->s_active, 1);
	mutex_init(&s->s_vfs_rename_mutex);
	lockdep_set_class(&s->s_vfs_rename_mutex, &type->s_vfs_rename_key);
	init_rwsem(&s->s_dquot.dqio_sem);
	s->s_maxbytes = MAX_NON_LFS;
	s->s_op = &default_op;
	s->s_time_gran = 1000000000;
	s->cleancache_poolid = CLEANCACHE_NO_POOL;

	s->s_shrink.seeks = DEFAULT_SEEKS;
	s->s_shrink.scan_objects = super_cache_scan;
	s->s_shrink.count_objects = super_cache_count;
	s->s_shrink.batch = 1024;
	s->s_shrink.flags = SHRINKER_NUMA_AWARE | SHRINKER_MEMCG_AWARE;
	return s;

fail:
	destroy_unused_super(s);
	return NULL;
}

/* Superblock refcounting  */

/*
 * Drop a superblock's refcount.  The caller must hold sb_lock.
 */
static void __put_super(struct super_block *s)
{
	if (!--s->s_count) {
		list_del_init(&s->s_list);
		WARN_ON(s->s_dentry_lru.node);
		WARN_ON(s->s_inode_lru.node);
		WARN_ON(!list_empty(&s->s_mounts));
		security_sb_free(s);
		put_user_ns(s->s_user_ns);
		kfree(s->s_subtype);
		call_rcu(&s->rcu, destroy_super_rcu);
	}
}

/**
 *	put_super	-	drop a temporary reference to superblock
 *	@sb: superblock in question
 *
 *	Drops a temporary reference, frees superblock if there's no
 *	references left.
 */
static void put_super(struct super_block *sb)
{
	spin_lock(&sb_lock);
	__put_super(sb);
	spin_unlock(&sb_lock);
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
		cleancache_invalidate_fs(s);
		unregister_shrinker(&s->s_shrink);
		fs->kill_sb(s);

		/*
		 * Since list_lru_destroy() may sleep, we cannot call it from
		 * put_super(), where we hold the sb_lock. Therefore we destroy
		 * the lru lists right now.
		 */
		list_lru_destroy(&s->s_dentry_lru);
		list_lru_destroy(&s->s_inode_lru);

		put_filesystem(fs);
		put_super(s);
	} else {
		up_write(&s->s_umount);
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
		down_write(&s->s_umount);
		deactivate_locked_super(s);
	}
}

EXPORT_SYMBOL(deactivate_super);

/**
 *	grab_super - acquire an active reference
 *	@s: reference we are trying to make active
 *
 *	Tries to acquire an active reference.  grab_super() is used when we
 * 	had just found a superblock in super_blocks or fs_type->fs_supers
 *	and want to turn it into a full-blown active reference.  grab_super()
 *	is called with sb_lock held and drops it.  Returns 1 in case of
 *	success, 0 if we had failed (superblock contents was already dead or
 *	dying when grab_super() had been called).  Note that this is only
 *	called for superblocks not in rundown mode (== ones still on ->fs_supers
 *	of their type), so increment of ->s_count is OK here.
 */
static int grab_super(struct super_block *s) __releases(sb_lock)
{
	s->s_count++;
	spin_unlock(&sb_lock);
	down_write(&s->s_umount);
	if ((s->s_flags & SB_BORN) && atomic_inc_not_zero(&s->s_active)) {
		put_super(s);
		return 1;
	}
	up_write(&s->s_umount);
	put_super(s);
	return 0;
}

/*
 *	trylock_super - try to grab ->s_umount shared
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
 *	Note that unlike get_super() et.al. this one does *not* bump ->s_count.
 *	The reason why it's safe is that we are OK with doing trylock instead
 *	of down_read().  There's a couple of places that are OK with that, but
 *	it's very much not a general-purpose interface.
 */
bool trylock_super(struct super_block *sb)
{
	if (down_read_trylock(&sb->s_umount)) {
		if (!hlist_unhashed(&sb->s_instances) &&
		    sb->s_root && (sb->s_flags & SB_BORN))
			return true;
		up_read(&sb->s_umount);
	}

	return false;
}

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
		shrink_dcache_for_umount(sb);
		sync_filesystem(sb);
		sb->s_flags &= ~SB_ACTIVE;

		fsnotify_unmount_inodes(sb);
		cgroup_writeback_umount();

		evict_inodes(sb);

		if (sb->s_dio_done_wq) {
			destroy_workqueue(sb->s_dio_done_wq);
			sb->s_dio_done_wq = NULL;
		}

		if (sop->put_super)
			sop->put_super(sb);

		if (!list_empty(&sb->s_inodes)) {
			printk("VFS: Busy inodes after unmount of %s. "
			   "Self-destruct in 5 seconds.  Have a nice day...\n",
			   sb->s_id);
		}
	}
	spin_lock(&sb_lock);
	/* should be initialized for __put_super_and_need_restart() */
	hlist_del_init(&sb->s_instances);
	spin_unlock(&sb_lock);
	up_write(&sb->s_umount);
	if (sb->s_bdi != &noop_backing_dev_info) {
		bdi_put(sb->s_bdi);
		sb->s_bdi = &noop_backing_dev_info;
	}
}

EXPORT_SYMBOL(generic_shutdown_super);

/**
 *	sget_userns -	find or create a superblock
 *	@type:	filesystem type superblock should belong to
 *	@test:	comparison callback
 *	@set:	setup callback
 *	@flags:	mount flags
 *	@user_ns: User namespace for the super_block
 *	@data:	argument to each of them
 */
struct super_block *sget_userns(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags, struct user_namespace *user_ns,
			void *data)
{
	struct super_block *s = NULL;
	struct super_block *old;
	int err;

	if (!(flags & (SB_KERNMOUNT|SB_SUBMOUNT)) &&
	    !(type->fs_flags & FS_USERNS_MOUNT) &&
	    !capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);
retry:
	spin_lock(&sb_lock);
	if (test) {
		hlist_for_each_entry(old, &type->fs_supers, s_instances) {
			if (!test(old, data))
				continue;
			if (user_ns != old->s_user_ns) {
				spin_unlock(&sb_lock);
				destroy_unused_super(s);
				return ERR_PTR(-EBUSY);
			}
			if (!grab_super(old))
				goto retry;
			destroy_unused_super(s);
			return old;
		}
	}
	if (!s) {
		spin_unlock(&sb_lock);
		s = alloc_super(type, (flags & ~SB_SUBMOUNT), user_ns);
		if (!s)
			return ERR_PTR(-ENOMEM);
		goto retry;
	}

	err = set(s, data);
	if (err) {
		spin_unlock(&sb_lock);
		destroy_unused_super(s);
		return ERR_PTR(err);
	}
	s->s_type = type;
	strlcpy(s->s_id, type->name, sizeof(s->s_id));
	list_add_tail(&s->s_list, &super_blocks);
	hlist_add_head(&s->s_instances, &type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(type);
	err = register_shrinker(&s->s_shrink);
	if (err) {
		deactivate_locked_super(s);
		s = ERR_PTR(err);
	}
	return s;
}

EXPORT_SYMBOL(sget_userns);

/**
 *	sget	-	find or create a superblock
 *	@type:	  filesystem type superblock should belong to
 *	@test:	  comparison callback
 *	@set:	  setup callback
 *	@flags:	  mount flags
 *	@data:	  argument to each of them
 */
struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags,
			void *data)
{
	struct user_namespace *user_ns = current_user_ns();

	/* We don't yet pass the user namespace of the parent
	 * mount through to here so always use &init_user_ns
	 * until that changes.
	 */
	if (flags & SB_SUBMOUNT)
		user_ns = &init_user_ns;

	/* Ensure the requestor has permissions over the target filesystem */
	if (!(flags & (SB_KERNMOUNT|SB_SUBMOUNT)) && !ns_capable(user_ns, CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	return sget_userns(type, test, set, flags, user_ns, data);
}

EXPORT_SYMBOL(sget);

void drop_super(struct super_block *sb)
{
	up_read(&sb->s_umount);
	put_super(sb);
}

EXPORT_SYMBOL(drop_super);

void drop_super_exclusive(struct super_block *sb)
{
	up_write(&sb->s_umount);
	put_super(sb);
}
EXPORT_SYMBOL(drop_super_exclusive);

static void __iterate_supers(void (*f)(struct super_block *))
{
	struct super_block *sb, *p = NULL;

	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (hlist_unhashed(&sb->s_instances))
			continue;
		sb->s_count++;
		spin_unlock(&sb_lock);

		f(sb);

		spin_lock(&sb_lock);
		if (p)
			__put_super(p);
		p = sb;
	}
	if (p)
		__put_super(p);
	spin_unlock(&sb_lock);
}
/**
 *	iterate_supers - call function for all active superblocks
 *	@f: function to call
 *	@arg: argument to pass to it
 *
 *	Scans the superblock list and calls given function, passing it
 *	locked superblock and given argument.
 */
void iterate_supers(void (*f)(struct super_block *, void *), void *arg)
{
	struct super_block *sb, *p = NULL;

	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (hlist_unhashed(&sb->s_instances))
			continue;
		sb->s_count++;
		spin_unlock(&sb_lock);

		down_read(&sb->s_umount);
		if (sb->s_root && (sb->s_flags & SB_BORN))
			f(sb, arg);
		up_read(&sb->s_umount);

		spin_lock(&sb_lock);
		if (p)
			__put_super(p);
		p = sb;
	}
	if (p)
		__put_super(p);
	spin_unlock(&sb_lock);
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
		sb->s_count++;
		spin_unlock(&sb_lock);

		down_read(&sb->s_umount);
		if (sb->s_root && (sb->s_flags & SB_BORN))
			f(sb, arg);
		up_read(&sb->s_umount);

		spin_lock(&sb_lock);
		if (p)
			__put_super(p);
		p = sb;
	}
	if (p)
		__put_super(p);
	spin_unlock(&sb_lock);
}

EXPORT_SYMBOL(iterate_supers_type);

static struct super_block *__get_super(struct block_device *bdev, bool excl)
{
	struct super_block *sb;

	if (!bdev)
		return NULL;

	spin_lock(&sb_lock);
rescan:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (hlist_unhashed(&sb->s_instances))
			continue;
		if (sb->s_bdev == bdev) {
			sb->s_count++;
			spin_unlock(&sb_lock);
			if (!excl)
				down_read(&sb->s_umount);
			else
				down_write(&sb->s_umount);
			/* still alive? */
			if (sb->s_root && (sb->s_flags & SB_BORN))
				return sb;
			if (!excl)
				up_read(&sb->s_umount);
			else
				up_write(&sb->s_umount);
			/* nope, got unmounted */
			spin_lock(&sb_lock);
			__put_super(sb);
			goto rescan;
		}
	}
	spin_unlock(&sb_lock);
	return NULL;
}

/**
 *	get_super - get the superblock of a device
 *	@bdev: device to get the superblock for
 *
 *	Scans the superblock list and finds the superblock of the file system
 *	mounted on the device given. %NULL is returned if no match is found.
 */
struct super_block *get_super(struct block_device *bdev)
{
	return __get_super(bdev, false);
}
EXPORT_SYMBOL(get_super);

static struct super_block *__get_super_thawed(struct block_device *bdev,
					      bool excl)
{
	while (1) {
		struct super_block *s = __get_super(bdev, excl);
		if (!s || s->s_writers.frozen == SB_UNFROZEN)
			return s;
		if (!excl)
			up_read(&s->s_umount);
		else
			up_write(&s->s_umount);
		wait_event(s->s_writers.wait_unfrozen,
			   s->s_writers.frozen == SB_UNFROZEN);
		put_super(s);
	}
}

/**
 *	get_super_thawed - get thawed superblock of a device
 *	@bdev: device to get the superblock for
 *
 *	Scans the superblock list and finds the superblock of the file system
 *	mounted on the device. The superblock is returned once it is thawed
 *	(or immediately if it was not frozen). %NULL is returned if no match
 *	is found.
 */
struct super_block *get_super_thawed(struct block_device *bdev)
{
	return __get_super_thawed(bdev, false);
}
EXPORT_SYMBOL(get_super_thawed);

/**
 *	get_super_exclusive_thawed - get thawed superblock of a device
 *	@bdev: device to get the superblock for
 *
 *	Scans the superblock list and finds the superblock of the file system
 *	mounted on the device. The superblock is returned once it is thawed
 *	(or immediately if it was not frozen) and s_umount semaphore is held
 *	in exclusive mode. %NULL is returned if no match is found.
 */
struct super_block *get_super_exclusive_thawed(struct block_device *bdev)
{
	return __get_super_thawed(bdev, true);
}
EXPORT_SYMBOL(get_super_exclusive_thawed);

/**
 * get_active_super - get an active reference to the superblock of a device
 * @bdev: device to get the superblock for
 *
 * Scans the superblock list and finds the superblock of the file system
 * mounted on the device given.  Returns the superblock with an active
 * reference or %NULL if none was found.
 */
struct super_block *get_active_super(struct block_device *bdev)
{
	struct super_block *sb;

	if (!bdev)
		return NULL;

restart:
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (hlist_unhashed(&sb->s_instances))
			continue;
		if (sb->s_bdev == bdev) {
			if (!grab_super(sb))
				goto restart;
			up_write(&sb->s_umount);
			return sb;
		}
	}
	spin_unlock(&sb_lock);
	return NULL;
}

struct super_block *user_get_super(dev_t dev)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
rescan:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (hlist_unhashed(&sb->s_instances))
			continue;
		if (sb->s_dev ==  dev) {
			sb->s_count++;
			spin_unlock(&sb_lock);
			down_read(&sb->s_umount);
			/* still alive? */
			if (sb->s_root && (sb->s_flags & SB_BORN))
				return sb;
			up_read(&sb->s_umount);
			/* nope, got unmounted */
			spin_lock(&sb_lock);
			__put_super(sb);
			goto rescan;
		}
	}
	spin_unlock(&sb_lock);
	return NULL;
}

/**
 *	do_remount_sb - asks filesystem to change mount options.
 *	@sb:	superblock in question
 *	@sb_flags: revised superblock flags
 *	@data:	the rest of options
 *      @force: whether or not to force the change
 *
 *	Alters the mount options of a mounted file system.
 */
int do_remount_sb(struct super_block *sb, int sb_flags, void *data, int force)
{
	int retval;
	int remount_ro;

	if (sb->s_writers.frozen != SB_UNFROZEN)
		return -EBUSY;

#ifdef CONFIG_BLOCK
	if (!(sb_flags & SB_RDONLY) && bdev_read_only(sb->s_bdev))
		return -EACCES;
#endif

	remount_ro = (sb_flags & SB_RDONLY) && !sb_rdonly(sb);

	if (remount_ro) {
		if (!hlist_empty(&sb->s_pins)) {
			up_write(&sb->s_umount);
			group_pin_kill(&sb->s_pins);
			down_write(&sb->s_umount);
			if (!sb->s_root)
				return 0;
			if (sb->s_writers.frozen != SB_UNFROZEN)
				return -EBUSY;
			remount_ro = (sb_flags & SB_RDONLY) && !sb_rdonly(sb);
		}
	}
	shrink_dcache_sb(sb);

	/* If we are remounting RDONLY and current sb is read/write,
	   make sure there are no rw files opened */
	if (remount_ro) {
		if (force) {
			sb->s_readonly_remount = 1;
			smp_wmb();
		} else {
			retval = sb_prepare_remount_readonly(sb);
			if (retval)
				return retval;
		}
	}

	if (sb->s_op->remount_fs) {
		retval = sb->s_op->remount_fs(sb, &sb_flags, data);
		if (retval) {
			if (!force)
				goto cancel_readonly;
			/* If forced remount, go ahead despite any errors */
			WARN(1, "forced remount of a %s fs returned %i\n",
			     sb->s_type->name, retval);
		}
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (sb_flags & MS_RMT_MASK);
	/* Needs to be ordered wrt mnt_is_readonly() */
	smp_wmb();
	sb->s_readonly_remount = 0;

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
	sb->s_readonly_remount = 0;
	return retval;
}

static void do_emergency_remount_callback(struct super_block *sb)
{
	down_write(&sb->s_umount);
	if (sb->s_root && sb->s_bdev && (sb->s_flags & SB_BORN) &&
	    !sb_rdonly(sb)) {
		/*
		 * What lock protects sb->s_flags??
		 */
		do_remount_sb(sb, SB_RDONLY, NULL, 1);
	}
	up_write(&sb->s_umount);
}

static void do_emergency_remount(struct work_struct *work)
{
	__iterate_supers(do_emergency_remount_callback);
	kfree(work);
	printk("Emergency Remount complete\n");
}

void emergency_remount(void)
{
	struct work_struct *work;

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (work) {
		INIT_WORK(work, do_emergency_remount);
		schedule_work(work);
	}
}

static void do_thaw_all_callback(struct super_block *sb)
{
	down_write(&sb->s_umount);
	if (sb->s_root && sb->s_flags & SB_BORN) {
		emergency_thaw_bdev(sb);
		thaw_super_locked(sb);
	} else {
		up_write(&sb->s_umount);
	}
}

static void do_thaw_all(struct work_struct *work)
{
	__iterate_supers(do_thaw_all_callback);
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

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (work) {
		INIT_WORK(work, do_thaw_all);
		schedule_work(work);
	}
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static DEFINE_IDA(unnamed_dev_ida);
static DEFINE_SPINLOCK(unnamed_dev_lock);/* protects the above */
/* Many userspace utilities consider an FSID of 0 invalid.
 * Always return at least 1 from get_anon_bdev.
 */
static int unnamed_dev_start = 1;

int get_anon_bdev(dev_t *p)
{
	int dev;
	int error;

 retry:
	if (ida_pre_get(&unnamed_dev_ida, GFP_ATOMIC) == 0)
		return -ENOMEM;
	spin_lock(&unnamed_dev_lock);
	error = ida_get_new_above(&unnamed_dev_ida, unnamed_dev_start, &dev);
	if (!error)
		unnamed_dev_start = dev + 1;
	spin_unlock(&unnamed_dev_lock);
	if (error == -EAGAIN)
		/* We raced and lost with another CPU. */
		goto retry;
	else if (error)
		return -EAGAIN;

	if (dev >= (1 << MINORBITS)) {
		spin_lock(&unnamed_dev_lock);
		ida_remove(&unnamed_dev_ida, dev);
		if (unnamed_dev_start > dev)
			unnamed_dev_start = dev;
		spin_unlock(&unnamed_dev_lock);
		return -EMFILE;
	}
	*p = MKDEV(0, dev & MINORMASK);
	return 0;
}
EXPORT_SYMBOL(get_anon_bdev);

void free_anon_bdev(dev_t dev)
{
	int slot = MINOR(dev);
	spin_lock(&unnamed_dev_lock);
	ida_remove(&unnamed_dev_ida, slot);
	if (slot < unnamed_dev_start)
		unnamed_dev_start = slot;
	spin_unlock(&unnamed_dev_lock);
}
EXPORT_SYMBOL(free_anon_bdev);

int set_anon_super(struct super_block *s, void *data)
{
	return get_anon_bdev(&s->s_dev);
}

EXPORT_SYMBOL(set_anon_super);

void kill_anon_super(struct super_block *sb)
{
	dev_t dev = sb->s_dev;
	generic_shutdown_super(sb);
	free_anon_bdev(dev);
}

EXPORT_SYMBOL(kill_anon_super);

void kill_litter_super(struct super_block *sb)
{
	if (sb->s_root)
		d_genocide(sb->s_root);
	kill_anon_super(sb);
}

EXPORT_SYMBOL(kill_litter_super);

static int ns_test_super(struct super_block *sb, void *data)
{
	return sb->s_fs_info == data;
}

static int ns_set_super(struct super_block *sb, void *data)
{
	sb->s_fs_info = data;
	return set_anon_super(sb, NULL);
}

struct dentry *mount_ns(struct file_system_type *fs_type,
	int flags, void *data, void *ns, struct user_namespace *user_ns,
	int (*fill_super)(struct super_block *, void *, int))
{
	struct super_block *sb;

	/* Don't allow mounting unless the caller has CAP_SYS_ADMIN
	 * over the namespace.
	 */
	if (!(flags & SB_KERNMOUNT) && !ns_capable(user_ns, CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	sb = sget_userns(fs_type, ns_test_super, ns_set_super, flags,
			 user_ns, ns);
	if (IS_ERR(sb))
		return ERR_CAST(sb);

	if (!sb->s_root) {
		int err;
		err = fill_super(sb, data, flags & SB_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(sb);
			return ERR_PTR(err);
		}

		sb->s_flags |= SB_ACTIVE;
	}

	return dget(sb->s_root);
}

EXPORT_SYMBOL(mount_ns);

#ifdef CONFIG_BLOCK
static int set_bdev_super(struct super_block *s, void *data)
{
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;
	s->s_bdi = bdi_get(s->s_bdev->bd_bdi);

	return 0;
}

static int test_bdev_super(struct super_block *s, void *data)
{
	return (void *)s->s_bdev == data;
}

struct dentry *mount_bdev(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data,
	int (*fill_super)(struct super_block *, void *, int))
{
	struct block_device *bdev;
	struct super_block *s;
	fmode_t mode = FMODE_READ | FMODE_EXCL;
	int error = 0;

	if (!(flags & SB_RDONLY))
		mode |= FMODE_WRITE;

	bdev = blkdev_get_by_path(dev_name, mode, fs_type);
	if (IS_ERR(bdev))
		return ERR_CAST(bdev);

	/*
	 * once the super is inserted into the list by sget, s_umount
	 * will protect the lockfs code from trying to start a snapshot
	 * while we are mounting
	 */
	mutex_lock(&bdev->bd_fsfreeze_mutex);
	if (bdev->bd_fsfreeze_count > 0) {
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		error = -EBUSY;
		goto error_bdev;
	}
	s = sget(fs_type, test_bdev_super, set_bdev_super, flags | SB_NOSEC,
		 bdev);
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	if (IS_ERR(s))
		goto error_s;

	if (s->s_root) {
		if ((flags ^ s->s_flags) & SB_RDONLY) {
			deactivate_locked_super(s);
			error = -EBUSY;
			goto error_bdev;
		}

		/*
		 * s_umount nests inside bd_mutex during
		 * __invalidate_device().  blkdev_put() acquires
		 * bd_mutex and can't be called under s_umount.  Drop
		 * s_umount temporarily.  This is safe as we're
		 * holding an active reference.
		 */
		up_write(&s->s_umount);
		blkdev_put(bdev, mode);
		down_write(&s->s_umount);
	} else {
		s->s_mode = mode;
		snprintf(s->s_id, sizeof(s->s_id), "%pg", bdev);
		sb_set_blocksize(s, block_size(bdev));
		error = fill_super(s, data, flags & SB_SILENT ? 1 : 0);
		if (error) {
			deactivate_locked_super(s);
			goto error;
		}

		s->s_flags |= SB_ACTIVE;
		bdev->bd_super = s;
	}

	return dget(s->s_root);

error_s:
	error = PTR_ERR(s);
error_bdev:
	blkdev_put(bdev, mode);
error:
	return ERR_PTR(error);
}
EXPORT_SYMBOL(mount_bdev);

void kill_block_super(struct super_block *sb)
{
	struct block_device *bdev = sb->s_bdev;
	fmode_t mode = sb->s_mode;

	bdev->bd_super = NULL;
	generic_shutdown_super(sb);
	sync_blockdev(bdev);
	WARN_ON_ONCE(!(mode & FMODE_EXCL));
	blkdev_put(bdev, mode | FMODE_EXCL);
}

EXPORT_SYMBOL(kill_block_super);
#endif

struct dentry *mount_nodev(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int))
{
	int error;
	struct super_block *s = sget(fs_type, NULL, set_anon_super, flags, NULL);

	if (IS_ERR(s))
		return ERR_CAST(s);

	error = fill_super(s, data, flags & SB_SILENT ? 1 : 0);
	if (error) {
		deactivate_locked_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= SB_ACTIVE;
	return dget(s->s_root);
}
EXPORT_SYMBOL(mount_nodev);

static int compare_single(struct super_block *s, void *p)
{
	return 1;
}

struct dentry *mount_single(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int))
{
	struct super_block *s;
	int error;

	s = sget(fs_type, compare_single, set_anon_super, flags, NULL);
	if (IS_ERR(s))
		return ERR_CAST(s);
	if (!s->s_root) {
		error = fill_super(s, data, flags & SB_SILENT ? 1 : 0);
		if (error) {
			deactivate_locked_super(s);
			return ERR_PTR(error);
		}
		s->s_flags |= SB_ACTIVE;
	} else {
		do_remount_sb(s, flags, data, 0);
	}
	return dget(s->s_root);
}
EXPORT_SYMBOL(mount_single);

struct dentry *
mount_fs(struct file_system_type *type, int flags, const char *name, void *data)
{
	struct dentry *root;
	struct super_block *sb;
	char *secdata = NULL;
	int error = -ENOMEM;

	if (data && !(type->fs_flags & FS_BINARY_MOUNTDATA)) {
		secdata = alloc_secdata();
		if (!secdata)
			goto out;

		error = security_sb_copy_data(data, secdata);
		if (error)
			goto out_free_secdata;
	}

	root = type->mount(type, flags, name, data);
	if (IS_ERR(root)) {
		error = PTR_ERR(root);
		goto out_free_secdata;
	}
	sb = root->d_sb;
	BUG_ON(!sb);
	WARN_ON(!sb->s_bdi);
	sb->s_flags |= SB_BORN;

	error = security_sb_kern_mount(sb, flags, secdata);
	if (error)
		goto out_sb;

	/*
	 * filesystems should never set s_maxbytes larger than MAX_LFS_FILESIZE
	 * but s_maxbytes was an unsigned long long for many releases. Throw
	 * this warning for a little while to try and catch filesystems that
	 * violate this rule.
	 */
	WARN((sb->s_maxbytes < 0), "%s set sb->s_maxbytes to "
		"negative value (%lld)\n", type->name, sb->s_maxbytes);

	up_write(&sb->s_umount);
	free_secdata(secdata);
	return root;
out_sb:
	dput(root);
	deactivate_locked_super(sb);
out_free_secdata:
	free_secdata(secdata);
out:
	return ERR_PTR(error);
}

/*
 * Setup private BDI for given superblock. It gets automatically cleaned up
 * in generic_shutdown_super().
 */
int super_setup_bdi_name(struct super_block *sb, char *fmt, ...)
{
	struct backing_dev_info *bdi;
	int err;
	va_list args;

	bdi = bdi_alloc(GFP_KERNEL);
	if (!bdi)
		return -ENOMEM;

	bdi->name = sb->s_type->name;

	va_start(args, fmt);
	err = bdi_register_va(bdi, fmt, args);
	va_end(args);
	if (err) {
		bdi_put(bdi);
		return err;
	}
	WARN_ON(sb->s_bdi != &noop_backing_dev_info);
	sb->s_bdi = bdi;

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

/*
 * This is an internal function, please use sb_end_{write,pagefault,intwrite}
 * instead.
 */
void __sb_end_write(struct super_block *sb, int level)
{
	percpu_up_read(sb->s_writers.rw_sem + level-1);
}
EXPORT_SYMBOL(__sb_end_write);

/*
 * This is an internal function, please use sb_start_{write,pagefault,intwrite}
 * instead.
 */
int __sb_start_write(struct super_block *sb, int level, bool wait)
{
	bool force_trylock = false;
	int ret = 1;

#ifdef CONFIG_LOCKDEP
	/*
	 * We want lockdep to tell us about possible deadlocks with freezing
	 * but it's it bit tricky to properly instrument it. Getting a freeze
	 * protection works as getting a read lock but there are subtle
	 * problems. XFS for example gets freeze protection on internal level
	 * twice in some cases, which is OK only because we already hold a
	 * freeze protection also on higher level. Due to these cases we have
	 * to use wait == F (trylock mode) which must not fail.
	 */
	if (wait) {
		int i;

		for (i = 0; i < level - 1; i++)
			if (percpu_rwsem_is_held(sb->s_writers.rw_sem + i)) {
				force_trylock = true;
				break;
			}
	}
#endif
	if (wait && !force_trylock)
		percpu_down_read(sb->s_writers.rw_sem + level-1);
	else
		ret = percpu_down_read_trylock(sb->s_writers.rw_sem + level-1);

	WARN_ON(force_trylock && !ret);
	return ret;
}
EXPORT_SYMBOL(__sb_start_write);

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
		percpu_rwsem_release(sb->s_writers.rw_sem + level, 0, _THIS_IP_);
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

static void sb_freeze_unlock(struct super_block *sb)
{
	int level;

	for (level = SB_FREEZE_LEVELS - 1; level >= 0; level--)
		percpu_up_write(sb->s_writers.rw_sem + level);
}

/**
 * freeze_super - lock the filesystem and force it into a consistent state
 * @sb: the super to lock
 *
 * Syncs the super to make sure the filesystem is consistent and calls the fs's
 * freeze_fs.  Subsequent calls to this without first thawing the fs will return
 * -EBUSY.
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
 */
int freeze_super(struct super_block *sb)
{
	int ret;

	atomic_inc(&sb->s_active);
	down_write(&sb->s_umount);
	if (sb->s_writers.frozen != SB_UNFROZEN) {
		deactivate_locked_super(sb);
		return -EBUSY;
	}

	if (!(sb->s_flags & SB_BORN)) {
		up_write(&sb->s_umount);
		return 0;	/* sic - it's "nothing to do" */
	}

	if (sb_rdonly(sb)) {
		/* Nothing to do really... */
		sb->s_writers.frozen = SB_FREEZE_COMPLETE;
		up_write(&sb->s_umount);
		return 0;
	}

	sb->s_writers.frozen = SB_FREEZE_WRITE;
	/* Release s_umount to preserve sb_start_write -> s_umount ordering */
	up_write(&sb->s_umount);
	sb_wait_write(sb, SB_FREEZE_WRITE);
	down_write(&sb->s_umount);

	/* Now we go and block page faults... */
	sb->s_writers.frozen = SB_FREEZE_PAGEFAULT;
	sb_wait_write(sb, SB_FREEZE_PAGEFAULT);

	/* All writers are done so after syncing there won't be dirty data */
	sync_filesystem(sb);

	/* Now wait for internal filesystem counter */
	sb->s_writers.frozen = SB_FREEZE_FS;
	sb_wait_write(sb, SB_FREEZE_FS);

	if (sb->s_op->freeze_fs) {
		ret = sb->s_op->freeze_fs(sb);
		if (ret) {
			printk(KERN_ERR
				"VFS:Filesystem freeze failed\n");
			sb->s_writers.frozen = SB_UNFROZEN;
			sb_freeze_unlock(sb);
			wake_up(&sb->s_writers.wait_unfrozen);
			deactivate_locked_super(sb);
			return ret;
		}
	}
	/*
	 * For debugging purposes so that fs can warn if it sees write activity
	 * when frozen is set to SB_FREEZE_COMPLETE, and for thaw_super().
	 */
	sb->s_writers.frozen = SB_FREEZE_COMPLETE;
	lockdep_sb_freeze_release(sb);
	up_write(&sb->s_umount);
	return 0;
}
EXPORT_SYMBOL(freeze_super);

/**
 * thaw_super -- unlock filesystem
 * @sb: the super to thaw
 *
 * Unlocks the filesystem and marks it writeable again after freeze_super().
 */
static int thaw_super_locked(struct super_block *sb)
{
	int error;

	if (sb->s_writers.frozen != SB_FREEZE_COMPLETE) {
		up_write(&sb->s_umount);
		return -EINVAL;
	}

	if (sb_rdonly(sb)) {
		sb->s_writers.frozen = SB_UNFROZEN;
		goto out;
	}

	lockdep_sb_freeze_acquire(sb);

	if (sb->s_op->unfreeze_fs) {
		error = sb->s_op->unfreeze_fs(sb);
		if (error) {
			printk(KERN_ERR
				"VFS:Filesystem thaw failed\n");
			lockdep_sb_freeze_release(sb);
			up_write(&sb->s_umount);
			return error;
		}
	}

	sb->s_writers.frozen = SB_UNFROZEN;
	sb_freeze_unlock(sb);
out:
	wake_up(&sb->s_writers.wait_unfrozen);
	deactivate_locked_super(sb);
	return 0;
}

int thaw_super(struct super_block *sb)
{
	down_write(&sb->s_umount);
	return thaw_super_locked(sb);
}
EXPORT_SYMBOL(thaw_super);
