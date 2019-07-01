// SPDX-License-Identifier: GPL-2.0+
/*
 * ioctl.c - NILFS ioctl operations.
 *
 * Copyright (C) 2007, 2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/capability.h>	/* capable() */
#include <linux/uaccess.h>	/* copy_from_user(), copy_to_user() */
#include <linux/vmalloc.h>
#include <linux/compat.h>	/* compat_ptr() */
#include <linux/mount.h>	/* mnt_want_write_file(), mnt_drop_write_file() */
#include <linux/buffer_head.h>
#include "nilfs.h"
#include "segment.h"
#include "bmap.h"
#include "cpfile.h"
#include "sufile.h"
#include "dat.h"

/**
 * nilfs_ioctl_wrap_copy - wrapping function of get/set metadata info
 * @nilfs: nilfs object
 * @argv: vector of arguments from userspace
 * @dir: set of direction flags
 * @dofunc: concrete function of get/set metadata info
 *
 * Description: nilfs_ioctl_wrap_copy() gets/sets metadata info by means of
 * calling dofunc() function on the basis of @argv argument.
 *
 * Return Value: On success, 0 is returned and requested metadata info
 * is copied into userspace. On error, one of the following
 * negative error codes is returned.
 *
 * %-EINVAL - Invalid arguments from userspace.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EFAULT - Failure during execution of requested operation.
 */
static int nilfs_ioctl_wrap_copy(struct the_nilfs *nilfs,
				 struct nilfs_argv *argv, int dir,
				 ssize_t (*dofunc)(struct the_nilfs *,
						   __u64 *, int,
						   void *, size_t, size_t))
{
	void *buf;
	void __user *base = (void __user *)(unsigned long)argv->v_base;
	size_t maxmembs, total, n;
	ssize_t nr;
	int ret, i;
	__u64 pos, ppos;

	if (argv->v_nmembs == 0)
		return 0;

	if (argv->v_size > PAGE_SIZE)
		return -EINVAL;

	/*
	 * Reject pairs of a start item position (argv->v_index) and a
	 * total count (argv->v_nmembs) which leads position 'pos' to
	 * overflow by the increment at the end of the loop.
	 */
	if (argv->v_index > ~(__u64)0 - argv->v_nmembs)
		return -EINVAL;

	buf = (void *)__get_free_pages(GFP_NOFS, 0);
	if (unlikely(!buf))
		return -ENOMEM;
	maxmembs = PAGE_SIZE / argv->v_size;

	ret = 0;
	total = 0;
	pos = argv->v_index;
	for (i = 0; i < argv->v_nmembs; i += n) {
		n = (argv->v_nmembs - i < maxmembs) ?
			argv->v_nmembs - i : maxmembs;
		if ((dir & _IOC_WRITE) &&
		    copy_from_user(buf, base + argv->v_size * i,
				   argv->v_size * n)) {
			ret = -EFAULT;
			break;
		}
		ppos = pos;
		nr = dofunc(nilfs, &pos, argv->v_flags, buf, argv->v_size,
			       n);
		if (nr < 0) {
			ret = nr;
			break;
		}
		if ((dir & _IOC_READ) &&
		    copy_to_user(base + argv->v_size * i, buf,
				 argv->v_size * nr)) {
			ret = -EFAULT;
			break;
		}
		total += nr;
		if ((size_t)nr < n)
			break;
		if (pos == ppos)
			pos += n;
	}
	argv->v_nmembs = total;

	free_pages((unsigned long)buf, 0);
	return ret;
}

/**
 * nilfs_ioctl_getflags - ioctl to support lsattr
 */
static int nilfs_ioctl_getflags(struct inode *inode, void __user *argp)
{
	unsigned int flags = NILFS_I(inode)->i_flags & FS_FL_USER_VISIBLE;

	return put_user(flags, (int __user *)argp);
}

/**
 * nilfs_ioctl_setflags - ioctl to support chattr
 */
static int nilfs_ioctl_setflags(struct inode *inode, struct file *filp,
				void __user *argp)
{
	struct nilfs_transaction_info ti;
	unsigned int flags, oldflags;
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	if (get_user(flags, (int __user *)argp))
		return -EFAULT;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	flags = nilfs_mask_flags(inode->i_mode, flags);

	inode_lock(inode);

	oldflags = NILFS_I(inode)->i_flags;

	ret = vfs_ioc_setflags_prepare(inode, oldflags, flags);
	if (ret)
		goto out;

	ret = nilfs_transaction_begin(inode->i_sb, &ti, 0);
	if (ret)
		goto out;

	NILFS_I(inode)->i_flags = (oldflags & ~FS_FL_USER_MODIFIABLE) |
		(flags & FS_FL_USER_MODIFIABLE);

	nilfs_set_inode_flags(inode);
	inode->i_ctime = current_time(inode);
	if (IS_SYNC(inode))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);

	nilfs_mark_inode_dirty(inode);
	ret = nilfs_transaction_commit(inode->i_sb);
out:
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return ret;
}

/**
 * nilfs_ioctl_getversion - get info about a file's version (generation number)
 */
static int nilfs_ioctl_getversion(struct inode *inode, void __user *argp)
{
	return put_user(inode->i_generation, (int __user *)argp);
}

/**
 * nilfs_ioctl_change_cpmode - change checkpoint mode (checkpoint/snapshot)
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_change_cpmode() function changes mode of
 * given checkpoint between checkpoint and snapshot state. This ioctl
 * is used in chcp and mkcp utilities.
 *
 * Return Value: On success, 0 is returned and mode of a checkpoint is
 * changed. On error, one of the following negative error codes
 * is returned.
 *
 * %-EPERM - Operation not permitted.
 *
 * %-EFAULT - Failure during checkpoint mode changing.
 */
static int nilfs_ioctl_change_cpmode(struct inode *inode, struct file *filp,
				     unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_transaction_info ti;
	struct nilfs_cpmode cpmode;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = -EFAULT;
	if (copy_from_user(&cpmode, argp, sizeof(cpmode)))
		goto out;

	mutex_lock(&nilfs->ns_snapshot_mount_mutex);

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_cpfile_change_cpmode(
		nilfs->ns_cpfile, cpmode.cm_cno, cpmode.cm_mode);
	if (unlikely(ret < 0))
		nilfs_transaction_abort(inode->i_sb);
	else
		nilfs_transaction_commit(inode->i_sb); /* never fails */

	mutex_unlock(&nilfs->ns_snapshot_mount_mutex);
out:
	mnt_drop_write_file(filp);
	return ret;
}

/**
 * nilfs_ioctl_delete_checkpoint - remove checkpoint
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_delete_checkpoint() function removes
 * checkpoint from NILFS2 file system. This ioctl is used in rmcp
 * utility.
 *
 * Return Value: On success, 0 is returned and a checkpoint is
 * removed. On error, one of the following negative error codes
 * is returned.
 *
 * %-EPERM - Operation not permitted.
 *
 * %-EFAULT - Failure during checkpoint removing.
 */
static int
nilfs_ioctl_delete_checkpoint(struct inode *inode, struct file *filp,
			      unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_transaction_info ti;
	__u64 cno;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = -EFAULT;
	if (copy_from_user(&cno, argp, sizeof(cno)))
		goto out;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_cpfile_delete_checkpoint(nilfs->ns_cpfile, cno);
	if (unlikely(ret < 0))
		nilfs_transaction_abort(inode->i_sb);
	else
		nilfs_transaction_commit(inode->i_sb); /* never fails */
out:
	mnt_drop_write_file(filp);
	return ret;
}

/**
 * nilfs_ioctl_do_get_cpinfo - callback method getting info about checkpoints
 * @nilfs: nilfs object
 * @posp: pointer on array of checkpoint's numbers
 * @flags: checkpoint mode (checkpoint or snapshot)
 * @buf: buffer for storing checkponts' info
 * @size: size in bytes of one checkpoint info item in array
 * @nmembs: number of checkpoints in array (numbers and infos)
 *
 * Description: nilfs_ioctl_do_get_cpinfo() function returns info about
 * requested checkpoints. The NILFS_IOCTL_GET_CPINFO ioctl is used in
 * lscp utility and by nilfs_cleanerd daemon.
 *
 * Return value: count of nilfs_cpinfo structures in output buffer.
 */
static ssize_t
nilfs_ioctl_do_get_cpinfo(struct the_nilfs *nilfs, __u64 *posp, int flags,
			  void *buf, size_t size, size_t nmembs)
{
	int ret;

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_cpfile_get_cpinfo(nilfs->ns_cpfile, posp, flags, buf,
				      size, nmembs);
	up_read(&nilfs->ns_segctor_sem);
	return ret;
}

/**
 * nilfs_ioctl_get_cpstat - get checkpoints statistics
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_get_cpstat() returns information about checkpoints.
 * The NILFS_IOCTL_GET_CPSTAT ioctl is used by lscp, rmcp utilities
 * and by nilfs_cleanerd daemon.
 *
 * Return Value: On success, 0 is returned, and checkpoints information is
 * copied into userspace pointer @argp. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EFAULT - Failure during getting checkpoints statistics.
 */
static int nilfs_ioctl_get_cpstat(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_cpstat cpstat;
	int ret;

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_cpfile_get_stat(nilfs->ns_cpfile, &cpstat);
	up_read(&nilfs->ns_segctor_sem);
	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &cpstat, sizeof(cpstat)))
		ret = -EFAULT;
	return ret;
}

/**
 * nilfs_ioctl_do_get_suinfo - callback method getting segment usage info
 * @nilfs: nilfs object
 * @posp: pointer on array of segment numbers
 * @flags: *not used*
 * @buf: buffer for storing suinfo array
 * @size: size in bytes of one suinfo item in array
 * @nmembs: count of segment numbers and suinfos in array
 *
 * Description: nilfs_ioctl_do_get_suinfo() function returns segment usage
 * info about requested segments. The NILFS_IOCTL_GET_SUINFO ioctl is used
 * in lssu, nilfs_resize utilities and by nilfs_cleanerd daemon.
 *
 * Return value: count of nilfs_suinfo structures in output buffer.
 */
static ssize_t
nilfs_ioctl_do_get_suinfo(struct the_nilfs *nilfs, __u64 *posp, int flags,
			  void *buf, size_t size, size_t nmembs)
{
	int ret;

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_sufile_get_suinfo(nilfs->ns_sufile, *posp, buf, size,
				      nmembs);
	up_read(&nilfs->ns_segctor_sem);
	return ret;
}

/**
 * nilfs_ioctl_get_sustat - get segment usage statistics
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_get_sustat() returns segment usage statistics.
 * The NILFS_IOCTL_GET_SUSTAT ioctl is used in lssu, nilfs_resize utilities
 * and by nilfs_cleanerd daemon.
 *
 * Return Value: On success, 0 is returned, and segment usage information is
 * copied into userspace pointer @argp. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EFAULT - Failure during getting segment usage statistics.
 */
static int nilfs_ioctl_get_sustat(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_sustat sustat;
	int ret;

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_sufile_get_stat(nilfs->ns_sufile, &sustat);
	up_read(&nilfs->ns_segctor_sem);
	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &sustat, sizeof(sustat)))
		ret = -EFAULT;
	return ret;
}

/**
 * nilfs_ioctl_do_get_vinfo - callback method getting virtual blocks info
 * @nilfs: nilfs object
 * @posp: *not used*
 * @flags: *not used*
 * @buf: buffer for storing array of nilfs_vinfo structures
 * @size: size in bytes of one vinfo item in array
 * @nmembs: count of vinfos in array
 *
 * Description: nilfs_ioctl_do_get_vinfo() function returns information
 * on virtual block addresses. The NILFS_IOCTL_GET_VINFO ioctl is used
 * by nilfs_cleanerd daemon.
 *
 * Return value: count of nilfs_vinfo structures in output buffer.
 */
static ssize_t
nilfs_ioctl_do_get_vinfo(struct the_nilfs *nilfs, __u64 *posp, int flags,
			 void *buf, size_t size, size_t nmembs)
{
	int ret;

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_dat_get_vinfo(nilfs->ns_dat, buf, size, nmembs);
	up_read(&nilfs->ns_segctor_sem);
	return ret;
}

/**
 * nilfs_ioctl_do_get_bdescs - callback method getting disk block descriptors
 * @nilfs: nilfs object
 * @posp: *not used*
 * @flags: *not used*
 * @buf: buffer for storing array of nilfs_bdesc structures
 * @size: size in bytes of one bdesc item in array
 * @nmembs: count of bdescs in array
 *
 * Description: nilfs_ioctl_do_get_bdescs() function returns information
 * about descriptors of disk block numbers. The NILFS_IOCTL_GET_BDESCS ioctl
 * is used by nilfs_cleanerd daemon.
 *
 * Return value: count of nilfs_bdescs structures in output buffer.
 */
static ssize_t
nilfs_ioctl_do_get_bdescs(struct the_nilfs *nilfs, __u64 *posp, int flags,
			  void *buf, size_t size, size_t nmembs)
{
	struct nilfs_bmap *bmap = NILFS_I(nilfs->ns_dat)->i_bmap;
	struct nilfs_bdesc *bdescs = buf;
	int ret, i;

	down_read(&nilfs->ns_segctor_sem);
	for (i = 0; i < nmembs; i++) {
		ret = nilfs_bmap_lookup_at_level(bmap,
						 bdescs[i].bd_offset,
						 bdescs[i].bd_level + 1,
						 &bdescs[i].bd_blocknr);
		if (ret < 0) {
			if (ret != -ENOENT) {
				up_read(&nilfs->ns_segctor_sem);
				return ret;
			}
			bdescs[i].bd_blocknr = 0;
		}
	}
	up_read(&nilfs->ns_segctor_sem);
	return nmembs;
}

/**
 * nilfs_ioctl_get_bdescs - get disk block descriptors
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_do_get_bdescs() function returns information
 * about descriptors of disk block numbers. The NILFS_IOCTL_GET_BDESCS ioctl
 * is used by nilfs_cleanerd daemon.
 *
 * Return Value: On success, 0 is returned, and disk block descriptors are
 * copied into userspace pointer @argp. On error, one of the following
 * negative error codes is returned.
 *
 * %-EINVAL - Invalid arguments from userspace.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EFAULT - Failure during getting disk block descriptors.
 */
static int nilfs_ioctl_get_bdescs(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_argv argv;
	int ret;

	if (copy_from_user(&argv, argp, sizeof(argv)))
		return -EFAULT;

	if (argv.v_size != sizeof(struct nilfs_bdesc))
		return -EINVAL;

	ret = nilfs_ioctl_wrap_copy(nilfs, &argv, _IOC_DIR(cmd),
				    nilfs_ioctl_do_get_bdescs);
	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &argv, sizeof(argv)))
		ret = -EFAULT;
	return ret;
}

/**
 * nilfs_ioctl_move_inode_block - prepare data/node block for moving by GC
 * @inode: inode object
 * @vdesc: descriptor of virtual block number
 * @buffers: list of moving buffers
 *
 * Description: nilfs_ioctl_move_inode_block() function registers data/node
 * buffer in the GC pagecache and submit read request.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - Requested block doesn't exist.
 *
 * %-EEXIST - Blocks conflict is detected.
 */
static int nilfs_ioctl_move_inode_block(struct inode *inode,
					struct nilfs_vdesc *vdesc,
					struct list_head *buffers)
{
	struct buffer_head *bh;
	int ret;

	if (vdesc->vd_flags == 0)
		ret = nilfs_gccache_submit_read_data(
			inode, vdesc->vd_offset, vdesc->vd_blocknr,
			vdesc->vd_vblocknr, &bh);
	else
		ret = nilfs_gccache_submit_read_node(
			inode, vdesc->vd_blocknr, vdesc->vd_vblocknr, &bh);

	if (unlikely(ret < 0)) {
		if (ret == -ENOENT)
			nilfs_msg(inode->i_sb, KERN_CRIT,
				  "%s: invalid virtual block address (%s): ino=%llu, cno=%llu, offset=%llu, blocknr=%llu, vblocknr=%llu",
				  __func__, vdesc->vd_flags ? "node" : "data",
				  (unsigned long long)vdesc->vd_ino,
				  (unsigned long long)vdesc->vd_cno,
				  (unsigned long long)vdesc->vd_offset,
				  (unsigned long long)vdesc->vd_blocknr,
				  (unsigned long long)vdesc->vd_vblocknr);
		return ret;
	}
	if (unlikely(!list_empty(&bh->b_assoc_buffers))) {
		nilfs_msg(inode->i_sb, KERN_CRIT,
			  "%s: conflicting %s buffer: ino=%llu, cno=%llu, offset=%llu, blocknr=%llu, vblocknr=%llu",
			  __func__, vdesc->vd_flags ? "node" : "data",
			  (unsigned long long)vdesc->vd_ino,
			  (unsigned long long)vdesc->vd_cno,
			  (unsigned long long)vdesc->vd_offset,
			  (unsigned long long)vdesc->vd_blocknr,
			  (unsigned long long)vdesc->vd_vblocknr);
		brelse(bh);
		return -EEXIST;
	}
	list_add_tail(&bh->b_assoc_buffers, buffers);
	return 0;
}

/**
 * nilfs_ioctl_move_blocks - move valid inode's blocks during garbage collection
 * @sb: superblock object
 * @argv: vector of arguments from userspace
 * @buf: array of nilfs_vdesc structures
 *
 * Description: nilfs_ioctl_move_blocks() function reads valid data/node
 * blocks that garbage collector specified with the array of nilfs_vdesc
 * structures and stores them into page caches of GC inodes.
 *
 * Return Value: Number of processed nilfs_vdesc structures or
 * error code, otherwise.
 */
static int nilfs_ioctl_move_blocks(struct super_block *sb,
				   struct nilfs_argv *argv, void *buf)
{
	size_t nmembs = argv->v_nmembs;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct inode *inode;
	struct nilfs_vdesc *vdesc;
	struct buffer_head *bh, *n;
	LIST_HEAD(buffers);
	ino_t ino;
	__u64 cno;
	int i, ret;

	for (i = 0, vdesc = buf; i < nmembs; ) {
		ino = vdesc->vd_ino;
		cno = vdesc->vd_cno;
		inode = nilfs_iget_for_gc(sb, ino, cno);
		if (IS_ERR(inode)) {
			ret = PTR_ERR(inode);
			goto failed;
		}
		if (list_empty(&NILFS_I(inode)->i_dirty)) {
			/*
			 * Add the inode to GC inode list. Garbage Collection
			 * is serialized and no two processes manipulate the
			 * list simultaneously.
			 */
			igrab(inode);
			list_add(&NILFS_I(inode)->i_dirty,
				 &nilfs->ns_gc_inodes);
		}

		do {
			ret = nilfs_ioctl_move_inode_block(inode, vdesc,
							   &buffers);
			if (unlikely(ret < 0)) {
				iput(inode);
				goto failed;
			}
			vdesc++;
		} while (++i < nmembs &&
			 vdesc->vd_ino == ino && vdesc->vd_cno == cno);

		iput(inode); /* The inode still remains in GC inode list */
	}

	list_for_each_entry_safe(bh, n, &buffers, b_assoc_buffers) {
		ret = nilfs_gccache_wait_and_mark_dirty(bh);
		if (unlikely(ret < 0)) {
			WARN_ON(ret == -EEXIST);
			goto failed;
		}
		list_del_init(&bh->b_assoc_buffers);
		brelse(bh);
	}
	return nmembs;

 failed:
	list_for_each_entry_safe(bh, n, &buffers, b_assoc_buffers) {
		list_del_init(&bh->b_assoc_buffers);
		brelse(bh);
	}
	return ret;
}

/**
 * nilfs_ioctl_delete_checkpoints - delete checkpoints
 * @nilfs: nilfs object
 * @argv: vector of arguments from userspace
 * @buf: array of periods of checkpoints numbers
 *
 * Description: nilfs_ioctl_delete_checkpoints() function deletes checkpoints
 * in the period from p_start to p_end, excluding p_end itself. The checkpoints
 * which have been already deleted are ignored.
 *
 * Return Value: Number of processed nilfs_period structures or
 * error code, otherwise.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - invalid checkpoints.
 */
static int nilfs_ioctl_delete_checkpoints(struct the_nilfs *nilfs,
					  struct nilfs_argv *argv, void *buf)
{
	size_t nmembs = argv->v_nmembs;
	struct inode *cpfile = nilfs->ns_cpfile;
	struct nilfs_period *periods = buf;
	int ret, i;

	for (i = 0; i < nmembs; i++) {
		ret = nilfs_cpfile_delete_checkpoints(
			cpfile, periods[i].p_start, periods[i].p_end);
		if (ret < 0)
			return ret;
	}
	return nmembs;
}

/**
 * nilfs_ioctl_free_vblocknrs - free virtual block numbers
 * @nilfs: nilfs object
 * @argv: vector of arguments from userspace
 * @buf: array of virtual block numbers
 *
 * Description: nilfs_ioctl_free_vblocknrs() function frees
 * the virtual block numbers specified by @buf and @argv->v_nmembs.
 *
 * Return Value: Number of processed virtual block numbers or
 * error code, otherwise.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - The virtual block number have not been allocated.
 */
static int nilfs_ioctl_free_vblocknrs(struct the_nilfs *nilfs,
				      struct nilfs_argv *argv, void *buf)
{
	size_t nmembs = argv->v_nmembs;
	int ret;

	ret = nilfs_dat_freev(nilfs->ns_dat, buf, nmembs);

	return (ret < 0) ? ret : nmembs;
}

/**
 * nilfs_ioctl_mark_blocks_dirty - mark blocks dirty
 * @nilfs: nilfs object
 * @argv: vector of arguments from userspace
 * @buf: array of block descriptors
 *
 * Description: nilfs_ioctl_mark_blocks_dirty() function marks
 * metadata file or data blocks as dirty.
 *
 * Return Value: Number of processed block descriptors or
 * error code, otherwise.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-ENOENT - the specified block does not exist (hole block)
 */
static int nilfs_ioctl_mark_blocks_dirty(struct the_nilfs *nilfs,
					 struct nilfs_argv *argv, void *buf)
{
	size_t nmembs = argv->v_nmembs;
	struct nilfs_bmap *bmap = NILFS_I(nilfs->ns_dat)->i_bmap;
	struct nilfs_bdesc *bdescs = buf;
	struct buffer_head *bh;
	int ret, i;

	for (i = 0; i < nmembs; i++) {
		/* XXX: use macro or inline func to check liveness */
		ret = nilfs_bmap_lookup_at_level(bmap,
						 bdescs[i].bd_offset,
						 bdescs[i].bd_level + 1,
						 &bdescs[i].bd_blocknr);
		if (ret < 0) {
			if (ret != -ENOENT)
				return ret;
			bdescs[i].bd_blocknr = 0;
		}
		if (bdescs[i].bd_blocknr != bdescs[i].bd_oblocknr)
			/* skip dead block */
			continue;
		if (bdescs[i].bd_level == 0) {
			ret = nilfs_mdt_get_block(nilfs->ns_dat,
						  bdescs[i].bd_offset,
						  false, NULL, &bh);
			if (unlikely(ret)) {
				WARN_ON(ret == -ENOENT);
				return ret;
			}
			mark_buffer_dirty(bh);
			nilfs_mdt_mark_dirty(nilfs->ns_dat);
			put_bh(bh);
		} else {
			ret = nilfs_bmap_mark(bmap, bdescs[i].bd_offset,
					      bdescs[i].bd_level);
			if (ret < 0) {
				WARN_ON(ret == -ENOENT);
				return ret;
			}
		}
	}
	return nmembs;
}

int nilfs_ioctl_prepare_clean_segments(struct the_nilfs *nilfs,
				       struct nilfs_argv *argv, void **kbufs)
{
	const char *msg;
	int ret;

	ret = nilfs_ioctl_delete_checkpoints(nilfs, &argv[1], kbufs[1]);
	if (ret < 0) {
		/*
		 * can safely abort because checkpoints can be removed
		 * independently.
		 */
		msg = "cannot delete checkpoints";
		goto failed;
	}
	ret = nilfs_ioctl_free_vblocknrs(nilfs, &argv[2], kbufs[2]);
	if (ret < 0) {
		/*
		 * can safely abort because DAT file is updated atomically
		 * using a copy-on-write technique.
		 */
		msg = "cannot delete virtual blocks from DAT file";
		goto failed;
	}
	ret = nilfs_ioctl_mark_blocks_dirty(nilfs, &argv[3], kbufs[3]);
	if (ret < 0) {
		/*
		 * can safely abort because the operation is nondestructive.
		 */
		msg = "cannot mark copying blocks dirty";
		goto failed;
	}
	return 0;

 failed:
	nilfs_msg(nilfs->ns_sb, KERN_ERR, "error %d preparing GC: %s", ret,
		  msg);
	return ret;
}

/**
 * nilfs_ioctl_clean_segments - clean segments
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_clean_segments() function makes garbage
 * collection operation in the environment of requested parameters
 * from userspace. The NILFS_IOCTL_CLEAN_SEGMENTS ioctl is used by
 * nilfs_cleanerd daemon.
 *
 * Return Value: On success, 0 is returned or error code, otherwise.
 */
static int nilfs_ioctl_clean_segments(struct inode *inode, struct file *filp,
				      unsigned int cmd, void __user *argp)
{
	struct nilfs_argv argv[5];
	static const size_t argsz[5] = {
		sizeof(struct nilfs_vdesc),
		sizeof(struct nilfs_period),
		sizeof(__u64),
		sizeof(struct nilfs_bdesc),
		sizeof(__u64),
	};
	void __user *base;
	void *kbufs[5];
	struct the_nilfs *nilfs;
	size_t len, nsegs;
	int n, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = -EFAULT;
	if (copy_from_user(argv, argp, sizeof(argv)))
		goto out;

	ret = -EINVAL;
	nsegs = argv[4].v_nmembs;
	if (argv[4].v_size != argsz[4])
		goto out;
	if (nsegs > UINT_MAX / sizeof(__u64))
		goto out;

	/*
	 * argv[4] points to segment numbers this ioctl cleans.  We
	 * use kmalloc() for its buffer because memory used for the
	 * segment numbers is enough small.
	 */
	kbufs[4] = memdup_user((void __user *)(unsigned long)argv[4].v_base,
			       nsegs * sizeof(__u64));
	if (IS_ERR(kbufs[4])) {
		ret = PTR_ERR(kbufs[4]);
		goto out;
	}
	nilfs = inode->i_sb->s_fs_info;

	for (n = 0; n < 4; n++) {
		ret = -EINVAL;
		if (argv[n].v_size != argsz[n])
			goto out_free;

		if (argv[n].v_nmembs > nsegs * nilfs->ns_blocks_per_segment)
			goto out_free;

		if (argv[n].v_nmembs >= UINT_MAX / argv[n].v_size)
			goto out_free;

		len = argv[n].v_size * argv[n].v_nmembs;
		base = (void __user *)(unsigned long)argv[n].v_base;
		if (len == 0) {
			kbufs[n] = NULL;
			continue;
		}

		kbufs[n] = vmalloc(len);
		if (!kbufs[n]) {
			ret = -ENOMEM;
			goto out_free;
		}
		if (copy_from_user(kbufs[n], base, len)) {
			ret = -EFAULT;
			vfree(kbufs[n]);
			goto out_free;
		}
	}

	/*
	 * nilfs_ioctl_move_blocks() will call nilfs_iget_for_gc(),
	 * which will operates an inode list without blocking.
	 * To protect the list from concurrent operations,
	 * nilfs_ioctl_move_blocks should be atomic operation.
	 */
	if (test_and_set_bit(THE_NILFS_GC_RUNNING, &nilfs->ns_flags)) {
		ret = -EBUSY;
		goto out_free;
	}

	ret = nilfs_ioctl_move_blocks(inode->i_sb, &argv[0], kbufs[0]);
	if (ret < 0) {
		nilfs_msg(inode->i_sb, KERN_ERR,
			  "error %d preparing GC: cannot read source blocks",
			  ret);
	} else {
		if (nilfs_sb_need_update(nilfs))
			set_nilfs_discontinued(nilfs);
		ret = nilfs_clean_segments(inode->i_sb, argv, kbufs);
	}

	nilfs_remove_all_gcinodes(nilfs);
	clear_nilfs_gc_running(nilfs);

out_free:
	while (--n >= 0)
		vfree(kbufs[n]);
	kfree(kbufs[4]);
out:
	mnt_drop_write_file(filp);
	return ret;
}

/**
 * nilfs_ioctl_sync - make a checkpoint
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: nilfs_ioctl_sync() function constructs a logical segment
 * for checkpointing.  This function guarantees that all modified data
 * and metadata are written out to the device when it successfully
 * returned.
 *
 * Return Value: On success, 0 is retured. On errors, one of the following
 * negative error code is returned.
 *
 * %-EROFS - Read only filesystem.
 *
 * %-EIO - I/O error
 *
 * %-ENOSPC - No space left on device (only in a panic state).
 *
 * %-ERESTARTSYS - Interrupted.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EFAULT - Failure during execution of requested operation.
 */
static int nilfs_ioctl_sync(struct inode *inode, struct file *filp,
			    unsigned int cmd, void __user *argp)
{
	__u64 cno;
	int ret;
	struct the_nilfs *nilfs;

	ret = nilfs_construct_segment(inode->i_sb);
	if (ret < 0)
		return ret;

	nilfs = inode->i_sb->s_fs_info;
	ret = nilfs_flush_device(nilfs);
	if (ret < 0)
		return ret;

	if (argp != NULL) {
		down_read(&nilfs->ns_segctor_sem);
		cno = nilfs->ns_cno - 1;
		up_read(&nilfs->ns_segctor_sem);
		if (copy_to_user(argp, &cno, sizeof(cno)))
			return -EFAULT;
	}
	return 0;
}

/**
 * nilfs_ioctl_resize - resize NILFS2 volume
 * @inode: inode object
 * @filp: file object
 * @argp: pointer on argument from userspace
 *
 * Return Value: On success, 0 is returned or error code, otherwise.
 */
static int nilfs_ioctl_resize(struct inode *inode, struct file *filp,
			      void __user *argp)
{
	__u64 newsize;
	int ret = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		goto out;

	ret = mnt_want_write_file(filp);
	if (ret)
		goto out;

	ret = -EFAULT;
	if (copy_from_user(&newsize, argp, sizeof(newsize)))
		goto out_drop_write;

	ret = nilfs_resize_fs(inode->i_sb, newsize);

out_drop_write:
	mnt_drop_write_file(filp);
out:
	return ret;
}

/**
 * nilfs_ioctl_trim_fs() - trim ioctl handle function
 * @inode: inode object
 * @argp: pointer on argument from userspace
 *
 * Decription: nilfs_ioctl_trim_fs is the FITRIM ioctl handle function. It
 * checks the arguments from userspace and calls nilfs_sufile_trim_fs, which
 * performs the actual trim operation.
 *
 * Return Value: On success, 0 is returned or negative error code, otherwise.
 */
static int nilfs_ioctl_trim_fs(struct inode *inode, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct request_queue *q = bdev_get_queue(nilfs->ns_bdev);
	struct fstrim_range range;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	if (copy_from_user(&range, argp, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(u64, range.minlen, q->limits.discard_granularity);

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_sufile_trim_fs(nilfs->ns_sufile, &range);
	up_read(&nilfs->ns_segctor_sem);

	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

/**
 * nilfs_ioctl_set_alloc_range - limit range of segments to be allocated
 * @inode: inode object
 * @argp: pointer on argument from userspace
 *
 * Decription: nilfs_ioctl_set_alloc_range() function defines lower limit
 * of segments in bytes and upper limit of segments in bytes.
 * The NILFS_IOCTL_SET_ALLOC_RANGE is used by nilfs_resize utility.
 *
 * Return Value: On success, 0 is returned or error code, otherwise.
 */
static int nilfs_ioctl_set_alloc_range(struct inode *inode, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	__u64 range[2];
	__u64 minseg, maxseg;
	unsigned long segbytes;
	int ret = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		goto out;

	ret = -EFAULT;
	if (copy_from_user(range, argp, sizeof(__u64[2])))
		goto out;

	ret = -ERANGE;
	if (range[1] > i_size_read(inode->i_sb->s_bdev->bd_inode))
		goto out;

	segbytes = nilfs->ns_blocks_per_segment * nilfs->ns_blocksize;

	minseg = range[0] + segbytes - 1;
	do_div(minseg, segbytes);
	maxseg = NILFS_SB2_OFFSET_BYTES(range[1]);
	do_div(maxseg, segbytes);
	maxseg--;

	ret = nilfs_sufile_set_alloc_range(nilfs->ns_sufile, minseg, maxseg);
out:
	return ret;
}

/**
 * nilfs_ioctl_get_info - wrapping function of get metadata info
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 * @membsz: size of an item in bytes
 * @dofunc: concrete function of getting metadata info
 *
 * Description: nilfs_ioctl_get_info() gets metadata info by means of
 * calling dofunc() function.
 *
 * Return Value: On success, 0 is returned and requested metadata info
 * is copied into userspace. On error, one of the following
 * negative error codes is returned.
 *
 * %-EINVAL - Invalid arguments from userspace.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EFAULT - Failure during execution of requested operation.
 */
static int nilfs_ioctl_get_info(struct inode *inode, struct file *filp,
				unsigned int cmd, void __user *argp,
				size_t membsz,
				ssize_t (*dofunc)(struct the_nilfs *,
						  __u64 *, int,
						  void *, size_t, size_t))

{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_argv argv;
	int ret;

	if (copy_from_user(&argv, argp, sizeof(argv)))
		return -EFAULT;

	if (argv.v_size < membsz)
		return -EINVAL;

	ret = nilfs_ioctl_wrap_copy(nilfs, &argv, _IOC_DIR(cmd), dofunc);
	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &argv, sizeof(argv)))
		ret = -EFAULT;
	return ret;
}

/**
 * nilfs_ioctl_set_suinfo - set segment usage info
 * @inode: inode object
 * @filp: file object
 * @cmd: ioctl's request code
 * @argp: pointer on argument from userspace
 *
 * Description: Expects an array of nilfs_suinfo_update structures
 * encapsulated in nilfs_argv and updates the segment usage info
 * according to the flags in nilfs_suinfo_update.
 *
 * Return Value: On success, 0 is returned. On error, one of the
 * following negative error codes is returned.
 *
 * %-EPERM - Not enough permissions
 *
 * %-EFAULT - Error copying input data
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - Invalid values in input (segment number, flags or nblocks)
 */
static int nilfs_ioctl_set_suinfo(struct inode *inode, struct file *filp,
				unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_transaction_info ti;
	struct nilfs_argv argv;
	size_t len;
	void __user *base;
	void *kbuf;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = -EFAULT;
	if (copy_from_user(&argv, argp, sizeof(argv)))
		goto out;

	ret = -EINVAL;
	if (argv.v_size < sizeof(struct nilfs_suinfo_update))
		goto out;

	if (argv.v_nmembs > nilfs->ns_nsegments)
		goto out;

	if (argv.v_nmembs >= UINT_MAX / argv.v_size)
		goto out;

	len = argv.v_size * argv.v_nmembs;
	if (!len) {
		ret = 0;
		goto out;
	}

	base = (void __user *)(unsigned long)argv.v_base;
	kbuf = vmalloc(len);
	if (!kbuf) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(kbuf, base, len)) {
		ret = -EFAULT;
		goto out_free;
	}

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_sufile_set_suinfo(nilfs->ns_sufile, kbuf, argv.v_size,
			argv.v_nmembs);
	if (unlikely(ret < 0))
		nilfs_transaction_abort(inode->i_sb);
	else
		nilfs_transaction_commit(inode->i_sb); /* never fails */

out_free:
	vfree(kbuf);
out:
	mnt_drop_write_file(filp);
	return ret;
}

long nilfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return nilfs_ioctl_getflags(inode, argp);
	case FS_IOC_SETFLAGS:
		return nilfs_ioctl_setflags(inode, filp, argp);
	case FS_IOC_GETVERSION:
		return nilfs_ioctl_getversion(inode, argp);
	case NILFS_IOCTL_CHANGE_CPMODE:
		return nilfs_ioctl_change_cpmode(inode, filp, cmd, argp);
	case NILFS_IOCTL_DELETE_CHECKPOINT:
		return nilfs_ioctl_delete_checkpoint(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_CPINFO:
		return nilfs_ioctl_get_info(inode, filp, cmd, argp,
					    sizeof(struct nilfs_cpinfo),
					    nilfs_ioctl_do_get_cpinfo);
	case NILFS_IOCTL_GET_CPSTAT:
		return nilfs_ioctl_get_cpstat(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_SUINFO:
		return nilfs_ioctl_get_info(inode, filp, cmd, argp,
					    sizeof(struct nilfs_suinfo),
					    nilfs_ioctl_do_get_suinfo);
	case NILFS_IOCTL_SET_SUINFO:
		return nilfs_ioctl_set_suinfo(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_SUSTAT:
		return nilfs_ioctl_get_sustat(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_VINFO:
		return nilfs_ioctl_get_info(inode, filp, cmd, argp,
					    sizeof(struct nilfs_vinfo),
					    nilfs_ioctl_do_get_vinfo);
	case NILFS_IOCTL_GET_BDESCS:
		return nilfs_ioctl_get_bdescs(inode, filp, cmd, argp);
	case NILFS_IOCTL_CLEAN_SEGMENTS:
		return nilfs_ioctl_clean_segments(inode, filp, cmd, argp);
	case NILFS_IOCTL_SYNC:
		return nilfs_ioctl_sync(inode, filp, cmd, argp);
	case NILFS_IOCTL_RESIZE:
		return nilfs_ioctl_resize(inode, filp, argp);
	case NILFS_IOCTL_SET_ALLOC_RANGE:
		return nilfs_ioctl_set_alloc_range(inode, argp);
	case FITRIM:
		return nilfs_ioctl_trim_fs(inode, argp);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long nilfs_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	case NILFS_IOCTL_CHANGE_CPMODE:
	case NILFS_IOCTL_DELETE_CHECKPOINT:
	case NILFS_IOCTL_GET_CPINFO:
	case NILFS_IOCTL_GET_CPSTAT:
	case NILFS_IOCTL_GET_SUINFO:
	case NILFS_IOCTL_SET_SUINFO:
	case NILFS_IOCTL_GET_SUSTAT:
	case NILFS_IOCTL_GET_VINFO:
	case NILFS_IOCTL_GET_BDESCS:
	case NILFS_IOCTL_CLEAN_SEGMENTS:
	case NILFS_IOCTL_SYNC:
	case NILFS_IOCTL_RESIZE:
	case NILFS_IOCTL_SET_ALLOC_RANGE:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return nilfs_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif
