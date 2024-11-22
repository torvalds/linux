// SPDX-License-Identifier: GPL-2.0-or-later
/* Network filesystem high-level buffered read support.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/task_io_accounting_ops.h>
#include "internal.h"

static void netfs_cache_expand_readahead(struct netfs_io_request *rreq,
					 unsigned long long *_start,
					 unsigned long long *_len,
					 unsigned long long i_size)
{
	struct netfs_cache_resources *cres = &rreq->cache_resources;

	if (cres->ops && cres->ops->expand_readahead)
		cres->ops->expand_readahead(cres, _start, _len, i_size);
}

static void netfs_rreq_expand(struct netfs_io_request *rreq,
			      struct readahead_control *ractl)
{
	/* Give the cache a chance to change the request parameters.  The
	 * resultant request must contain the original region.
	 */
	netfs_cache_expand_readahead(rreq, &rreq->start, &rreq->len, rreq->i_size);

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

/*
 * Begin an operation, and fetch the stored zero point value from the cookie if
 * available.
 */
static int netfs_begin_cache_read(struct netfs_io_request *rreq, struct netfs_inode *ctx)
{
	return fscache_begin_read_operation(&rreq->cache_resources, netfs_i_cookie(ctx));
}

/*
 * Decant the list of folios to read into a rolling buffer.
 */
static size_t netfs_load_buffer_from_ra(struct netfs_io_request *rreq,
					struct folio_queue *folioq,
					struct folio_batch *put_batch)
{
	unsigned int order, nr;
	size_t size = 0;

	nr = __readahead_batch(rreq->ractl, (struct page **)folioq->vec.folios,
			       ARRAY_SIZE(folioq->vec.folios));
	folioq->vec.nr = nr;
	for (int i = 0; i < nr; i++) {
		struct folio *folio = folioq_folio(folioq, i);

		trace_netfs_folio(folio, netfs_folio_trace_read);
		order = folio_order(folio);
		folioq->orders[i] = order;
		size += PAGE_SIZE << order;

		if (!folio_batch_add(put_batch, folio))
			folio_batch_release(put_batch);
	}

	for (int i = nr; i < folioq_nr_slots(folioq); i++)
		folioq_clear(folioq, i);

	return size;
}

/*
 * netfs_prepare_read_iterator - Prepare the subreq iterator for I/O
 * @subreq: The subrequest to be set up
 *
 * Prepare the I/O iterator representing the read buffer on a subrequest for
 * the filesystem to use for I/O (it can be passed directly to a socket).  This
 * is intended to be called from the ->issue_read() method once the filesystem
 * has trimmed the request to the size it wants.
 *
 * Returns the limited size if successful and -ENOMEM if insufficient memory
 * available.
 *
 * [!] NOTE: This must be run in the same thread as ->issue_read() was called
 * in as we access the readahead_control struct.
 */
static ssize_t netfs_prepare_read_iterator(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	size_t rsize = subreq->len;

	if (subreq->source == NETFS_DOWNLOAD_FROM_SERVER)
		rsize = umin(rsize, rreq->io_streams[0].sreq_max_len);

	if (rreq->ractl) {
		/* If we don't have sufficient folios in the rolling buffer,
		 * extract a folioq's worth from the readahead region at a time
		 * into the buffer.  Note that this acquires a ref on each page
		 * that we will need to release later - but we don't want to do
		 * that until after we've started the I/O.
		 */
		struct folio_batch put_batch;

		folio_batch_init(&put_batch);
		while (rreq->submitted < subreq->start + rsize) {
			struct folio_queue *tail = rreq->buffer_tail, *new;
			size_t added;

			new = kmalloc(sizeof(*new), GFP_NOFS);
			if (!new)
				return -ENOMEM;
			netfs_stat(&netfs_n_folioq);
			folioq_init(new);
			new->prev = tail;
			tail->next = new;
			rreq->buffer_tail = new;
			added = netfs_load_buffer_from_ra(rreq, new, &put_batch);
			rreq->iter.count += added;
			rreq->submitted += added;
		}
		folio_batch_release(&put_batch);
	}

	subreq->len = rsize;
	if (unlikely(rreq->io_streams[0].sreq_max_segs)) {
		size_t limit = netfs_limit_iter(&rreq->iter, 0, rsize,
						rreq->io_streams[0].sreq_max_segs);

		if (limit < rsize) {
			subreq->len = limit;
			trace_netfs_sreq(subreq, netfs_sreq_trace_limited);
		}
	}

	subreq->io_iter	= rreq->iter;

	if (iov_iter_is_folioq(&subreq->io_iter)) {
		if (subreq->io_iter.folioq_slot >= folioq_nr_slots(subreq->io_iter.folioq)) {
			subreq->io_iter.folioq = subreq->io_iter.folioq->next;
			subreq->io_iter.folioq_slot = 0;
		}
		subreq->curr_folioq = (struct folio_queue *)subreq->io_iter.folioq;
		subreq->curr_folioq_slot = subreq->io_iter.folioq_slot;
		subreq->curr_folio_order = subreq->curr_folioq->orders[subreq->curr_folioq_slot];
	}

	iov_iter_truncate(&subreq->io_iter, subreq->len);
	iov_iter_advance(&rreq->iter, subreq->len);
	return subreq->len;
}

static enum netfs_io_source netfs_cache_prepare_read(struct netfs_io_request *rreq,
						     struct netfs_io_subrequest *subreq,
						     loff_t i_size)
{
	struct netfs_cache_resources *cres = &rreq->cache_resources;

	if (!cres->ops)
		return NETFS_DOWNLOAD_FROM_SERVER;
	return cres->ops->prepare_read(subreq, i_size);
}

static void netfs_cache_read_terminated(void *priv, ssize_t transferred_or_error,
					bool was_async)
{
	struct netfs_io_subrequest *subreq = priv;

	if (transferred_or_error < 0) {
		netfs_read_subreq_terminated(subreq, transferred_or_error, was_async);
		return;
	}

	if (transferred_or_error > 0)
		subreq->transferred += transferred_or_error;
	netfs_read_subreq_terminated(subreq, 0, was_async);
}

/*
 * Issue a read against the cache.
 * - Eats the caller's ref on subreq.
 */
static void netfs_read_cache_to_pagecache(struct netfs_io_request *rreq,
					  struct netfs_io_subrequest *subreq)
{
	struct netfs_cache_resources *cres = &rreq->cache_resources;

	netfs_stat(&netfs_n_rh_read);
	cres->ops->read(cres, subreq->start, &subreq->io_iter, NETFS_READ_HOLE_IGNORE,
			netfs_cache_read_terminated, subreq);
}

/*
 * Perform a read to the pagecache from a series of sources of different types,
 * slicing up the region to be read according to available cache blocks and
 * network rsize.
 */
static void netfs_read_to_pagecache(struct netfs_io_request *rreq)
{
	struct netfs_inode *ictx = netfs_inode(rreq->inode);
	unsigned long long start = rreq->start;
	ssize_t size = rreq->len;
	int ret = 0;

	atomic_inc(&rreq->nr_outstanding);

	do {
		struct netfs_io_subrequest *subreq;
		enum netfs_io_source source = NETFS_DOWNLOAD_FROM_SERVER;
		ssize_t slice;

		subreq = netfs_alloc_subrequest(rreq);
		if (!subreq) {
			ret = -ENOMEM;
			break;
		}

		subreq->start	= start;
		subreq->len	= size;

		atomic_inc(&rreq->nr_outstanding);
		spin_lock_bh(&rreq->lock);
		list_add_tail(&subreq->rreq_link, &rreq->subrequests);
		subreq->prev_donated = rreq->prev_donated;
		rreq->prev_donated = 0;
		trace_netfs_sreq(subreq, netfs_sreq_trace_added);
		spin_unlock_bh(&rreq->lock);

		source = netfs_cache_prepare_read(rreq, subreq, rreq->i_size);
		subreq->source = source;
		if (source == NETFS_DOWNLOAD_FROM_SERVER) {
			unsigned long long zp = umin(ictx->zero_point, rreq->i_size);
			size_t len = subreq->len;

			if (subreq->start >= zp) {
				subreq->source = source = NETFS_FILL_WITH_ZEROES;
				goto fill_with_zeroes;
			}

			if (len > zp - subreq->start)
				len = zp - subreq->start;
			if (len == 0) {
				pr_err("ZERO-LEN READ: R=%08x[%x] l=%zx/%zx s=%llx z=%llx i=%llx",
				       rreq->debug_id, subreq->debug_index,
				       subreq->len, size,
				       subreq->start, ictx->zero_point, rreq->i_size);
				break;
			}
			subreq->len = len;

			netfs_stat(&netfs_n_rh_download);
			if (rreq->netfs_ops->prepare_read) {
				ret = rreq->netfs_ops->prepare_read(subreq);
				if (ret < 0) {
					atomic_dec(&rreq->nr_outstanding);
					netfs_put_subrequest(subreq, false,
							     netfs_sreq_trace_put_cancel);
					break;
				}
				trace_netfs_sreq(subreq, netfs_sreq_trace_prepare);
			}

			slice = netfs_prepare_read_iterator(subreq);
			if (slice < 0) {
				atomic_dec(&rreq->nr_outstanding);
				netfs_put_subrequest(subreq, false, netfs_sreq_trace_put_cancel);
				ret = slice;
				break;
			}

			rreq->netfs_ops->issue_read(subreq);
			goto done;
		}

	fill_with_zeroes:
		if (source == NETFS_FILL_WITH_ZEROES) {
			subreq->source = NETFS_FILL_WITH_ZEROES;
			trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
			netfs_stat(&netfs_n_rh_zero);
			slice = netfs_prepare_read_iterator(subreq);
			__set_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags);
			netfs_read_subreq_terminated(subreq, 0, false);
			goto done;
		}

		if (source == NETFS_READ_FROM_CACHE) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
			slice = netfs_prepare_read_iterator(subreq);
			netfs_read_cache_to_pagecache(rreq, subreq);
			goto done;
		}

		pr_err("Unexpected read source %u\n", source);
		WARN_ON_ONCE(1);
		break;

	done:
		size -= slice;
		start += slice;
		cond_resched();
	} while (size > 0);

	if (atomic_dec_and_test(&rreq->nr_outstanding))
		netfs_rreq_terminated(rreq, false);

	/* Defer error return as we may need to wait for outstanding I/O. */
	cmpxchg(&rreq->error, 0, ret);
}

/*
 * Wait for the read operation to complete, successfully or otherwise.
 */
static int netfs_wait_for_read(struct netfs_io_request *rreq)
{
	int ret;

	trace_netfs_rreq(rreq, netfs_rreq_trace_wait_ip);
	wait_on_bit(&rreq->flags, NETFS_RREQ_IN_PROGRESS, TASK_UNINTERRUPTIBLE);
	ret = rreq->error;
	if (ret == 0 && rreq->submitted < rreq->len) {
		trace_netfs_failure(rreq, NULL, ret, netfs_fail_short_read);
		ret = -EIO;
	}

	return ret;
}

/*
 * Set up the initial folioq of buffer folios in the rolling buffer and set the
 * iterator to refer to it.
 */
static int netfs_prime_buffer(struct netfs_io_request *rreq)
{
	struct folio_queue *folioq;
	struct folio_batch put_batch;
	size_t added;

	folioq = kmalloc(sizeof(*folioq), GFP_KERNEL);
	if (!folioq)
		return -ENOMEM;
	netfs_stat(&netfs_n_folioq);
	folioq_init(folioq);
	rreq->buffer = folioq;
	rreq->buffer_tail = folioq;
	rreq->submitted = rreq->start;
	iov_iter_folio_queue(&rreq->iter, ITER_DEST, folioq, 0, 0, 0);

	folio_batch_init(&put_batch);
	added = netfs_load_buffer_from_ra(rreq, folioq, &put_batch);
	folio_batch_release(&put_batch);
	rreq->iter.count += added;
	rreq->submitted += added;
	return 0;
}

/**
 * netfs_readahead - Helper to manage a read request
 * @ractl: The description of the readahead request
 *
 * Fulfil a readahead request by drawing data from the cache if possible, or
 * the netfs if not.  Space beyond the EOF is zero-filled.  Multiple I/O
 * requests from different sources will get munged together.  If necessary, the
 * readahead window can be expanded in either direction to a more convenient
 * alighment for RPC efficiency or to make storage in the cache feasible.
 *
 * The calling netfs must initialise a netfs context contiguous to the vfs
 * inode before calling this.
 *
 * This is usable whether or not caching is enabled.
 */
void netfs_readahead(struct readahead_control *ractl)
{
	struct netfs_io_request *rreq;
	struct netfs_inode *ictx = netfs_inode(ractl->mapping->host);
	unsigned long long start = readahead_pos(ractl);
	size_t size = readahead_length(ractl);
	int ret;

	rreq = netfs_alloc_request(ractl->mapping, ractl->file, start, size,
				   NETFS_READAHEAD);
	if (IS_ERR(rreq))
		return;

	ret = netfs_begin_cache_read(rreq, ictx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto cleanup_free;

	netfs_stat(&netfs_n_rh_readahead);
	trace_netfs_read(rreq, readahead_pos(ractl), readahead_length(ractl),
			 netfs_read_trace_readahead);

	netfs_rreq_expand(rreq, ractl);

	rreq->ractl = ractl;
	if (netfs_prime_buffer(rreq) < 0)
		goto cleanup_free;
	netfs_read_to_pagecache(rreq);

	netfs_put_request(rreq, true, netfs_rreq_trace_put_return);
	return;

cleanup_free:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_failed);
	return;
}
EXPORT_SYMBOL(netfs_readahead);

/*
 * Create a rolling buffer with a single occupying folio.
 */
static int netfs_create_singular_buffer(struct netfs_io_request *rreq, struct folio *folio)
{
	struct folio_queue *folioq;

	folioq = kmalloc(sizeof(*folioq), GFP_KERNEL);
	if (!folioq)
		return -ENOMEM;

	netfs_stat(&netfs_n_folioq);
	folioq_init(folioq);
	folioq_append(folioq, folio);
	BUG_ON(folioq_folio(folioq, 0) != folio);
	BUG_ON(folioq_folio_order(folioq, 0) != folio_order(folio));
	rreq->buffer = folioq;
	rreq->buffer_tail = folioq;
	rreq->submitted = rreq->start + rreq->len;
	iov_iter_folio_queue(&rreq->iter, ITER_DEST, folioq, 0, 0, rreq->len);
	rreq->ractl = (struct readahead_control *)1UL;
	return 0;
}

/*
 * Read into gaps in a folio partially filled by a streaming write.
 */
static int netfs_read_gaps(struct file *file, struct folio *folio)
{
	struct netfs_io_request *rreq;
	struct address_space *mapping = folio->mapping;
	struct netfs_folio *finfo = netfs_folio_info(folio);
	struct netfs_inode *ctx = netfs_inode(mapping->host);
	struct folio *sink = NULL;
	struct bio_vec *bvec;
	unsigned int from = finfo->dirty_offset;
	unsigned int to = from + finfo->dirty_len;
	unsigned int off = 0, i = 0;
	size_t flen = folio_size(folio);
	size_t nr_bvec = flen / PAGE_SIZE + 2;
	size_t part;
	int ret;

	_enter("%lx", folio->index);

	rreq = netfs_alloc_request(mapping, file, folio_pos(folio), flen, NETFS_READ_GAPS);
	if (IS_ERR(rreq)) {
		ret = PTR_ERR(rreq);
		goto alloc_error;
	}

	ret = netfs_begin_cache_read(rreq, ctx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto discard;

	netfs_stat(&netfs_n_rh_read_folio);
	trace_netfs_read(rreq, rreq->start, rreq->len, netfs_read_trace_read_gaps);

	/* Fiddle the buffer so that a gap at the beginning and/or a gap at the
	 * end get copied to, but the middle is discarded.
	 */
	ret = -ENOMEM;
	bvec = kmalloc_array(nr_bvec, sizeof(*bvec), GFP_KERNEL);
	if (!bvec)
		goto discard;

	sink = folio_alloc(GFP_KERNEL, 0);
	if (!sink) {
		kfree(bvec);
		goto discard;
	}

	trace_netfs_folio(folio, netfs_folio_trace_read_gaps);

	rreq->direct_bv = bvec;
	rreq->direct_bv_count = nr_bvec;
	if (from > 0) {
		bvec_set_folio(&bvec[i++], folio, from, 0);
		off = from;
	}
	while (off < to) {
		part = min_t(size_t, to - off, PAGE_SIZE);
		bvec_set_folio(&bvec[i++], sink, part, 0);
		off += part;
	}
	if (to < flen)
		bvec_set_folio(&bvec[i++], folio, flen - to, to);
	iov_iter_bvec(&rreq->iter, ITER_DEST, bvec, i, rreq->len);
	rreq->submitted = rreq->start + flen;

	netfs_read_to_pagecache(rreq);

	if (sink)
		folio_put(sink);

	ret = netfs_wait_for_read(rreq);
	if (ret == 0) {
		flush_dcache_folio(folio);
		folio_mark_uptodate(folio);
	}
	folio_unlock(folio);
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);
	return ret < 0 ? ret : 0;

discard:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_discard);
alloc_error:
	folio_unlock(folio);
	return ret;
}

/**
 * netfs_read_folio - Helper to manage a read_folio request
 * @file: The file to read from
 * @folio: The folio to read
 *
 * Fulfil a read_folio request by drawing data from the cache if
 * possible, or the netfs if not.  Space beyond the EOF is zero-filled.
 * Multiple I/O requests from different sources will get munged together.
 *
 * The calling netfs must initialise a netfs context contiguous to the vfs
 * inode before calling this.
 *
 * This is usable whether or not caching is enabled.
 */
int netfs_read_folio(struct file *file, struct folio *folio)
{
	struct address_space *mapping = folio->mapping;
	struct netfs_io_request *rreq;
	struct netfs_inode *ctx = netfs_inode(mapping->host);
	int ret;

	if (folio_test_dirty(folio)) {
		trace_netfs_folio(folio, netfs_folio_trace_read_gaps);
		return netfs_read_gaps(file, folio);
	}

	_enter("%lx", folio->index);

	rreq = netfs_alloc_request(mapping, file,
				   folio_pos(folio), folio_size(folio),
				   NETFS_READPAGE);
	if (IS_ERR(rreq)) {
		ret = PTR_ERR(rreq);
		goto alloc_error;
	}

	ret = netfs_begin_cache_read(rreq, ctx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto discard;

	netfs_stat(&netfs_n_rh_read_folio);
	trace_netfs_read(rreq, rreq->start, rreq->len, netfs_read_trace_readpage);

	/* Set up the output buffer */
	ret = netfs_create_singular_buffer(rreq, folio);
	if (ret < 0)
		goto discard;

	netfs_read_to_pagecache(rreq);
	ret = netfs_wait_for_read(rreq);
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);
	return ret < 0 ? ret : 0;

discard:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_discard);
alloc_error:
	folio_unlock(folio);
	return ret;
}
EXPORT_SYMBOL(netfs_read_folio);

/*
 * Prepare a folio for writing without reading first
 * @folio: The folio being prepared
 * @pos: starting position for the write
 * @len: length of write
 * @always_fill: T if the folio should always be completely filled/cleared
 *
 * In some cases, write_begin doesn't need to read at all:
 * - full folio write
 * - write that lies in a folio that is completely beyond EOF
 * - write that covers the folio from start to EOF or beyond it
 *
 * If any of these criteria are met, then zero out the unwritten parts
 * of the folio and return true. Otherwise, return false.
 */
static bool netfs_skip_folio_read(struct folio *folio, loff_t pos, size_t len,
				 bool always_fill)
{
	struct inode *inode = folio_inode(folio);
	loff_t i_size = i_size_read(inode);
	size_t offset = offset_in_folio(folio, pos);
	size_t plen = folio_size(folio);

	if (unlikely(always_fill)) {
		if (pos - offset + len <= i_size)
			return false; /* Page entirely before EOF */
		zero_user_segment(&folio->page, 0, plen);
		folio_mark_uptodate(folio);
		return true;
	}

	/* Full folio write */
	if (offset == 0 && len >= plen)
		return true;

	/* Page entirely beyond the end of the file */
	if (pos - offset >= i_size)
		goto zero_out;

	/* Write that covers from the start of the folio to EOF or beyond */
	if (offset == 0 && (pos + len) >= i_size)
		goto zero_out;

	return false;
zero_out:
	zero_user_segments(&folio->page, 0, offset, offset + len, plen);
	return true;
}

/**
 * netfs_write_begin - Helper to prepare for writing [DEPRECATED]
 * @ctx: The netfs context
 * @file: The file to read from
 * @mapping: The mapping to read from
 * @pos: File position at which the write will begin
 * @len: The length of the write (may extend beyond the end of the folio chosen)
 * @_folio: Where to put the resultant folio
 * @_fsdata: Place for the netfs to store a cookie
 *
 * Pre-read data for a write-begin request by drawing data from the cache if
 * possible, or the netfs if not.  Space beyond the EOF is zero-filled.
 * Multiple I/O requests from different sources will get munged together.
 *
 * The calling netfs must provide a table of operations, only one of which,
 * issue_read, is mandatory.
 *
 * The check_write_begin() operation can be provided to check for and flush
 * conflicting writes once the folio is grabbed and locked.  It is passed a
 * pointer to the fsdata cookie that gets returned to the VM to be passed to
 * write_end.  It is permitted to sleep.  It should return 0 if the request
 * should go ahead or it may return an error.  It may also unlock and put the
 * folio, provided it sets ``*foliop`` to NULL, in which case a return of 0
 * will cause the folio to be re-got and the process to be retried.
 *
 * The calling netfs must initialise a netfs context contiguous to the vfs
 * inode before calling this.
 *
 * This is usable whether or not caching is enabled.
 *
 * Note that this should be considered deprecated and netfs_perform_write()
 * used instead.
 */
int netfs_write_begin(struct netfs_inode *ctx,
		      struct file *file, struct address_space *mapping,
		      loff_t pos, unsigned int len, struct folio **_folio,
		      void **_fsdata)
{
	struct netfs_io_request *rreq;
	struct folio *folio;
	pgoff_t index = pos >> PAGE_SHIFT;
	int ret;

retry:
	folio = __filemap_get_folio(mapping, index, FGP_WRITEBEGIN,
				    mapping_gfp_mask(mapping));
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	if (ctx->ops->check_write_begin) {
		/* Allow the netfs (eg. ceph) to flush conflicts. */
		ret = ctx->ops->check_write_begin(file, pos, len, &folio, _fsdata);
		if (ret < 0) {
			trace_netfs_failure(NULL, NULL, ret, netfs_fail_check_write_begin);
			goto error;
		}
		if (!folio)
			goto retry;
	}

	if (folio_test_uptodate(folio))
		goto have_folio;

	/* If the page is beyond the EOF, we want to clear it - unless it's
	 * within the cache granule containing the EOF, in which case we need
	 * to preload the granule.
	 */
	if (!netfs_is_cache_enabled(ctx) &&
	    netfs_skip_folio_read(folio, pos, len, false)) {
		netfs_stat(&netfs_n_rh_write_zskip);
		goto have_folio_no_wait;
	}

	rreq = netfs_alloc_request(mapping, file,
				   folio_pos(folio), folio_size(folio),
				   NETFS_READ_FOR_WRITE);
	if (IS_ERR(rreq)) {
		ret = PTR_ERR(rreq);
		goto error;
	}
	rreq->no_unlock_folio	= folio->index;
	__set_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags);

	ret = netfs_begin_cache_read(rreq, ctx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto error_put;

	netfs_stat(&netfs_n_rh_write_begin);
	trace_netfs_read(rreq, pos, len, netfs_read_trace_write_begin);

	/* Set up the output buffer */
	ret = netfs_create_singular_buffer(rreq, folio);
	if (ret < 0)
		goto error_put;

	netfs_read_to_pagecache(rreq);
	ret = netfs_wait_for_read(rreq);
	if (ret < 0)
		goto error;
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);

have_folio:
	ret = folio_wait_private_2_killable(folio);
	if (ret < 0)
		goto error;
have_folio_no_wait:
	*_folio = folio;
	_leave(" = 0");
	return 0;

error_put:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_failed);
error:
	if (folio) {
		folio_unlock(folio);
		folio_put(folio);
	}
	_leave(" = %d", ret);
	return ret;
}
EXPORT_SYMBOL(netfs_write_begin);

/*
 * Preload the data into a page we're proposing to write into.
 */
int netfs_prefetch_for_write(struct file *file, struct folio *folio,
			     size_t offset, size_t len)
{
	struct netfs_io_request *rreq;
	struct address_space *mapping = folio->mapping;
	struct netfs_inode *ctx = netfs_inode(mapping->host);
	unsigned long long start = folio_pos(folio);
	size_t flen = folio_size(folio);
	int ret;

	_enter("%zx @%llx", flen, start);

	ret = -ENOMEM;

	rreq = netfs_alloc_request(mapping, file, start, flen,
				   NETFS_READ_FOR_WRITE);
	if (IS_ERR(rreq)) {
		ret = PTR_ERR(rreq);
		goto error;
	}

	rreq->no_unlock_folio = folio->index;
	__set_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags);
	ret = netfs_begin_cache_read(rreq, ctx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto error_put;

	netfs_stat(&netfs_n_rh_write_begin);
	trace_netfs_read(rreq, start, flen, netfs_read_trace_prefetch_for_write);

	/* Set up the output buffer */
	ret = netfs_create_singular_buffer(rreq, folio);
	if (ret < 0)
		goto error_put;

	folioq_mark2(rreq->buffer, 0);
	netfs_read_to_pagecache(rreq);
	ret = netfs_wait_for_read(rreq);
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);
	return ret;

error_put:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_discard);
error:
	_leave(" = %d", ret);
	return ret;
}

/**
 * netfs_buffered_read_iter - Filesystem buffered I/O read routine
 * @iocb: kernel I/O control block
 * @iter: destination for the data read
 *
 * This is the ->read_iter() routine for all filesystems that can use the page
 * cache directly.
 *
 * The IOCB_NOWAIT flag in iocb->ki_flags indicates that -EAGAIN shall be
 * returned when no data can be read without waiting for I/O requests to
 * complete; it doesn't prevent readahead.
 *
 * The IOCB_NOIO flag in iocb->ki_flags indicates that no new I/O requests
 * shall be made for the read or for readahead.  When no data can be read,
 * -EAGAIN shall be returned.  When readahead would be triggered, a partial,
 * possibly empty read shall be returned.
 *
 * Return:
 * * number of bytes copied, even for partial reads
 * * negative error code (or 0 if IOCB_NOIO) if nothing was read
 */
ssize_t netfs_buffered_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct netfs_inode *ictx = netfs_inode(inode);
	ssize_t ret;

	if (WARN_ON_ONCE((iocb->ki_flags & IOCB_DIRECT) ||
			 test_bit(NETFS_ICTX_UNBUFFERED, &ictx->flags)))
		return -EINVAL;

	ret = netfs_start_io_read(inode);
	if (ret == 0) {
		ret = filemap_read(iocb, iter, 0);
		netfs_end_io_read(inode);
	}
	return ret;
}
EXPORT_SYMBOL(netfs_buffered_read_iter);

/**
 * netfs_file_read_iter - Generic filesystem read routine
 * @iocb: kernel I/O control block
 * @iter: destination for the data read
 *
 * This is the ->read_iter() routine for all filesystems that can use the page
 * cache directly.
 *
 * The IOCB_NOWAIT flag in iocb->ki_flags indicates that -EAGAIN shall be
 * returned when no data can be read without waiting for I/O requests to
 * complete; it doesn't prevent readahead.
 *
 * The IOCB_NOIO flag in iocb->ki_flags indicates that no new I/O requests
 * shall be made for the read or for readahead.  When no data can be read,
 * -EAGAIN shall be returned.  When readahead would be triggered, a partial,
 * possibly empty read shall be returned.
 *
 * Return:
 * * number of bytes copied, even for partial reads
 * * negative error code (or 0 if IOCB_NOIO) if nothing was read
 */
ssize_t netfs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct netfs_inode *ictx = netfs_inode(iocb->ki_filp->f_mapping->host);

	if ((iocb->ki_flags & IOCB_DIRECT) ||
	    test_bit(NETFS_ICTX_UNBUFFERED, &ictx->flags))
		return netfs_unbuffered_read_iter(iocb, iter);

	return netfs_buffered_read_iter(iocb, iter);
}
EXPORT_SYMBOL(netfs_file_read_iter);
