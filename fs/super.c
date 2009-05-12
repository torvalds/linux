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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/acct.h>
#include <linux/blkdev.h>
#include <linux/quotaops.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/vfs.h>
#include <linux/writeback.h>		/* for the emergency remount stuff */
#include <linux/idr.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/file.h>
#include <asm/uaccess.h>
#include "internal.h"


LIST_HEAD(super_blocks);
DEFINE_SPINLOCK(sb_lock);

/**
 *	alloc_super	-	create new superblock
 *	@type:	filesystem type superblock should belong to
 *
 *	Allocates and initializes a new &struct super_block.  alloc_super()
 *	returns a pointer new superblock or %NULL if allocation had failed.
 */
static struct super_block *alloc_super(struct file_system_type *type)
{
	struct super_block *s = kzalloc(sizeof(struct super_block),  GFP_USER);
	static struct super_operations default_op;

	if (s) {
		if (security_sb_alloc(s)) {
			kfree(s);
			s = NULL;
			goto out;
		}
		INIT_LIST_HEAD(&s->s_dirty);
		INIT_LIST_HEAD(&s->s_io);
		INIT_LIST_HEAD(&s->s_more_io);
		INIT_LIST_HEAD(&s->s_files);
		INIT_LIST_HEAD(&s->s_instances);
		INIT_HLIST_HEAD(&s->s_anon);
		INIT_LIST_HEAD(&s->s_inodes);
		INIT_LIST_HEAD(&s->s_dentry_lru);
		init_rwsem(&s->s_umount);
		mutex_init(&s->s_lock);
		lockdep_set_class(&s->s_umount, &type->s_umount_key);
		/*
		 * The locking rules for s_lock are up to the
		 * filesystem. For example ext3fs has different
		 * lock ordering than usbfs:
		 */
		lockdep_set_class(&s->s_lock, &type->s_lock_key);
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
		s->s_count = S_BIAS;
		atomic_set(&s->s_active, 1);
		mutex_init(&s->s_vfs_rename_mutex);
		mutex_init(&s->s_dquot.dqio_mutex);
		mutex_init(&s->s_dquot.dqonoff_mutex);
		init_rwsem(&s->s_dquot.dqptr_sem);
		init_waitqueue_head(&s->s_wait_unfrozen);
		s->s_maxbytes = MAX_NON_LFS;
		s->dq_op = sb_dquot_ops;
		s->s_qcop = sb_quotactl_ops;
		s->s_op = &default_op;
		s->s_time_gran = 1000000000;
	}
out:
	return s;
}

/**
 *	destroy_super	-	frees a superblock
 *	@s: superblock to free
 *
 *	Frees a superblock.
 */
static inline void destroy_super(struct super_block *s)
{
	security_sb_free(s);
	kfree(s->s_subtype);
	kfree(s->s_options);
	kfree(s);
}

/* Superblock refcounting  */

/*
 * Drop a superblock's refcount.  Returns non-zero if the superblock was
 * destroyed.  The caller must hold sb_lock.
 */
static int __put_super(struct super_block *sb)
{
	int ret = 0;

	if (!--sb->s_count) {
		destroy_super(sb);
		ret = 1;
	}
	return ret;
}

/*
 * Drop a superblock's refcount.
 * Returns non-zero if the superblock is about to be destroyed and
 * at least is already removed from super_blocks list, so if we are
 * making a loop through super blocks then we need to restart.
 * The caller must hold sb_lock.
 */
int __put_super_and_need_restart(struct super_block *sb)
{
	/* check for race with generic_shutdown_super() */
	if (list_empty(&sb->s_list)) {
		/* super block is removed, need to restart... */
		__put_super(sb);
		return 1;
	}
	/* can't be the last, since s_list is still in use */
	sb->s_count--;
	BUG_ON(sb->s_count == 0);
	return 0;
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
 *	deactivate_super	-	drop an active reference to superblock
 *	@s: superblock to deactivate
 *
 *	Drops an active reference to superblock, acquiring a temprory one if
 *	there is no active references left.  In that case we lock superblock,
 *	tell fs driver to shut it down and drop the temporary reference we
 *	had just acquired.
 */
void deactivate_super(struct super_block *s)
{
	struct file_system_type *fs = s->s_type;
	if (atomic_dec_and_lock(&s->s_active, &sb_lock)) {
		s->s_count -= S_BIAS-1;
		spin_unlock(&sb_lock);
		vfs_dq_off(s, 0);
		down_write(&s->s_umount);
		fs->kill_sb(s);
		put_filesystem(fs);
		put_super(s);
	}
}

EXPORT_SYMBOL(deactivate_super);

/**
 *	deactivate_locked_super	-	drop an active reference to superblock
 *	@s: superblock to deactivate
 *
 *	Equivalent of up_write(&s->s_umount); deactivate_super(s);, except that
 *	it does not unlock it until it's all over.  As the result, it's safe to
 *	use to dispose of new superblock on ->get_sb() failure exits - nobody
 *	will see the sucker until it's all over.  Equivalent using up_write +
 *	deactivate_super is safe for that purpose only if superblock is either
 *	safe to use or has NULL ->s_root when we unlock.
 */
void deactivate_locked_super(struct super_block *s)
{
	struct file_system_type *fs = s->s_type;
	if (atomic_dec_and_lock(&s->s_active, &sb_lock)) {
		s->s_count -= S_BIAS-1;
		spin_unlock(&sb_lock);
		vfs_dq_off(s, 0);
		fs->kill_sb(s);
		put_filesystem(fs);
		put_super(s);
	} else {
		up_write(&s->s_umount);
	}
}

EXPORT_SYMBOL(deactivate_locked_super);

/**
 *	grab_super - acquire an active reference
 *	@s: reference we are trying to make active
 *
 *	Tries to acquire an active reference.  grab_super() is used when we
 * 	had just found a superblock in super_blocks or fs_type->fs_supers
 *	and want to turn it into a full-blown active reference.  grab_super()
 *	is called with sb_lock held and drops it.  Returns 1 in case of
 *	success, 0 if we had failed (superblock contents was already dead or
 *	dying when grab_super() had been called).
 */
static int grab_super(struct super_block *s) __releases(sb_lock)
{
	s->s_count++;
	spin_unlock(&sb_lock);
	down_write(&s->s_umount);
	if (s->s_root) {
		spin_lock(&sb_lock);
		if (s->s_count > S_BIAS) {
			atomic_inc(&s->s_active);
			s->s_count--;
			spin_unlock(&sb_lock);
			return 1;
		}
		spin_unlock(&sb_lock);
	}
	up_write(&s->s_umount);
	put_super(s);
	yield();
	return 0;
}

/*
 * Superblock locking.  We really ought to get rid of these two.
 */
void lock_super(struct super_block * sb)
{
	get_fs_excl();
	mutex_lock(&sb->s_lock);
}

void unlock_super(struct super_block * sb)
{
	put_fs_excl();
	mutex_unlock(&sb->s_lock);
}

EXPORT_SYMBOL(lock_super);
EXPORT_SYMBOL(unlock_super);

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
		get_fs_excl();
		sb->s_flags &= ~MS_ACTIVE;

		/* bad name - it should be evict_inodes() */
		invalidate_inodes(sb);

		if (sop->put_super)
			sop->put_super(sb);

		/* Forget any remaining inodes */
		if (invalidate_inodes(sb)) {
			printk("VFS: Busy inodes after unmount of %s. "
			   "Self-destruct in 5 seconds.  Have a nice day...\n",
			   sb->s_id);
		}
		put_fs_excl();
	}
	spin_lock(&sb_lock);
	/* should be initialized for __put_super_and_need_restart() */
	list_del_init(&sb->s_list);
	list_del(&sb->s_instances);
	spin_unlock(&sb_lock);
	up_write(&sb->s_umount);
}

EXPORT_SYMBOL(generic_shutdown_super);

/**
 *	sget	-	find or create a superblock
 *	@type:	filesystem type superblock should belong to
 *	@test:	comparison callback
 *	@set:	setup callback
 *	@data:	argument to each of them
 */
struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			void *data)
{
	struct super_block *s = NULL;
	struct super_block *old;
	int err;

retry:
	spin_lock(&sb_lock);
	if (test) {
		list_for_each_entry(old, &type->fs_supers, s_instances) {
			if (!test(old, data))
				continue;
			if (!grab_super(old))
				goto retry;
			if (s) {
				up_write(&s->s_umount);
				destroy_super(s);
			}
			return old;
		}
	}
	if (!s) {
		spin_unlock(&sb_lock);
		s = alloc_super(type);
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
	list_add(&s->s_instances, &type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(type);
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
 * sync_supers - helper for periodic superblock writeback
 *
 * Call the write_super method if present on all dirty superblocks in
 * the system.  This is for the periodic writeback used by most older
 * filesystems.  For data integrity superblock writeback use
 * sync_filesystems() instead.
 *
 * Note: check the dirty flag before waiting, so we don't
 * hold up the sync while mounting a device. (The newly
 * mounted device won't need syncing.)
 */
void sync_supers(void)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (sb->s_op->write_super && sb->s_dirt) {
			sb->s_count++;
			spin_unlock(&sb_lock);

			down_read(&sb->s_umount);
			if (sb->s_root && sb->s_dirt)
				sb->s_op->write_super(sb);
			up_read(&sb->s_umount);

			spin_lock(&sb_lock);
			if (__put_super_and_need_restart(sb))
				goto restart;
		}
	}
	spin_unlock(&sb_lock);
}

/**
 *	get_super - get the superblock of a device
 *	@bdev: device to get the superblock for
 *	
 *	Scans the superblock list and finds the superblock of the file system
 *	mounted on the device given. %NULL is returned if no match is found.
 */

struct super_block * get_super(struct block_device *bdev)
{
	struct super_block *sb;

	if (!bdev)
		return NULL;

	spin_lock(&sb_lock);
rescan:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (sb->s_bdev == bdev) {
			sb->s_count++;
			spin_unlock(&sb_lock);
			down_read(&sb->s_umount);
			if (sb->s_root)
				return sb;
			up_read(&sb->s_umount);
			/* restart only when sb is no longer on the list */
			spin_lock(&sb_lock);
			if (__put_super_and_need_restart(sb))
				goto rescan;
		}
	}
	spin_unlock(&sb_lock);
	return NULL;
}

EXPORT_SYMBOL(get_super);
 
struct super_block * user_get_super(dev_t dev)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
rescan:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (sb->s_dev ==  dev) {
			sb->s_count++;
			spin_unlock(&sb_lock);
			down_read(&sb->s_umount);
			if (sb->s_root)
				return sb;
			up_read(&sb->s_umount);
			/* restart only when sb is no longer on the list */
			spin_lock(&sb_lock);
			if (__put_super_and_need_restart(sb))
				goto rescan;
		}
	}
	spin_unlock(&sb_lock);
	return NULL;
}

SYSCALL_DEFINE2(ustat, unsigned, dev, struct ustat __user *, ubuf)
{
        struct super_block *s;
        struct ustat tmp;
        struct kstatfs sbuf;
	int err = -EINVAL;

        s = user_get_super(new_decode_dev(dev));
        if (s == NULL)
                goto out;
	err = vfs_statfs(s->s_root, &sbuf);
	drop_super(s);
	if (err)
		goto out;

        memset(&tmp,0,sizeof(struct ustat));
        tmp.f_tfree = sbuf.f_bfree;
        tmp.f_tinode = sbuf.f_ffree;

        err = copy_to_user(ubuf,&tmp,sizeof(struct ustat)) ? -EFAULT : 0;
out:
	return err;
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
	int remount_rw;
	
#ifdef CONFIG_BLOCK
	if (!(flags & MS_RDONLY) && bdev_read_only(sb->s_bdev))
		return -EACCES;
#endif
	if (flags & MS_RDONLY)
		acct_auto_close(sb);
	shrink_dcache_sb(sb);
	sync_filesystem(sb);

	/* If we are remounting RDONLY and current sb is read/write,
	   make sure there are no rw files opened */
	if ((flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY)) {
		if (force)
			mark_files_ro(sb);
		else if (!fs_may_remount_ro(sb)) {
			unlock_kernel();
			return -EBUSY;
		}
		retval = vfs_dq_off(sb, 1);
		if (retval < 0 && retval != -ENOSYS) {
			unlock_kernel();
			return -EBUSY;
		}
	}
	remount_rw = !(flags & MS_RDONLY) && (sb->s_flags & MS_RDONLY);

	if (sb->s_op->remount_fs) {
		retval = sb->s_op->remount_fs(sb, &flags, data);
		if (retval) {
			unlock_kernel();
			return retval;
		}
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);
	if (remount_rw)
		vfs_dq_quota_on_remount(sb);
	return 0;
}

static void do_emergency_remount(struct work_struct *work)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_write(&sb->s_umount);
		if (sb->s_root && sb->s_bdev && !(sb->s_flags & MS_RDONLY)) {
			/*
			 * ->remount_fs needs lock_kernel().
			 *
			 * What lock protects sb->s_flags??
			 */
			do_remount_sb(sb, MS_RDONLY, NULL, 1);
		}
		up_write(&sb->s_umount);
		put_super(sb);
		spin_lock(&sb_lock);
	}
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

int set_anon_super(struct super_block *s, void *data)
{
	int dev;
	int error;

 retry:
	if (ida_pre_get(&unnamed_dev_ida, GFP_ATOMIC) == 0)
		return -ENOMEM;
	spin_lock(&unnamed_dev_lock);
	error = ida_get_new(&unnamed_dev_ida, &dev);
	spin_unlock(&unnamed_dev_lock);
	if (error == -EAGAIN)
		/* We raced and lost with another CPU. */
		goto retry;
	else if (error)
		return -EAGAIN;

	if ((dev & MAX_ID_MASK) == (1 << MINORBITS)) {
		spin_lock(&unnamed_dev_lock);
		ida_remove(&unnamed_dev_ida, dev);
		spin_unlock(&unnamed_dev_lock);
		return -EMFILE;
	}
	s->s_dev = MKDEV(0, dev & MINORMASK);
	return 0;
}

EXPORT_SYMBOL(set_anon_super);

void kill_anon_super(struct super_block *sb)
{
	int slot = MINOR(sb->s_dev);

	generic_shutdown_super(sb);
	spin_lock(&unnamed_dev_lock);
	ida_remove(&unnamed_dev_ida, slot);
	spin_unlock(&unnamed_dev_lock);
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

int get_sb_ns(struct file_system_type *fs_type, int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int),
	struct vfsmount *mnt)
{
	struct super_block *sb;

	sb = sget(fs_type, ns_test_super, ns_set_super, data);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		int err;
		sb->s_flags = flags;
		err = fill_super(sb, data, flags & MS_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(sb);
			return err;
		}

		sb->s_flags |= MS_ACTIVE;
	}

	simple_set_mnt(mnt, sb);
	return 0;
}

EXPORT_SYMBOL(get_sb_ns);

#ifdef CONFIG_BLOCK
static int set_bdev_super(struct super_block *s, void *data)
{
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;
	return 0;
}

static int test_bdev_super(struct super_block *s, void *data)
{
	return (void *)s->s_bdev == data;
}

int get_sb_bdev(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data,
	int (*fill_super)(struct super_block *, void *, int),
	struct vfsmount *mnt)
{
	struct block_device *bdev;
	struct super_block *s;
	fmode_t mode = FMODE_READ;
	int error = 0;

	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;

	bdev = open_bdev_exclusive(dev_name, mode, fs_type);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	/*
	 * once the super is inserted into the list by sget, s_umount
	 * will protect the lockfs code from trying to start a snapshot
	 * while we are mounting
	 */
	down(&bdev->bd_mount_sem);
	s = sget(fs_type, test_bdev_super, set_bdev_super, bdev);
	up(&bdev->bd_mount_sem);
	if (IS_ERR(s))
		goto error_s;

	if (s->s_root) {
		if ((flags ^ s->s_flags) & MS_RDONLY) {
			deactivate_locked_super(s);
			error = -EBUSY;
			goto error_bdev;
		}

		close_bdev_exclusive(bdev, mode);
	} else {
		char b[BDEVNAME_SIZE];

		s->s_flags = flags;
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

	simple_set_mnt(mnt, s);
	return 0;

error_s:
	error = PTR_ERR(s);
error_bdev:
	close_bdev_exclusive(bdev, mode);
error:
	return error;
}

EXPORT_SYMBOL(get_sb_bdev);

void kill_block_super(struct super_block *sb)
{
	struct block_device *bdev = sb->s_bdev;
	fmode_t mode = sb->s_mode;

	bdev->bd_super = NULL;
	generic_shutdown_super(sb);
	sync_blockdev(bdev);
	close_bdev_exclusive(bdev, mode);
}

EXPORT_SYMBOL(kill_block_super);
#endif

int get_sb_nodev(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int),
	struct vfsmount *mnt)
{
	int error;
	struct super_block *s = sget(fs_type, NULL, set_anon_super, NULL);

	if (IS_ERR(s))
		return PTR_ERR(s);

	s->s_flags = flags;

	error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
	if (error) {
		deactivate_locked_super(s);
		return error;
	}
	s->s_flags |= MS_ACTIVE;
	simple_set_mnt(mnt, s);
	return 0;
}

EXPORT_SYMBOL(get_sb_nodev);

static int compare_single(struct super_block *s, void *p)
{
	return 1;
}

int get_sb_single(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int),
	struct vfsmount *mnt)
{
	struct super_block *s;
	int error;

	s = sget(fs_type, compare_single, set_anon_super, NULL);
	if (IS_ERR(s))
		return PTR_ERR(s);
	if (!s->s_root) {
		s->s_flags = flags;
		error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error) {
			deactivate_locked_super(s);
			return error;
		}
		s->s_flags |= MS_ACTIVE;
	}
	do_remount_sb(s, flags, data, 0);
	simple_set_mnt(mnt, s);
	return 0;
}

EXPORT_SYMBOL(get_sb_single);

struct vfsmount *
vfs_kern_mount(struct file_system_type *type, int flags, const char *name, void *data)
{
	struct vfsmount *mnt;
	char *secdata = NULL;
	int error;

	if (!type)
		return ERR_PTR(-ENODEV);

	error = -ENOMEM;
	mnt = alloc_vfsmnt(name);
	if (!mnt)
		goto out;

	if (data && !(type->fs_flags & FS_BINARY_MOUNTDATA)) {
		secdata = alloc_secdata();
		if (!secdata)
			goto out_mnt;

		error = security_sb_copy_data(data, secdata);
		if (error)
			goto out_free_secdata;
	}

	error = type->get_sb(type, flags, name, data, mnt);
	if (error < 0)
		goto out_free_secdata;
	BUG_ON(!mnt->mnt_sb);

 	error = security_sb_kern_mount(mnt->mnt_sb, flags, secdata);
 	if (error)
 		goto out_sb;

	mnt->mnt_mountpoint = mnt->mnt_root;
	mnt->mnt_parent = mnt;
	up_write(&mnt->mnt_sb->s_umount);
	free_secdata(secdata);
	return mnt;
out_sb:
	dput(mnt->mnt_root);
	deactivate_locked_super(mnt->mnt_sb);
out_free_secdata:
	free_secdata(secdata);
out_mnt:
	free_vfsmnt(mnt);
out:
	return ERR_PTR(error);
}

EXPORT_SYMBOL_GPL(vfs_kern_mount);

static struct vfsmount *fs_set_subtype(struct vfsmount *mnt, const char *fstype)
{
	int err;
	const char *subtype = strchr(fstype, '.');
	if (subtype) {
		subtype++;
		err = -EINVAL;
		if (!subtype[0])
			goto err;
	} else
		subtype = "";

	mnt->mnt_sb->s_subtype = kstrdup(subtype, GFP_KERNEL);
	err = -ENOMEM;
	if (!mnt->mnt_sb->s_subtype)
		goto err;
	return mnt;

 err:
	mntput(mnt);
	return ERR_PTR(err);
}

struct vfsmount *
do_kern_mount(const char *fstype, int flags, const char *name, void *data)
{
	struct file_system_type *type = get_fs_type(fstype);
	struct vfsmount *mnt;
	if (!type)
		return ERR_PTR(-ENODEV);
	mnt = vfs_kern_mount(type, flags, name, data);
	if (!IS_ERR(mnt) && (type->fs_flags & FS_HAS_SUBTYPE) &&
	    !mnt->mnt_sb->s_subtype)
		mnt = fs_set_subtype(mnt, fstype);
	put_filesystem(type);
	return mnt;
}
EXPORT_SYMBOL_GPL(do_kern_mount);

struct vfsmount *kern_mount_data(struct file_system_type *type, void *data)
{
	return vfs_kern_mount(type, MS_KERNMOUNT, type->name, data);
}

EXPORT_SYMBOL_GPL(kern_mount_data);
