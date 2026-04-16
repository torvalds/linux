// SPDX-License-Identifier: GPL-2.0-or-later
/* Unbuffered and direct write support.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/uio.h>
#include "internal.h"

/*
 * Perform the cleanup rituals after an unbuffered write is complete.
 */
static void netfs_unbuffered_write_done(struct netfs_io_request *wreq)
{
	struct netfs_inode *ictx = netfs_inode(wreq->inode);

	_enter("R=%x", wreq->debug_id);

	/* Okay, declare that all I/O is complete. */
	trace_netfs_rreq(wreq, netfs_rreq_trace_write_done);

	if (!wreq->error)
		netfs_update_i_size(ictx, &ictx->inode, wreq->start, wreq->transferred);

	if (wreq->origin == NETFS_DIO_WRITE &&
	    wreq->mapping->nrpages) {
		/* mmap may have got underfoot and we may now have folios
		 * locally covering the region we just wrote.  Attempt to
		 * discard the folios, but leave in place any modified locally.
		 * ->write_iter() is prevented from interfering by the DIO
		 * counter.
		 */
		pgoff_t first = wreq->start >> PAGE_SHIFT;
		pgoff_t last = (wreq->start + wreq->transferred - 1) >> PAGE_SHIFT;

		invalidate_inode_pages2_range(wreq->mapping, first, last);
	}

	if (wreq->origin == NETFS_DIO_WRITE)
		inode_dio_end(wreq->inode);

	_debug("finished");
	netfs_wake_rreq_flag(wreq, NETFS_RREQ_IN_PROGRESS, netfs_rreq_trace_wake_ip);
	/* As we cleared NETFS_RREQ_IN_PROGRESS, we acquired its ref. */

	if (wreq->iocb) {
		size_t written = umin(wreq->transferred, wreq->len);

		wreq->iocb->ki_pos += written;
		if (wreq->iocb->ki_complete) {
			trace_netfs_rreq(wreq, netfs_rreq_trace_ki_complete);
			wreq->iocb->ki_complete(wreq->iocb, wreq->error ?: written);
		}
		wreq->iocb = VFS_PTR_POISON;
	}

	netfs_clear_subrequests(wreq);
}

/*
 * Collect the subrequest results of unbuffered write subrequests.
 */
static void netfs_unbuffered_write_collect(struct netfs_io_request *wreq,
					   struct netfs_io_stream *stream,
					   struct netfs_io_subrequest *subreq)
{
	trace_netfs_collect_sreq(wreq, subreq);

	spin_lock(&wreq->lock);
	list_del_init(&subreq->rreq_link);
	spin_unlock(&wreq->lock);

	wreq->transferred += subreq->transferred;
	iov_iter_advance(&wreq->buffer.iter, subreq->transferred);

	stream->collected_to = subreq->start + subreq->transferred;
	wreq->collected_to = stream->collected_to;
	netfs_put_subrequest(subreq, netfs_sreq_trace_put_done);

	trace_netfs_collect_stream(wreq, stream);
	trace_netfs_collect_state(wreq, wreq->collected_to, 0);
}

/*
 * Write data to the server without going through the pagecache and without
 * writing it to the local cache.  We dispatch the subrequests serially and
 * wait for each to complete before dispatching the next, lest we leave a gap
 * in the data written due to a failure such as ENOSPC.  We could, however
 * attempt to do preparation such as content encryption for the next subreq
 * whilst the current is in progress.
 */
static int netfs_unbuffered_write(struct netfs_io_request *wreq)
{
	struct netfs_io_subrequest *subreq = NULL;
	struct netfs_io_stream *stream = &wreq->io_streams[0];
	int ret;

	_enter("%llx", wreq->len);

	if (wreq->origin == NETFS_DIO_WRITE)
		inode_dio_begin(wreq->inode);

	stream->collected_to = wreq->start;

	for (;;) {
		bool retry = false;

		if (!subreq) {
			netfs_prepare_write(wreq, stream, wreq->start + wreq->transferred);
			subreq = stream->construct;
			stream->construct = NULL;
			stream->front = NULL;
		}

		/* Check if (re-)preparation failed. */
		if (unlikely(test_bit(NETFS_SREQ_FAILED, &subreq->flags))) {
			netfs_write_subrequest_terminated(subreq, subreq->error);
			wreq->error = subreq->error;
			break;
		}

		iov_iter_truncate(&subreq->io_iter, wreq->len - wreq->transferred);
		if (!iov_iter_count(&subreq->io_iter))
			break;

		subreq->len = netfs_limit_iter(&subreq->io_iter, 0,
					       stream->sreq_max_len,
					       stream->sreq_max_segs);
		iov_iter_truncate(&subreq->io_iter, subreq->len);
		stream->submit_extendable_to = subreq->len;

		trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
		stream->issue_write(subreq);

		/* Async, need to wait. */
		netfs_wait_for_in_progress_stream(wreq, stream);

		if (test_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags)) {
			retry = true;
		} else if (test_bit(NETFS_SREQ_FAILED, &subreq->flags)) {
			ret = subreq->error;
			wreq->error = ret;
			netfs_see_subrequest(subreq, netfs_sreq_trace_see_failed);
			subreq = NULL;
			break;
		}
		ret = 0;

		if (!retry) {
			netfs_unbuffered_write_collect(wreq, stream, subreq);
			subreq = NULL;
			if (wreq->transferred >= wreq->len)
				break;
			if (!wreq->iocb && signal_pending(current)) {
				ret = wreq->transferred ? -EINTR : -ERESTARTSYS;
				trace_netfs_rreq(wreq, netfs_rreq_trace_intr);
				break;
			}
			continue;
		}

		/* We need to retry the last subrequest, so first reset the
		 * iterator, taking into account what, if anything, we managed
		 * to transfer.
		 */
		subreq->error = -EAGAIN;
		trace_netfs_sreq(subreq, netfs_sreq_trace_retry);
		if (subreq->transferred > 0)
			iov_iter_advance(&wreq->buffer.iter, subreq->transferred);

		if (stream->source == NETFS_UPLOAD_TO_SERVER &&
		    wreq->netfs_ops->retry_request)
			wreq->netfs_ops->retry_request(wreq, stream);

		__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
		__clear_bit(NETFS_SREQ_BOUNDARY, &subreq->flags);
		__clear_bit(NETFS_SREQ_FAILED, &subreq->flags);
		subreq->io_iter		= wreq->buffer.iter;
		subreq->start		= wreq->start + wreq->transferred;
		subreq->len		= wreq->len   - wreq->transferred;
		subreq->transferred	= 0;
		subreq->retry_count	+= 1;
		stream->sreq_max_len	= UINT_MAX;
		stream->sreq_max_segs	= INT_MAX;

		netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
		stream->prepare_write(subreq);

		__set_bit(NETFS_SREQ_IN_PROGRESS, &subreq->flags);
		netfs_stat(&netfs_n_wh_retry_write_subreq);
	}

	netfs_unbuffered_write_done(wreq);
	_leave(" = %d", ret);
	return ret;
}

static void netfs_unbuffered_write_async(struct work_struct *work)
{
	struct netfs_io_request *wreq = container_of(work, struct netfs_io_request, work);

	netfs_unbuffered_write(wreq);
	netfs_put_request(wreq, netfs_rreq_trace_put_complete);
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
				goto error_put;
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

		wreq->len = iov_iter_count(&wreq->buffer.iter);
	}

	__set_bit(NETFS_RREQ_USE_IO_ITER, &wreq->flags);

	/* Copy the data into the bounce buffer and encrypt it. */
	// TODO

	/* Dispatch the write. */
	__set_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags);

	if (async) {
		INIT_WORK(&wreq->work, netfs_unbuffered_write_async);
		wreq->iocb = iocb;
		queue_work(system_dfl_wq, &wreq->work);
		ret = -EIOCBQUEUED;
	} else {
		ret = netfs_unbuffered_write(wreq);
		if (ret < 0) {
			_debug("begin = %zd", ret);
		} else {
			iocb->ki_pos += wreq->transferred;
			ret = wreq->transferred ?: wreq->error;
		}

		netfs_put_request(wreq, netfs_rreq_trace_put_complete);
	}

	netfs_put_request(wreq, netfs_rreq_trace_put_return);
	return ret;

error_put:
	netfs_put_failed_request(wreq);
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
