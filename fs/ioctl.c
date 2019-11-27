// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ioctl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/falloc.h>
#include <linux/sched/signal.h>

#include "internal.h"

#include <asm/ioctls.h>

/* So that the fiemap access checks can't overflow on 32 bit machines. */
#define FIEMAP_MAX_EXTENTS	(UINT_MAX / sizeof(struct fiemap_extent))

/**
 * vfs_ioctl - call filesystem specific ioctl methods
 * @filp:	open file to invoke ioctl method on
 * @cmd:	ioctl command to execute
 * @arg:	command-specific argument for ioctl
 *
 * Invokes filesystem specific ->unlocked_ioctl, if one exists; otherwise
 * returns -ENOTTY.
 *
 * Returns 0 on success, -errno on error.
 */
long vfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int error = -ENOTTY;

	if (!filp->f_op->unlocked_ioctl)
		goto out;

	error = filp->f_op->unlocked_ioctl(filp, cmd, arg);
	if (error == -ENOIOCTLCMD)
		error = -ENOTTY;
 out:
	return error;
}
EXPORT_SYMBOL(vfs_ioctl);

static int ioctl_fibmap(struct file *filp, int __user *p)
{
	struct address_space *mapping = filp->f_mapping;
	int res, block;

	/* do we support this mess? */
	if (!mapping->a_ops->bmap)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	res = get_user(block, p);
	if (res)
		return res;
	res = mapping->a_ops->bmap(mapping, block);
	return put_user(res, p);
}

/**
 * fiemap_fill_next_extent - Fiemap helper function
 * @fieinfo:	Fiemap context passed into ->fiemap
 * @logical:	Extent logical start offset, in bytes
 * @phys:	Extent physical start offset, in bytes
 * @len:	Extent length, in bytes
 * @flags:	FIEMAP_EXTENT flags that describe this extent
 *
 * Called from file system ->fiemap callback. Will populate extent
 * info as passed in via arguments and copy to user memory. On
 * success, extent count on fieinfo is incremented.
 *
 * Returns 0 on success, -errno on error, 1 if this was the last
 * extent that will fit in user array.
 */
#define SET_UNKNOWN_FLAGS	(FIEMAP_EXTENT_DELALLOC)
#define SET_NO_UNMOUNTED_IO_FLAGS	(FIEMAP_EXTENT_DATA_ENCRYPTED)
#define SET_NOT_ALIGNED_FLAGS	(FIEMAP_EXTENT_DATA_TAIL|FIEMAP_EXTENT_DATA_INLINE)
int fiemap_fill_next_extent(struct fiemap_extent_info *fieinfo, u64 logical,
			    u64 phys, u64 len, u32 flags)
{
	struct fiemap_extent extent;
	struct fiemap_extent __user *dest = fieinfo->fi_extents_start;

	/* only count the extents */
	if (fieinfo->fi_extents_max == 0) {
		fieinfo->fi_extents_mapped++;
		return (flags & FIEMAP_EXTENT_LAST) ? 1 : 0;
	}

	if (fieinfo->fi_extents_mapped >= fieinfo->fi_extents_max)
		return 1;

	if (flags & SET_UNKNOWN_FLAGS)
		flags |= FIEMAP_EXTENT_UNKNOWN;
	if (flags & SET_NO_UNMOUNTED_IO_FLAGS)
		flags |= FIEMAP_EXTENT_ENCODED;
	if (flags & SET_NOT_ALIGNED_FLAGS)
		flags |= FIEMAP_EXTENT_NOT_ALIGNED;

	memset(&extent, 0, sizeof(extent));
	extent.fe_logical = logical;
	extent.fe_physical = phys;
	extent.fe_length = len;
	extent.fe_flags = flags;

	dest += fieinfo->fi_extents_mapped;
	if (copy_to_user(dest, &extent, sizeof(extent)))
		return -EFAULT;

	fieinfo->fi_extents_mapped++;
	if (fieinfo->fi_extents_mapped == fieinfo->fi_extents_max)
		return 1;
	return (flags & FIEMAP_EXTENT_LAST) ? 1 : 0;
}
EXPORT_SYMBOL(fiemap_fill_next_extent);

/**
 * fiemap_check_flags - check validity of requested flags for fiemap
 * @fieinfo:	Fiemap context passed into ->fiemap
 * @fs_flags:	Set of fiemap flags that the file system understands
 *
 * Called from file system ->fiemap callback. This will compute the
 * intersection of valid fiemap flags and those that the fs supports. That
 * value is then compared against the user supplied flags. In case of bad user
 * flags, the invalid values will be written into the fieinfo structure, and
 * -EBADR is returned, which tells ioctl_fiemap() to return those values to
 * userspace. For this reason, a return code of -EBADR should be preserved.
 *
 * Returns 0 on success, -EBADR on bad flags.
 */
int fiemap_check_flags(struct fiemap_extent_info *fieinfo, u32 fs_flags)
{
	u32 incompat_flags;

	incompat_flags = fieinfo->fi_flags & ~(FIEMAP_FLAGS_COMPAT & fs_flags);
	if (incompat_flags) {
		fieinfo->fi_flags = incompat_flags;
		return -EBADR;
	}
	return 0;
}
EXPORT_SYMBOL(fiemap_check_flags);

static int fiemap_check_ranges(struct super_block *sb,
			       u64 start, u64 len, u64 *new_len)
{
	u64 maxbytes = (u64) sb->s_maxbytes;

	*new_len = len;

	if (len == 0)
		return -EINVAL;

	if (start > maxbytes)
		return -EFBIG;

	/*
	 * Shrink request scope to what the fs can actually handle.
	 */
	if (len > maxbytes || (maxbytes - len) < start)
		*new_len = maxbytes - start;

	return 0;
}

static int ioctl_fiemap(struct file *filp, struct fiemap __user *ufiemap)
{
	struct fiemap fiemap;
	struct fiemap_extent_info fieinfo = { 0, };
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	u64 len;
	int error;

	if (!inode->i_op->fiemap)
		return -EOPNOTSUPP;

	if (copy_from_user(&fiemap, ufiemap, sizeof(fiemap)))
		return -EFAULT;

	if (fiemap.fm_extent_count > FIEMAP_MAX_EXTENTS)
		return -EINVAL;

	error = fiemap_check_ranges(sb, fiemap.fm_start, fiemap.fm_length,
				    &len);
	if (error)
		return error;

	fieinfo.fi_flags = fiemap.fm_flags;
	fieinfo.fi_extents_max = fiemap.fm_extent_count;
	fieinfo.fi_extents_start = ufiemap->fm_extents;

	if (fiemap.fm_extent_count != 0 &&
	    !access_ok(fieinfo.fi_extents_start,
		       fieinfo.fi_extents_max * sizeof(struct fiemap_extent)))
		return -EFAULT;

	if (fieinfo.fi_flags & FIEMAP_FLAG_SYNC)
		filemap_write_and_wait(inode->i_mapping);

	error = inode->i_op->fiemap(inode, &fieinfo, fiemap.fm_start, len);
	fiemap.fm_flags = fieinfo.fi_flags;
	fiemap.fm_mapped_extents = fieinfo.fi_extents_mapped;
	if (copy_to_user(ufiemap, &fiemap, sizeof(fiemap)))
		error = -EFAULT;

	return error;
}

static long ioctl_file_clone(struct file *dst_file, unsigned long srcfd,
			     u64 off, u64 olen, u64 destoff)
{
	struct fd src_file = fdget(srcfd);
	loff_t cloned;
	int ret;

	if (!src_file.file)
		return -EBADF;
	ret = -EXDEV;
	if (src_file.file->f_path.mnt != dst_file->f_path.mnt)
		goto fdput;
	cloned = vfs_clone_file_range(src_file.file, off, dst_file, destoff,
				      olen, 0);
	if (cloned < 0)
		ret = cloned;
	else if (olen && cloned != olen)
		ret = -EINVAL;
	else
		ret = 0;
fdput:
	fdput(src_file);
	return ret;
}

static long ioctl_file_clone_range(struct file *file,
				   struct file_clone_range __user *argp)
{
	struct file_clone_range args;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;
	return ioctl_file_clone(file, args.src_fd, args.src_offset,
				args.src_length, args.dest_offset);
}

#ifdef CONFIG_BLOCK

static inline sector_t logical_to_blk(struct inode *inode, loff_t offset)
{
	return (offset >> inode->i_blkbits);
}

static inline loff_t blk_to_logical(struct inode *inode, sector_t blk)
{
	return (blk << inode->i_blkbits);
}

/**
 * __generic_block_fiemap - FIEMAP for block based inodes (no locking)
 * @inode: the inode to map
 * @fieinfo: the fiemap info struct that will be passed back to userspace
 * @start: where to start mapping in the inode
 * @len: how much space to map
 * @get_block: the fs's get_block function
 *
 * This does FIEMAP for block based inodes.  Basically it will just loop
 * through get_block until we hit the number of extents we want to map, or we
 * go past the end of the file and hit a hole.
 *
 * If it is possible to have data blocks beyond a hole past @inode->i_size, then
 * please do not use this function, it will stop at the first unmapped block
 * beyond i_size.
 *
 * If you use this function directly, you need to do your own locking. Use
 * generic_block_fiemap if you want the locking done for you.
 */

int __generic_block_fiemap(struct inode *inode,
			   struct fiemap_extent_info *fieinfo, loff_t start,
			   loff_t len, get_block_t *get_block)
{
	struct buffer_head map_bh;
	sector_t start_blk, last_blk;
	loff_t isize = i_size_read(inode);
	u64 logical = 0, phys = 0, size = 0;
	u32 flags = FIEMAP_EXTENT_MERGED;
	bool past_eof = false, whole_file = false;
	int ret = 0;

	ret = fiemap_check_flags(fieinfo, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	/*
	 * Either the i_mutex or other appropriate locking needs to be held
	 * since we expect isize to not change at all through the duration of
	 * this call.
	 */
	if (len >= isize) {
		whole_file = true;
		len = isize;
	}

	/*
	 * Some filesystems can't deal with being asked to map less than
	 * blocksize, so make sure our len is at least block length.
	 */
	if (logical_to_blk(inode, len) == 0)
		len = blk_to_logical(inode, 1);

	start_blk = logical_to_blk(inode, start);
	last_blk = logical_to_blk(inode, start + len - 1);

	do {
		/*
		 * we set b_size to the total size we want so it will map as
		 * many contiguous blocks as possible at once
		 */
		memset(&map_bh, 0, sizeof(struct buffer_head));
		map_bh.b_size = len;

		ret = get_block(inode, start_blk, &map_bh, 0);
		if (ret)
			break;

		/* HOLE */
		if (!buffer_mapped(&map_bh)) {
			start_blk++;

			/*
			 * We want to handle the case where there is an
			 * allocated block at the front of the file, and then
			 * nothing but holes up to the end of the file properly,
			 * to make sure that extent at the front gets properly
			 * marked with FIEMAP_EXTENT_LAST
			 */
			if (!past_eof &&
			    blk_to_logical(inode, start_blk) >= isize)
				past_eof = 1;

			/*
			 * First hole after going past the EOF, this is our
			 * last extent
			 */
			if (past_eof && size) {
				flags = FIEMAP_EXTENT_MERGED|FIEMAP_EXTENT_LAST;
				ret = fiemap_fill_next_extent(fieinfo, logical,
							      phys, size,
							      flags);
			} else if (size) {
				ret = fiemap_fill_next_extent(fieinfo, logical,
							      phys, size, flags);
				size = 0;
			}

			/* if we have holes up to/past EOF then we're done */
			if (start_blk > last_blk || past_eof || ret)
				break;
		} else {
			/*
			 * We have gone over the length of what we wanted to
			 * map, and it wasn't the entire file, so add the extent
			 * we got last time and exit.
			 *
			 * This is for the case where say we want to map all the
			 * way up to the second to the last block in a file, but
			 * the last block is a hole, making the second to last
			 * block FIEMAP_EXTENT_LAST.  In this case we want to
			 * see if there is a hole after the second to last block
			 * so we can mark it properly.  If we found data after
			 * we exceeded the length we were requesting, then we
			 * are good to go, just add the extent to the fieinfo
			 * and break
			 */
			if (start_blk > last_blk && !whole_file) {
				ret = fiemap_fill_next_extent(fieinfo, logical,
							      phys, size,
							      flags);
				break;
			}

			/*
			 * if size != 0 then we know we already have an extent
			 * to add, so add it.
			 */
			if (size) {
				ret = fiemap_fill_next_extent(fieinfo, logical,
							      phys, size,
							      flags);
				if (ret)
					break;
			}

			logical = blk_to_logical(inode, start_blk);
			phys = blk_to_logical(inode, map_bh.b_blocknr);
			size = map_bh.b_size;
			flags = FIEMAP_EXTENT_MERGED;

			start_blk += logical_to_blk(inode, size);

			/*
			 * If we are past the EOF, then we need to make sure as
			 * soon as we find a hole that the last extent we found
			 * is marked with FIEMAP_EXTENT_LAST
			 */
			if (!past_eof && logical + size >= isize)
				past_eof = true;
		}
		cond_resched();
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

	} while (1);

	/* If ret is 1 then we just hit the end of the extent array */
	if (ret == 1)
		ret = 0;

	return ret;
}
EXPORT_SYMBOL(__generic_block_fiemap);

/**
 * generic_block_fiemap - FIEMAP for block based inodes
 * @inode: The inode to map
 * @fieinfo: The mapping information
 * @start: The initial block to map
 * @len: The length of the extect to attempt to map
 * @get_block: The block mapping function for the fs
 *
 * Calls __generic_block_fiemap to map the inode, after taking
 * the inode's mutex lock.
 */

int generic_block_fiemap(struct inode *inode,
			 struct fiemap_extent_info *fieinfo, u64 start,
			 u64 len, get_block_t *get_block)
{
	int ret;
	inode_lock(inode);
	ret = __generic_block_fiemap(inode, fieinfo, start, len, get_block);
	inode_unlock(inode);
	return ret;
}
EXPORT_SYMBOL(generic_block_fiemap);

#endif  /*  CONFIG_BLOCK  */

/*
 * This provides compatibility with legacy XFS pre-allocation ioctls
 * which predate the fallocate syscall.
 *
 * Only the l_start, l_len and l_whence fields of the 'struct space_resv'
 * are used here, rest are ignored.
 */
static int ioctl_preallocate(struct file *filp, int mode, void __user *argp)
{
	struct inode *inode = file_inode(filp);
	struct space_resv sr;

	if (copy_from_user(&sr, argp, sizeof(sr)))
		return -EFAULT;

	switch (sr.l_whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		sr.l_start += filp->f_pos;
		break;
	case SEEK_END:
		sr.l_start += i_size_read(inode);
		break;
	default:
		return -EINVAL;
	}

	return vfs_fallocate(filp, mode | FALLOC_FL_KEEP_SIZE, sr.l_start,
			sr.l_len);
}

/* on ia32 l_start is on a 32-bit boundary */
#if defined CONFIG_COMPAT && defined(CONFIG_X86_64)
/* just account for different alignment */
static int compat_ioctl_preallocate(struct file *file, int mode,
				    struct space_resv_32 __user *argp)
{
	struct inode *inode = file_inode(file);
	struct space_resv_32 sr;

	if (copy_from_user(&sr, argp, sizeof(sr)))
		return -EFAULT;

	switch (sr.l_whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		sr.l_start += file->f_pos;
		break;
	case SEEK_END:
		sr.l_start += i_size_read(inode);
		break;
	default:
		return -EINVAL;
	}

	return vfs_fallocate(file, mode | FALLOC_FL_KEEP_SIZE, sr.l_start, sr.l_len);
}
#endif

static int file_ioctl(struct file *filp, unsigned int cmd, int __user *p)
{
	struct inode *inode = file_inode(filp);

	switch (cmd) {
	case FIBMAP:
		return ioctl_fibmap(filp, p);
	case FIONREAD:
		return put_user(i_size_read(inode) - filp->f_pos, p);
	case FS_IOC_RESVSP:
	case FS_IOC_RESVSP64:
		return ioctl_preallocate(filp, 0, p);
	case FS_IOC_UNRESVSP:
	case FS_IOC_UNRESVSP64:
		return ioctl_preallocate(filp, FALLOC_FL_PUNCH_HOLE, p);
	case FS_IOC_ZERO_RANGE:
		return ioctl_preallocate(filp, FALLOC_FL_ZERO_RANGE, p);
	}

	return -ENOIOCTLCMD;
}

static int ioctl_fionbio(struct file *filp, int __user *argp)
{
	unsigned int flag;
	int on, error;

	error = get_user(on, argp);
	if (error)
		return error;
	flag = O_NONBLOCK;
#ifdef __sparc__
	/* SunOS compatibility item. */
	if (O_NONBLOCK != O_NDELAY)
		flag |= O_NDELAY;
#endif
	spin_lock(&filp->f_lock);
	if (on)
		filp->f_flags |= flag;
	else
		filp->f_flags &= ~flag;
	spin_unlock(&filp->f_lock);
	return error;
}

static int ioctl_fioasync(unsigned int fd, struct file *filp,
			  int __user *argp)
{
	unsigned int flag;
	int on, error;

	error = get_user(on, argp);
	if (error)
		return error;
	flag = on ? FASYNC : 0;

	/* Did FASYNC state change ? */
	if ((flag ^ filp->f_flags) & FASYNC) {
		if (filp->f_op->fasync)
			/* fasync() adjusts filp->f_flags */
			error = filp->f_op->fasync(fd, filp, on);
		else
			error = -ENOTTY;
	}
	return error < 0 ? error : 0;
}

static int ioctl_fsfreeze(struct file *filp)
{
	struct super_block *sb = file_inode(filp)->i_sb;

	if (!ns_capable(sb->s_user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/* If filesystem doesn't support freeze feature, return. */
	if (sb->s_op->freeze_fs == NULL && sb->s_op->freeze_super == NULL)
		return -EOPNOTSUPP;

	/* Freeze */
	if (sb->s_op->freeze_super)
		return sb->s_op->freeze_super(sb);
	return freeze_super(sb);
}

static int ioctl_fsthaw(struct file *filp)
{
	struct super_block *sb = file_inode(filp)->i_sb;

	if (!ns_capable(sb->s_user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/* Thaw */
	if (sb->s_op->thaw_super)
		return sb->s_op->thaw_super(sb);
	return thaw_super(sb);
}

static int ioctl_file_dedupe_range(struct file *file,
				   struct file_dedupe_range __user *argp)
{
	struct file_dedupe_range *same = NULL;
	int ret;
	unsigned long size;
	u16 count;

	if (get_user(count, &argp->dest_count)) {
		ret = -EFAULT;
		goto out;
	}

	size = offsetof(struct file_dedupe_range __user, info[count]);
	if (size > PAGE_SIZE) {
		ret = -ENOMEM;
		goto out;
	}

	same = memdup_user(argp, size);
	if (IS_ERR(same)) {
		ret = PTR_ERR(same);
		same = NULL;
		goto out;
	}

	same->dest_count = count;
	ret = vfs_dedupe_file_range(file, same);
	if (ret)
		goto out;

	ret = copy_to_user(argp, same, size);
	if (ret)
		ret = -EFAULT;

out:
	kfree(same);
	return ret;
}

/*
 * do_vfs_ioctl() is not for drivers and not intended to be EXPORT_SYMBOL()'d.
 * It's just a simple helper for sys_ioctl and compat_sys_ioctl.
 *
 * When you add any new common ioctls to the switches above and below,
 * please ensure they have compatible arguments in compat mode.
 */
static int do_vfs_ioctl(struct file *filp, unsigned int fd,
			unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct inode *inode = file_inode(filp);

	switch (cmd) {
	case FIOCLEX:
		set_close_on_exec(fd, 1);
		return 0;

	case FIONCLEX:
		set_close_on_exec(fd, 0);
		return 0;

	case FIONBIO:
		return ioctl_fionbio(filp, argp);

	case FIOASYNC:
		return ioctl_fioasync(fd, filp, argp);

	case FIOQSIZE:
		if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode) ||
		    S_ISLNK(inode->i_mode)) {
			loff_t res = inode_get_bytes(inode);
			return copy_to_user(argp, &res, sizeof(res)) ?
					    -EFAULT : 0;
		}

		return -ENOTTY;

	case FIFREEZE:
		return ioctl_fsfreeze(filp);

	case FITHAW:
		return ioctl_fsthaw(filp);

	case FS_IOC_FIEMAP:
		return ioctl_fiemap(filp, argp);

	case FIGETBSZ:
		/* anon_bdev filesystems may not have a block size */
		if (!inode->i_sb->s_blocksize)
			return -EINVAL;

		return put_user(inode->i_sb->s_blocksize, (int __user *)argp);

	case FICLONE:
		return ioctl_file_clone(filp, arg, 0, 0, 0);

	case FICLONERANGE:
		return ioctl_file_clone_range(filp, argp);

	case FIDEDUPERANGE:
		return ioctl_file_dedupe_range(filp, argp);

	default:
		if (S_ISREG(inode->i_mode))
			return file_ioctl(filp, cmd, argp);
		break;
	}

	return -ENOIOCTLCMD;
}

int ksys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fd f = fdget(fd);
	int error;

	if (!f.file)
		return -EBADF;

	error = security_file_ioctl(f.file, cmd, arg);
	if (error)
		goto out;

	error = do_vfs_ioctl(f.file, fd, cmd, arg);
	if (error == -ENOIOCTLCMD)
		error = vfs_ioctl(f.file, cmd, arg);

out:
	fdput(f);
	return error;
}

SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
{
	return ksys_ioctl(fd, cmd, arg);
}

#ifdef CONFIG_COMPAT
/**
 * compat_ptr_ioctl - generic implementation of .compat_ioctl file operation
 *
 * This is not normally called as a function, but instead set in struct
 * file_operations as
 *
 *     .compat_ioctl = compat_ptr_ioctl,
 *
 * On most architectures, the compat_ptr_ioctl() just passes all arguments
 * to the corresponding ->ioctl handler. The exception is arch/s390, where
 * compat_ptr() clears the top bit of a 32-bit pointer value, so user space
 * pointers to the second 2GB alias the first 2GB, as is the case for
 * native 32-bit s390 user space.
 *
 * The compat_ptr_ioctl() function must therefore be used only with ioctl
 * functions that either ignore the argument or pass a pointer to a
 * compatible data type.
 *
 * If any ioctl command handled by fops->unlocked_ioctl passes a plain
 * integer instead of a pointer, or any of the passed data types
 * is incompatible between 32-bit and 64-bit architectures, a proper
 * handler is required instead of compat_ptr_ioctl.
 */
long compat_ptr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (!file->f_op->unlocked_ioctl)
		return -ENOIOCTLCMD;

	return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
EXPORT_SYMBOL(compat_ptr_ioctl);

COMPAT_SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd,
		       compat_ulong_t, arg)
{
	struct fd f = fdget(fd);
	int error;

	if (!f.file)
		return -EBADF;

	/* RED-PEN how should LSM module know it's handling 32bit? */
	error = security_file_ioctl(f.file, cmd, arg);
	if (error)
		goto out;

	switch (cmd) {
	/* FICLONE takes an int argument, so don't use compat_ptr() */
	case FICLONE:
		error = ioctl_file_clone(f.file, arg, 0, 0, 0);
		break;

#if defined(CONFIG_X86_64)
	/* these get messy on amd64 due to alignment differences */
	case FS_IOC_RESVSP_32:
	case FS_IOC_RESVSP64_32:
		error = compat_ioctl_preallocate(f.file, 0, compat_ptr(arg));
		break;
	case FS_IOC_UNRESVSP_32:
	case FS_IOC_UNRESVSP64_32:
		error = compat_ioctl_preallocate(f.file, FALLOC_FL_PUNCH_HOLE,
				compat_ptr(arg));
		break;
	case FS_IOC_ZERO_RANGE_32:
		error = compat_ioctl_preallocate(f.file, FALLOC_FL_ZERO_RANGE,
				compat_ptr(arg));
		break;
#endif

	/*
	 * everything else in do_vfs_ioctl() takes either a compatible
	 * pointer argument or no argument -- call it with a modified
	 * argument.
	 */
	default:
		error = do_vfs_ioctl(f.file, fd, cmd,
				     (unsigned long)compat_ptr(arg));
		if (error != -ENOIOCTLCMD)
			break;

		if (f.file->f_op->compat_ioctl)
			error = f.file->f_op->compat_ioctl(f.file, cmd, arg);
		if (error == -ENOIOCTLCMD)
			error = -ENOTTY;
		break;
	}

 out:
	fdput(f);

	return error;
}
#endif
