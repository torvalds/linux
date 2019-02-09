/*
 *  linux/fs/nfs/file.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/falloc.h>
#include <linux/nfs_fs.h>
#include <uapi/linux/btrfs.h>	/* BTRFS_IOC_CLONE/BTRFS_IOC_CLONE_RANGE */
#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "fscache.h"
#include "pnfs.h"

#include "nfstrace.h"

#ifdef CONFIG_NFS_V4_2
#include "nfs42.h"
#endif

#define NFSDBG_FACILITY		NFSDBG_FILE

static int
nfs4_file_open(struct inode *inode, struct file *filp)
{
	struct nfs_open_context *ctx;
	struct dentry *dentry = filp->f_path.dentry;
	struct dentry *parent = NULL;
	struct inode *dir;
	unsigned openflags = filp->f_flags;
	struct iattr attr;
	int err;

	/*
	 * If no cached dentry exists or if it's negative, NFSv4 handled the
	 * opens in ->lookup() or ->create().
	 *
	 * We only get this far for a cached positive dentry.  We skipped
	 * revalidation, so handle it here by dropping the dentry and returning
	 * -EOPENSTALE.  The VFS will retry the lookup/create/open.
	 */

	dprintk("NFS: open file(%pd2)\n", dentry);

	err = nfs_check_flags(openflags);
	if (err)
		return err;

	if ((openflags & O_ACCMODE) == 3)
		openflags--;

	/* We can't create new files here */
	openflags &= ~(O_CREAT|O_EXCL);

	parent = dget_parent(dentry);
	dir = d_inode(parent);

	ctx = alloc_nfs_open_context(filp->f_path.dentry, filp->f_mode);
	err = PTR_ERR(ctx);
	if (IS_ERR(ctx))
		goto out;

	attr.ia_valid = ATTR_OPEN;
	if (openflags & O_TRUNC) {
		attr.ia_valid |= ATTR_SIZE;
		attr.ia_size = 0;
		nfs_sync_inode(inode);
	}

	inode = NFS_PROTO(dir)->open_context(dir, ctx, openflags, &attr, NULL);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		switch (err) {
		case -EPERM:
		case -EACCES:
		case -EDQUOT:
		case -ENOSPC:
		case -EROFS:
			goto out_put_ctx;
		default:
			goto out_drop;
		}
	}
	if (inode != d_inode(dentry))
		goto out_drop;

	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	nfs_file_set_open_context(filp, ctx);
	nfs_fscache_open_file(inode, filp);
	err = 0;

out_put_ctx:
	put_nfs_open_context(ctx);
out:
	dput(parent);
	return err;

out_drop:
	d_drop(dentry);
	err = -EOPENSTALE;
	goto out_put_ctx;
}

/*
 * Flush all dirty pages, and check for write errors.
 */
static int
nfs4_file_flush(struct file *file, fl_owner_t id)
{
	struct inode	*inode = file_inode(file);

	dprintk("NFS: flush(%pD2)\n", file);

	nfs_inc_stats(inode, NFSIOS_VFSFLUSH);
	if ((file->f_mode & FMODE_WRITE) == 0)
		return 0;

	/*
	 * If we're holding a write delegation, then check if we're required
	 * to flush the i/o on close. If not, then just start the i/o now.
	 */
	if (!nfs4_delegation_flush_on_close(inode))
		return filemap_fdatawrite(file->f_mapping);

	/* Flush writes to the server and return any errors */
	return vfs_fsync(file, 0);
}

static int
nfs4_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct inode *inode = file_inode(file);

	trace_nfs_fsync_enter(inode);

	nfs_inode_dio_wait(inode);
	do {
		ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
		if (ret != 0)
			break;
		mutex_lock(&inode->i_mutex);
		ret = nfs_file_fsync_commit(file, start, end, datasync);
		if (!ret)
			ret = pnfs_sync_inode(inode, !!datasync);
		mutex_unlock(&inode->i_mutex);
		/*
		 * If nfs_file_fsync_commit detected a server reboot, then
		 * resend all dirty pages that might have been covered by
		 * the NFS_CONTEXT_RESEND_WRITES flag
		 */
		start = 0;
		end = LLONG_MAX;
	} while (ret == -EAGAIN);

	trace_nfs_fsync_exit(inode, ret);
	return ret;
}

#ifdef CONFIG_NFS_V4_2
static loff_t nfs4_file_llseek(struct file *filep, loff_t offset, int whence)
{
	loff_t ret;

	switch (whence) {
	case SEEK_HOLE:
	case SEEK_DATA:
		ret = nfs42_proc_llseek(filep, offset, whence);
		if (ret != -ENOTSUPP)
			return ret;
	default:
		return nfs_file_llseek(filep, offset, whence);
	}
}

static long nfs42_fallocate(struct file *filep, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(filep);
	long ret;

	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	if ((mode != 0) && (mode != (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE)))
		return -EOPNOTSUPP;

	ret = inode_newsize_ok(inode, offset + len);
	if (ret < 0)
		return ret;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		return nfs42_proc_deallocate(filep, offset, len);
	return nfs42_proc_allocate(filep, offset, len);
}

static noinline long
nfs42_ioctl_clone(struct file *dst_file, unsigned long srcfd,
		  u64 src_off, u64 dst_off, u64 count)
{
	struct inode *dst_inode = file_inode(dst_file);
	struct nfs_server *server = NFS_SERVER(dst_inode);
	struct fd src_file;
	struct inode *src_inode;
	unsigned int bs = server->clone_blksize;
	bool same_inode = false;
	int ret;

	/* dst file must be opened for writing */
	if (!(dst_file->f_mode & FMODE_WRITE))
		return -EINVAL;

	ret = mnt_want_write_file(dst_file);
	if (ret)
		return ret;

	src_file = fdget(srcfd);
	if (!src_file.file) {
		ret = -EBADF;
		goto out_drop_write;
	}

	src_inode = file_inode(src_file.file);

	if (src_inode == dst_inode)
		same_inode = true;

	/* src file must be opened for reading */
	if (!(src_file.file->f_mode & FMODE_READ))
		goto out_fput;

	/* src and dst must be regular files */
	ret = -EISDIR;
	if (!S_ISREG(src_inode->i_mode) || !S_ISREG(dst_inode->i_mode))
		goto out_fput;

	ret = -EXDEV;
	if (src_file.file->f_path.mnt != dst_file->f_path.mnt ||
	    src_inode->i_sb != dst_inode->i_sb)
		goto out_fput;

	/* check alignment w.r.t. clone_blksize */
	ret = -EINVAL;
	if (bs) {
		if (!IS_ALIGNED(src_off, bs) || !IS_ALIGNED(dst_off, bs))
			goto out_fput;
		if (!IS_ALIGNED(count, bs) && i_size_read(src_inode) != (src_off + count))
			goto out_fput;
	}

	/* verify if ranges are overlapped within the same file */
	if (same_inode) {
		if (dst_off + count > src_off && dst_off < src_off + count)
			goto out_fput;
	}

	/* XXX: do we lock at all? what if server needs CB_RECALL_LAYOUT? */
	if (same_inode) {
		mutex_lock(&src_inode->i_mutex);
	} else if (dst_inode < src_inode) {
		mutex_lock_nested(&dst_inode->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&src_inode->i_mutex, I_MUTEX_CHILD);
	} else {
		mutex_lock_nested(&src_inode->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&dst_inode->i_mutex, I_MUTEX_CHILD);
	}

	/* flush all pending writes on both src and dst so that server
	 * has the latest data */
	ret = nfs_sync_inode(src_inode);
	if (ret)
		goto out_unlock;
	ret = nfs_sync_inode(dst_inode);
	if (ret)
		goto out_unlock;

	ret = nfs42_proc_clone(src_file.file, dst_file, src_off, dst_off, count);

	/* truncate inode page cache of the dst range so that future reads can fetch
	 * new data from server */
	if (!ret)
		truncate_inode_pages_range(&dst_inode->i_data, dst_off, dst_off + count - 1);

out_unlock:
	if (same_inode) {
		mutex_unlock(&src_inode->i_mutex);
	} else if (dst_inode < src_inode) {
		mutex_unlock(&src_inode->i_mutex);
		mutex_unlock(&dst_inode->i_mutex);
	} else {
		mutex_unlock(&dst_inode->i_mutex);
		mutex_unlock(&src_inode->i_mutex);
	}
out_fput:
	fdput(src_file);
out_drop_write:
	mnt_drop_write_file(dst_file);
	return ret;
}

static long nfs42_ioctl_clone_range(struct file *dst_file, void __user *argp)
{
	struct btrfs_ioctl_clone_range_args args;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	return nfs42_ioctl_clone(dst_file, args.src_fd, args.src_offset,
				 args.dest_offset, args.src_length);
}

long nfs4_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case BTRFS_IOC_CLONE:
		return nfs42_ioctl_clone(file, arg, 0, 0, 0);
	case BTRFS_IOC_CLONE_RANGE:
		return nfs42_ioctl_clone_range(file, argp);
	}

	return -ENOTTY;
}
#endif /* CONFIG_NFS_V4_2 */

const struct file_operations nfs4_file_operations = {
	.read_iter	= nfs_file_read,
	.write_iter	= nfs_file_write,
	.mmap		= nfs_file_mmap,
	.open		= nfs4_file_open,
	.flush		= nfs4_file_flush,
	.release	= nfs_file_release,
	.fsync		= nfs4_file_fsync,
	.lock		= nfs_lock,
	.flock		= nfs_flock,
	.splice_read	= nfs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.check_flags	= nfs_check_flags,
	.setlease	= simple_nosetlease,
#ifdef CONFIG_NFS_V4_2
	.llseek		= nfs4_file_llseek,
	.fallocate	= nfs42_fallocate,
	.unlocked_ioctl = nfs4_ioctl,
	.compat_ioctl	= nfs4_ioctl,
#else
	.llseek		= nfs_file_llseek,
#endif
};
