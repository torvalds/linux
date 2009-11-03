/*
 * High-level sync()-related operations
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/syscalls.h>
#include <linux/linkage.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include "internal.h"

#define VALID_FLAGS (SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE| \
			SYNC_FILE_RANGE_WAIT_AFTER)

/*
 * Do the filesystem syncing work. For simple filesystems
 * writeback_inodes_sb(sb) just dirties buffers with inodes so we have to
 * submit IO for these buffers via __sync_blockdev(). This also speeds up the
 * wait == 1 case since in that case write_inode() functions do
 * sync_dirty_buffer() and thus effectively write one block at a time.
 */
static int __sync_filesystem(struct super_block *sb, int wait)
{
	/*
	 * This should be safe, as we require bdi backing to actually
	 * write out data in the first place
	 */
	if (!sb->s_bdi)
		return 0;

	/* Avoid doing twice syncing and cache pruning for quota sync */
	if (!wait) {
		writeout_quota_sb(sb, -1);
		writeback_inodes_sb(sb);
	} else {
		sync_quota_sb(sb, -1);
		sync_inodes_sb(sb);
	}
	if (sb->s_op->sync_fs)
		sb->s_op->sync_fs(sb, wait);
	return __sync_blockdev(sb->s_bdev, wait);
}

/*
 * Write out and wait upon all dirty data associated with this
 * superblock.  Filesystem data as well as the underlying block
 * device.  Takes the superblock lock.
 */
int sync_filesystem(struct super_block *sb)
{
	int ret;

	/*
	 * We need to be protected against the filesystem going from
	 * r/o to r/w or vice versa.
	 */
	WARN_ON(!rwsem_is_locked(&sb->s_umount));

	/*
	 * No point in syncing out anything if the filesystem is read-only.
	 */
	if (sb->s_flags & MS_RDONLY)
		return 0;

	ret = __sync_filesystem(sb, 0);
	if (ret < 0)
		return ret;
	return __sync_filesystem(sb, 1);
}
EXPORT_SYMBOL_GPL(sync_filesystem);

/*
 * Sync all the data for all the filesystems (called by sys_sync() and
 * emergency sync)
 *
 * This operation is careful to avoid the livelock which could easily happen
 * if two or more filesystems are being continuously dirtied.  s_need_sync
 * is used only here.  We set it against all filesystems and then clear it as
 * we sync them.  So redirtied filesystems are skipped.
 *
 * But if process A is currently running sync_filesystems and then process B
 * calls sync_filesystems as well, process B will set all the s_need_sync
 * flags again, which will cause process A to resync everything.  Fix that with
 * a local mutex.
 */
static void sync_filesystems(int wait)
{
	struct super_block *sb;
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);		/* Could be down_interruptible */
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &super_blocks, s_list)
		sb->s_need_sync = 1;

restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		if (!sb->s_need_sync)
			continue;
		sb->s_need_sync = 0;
		sb->s_count++;
		spin_unlock(&sb_lock);

		down_read(&sb->s_umount);
		if (!(sb->s_flags & MS_RDONLY) && sb->s_root && sb->s_bdi)
			__sync_filesystem(sb, wait);
		up_read(&sb->s_umount);

		/* restart only when sb is no longer on the list */
		spin_lock(&sb_lock);
		if (__put_super_and_need_restart(sb))
			goto restart;
	}
	spin_unlock(&sb_lock);
	mutex_unlock(&mutex);
}

/*
 * sync everything.  Start out by waking pdflush, because that writes back
 * all queues in parallel.
 */
SYSCALL_DEFINE0(sync)
{
	wakeup_flusher_threads(0);
	sync_filesystems(0);
	sync_filesystems(1);
	if (unlikely(laptop_mode))
		laptop_sync_completion();
	return 0;
}

static void do_sync_work(struct work_struct *work)
{
	/*
	 * Sync twice to reduce the possibility we skipped some inodes / pages
	 * because they were temporarily locked
	 */
	sync_filesystems(0);
	sync_filesystems(0);
	printk("Emergency Sync complete\n");
	kfree(work);
}

void emergency_sync(void)
{
	struct work_struct *work;

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (work) {
		INIT_WORK(work, do_sync_work);
		schedule_work(work);
	}
}

/*
 * Generic function to fsync a file.
 *
 * filp may be NULL if called via the msync of a vma.
 */
int file_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct inode * inode = dentry->d_inode;
	struct super_block * sb;
	int ret, err;

	/* sync the inode to buffers */
	ret = write_inode_now(inode, 0);

	/* sync the superblock to buffers */
	sb = inode->i_sb;
	if (sb->s_dirt && sb->s_op->write_super)
		sb->s_op->write_super(sb);

	/* .. finally sync the buffers to disk */
	err = sync_blockdev(sb->s_bdev);
	if (!ret)
		ret = err;
	return ret;
}
EXPORT_SYMBOL(file_fsync);

/**
 * vfs_fsync_range - helper to sync a range of data & metadata to disk
 * @file:		file to sync
 * @dentry:		dentry of @file
 * @start:		offset in bytes of the beginning of data range to sync
 * @end:		offset in bytes of the end of data range (inclusive)
 * @datasync:		perform only datasync
 *
 * Write back data in range @start..@end and metadata for @file to disk.  If
 * @datasync is set only metadata needed to access modified file data is
 * written.
 *
 * In case this function is called from nfsd @file may be %NULL and
 * only @dentry is set.  This can only happen when the filesystem
 * implements the export_operations API.
 */
int vfs_fsync_range(struct file *file, struct dentry *dentry, loff_t start,
		    loff_t end, int datasync)
{
	const struct file_operations *fop;
	struct address_space *mapping;
	int err, ret;

	/*
	 * Get mapping and operations from the file in case we have
	 * as file, or get the default values for them in case we
	 * don't have a struct file available.  Damn nfsd..
	 */
	if (file) {
		mapping = file->f_mapping;
		fop = file->f_op;
	} else {
		mapping = dentry->d_inode->i_mapping;
		fop = dentry->d_inode->i_fop;
	}

	if (!fop || !fop->fsync) {
		ret = -EINVAL;
		goto out;
	}

	ret = filemap_write_and_wait_range(mapping, start, end);

	/*
	 * We need to protect against concurrent writers, which could cause
	 * livelocks in fsync_buffers_list().
	 */
	mutex_lock(&mapping->host->i_mutex);
	err = fop->fsync(file, dentry, datasync);
	if (!ret)
		ret = err;
	mutex_unlock(&mapping->host->i_mutex);

out:
	return ret;
}
EXPORT_SYMBOL(vfs_fsync_range);

/**
 * vfs_fsync - perform a fsync or fdatasync on a file
 * @file:		file to sync
 * @dentry:		dentry of @file
 * @datasync:		only perform a fdatasync operation
 *
 * Write back data and metadata for @file to disk.  If @datasync is
 * set only metadata needed to access modified file data is written.
 *
 * In case this function is called from nfsd @file may be %NULL and
 * only @dentry is set.  This can only happen when the filesystem
 * implements the export_operations API.
 */
int vfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	return vfs_fsync_range(file, dentry, 0, LLONG_MAX, datasync);
}
EXPORT_SYMBOL(vfs_fsync);

static int do_fsync(unsigned int fd, int datasync)
{
	struct file *file;
	int ret = -EBADF;

	file = fget(fd);
	if (file) {
		ret = vfs_fsync(file, file->f_path.dentry, datasync);
		fput(file);
	}
	return ret;
}

SYSCALL_DEFINE1(fsync, unsigned int, fd)
{
	return do_fsync(fd, 0);
}

SYSCALL_DEFINE1(fdatasync, unsigned int, fd)
{
	return do_fsync(fd, 1);
}

/**
 * generic_write_sync - perform syncing after a write if file / inode is sync
 * @file:	file to which the write happened
 * @pos:	offset where the write started
 * @count:	length of the write
 *
 * This is just a simple wrapper about our general syncing function.
 */
int generic_write_sync(struct file *file, loff_t pos, loff_t count)
{
	if (!(file->f_flags & O_SYNC) && !IS_SYNC(file->f_mapping->host))
		return 0;
	return vfs_fsync_range(file, file->f_path.dentry, pos,
			       pos + count - 1, 1);
}
EXPORT_SYMBOL(generic_write_sync);

/*
 * sys_sync_file_range() permits finely controlled syncing over a segment of
 * a file in the range offset .. (offset+nbytes-1) inclusive.  If nbytes is
 * zero then sys_sync_file_range() will operate from offset out to EOF.
 *
 * The flag bits are:
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE: wait upon writeout of all pages in the range
 * before performing the write.
 *
 * SYNC_FILE_RANGE_WRITE: initiate writeout of all those dirty pages in the
 * range which are not presently under writeback. Note that this may block for
 * significant periods due to exhaustion of disk request structures.
 *
 * SYNC_FILE_RANGE_WAIT_AFTER: wait upon writeout of all pages in the range
 * after performing the write.
 *
 * Useful combinations of the flag bits are:
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE: ensures that all pages
 * in the range which were dirty on entry to sys_sync_file_range() are placed
 * under writeout.  This is a start-write-for-data-integrity operation.
 *
 * SYNC_FILE_RANGE_WRITE: start writeout of all dirty pages in the range which
 * are not presently under writeout.  This is an asynchronous flush-to-disk
 * operation.  Not suitable for data integrity operations.
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE (or SYNC_FILE_RANGE_WAIT_AFTER): wait for
 * completion of writeout of all pages in the range.  This will be used after an
 * earlier SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE operation to wait
 * for that operation to complete and to return the result.
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER:
 * a traditional sync() operation.  This is a write-for-data-integrity operation
 * which will ensure that all pages in the range which were dirty on entry to
 * sys_sync_file_range() are committed to disk.
 *
 *
 * SYNC_FILE_RANGE_WAIT_BEFORE and SYNC_FILE_RANGE_WAIT_AFTER will detect any
 * I/O errors or ENOSPC conditions and will return those to the caller, after
 * clearing the EIO and ENOSPC flags in the address_space.
 *
 * It should be noted that none of these operations write out the file's
 * metadata.  So unless the application is strictly performing overwrites of
 * already-instantiated disk blocks, there are no guarantees here that the data
 * will be available after a crash.
 */
SYSCALL_DEFINE(sync_file_range)(int fd, loff_t offset, loff_t nbytes,
				unsigned int flags)
{
	int ret;
	struct file *file;
	loff_t endbyte;			/* inclusive */
	int fput_needed;
	umode_t i_mode;

	ret = -EINVAL;
	if (flags & ~VALID_FLAGS)
		goto out;

	endbyte = offset + nbytes;

	if ((s64)offset < 0)
		goto out;
	if ((s64)endbyte < 0)
		goto out;
	if (endbyte < offset)
		goto out;

	if (sizeof(pgoff_t) == 4) {
		if (offset >= (0x100000000ULL << PAGE_CACHE_SHIFT)) {
			/*
			 * The range starts outside a 32 bit machine's
			 * pagecache addressing capabilities.  Let it "succeed"
			 */
			ret = 0;
			goto out;
		}
		if (endbyte >= (0x100000000ULL << PAGE_CACHE_SHIFT)) {
			/*
			 * Out to EOF
			 */
			nbytes = 0;
		}
	}

	if (nbytes == 0)
		endbyte = LLONG_MAX;
	else
		endbyte--;		/* inclusive */

	ret = -EBADF;
	file = fget_light(fd, &fput_needed);
	if (!file)
		goto out;

	i_mode = file->f_path.dentry->d_inode->i_mode;
	ret = -ESPIPE;
	if (!S_ISREG(i_mode) && !S_ISBLK(i_mode) && !S_ISDIR(i_mode) &&
			!S_ISLNK(i_mode))
		goto out_put;

	ret = do_sync_mapping_range(file->f_mapping, offset, endbyte, flags);
out_put:
	fput_light(file, fput_needed);
out:
	return ret;
}
#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS
asmlinkage long SyS_sync_file_range(long fd, loff_t offset, loff_t nbytes,
				    long flags)
{
	return SYSC_sync_file_range((int) fd, offset, nbytes,
				    (unsigned int) flags);
}
SYSCALL_ALIAS(sys_sync_file_range, SyS_sync_file_range);
#endif

/* It would be nice if people remember that not all the world's an i386
   when they introduce new system calls */
SYSCALL_DEFINE(sync_file_range2)(int fd, unsigned int flags,
				 loff_t offset, loff_t nbytes)
{
	return sys_sync_file_range(fd, offset, nbytes, flags);
}
#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS
asmlinkage long SyS_sync_file_range2(long fd, long flags,
				     loff_t offset, loff_t nbytes)
{
	return SYSC_sync_file_range2((int) fd, (unsigned int) flags,
				     offset, nbytes);
}
SYSCALL_ALIAS(sys_sync_file_range2, SyS_sync_file_range2);
#endif

/*
 * `endbyte' is inclusive
 */
int do_sync_mapping_range(struct address_space *mapping, loff_t offset,
			  loff_t endbyte, unsigned int flags)
{
	int ret;

	if (!mapping) {
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
	if (flags & SYNC_FILE_RANGE_WAIT_BEFORE) {
		ret = wait_on_page_writeback_range(mapping,
					offset >> PAGE_CACHE_SHIFT,
					endbyte >> PAGE_CACHE_SHIFT);
		if (ret < 0)
			goto out;
	}

	if (flags & SYNC_FILE_RANGE_WRITE) {
		ret = __filemap_fdatawrite_range(mapping, offset, endbyte,
						WB_SYNC_ALL);
		if (ret < 0)
			goto out;
	}

	if (flags & SYNC_FILE_RANGE_WAIT_AFTER) {
		ret = wait_on_page_writeback_range(mapping,
					offset >> PAGE_CACHE_SHIFT,
					endbyte >> PAGE_CACHE_SHIFT);
	}
out:
	return ret;
}
EXPORT_SYMBOL_GPL(do_sync_mapping_range);
