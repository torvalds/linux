// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem high-level write support.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include "internal.h"

/**
 * netfs_create_write_request - Create a write operation.
 * @wreq: The write request this is storing from.
 * @dest: The destination type
 * @start: Start of the region this write will modify
 * @len: Length of the modification
 * @worker: The worker function to handle the write(s)
 *
 * Allocate a write operation, set it up and add it to the list on a write
 * request.
 */
struct netfs_io_subrequest *netfs_create_write_request(struct netfs_io_request *wreq,
						       enum netfs_io_source dest,
						       loff_t start, size_t len,
						       work_func_t worker)
{
	struct netfs_io_subrequest *subreq;

	subreq = netfs_alloc_subrequest(wreq);
	if (subreq) {
		INIT_WORK(&subreq->work, worker);
		subreq->source	= dest;
		subreq->start	= start;
		subreq->len	= len;
		subreq->debug_index = wreq->subreq_counter++;

		switch (subreq->source) {
		case NETFS_UPLOAD_TO_SERVER:
			netfs_stat(&netfs_n_wh_upload);
			break;
		case NETFS_WRITE_TO_CACHE:
			netfs_stat(&netfs_n_wh_write);
			break;
		default:
			BUG();
		}

		subreq->io_iter = wreq->io_iter;
		iov_iter_advance(&subreq->io_iter, subreq->start - wreq->start);
		iov_iter_truncate(&subreq->io_iter, subreq->len);

		trace_netfs_sreq_ref(wreq->debug_id, subreq->debug_index,
				     refcount_read(&subreq->ref),
				     netfs_sreq_trace_new);
		atomic_inc(&wreq->nr_outstanding);
		list_add_tail(&subreq->rreq_link, &wreq->subrequests);
		trace_netfs_sreq(subreq, netfs_sreq_trace_prepare);
	}

	return subreq;
}
EXPORT_SYMBOL(netfs_create_write_request);

/*
 * Process a completed write request once all the component operations have
 * been completed.
 */
static void netfs_write_terminated(struct netfs_io_request *wreq, bool was_async)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_inode *ctx = netfs_inode(wreq->inode);
	size_t transferred = 0;

	_enter("R=%x[]", wreq->debug_id);

	trace_netfs_rreq(wreq, netfs_rreq_trace_write_done);

	list_for_each_entry(subreq, &wreq->subrequests, rreq_link) {
		if (subreq->error || subreq->transferred == 0)
			break;
		transferred += subreq->transferred;
		if (subreq->transferred < subreq->len)
			break;
	}
	wreq->transferred = transferred;

	list_for_each_entry(subreq, &wreq->subrequests, rreq_link) {
		if (!subreq->error)
			continue;
		switch (subreq->source) {
		case NETFS_UPLOAD_TO_SERVER:
			/* Depending on the type of failure, this may prevent
			 * writeback completion unless we're in disconnected
			 * mode.
			 */
			if (!wreq->error)
				wreq->error = subreq->error;
			break;

		case NETFS_WRITE_TO_CACHE:
			/* Failure doesn't prevent writeback completion unless
			 * we're in disconnected mode.
			 */
			if (subreq->error != -ENOBUFS)
				ctx->ops->invalidate_cache(wreq);
			break;

		default:
			WARN_ON_ONCE(1);
			if (!wreq->error)
				wreq->error = -EIO;
			return;
		}
	}

	wreq->cleanup(wreq);

	if (wreq->origin == NETFS_DIO_WRITE &&
	    wreq->mapping->nrpages) {
		pgoff_t first = wreq->start >> PAGE_SHIFT;
		pgoff_t last = (wreq->start + wreq->transferred - 1) >> PAGE_SHIFT;
		invalidate_inode_pages2_range(wreq->mapping, first, last);
	}

	if (wreq->origin == NETFS_DIO_WRITE)
		inode_dio_end(wreq->inode);

	_debug("finished");
	trace_netfs_rreq(wreq, netfs_rreq_trace_wake_ip);
	clear_bit_unlock(NETFS_RREQ_IN_PROGRESS, &wreq->flags);
	wake_up_bit(&wreq->flags, NETFS_RREQ_IN_PROGRESS);

	if (wreq->iocb) {
		wreq->iocb->ki_pos += transferred;
		if (wreq->iocb->ki_complete)
			wreq->iocb->ki_complete(
				wreq->iocb, wreq->error ? wreq->error : transferred);
	}

	netfs_clear_subrequests(wreq, was_async);
	netfs_put_request(wreq, was_async, netfs_rreq_trace_put_complete);
}

/*
 * Deal with the completion of writing the data to the cache.
 */
void netfs_write_subrequest_terminated(void *_op, ssize_t transferred_or_error,
				       bool was_async)
{
	struct netfs_io_subrequest *subreq = _op;
	struct netfs_io_request *wreq = subreq->rreq;
	unsigned int u;

	_enter("%x[%x] %zd", wreq->debug_id, subreq->debug_index, transferred_or_error);

	switch (subreq->source) {
	case NETFS_UPLOAD_TO_SERVER:
		netfs_stat(&netfs_n_wh_upload_done);
		break;
	case NETFS_WRITE_TO_CACHE:
		netfs_stat(&netfs_n_wh_write_done);
		break;
	case NETFS_INVALID_WRITE:
		break;
	default:
		BUG();
	}

	if (IS_ERR_VALUE(transferred_or_error)) {
		subreq->error = transferred_or_error;
		trace_netfs_failure(wreq, subreq, transferred_or_error,
				    netfs_fail_write);
		goto failed;
	}

	if (WARN(transferred_or_error > subreq->len - subreq->transferred,
		 "Subreq excess write: R%x[%x] %zd > %zu - %zu",
		 wreq->debug_id, subreq->debug_index,
		 transferred_or_error, subreq->len, subreq->transferred))
		transferred_or_error = subreq->len - subreq->transferred;

	subreq->error = 0;
	subreq->transferred += transferred_or_error;

	if (iov_iter_count(&subreq->io_iter) != subreq->len - subreq->transferred)
		pr_warn("R=%08x[%u] ITER POST-MISMATCH %zx != %zx-%zx %x\n",
			wreq->debug_id, subreq->debug_index,
			iov_iter_count(&subreq->io_iter), subreq->len,
			subreq->transferred, subreq->io_iter.iter_type);

	if (subreq->transferred < subreq->len)
		goto incomplete;

	__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
out:
	trace_netfs_sreq(subreq, netfs_sreq_trace_terminated);

	/* If we decrement nr_outstanding to 0, the ref belongs to us. */
	u = atomic_dec_return(&wreq->nr_outstanding);
	if (u == 0)
		netfs_write_terminated(wreq, was_async);
	else if (u == 1)
		wake_up_var(&wreq->nr_outstanding);

	netfs_put_subrequest(subreq, was_async, netfs_sreq_trace_put_terminated);
	return;

incomplete:
	if (transferred_or_error == 0) {
		if (__test_and_set_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags)) {
			subreq->error = -ENODATA;
			goto failed;
		}
	} else {
		__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
	}

	__set_bit(NETFS_SREQ_SHORT_IO, &subreq->flags);
	set_bit(NETFS_RREQ_INCOMPLETE_IO, &wreq->flags);
	goto out;

failed:
	switch (subreq->source) {
	case NETFS_WRITE_TO_CACHE:
		netfs_stat(&netfs_n_wh_write_failed);
		set_bit(NETFS_RREQ_INCOMPLETE_IO, &wreq->flags);
		break;
	case NETFS_UPLOAD_TO_SERVER:
		netfs_stat(&netfs_n_wh_upload_failed);
		set_bit(NETFS_RREQ_FAILED, &wreq->flags);
		wreq->error = subreq->error;
		break;
	default:
		break;
	}
	goto out;
}
EXPORT_SYMBOL(netfs_write_subrequest_terminated);

static void netfs_write_to_cache_op(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *wreq = subreq->rreq;
	struct netfs_cache_resources *cres = &wreq->cache_resources;

	trace_netfs_sreq(subreq, netfs_sreq_trace_submit);

	cres->ops->write(cres, subreq->start, &subreq->io_iter,
			 netfs_write_subrequest_terminated, subreq);
}

static void netfs_write_to_cache_op_worker(struct work_struct *work)
{
	struct netfs_io_subrequest *subreq =
		container_of(work, struct netfs_io_subrequest, work);

	netfs_write_to_cache_op(subreq);
}

/**
 * netfs_queue_write_request - Queue a write request for attention
 * @subreq: The write request to be queued
 *
 * Queue the specified write request for processing by a worker thread.  We
 * pass the caller's ref on the request to the worker thread.
 */
void netfs_queue_write_request(struct netfs_io_subrequest *subreq)
{
	if (!queue_work(system_unbound_wq, &subreq->work))
		netfs_put_subrequest(subreq, false, netfs_sreq_trace_put_wip);
}
EXPORT_SYMBOL(netfs_queue_write_request);

/*
 * Set up a op for writing to the cache.
 */
static void netfs_set_up_write_to_cache(struct netfs_io_request *wreq)
{
	struct netfs_cache_resources *cres = &wreq->cache_resources;
	struct netfs_io_subrequest *subreq;
	struct netfs_inode *ctx = netfs_inode(wreq->inode);
	struct fscache_cookie *cookie = netfs_i_cookie(ctx);
	loff_t start = wreq->start;
	size_t len = wreq->len;
	int ret;

	if (!fscache_cookie_enabled(cookie)) {
		clear_bit(NETFS_RREQ_WRITE_TO_CACHE, &wreq->flags);
		return;
	}

	_debug("write to cache");
	ret = fscache_begin_write_operation(cres, cookie);
	if (ret < 0)
		return;

	ret = cres->ops->prepare_write(cres, &start, &len, wreq->upper_len,
				       i_size_read(wreq->inode), true);
	if (ret < 0)
		return;

	subreq = netfs_create_write_request(wreq, NETFS_WRITE_TO_CACHE, start, len,
					    netfs_write_to_cache_op_worker);
	if (!subreq)
		return;

	netfs_write_to_cache_op(subreq);
}

/*
 * Begin the process of writing out a chunk of data.
 *
 * We are given a write request that holds a series of dirty regions and
 * (partially) covers a sequence of folios, all of which are present.  The
 * pages must have been marked as writeback as appropriate.
 *
 * We need to perform the following steps:
 *
 * (1) If encrypting, create an output buffer and encrypt each block of the
 *     data into it, otherwise the output buffer will point to the original
 *     folios.
 *
 * (2) If the data is to be cached, set up a write op for the entire output
 *     buffer to the cache, if the cache wants to accept it.
 *
 * (3) If the data is to be uploaded (ie. not merely cached):
 *
 *     (a) If the data is to be compressed, create a compression buffer and
 *         compress the data into it.
 *
 *     (b) For each destination we want to upload to, set up write ops to write
 *         to that destination.  We may need multiple writes if the data is not
 *         contiguous or the span exceeds wsize for a server.
 */
int netfs_begin_write(struct netfs_io_request *wreq, bool may_wait,
		      enum netfs_write_trace what)
{
	struct netfs_inode *ctx = netfs_inode(wreq->inode);

	_enter("R=%x %llx-%llx f=%lx",
	       wreq->debug_id, wreq->start, wreq->start + wreq->len - 1,
	       wreq->flags);

	trace_netfs_write(wreq, what);
	if (wreq->len == 0 || wreq->iter.count == 0) {
		pr_err("Zero-sized write [R=%x]\n", wreq->debug_id);
		return -EIO;
	}

	if (wreq->origin == NETFS_DIO_WRITE)
		inode_dio_begin(wreq->inode);

	wreq->io_iter = wreq->iter;

	/* ->outstanding > 0 carries a ref */
	netfs_get_request(wreq, netfs_rreq_trace_get_for_outstanding);
	atomic_set(&wreq->nr_outstanding, 1);

	/* Start the encryption/compression going.  We can do that in the
	 * background whilst we generate a list of write ops that we want to
	 * perform.
	 */
	// TODO: Encrypt or compress the region as appropriate

	/* We need to write all of the region to the cache */
	if (test_bit(NETFS_RREQ_WRITE_TO_CACHE, &wreq->flags))
		netfs_set_up_write_to_cache(wreq);

	/* However, we don't necessarily write all of the region to the server.
	 * Caching of reads is being managed this way also.
	 */
	if (test_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags))
		ctx->ops->create_write_requests(wreq, wreq->start, wreq->len);

	if (atomic_dec_and_test(&wreq->nr_outstanding))
		netfs_write_terminated(wreq, false);

	if (!may_wait)
		return -EIOCBQUEUED;

	wait_on_bit(&wreq->flags, NETFS_RREQ_IN_PROGRESS,
		    TASK_UNINTERRUPTIBLE);
	return wreq->error;
}

/*
 * Begin a write operation for writing through the pagecache.
 */
struct netfs_io_request *netfs_begin_writethrough(struct kiocb *iocb, size_t len)
{
	struct netfs_io_request *wreq;
	struct file *file = iocb->ki_filp;

	wreq = netfs_alloc_request(file->f_mapping, file, iocb->ki_pos, len,
				   NETFS_WRITETHROUGH);
	if (IS_ERR(wreq))
		return wreq;

	trace_netfs_write(wreq, netfs_write_trace_writethrough);

	__set_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags);
	iov_iter_xarray(&wreq->iter, ITER_SOURCE, &wreq->mapping->i_pages, wreq->start, 0);
	wreq->io_iter = wreq->iter;

	/* ->outstanding > 0 carries a ref */
	netfs_get_request(wreq, netfs_rreq_trace_get_for_outstanding);
	atomic_set(&wreq->nr_outstanding, 1);
	return wreq;
}

static void netfs_submit_writethrough(struct netfs_io_request *wreq, bool final)
{
	struct netfs_inode *ictx = netfs_inode(wreq->inode);
	unsigned long long start;
	size_t len;

	if (!test_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags))
		return;

	start = wreq->start + wreq->submitted;
	len = wreq->iter.count - wreq->submitted;
	if (!final) {
		len /= wreq->wsize; /* Round to number of maximum packets */
		len *= wreq->wsize;
	}

	ictx->ops->create_write_requests(wreq, start, len);
	wreq->submitted += len;
}

/*
 * Advance the state of the write operation used when writing through the
 * pagecache.  Data has been copied into the pagecache that we need to append
 * to the request.  If we've added more than wsize then we need to create a new
 * subrequest.
 */
int netfs_advance_writethrough(struct netfs_io_request *wreq, size_t copied, bool to_page_end)
{
	_enter("ic=%zu sb=%zu ws=%u cp=%zu tp=%u",
	       wreq->iter.count, wreq->submitted, wreq->wsize, copied, to_page_end);

	wreq->iter.count += copied;
	wreq->io_iter.count += copied;
	if (to_page_end && wreq->io_iter.count - wreq->submitted >= wreq->wsize)
		netfs_submit_writethrough(wreq, false);

	return wreq->error;
}

/*
 * End a write operation used when writing through the pagecache.
 */
int netfs_end_writethrough(struct netfs_io_request *wreq, struct kiocb *iocb)
{
	int ret = -EIOCBQUEUED;

	_enter("ic=%zu sb=%zu ws=%u",
	       wreq->iter.count, wreq->submitted, wreq->wsize);

	if (wreq->submitted < wreq->io_iter.count)
		netfs_submit_writethrough(wreq, true);

	if (atomic_dec_and_test(&wreq->nr_outstanding))
		netfs_write_terminated(wreq, false);

	if (is_sync_kiocb(iocb)) {
		wait_on_bit(&wreq->flags, NETFS_RREQ_IN_PROGRESS,
			    TASK_UNINTERRUPTIBLE);
		ret = wreq->error;
	}

	netfs_put_request(wreq, false, netfs_rreq_trace_put_return);
	return ret;
}
