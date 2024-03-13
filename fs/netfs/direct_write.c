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
	unsigned long long end = wreq->start + wreq->len;

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
static ssize_t netfs_unbuffered_write_iter_locked(struct kiocb *iocb, struct iov_iter *iter,
						  struct netfs_group *netfs_group)
{
	struct netfs_io_request *wreq;
	unsigned long long start = iocb->ki_pos;
	unsigned long long end = start + iov_iter_count(iter);
	ssize_t ret, n;
	bool async = !is_sync_kiocb(iocb);

	_enter("");

	/* We're going to need a bounce buffer if what we transmit is going to
	 * be different in some way to the source buffer, e.g. because it gets
	 * encrypted/compressed or because it needs expanding to a block size.
	 */
	// TODO

	_debug("uw %llx-%llx", start, end);

	wreq = netfs_alloc_request(iocb->ki_filp->f_mapping, iocb->ki_filp,
				   start, end - start,
				   iocb->ki_flags & IOCB_DIRECT ?
				   NETFS_DIO_WRITE : NETFS_UNBUFFERED_WRITE);
	if (IS_ERR(wreq))
		return PTR_ERR(wreq);

	{
		/* If this is an async op and we're not using a bounce buffer,
		 * we have to save the source buffer as the iterator is only
		 * good until we return.  In such a case, extract an iterator
		 * to represent as much of the the output buffer as we can
		 * manage.  Note that the extraction might not be able to
		 * allocate a sufficiently large bvec array and may shorten the
		 * request.
		 */
		if (async || user_backed_iter(iter)) {
			n = netfs_extract_user_iter(iter, wreq->len, &wreq->iter, 0);
			if (n < 0) {
				ret = n;
				goto out;
			}
			wreq->direct_bv = (struct bio_vec *)wreq->iter.bvec;
			wreq->direct_bv_count = n;
			wreq->direct_bv_unpin = iov_iter_extract_will_pin(iter);
			wreq->len = iov_iter_count(&wreq->iter);
		} else {
			wreq->iter = *iter;
		}

		wreq->io_iter = wreq->iter;
	}

	/* Copy the data into the bounce buffer and encrypt it. */
	// TODO

	/* Dispatch the write. */
	__set_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags);
	if (async)
		wreq->iocb = iocb;
	wreq->cleanup = netfs_cleanup_dio_write;
	ret = netfs_begin_write(wreq, is_sync_kiocb(iocb),
				iocb->ki_flags & IOCB_DIRECT ?
				netfs_write_trace_dio_write :
				netfs_write_trace_unbuffered_write);
	if (ret < 0) {
		_debug("begin = %zd", ret);
		goto out;
	}

	if (!async) {
		trace_netfs_rreq(wreq, netfs_rreq_trace_wait_ip);
		wait_on_bit(&wreq->flags, NETFS_RREQ_IN_PROGRESS,
			    TASK_UNINTERRUPTIBLE);

		ret = wreq->error;
		_debug("waited = %zd", ret);
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
	struct inode *inode = file->f_mapping->host;
	struct netfs_inode *ictx = netfs_inode(inode);
	unsigned long long end;
	ssize_t ret;

	_enter("%llx,%zx,%llx", iocb->ki_pos, iov_iter_count(from), i_size_read(inode));

	trace_netfs_write_iter(iocb, from);
	netfs_stat(&netfs_n_rh_dio_write);

	ret = netfs_start_io_direct(inode);
	if (ret < 0)
		return ret;
	ret = generic_write_checks(iocb, from);
	if (ret < 0)
		goto out;
	ret = file_remove_privs(file);
	if (ret < 0)
		goto out;
	ret = file_update_time(file);
	if (ret < 0)
		goto out;
	ret = kiocb_invalidate_pages(iocb, iov_iter_count(from));
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
