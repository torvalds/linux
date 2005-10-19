/*
 *  linux/fs/nfs/file.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Changes Copyright (C) 1994 by Florian La Roche
 *   - Do not copy data too often around in the kernel.
 *   - In nfs_file_read the return value of kmalloc wasn't checked.
 *   - Put in a better version of read look-ahead buffering. Original idea
 *     and implementation by Wai S Kok elekokws@ee.nus.sg.
 *
 *  Expire cache on write to a file by Wai S Kok (Oct 1994).
 *
 *  Total rewrite of read side for new NFS buffer cache.. Linus.
 *
 *  nfs regular file handling functions
 */

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include "delegation.h"

#define NFSDBG_FACILITY		NFSDBG_FILE

static int nfs_file_open(struct inode *, struct file *);
static int nfs_file_release(struct inode *, struct file *);
static loff_t nfs_file_llseek(struct file *file, loff_t offset, int origin);
static int  nfs_file_mmap(struct file *, struct vm_area_struct *);
static ssize_t nfs_file_sendfile(struct file *, loff_t *, size_t, read_actor_t, void *);
static ssize_t nfs_file_read(struct kiocb *, char __user *, size_t, loff_t);
static ssize_t nfs_file_write(struct kiocb *, const char __user *, size_t, loff_t);
static int  nfs_file_flush(struct file *);
static int  nfs_fsync(struct file *, struct dentry *dentry, int datasync);
static int nfs_check_flags(int flags);
static int nfs_lock(struct file *filp, int cmd, struct file_lock *fl);
static int nfs_flock(struct file *filp, int cmd, struct file_lock *fl);

struct file_operations nfs_file_operations = {
	.llseek		= nfs_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read		= nfs_file_read,
	.aio_write		= nfs_file_write,
	.mmap		= nfs_file_mmap,
	.open		= nfs_file_open,
	.flush		= nfs_file_flush,
	.release	= nfs_file_release,
	.fsync		= nfs_fsync,
	.lock		= nfs_lock,
	.flock		= nfs_flock,
	.sendfile	= nfs_file_sendfile,
	.check_flags	= nfs_check_flags,
};

struct inode_operations nfs_file_inode_operations = {
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
};

#ifdef CONFIG_NFS_V3
struct inode_operations nfs3_file_inode_operations = {
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.listxattr	= nfs3_listxattr,
	.getxattr	= nfs3_getxattr,
	.setxattr	= nfs3_setxattr,
	.removexattr	= nfs3_removexattr,
};
#endif  /* CONFIG_NFS_v3 */

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

static int nfs_check_flags(int flags)
{
	if ((flags & (O_APPEND | O_DIRECT)) == (O_APPEND | O_DIRECT))
		return -EINVAL;

	return 0;
}

/*
 * Open file
 */
static int
nfs_file_open(struct inode *inode, struct file *filp)
{
	struct nfs_server *server = NFS_SERVER(inode);
	int (*open)(struct inode *, struct file *);
	int res;

	res = nfs_check_flags(filp->f_flags);
	if (res)
		return res;

	lock_kernel();
	/* Do NFSv4 open() call */
	if ((open = server->rpc_ops->file_open) != NULL)
		res = open(inode, filp);
	unlock_kernel();
	return res;
}

static int
nfs_file_release(struct inode *inode, struct file *filp)
{
	/* Ensure that dirty pages are flushed out with the right creds */
	if (filp->f_mode & FMODE_WRITE)
		filemap_fdatawrite(filp->f_mapping);
	return NFS_PROTO(inode)->file_release(inode, filp);
}

/**
 * nfs_revalidate_file - Revalidate the page cache & related metadata
 * @inode - pointer to inode struct
 * @file - pointer to file
 */
static int nfs_revalidate_file(struct inode *inode, struct file *filp)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int retval = 0;

	if ((nfsi->cache_validity & (NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_ATTR))
			|| nfs_attribute_timeout(inode))
		retval = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
	nfs_revalidate_mapping(inode, filp->f_mapping);
	return 0;
}

/**
 * nfs_revalidate_size - Revalidate the file size
 * @inode - pointer to inode struct
 * @file - pointer to struct file
 *
 * Revalidates the file length. This is basically a wrapper around
 * nfs_revalidate_inode() that takes into account the fact that we may
 * have cached writes (in which case we don't care about the server's
 * idea of what the file length is), or O_DIRECT (in which case we
 * shouldn't trust the cache).
 */
static int nfs_revalidate_file_size(struct inode *inode, struct file *filp)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);

	if (server->flags & NFS_MOUNT_NOAC)
		goto force_reval;
	if (filp->f_flags & O_DIRECT)
		goto force_reval;
	if (nfsi->npages != 0)
		return 0;
	if (!(nfsi->cache_validity & NFS_INO_REVAL_PAGECACHE) && !nfs_attribute_timeout(inode))
		return 0;
force_reval:
	return __nfs_revalidate_inode(server, inode);
}

static loff_t nfs_file_llseek(struct file *filp, loff_t offset, int origin)
{
	/* origin == SEEK_END => we must revalidate the cached file length */
	if (origin == 2) {
		struct inode *inode = filp->f_mapping->host;
		int retval = nfs_revalidate_file_size(inode, filp);
		if (retval < 0)
			return (loff_t)retval;
	}
	return remote_llseek(filp, offset, origin);
}

/*
 * Flush all dirty pages, and check for write errors.
 *
 */
static int
nfs_file_flush(struct file *file)
{
	struct nfs_open_context *ctx = (struct nfs_open_context *)file->private_data;
	struct inode	*inode = file->f_dentry->d_inode;
	int		status;

	dfprintk(VFS, "nfs: flush(%s/%ld)\n", inode->i_sb->s_id, inode->i_ino);

	if ((file->f_mode & FMODE_WRITE) == 0)
		return 0;
	lock_kernel();
	/* Ensure that data+attribute caches are up to date after close() */
	status = nfs_wb_all(inode);
	if (!status) {
		status = ctx->error;
		ctx->error = 0;
		if (!status && !nfs_have_delegation(inode, FMODE_READ))
			__nfs_revalidate_inode(NFS_SERVER(inode), inode);
	}
	unlock_kernel();
	return status;
}

static ssize_t
nfs_file_read(struct kiocb *iocb, char __user * buf, size_t count, loff_t pos)
{
	struct dentry * dentry = iocb->ki_filp->f_dentry;
	struct inode * inode = dentry->d_inode;
	ssize_t result;

#ifdef CONFIG_NFS_DIRECTIO
	if (iocb->ki_filp->f_flags & O_DIRECT)
		return nfs_file_direct_read(iocb, buf, count, pos);
#endif

	dfprintk(VFS, "nfs: read(%s/%s, %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) count, (unsigned long) pos);

	result = nfs_revalidate_file(inode, iocb->ki_filp);
	if (!result)
		result = generic_file_aio_read(iocb, buf, count, pos);
	return result;
}

static ssize_t
nfs_file_sendfile(struct file *filp, loff_t *ppos, size_t count,
		read_actor_t actor, void *target)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	ssize_t res;

	dfprintk(VFS, "nfs: sendfile(%s/%s, %lu@%Lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) count, (unsigned long long) *ppos);

	res = nfs_revalidate_file(inode, filp);
	if (!res)
		res = generic_file_sendfile(filp, ppos, count, actor, target);
	return res;
}

static int
nfs_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	int	status;

	dfprintk(VFS, "nfs: mmap(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	status = nfs_revalidate_file(inode, file);
	if (!status)
		status = generic_file_mmap(file, vma);
	return status;
}

/*
 * Flush any dirty pages for this process, and check for write errors.
 * The return status from this call provides a reliable indication of
 * whether any write errors occurred for this process.
 */
static int
nfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct nfs_open_context *ctx = (struct nfs_open_context *)file->private_data;
	struct inode *inode = dentry->d_inode;
	int status;

	dfprintk(VFS, "nfs: fsync(%s/%ld)\n", inode->i_sb->s_id, inode->i_ino);

	lock_kernel();
	status = nfs_wb_all(inode);
	if (!status) {
		status = ctx->error;
		ctx->error = 0;
	}
	unlock_kernel();
	return status;
}

/*
 * This does the "real" work of the write. The generic routine has
 * allocated the page, locked it, done all the page alignment stuff
 * calculations etc. Now we should just copy the data from user
 * space and write it back to the real medium..
 *
 * If the writer ends up delaying the write, the writer needs to
 * increment the page use counts until he is done with the page.
 */
static int nfs_prepare_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	return nfs_flush_incompatible(file, page);
}

static int nfs_commit_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	long status;

	lock_kernel();
	status = nfs_updatepage(file, page, offset, to-offset);
	unlock_kernel();
	return status;
}

struct address_space_operations nfs_file_aops = {
	.readpage = nfs_readpage,
	.readpages = nfs_readpages,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.writepage = nfs_writepage,
	.writepages = nfs_writepages,
	.prepare_write = nfs_prepare_write,
	.commit_write = nfs_commit_write,
#ifdef CONFIG_NFS_DIRECTIO
	.direct_IO = nfs_direct_IO,
#endif
};

/* 
 * Write to a file (through the page cache).
 */
static ssize_t
nfs_file_write(struct kiocb *iocb, const char __user *buf, size_t count, loff_t pos)
{
	struct dentry * dentry = iocb->ki_filp->f_dentry;
	struct inode * inode = dentry->d_inode;
	ssize_t result;

#ifdef CONFIG_NFS_DIRECTIO
	if (iocb->ki_filp->f_flags & O_DIRECT)
		return nfs_file_direct_write(iocb, buf, count, pos);
#endif

	dfprintk(VFS, "nfs: write(%s/%s(%ld), %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino, (unsigned long) count, (unsigned long) pos);

	result = -EBUSY;
	if (IS_SWAPFILE(inode))
		goto out_swapfile;
	/*
	 * O_APPEND implies that we must revalidate the file length.
	 */
	if (iocb->ki_filp->f_flags & O_APPEND) {
		result = nfs_revalidate_file_size(inode, iocb->ki_filp);
		if (result)
			goto out;
	}
	nfs_revalidate_mapping(inode, iocb->ki_filp->f_mapping);

	result = count;
	if (!count)
		goto out;

	result = generic_file_aio_write(iocb, buf, count, pos);
out:
	return result;

out_swapfile:
	printk(KERN_INFO "NFS: attempt to write to active swap file!\n");
	goto out;
}

static int do_getlk(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_mapping->host;
	int status = 0;

	lock_kernel();
	/* Use local locking if mounted with "-onolock" */
	if (!(NFS_SERVER(inode)->flags & NFS_MOUNT_NONLM))
		status = NFS_PROTO(inode)->lock(filp, cmd, fl);
	else {
		struct file_lock *cfl = posix_test_lock(filp, fl);

		fl->fl_type = F_UNLCK;
		if (cfl != NULL)
			memcpy(fl, cfl, sizeof(*fl));
	}
	unlock_kernel();
	return status;
}

static int do_vfs_lock(struct file *file, struct file_lock *fl)
{
	int res = 0;
	switch (fl->fl_flags & (FL_POSIX|FL_FLOCK)) {
		case FL_POSIX:
			res = posix_lock_file_wait(file, fl);
			break;
		case FL_FLOCK:
			res = flock_lock_file_wait(file, fl);
			break;
		default:
			BUG();
	}
	if (res < 0)
		printk(KERN_WARNING "%s: VFS is out of sync with lock manager!\n",
				__FUNCTION__);
	return res;
}

static int do_unlk(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_mapping->host;
	sigset_t oldset;
	int status;

	rpc_clnt_sigmask(NFS_CLIENT(inode), &oldset);
	/*
	 * Flush all pending writes before doing anything
	 * with locks..
	 */
	filemap_fdatawrite(filp->f_mapping);
	down(&inode->i_sem);
	nfs_wb_all(inode);
	up(&inode->i_sem);
	filemap_fdatawait(filp->f_mapping);

	/* NOTE: special case
	 * 	If we're signalled while cleaning up locks on process exit, we
	 * 	still need to complete the unlock.
	 */
	lock_kernel();
	/* Use local locking if mounted with "-onolock" */
	if (!(NFS_SERVER(inode)->flags & NFS_MOUNT_NONLM))
		status = NFS_PROTO(inode)->lock(filp, cmd, fl);
	else
		status = do_vfs_lock(filp, fl);
	unlock_kernel();
	rpc_clnt_sigunmask(NFS_CLIENT(inode), &oldset);
	return status;
}

static int do_setlk(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_mapping->host;
	sigset_t oldset;
	int status;

	rpc_clnt_sigmask(NFS_CLIENT(inode), &oldset);
	/*
	 * Flush all pending writes before doing anything
	 * with locks..
	 */
	status = filemap_fdatawrite(filp->f_mapping);
	if (status == 0) {
		down(&inode->i_sem);
		status = nfs_wb_all(inode);
		up(&inode->i_sem);
		if (status == 0)
			status = filemap_fdatawait(filp->f_mapping);
	}
	if (status < 0)
		goto out;

	lock_kernel();
	/* Use local locking if mounted with "-onolock" */
	if (!(NFS_SERVER(inode)->flags & NFS_MOUNT_NONLM)) {
		status = NFS_PROTO(inode)->lock(filp, cmd, fl);
		/* If we were signalled we still need to ensure that
		 * we clean up any state on the server. We therefore
		 * record the lock call as having succeeded in order to
		 * ensure that locks_remove_posix() cleans it out when
		 * the process exits.
		 */
		if (status == -EINTR || status == -ERESTARTSYS)
			do_vfs_lock(filp, fl);
	} else
		status = do_vfs_lock(filp, fl);
	unlock_kernel();
	if (status < 0)
		goto out;
	/*
	 * Make sure we clear the cache whenever we try to get the lock.
	 * This makes locking act as a cache coherency point.
	 */
	filemap_fdatawrite(filp->f_mapping);
	down(&inode->i_sem);
	nfs_wb_all(inode);	/* we may have slept */
	up(&inode->i_sem);
	filemap_fdatawait(filp->f_mapping);
	nfs_zap_caches(inode);
out:
	rpc_clnt_sigunmask(NFS_CLIENT(inode), &oldset);
	return status;
}

/*
 * Lock a (portion of) a file
 */
static int nfs_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode * inode = filp->f_mapping->host;

	dprintk("NFS: nfs_lock(f=%s/%ld, t=%x, fl=%x, r=%Ld:%Ld)\n",
			inode->i_sb->s_id, inode->i_ino,
			fl->fl_type, fl->fl_flags,
			(long long)fl->fl_start, (long long)fl->fl_end);

	if (!inode)
		return -EINVAL;

	/* No mandatory locks over NFS */
	if ((inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (IS_GETLK(cmd))
		return do_getlk(filp, cmd, fl);
	if (fl->fl_type == F_UNLCK)
		return do_unlk(filp, cmd, fl);
	return do_setlk(filp, cmd, fl);
}

/*
 * Lock a (portion of) a file
 */
static int nfs_flock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode * inode = filp->f_mapping->host;

	dprintk("NFS: nfs_flock(f=%s/%ld, t=%x, fl=%x)\n",
			inode->i_sb->s_id, inode->i_ino,
			fl->fl_type, fl->fl_flags);

	if (!inode)
		return -EINVAL;

	/*
	 * No BSD flocks over NFS allowed.
	 * Note: we could try to fake a POSIX lock request here by
	 * using ((u32) filp | 0x80000000) or some such as the pid.
	 * Not sure whether that would be unique, though, or whether
	 * that would break in other places.
	 */
	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;

	/* We're simulating flock() locks using posix locks on the server */
	fl->fl_owner = (fl_owner_t)filp;
	fl->fl_start = 0;
	fl->fl_end = OFFSET_MAX;

	if (fl->fl_type == F_UNLCK)
		return do_unlk(filp, cmd, fl);
	return do_setlk(filp, cmd, fl);
}
