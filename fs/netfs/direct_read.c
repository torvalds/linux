// SPDX-License-Identifier: GPL-2.0-or-later
/* Direct I/O support.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/sched/mm.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/netfs.h>
#include "internal.h"

static void netfs_prepare_dio_read_iterator(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	size_t rsize;

	rsize = umin(subreq->len, rreq->io_streams[0].sreq_max_len);
	subreq->len = rsize;

	if (unlikely(rreq->io_streams[0].sreq_max_segs)) {
		size_t limit = netfs_limit_iter(&rreq->iter, 0, rsize,
						rreq->io_streams[0].sreq_max_segs);

		if (limit < rsize) {
			subreq->len = limit;
			trace_netfs_sreq(subreq, netfs_sreq_trace_limited);
		}
	}

	trace_netfs_sreq(subreq, netfs_sreq_trace_prepare);

	subreq->io_iter	= rreq->iter;
	iov_iter_truncate(&subreq->io_iter, subreq->len);
	iov_iter_advance(&rreq->iter, subreq->len);
}

/*
 * Perform a read to a buffer from the server, slicing up the region to be read
 * according to the network rsize.
 */
static int netfs_dispatch_unbuffered_reads(struct netfs_io_request *rreq)
{
	unsigned long long start = rreq->start;
	ssize_t size = rreq->len;
	int ret = 0;

	atomic_set(&rreq->nr_outstanding, 1);

	do {
		struct netfs_io_subrequest *subreq;
		ssize_t slice;

		subreq = netfs_alloc_subrequest(rreq);
		if (!subreq) {
			ret = -ENOMEM;
			break;
		}

		subreq->source	= NETFS_DOWNLOAD_FROM_SERVER;
		subreq->start	= start;
		subreq->len	= size;

		atomic_inc(&rreq->nr_outstanding);
		spin_lock_bh(&rreq->lock);
		list_add_tail(&subreq->rreq_link, &rreq->subrequests);
		subreq->prev_donated = rreq->prev_donated;
		rreq->prev_donated = 0;
		trace_netfs_sreq(subreq, netfs_sreq_trace_added);
		spin_unlock_bh(&rreq->lock);

		netfs_stat(&netfs_n_rh_download);
		if (rreq->netfs_ops->prepare_read) {
			ret = rreq->netfs_ops->prepare_read(subreq);
			if (ret < 0) {
				atomic_dec(&rreq->nr_outstanding);
				netfs_put_subrequest(subreq, false, netfs_sreq_trace_put_cancel);
				break;
			}
		}

		netfs_prepare_dio_read_iterator(subreq);
		slice = subreq->len;
		rreq->netfs_ops->issue_read(subreq);

		size -= slice;
		start += slice;
		rreq->submitted += slice;

		if (test_bit(NETFS_RREQ_BLOCKED, &rreq->flags) &&
		    test_bit(NETFS_RREQ_NONBLOCK, &rreq->flags))
			break;
		cond_resched();
	} while (size > 0);

	if (atomic_dec_and_test(&rreq->nr_outstanding))
		netfs_rreq_terminated(rreq, false);
	return ret;
}

/*
 * Perform a read to an application buffer, bypassing the pagecache and the
 * local disk cache.
 */
static int netfs_unbuffered_read(struct netfs_io_request *rreq, bool sync)
{
	int ret;

	_enter("R=%x %llx-%llx",
	       rreq->debug_id, rreq->start, rreq->start + rreq->len - 1);

	if (rreq->len == 0) {
		pr_err("Zero-sized read [R=%x]\n", rreq->debug_id);
		return -EIO;
	}

	// TODO: Use bounce buffer if requested

	inode_dio_begin(rreq->inode);

	ret = netfs_dispatch_unbuffered_reads(rreq);

	if (!rreq->submitted) {
		netfs_put_request(rreq, false, netfs_rreq_trace_put_no_submit);
		inode_dio_end(rreq->inode);
		ret = 0;
		goto out;
	}

	if (sync) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_wait_ip);
		wait_on_bit(&rreq->flags, NETFS_RREQ_IN_PROGRESS,
			    TASK_UNINTERRUPTIBLE);

		ret = rreq->error;
		if (ret == 0 && rreq->submitted < rreq->len &&
		    rreq->origin != NETFS_DIO_READ) {
			trace_netfs_failure(rreq, NULL, ret, netfs_fail_short_read);
			ret = -EIO;
		}
	} else {
		ret = -EIOCBQUEUED;
	}

out:
	_leave(" = %d", ret);
	return ret;
}

/**
 * netfs_unbuffered_read_iter_locked - Perform an unbuffered or direct I/O read
 * @iocb: The I/O control descriptor describing the read
 * @iter: The output buffer (also specifies read length)
 *
 * Perform an unbuffered I/O or direct I/O from the file in @iocb to the
 * output buffer.  No use is made of the pagecache.
 *
 * The caller must hold any appropriate locks.
 */
ssize_t netfs_unbuffered_read_iter_locked(struct kiocb *iocb, struct iov_iter *iter)
{
	struct netfs_io_request *rreq;
	ssize_t ret;
	size_t orig_count = iov_iter_count(iter);
	bool sync = is_sync_kiocb(iocb);

	_enter("");

	if (!orig_count)
		return 0; /* Don't update atime */

	ret = kiocb_write_and_wait(iocb, orig_count);
	if (ret < 0)
		return ret;
	file_accessed(iocb->ki_filp);

	rreq = netfs_alloc_request(iocb->ki_filp->f_mapping, iocb->ki_filp,
				   iocb->ki_pos, orig_count,
				   NETFS_DIO_READ);
	if (IS_ERR(rreq))
		return PTR_ERR(rreq);

	netfs_stat(&netfs_n_rh_dio_read);
	trace_netfs_read(rreq, rreq->start, rreq->len, netfs_read_trace_dio_read);

	/* If this is an async op, we have to keep track of the destination
	 * buffer for ourselves as the caller's iterator will be trashed when
	 * we return.
	 *
	 * In such a case, extract an iterator to represent as much of the the
	 * output buffer as we can manage.  Note that the extraction might not
	 * be able to allocate a sufficiently large bvec array and may shorten
	 * the request.
	 */
	if (user_backed_iter(iter)) {
		ret = netfs_extract_user_iter(iter, rreq->len, &rreq->iter, 0);
		if (ret < 0)
			goto out;
		rreq->direct_bv = (struct bio_vec *)rreq->iter.bvec;
		rreq->direct_bv_count = ret;
		rreq->direct_bv_unpin = iov_iter_extract_will_pin(iter);
		rreq->len = iov_iter_count(&rreq->iter);
	} else {
		rreq->iter = *iter;
		rreq->len = orig_count;
		rreq->direct_bv_unpin = false;
		iov_iter_advance(iter, orig_count);
	}

	// TODO: Set up bounce buffer if needed

	if (!sync)
		rreq->iocb = iocb;

	ret = netfs_unbuffered_read(rreq, sync);
	if (ret < 0)
		goto out; /* May be -EIOCBQUEUED */
	if (sync) {
		// TODO: Copy from bounce buffer
		iocb->ki_pos += rreq->transferred;
		ret = rreq->transferred;
	}

out:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);
	if (ret > 0)
		orig_count -= ret;
	return ret;
}
EXPORT_SYMBOL(netfs_unbuffered_read_iter_locked);

/**
 * netfs_unbuffered_read_iter - Perform an unbuffered or direct I/O read
 * @iocb: The I/O control descriptor describing the read
 * @iter: The output buffer (also specifies read length)
 *
 * Perform an unbuffered I/O or direct I/O from the file in @iocb to the
 * output buffer.  No use is made of the pagecache.
 */
ssize_t netfs_unbuffered_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t ret;

	if (!iter->count)
		return 0; /* Don't update atime */

	ret = netfs_start_io_direct(inode);
	if (ret == 0) {
		ret = netfs_unbuffered_read_iter_locked(iocb, iter);
		netfs_end_io_direct(inode);
	}
	return ret;
}
EXPORT_SYMBOL(netfs_unbuffered_read_iter);
