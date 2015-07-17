/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS file operations.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include <linux/fs.h>
#include <linux/pagemap.h>

#define wake_up_daemon_for_return(op)			\
do {							\
	spin_lock(&op->lock);                           \
	op->io_completed = 1;                           \
	spin_unlock(&op->lock);                         \
	wake_up_interruptible(&op->io_completion_waitq);\
} while (0)

/*
 * Copy to client-core's address space from the buffers specified
 * by the iovec upto total_size bytes.
 * NOTE: the iovector can either contain addresses which
 *       can futher be kernel-space or user-space addresses.
 *       or it can pointers to struct page's
 */
static int precopy_buffers(struct pvfs2_bufmap *bufmap,
			   int buffer_index,
			   const struct iovec *vec,
			   unsigned long nr_segs,
			   size_t total_size,
			   int from_user)
{
	int ret = 0;

	/*
	 * copy data from application/kernel by pulling it out
	 * of the iovec.
	 */
	/* Are we copying from User Virtual Addresses? */
	if (from_user)
		ret = pvfs_bufmap_copy_iovec_from_user(
			bufmap,
			buffer_index,
			vec,
			nr_segs,
			total_size);
	/* Are we copying from Kernel Virtual Addresses? */
	else
		ret = pvfs_bufmap_copy_iovec_from_kernel(
			bufmap,
			buffer_index,
			vec,
			nr_segs,
			total_size);
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
static int postcopy_buffers(struct pvfs2_bufmap *bufmap,
			    int buffer_index,
			    const struct iovec *vec,
			    int nr_segs,
			    size_t total_size,
			    int to_user)
{
	int ret = 0;

	/*
	 * copy data to application/kernel by pushing it out to
	 * the iovec. NOTE; target buffers can be addresses or
	 * struct page pointers.
	 */
	if (total_size) {
		/* Are we copying to User Virtual Addresses? */
		if (to_user)
			ret = pvfs_bufmap_copy_to_user_iovec(
				bufmap,
				buffer_index,
				vec,
				nr_segs,
				total_size);
		/* Are we copying to Kern Virtual Addresses? */
		else
			ret = pvfs_bufmap_copy_to_kernel_iovec(
				bufmap,
				buffer_index,
				vec,
				nr_segs,
				total_size);
		if (ret < 0)
			gossip_err("%s: Failed to copy-out buffers.  Please make sure that the pvfs2-client is running (%ld)\n",
				__func__,
				(long)ret);
	}
	return ret;
}

/*
 * Post and wait for the I/O upcall to finish
 */
static ssize_t wait_for_direct_io(enum PVFS_io_type type, struct inode *inode,
		loff_t *offset, struct iovec *vec, unsigned long nr_segs,
		size_t total_size, loff_t readahead_size, int to_user)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_khandle *handle = &pvfs2_inode->refn.khandle;
	struct pvfs2_bufmap *bufmap = NULL;
	struct pvfs2_kernel_op_s *new_op = NULL;
	int buffer_index = -1;
	ssize_t ret;

	new_op = op_alloc(PVFS2_VFS_OP_FILE_IO);
	if (!new_op) {
		ret = -ENOMEM;
		goto out;
	}
	/* synchronous I/O */
	new_op->upcall.req.io.async_vfs_io = PVFS_VFS_SYNC_IO;
	new_op->upcall.req.io.readahead_size = readahead_size;
	new_op->upcall.req.io.io_type = type;
	new_op->upcall.req.io.refn = pvfs2_inode->refn;

populate_shared_memory:
	/* get a shared buffer index */
	ret = pvfs_bufmap_get(&bufmap, &buffer_index);
	if (ret < 0) {
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: pvfs_bufmap_get failure (%ld)\n",
			     __func__, (long)ret);
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
		     "%s(%pU): copy_to_user %d nr_segs %lu, offset: %llu total_size: %zd\n",
		     __func__,
		     handle,
		     to_user,
		     nr_segs,
		     llu(*offset),
		     total_size);
	/*
	 * Stage 1: copy the buffers into client-core's address space
	 * precopy_buffers only pertains to writes.
	 */
	if (type == PVFS_IO_WRITE) {
		ret = precopy_buffers(bufmap,
				      buffer_index,
				      vec,
				      nr_segs,
				      total_size,
				      to_user);
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
				type == PVFS_IO_WRITE ?
					"file_write" :
					"file_read",
				get_interruptible_flag(inode));

	/*
	 * If service_operation() returns -EAGAIN #and# the operation was
	 * purged from pvfs2_request_list or htable_ops_in_progress, then
	 * we know that the client was restarted, causing the shared memory
	 * area to be wiped clean.  To restart a  write operation in this
	 * case, we must re-copy the data from the user's iovec to a NEW
	 * shared memory location. To restart a read operation, we must get
	 * a new shared memory location.
	 */
	if (ret == -EAGAIN && op_state_purged(new_op)) {
		pvfs_bufmap_put(bufmap, buffer_index);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s:going to repopulate_shared_memory.\n",
			     __func__);
		goto populate_shared_memory;
	}

	if (ret < 0) {
		handle_io_error(); /* defined in pvfs2-kernel.h */
		/*
		   don't write an error to syslog on signaled operation
		   termination unless we've got debugging turned on, as
		   this can happen regularly (i.e. ctrl-c)
		 */
		if (ret == -EINTR)
			gossip_debug(GOSSIP_FILE_DEBUG,
				     "%s: returning error %ld\n", __func__,
				     (long)ret);
		else
			gossip_err("%s: error in %s handle %pU, returning %zd\n",
				__func__,
				type == PVFS_IO_READ ?
					"read from" : "write to",
				handle, ret);
		goto out;
	}

	/*
	 * Stage 3: Post copy buffers from client-core's address space
	 * postcopy_buffers only pertains to reads.
	 */
	if (type == PVFS_IO_READ) {
		ret = postcopy_buffers(bufmap,
				       buffer_index,
				       vec,
				       nr_segs,
				       new_op->downcall.resp.io.amt_complete,
				       to_user);
		if (ret < 0) {
			/*
			 * put error codes in downcall so that handle_io_error()
			 * preserves it properly
			 */
			new_op->downcall.status = ret;
			handle_io_error();
			goto out;
		}
	}
	gossip_debug(GOSSIP_FILE_DEBUG,
	    "%s(%pU): Amount written as returned by the sys-io call:%d\n",
	    __func__,
	    handle,
	    (int)new_op->downcall.resp.io.amt_complete);

	ret = new_op->downcall.resp.io.amt_complete;

	/*
	   tell the device file owner waiting on I/O that this read has
	   completed and it can return now.  in this exact case, on
	   wakeup the daemon will free the op, so we *cannot* touch it
	   after this.
	 */
	wake_up_daemon_for_return(new_op);
	new_op = NULL;

out:
	if (buffer_index >= 0) {
		pvfs_bufmap_put(bufmap, buffer_index);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): PUT buffer_index %d\n",
			     __func__, handle, buffer_index);
		buffer_index = -1;
	}
	if (new_op) {
		op_release(new_op);
		new_op = NULL;
	}
	return ret;
}

/*
 * The reason we need to do this is to be able to support readv and writev
 * that are larger than (pvfs_bufmap_size_query()) Default is
 * PVFS2_BUFMAP_DEFAULT_DESC_SIZE MB. What that means is that we will
 * create a new io vec descriptor for those memory addresses that
 * go beyond the limit. Return value for this routine is negative in case
 * of errors and 0 in case of success.
 *
 * Further, the new_nr_segs pointer is updated to hold the new value
 * of number of iovecs, the new_vec pointer is updated to hold the pointer
 * to the new split iovec, and the size array is an array of integers holding
 * the number of iovecs that straddle pvfs_bufmap_size_query().
 * The max_new_nr_segs value is computed by the caller and returned.
 * (It will be (count of all iov_len/ block_size) + 1).
 */
static int split_iovecs(unsigned long max_new_nr_segs,		/* IN */
			unsigned long nr_segs,			/* IN */
			const struct iovec *original_iovec,	/* IN */
			unsigned long *new_nr_segs,		/* OUT */
			struct iovec **new_vec,			/* OUT */
			unsigned long *seg_count,		/* OUT */
			unsigned long **seg_array)		/* OUT */
{
	unsigned long seg;
	unsigned long count = 0;
	unsigned long begin_seg;
	unsigned long tmpnew_nr_segs = 0;
	struct iovec *new_iovec = NULL;
	struct iovec *orig_iovec;
	unsigned long *sizes = NULL;
	unsigned long sizes_count = 0;

	if (nr_segs <= 0 ||
	    original_iovec == NULL ||
	    new_nr_segs == NULL ||
	    new_vec == NULL ||
	    seg_count == NULL ||
	    seg_array == NULL ||
	    max_new_nr_segs <= 0) {
		gossip_err("Invalid parameters to split_iovecs\n");
		return -EINVAL;
	}
	*new_nr_segs = 0;
	*new_vec = NULL;
	*seg_count = 0;
	*seg_array = NULL;
	/* copy the passed in iovec descriptor to a temp structure */
	orig_iovec = kmalloc_array(nr_segs,
				   sizeof(*orig_iovec),
				   PVFS2_BUFMAP_GFP_FLAGS);
	if (orig_iovec == NULL) {
		gossip_err(
		    "split_iovecs: Could not allocate memory for %lu bytes!\n",
		    (unsigned long)(nr_segs * sizeof(*orig_iovec)));
		return -ENOMEM;
	}
	new_iovec = kcalloc(max_new_nr_segs,
			    sizeof(*new_iovec),
			    PVFS2_BUFMAP_GFP_FLAGS);
	if (new_iovec == NULL) {
		kfree(orig_iovec);
		gossip_err(
		    "split_iovecs: Could not allocate memory for %lu bytes!\n",
		    (unsigned long)(max_new_nr_segs * sizeof(*new_iovec)));
		return -ENOMEM;
	}
	sizes = kcalloc(max_new_nr_segs,
			sizeof(*sizes),
			PVFS2_BUFMAP_GFP_FLAGS);
	if (sizes == NULL) {
		kfree(new_iovec);
		kfree(orig_iovec);
		gossip_err(
		    "split_iovecs: Could not allocate memory for %lu bytes!\n",
		    (unsigned long)(max_new_nr_segs * sizeof(*sizes)));
		return -ENOMEM;
	}
	/* copy the passed in iovec to a temp structure */
	memcpy(orig_iovec, original_iovec, nr_segs * sizeof(*orig_iovec));
	begin_seg = 0;
repeat:
	for (seg = begin_seg; seg < nr_segs; seg++) {
		if (tmpnew_nr_segs >= max_new_nr_segs ||
		    sizes_count >= max_new_nr_segs) {
			kfree(sizes);
			kfree(orig_iovec);
			kfree(new_iovec);
			gossip_err
			    ("split_iovecs: exceeded the index limit (%lu)\n",
			    tmpnew_nr_segs);
			return -EINVAL;
		}
		if (count + orig_iovec[seg].iov_len <
		    pvfs_bufmap_size_query()) {
			count += orig_iovec[seg].iov_len;
			memcpy(&new_iovec[tmpnew_nr_segs],
			       &orig_iovec[seg],
			       sizeof(*new_iovec));
			tmpnew_nr_segs++;
			sizes[sizes_count]++;
		} else {
			new_iovec[tmpnew_nr_segs].iov_base =
			    orig_iovec[seg].iov_base;
			new_iovec[tmpnew_nr_segs].iov_len =
			    (pvfs_bufmap_size_query() - count);
			tmpnew_nr_segs++;
			sizes[sizes_count]++;
			sizes_count++;
			begin_seg = seg;
			orig_iovec[seg].iov_base +=
			    (pvfs_bufmap_size_query() - count);
			orig_iovec[seg].iov_len -=
			    (pvfs_bufmap_size_query() - count);
			count = 0;
			break;
		}
	}
	if (seg != nr_segs)
		goto repeat;
	else
		sizes_count++;

	*new_nr_segs = tmpnew_nr_segs;
	/* new_iovec is freed by the caller */
	*new_vec = new_iovec;
	*seg_count = sizes_count;
	/* seg_array is also freed by the caller */
	*seg_array = sizes;
	kfree(orig_iovec);
	return 0;
}

static long bound_max_iovecs(const struct iovec *curr, unsigned long nr_segs,
			     ssize_t *total_count)
{
	unsigned long i;
	long max_nr_iovecs;
	ssize_t total;
	ssize_t count;

	total = 0;
	count = 0;
	max_nr_iovecs = 0;
	for (i = 0; i < nr_segs; i++) {
		const struct iovec *iv = &curr[i];

		count += iv->iov_len;
		if (unlikely((ssize_t) (count | iv->iov_len) < 0))
			return -EINVAL;
		if (total + iv->iov_len < pvfs_bufmap_size_query()) {
			total += iv->iov_len;
			max_nr_iovecs++;
		} else {
			total =
			    (total + iv->iov_len - pvfs_bufmap_size_query());
			max_nr_iovecs += (total / pvfs_bufmap_size_query() + 2);
		}
	}
	*total_count = count;
	return max_nr_iovecs;
}

/*
 * Common entry point for read/write/readv/writev
 * This function will dispatch it to either the direct I/O
 * or buffered I/O path depending on the mount options and/or
 * augmented/extended metadata attached to the file.
 * Note: File extended attributes override any mount options.
 */
static ssize_t do_readv_writev(enum PVFS_io_type type, struct file *file,
		loff_t *offset, const struct iovec *iov, unsigned long nr_segs)
{
	struct inode *inode = file->f_mapping->host;
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_khandle *handle = &pvfs2_inode->refn.khandle;
	ssize_t ret;
	ssize_t total_count;
	unsigned int to_free;
	size_t count;
	unsigned long seg;
	unsigned long new_nr_segs = 0;
	unsigned long max_new_nr_segs = 0;
	unsigned long seg_count = 0;
	unsigned long *seg_array = NULL;
	struct iovec *iovecptr = NULL;
	struct iovec *ptr = NULL;

	total_count = 0;
	ret = -EINVAL;
	count = 0;
	to_free = 0;

	/* Compute total and max number of segments after split */
	max_new_nr_segs = bound_max_iovecs(iov, nr_segs, &count);
	if (max_new_nr_segs < 0) {
		gossip_lerr("%s: could not bound iovec %lu\n",
			    __func__,
			    max_new_nr_segs);
		goto out;
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		"%s-BEGIN(%pU): count(%d) after estimate_max_iovecs.\n",
		__func__,
		handle,
		(int)count);

	if (type == PVFS_IO_WRITE) {
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

	/*
	 * if the total size of data transfer requested is greater than
	 * the kernel-set blocksize of PVFS2, then we split the iovecs
	 * such that no iovec description straddles a block size limit
	 */

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s: pvfs_bufmap_size:%d\n",
		     __func__,
		     pvfs_bufmap_size_query());

	if (count > pvfs_bufmap_size_query()) {
		/*
		 * Split up the given iovec description such that
		 * no iovec descriptor straddles over the block-size limitation.
		 * This makes us our job easier to stage the I/O.
		 * In addition, this function will also compute an array
		 * with seg_count entries that will store the number of
		 * segments that straddle the block-size boundaries.
		 */
		ret = split_iovecs(max_new_nr_segs,	/* IN */
				   nr_segs,		/* IN */
				   iov,			/* IN */
				   &new_nr_segs,	/* OUT */
				   &iovecptr,		/* OUT */
				   &seg_count,		/* OUT */
				   &seg_array);		/* OUT */
		if (ret < 0) {
			gossip_err("%s: Failed to split iovecs to satisfy larger than blocksize readv/writev request %zd\n",
				__func__,
				ret);
			goto out;
		}
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: Splitting iovecs from %lu to %lu"
			     " [max_new %lu]\n",
			     __func__,
			     nr_segs,
			     new_nr_segs,
			     max_new_nr_segs);
		/* We must free seg_array and iovecptr */
		to_free = 1;
	} else {
		new_nr_segs = nr_segs;
		/* use the given iovec description */
		iovecptr = (struct iovec *)iov;
		/* There is only 1 element in the seg_array */
		seg_count = 1;
		/* and its value is the number of segments passed in */
		seg_array = &nr_segs;
		/* We dont have to free up anything */
		to_free = 0;
	}
	ptr = iovecptr;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU) %zd@%llu\n",
		     __func__,
		     handle,
		     count,
		     llu(*offset));
	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): new_nr_segs: %lu, seg_count: %lu\n",
		     __func__,
		     handle,
		     new_nr_segs, seg_count);

/* PVFS2_KERNEL_DEBUG is a CFLAGS define. */
#ifdef PVFS2_KERNEL_DEBUG
	for (seg = 0; seg < new_nr_segs; seg++)
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: %d) %p to %p [%d bytes]\n",
			     __func__,
			     (int)seg + 1,
			     iovecptr[seg].iov_base,
			     iovecptr[seg].iov_base + iovecptr[seg].iov_len,
			     (int)iovecptr[seg].iov_len);
	for (seg = 0; seg < seg_count; seg++)
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: %zd) %lu\n",
			     __func__,
			     seg + 1,
			     seg_array[seg]);
#endif
	seg = 0;
	while (total_count < count) {
		size_t each_count;
		size_t amt_complete;

		/* how much to transfer in this loop iteration */
		each_count =
		   (((count - total_count) > pvfs_bufmap_size_query()) ?
			pvfs_bufmap_size_query() :
			(count - total_count));

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

		ret = wait_for_direct_io(type, inode, offset, ptr,
				seg_array[seg], each_count, 0, 1);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): return from wait_for_io:%d\n",
			     __func__,
			     handle,
			     (int)ret);

		if (ret < 0)
			goto out;

		/* advance the iovec pointer */
		ptr += seg_array[seg];
		seg++;
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

	if (total_count > 0)
		ret = total_count;
out:
	if (to_free) {
		kfree(iovecptr);
		kfree(seg_array);
	}
	if (ret > 0) {
		if (type == PVFS_IO_READ) {
			file_accessed(file);
		} else {
			SetMtimeFlag(pvfs2_inode);
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
ssize_t pvfs2_inode_read(struct inode *inode,
			 char __user *buf,
			 size_t count,
			 loff_t *offset,
			 loff_t readahead_size)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	size_t bufmap_size;
	struct iovec vec;
	ssize_t ret = -EINVAL;

	g_pvfs2_stats.reads++;

	vec.iov_base = buf;
	vec.iov_len = count;

	bufmap_size = pvfs_bufmap_size_query();
	if (count > bufmap_size) {
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s: count is too large (%zd/%zd)!\n",
			     __func__, count, bufmap_size);
		return -EINVAL;
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU) %zd@%llu\n",
		     __func__,
		     &pvfs2_inode->refn.khandle,
		     count,
		     llu(*offset));

	ret = wait_for_direct_io(PVFS_IO_READ, inode, offset, &vec, 1,
			count, readahead_size, 0);
	if (ret > 0)
		*offset += ret;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): Value(%zd) returned.\n",
		     __func__,
		     &pvfs2_inode->refn.khandle,
		     ret);

	return ret;
}

static ssize_t pvfs2_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	loff_t pos = *(&iocb->ki_pos);
	ssize_t rc = 0;
	unsigned long nr_segs = iter->nr_segs;

	BUG_ON(iocb->private);

	gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_read_iter\n");

	g_pvfs2_stats.reads++;

	rc = do_readv_writev(PVFS_IO_READ,
			     file,
			     &pos,
			     iter->iov,
			     nr_segs);
	iocb->ki_pos = pos;

	return rc;
}

static ssize_t pvfs2_file_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	loff_t pos = *(&iocb->ki_pos);
	unsigned long nr_segs = iter->nr_segs;
	ssize_t rc;

	BUG_ON(iocb->private);

	gossip_debug(GOSSIP_FILE_DEBUG, "pvfs2_file_write_iter\n");

	mutex_lock(&file->f_mapping->host->i_mutex);

	/* Make sure generic_write_checks sees an up to date inode size. */
	if (file->f_flags & O_APPEND) {
		rc = pvfs2_inode_getattr(file->f_mapping->host,
					 PVFS_ATTR_SYS_SIZE);
		if (rc) {
			gossip_err("%s: pvfs2_inode_getattr failed, rc:%zd:.\n",
				   __func__, rc);
			goto out;
		}
	}

	if (file->f_pos > i_size_read(file->f_mapping->host))
		pvfs2_i_size_write(file->f_mapping->host, file->f_pos);

	rc = generic_write_checks(iocb, iter);

	if (rc <= 0) {
		gossip_err("%s: generic_write_checks failed, rc:%zd:.\n",
			   __func__, rc);
		goto out;
	}

	rc = do_readv_writev(PVFS_IO_WRITE,
			     file,
			     &pos,
			     iter->iov,
			     nr_segs);
	if (rc < 0) {
		gossip_err("%s: do_readv_writev failed, rc:%zd:.\n",
			   __func__, rc);
		goto out;
	}

	iocb->ki_pos = pos;
	g_pvfs2_stats.writes++;

out:

	mutex_unlock(&file->f_mapping->host->i_mutex);
	return rc;
}

/*
 * Perform a miscellaneous operation on a file.
 */
long pvfs2_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;
	__u64 val = 0;
	unsigned long uval;

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "pvfs2_ioctl: called with cmd %d\n",
		     cmd);

	/*
	 * we understand some general ioctls on files, such as the immutable
	 * and append flags
	 */
	if (cmd == FS_IOC_GETFLAGS) {
		val = 0;
		ret = pvfs2_xattr_get_default(file->f_path.dentry,
					      "user.pvfs2.meta_hint",
					      &val,
					      sizeof(val),
					      0);
		if (ret < 0 && ret != -ENODATA)
			return ret;
		else if (ret == -ENODATA)
			val = 0;
		uval = val;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "pvfs2_ioctl: FS_IOC_GETFLAGS: %llu\n",
			     (unsigned long long)uval);
		return put_user(uval, (int __user *)arg);
	} else if (cmd == FS_IOC_SETFLAGS) {
		ret = 0;
		if (get_user(uval, (int __user *)arg))
			return -EFAULT;
		/*
		 * PVFS_MIRROR_FL is set internally when the mirroring mode
		 * is turned on for a file. The user is not allowed to turn
		 * on this bit, but the bit is present if the user first gets
		 * the flags and then updates the flags with some new
		 * settings. So, we ignore it in the following edit. bligon.
		 */
		if ((uval & ~PVFS_MIRROR_FL) &
		    (~(FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NOATIME_FL))) {
			gossip_err("pvfs2_ioctl: the FS_IOC_SETFLAGS only supports setting one of FS_IMMUTABLE_FL|FS_APPEND_FL|FS_NOATIME_FL\n");
			return -EINVAL;
		}
		val = uval;
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "pvfs2_ioctl: FS_IOC_SETFLAGS: %llu\n",
			     (unsigned long long)val);
		ret = pvfs2_xattr_set_default(file->f_path.dentry,
					      "user.pvfs2.meta_hint",
					      &val,
					      sizeof(val),
					      0,
					      0);
	}

	return ret;
}

/*
 * Memory map a region of a file.
 */
static int pvfs2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	gossip_debug(GOSSIP_FILE_DEBUG,
		     "pvfs2_file_mmap: called on %s\n",
		     (file ?
			(char *)file->f_path.dentry->d_name.name :
			(char *)"Unknown"));

	/* set the sequential readahead hint */
	vma->vm_flags |= VM_SEQ_READ;
	vma->vm_flags &= ~VM_RAND_READ;
	return generic_file_mmap(file, vma);
}

#define mapping_nrpages(idata) ((idata)->nrpages)

/*
 * Called to notify the module that there are no more references to
 * this file (i.e. no processes have it open).
 *
 * \note Not called when each file is closed.
 */
int pvfs2_file_release(struct inode *inode, struct file *file)
{
	gossip_debug(GOSSIP_FILE_DEBUG,
		     "pvfs2_file_release: called on %s\n",
		     file->f_path.dentry->d_name.name);

	pvfs2_flush_inode(inode);

	/*
	   remove all associated inode pages from the page cache and mmap
	   readahead cache (if any); this forces an expensive refresh of
	   data for the next caller of mmap (or 'get_block' accesses)
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
int pvfs2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret = -EINVAL;
	struct pvfs2_inode_s *pvfs2_inode =
		PVFS2_I(file->f_path.dentry->d_inode);
	struct pvfs2_kernel_op_s *new_op = NULL;

	/* required call */
	filemap_write_and_wait_range(file->f_mapping, start, end);

	new_op = op_alloc(PVFS2_VFS_OP_FSYNC);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.fsync.refn = pvfs2_inode->refn;

	ret = service_operation(new_op,
			"pvfs2_fsync",
			get_interruptible_flag(file->f_path.dentry->d_inode));

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "pvfs2_fsync got return value of %d\n",
		     ret);

	op_release(new_op);

	pvfs2_flush_inode(file->f_path.dentry->d_inode);
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
loff_t pvfs2_file_llseek(struct file *file, loff_t offset, int origin)
{
	int ret = -EINVAL;
	struct inode *inode = file->f_path.dentry->d_inode;

	if (!inode) {
		gossip_err("pvfs2_file_llseek: invalid inode (NULL)\n");
		return ret;
	}

	if (origin == PVFS2_SEEK_END) {
		/*
		 * revalidate the inode's file size.
		 * NOTE: We are only interested in file size here,
		 * so we set mask accordingly.
		 */
		ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_SIZE);
		if (ret) {
			gossip_debug(GOSSIP_FILE_DEBUG,
				     "%s:%s:%d calling make bad inode\n",
				     __FILE__,
				     __func__,
				     __LINE__);
			pvfs2_make_bad_inode(inode);
			return ret;
		}
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "pvfs2_file_llseek: offset is %ld | origin is %d | "
		     "inode size is %lu\n",
		     (long)offset,
		     origin,
		     (unsigned long)file->f_path.dentry->d_inode->i_size);

	return generic_file_llseek(file, offset, origin);
}

/*
 * Support local locks (locks that only this kernel knows about)
 * if Orangefs was mounted -o local_lock.
 */
int pvfs2_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	int rc = -ENOLCK;

	if (PVFS2_SB(filp->f_inode->i_sb)->flags & PVFS2_OPT_LOCAL_LOCK) {
		if (cmd == F_GETLK) {
			rc = 0;
			posix_test_lock(filp, fl);
		} else {
			rc = posix_lock_file(filp, fl, NULL);
		}
	}

	return rc;
}

/** PVFS2 implementation of VFS file operations */
const struct file_operations pvfs2_file_operations = {
	.llseek		= pvfs2_file_llseek,
	.read_iter	= pvfs2_file_read_iter,
	.write_iter	= pvfs2_file_write_iter,
	.lock		= pvfs2_lock,
	.unlocked_ioctl	= pvfs2_ioctl,
	.mmap		= pvfs2_file_mmap,
	.open		= generic_file_open,
	.release	= pvfs2_file_release,
	.fsync		= pvfs2_fsync,
};
