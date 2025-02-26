// SPDX-License-Identifier: GPL-2.0-or-later
/* Unbuffered and direct write support.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/uio.h>
#include "internal.h"

static void netfs_cleanup_dio_write(struct netfs_io_request *wreq)
{
	struct inode *inode = wreq->inode;
	unsigned long long end = wreq->start + wreq->transferred;

	if (!wreq->error &&
	    i_size_read(inode) < end) {
		if (wreq->netfs_ops->update_i_size)
			wreq->netfs_ops->update_i_size(inode, end);
		else
			i_size_write(inode, end);
	}
}

/*
 * Perform an unbuffered write where we may have to do an RMW operation on an
 * encrypted file.  This can also be used for direct I/O writes.
 */
ssize_t netfs_unbuffered_write_iter_locked(struct kiocb *iocb, struct iov_iter *iter,
						  struct netfs_group *netfs_group)
{
	struct netfs_io_request *wreq;
	unsigned long long start = iocb->ki_pos;
	unsigned long long end = start + iov_iter_count(iter);
	ssize_t ret, n;
	size_t len = iov_iter_count(iter);
	bool async = !is_sync_kiocb(iocb);

	_enter("");

	/* We're going to need a bounce buffer if what we transmit is going to
	 * be different in some way to the source buffer, e.g. because it gets
	 * encrypted/compressed or because it needs expanding to a block size.
	 */
	// TODO

	_debug("uw %llx-%llx", start, end);

	wreq = netfs_create_write_req(iocb->ki_filp->f_mapping, iocb->ki_filp, start,
				      iocb->ki_flags & IOCB_DIRECT ?
				      NETFS_DIO_WRITE : NETFS_UNBUFFERED_WRITE);
	if (IS_ERR(wreq))
		return PTR_ERR(wreq);

	wreq->io_streams[0].avail = true;
	trace_netfs_write(wreq, (iocb->ki_flags & IOCB_DIRECT ?
				 netfs_write_trace_dio_write :
				 netfs_write_trace_unbuffered_write));

	{
		/* If this is an async op and we're not using a bounce buffer,
		 * we have to save the source buffer as the iterator is only
		 * good until we return.  In such a case, extract an iterator
		 * to represent as much of the the output buffer as we can
		 * manage.  Note that the extraction might not be able to
		 * allocate a sufficiently large bvec array and may shorten the
		 * request.
		 */
		if (user_backed_iter(iter)) {
			n = netfs_extract_user_iter(iter, len, &wreq->buffer.iter, 0);
			if (n < 0) {
				ret = n;
				goto out;
			}
			wreq->direct_bv = (struct bio_vec *)wreq->buffer.iter.bvec;
			wreq->direct_bv_count = n;
			wreq->direct_bv_unpin = iov_iter_extract_will_pin(iter);
		} else {
			/* If this is a kernel-generated async DIO request,
			 * assume that any resources the iterator points to
			 * (eg. a bio_vec array) will persist till the end of
			 * the op.
			 */
			wreq->buffer.iter = *iter;
		}
	}

	__set_bit(NETFS_RREQ_USE_IO_ITER, &wreq->flags);

	/* Copy the data into the bounce buffer and encrypt it. */
	// TODO

	/* Dispatch the write. */
	__set_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags);
	if (async)
		wreq->iocb = iocb;
	wreq->len = iov_iter_count(&wreq->buffer.iter);
	wreq->cleanup = netfs_cleanup_dio_write;
	ret = netfs_unbuffered_write(wreq, is_sync_kiocb(iocb), wreq->len);
	if (ret < 0) {
		_debug("begin = %zd", ret);
		goto out;
	}

	if (!async) {
		trace_netfs_rreq(wreq, netfs_rreq_trace_wait_ip);
		wait_on_bit(&wreq->flags, NETFS_RREQ_IN_PROGRESS,
			    TASK_UNINTERRUPTIBLE);
		ret = wreq->error;
		if (ret == 0) {
			ret = wreq->transferred;
			iocb->ki_pos += ret;
		}
	} else {
		ret = -EIOCBQUEUED;
	}

out:
	netfs_put_request(wreq, false, netfs_rreq_trace_put_return);
	return ret;
}
EXPORT_SYMBOL(netfs_unbuffered_write_iter_locked);

/**
 * netfs_unbuffered_write_iter - Unbuffered write to a file
 * @iocb: IO state structure
 * @from: iov_iter with data to write
 *
 * Do an unbuffered write to a file, writing the data directly to the server
 * and not lodging the data in the pagecache.
 *
 * Return:
 * * Negative error code if no data has been written at all of
 *   vfs_fsync_range() failed for a synchronous write
 * * Number of bytes written, even for truncated writes
 */
ssize_t netfs_unbuffered_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct netfs_inode *ictx = netfs_inode(inode);
	ssize_t ret;
	loff_t pos = iocb->ki_pos;
	unsigned long long end = pos + iov_iter_count(from) - 1;

	_enter("%llx,%zx,%llx", pos, iov_iter_count(from), i_size_read(inode));

	if (!iov_iter_count(from))
		return 0;

	trace_netfs_write_iter(iocb, from);
	netfs_stat(&netfs_n_wh_dio_write);

	ret = netfs_start_io_direct(inode);
	if (ret < 0)
		return ret;
	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out;
	ret = file_remove_privs(file);
	if (ret < 0)
		goto out;
	ret = file_update_time(file);
	if (ret < 0)
		goto out;
	if (iocb->ki_flags & IOCB_NOWAIT) {
		/* We could block if there are any pages in the range. */
		ret = -EAGAIN;
		if (filemap_range_has_page(mapping, pos, end))
			if (filemap_invalidate_inode(inode, true, pos, end))
				goto out;
	} else {
		ret = filemap_write_and_wait_range(mapping, pos, end);
		if (ret < 0)
			goto out;
	}

	/*
	 * After a write we want buffered reads to be sure to go to disk to get
	 * the new data.  We invalidate clean cached page from the region we're
	 * about to write.  We do this *before* the write so that we can return
	 * without clobbering -EIOCBQUEUED from ->direct_IO().
	 */
	ret = filemap_invalidate_inode(inode, true, pos, end);
	if (ret < 0)
		goto out;
	end = iocb->ki_pos + iov_iter_count(from);
	if (end > ictx->zero_point)
		ictx->zero_point = end;

	fscache_invalidate(netfs_i_cookie(ictx), NULL, i_size_read(inode),
			   FSCACHE_INVAL_DIO_WRITE);
	ret = netfs_unbuffered_write_iter_locked(iocb, from, NULL);
out:
	netfs_end_io_direct(inode);
	return ret;
}
EXPORT_SYMBOL(netfs_unbuffered_write_iter);
