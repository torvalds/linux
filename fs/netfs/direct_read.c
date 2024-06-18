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
	bool async = !is_sync_kiocb(iocb);

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

	if (async)
		rreq->iocb = iocb;

	ret = netfs_begin_read(rreq, is_sync_kiocb(iocb));
	if (ret < 0)
		goto out; /* May be -EIOCBQUEUED */
	if (!async) {
		// TODO: Copy from bounce buffer
		iocb->ki_pos += rreq->transferred;
		ret = rreq->transferred;
	}

out:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);
	if (ret > 0)
		orig_count -= ret;
	if (ret != -EIOCBQUEUED)
		iov_iter_revert(iter, orig_count - iov_iter_count(iter));
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
