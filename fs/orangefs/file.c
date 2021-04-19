// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 * Copyright 2018 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS file operations.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"
#include <linux/fs.h>
#include <linux/pagemap.h>

static int flush_racache(struct inode *inode)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	int ret;

	gossip_debug(GOSSIP_UTILS_DEBUG,
	    "%s: %pU: Handle is %pU | fs_id %d\n", __func__,
	    get_khandle_from_ino(inode), &orangefs_inode->refn.khandle,
	    orangefs_inode->refn.fs_id);

	new_op = op_alloc(ORANGEFS_VFS_OP_RA_FLUSH);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.ra_cache_flush.refn = orangefs_inode->refn;

	ret = service_operation(new_op, "orangefs_flush_racache",
	    get_interruptible_flag(inode));

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: got return value of %d\n",
	    __func__, ret);

	op_release(new_op);
	return ret;
}

/*
 * Post and wait for the I/O upcall to finish
 */
ssize_t wait_for_direct_io(enum ORANGEFS_io_type type, struct inode *inode,
	loff_t *offset, struct iov_iter *iter, size_t total_size,
	loff_t readahead_size, struct orangefs_write_range *wr,
	int *index_return, struct file *file)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_khandle *handle = &orangefs_inode->refn.khandle;
	struct orangefs_kernel_op_s *new_op = NULL;
	int buffer_index;
	ssize_t ret;
	size_t copy_amount;
	int open_for_read;
	int open_for_write;

	new_op = op_alloc(ORANGEFS_VFS_OP_FILE_IO);
	if (!new_op)
		return -ENOMEM;

	/* synchronous I/O */
	new_op->upcall.req.io.readahead_size = readahead_size;
	new_op->upcall.req.io.io_type = type;
	new_op->upcall.req.io.refn = orangefs_inode->refn;

populate_shared_memory:
	/* get a shared buffer index */
	buffer_index = orangefs_bufmap_get();
	if (buffer_index < 0) {
		ret = buffer_index;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: orangefs_bufmap_get failure (%zd)\n",
			     __func__, ret);
		goto out;
	}
	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): GET op %p -> buffer_index %d\n",
		     __func__,
		     handle,
		     new_op,
		     buffer_index);

	new_op->uses_shared_memory = 1;
	new_op->upcall.req.io.buf_index = buffer_index;
	new_op->upcall.req.io.count = total_size;
	new_op->upcall.req.io.offset = *offset;
	if (type == ORANGEFS_IO_WRITE && wr) {
		new_op->upcall.uid = from_kuid(&init_user_ns, wr->uid);
		new_op->upcall.gid = from_kgid(&init_user_ns, wr->gid);
	}
	/*
	 * Orangefs has no open, and orangefs checks file permissions
	 * on each file access. Posix requires that file permissions
	 * be checked on open and nowhere else. Orangefs-through-the-kernel
	 * needs to seem posix compliant.
	 *
	 * The VFS opens files, even if the filesystem provides no
	 * method. We can see if a file was successfully opened for
	 * read and or for write by looking at file->f_mode.
	 *
	 * When writes are flowing from the page cache, file is no
	 * longer available. We can trust the VFS to have checked
	 * file->f_mode before writing to the page cache.
	 *
	 * The mode of a file might change between when it is opened
	 * and IO commences, or it might be created with an arbitrary mode.
	 *
	 * We'll make sure we don't hit EACCES during the IO stage by
	 * using UID 0. Some of the time we have access without changing
	 * to UID 0 - how to check?
	 */
	if (file) {
		open_for_write = file->f_mode & FMODE_WRITE;
		open_for_read = file->f_mode & FMODE_READ;
	} else {
		open_for_write = 1;
		open_for_read = 0; /* not relevant? */
	}
	if ((type == ORANGEFS_IO_WRITE) && open_for_write)
		new_op->upcall.uid = 0;
	if ((type == ORANGEFS_IO_READ) && open_for_read)
		new_op->upcall.uid = 0;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): offset: %llu total_size: %zd\n",
		     __func__,
		     handle,
		     llu(*offset),
		     total_size);
	/*
	 * Stage 1: copy the buffers into client-core's address space
	 */
	if (type == ORANGEFS_IO_WRITE && total_size) {
		ret = orangefs_bufmap_copy_from_iovec(iter, buffer_index,
		    total_size);
		if (ret < 0) {
			gossip_err("%s: Failed to copy-in buffers. Please make sure that the pvfs2-client is running. %ld\n",
			    __func__, (long)ret);
			goto out;
		}
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): Calling post_io_request with tag (%llu)\n",
		     __func__,
		     handle,
		     llu(new_op->tag));

	/* Stage 2: Service the I/O operation */
	ret = service_operation(new_op,
				type == ORANGEFS_IO_WRITE ?
					"file_write" :
					"file_read",
				get_interruptible_flag(inode));

	/*
	 * If service_operation() returns -EAGAIN #and# the operation was
	 * purged from orangefs_request_list or htable_ops_in_progress, then
	 * we know that the client was restarted, causing the shared memory
	 * area to be wiped clean.  To restart a  write operation in this
	 * case, we must re-copy the data from the user's iovec to a NEW
	 * shared memory location. To restart a read operation, we must get
	 * a new shared memory location.
	 */
	if (ret == -EAGAIN && op_state_purged(new_op)) {
		orangefs_bufmap_put(buffer_index);
		if (type == ORANGEFS_IO_WRITE)
			iov_iter_revert(iter, total_size);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s:going to repopulate_shared_memory.\n",
			     __func__);
		goto populate_shared_memory;
	}

	if (ret < 0) {
		if (ret == -EINTR) {
			/*
			 * We can't return EINTR if any data was written,
			 * it's not POSIX. It is minimally acceptable
			 * to give a partial write, the way NFS does.
			 *
			 * It would be optimal to return all or nothing,
			 * but if a userspace write is bigger than
			 * an IO buffer, and the interrupt occurs
			 * between buffer writes, that would not be
			 * possible.
			 */
			switch (new_op->op_state - OP_VFS_STATE_GIVEN_UP) {
			/*
			 * If the op was waiting when the interrupt
			 * occurred, then the client-core did not
			 * trigger the write.
			 */
			case OP_VFS_STATE_WAITING:
				if (*offset == 0)
					ret = -EINTR;
				else
					ret = 0;
				break;
			/*
			 * If the op was in progress when the interrupt
			 * occurred, then the client-core was able to
			 * trigger the write.
			 */
			case OP_VFS_STATE_INPROGR:
				if (type == ORANGEFS_IO_READ)
					ret = -EINTR;
				else
					ret = total_size;
				break;
			default:
				gossip_err("%s: unexpected op state :%d:.\n",
					   __func__,
					   new_op->op_state);
				ret = 0;
				break;
			}
			gossip_debug(GOSSIP_FILE_DEBUG,
				     "%s: got EINTR, state:%d: %p\n",
				     __func__,
				     new_op->op_state,
				     new_op);
		} else {
			gossip_err("%s: error in %s handle %pU, returning %zd\n",
				__func__,
				type == ORANGEFS_IO_READ ?
					"read from" : "write to",
				handle, ret);
		}
		if (orangefs_cancel_op_in_progress(new_op))
			return ret;

		goto out;
	}

	/*
	 * Stage 3: Post copy buffers from client-core's address space
	 */
	if (type == ORANGEFS_IO_READ && new_op->downcall.resp.io.amt_complete) {
		/*
		 * NOTE: the iovector can either contain addresses which
		 *       can futher be kernel-space or user-space addresses.
		 *       or it can pointers to struct page's
		 */

		/*
		 * When reading, readahead_size will only be zero when
		 * we're doing O_DIRECT, otherwise we got here from
		 * orangefs_readpage.
		 *
		 * If we got here from orangefs_readpage we want to
		 * copy either a page or the whole file into the io
		 * vector, whichever is smaller.
		 */
		if (readahead_size)
			copy_amount =
				min(new_op->downcall.resp.io.amt_complete,
					(__s64)PAGE_SIZE);
		else
			copy_amount = new_op->downcall.resp.io.amt_complete;

		ret = orangefs_bufmap_copy_to_iovec(iter, buffer_index,
			copy_amount);
		if (ret < 0) {
			gossip_err("%s: Failed to copy-out buffers. Please make sure that the pvfs2-client is running (%ld)\n",
			    __func__, (long)ret);
			goto out;
		}
	}
	gossip_debug(GOSSIP_FILE_DEBUG,
	    "%s(%pU): Amount %s, returned by the sys-io call:%d\n",
	    __func__,
	    handle,
	    type == ORANGEFS_IO_READ ?  "read" : "written",
	    (int)new_op->downcall.resp.io.amt_complete);

	ret = new_op->downcall.resp.io.amt_complete;

out:
	if (buffer_index >= 0) {
		if ((readahead_size) && (type == ORANGEFS_IO_READ)) {
			/* readpage */
			*index_return = buffer_index;
			gossip_debug(GOSSIP_FILE_DEBUG,
				"%s: hold on to buffer_index :%d:\n",
				__func__, buffer_index);
		} else {
			/* O_DIRECT */
			orangefs_bufmap_put(buffer_index);
			gossip_debug(GOSSIP_FILE_DEBUG,
				"%s(%pU): PUT buffer_index %d\n",
				__func__, handle, buffer_index);
		}
	}
	op_release(new_op);
	return ret;
}

int orangefs_revalidate_mapping(struct inode *inode)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct address_space *mapping = inode->i_mapping;
	unsigned long *bitlock = &orangefs_inode->bitlock;
	int ret;

	while (1) {
		ret = wait_on_bit(bitlock, 1, TASK_KILLABLE);
		if (ret)
			return ret;
		spin_lock(&inode->i_lock);
		if (test_bit(1, bitlock)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (!time_before(jiffies, orangefs_inode->mapping_time))
			break;
		spin_unlock(&inode->i_lock);
		return 0;
	}

	set_bit(1, bitlock);
	smp_wmb();
	spin_unlock(&inode->i_lock);

	unmap_mapping_range(mapping, 0, 0, 0);
	ret = filemap_write_and_wait(mapping);
	if (!ret)
		ret = invalidate_inode_pages2(mapping);

	orangefs_inode->mapping_time = jiffies +
	    orangefs_cache_timeout_msecs*HZ/1000;

	clear_bit(1, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, 1);

	return ret;
}

static ssize_t orangefs_file_read_iter(struct kiocb *iocb,
    struct iov_iter *iter)
{
	int ret;
	orangefs_stats.reads++;

	down_read(&file_inode(iocb->ki_filp)->i_rwsem);
	ret = orangefs_revalidate_mapping(file_inode(iocb->ki_filp));
	if (ret)
		goto out;

	ret = generic_file_read_iter(iocb, iter);
out:
	up_read(&file_inode(iocb->ki_filp)->i_rwsem);
	return ret;
}

static ssize_t orangefs_file_write_iter(struct kiocb *iocb,
    struct iov_iter *iter)
{
	int ret;
	orangefs_stats.writes++;

	if (iocb->ki_pos > i_size_read(file_inode(iocb->ki_filp))) {
		ret = orangefs_revalidate_mapping(file_inode(iocb->ki_filp));
		if (ret)
			return ret;
	}

	ret = generic_file_write_iter(iocb, iter);
	return ret;
}

static int orangefs_getflags(struct inode *inode, unsigned long *uval)
{
	__u64 val = 0;
	int ret;

	ret = orangefs_inode_getxattr(inode,
				      "user.pvfs2.meta_hint",
				      &val, sizeof(val));
	if (ret < 0 && ret != -ENODATA)
		return ret;
	else if (ret == -ENODATA)
		val = 0;
	*uval = val;
	return 0;
}

/*
 * Perform a miscellaneous operation on a file.
 */
static long orangefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	int ret = -ENOTTY;
	__u64 val = 0;
	unsigned long uval;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_ioctl: called with cmd %d\n",
		     cmd);

	/*
	 * we understand some general ioctls on files, such as the immutable
	 * and append flags
	 */
	if (cmd == FS_IOC_GETFLAGS) {
		ret = orangefs_getflags(inode, &uval);
		if (ret)
			return ret;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "orangefs_ioctl: FS_IOC_GETFLAGS: %llu\n",
			     (unsigned long long)uval);
		return put_user(uval, (int __user *)arg);
	} else if (cmd == FS_IOC_SETFLAGS) {
		unsigned long old_uval;

		ret = 0;
		if (get_user(uval, (int __user *)arg))
			return -EFAULT;
		/*
		 * ORANGEFS_MIRROR_FL is set internally when the mirroring mode
		 * is turned on for a file. The user is not allowed to turn
		 * on this bit, but the bit is present if the user first gets
		 * the flags and then updates the flags with some new
		 * settings. So, we ignore it in the following edit. bligon.
		 */
		if ((uval & ~ORANGEFS_MIRROR_FL) &
		    (~(FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NOATIME_FL))) {
			gossip_err("orangefs_ioctl: the FS_IOC_SETFLAGS only supports setting one of FS_IMMUTABLE_FL|FS_APPEND_FL|FS_NOATIME_FL\n");
			return -EINVAL;
		}
		ret = orangefs_getflags(inode, &old_uval);
		if (ret)
			return ret;
		ret = vfs_ioc_setflags_prepare(inode, old_uval, uval);
		if (ret)
			return ret;
		val = uval;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "orangefs_ioctl: FS_IOC_SETFLAGS: %llu\n",
			     (unsigned long long)val);
		ret = orangefs_inode_setxattr(inode,
					      "user.pvfs2.meta_hint",
					      &val, sizeof(val), 0);
	}

	return ret;
}

static vm_fault_t orangefs_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	int ret;
	ret = orangefs_inode_getattr(file->f_mapping->host,
	    ORANGEFS_GETATTR_SIZE);
	if (ret == -ESTALE)
		ret = -EIO;
	if (ret) {
		gossip_err("%s: orangefs_inode_getattr failed, "
		    "ret:%d:.\n", __func__, ret);
		return VM_FAULT_SIGBUS;
	}
	return filemap_fault(vmf);
}

static const struct vm_operations_struct orangefs_file_vm_ops = {
	.fault = orangefs_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = orangefs_page_mkwrite,
};

/*
 * Memory map a region of a file.
 */
static int orangefs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;

	ret = orangefs_revalidate_mapping(file_inode(file));
	if (ret)
		return ret;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_file_mmap: called on %pD\n", file);

	/* set the sequential readahead hint */
	vma->vm_flags |= VM_SEQ_READ;
	vma->vm_flags &= ~VM_RAND_READ;

	file_accessed(file);
	vma->vm_ops = &orangefs_file_vm_ops;
	return 0;
}

#define mapping_nrpages(idata) ((idata)->nrpages)

/*
 * Called to notify the module that there are no more references to
 * this file (i.e. no processes have it open).
 *
 * \note Not called when each file is closed.
 */
static int orangefs_file_release(struct inode *inode, struct file *file)
{
	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_file_release: called on %pD\n",
		     file);

	/*
	 * remove all associated inode pages from the page cache and
	 * readahead cache (if any); this forces an expensive refresh of
	 * data for the next caller of mmap (or 'get_block' accesses)
	 */
	if (file_inode(file) &&
	    file_inode(file)->i_mapping &&
	    mapping_nrpages(&file_inode(file)->i_data)) {
		if (orangefs_features & ORANGEFS_FEATURE_READAHEAD) {
			gossip_debug(GOSSIP_INODE_DEBUG,
			    "calling flush_racache on %pU\n",
			    get_khandle_from_ino(inode));
			flush_racache(inode);
			gossip_debug(GOSSIP_INODE_DEBUG,
			    "flush_racache finished\n");
		}

	}
	return 0;
}

/*
 * Push all data for a specific file onto permanent storage.
 */
static int orangefs_fsync(struct file *file,
		       loff_t start,
		       loff_t end,
		       int datasync)
{
	int ret;
	struct orangefs_inode_s *orangefs_inode =
		ORANGEFS_I(file_inode(file));
	struct orangefs_kernel_op_s *new_op = NULL;

	ret = filemap_write_and_wait_range(file_inode(file)->i_mapping,
	    start, end);
	if (ret < 0)
		return ret;

	new_op = op_alloc(ORANGEFS_VFS_OP_FSYNC);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.fsync.refn = orangefs_inode->refn;

	ret = service_operation(new_op,
			"orangefs_fsync",
			get_interruptible_flag(file_inode(file)));

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_fsync got return value of %d\n",
		     ret);

	op_release(new_op);
	return ret;
}

/*
 * Change the file pointer position for an instance of an open file.
 *
 * \note If .llseek is overriden, we must acquire lock as described in
 *       Documentation/filesystems/locking.rst.
 *
 * Future upgrade could support SEEK_DATA and SEEK_HOLE but would
 * require much changes to the FS
 */
static loff_t orangefs_file_llseek(struct file *file, loff_t offset, int origin)
{
	int ret = -EINVAL;
	struct inode *inode = file_inode(file);

	if (origin == SEEK_END) {
		/*
		 * revalidate the inode's file size.
		 * NOTE: We are only interested in file size here,
		 * so we set mask accordingly.
		 */
		ret = orangefs_inode_getattr(file->f_mapping->host,
		    ORANGEFS_GETATTR_SIZE);
		if (ret == -ESTALE)
			ret = -EIO;
		if (ret) {
			gossip_debug(GOSSIP_FILE_DEBUG,
				     "%s:%s:%d calling make bad inode\n",
				     __FILE__,
				     __func__,
				     __LINE__);
			return ret;
		}
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_file_llseek: offset is %ld | origin is %d"
		     " | inode size is %lu\n",
		     (long)offset,
		     origin,
		     (unsigned long)i_size_read(inode));

	return generic_file_llseek(file, offset, origin);
}

/*
 * Support local locks (locks that only this kernel knows about)
 * if Orangefs was mounted -o local_lock.
 */
static int orangefs_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	int rc = -EINVAL;

	if (ORANGEFS_SB(file_inode(filp)->i_sb)->flags & ORANGEFS_OPT_LOCAL_LOCK) {
		if (cmd == F_GETLK) {
			rc = 0;
			posix_test_lock(filp, fl);
		} else {
			rc = posix_lock_file(filp, fl, NULL);
		}
	}

	return rc;
}

static int orangefs_flush(struct file *file, fl_owner_t id)
{
	/*
	 * This is vfs_fsync_range(file, 0, LLONG_MAX, 0) without the
	 * service_operation in orangefs_fsync.
	 *
	 * Do not send fsync to OrangeFS server on a close.  Do send fsync
	 * on an explicit fsync call.  This duplicates historical OrangeFS
	 * behavior.
	 */
	int r;

	r = filemap_write_and_wait_range(file->f_mapping, 0, LLONG_MAX);
	if (r > 0)
		return 0;
	else
		return r;
}

/** ORANGEFS implementation of VFS file operations */
const struct file_operations orangefs_file_operations = {
	.llseek		= orangefs_file_llseek,
	.read_iter	= orangefs_file_read_iter,
	.write_iter	= orangefs_file_write_iter,
	.lock		= orangefs_lock,
	.unlocked_ioctl	= orangefs_ioctl,
	.mmap		= orangefs_file_mmap,
	.open		= generic_file_open,
	.splice_read    = generic_file_splice_read,
	.splice_write   = iter_file_splice_write,
	.flush		= orangefs_flush,
	.release	= orangefs_file_release,
	.fsync		= orangefs_fsync,
};
