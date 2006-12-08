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
#include <linux/buffer_head.h>		/* for fsync_super() */
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/vfs.h>
#include <linux/writeback.h>		/* for the emergency remount stuff */
#include <linux/idr.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>


void get_filesystem(struct file_system_type *fs);
void put_filesystem(struct file_system_type *fs);
struct file_system_type *get_fs_type(const char *name);

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
		INIT_LIST_HEAD(&s->s_files);
		INIT_LIST_HEAD(&s->s_instances);
		INIT_HLIST_HEAD(&s->s_anon);
		INIT_LIST_HEAD(&s->s_inodes);
		init_rwsem(&s->s_umount);
		mutex_init(&s->s_lock);
		lockdep_set_class(&s->s_umount, &type->s_umount_key);
		/*
		 * The locking rules for s_lock are up to the
		 * filesystem. For example ext3fs has different
		 * lock ordering than usbfs:
		 */
		lockdep_set_class(&s->s_lock, &type->s_lock_key);
		down_write(&s->s_umount);
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
	kfree(s);
}

/* Superblock refcounting  */

/*
 * Drop a superblock's refcount.  Returns non-zero if the superblock was
 * destroyed.  The caller must hold sb_lock.
 */
int __put_super(struct super_block *sb)
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
		DQUOT_OFF(s);
		down_write(&s->s_umount);
		fs->kill_sb(s);
		put_filesystem(fs);
		put_super(s);
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

/*
 * Write out and wait upon all dirty data associated with this
 * superblock.  Filesystem data as well as the underlying block
 * device.  Takes the superblock lock.  Requires a second blkdev
 * flush by the caller to complete the operation.
 */
void __fsync_super(struct super_block *sb)
{
	sync_inodes_sb(sb, 0);
	DQUOT_SYNC(sb);
	lock_super(sb);
	if (sb->s_dirt && sb->s_op->write_super)
		sb->s_op->write_super(sb);
	unlock_super(sb);
	if (sb->s_op->sync_fs)
		sb->s_op->sync_fs(sb, 1);
	sync_blockdev(sb->s_bdev);
	sync_inodes_sb(sb, 1);
}

/*
 * Write out and wait upon all dirty data associated with this
 * superblock.  Filesystem data as well as the underlying block
 * device.  Takes the superblock lock.
 */
int fsync_super(struct super_block *sb)
{
	__fsync_super(sb);
	return sync_blockdev(sb->s_bdev);
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
	struct super_operations *sop = sb->s_op;

	if (sb->s_root) {
		shrink_dcache_for_umount(sb);
		fsync_super(sb);
		lock_super(sb);
		sb->s_flags &= ~MS_ACTIVE;
		/* bad name - it should be evict_inodes() */
		invalidate_inodes(sb);
		lock_kernel();

		if (sop->write_super && sb->s_dirt)
			sop->write_super(sb);
		if (sop->put_super)
			sop->put_super(sb);

		/* Forget any remaining inodes */
		if (invalidate_inodes(sb)) {
			printk("VFS: Busy inodes after unmount of %s. "
			   "Self-destruct in 5 seconds.  Have a nice day...\n",
			   sb->s_id);
		}

		unlock_kernel();
		unlock_super(sb);
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
	struct list_head *p;
	int err;

retry:
	spin_lock(&sb_lock);
	if (test) list_for_each(p, &type->fs_supers) {
		struct super_block *old;
		old = list_entry(p, struct super_block, s_instances);
		if (!test(old, data))
			continue;
		if (!grab_super(old))
			goto retry;
		if (s)
			destroy_super(s);
		return old;
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

static inline void write_super(struct super_block *sb)
{
	lock_super(sb);
	if (sb->s_root && sb->s_dirt)
		if (sb->s_op->write_super)
			sb->s_op->write_super(sb);
	unlock_super(sb);
}

/*
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
		if (sb->s_dirt) {
			sb->s_count++;
			spin_unlock(&sb_lock);
			down_read(&sb->s_umount);
			write_super(sb);
			up_read(&sb->s_umount);
			spin_lock(&sb_lock);
			if (__put_super_and_need_restart(sb))
				goto restart;
		}
	}
	spin_unlock(&sb_lock);
}

/*
 * Call the ->sync_fs super_op against all filesytems which are r/w and
 * which implement it.
 *
 * This operation is careful to avoid the livelock which could easily happen
 * if two or more filesystems are being continuously dirtied.  s_need_sync_fs
 * is used only here.  We set it against all filesystems and then clear it as
 * we sync them.  So redirtied filesystems are skipped.
 *
 * But if process A is currently running sync_filesytems and then process B
 * calls sync_filesystems as well, process B will set all the s_need_sync_fs
 * flags again, which will cause process A to resync everything.  Fix that with
 * a local mutex.
 *
 * (Fabian) Avoid sync_fs with clean fs & wait mode 0
 */
void sync_filesystems(int wait)
{
	struct super_block *sb;
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);		/* Could be down_interruptible */
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (!sb->s_op->sync_fs)
			continue;
		if (sb->s_flags & MS_RDONLY)
			continue;
		sb->s_need_sync_fs = 1;
	}

restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (!sb->s_need_sync_fs)
			continue;
		sb->s_need_sync_fs = 0;
		if (sb->s_flags & MS_RDONLY)
			continue;	/* hm.  Was remounted r/o meanwhile */
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (sb->s_root && (wait || sb->s_dirt))
			sb->s_op->sync_fs(sb, wait);
		up_read(&sb->s_umount);
		/* restart only when sb is no longer on the list */
		spin_lock(&sb_lock);
		if (__put_super_and_need_restart(sb))
			goto restart;
	}
	spin_unlock(&sb_lock);
	mutex_unlock(&mutex);
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

asmlinkage long sys_ustat(unsigned dev, struct ustat __user * ubuf)
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
 *	mark_files_ro
 *	@sb: superblock in question
 *
 *	All files are marked read/only.  We don't care about pending
 *	delete files so this should be used in 'force' mode only
 */

static void mark_files_ro(struct super_block *sb)
{
	struct file *f;

	file_list_lock();
	list_for_each_entry(f, &sb->s_files, f_u.fu_list) {
		if (S_ISREG(f->f_dentry->d_inode->i_mode) && file_count(f))
			f->f_mode &= ~FMODE_WRITE;
	}
	file_list_unlock();
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
	
#ifdef CONFIG_BLOCK
	if (!(flags & MS_RDONLY) && bdev_read_only(sb->s_bdev))
		return -EACCES;
#endif
	if (flags & MS_RDONLY)
		acct_auto_close(sb);
	shrink_dcache_sb(sb);
	fsync_super(sb);

	/* If we are remounting RDONLY and current sb is read/write,
	   make sure there are no rw files opened */
	if ((flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY)) {
		if (force)
			mark_files_ro(sb);
		else if (!fs_may_remount_ro(sb))
			return -EBUSY;
	}

	if (sb->s_op->remount_fs) {
		lock_super(sb);
		retval = sb->s_op->remount_fs(sb, &flags, data);
		unlock_super(sb);
		if (retval)
			return retval;
	}
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);
	return 0;
}

static void do_emergency_remount(unsigned long foo)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list) {
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (sb->s_root && sb->s_bdev && !(sb->s_flags & MS_RDONLY)) {
			/*
			 * ->remount_fs needs lock_kernel().
			 *
			 * What lock protects sb->s_flags??
			 */
			lock_kernel();
			do_remount_sb(sb, MS_RDONLY, NULL, 1);
			unlock_kernel();
		}
		drop_super(sb);
		spin_lock(&sb_lock);
	}
	spin_unlock(&sb_lock);
	printk("Emergency Remount complete\n");
}

void emergency_remount(void)
{
	pdflush_operation(do_emergency_remount, 0);
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

static struct idr unnamed_dev_idr;
static DEFINE_SPINLOCK(unnamed_dev_lock);/* protects the above */

int set_anon_super(struct super_block *s, void *data)
{
	int dev;
	int error;

 retry:
	if (idr_pre_get(&unnamed_dev_idr, GFP_ATOMIC) == 0)
		return -ENOMEM;
	spin_lock(&unnamed_dev_lock);
	error = idr_get_new(&unnamed_dev_idr, NULL, &dev);
	spin_unlock(&unnamed_dev_lock);
	if (error == -EAGAIN)
		/* We raced and lost with another CPU. */
		goto retry;
	else if (error)
		return -EAGAIN;

	if ((dev & MAX_ID_MASK) == (1 << MINORBITS)) {
		spin_lock(&unnamed_dev_lock);
		idr_remove(&unnamed_dev_idr, dev);
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
	idr_remove(&unnamed_dev_idr, slot);
	spin_unlock(&unnamed_dev_lock);
}

EXPORT_SYMBOL(kill_anon_super);

void __init unnamed_dev_init(void)
{
	idr_init(&unnamed_dev_idr);
}

void kill_litter_super(struct super_block *sb)
{
	if (sb->s_root)
		d_genocide(sb->s_root);
	kill_anon_super(sb);
}

EXPORT_SYMBOL(kill_litter_super);

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

static void bdev_uevent(struct block_device *bdev, enum kobject_action action)
{
	if (bdev->bd_disk) {
		if (bdev->bd_part)
			kobject_uevent(&bdev->bd_part->kobj, action);
		else
			kobject_uevent(&bdev->bd_disk->kobj, action);
	}
}

int get_sb_bdev(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data,
	int (*fill_super)(struct super_block *, void *, int),
	struct vfsmount *mnt)
{
	struct block_device *bdev;
	struct super_block *s;
	int error = 0;

	bdev = open_bdev_excl(dev_name, flags, fs_type);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	/*
	 * once the super is inserted into the list by sget, s_umount
	 * will protect the lockfs code from trying to start a snapshot
	 * while we are mounting
	 */
	mutex_lock(&bdev->bd_mount_mutex);
	s = sget(fs_type, test_bdev_super, set_bdev_super, bdev);
	mutex_unlock(&bdev->bd_mount_mutex);
	if (IS_ERR(s))
		goto error_s;

	if (s->s_root) {
		if ((flags ^ s->s_flags) & MS_RDONLY) {
			up_write(&s->s_umount);
			deactivate_super(s);
			error = -EBUSY;
			goto error_bdev;
		}

		close_bdev_excl(bdev);
	} else {
		char b[BDEVNAME_SIZE];

		s->s_flags = flags;
		strlcpy(s->s_id, bdevname(bdev, b), sizeof(s->s_id));
		sb_set_blocksize(s, block_size(bdev));
		error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error) {
			up_write(&s->s_umount);
			deactivate_super(s);
			goto error;
		}

		s->s_flags |= MS_ACTIVE;
		bdev_uevent(bdev, KOBJ_MOUNT);
	}

	return simple_set_mnt(mnt, s);

error_s:
	error = PTR_ERR(s);
error_bdev:
	close_bdev_excl(bdev);
error:
	return error;
}

EXPORT_SYMBOL(get_sb_bdev);

void kill_block_super(struct super_block *sb)
{
	struct block_device *bdev = sb->s_bdev;

	bdev_uevent(bdev, KOBJ_UMOUNT);
	generic_shutdown_super(sb);
	sync_blockdev(bdev);
	close_bdev_excl(bdev);
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
		up_write(&s->s_umount);
		deactivate_super(s);
		return error;
	}
	s->s_flags |= MS_ACTIVE;
	return simple_set_mnt(mnt, s);
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
			up_write(&s->s_umount);
			deactivate_super(s);
			return error;
		}
		s->s_flags |= MS_ACTIVE;
	}
	do_remount_sb(s, flags, data, 0);
	return simple_set_mnt(mnt, s);
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

	if (data) {
		secdata = alloc_secdata();
		if (!secdata)
			goto out_mnt;

		error = security_sb_copy_data(type, data, secdata);
		if (error)
			goto out_free_secdata;
	}

	error = type->get_sb(type, flags, name, data, mnt);
	if (error < 0)
		goto out_free_secdata;

 	error = security_sb_kern_mount(mnt->mnt_sb, secdata);
 	if (error)
 		goto out_sb;

	mnt->mnt_mountpoint = mnt->mnt_root;
	mnt->mnt_parent = mnt;
	up_write(&mnt->mnt_sb->s_umount);
	free_secdata(secdata);
	return mnt;
out_sb:
	dput(mnt->mnt_root);
	up_write(&mnt->mnt_sb->s_umount);
	deactivate_super(mnt->mnt_sb);
out_free_secdata:
	free_secdata(secdata);
out_mnt:
	free_vfsmnt(mnt);
out:
	return ERR_PTR(error);
}

EXPORT_SYMBOL_GPL(vfs_kern_mount);

struct vfsmount *
do_kern_mount(const char *fstype, int flags, const char *name, void *data)
{
	struct file_system_type *type = get_fs_type(fstype);
	struct vfsmount *mnt;
	if (!type)
		return ERR_PTR(-ENODEV);
	mnt = vfs_kern_mount(type, flags, name, data);
	put_filesystem(type);
	return mnt;
}

struct vfsmount *kern_mount(struct file_system_type *type)
{
	return vfs_kern_mount(type, 0, type->name, NULL);
}

EXPORT_SYMBOL(kern_mount);
