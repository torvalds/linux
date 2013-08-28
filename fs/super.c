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
#include <linux/acct.h>
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
#include "internal.h"


LIST_HEAD(super_blocks);
DEFINE_SPINLOCK(sb_lock);

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

	if (!grab_super_passive(sb))
		return SHRINK_STOP;

	if (sb->s_op->nr_cached_objects)
		fs_objects = sb->s_op->nr_cached_objects(sb, sc->nid);

	inodes = list_lru_count_node(&sb->s_inode_lru, sc->nid);
	dentries = list_lru_count_node(&sb->s_dentry_lru, sc->nid);
	total_objects = dentries + inodes + fs_objects + 1;

	/* proportion the scan between the caches */
	dentries = mult_frac(sc->nr_to_scan, dentries, total_objects);
	inodes = mult_frac(sc->nr_to_scan, inodes, total_objects);

	/*
	 * prune the dcache first as the icache is pinned by it, then
	 * prune the icache, followed by the filesystem specific caches
	 */
	freed = prune_dcache_sb(sb, dentries, sc->nid);
	freed += prune_icache_sb(sb, inodes, sc->nid);

	if (fs_objects) {
		fs_objects = mult_frac(sc->nr_to_scan, fs_objects,
								total_objects);
		freed += sb->s_op->free_cached_objects(sb, fs_objects,
						       sc->nid);
	}

	drop_super(sb);
	return freed;
}

static unsigned long super_cache_count(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	struct super_block *sb;
	long	total_objects = 0;

	sb = container_of(shrink, struct super_block, s_shrink);

	if (!grab_super_passive(sb))
		return 0;

	if (sb->s_op && sb->s_op->nr_cached_objects)
		total_objects = sb->s_op->nr_cached_objects(sb,
						 sc->nid);

	total_objects += list_lru_count_node(&sb->s_dentry_lru,
						 sc->nid);
	total_objects += list_lru_count_node(&sb->s_inode_lru,
						 sc->nid);

	total_objects = vfs_pressure_ratio(total_objects);
	drop_super(sb);
	return total_objects;
}

static int init_sb_writers(struct super_block *s, struct file_system_type *type)
{
	int err;
	int i;

	for (i = 0; i < SB_FREEZE_LEVELS; i++) {
		err = percpu_counter_init(&s->s_writers.counter[i], 0);
		if (err < 0)
			goto err_out;
		lockdep_init_map(&s->s_writers.lock_map[i], sb_writers_name[i],
				 &type->s_writers_key[i], 0);
	}
	init_waitqueue_head(&s->s_writers.wait);
	init_waitqueue_head(&s->s_writers.wait_unfrozen);
	return 0;
err_out:
	while (--i >= 0)
		percpu_counter_destroy(&s->s_writers.counter[i]);
	return err;
}

static void destroy_sb_writers(struct super_block *s)
{
	int i;

	for (i = 0; i < SB_FREEZE_LEVELS; i++)
		percpu_counter_destroy(&s->s_writers.counter[i]);
}

/**
 *	alloc_super	-	create new superblock
 *	@type:	filesystem type superblock should belong to
 *	@flags: the mount flags
 *
 *	Allocates and initializes a new &struct super_block.  alloc_super()
 *	returns a pointer new superblock or %NULL if allocation had failed.
 */
static struct super_block *alloc_super(struct file_system_type *type, int flags)
{
	struct super_block *s = kzalloc(sizeof(struct super_block),  GFP_USER);
	static const struct super_operations default_op;

	if (s) {
		if (security_sb_alloc(s))
			goto out_free_sb;

#ifdef CONFIG_SMP
		s->s_files = alloc_percpu(struct list_head);
		if (!s->s_files)
			goto err_out;
		else {
			int i;

			for_each_possible_cpu(i)
				INIT_LIST_HEAD(per_cpu_ptr(s->s_files, i));
		}
#else
		INIT_LIST_HEAD(&s->s_files);
#endif
		if (init_sb_writers(s, type))
			goto err_out;
		s->s_flags = flags;
		s->s_bdi = &default_backing_dev_info;
		INIT_HLIST_NODE(&s->s_instances);
		INIT_HLIST_BL_HEAD(&s->s_anon);
		INIT_LIST_HEAD(&s->s_inodes);

		if (list_lru_init(&s->s_dentry_lru))
			goto err_out;
		if (list_lru_init(&s->s_inode_lru))
			goto err_out_dentry_lru;

		INIT_LIST_HEAD(&s->s_mounts);
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
		s->s_count = 1;
		atomic_set(&s->s_active, 1);
		mutex_init(&s->s_vfs_rename_mutex);
		lockdep_set_class(&s->s_vfs_rename_mutex, &type->s_vfs_rename_key);
		mutex_init(&s->s_dquot.dqio_mutex);
		mutex_init(&s->s_dquot.dqonoff_mutex);
		init_rwsem(&s->s_dquot.dqptr_sem);
		s->s_maxbytes = MAX_NON_LFS;
		s->s_op = &default_op;
		s->s_time_gran = 1000000000;
		s->cleancache_poolid = -1;

		s->s_shrink.seeks = DEFAULT_SEEKS;
		s->s_shrink.scan_objects = super_cache_scan;
		s->s_shrink.count_objects = super_cache_count;
		s->s_shrink.batch = 1024;
		s->s_shrink.flags = SHRINKER_NUMA_AWARE;
	}
out:
	return s;

err_out_dentry_lru:
	list_lru_destroy(&s->s_dentry_lru);
err_out:
	security_sb_free(s);
#ifdef CONFIG_SMP
	if (s->s_files)
		free_percpu(s->s_files);
#endif
	destroy_sb_writers(s);
out_free_sb:
	kfree(s);
	s = NULL;
	goto out;
}

/**
 *	destroy_super	-	frees a superblock
 *	@s: superblock to free
 *
 *	Frees a superblock.
 */
static inline void destroy_super(struct super_block *s)
{
#ifdef CONFIG_SMP
	free_percpu(s->s_files);
#endif
	destroy_sb_writers(s);
	security_sb_free(s);
	WARN_ON(!list_empty(&s->s_mounts));
	kfree(s->s_subtype);
	kfree(s->s_options);
	kfree(s);
}

/* Superblock refcounting  */

/*
 * Drop a superblock's refcount.  The caller must hold sb_lock.
 */
static void __put_super(struct super_block *sb)
{
	if (!--sb->s_count) {
		list_del_init(&sb->s_list);
		destroy_super(sb);
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
 *	Drops an active reference to superblock, converting it into a temprory
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
		fs->kill_sb(s);

		/* caches are now gone, we can safely kill the shrinker now */
		unregister_shrinker(&s->s_shrink);
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
	if ((s->s_flags & MS_BORN) && atomic_inc_not_zero(&s->s_active)) {
		put_super(s);
		return 1;
	}
	up_write(&s->s_umount);
	put_super(s);
	return 0;
}

/*
 *	grab_super_passive - acquire a passive reference
 *	@sb: reference we are trying to grab
 *
 *	Tries to acquire a passive reference. This is used in places where we
 *	cannot take an active reference but we need to ensure that the
 *	superblock does not go away while we are working on it. It returns
 *	false if a reference was not gained, and returns true with the s_umount
 *	lock held in read mode if a reference is gained. On successful return,
 *	the caller must drop the s_umount lock and the passive reference when
 *	done.
 */
bool grab_super_passive(struct super_block *sb)
{
	spin_lock(&sb_lock);
	if (hlist_unhashed(&sb->s_instances)) {
		spin_unlock(&sb_lock);
		return false;
	}

	sb->s_count++;
	spin_unlock(&sb_lock);

	if (down_read_trylock(&sb->s_umount)) {
		if (sb->s_root && (sb->s_flags & MS_BORN))
			return true;
		up_read(&sb->s_umount);
	}

	put_super(sb);
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
		sb->s_flags &= ~MS_ACTIVE;

		fsnotify_unmount_inodes(&sb->s_inodes);

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
}

EXPORT_SYMBOL(generic_shutdown_super);

/**
 *	sget	-	find or create a superblock
 *	@type:	filesystem type superblock should belong to
 *	@test:	comparison callback
 *	@set:	setup callback
 *	@flags:	mount flags
 *	@data:	argument to each of them
 */
struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags,
			void *data)
{
	struct super_block *s = NULL;
	struct super_block *old;
	int err;

retry:
	spin_lock(&sb_lock);
	if (test) {
		hlist_for_each_entry(old, &type->fs_supers, s_instances) {
			if (!test(old, data))
				continue;
			if (!grab_super(old))
				goto retry;
			if (s) {
				up_write(&s->s_umount);
				destroy_super(s);
				s = NULL;
			}
			return old;
		}
	}
	if (!s) {
		spin_unlock(&sb_lock);
		s = alloc_super(type, flags);
		if (!s)
			return ERR_PTR(-ENOMEM);
		goto retry;
	}
		
	err = set(s, data);
	if (err) {
		spin_unlock(&sb_lock);
		up_write(&s->s_umount);
		destroy_super(s);
		return ERR_PTR(err);
	}
	s->s_type = type;
	strlcpy(s->s_id, type->name, sizeof(s->s_id));
	list_add_tail(&s->s_list, &super_blocks);
	hlist_add_head(&s->s_instances, &type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(type);
	register_shrinker(&s->s_shrink);
	return s;
}

EXPORT_SYMBOL(sget);

void drop_super(struct super_block *sb)
{
	up_read(&sb->s_umount);
	put_super(sb);
}

EXPORT_SYMBOL(drop_super);

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
		if (sb->s_root && (sb->s_flags & MS_BORN))
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
		if (sb->s_root && (sb->s_flags & MS_BORN))
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

/**
 *	get_super - get the superblock of a device
 *	@bdev: device to get the superblock for
 *	
 *	Scans the superblock list and finds the superblock of the file system
 *	mounted on the device given. %NULL is returned if no match is found.
 */

struct super_block *get_super(struct block_device *bdev)
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
			down_read(&sb->s_umount);
			/* still alive? */
			if (sb->s_root && (sb->s_flags & MS_BORN))
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

EXPORT_SYMBOL(get_super);

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
	while (1) {
		struct super_block *s = get_super(bdev);
		if (!s || s->s_writers.frozen == SB_UNFROZEN)
			return s;
		up_read(&s->s_umount);
		wait_event(s->s_writers.wait_unfrozen,
			   s->s_writers.frozen == SB_UNFROZEN);
		put_super(s);
	}
}
EXPORT_SYMBOL(get_super_thawed);

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
			if (sb->s_root && (sb->s_flags & MS_BORN))
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
 *	@flags:	numeric part of options
 *	@data:	the rest of options
 *      @force: whether or not to force the change
 *
 *	Alters the mount options of a mounted file system.
 */
int do_remount_sb(struct super_block *sb, int flags, void *data, int force)
{
	int retval;
	int remount_ro;

	if (sb->s_writers.frozen != SB_UNFROZEN)
		return -EBUSY;

#ifdef CONFIG_BLOCK
	if (!(flags & MS_RDONLY) && bdev_read_only(sb->s_bdev))
		return -EACCES;
#endif

	if (flags & MS_RDONLY)
		acct_auto_close(sb);
	shrink_dcache_sb(sb);
	sync_filesystem(sb);

	remount_ro = (flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY);

	/* If we are remounting RDONLY and current sb is read/write,
	   make sure there are no rw files opened */
	if (remount_ro) {
		if (force) {
			mark_files_ro(sb);
		} else {
			retval = sb_prepare_remount_readonly(sb);
			if (retval)
				return retval;
		}
	}

	if (sb->s_op->remount_fs) {
		retval = sb->s_op->remount_fs(sb, &flags, data);
		if (retval) {
			if (!force)
				goto cancel_readonly;
			/* If forced remount, go ahead despite any errors */
			WARN(1, "forced remount of a %s fs returned %i\n",
			     sb->s_type->name, retval);
		}
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);
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

static void do_emergency_remount(struct work_struct *work)
{
	struct super_block *sb, *p = NULL;

	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (hlist_unhashed(&sb->s_instances))
			continue;
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_write(&sb->s_umount);
		if (sb->s_root && sb->s_bdev && (sb->s_flags & MS_BORN) &&
		    !(sb->s_flags & MS_RDONLY)) {
			/*
			 * What lock protects sb->s_flags??
			 */
			do_remount_sb(sb, MS_RDONLY, NULL, 1);
		}
		up_write(&sb->s_umount);
		spin_lock(&sb_lock);
		if (p)
			__put_super(p);
		p = sb;
	}
	if (p)
		__put_super(p);
	spin_unlock(&sb_lock);
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

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static DEFINE_IDA(unnamed_dev_ida);
static DEFINE_SPINLOCK(unnamed_dev_lock);/* protects the above */
static int unnamed_dev_start = 0; /* don't bother trying below it */

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

	if (dev == (1 << MINORBITS)) {
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
	int error = get_anon_bdev(&s->s_dev);
	if (!error)
		s->s_bdi = &noop_backing_dev_info;
	return error;
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

struct dentry *mount_ns(struct file_system_type *fs_type, int flags,
	void *data, int (*fill_super)(struct super_block *, void *, int))
{
	struct super_block *sb;

	sb = sget(fs_type, ns_test_super, ns_set_super, flags, data);
	if (IS_ERR(sb))
		return ERR_CAST(sb);

	if (!sb->s_root) {
		int err;
		err = fill_super(sb, data, flags & MS_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(sb);
			return ERR_PTR(err);
		}

		sb->s_flags |= MS_ACTIVE;
	}

	return dget(sb->s_root);
}

EXPORT_SYMBOL(mount_ns);

#ifdef CONFIG_BLOCK
static int set_bdev_super(struct super_block *s, void *data)
{
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;

	/*
	 * We set the bdi here to the queue backing, file systems can
	 * overwrite this in ->fill_super()
	 */
	s->s_bdi = &bdev_get_queue(s->s_bdev)->backing_dev_info;
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

	if (!(flags & MS_RDONLY))
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
	s = sget(fs_type, test_bdev_super, set_bdev_super, flags | MS_NOSEC,
		 bdev);
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	if (IS_ERR(s))
		goto error_s;

	if (s->s_root) {
		if ((flags ^ s->s_flags) & MS_RDONLY) {
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
		char b[BDEVNAME_SIZE];

		s->s_mode = mode;
		strlcpy(s->s_id, bdevname(bdev, b), sizeof(s->s_id));
		sb_set_blocksize(s, block_size(bdev));
		error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error) {
			deactivate_locked_super(s);
			goto error;
		}

		s->s_flags |= MS_ACTIVE;
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

	error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
	if (error) {
		deactivate_locked_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= MS_ACTIVE;
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
		error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error) {
			deactivate_locked_super(s);
			return ERR_PTR(error);
		}
		s->s_flags |= MS_ACTIVE;
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
	WARN_ON(sb->s_bdi == &default_backing_dev_info);
	sb->s_flags |= MS_BORN;

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
 * This is an internal function, please use sb_end_{write,pagefault,intwrite}
 * instead.
 */
void __sb_end_write(struct super_block *sb, int level)
{
	percpu_counter_dec(&sb->s_writers.counter[level-1]);
	/*
	 * Make sure s_writers are updated before we wake up waiters in
	 * freeze_super().
	 */
	smp_mb();
	if (waitqueue_active(&sb->s_writers.wait))
		wake_up(&sb->s_writers.wait);
	rwsem_release(&sb->s_writers.lock_map[level-1], 1, _RET_IP_);
}
EXPORT_SYMBOL(__sb_end_write);

#ifdef CONFIG_LOCKDEP
/*
 * We want lockdep to tell us about possible deadlocks with freezing but
 * it's it bit tricky to properly instrument it. Getting a freeze protection
 * works as getting a read lock but there are subtle problems. XFS for example
 * gets freeze protection on internal level twice in some cases, which is OK
 * only because we already hold a freeze protection also on higher level. Due
 * to these cases we have to tell lockdep we are doing trylock when we
 * already hold a freeze protection for a higher freeze level.
 */
static void acquire_freeze_lock(struct super_block *sb, int level, bool trylock,
				unsigned long ip)
{
	int i;

	if (!trylock) {
		for (i = 0; i < level - 1; i++)
			if (lock_is_held(&sb->s_writers.lock_map[i])) {
				trylock = true;
				break;
			}
	}
	rwsem_acquire_read(&sb->s_writers.lock_map[level-1], 0, trylock, ip);
}
#endif

/*
 * This is an internal function, please use sb_start_{write,pagefault,intwrite}
 * instead.
 */
int __sb_start_write(struct super_block *sb, int level, bool wait)
{
retry:
	if (unlikely(sb->s_writers.frozen >= level)) {
		if (!wait)
			return 0;
		wait_event(sb->s_writers.wait_unfrozen,
			   sb->s_writers.frozen < level);
	}

#ifdef CONFIG_LOCKDEP
	acquire_freeze_lock(sb, level, !wait, _RET_IP_);
#endif
	percpu_counter_inc(&sb->s_writers.counter[level-1]);
	/*
	 * Make sure counter is updated before we check for frozen.
	 * freeze_super() first sets frozen and then checks the counter.
	 */
	smp_mb();
	if (unlikely(sb->s_writers.frozen >= level)) {
		__sb_end_write(sb, level);
		goto retry;
	}
	return 1;
}
EXPORT_SYMBOL(__sb_start_write);

/**
 * sb_wait_write - wait until all writers to given file system finish
 * @sb: the super for which we wait
 * @level: type of writers we wait for (normal vs page fault)
 *
 * This function waits until there are no writers of given type to given file
 * system. Caller of this function should make sure there can be no new writers
 * of type @level before calling this function. Otherwise this function can
 * livelock.
 */
static void sb_wait_write(struct super_block *sb, int level)
{
	s64 writers;

	/*
	 * We just cycle-through lockdep here so that it does not complain
	 * about returning with lock to userspace
	 */
	rwsem_acquire(&sb->s_writers.lock_map[level-1], 0, 0, _THIS_IP_);
	rwsem_release(&sb->s_writers.lock_map[level-1], 1, _THIS_IP_);

	do {
		DEFINE_WAIT(wait);

		/*
		 * We use a barrier in prepare_to_wait() to separate setting
		 * of frozen and checking of the counter
		 */
		prepare_to_wait(&sb->s_writers.wait, &wait,
				TASK_UNINTERRUPTIBLE);

		writers = percpu_counter_sum(&sb->s_writers.counter[level-1]);
		if (writers)
			schedule();

		finish_wait(&sb->s_writers.wait, &wait);
	} while (writers);
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

	if (!(sb->s_flags & MS_BORN)) {
		up_write(&sb->s_umount);
		return 0;	/* sic - it's "nothing to do" */
	}

	if (sb->s_flags & MS_RDONLY) {
		/* Nothing to do really... */
		sb->s_writers.frozen = SB_FREEZE_COMPLETE;
		up_write(&sb->s_umount);
		return 0;
	}

	/* From now on, no new normal writers can start */
	sb->s_writers.frozen = SB_FREEZE_WRITE;
	smp_wmb();

	/* Release s_umount to preserve sb_start_write -> s_umount ordering */
	up_write(&sb->s_umount);

	sb_wait_write(sb, SB_FREEZE_WRITE);

	/* Now we go and block page faults... */
	down_write(&sb->s_umount);
	sb->s_writers.frozen = SB_FREEZE_PAGEFAULT;
	smp_wmb();

	sb_wait_write(sb, SB_FREEZE_PAGEFAULT);

	/* All writers are done so after syncing there won't be dirty data */
	sync_filesystem(sb);

	/* Now wait for internal filesystem counter */
	sb->s_writers.frozen = SB_FREEZE_FS;
	smp_wmb();
	sb_wait_write(sb, SB_FREEZE_FS);

	if (sb->s_op->freeze_fs) {
		ret = sb->s_op->freeze_fs(sb);
		if (ret) {
			printk(KERN_ERR
				"VFS:Filesystem freeze failed\n");
			sb->s_writers.frozen = SB_UNFROZEN;
			smp_wmb();
			wake_up(&sb->s_writers.wait_unfrozen);
			deactivate_locked_super(sb);
			return ret;
		}
	}
	/*
	 * This is just for debugging purposes so that fs can warn if it
	 * sees write activity when frozen is set to SB_FREEZE_COMPLETE.
	 */
	sb->s_writers.frozen = SB_FREEZE_COMPLETE;
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
int thaw_super(struct super_block *sb)
{
	int error;

	down_write(&sb->s_umount);
	if (sb->s_writers.frozen == SB_UNFROZEN) {
		up_write(&sb->s_umount);
		return -EINVAL;
	}

	if (sb->s_flags & MS_RDONLY)
		goto out;

	if (sb->s_op->unfreeze_fs) {
		error = sb->s_op->unfreeze_fs(sb);
		if (error) {
			printk(KERN_ERR
				"VFS:Filesystem thaw failed\n");
			up_write(&sb->s_umount);
			return error;
		}
	}

out:
	sb->s_writers.frozen = SB_UNFROZEN;
	smp_wmb();
	wake_up(&sb->s_writers.wait_unfrozen);
	deactivate_locked_super(sb);

	return 0;
}
EXPORT_SYMBOL(thaw_super);
