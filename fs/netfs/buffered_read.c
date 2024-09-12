// SPDX-License-Identifier: GPL-2.0-or-later
/* Network filesystem high-level buffered read support.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/task_io_accounting_ops.h>
#include "internal.h"

/*
 * [DEPRECATED] Unlock the folios in a read operation for when the filesystem
 * is using PG_private_2 and direct writing to the cache from here rather than
 * marking the page for writeback.
 *
 * Note that we don't touch folio->private in this code.
 */
static void netfs_rreq_unlock_folios_pgpriv2(struct netfs_io_request *rreq,
					     size_t *account)
{
	struct netfs_io_subrequest *subreq;
	struct folio *folio;
	pgoff_t start_page = rreq->start / PAGE_SIZE;
	pgoff_t last_page = ((rreq->start + rreq->len) / PAGE_SIZE) - 1;
	bool subreq_failed = false;

	XA_STATE(xas, &rreq->mapping->i_pages, start_page);

	/* Walk through the pagecache and the I/O request lists simultaneously.
	 * We may have a mixture of cached and uncached sections and we only
	 * really want to write out the uncached sections.  This is slightly
	 * complicated by the possibility that we might have huge pages with a
	 * mixture inside.
	 */
	subreq = list_first_entry(&rreq->subrequests,
				  struct netfs_io_subrequest, rreq_link);
	subreq_failed = (subreq->error < 0);

	trace_netfs_rreq(rreq, netfs_rreq_trace_unlock_pgpriv2);

	rcu_read_lock();
	xas_for_each(&xas, folio, last_page) {
		loff_t pg_end;
		bool pg_failed = false;
		bool folio_started = false;

		if (xas_retry(&xas, folio))
			continue;

		pg_end = folio_pos(folio) + folio_size(folio) - 1;

		for (;;) {
			loff_t sreq_end;

			if (!subreq) {
				pg_failed = true;
				break;
			}

			if (!folio_started &&
			    test_bit(NETFS_SREQ_COPY_TO_CACHE, &subreq->flags) &&
			    fscache_operation_valid(&rreq->cache_resources)) {
				trace_netfs_folio(folio, netfs_folio_trace_copy_to_cache);
				folio_start_private_2(folio);
				folio_started = true;
			}

			pg_failed |= subreq_failed;
			sreq_end = subreq->start + subreq->len - 1;
			if (pg_end < sreq_end)
				break;

			*account += subreq->transferred;
			if (!list_is_last(&subreq->rreq_link, &rreq->subrequests)) {
				subreq = list_next_entry(subreq, rreq_link);
				subreq_failed = (subreq->error < 0);
			} else {
				subreq = NULL;
				subreq_failed = false;
			}

			if (pg_end == sreq_end)
				break;
		}

		if (!pg_failed) {
			flush_dcache_folio(folio);
			folio_mark_uptodate(folio);
		}

		if (!test_bit(NETFS_RREQ_DONT_UNLOCK_FOLIOS, &rreq->flags)) {
			if (folio->index == rreq->no_unlock_folio &&
			    test_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags))
				_debug("no unlock");
			else
				folio_unlock(folio);
		}
	}
	rcu_read_unlock();
}

/*
 * Unlock the folios in a read operation.  We need to set PG_writeback on any
 * folios we're going to write back before we unlock them.
 *
 * Note that if the deprecated NETFS_RREQ_USE_PGPRIV2 is set then we use
 * PG_private_2 and do a direct write to the cache from here instead.
 */
void netfs_rreq_unlock_folios(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_folio *finfo;
	struct folio *folio;
	pgoff_t start_page = rreq->start / PAGE_SIZE;
	pgoff_t last_page = ((rreq->start + rreq->len) / PAGE_SIZE) - 1;
	size_t account = 0;
	bool subreq_failed = false;

	XA_STATE(xas, &rreq->mapping->i_pages, start_page);

	if (test_bit(NETFS_RREQ_FAILED, &rreq->flags)) {
		__clear_bit(NETFS_RREQ_COPY_TO_CACHE, &rreq->flags);
		list_for_each_entry(subreq, &rreq->subrequests, rreq_link) {
			__clear_bit(NETFS_SREQ_COPY_TO_CACHE, &subreq->flags);
		}
	}

	/* Handle deprecated PG_private_2 case. */
	if (test_bit(NETFS_RREQ_USE_PGPRIV2, &rreq->flags)) {
		netfs_rreq_unlock_folios_pgpriv2(rreq, &account);
		goto out;
	}

	/* Walk through the pagecache and the I/O request lists simultaneously.
	 * We may have a mixture of cached and uncached sections and we only
	 * really want to write out the uncached sections.  This is slightly
	 * complicated by the possibility that we might have huge pages with a
	 * mixture inside.
	 */
	subreq = list_first_entry(&rreq->subrequests,
				  struct netfs_io_subrequest, rreq_link);
	subreq_failed = (subreq->error < 0);

	trace_netfs_rreq(rreq, netfs_rreq_trace_unlock);

	rcu_read_lock();
	xas_for_each(&xas, folio, last_page) {
		loff_t pg_end;
		bool pg_failed = false;
		bool wback_to_cache = false;

		if (xas_retry(&xas, folio))
			continue;

		pg_end = folio_pos(folio) + folio_size(folio) - 1;

		for (;;) {
			loff_t sreq_end;

			if (!subreq) {
				pg_failed = true;
				break;
			}

			wback_to_cache |= test_bit(NETFS_SREQ_COPY_TO_CACHE, &subreq->flags);
			pg_failed |= subreq_failed;
			sreq_end = subreq->start + subreq->len - 1;
			if (pg_end < sreq_end)
				break;

			account += subreq->transferred;
			if (!list_is_last(&subreq->rreq_link, &rreq->subrequests)) {
				subreq = list_next_entry(subreq, rreq_link);
				subreq_failed = (subreq->error < 0);
			} else {
				subreq = NULL;
				subreq_failed = false;
			}

			if (pg_end == sreq_end)
				break;
		}

		if (!pg_failed) {
			flush_dcache_folio(folio);
			finfo = netfs_folio_info(folio);
			if (finfo) {
				trace_netfs_folio(folio, netfs_folio_trace_filled_gaps);
				if (finfo->netfs_group)
					folio_change_private(folio, finfo->netfs_group);
				else
					folio_detach_private(folio);
				kfree(finfo);
			}
			folio_mark_uptodate(folio);
			if (wback_to_cache && !WARN_ON_ONCE(folio_get_private(folio) != NULL)) {
				trace_netfs_folio(folio, netfs_folio_trace_copy_to_cache);
				folio_attach_private(folio, NETFS_FOLIO_COPY_TO_CACHE);
				filemap_dirty_folio(folio->mapping, folio);
			}
		}

		if (!test_bit(NETFS_RREQ_DONT_UNLOCK_FOLIOS, &rreq->flags)) {
			if (folio->index == rreq->no_unlock_folio &&
			    test_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags))
				_debug("no unlock");
			else
				folio_unlock(folio);
		}
	}
	rcu_read_unlock();

out:
	task_io_account_read(account);
	if (rreq->netfs_ops->done)
		rreq->netfs_ops->done(rreq);
}

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
	struct netfs_inode *ctx = netfs_inode(ractl->mapping->host);
	int ret;

	_enter("%lx,%x", readahead_index(ractl), readahead_count(ractl));

	if (readahead_count(ractl) == 0)
		return;

	rreq = netfs_alloc_request(ractl->mapping, ractl->file,
				   readahead_pos(ractl),
				   readahead_length(ractl),
				   NETFS_READAHEAD);
	if (IS_ERR(rreq))
		return;

	ret = netfs_begin_cache_read(rreq, ctx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto cleanup_free;

	netfs_stat(&netfs_n_rh_readahead);
	trace_netfs_read(rreq, readahead_pos(ractl), readahead_length(ractl),
			 netfs_read_trace_readahead);

	netfs_rreq_expand(rreq, ractl);

	/* Set up the output buffer */
	iov_iter_xarray(&rreq->iter, ITER_DEST, &ractl->mapping->i_pages,
			rreq->start, rreq->len);

	/* Drop the refs on the folios here rather than in the cache or
	 * filesystem.  The locks will be dropped in netfs_rreq_unlock().
	 */
	while (readahead_folio(ractl))
		;

	netfs_begin_read(rreq, false);
	netfs_put_request(rreq, false, netfs_rreq_trace_put_return);
	return;

cleanup_free:
	netfs_put_request(rreq, false, netfs_rreq_trace_put_failed);
	return;
}
EXPORT_SYMBOL(netfs_readahead);

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
	struct folio *sink = NULL;
	int ret;

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
	if (folio_test_dirty(folio)) {
		/* Handle someone trying to read from an unflushed streaming
		 * write.  We fiddle the buffer so that a gap at the beginning
		 * and/or a gap at the end get copied to, but the middle is
		 * discarded.
		 */
		struct netfs_folio *finfo = netfs_folio_info(folio);
		struct bio_vec *bvec;
		unsigned int from = finfo->dirty_offset;
		unsigned int to = from + finfo->dirty_len;
		unsigned int off = 0, i = 0;
		size_t flen = folio_size(folio);
		size_t nr_bvec = flen / PAGE_SIZE + 2;
		size_t part;

		ret = -ENOMEM;
		bvec = kmalloc_array(nr_bvec, sizeof(*bvec), GFP_KERNEL);
		if (!bvec)
			goto discard;

		sink = folio_alloc(GFP_KERNEL, 0);
		if (!sink)
			goto discard;

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
	} else {
		iov_iter_xarray(&rreq->iter, ITER_DEST, &mapping->i_pages,
				rreq->start, rreq->len);
	}

	ret = netfs_begin_read(rreq, true);
	if (sink)
		folio_put(sink);
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
 * Multiple I/O requests from different sources will get munged together.  If
 * necessary, the readahead window can be expanded in either direction to a
 * more convenient alighment for RPC efficiency or to make storage in the cache
 * feasible.
 *
 * The calling netfs must provide a table of operations, only one of which,
 * issue_op, is mandatory.
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

	DEFINE_READAHEAD(ractl, file, NULL, mapping, index);

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

	/* Expand the request to meet caching requirements and download
	 * preferences.
	 */
	ractl._nr_pages = folio_nr_pages(folio);
	netfs_rreq_expand(rreq, &ractl);

	/* Set up the output buffer */
	iov_iter_xarray(&rreq->iter, ITER_DEST, &mapping->i_pages,
			rreq->start, rreq->len);

	/* We hold the folio locks, so we can drop the references */
	folio_get(folio);
	while (readahead_folio(&ractl))
		;

	ret = netfs_begin_read(rreq, true);
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
	iov_iter_xarray(&rreq->iter, ITER_DEST, &mapping->i_pages,
			rreq->start, rreq->len);

	ret = netfs_begin_read(rreq, true);
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
