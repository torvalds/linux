// SPDX-License-Identifier: GPL-2.0-or-later
/* Network filesystem high-level read support.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
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
#define CREATE_TRACE_POINTS
#include <trace/events/netfs.h>

MODULE_DESCRIPTION("Network fs support");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned netfs_debug;
module_param_named(debug, netfs_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(netfs_debug, "Netfs support debugging mask");

static void netfs_rreq_work(struct work_struct *);
static void __netfs_put_subrequest(struct netfs_read_subrequest *, bool);

static void netfs_put_subrequest(struct netfs_read_subrequest *subreq,
				 bool was_async)
{
	if (refcount_dec_and_test(&subreq->usage))
		__netfs_put_subrequest(subreq, was_async);
}

static struct netfs_read_request *netfs_alloc_read_request(
	const struct netfs_read_request_ops *ops, void *netfs_priv,
	struct file *file)
{
	static atomic_t debug_ids;
	struct netfs_read_request *rreq;

	rreq = kzalloc(sizeof(struct netfs_read_request), GFP_KERNEL);
	if (rreq) {
		rreq->netfs_ops	= ops;
		rreq->netfs_priv = netfs_priv;
		rreq->inode	= file_inode(file);
		rreq->i_size	= i_size_read(rreq->inode);
		rreq->debug_id	= atomic_inc_return(&debug_ids);
		INIT_LIST_HEAD(&rreq->subrequests);
		INIT_WORK(&rreq->work, netfs_rreq_work);
		refcount_set(&rreq->usage, 1);
		__set_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags);
		ops->init_rreq(rreq, file);
		netfs_stat(&netfs_n_rh_rreq);
	}

	return rreq;
}

static void netfs_get_read_request(struct netfs_read_request *rreq)
{
	refcount_inc(&rreq->usage);
}

static void netfs_rreq_clear_subreqs(struct netfs_read_request *rreq,
				     bool was_async)
{
	struct netfs_read_subrequest *subreq;

	while (!list_empty(&rreq->subrequests)) {
		subreq = list_first_entry(&rreq->subrequests,
					  struct netfs_read_subrequest, rreq_link);
		list_del(&subreq->rreq_link);
		netfs_put_subrequest(subreq, was_async);
	}
}

static void netfs_free_read_request(struct work_struct *work)
{
	struct netfs_read_request *rreq =
		container_of(work, struct netfs_read_request, work);
	netfs_rreq_clear_subreqs(rreq, false);
	if (rreq->netfs_priv)
		rreq->netfs_ops->cleanup(rreq->mapping, rreq->netfs_priv);
	trace_netfs_rreq(rreq, netfs_rreq_trace_free);
	kfree(rreq);
	netfs_stat_d(&netfs_n_rh_rreq);
}

static void netfs_put_read_request(struct netfs_read_request *rreq, bool was_async)
{
	if (refcount_dec_and_test(&rreq->usage)) {
		if (was_async) {
			rreq->work.func = netfs_free_read_request;
			if (!queue_work(system_unbound_wq, &rreq->work))
				BUG();
		} else {
			netfs_free_read_request(&rreq->work);
		}
	}
}

/*
 * Allocate and partially initialise an I/O request structure.
 */
static struct netfs_read_subrequest *netfs_alloc_subrequest(
	struct netfs_read_request *rreq)
{
	struct netfs_read_subrequest *subreq;

	subreq = kzalloc(sizeof(struct netfs_read_subrequest), GFP_KERNEL);
	if (subreq) {
		INIT_LIST_HEAD(&subreq->rreq_link);
		refcount_set(&subreq->usage, 2);
		subreq->rreq = rreq;
		netfs_get_read_request(rreq);
		netfs_stat(&netfs_n_rh_sreq);
	}

	return subreq;
}

static void netfs_get_read_subrequest(struct netfs_read_subrequest *subreq)
{
	refcount_inc(&subreq->usage);
}

static void __netfs_put_subrequest(struct netfs_read_subrequest *subreq,
				   bool was_async)
{
	struct netfs_read_request *rreq = subreq->rreq;

	trace_netfs_sreq(subreq, netfs_sreq_trace_free);
	kfree(subreq);
	netfs_stat_d(&netfs_n_rh_sreq);
	netfs_put_read_request(rreq, was_async);
}

/*
 * Clear the unread part of an I/O request.
 */
static void netfs_clear_unread(struct netfs_read_subrequest *subreq)
{
	struct iov_iter iter;

	iov_iter_xarray(&iter, WRITE, &subreq->rreq->mapping->i_pages,
			subreq->start + subreq->transferred,
			subreq->len   - subreq->transferred);
	iov_iter_zero(iov_iter_count(&iter), &iter);
}

/*
 * Fill a subrequest region with zeroes.
 */
static void netfs_fill_with_zeroes(struct netfs_read_request *rreq,
				   struct netfs_read_subrequest *subreq)
{
	netfs_stat(&netfs_n_rh_zero);
	__set_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags);
	netfs_subreq_terminated(subreq, 0, false);
}

/*
 * Ask the netfs to issue a read request to the server for us.
 *
 * The netfs is expected to read from subreq->pos + subreq->transferred to
 * subreq->pos + subreq->len - 1.  It may not backtrack and write data into the
 * buffer prior to the transferred point as it might clobber dirty data
 * obtained from the cache.
 *
 * Alternatively, the netfs is allowed to indicate one of two things:
 *
 * - NETFS_SREQ_SHORT_READ: A short read - it will get called again to try and
 *   make progress.
 *
 * - NETFS_SREQ_CLEAR_TAIL: A short read - the rest of the buffer will be
 *   cleared.
 */
static void netfs_read_from_server(struct netfs_read_request *rreq,
				   struct netfs_read_subrequest *subreq)
{
	netfs_stat(&netfs_n_rh_download);
	rreq->netfs_ops->issue_op(subreq);
}

/*
 * Release those waiting.
 */
static void netfs_rreq_completed(struct netfs_read_request *rreq, bool was_async)
{
	trace_netfs_rreq(rreq, netfs_rreq_trace_done);
	netfs_rreq_clear_subreqs(rreq, was_async);
	netfs_put_read_request(rreq, was_async);
}

/*
 * Unlock the pages in a read operation.  We need to set PG_fscache on any
 * pages we're going to write back before we unlock them.
 */
static void netfs_rreq_unlock(struct netfs_read_request *rreq)
{
	struct netfs_read_subrequest *subreq;
	struct page *page;
	unsigned int iopos, account = 0;
	pgoff_t start_page = rreq->start / PAGE_SIZE;
	pgoff_t last_page = ((rreq->start + rreq->len) / PAGE_SIZE) - 1;
	bool subreq_failed = false;
	int i;

	XA_STATE(xas, &rreq->mapping->i_pages, start_page);

	if (test_bit(NETFS_RREQ_FAILED, &rreq->flags)) {
		__clear_bit(NETFS_RREQ_WRITE_TO_CACHE, &rreq->flags);
		list_for_each_entry(subreq, &rreq->subrequests, rreq_link) {
			__clear_bit(NETFS_SREQ_WRITE_TO_CACHE, &subreq->flags);
		}
	}

	/* Walk through the pagecache and the I/O request lists simultaneously.
	 * We may have a mixture of cached and uncached sections and we only
	 * really want to write out the uncached sections.  This is slightly
	 * complicated by the possibility that we might have huge pages with a
	 * mixture inside.
	 */
	subreq = list_first_entry(&rreq->subrequests,
				  struct netfs_read_subrequest, rreq_link);
	iopos = 0;
	subreq_failed = (subreq->error < 0);

	trace_netfs_rreq(rreq, netfs_rreq_trace_unlock);

	rcu_read_lock();
	xas_for_each(&xas, page, last_page) {
		unsigned int pgpos = (page->index - start_page) * PAGE_SIZE;
		unsigned int pgend = pgpos + thp_size(page);
		bool pg_failed = false;

		for (;;) {
			if (!subreq) {
				pg_failed = true;
				break;
			}
			if (test_bit(NETFS_SREQ_WRITE_TO_CACHE, &subreq->flags))
				set_page_fscache(page);
			pg_failed |= subreq_failed;
			if (pgend < iopos + subreq->len)
				break;

			account += subreq->transferred;
			iopos += subreq->len;
			if (!list_is_last(&subreq->rreq_link, &rreq->subrequests)) {
				subreq = list_next_entry(subreq, rreq_link);
				subreq_failed = (subreq->error < 0);
			} else {
				subreq = NULL;
				subreq_failed = false;
			}
			if (pgend == iopos)
				break;
		}

		if (!pg_failed) {
			for (i = 0; i < thp_nr_pages(page); i++)
				flush_dcache_page(page);
			SetPageUptodate(page);
		}

		if (!test_bit(NETFS_RREQ_DONT_UNLOCK_PAGES, &rreq->flags)) {
			if (page->index == rreq->no_unlock_page &&
			    test_bit(NETFS_RREQ_NO_UNLOCK_PAGE, &rreq->flags))
				_debug("no unlock");
			else
				unlock_page(page);
		}
	}
	rcu_read_unlock();

	task_io_account_read(account);
	if (rreq->netfs_ops->done)
		rreq->netfs_ops->done(rreq);
}

/*
 * Handle a short read.
 */
static void netfs_rreq_short_read(struct netfs_read_request *rreq,
				  struct netfs_read_subrequest *subreq)
{
	__clear_bit(NETFS_SREQ_SHORT_READ, &subreq->flags);
	__set_bit(NETFS_SREQ_SEEK_DATA_READ, &subreq->flags);

	netfs_stat(&netfs_n_rh_short_read);
	trace_netfs_sreq(subreq, netfs_sreq_trace_resubmit_short);

	netfs_get_read_subrequest(subreq);
	atomic_inc(&rreq->nr_rd_ops);
	netfs_read_from_server(rreq, subreq);
}

/*
 * Resubmit any short or failed operations.  Returns true if we got the rreq
 * ref back.
 */
static bool netfs_rreq_perform_resubmissions(struct netfs_read_request *rreq)
{
	struct netfs_read_subrequest *subreq;

	WARN_ON(in_interrupt());

	trace_netfs_rreq(rreq, netfs_rreq_trace_resubmit);

	/* We don't want terminating submissions trying to wake us up whilst
	 * we're still going through the list.
	 */
	atomic_inc(&rreq->nr_rd_ops);

	__clear_bit(NETFS_RREQ_INCOMPLETE_IO, &rreq->flags);
	list_for_each_entry(subreq, &rreq->subrequests, rreq_link) {
		if (subreq->error) {
			if (subreq->source != NETFS_READ_FROM_CACHE)
				break;
			subreq->source = NETFS_DOWNLOAD_FROM_SERVER;
			subreq->error = 0;
			netfs_stat(&netfs_n_rh_download_instead);
			trace_netfs_sreq(subreq, netfs_sreq_trace_download_instead);
			netfs_get_read_subrequest(subreq);
			atomic_inc(&rreq->nr_rd_ops);
			netfs_read_from_server(rreq, subreq);
		} else if (test_bit(NETFS_SREQ_SHORT_READ, &subreq->flags)) {
			netfs_rreq_short_read(rreq, subreq);
		}
	}

	/* If we decrement nr_rd_ops to 0, the usage ref belongs to us. */
	if (atomic_dec_and_test(&rreq->nr_rd_ops))
		return true;

	wake_up_var(&rreq->nr_rd_ops);
	return false;
}

/*
 * Assess the state of a read request and decide what to do next.
 *
 * Note that we could be in an ordinary kernel thread, on a workqueue or in
 * softirq context at this point.  We inherit a ref from the caller.
 */
static void netfs_rreq_assess(struct netfs_read_request *rreq, bool was_async)
{
	trace_netfs_rreq(rreq, netfs_rreq_trace_assess);

again:
	if (!test_bit(NETFS_RREQ_FAILED, &rreq->flags) &&
	    test_bit(NETFS_RREQ_INCOMPLETE_IO, &rreq->flags)) {
		if (netfs_rreq_perform_resubmissions(rreq))
			goto again;
		return;
	}

	netfs_rreq_unlock(rreq);

	clear_bit_unlock(NETFS_RREQ_IN_PROGRESS, &rreq->flags);
	wake_up_bit(&rreq->flags, NETFS_RREQ_IN_PROGRESS);

	netfs_rreq_completed(rreq, was_async);
}

static void netfs_rreq_work(struct work_struct *work)
{
	struct netfs_read_request *rreq =
		container_of(work, struct netfs_read_request, work);
	netfs_rreq_assess(rreq, false);
}

/*
 * Handle the completion of all outstanding I/O operations on a read request.
 * We inherit a ref from the caller.
 */
static void netfs_rreq_terminated(struct netfs_read_request *rreq,
				  bool was_async)
{
	if (test_bit(NETFS_RREQ_INCOMPLETE_IO, &rreq->flags) &&
	    was_async) {
		if (!queue_work(system_unbound_wq, &rreq->work))
			BUG();
	} else {
		netfs_rreq_assess(rreq, was_async);
	}
}

/**
 * netfs_subreq_terminated - Note the termination of an I/O operation.
 * @subreq: The I/O request that has terminated.
 * @transferred_or_error: The amount of data transferred or an error code.
 * @was_async: The termination was asynchronous
 *
 * This tells the read helper that a contributory I/O operation has terminated,
 * one way or another, and that it should integrate the results.
 *
 * The caller indicates in @transferred_or_error the outcome of the operation,
 * supplying a positive value to indicate the number of bytes transferred, 0 to
 * indicate a failure to transfer anything that should be retried or a negative
 * error code.  The helper will look after reissuing I/O operations as
 * appropriate and writing downloaded data to the cache.
 *
 * If @was_async is true, the caller might be running in softirq or interrupt
 * context and we can't sleep.
 */
void netfs_subreq_terminated(struct netfs_read_subrequest *subreq,
			     ssize_t transferred_or_error,
			     bool was_async)
{
	struct netfs_read_request *rreq = subreq->rreq;
	int u;

	_enter("[%u]{%llx,%lx},%zd",
	       subreq->debug_index, subreq->start, subreq->flags,
	       transferred_or_error);

	switch (subreq->source) {
	case NETFS_READ_FROM_CACHE:
		netfs_stat(&netfs_n_rh_read_done);
		break;
	case NETFS_DOWNLOAD_FROM_SERVER:
		netfs_stat(&netfs_n_rh_download_done);
		break;
	default:
		break;
	}

	if (IS_ERR_VALUE(transferred_or_error)) {
		subreq->error = transferred_or_error;
		goto failed;
	}

	if (WARN(transferred_or_error > subreq->len - subreq->transferred,
		 "Subreq overread: R%x[%x] %zd > %zu - %zu",
		 rreq->debug_id, subreq->debug_index,
		 transferred_or_error, subreq->len, subreq->transferred))
		transferred_or_error = subreq->len - subreq->transferred;

	subreq->error = 0;
	subreq->transferred += transferred_or_error;
	if (subreq->transferred < subreq->len)
		goto incomplete;

complete:
	__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
	if (test_bit(NETFS_SREQ_WRITE_TO_CACHE, &subreq->flags))
		set_bit(NETFS_RREQ_WRITE_TO_CACHE, &rreq->flags);

out:
	trace_netfs_sreq(subreq, netfs_sreq_trace_terminated);

	/* If we decrement nr_rd_ops to 0, the ref belongs to us. */
	u = atomic_dec_return(&rreq->nr_rd_ops);
	if (u == 0)
		netfs_rreq_terminated(rreq, was_async);
	else if (u == 1)
		wake_up_var(&rreq->nr_rd_ops);

	netfs_put_subrequest(subreq, was_async);
	return;

incomplete:
	if (test_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags)) {
		netfs_clear_unread(subreq);
		subreq->transferred = subreq->len;
		goto complete;
	}

	if (transferred_or_error == 0) {
		if (__test_and_set_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags)) {
			subreq->error = -ENODATA;
			goto failed;
		}
	} else {
		__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
	}

	__set_bit(NETFS_SREQ_SHORT_READ, &subreq->flags);
	set_bit(NETFS_RREQ_INCOMPLETE_IO, &rreq->flags);
	goto out;

failed:
	if (subreq->source == NETFS_READ_FROM_CACHE) {
		netfs_stat(&netfs_n_rh_read_failed);
		set_bit(NETFS_RREQ_INCOMPLETE_IO, &rreq->flags);
	} else {
		netfs_stat(&netfs_n_rh_download_failed);
		set_bit(NETFS_RREQ_FAILED, &rreq->flags);
		rreq->error = subreq->error;
	}
	goto out;
}
EXPORT_SYMBOL(netfs_subreq_terminated);

static enum netfs_read_source netfs_cache_prepare_read(struct netfs_read_subrequest *subreq,
						       loff_t i_size)
{
	struct netfs_read_request *rreq = subreq->rreq;

	if (subreq->start >= rreq->i_size)
		return NETFS_FILL_WITH_ZEROES;
	return NETFS_DOWNLOAD_FROM_SERVER;
}

/*
 * Work out what sort of subrequest the next one will be.
 */
static enum netfs_read_source
netfs_rreq_prepare_read(struct netfs_read_request *rreq,
			struct netfs_read_subrequest *subreq)
{
	enum netfs_read_source source;

	_enter("%llx-%llx,%llx", subreq->start, subreq->start + subreq->len, rreq->i_size);

	source = netfs_cache_prepare_read(subreq, rreq->i_size);
	if (source == NETFS_INVALID_READ)
		goto out;

	if (source == NETFS_DOWNLOAD_FROM_SERVER) {
		/* Call out to the netfs to let it shrink the request to fit
		 * its own I/O sizes and boundaries.  If it shinks it here, it
		 * will be called again to make simultaneous calls; if it wants
		 * to make serial calls, it can indicate a short read and then
		 * we will call it again.
		 */
		if (subreq->len > rreq->i_size - subreq->start)
			subreq->len = rreq->i_size - subreq->start;

		if (rreq->netfs_ops->clamp_length &&
		    !rreq->netfs_ops->clamp_length(subreq)) {
			source = NETFS_INVALID_READ;
			goto out;
		}
	}

	if (WARN_ON(subreq->len == 0))
		source = NETFS_INVALID_READ;

out:
	subreq->source = source;
	trace_netfs_sreq(subreq, netfs_sreq_trace_prepare);
	return source;
}

/*
 * Slice off a piece of a read request and submit an I/O request for it.
 */
static bool netfs_rreq_submit_slice(struct netfs_read_request *rreq,
				    unsigned int *_debug_index)
{
	struct netfs_read_subrequest *subreq;
	enum netfs_read_source source;

	subreq = netfs_alloc_subrequest(rreq);
	if (!subreq)
		return false;

	subreq->debug_index	= (*_debug_index)++;
	subreq->start		= rreq->start + rreq->submitted;
	subreq->len		= rreq->len   - rreq->submitted;

	_debug("slice %llx,%zx,%zx", subreq->start, subreq->len, rreq->submitted);
	list_add_tail(&subreq->rreq_link, &rreq->subrequests);

	/* Call out to the cache to find out what it can do with the remaining
	 * subset.  It tells us in subreq->flags what it decided should be done
	 * and adjusts subreq->len down if the subset crosses a cache boundary.
	 *
	 * Then when we hand the subset, it can choose to take a subset of that
	 * (the starts must coincide), in which case, we go around the loop
	 * again and ask it to download the next piece.
	 */
	source = netfs_rreq_prepare_read(rreq, subreq);
	if (source == NETFS_INVALID_READ)
		goto subreq_failed;

	atomic_inc(&rreq->nr_rd_ops);

	rreq->submitted += subreq->len;

	trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
	switch (source) {
	case NETFS_FILL_WITH_ZEROES:
		netfs_fill_with_zeroes(rreq, subreq);
		break;
	case NETFS_DOWNLOAD_FROM_SERVER:
		netfs_read_from_server(rreq, subreq);
		break;
	default:
		BUG();
	}

	return true;

subreq_failed:
	rreq->error = subreq->error;
	netfs_put_subrequest(subreq, false);
	return false;
}

static void netfs_rreq_expand(struct netfs_read_request *rreq,
			      struct readahead_control *ractl)
{
	/* Give the netfs a chance to change the request parameters.  The
	 * resultant request must contain the original region.
	 */
	if (rreq->netfs_ops->expand_readahead)
		rreq->netfs_ops->expand_readahead(rreq);

	/* Expand the request if the cache wants it to start earlier.  Note
	 * that the expansion may get further extended if the VM wishes to
	 * insert THPs and the preferred start and/or end wind up in the middle
	 * of THPs.
	 *
	 * If this is the case, however, the THP size should be an integer
	 * multiple of the cache granule size, so we get a whole number of
	 * granules to deal with.
	 */
	if (rreq->start  != readahead_pos(ractl) ||
	    rreq->len != readahead_length(ractl)) {
		readahead_expand(ractl, rreq->start, rreq->len);
		rreq->start  = readahead_pos(ractl);
		rreq->len = readahead_length(ractl);

		trace_netfs_read(rreq, readahead_pos(ractl), readahead_length(ractl),
				 netfs_read_trace_expanded);
	}
}

/**
 * netfs_readahead - Helper to manage a read request
 * @ractl: The description of the readahead request
 * @ops: The network filesystem's operations for the helper to use
 * @netfs_priv: Private netfs data to be retained in the request
 *
 * Fulfil a readahead request by drawing data from the cache if possible, or
 * the netfs if not.  Space beyond the EOF is zero-filled.  Multiple I/O
 * requests from different sources will get munged together.  If necessary, the
 * readahead window can be expanded in either direction to a more convenient
 * alighment for RPC efficiency or to make storage in the cache feasible.
 *
 * The calling netfs must provide a table of operations, only one of which,
 * issue_op, is mandatory.  It may also be passed a private token, which will
 * be retained in rreq->netfs_priv and will be cleaned up by ops->cleanup().
 *
 * This is usable whether or not caching is enabled.
 */
void netfs_readahead(struct readahead_control *ractl,
		     const struct netfs_read_request_ops *ops,
		     void *netfs_priv)
{
	struct netfs_read_request *rreq;
	struct page *page;
	unsigned int debug_index = 0;

	_enter("%lx,%x", readahead_index(ractl), readahead_count(ractl));

	if (readahead_count(ractl) == 0)
		goto cleanup;

	rreq = netfs_alloc_read_request(ops, netfs_priv, ractl->file);
	if (!rreq)
		goto cleanup;
	rreq->mapping	= ractl->mapping;
	rreq->start	= readahead_pos(ractl);
	rreq->len	= readahead_length(ractl);

	netfs_stat(&netfs_n_rh_readahead);
	trace_netfs_read(rreq, readahead_pos(ractl), readahead_length(ractl),
			 netfs_read_trace_readahead);

	netfs_rreq_expand(rreq, ractl);

	atomic_set(&rreq->nr_rd_ops, 1);
	do {
		if (!netfs_rreq_submit_slice(rreq, &debug_index))
			break;

	} while (rreq->submitted < rreq->len);

	/* Drop the refs on the pages here rather than in the cache or
	 * filesystem.  The locks will be dropped in netfs_rreq_unlock().
	 */
	while ((page = readahead_page(ractl)))
		put_page(page);

	/* If we decrement nr_rd_ops to 0, the ref belongs to us. */
	if (atomic_dec_and_test(&rreq->nr_rd_ops))
		netfs_rreq_assess(rreq, false);
	return;

cleanup:
	if (netfs_priv)
		ops->cleanup(ractl->mapping, netfs_priv);
	return;
}
EXPORT_SYMBOL(netfs_readahead);

/**
 * netfs_page - Helper to manage a readpage request
 * @file: The file to read from
 * @page: The page to read
 * @ops: The network filesystem's operations for the helper to use
 * @netfs_priv: Private netfs data to be retained in the request
 *
 * Fulfil a readpage request by drawing data from the cache if possible, or the
 * netfs if not.  Space beyond the EOF is zero-filled.  Multiple I/O requests
 * from different sources will get munged together.
 *
 * The calling netfs must provide a table of operations, only one of which,
 * issue_op, is mandatory.  It may also be passed a private token, which will
 * be retained in rreq->netfs_priv and will be cleaned up by ops->cleanup().
 *
 * This is usable whether or not caching is enabled.
 */
int netfs_readpage(struct file *file,
		   struct page *page,
		   const struct netfs_read_request_ops *ops,
		   void *netfs_priv)
{
	struct netfs_read_request *rreq;
	unsigned int debug_index = 0;
	int ret;

	_enter("%lx", page_index(page));

	rreq = netfs_alloc_read_request(ops, netfs_priv, file);
	if (!rreq) {
		if (netfs_priv)
			ops->cleanup(netfs_priv, page_file_mapping(page));
		unlock_page(page);
		return -ENOMEM;
	}
	rreq->mapping	= page_file_mapping(page);
	rreq->start	= page_index(page) * PAGE_SIZE;
	rreq->len	= thp_size(page);

	netfs_stat(&netfs_n_rh_readpage);
	trace_netfs_read(rreq, rreq->start, rreq->len, netfs_read_trace_readpage);

	netfs_get_read_request(rreq);

	atomic_set(&rreq->nr_rd_ops, 1);
	do {
		if (!netfs_rreq_submit_slice(rreq, &debug_index))
			break;

	} while (rreq->submitted < rreq->len);

	/* Keep nr_rd_ops incremented so that the ref always belongs to us, and
	 * the service code isn't punted off to a random thread pool to
	 * process.
	 */
	do {
		wait_var_event(&rreq->nr_rd_ops, atomic_read(&rreq->nr_rd_ops) == 1);
		netfs_rreq_assess(rreq, false);
	} while (test_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags));

	ret = rreq->error;
	if (ret == 0 && rreq->submitted < rreq->len)
		ret = -EIO;
	netfs_put_read_request(rreq, false);
	return ret;
}
EXPORT_SYMBOL(netfs_readpage);
