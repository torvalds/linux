/*
 * (C) 2001 Clemson University and The University of Chicago
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

/*
 * Copy to client-core's address space from the buffers specified
 * by the iovec upto total_size bytes.
 * NOTE: the iovector can either contain addresses which
 *       can futher be kernel-space or user-space addresses.
 *       or it can pointers to struct page's
 */
static int precopy_buffers(int buffer_index,
			   struct iov_iter *iter,
			   size_t total_size)
{
	int ret = 0;
	/*
	 * copy data from application/kernel by pulling it out
	 * of the iovec.
	 */


	if (total_size) {
		ret = orangefs_bufmap_copy_from_iovec(iter,
						      buffer_index,
						      total_size);
		if (ret < 0)
		gossip_err("%s: Failed to copy-in buffers. Please make sure that the pvfs2-client is running. %ld\n",
			   __func__,
			   (long)ret);
	}

	if (ret < 0)
		gossip_err("%s: Failed to copy-in buffers. Please make sure that the pvfs2-client is running. %ld\n",
			__func__,
			(long)ret);
	return ret;
}

/*
 * Copy from client-core's address space to the buffers specified
 * by the iovec upto total_size bytes.
 * NOTE: the iovector can either contain addresses which
 *       can futher be kernel-space or user-space addresses.
 *       or it can pointers to struct page's
 */
static int postcopy_buffers(int buffer_index,
			    struct iov_iter *iter,
			    size_t total_size)
{
	int ret = 0;
	/*
	 * copy data to application/kernel by pushing it out to
	 * the iovec. NOTE; target buffers can be addresses or
	 * struct page pointers.
	 */
	if (total_size) {
		ret = orangefs_bufmap_copy_to_iovec(iter,
						    buffer_index,
						    total_size);
		if (ret < 0)
			gossip_err("%s: Failed to copy-out buffers. Please make sure that the pvfs2-client is running (%ld)\n",
				__func__,
				(long)ret);
	}
	return ret;
}

/*
 * Post and wait for the I/O upcall to finish
 */
static ssize_t wait_for_direct_io(enum ORANGEFS_io_type type, struct inode *inode,
		loff_t *offset, struct iov_iter *iter,
		size_t total_size, loff_t readahead_size)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_khandle *handle = &orangefs_inode->refn.khandle;
	struct orangefs_kernel_op_s *new_op = NULL;
	struct iov_iter saved = *iter;
	int buffer_index = -1;
	ssize_t ret;

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

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): offset: %llu total_size: %zd\n",
		     __func__,
		     handle,
		     llu(*offset),
		     total_size);
	/*
	 * Stage 1: copy the buffers into client-core's address space
	 * precopy_buffers only pertains to writes.
	 */
	if (type == ORANGEFS_IO_WRITE) {
		ret = precopy_buffers(buffer_index,
				      iter,
				      total_size);
		if (ret < 0)
			goto out;
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
		buffer_index = -1;
		if (type == ORANGEFS_IO_WRITE)
			*iter = saved;
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
	 * postcopy_buffers only pertains to reads.
	 */
	if (type == ORANGEFS_IO_READ) {
		ret = postcopy_buffers(buffer_index,
				       iter,
				       new_op->downcall.resp.io.amt_complete);
		if (ret < 0)
			goto out;
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
		orangefs_bufmap_put(buffer_index);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): PUT buffer_index %d\n",
			     __func__, handle, buffer_index);
		buffer_index = -1;
	}
	op_release(new_op);
	return ret;
}

/*
 * Common entry point for read/write/readv/writev
 * This function will dispatch it to either the direct I/O
 * or buffered I/O path depending on the mount options and/or
 * augmented/extended metadata attached to the file.
 * Note: File extended attributes override any mount options.
 */
static ssize_t do_readv_writev(enum ORANGEFS_io_type type, struct file *file,
		loff_t *offset, struct iov_iter *iter)
{
	struct inode *inode = file->f_mapping->host;
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_khandle *handle = &orangefs_inode->refn.khandle;
	size_t count = iov_iter_count(iter);
	ssize_t total_count = 0;
	ssize_t ret = -EINVAL;

	gossip_debug(GOSSIP_FILE_DEBUG,
		"%s-BEGIN(%pU): count(%d) after estimate_max_iovecs.\n",
		__func__,
		handle,
		(int)count);

	if (type == ORANGEFS_IO_WRITE) {
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): proceeding with offset : %llu, "
			     "size %d\n",
			     __func__,
			     handle,
			     llu(*offset),
			     (int)count);
	}

	if (count == 0) {
		ret = 0;
		goto out;
	}

	while (iov_iter_count(iter)) {
		size_t each_count = iov_iter_count(iter);
		size_t amt_complete;

		/* how much to transfer in this loop iteration */
		if (each_count > orangefs_bufmap_size_query())
			each_count = orangefs_bufmap_size_query();

		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): size of each_count(%d)\n",
			     __func__,
			     handle,
			     (int)each_count);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): BEFORE wait_for_io: offset is %d\n",
			     __func__,
			     handle,
			     (int)*offset);

		ret = wait_for_direct_io(type, inode, offset, iter,
				each_count, 0);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): return from wait_for_io:%d\n",
			     __func__,
			     handle,
			     (int)ret);

		if (ret < 0)
			goto out;

		*offset += ret;
		total_count += ret;
		amt_complete = ret;

		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): AFTER wait_for_io: offset is %d\n",
			     __func__,
			     handle,
			     (int)*offset);

		/*
		 * if we got a short I/O operations,
		 * fall out and return what we got so far
		 */
		if (amt_complete < each_count)
			break;
	} /*end while */

out:
	if (total_count > 0)
		ret = total_count;
	if (ret > 0) {
		if (type == ORANGEFS_IO_READ) {
			file_accessed(file);
		} else {
			SetMtimeFlag(orangefs_inode);
			inode->i_mtime = CURRENT_TIME;
			mark_inode_dirty_sync(inode);
		}
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): Value(%d) returned.\n",
		     __func__,
		     handle,
		     (int)ret);

	return ret;
}

/*
 * Read data from a specified offset in a file (referenced by inode).
 * Data may be placed either in a user or kernel buffer.
 */
ssize_t orangefs_inode_read(struct inode *inode,
			    struct iov_iter *iter,
			    loff_t *offset,
			    loff_t readahead_size)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	size_t count = iov_iter_count(iter);
	size_t bufmap_size;
	ssize_t ret = -EINVAL;

	g_orangefs_stats.reads++;

	bufmap_size = orangefs_bufmap_size_query();
	if (count > bufmap_size) {
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: count is too large (%zd/%zd)!\n",
			     __func__, count, bufmap_size);
		return -EINVAL;
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU) %zd@%llu\n",
		     __func__,
		     &orangefs_inode->refn.khandle,
		     count,
		     llu(*offset));

	ret = wait_for_direct_io(ORANGEFS_IO_READ, inode, offset, iter,
			count, readahead_size);
	if (ret > 0)
		*offset += ret;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): Value(%zd) returned.\n",
		     __func__,
		     &orangefs_inode->refn.khandle,
		     ret);

	return ret;
}

static ssize_t orangefs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	loff_t pos = *(&iocb->ki_pos);
	ssize_t rc = 0;

	BUG_ON(iocb->private);

	gossip_debug(GOSSIP_FILE_DEBUG, "orangefs_file_read_iter\n");

	g_orangefs_stats.reads++;

	rc = do_readv_writev(ORANGEFS_IO_READ, file, &pos, iter);
	iocb->ki_pos = pos;

	return rc;
}

static ssize_t orangefs_file_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	loff_t pos;
	ssize_t rc;

	BUG_ON(iocb->private);

	gossip_debug(GOSSIP_FILE_DEBUG, "orangefs_file_write_iter\n");

	inode_lock(file->f_mapping->host);

	/* Make sure generic_write_checks sees an up to date inode size. */
	if (file->f_flags & O_APPEND) {
		rc = orangefs_inode_getattr(file->f_mapping->host, 0, 1);
		if (rc == -ESTALE)
			rc = -EIO;
		if (rc) {
			gossip_err("%s: orangefs_inode_getattr failed, "
			    "rc:%zd:.\n", __func__, rc);
			goto out;
		}
	}

	if (file->f_pos > i_size_read(file->f_mapping->host))
		orangefs_i_size_write(file->f_mapping->host, file->f_pos);

	rc = generic_write_checks(iocb, iter);

	if (rc <= 0) {
		gossip_err("%s: generic_write_checks failed, rc:%zd:.\n",
			   __func__, rc);
		goto out;
	}

	/*
	 * if we are appending, generic_write_checks would have updated
	 * pos to the end of the file, so we will wait till now to set
	 * pos...
	 */
	pos = *(&iocb->ki_pos);

	rc = do_readv_writev(ORANGEFS_IO_WRITE,
			     file,
			     &pos,
			     iter);
	if (rc < 0) {
		gossip_err("%s: do_readv_writev failed, rc:%zd:.\n",
			   __func__, rc);
		goto out;
	}

	iocb->ki_pos = pos;
	g_orangefs_stats.writes++;

out:

	inode_unlock(file->f_mapping->host);
	return rc;
}

/*
 * Perform a miscellaneous operation on a file.
 */
static long orangefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
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
		val = 0;
		ret = orangefs_inode_getxattr(file_inode(file),
					      ORANGEFS_XATTR_NAME_DEFAULT_PREFIX,
					      "user.pvfs2.meta_hint",
					      &val, sizeof(val));
		if (ret < 0 && ret != -ENODATA)
			return ret;
		else if (ret == -ENODATA)
			val = 0;
		uval = val;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "orangefs_ioctl: FS_IOC_GETFLAGS: %llu\n",
			     (unsigned long long)uval);
		return put_user(uval, (int __user *)arg);
	} else if (cmd == FS_IOC_SETFLAGS) {
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
		val = uval;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "orangefs_ioctl: FS_IOC_SETFLAGS: %llu\n",
			     (unsigned long long)val);
		ret = orangefs_inode_setxattr(file_inode(file),
					      ORANGEFS_XATTR_NAME_DEFAULT_PREFIX,
					      "user.pvfs2.meta_hint",
					      &val, sizeof(val), 0);
	}

	return ret;
}

/*
 * Memory map a region of a file.
 */
static int orangefs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_file_mmap: called on %s\n",
		     (file ?
			(char *)file->f_path.dentry->d_name.name :
			(char *)"Unknown"));

	/* set the sequential readahead hint */
	vma->vm_flags |= VM_SEQ_READ;
	vma->vm_flags &= ~VM_RAND_READ;

	/* Use readonly mmap since we cannot support writable maps. */
	return generic_file_readonly_mmap(file, vma);
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
		     "orangefs_file_release: called on %s\n",
		     file->f_path.dentry->d_name.name);

	orangefs_flush_inode(inode);

	/*
	 * remove all associated inode pages from the page cache and mmap
	 * readahead cache (if any); this forces an expensive refresh of
	 * data for the next caller of mmap (or 'get_block' accesses)
	 */
	if (file->f_path.dentry->d_inode &&
	    file->f_path.dentry->d_inode->i_mapping &&
	    mapping_nrpages(&file->f_path.dentry->d_inode->i_data))
		truncate_inode_pages(file->f_path.dentry->d_inode->i_mapping,
				     0);
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
	int ret = -EINVAL;
	struct orangefs_inode_s *orangefs_inode =
		ORANGEFS_I(file->f_path.dentry->d_inode);
	struct orangefs_kernel_op_s *new_op = NULL;

	/* required call */
	filemap_write_and_wait_range(file->f_mapping, start, end);

	new_op = op_alloc(ORANGEFS_VFS_OP_FSYNC);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.fsync.refn = orangefs_inode->refn;

	ret = service_operation(new_op,
			"orangefs_fsync",
			get_interruptible_flag(file->f_path.dentry->d_inode));

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "orangefs_fsync got return value of %d\n",
		     ret);

	op_release(new_op);

	orangefs_flush_inode(file->f_path.dentry->d_inode);
	return ret;
}

/*
 * Change the file pointer position for an instance of an open file.
 *
 * \note If .llseek is overriden, we must acquire lock as described in
 *       Documentation/filesystems/Locking.
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
		ret = orangefs_inode_getattr(file->f_mapping->host, 0, 1);
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

	if (ORANGEFS_SB(filp->f_inode->i_sb)->flags & ORANGEFS_OPT_LOCAL_LOCK) {
		if (cmd == F_GETLK) {
			rc = 0;
			posix_test_lock(filp, fl);
		} else {
			rc = posix_lock_file(filp, fl, NULL);
		}
	}

	return rc;
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
	.release	= orangefs_file_release,
	.fsync		= orangefs_fsync,
};
