// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem high-level write support.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/pagevec.h>
#include "internal.h"

/*
 * Determined write method.  Adjust netfs_folio_traces if this is changed.
 */
enum netfs_how_to_modify {
	NETFS_FOLIO_IS_UPTODATE,	/* Folio is uptodate already */
	NETFS_JUST_PREFETCH,		/* We have to read the folio anyway */
	NETFS_WHOLE_FOLIO_MODIFY,	/* We're going to overwrite the whole folio */
	NETFS_MODIFY_AND_CLEAR,		/* We can assume there is no data to be downloaded. */
	NETFS_STREAMING_WRITE,		/* Store incomplete data in non-uptodate page. */
	NETFS_STREAMING_WRITE_CONT,	/* Continue streaming write. */
	NETFS_FLUSH_CONTENT,		/* Flush incompatible content. */
};

static void netfs_cleanup_buffered_write(struct netfs_io_request *wreq);

static void netfs_set_group(struct folio *folio, struct netfs_group *netfs_group)
{
	if (netfs_group && !folio_get_private(folio))
		folio_attach_private(folio, netfs_get_group(netfs_group));
}

#if IS_ENABLED(CONFIG_FSCACHE)
static void netfs_folio_start_fscache(bool caching, struct folio *folio)
{
	if (caching)
		folio_start_fscache(folio);
}
#else
static void netfs_folio_start_fscache(bool caching, struct folio *folio)
{
}
#endif

/*
 * Decide how we should modify a folio.  We might be attempting to do
 * write-streaming, in which case we don't want to a local RMW cycle if we can
 * avoid it.  If we're doing local caching or content crypto, we award that
 * priority over avoiding RMW.  If the file is open readably, then we also
 * assume that we may want to read what we wrote.
 */
static enum netfs_how_to_modify netfs_how_to_modify(struct netfs_inode *ctx,
						    struct file *file,
						    struct folio *folio,
						    void *netfs_group,
						    size_t flen,
						    size_t offset,
						    size_t len,
						    bool maybe_trouble)
{
	struct netfs_folio *finfo = netfs_folio_info(folio);
	loff_t pos = folio_file_pos(folio);

	_enter("");

	if (netfs_folio_group(folio) != netfs_group)
		return NETFS_FLUSH_CONTENT;

	if (folio_test_uptodate(folio))
		return NETFS_FOLIO_IS_UPTODATE;

	if (pos >= ctx->zero_point)
		return NETFS_MODIFY_AND_CLEAR;

	if (!maybe_trouble && offset == 0 && len >= flen)
		return NETFS_WHOLE_FOLIO_MODIFY;

	if (file->f_mode & FMODE_READ)
		goto no_write_streaming;
	if (test_bit(NETFS_ICTX_NO_WRITE_STREAMING, &ctx->flags))
		goto no_write_streaming;

	if (netfs_is_cache_enabled(ctx)) {
		/* We don't want to get a streaming write on a file that loses
		 * caching service temporarily because the backing store got
		 * culled.
		 */
		if (!test_bit(NETFS_ICTX_NO_WRITE_STREAMING, &ctx->flags))
			set_bit(NETFS_ICTX_NO_WRITE_STREAMING, &ctx->flags);
		goto no_write_streaming;
	}

	if (!finfo)
		return NETFS_STREAMING_WRITE;

	/* We can continue a streaming write only if it continues on from the
	 * previous.  If it overlaps, we must flush lest we suffer a partial
	 * copy and disjoint dirty regions.
	 */
	if (offset == finfo->dirty_offset + finfo->dirty_len)
		return NETFS_STREAMING_WRITE_CONT;
	return NETFS_FLUSH_CONTENT;

no_write_streaming:
	if (finfo) {
		netfs_stat(&netfs_n_wh_wstream_conflict);
		return NETFS_FLUSH_CONTENT;
	}
	return NETFS_JUST_PREFETCH;
}

/*
 * Grab a folio for writing and lock it.  Attempt to allocate as large a folio
 * as possible to hold as much of the remaining length as possible in one go.
 */
static struct folio *netfs_grab_folio_for_write(struct address_space *mapping,
						loff_t pos, size_t part)
{
	pgoff_t index = pos / PAGE_SIZE;
	fgf_t fgp_flags = FGP_WRITEBEGIN;

	if (mapping_large_folio_support(mapping))
		fgp_flags |= fgf_set_order(pos % PAGE_SIZE + part);

	return __filemap_get_folio(mapping, index, fgp_flags,
				   mapping_gfp_mask(mapping));
}

/**
 * netfs_perform_write - Copy data into the pagecache.
 * @iocb: The operation parameters
 * @iter: The source buffer
 * @netfs_group: Grouping for dirty pages (eg. ceph snaps).
 *
 * Copy data into pagecache pages attached to the inode specified by @iocb.
 * The caller must hold appropriate inode locks.
 *
 * Dirty pages are tagged with a netfs_folio struct if they're not up to date
 * to indicate the range modified.  Dirty pages may also be tagged with a
 * netfs-specific grouping such that data from an old group gets flushed before
 * a new one is started.
 */
ssize_t netfs_perform_write(struct kiocb *iocb, struct iov_iter *iter,
			    struct netfs_group *netfs_group)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	struct netfs_inode *ctx = netfs_inode(inode);
	struct writeback_control wbc = {
		.sync_mode	= WB_SYNC_NONE,
		.for_sync	= true,
		.nr_to_write	= LONG_MAX,
		.range_start	= iocb->ki_pos,
		.range_end	= iocb->ki_pos + iter->count,
	};
	struct netfs_io_request *wreq = NULL;
	struct netfs_folio *finfo;
	struct folio *folio;
	enum netfs_how_to_modify howto;
	enum netfs_folio_trace trace;
	unsigned int bdp_flags = (iocb->ki_flags & IOCB_SYNC) ? 0: BDP_ASYNC;
	ssize_t written = 0, ret;
	loff_t i_size, pos = iocb->ki_pos, from, to;
	size_t max_chunk = PAGE_SIZE << MAX_PAGECACHE_ORDER;
	bool maybe_trouble = false;

	if (unlikely(test_bit(NETFS_ICTX_WRITETHROUGH, &ctx->flags) ||
		     iocb->ki_flags & (IOCB_DSYNC | IOCB_SYNC))
	    ) {
		if (pos < i_size_read(inode)) {
			ret = filemap_write_and_wait_range(mapping, pos, pos + iter->count);
			if (ret < 0) {
				goto out;
			}
		}

		wbc_attach_fdatawrite_inode(&wbc, mapping->host);

		wreq = netfs_begin_writethrough(iocb, iter->count);
		if (IS_ERR(wreq)) {
			wbc_detach_inode(&wbc);
			ret = PTR_ERR(wreq);
			wreq = NULL;
			goto out;
		}
		if (!is_sync_kiocb(iocb))
			wreq->iocb = iocb;
		wreq->cleanup = netfs_cleanup_buffered_write;
	}

	do {
		size_t flen;
		size_t offset;	/* Offset into pagecache folio */
		size_t part;	/* Bytes to write to folio */
		size_t copied;	/* Bytes copied from user */

		ret = balance_dirty_pages_ratelimited_flags(mapping, bdp_flags);
		if (unlikely(ret < 0))
			break;

		offset = pos & (max_chunk - 1);
		part = min(max_chunk - offset, iov_iter_count(iter));

		/* Bring in the user pages that we will copy from _first_ lest
		 * we hit a nasty deadlock on copying from the same page as
		 * we're writing to, without it being marked uptodate.
		 *
		 * Not only is this an optimisation, but it is also required to
		 * check that the address is actually valid, when atomic
		 * usercopies are used below.
		 *
		 * We rely on the page being held onto long enough by the LRU
		 * that we can grab it below if this causes it to be read.
		 */
		ret = -EFAULT;
		if (unlikely(fault_in_iov_iter_readable(iter, part) == part))
			break;

		folio = netfs_grab_folio_for_write(mapping, pos, part);
		if (IS_ERR(folio)) {
			ret = PTR_ERR(folio);
			break;
		}

		flen = folio_size(folio);
		offset = pos & (flen - 1);
		part = min_t(size_t, flen - offset, part);

		if (signal_pending(current)) {
			ret = written ? -EINTR : -ERESTARTSYS;
			goto error_folio_unlock;
		}

		/* See if we need to prefetch the area we're going to modify.
		 * We need to do this before we get a lock on the folio in case
		 * there's more than one writer competing for the same cache
		 * block.
		 */
		howto = netfs_how_to_modify(ctx, file, folio, netfs_group,
					    flen, offset, part, maybe_trouble);
		_debug("howto %u", howto);
		switch (howto) {
		case NETFS_JUST_PREFETCH:
			ret = netfs_prefetch_for_write(file, folio, offset, part);
			if (ret < 0) {
				_debug("prefetch = %zd", ret);
				goto error_folio_unlock;
			}
			break;
		case NETFS_FOLIO_IS_UPTODATE:
		case NETFS_WHOLE_FOLIO_MODIFY:
		case NETFS_STREAMING_WRITE_CONT:
			break;
		case NETFS_MODIFY_AND_CLEAR:
			zero_user_segment(&folio->page, 0, offset);
			break;
		case NETFS_STREAMING_WRITE:
			ret = -EIO;
			if (WARN_ON(folio_get_private(folio)))
				goto error_folio_unlock;
			break;
		case NETFS_FLUSH_CONTENT:
			trace_netfs_folio(folio, netfs_flush_content);
			from = folio_pos(folio);
			to = from + folio_size(folio) - 1;
			folio_unlock(folio);
			folio_put(folio);
			ret = filemap_write_and_wait_range(mapping, from, to);
			if (ret < 0)
				goto error_folio_unlock;
			continue;
		}

		if (mapping_writably_mapped(mapping))
			flush_dcache_folio(folio);

		copied = copy_folio_from_iter_atomic(folio, offset, part, iter);

		flush_dcache_folio(folio);

		/* Deal with a (partially) failed copy */
		if (copied == 0) {
			ret = -EFAULT;
			goto error_folio_unlock;
		}

		trace = (enum netfs_folio_trace)howto;
		switch (howto) {
		case NETFS_FOLIO_IS_UPTODATE:
		case NETFS_JUST_PREFETCH:
			netfs_set_group(folio, netfs_group);
			break;
		case NETFS_MODIFY_AND_CLEAR:
			zero_user_segment(&folio->page, offset + copied, flen);
			netfs_set_group(folio, netfs_group);
			folio_mark_uptodate(folio);
			break;
		case NETFS_WHOLE_FOLIO_MODIFY:
			if (unlikely(copied < part)) {
				maybe_trouble = true;
				iov_iter_revert(iter, copied);
				copied = 0;
				goto retry;
			}
			netfs_set_group(folio, netfs_group);
			folio_mark_uptodate(folio);
			break;
		case NETFS_STREAMING_WRITE:
			if (offset == 0 && copied == flen) {
				netfs_set_group(folio, netfs_group);
				folio_mark_uptodate(folio);
				trace = netfs_streaming_filled_page;
				break;
			}
			finfo = kzalloc(sizeof(*finfo), GFP_KERNEL);
			if (!finfo) {
				iov_iter_revert(iter, copied);
				ret = -ENOMEM;
				goto error_folio_unlock;
			}
			finfo->netfs_group = netfs_get_group(netfs_group);
			finfo->dirty_offset = offset;
			finfo->dirty_len = copied;
			folio_attach_private(folio, (void *)((unsigned long)finfo |
							     NETFS_FOLIO_INFO));
			break;
		case NETFS_STREAMING_WRITE_CONT:
			finfo = netfs_folio_info(folio);
			finfo->dirty_len += copied;
			if (finfo->dirty_offset == 0 && finfo->dirty_len == flen) {
				if (finfo->netfs_group)
					folio_change_private(folio, finfo->netfs_group);
				else
					folio_detach_private(folio);
				folio_mark_uptodate(folio);
				kfree(finfo);
				trace = netfs_streaming_cont_filled_page;
			}
			break;
		default:
			WARN(true, "Unexpected modify type %u ix=%lx\n",
			     howto, folio->index);
			ret = -EIO;
			goto error_folio_unlock;
		}

		trace_netfs_folio(folio, trace);

		/* Update the inode size if we moved the EOF marker */
		i_size = i_size_read(inode);
		pos += copied;
		if (pos > i_size) {
			if (ctx->ops->update_i_size) {
				ctx->ops->update_i_size(inode, pos);
			} else {
				i_size_write(inode, pos);
#if IS_ENABLED(CONFIG_FSCACHE)
				fscache_update_cookie(ctx->cache, NULL, &pos);
#endif
			}
		}
		written += copied;

		if (likely(!wreq)) {
			folio_mark_dirty(folio);
		} else {
			if (folio_test_dirty(folio))
				/* Sigh.  mmap. */
				folio_clear_dirty_for_io(folio);
			/* We make multiple writes to the folio... */
			if (!folio_test_writeback(folio)) {
				folio_wait_fscache(folio);
				folio_start_writeback(folio);
				folio_start_fscache(folio);
				if (wreq->iter.count == 0)
					trace_netfs_folio(folio, netfs_folio_trace_wthru);
				else
					trace_netfs_folio(folio, netfs_folio_trace_wthru_plus);
			}
			netfs_advance_writethrough(wreq, copied,
						   offset + copied == flen);
		}
	retry:
		folio_unlock(folio);
		folio_put(folio);
		folio = NULL;

		cond_resched();
	} while (iov_iter_count(iter));

out:
	if (unlikely(wreq)) {
		ret = netfs_end_writethrough(wreq, iocb);
		wbc_detach_inode(&wbc);
		if (ret == -EIOCBQUEUED)
			return ret;
	}

	iocb->ki_pos += written;
	_leave(" = %zd [%zd]", written, ret);
	return written ? written : ret;

error_folio_unlock:
	folio_unlock(folio);
	folio_put(folio);
	goto out;
}
EXPORT_SYMBOL(netfs_perform_write);

/**
 * netfs_buffered_write_iter_locked - write data to a file
 * @iocb:	IO state structure (file, offset, etc.)
 * @from:	iov_iter with data to write
 * @netfs_group: Grouping for dirty pages (eg. ceph snaps).
 *
 * This function does all the work needed for actually writing data to a
 * file. It does all basic checks, removes SUID from the file, updates
 * modification times and calls proper subroutines depending on whether we
 * do direct IO or a standard buffered write.
 *
 * The caller must hold appropriate locks around this function and have called
 * generic_write_checks() already.  The caller is also responsible for doing
 * any necessary syncing afterwards.
 *
 * This function does *not* take care of syncing data in case of O_SYNC write.
 * A caller has to handle it. This is mainly due to the fact that we want to
 * avoid syncing under i_rwsem.
 *
 * Return:
 * * number of bytes written, even for truncated writes
 * * negative error code if no data has been written at all
 */
ssize_t netfs_buffered_write_iter_locked(struct kiocb *iocb, struct iov_iter *from,
					 struct netfs_group *netfs_group)
{
	struct file *file = iocb->ki_filp;
	ssize_t ret;

	trace_netfs_write_iter(iocb, from);

	ret = file_remove_privs(file);
	if (ret)
		return ret;

	ret = file_update_time(file);
	if (ret)
		return ret;

	return netfs_perform_write(iocb, from, netfs_group);
}
EXPORT_SYMBOL(netfs_buffered_write_iter_locked);

/**
 * netfs_file_write_iter - write data to a file
 * @iocb: IO state structure
 * @from: iov_iter with data to write
 *
 * Perform a write to a file, writing into the pagecache if possible and doing
 * an unbuffered write instead if not.
 *
 * Return:
 * * Negative error code if no data has been written at all of
 *   vfs_fsync_range() failed for a synchronous write
 * * Number of bytes written, even for truncated writes
 */
ssize_t netfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct netfs_inode *ictx = netfs_inode(inode);
	ssize_t ret;

	_enter("%llx,%zx,%llx", iocb->ki_pos, iov_iter_count(from), i_size_read(inode));

	if ((iocb->ki_flags & IOCB_DIRECT) ||
	    test_bit(NETFS_ICTX_UNBUFFERED, &ictx->flags))
		return netfs_unbuffered_write_iter(iocb, from);

	ret = netfs_start_io_write(inode);
	if (ret < 0)
		return ret;

	ret = generic_write_checks(iocb, from);
	if (ret > 0)
		ret = netfs_buffered_write_iter_locked(iocb, from, NULL);
	netfs_end_io_write(inode);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}
EXPORT_SYMBOL(netfs_file_write_iter);

/*
 * Notification that a previously read-only page is about to become writable.
 * Note that the caller indicates a single page of a multipage folio.
 */
vm_fault_t netfs_page_mkwrite(struct vm_fault *vmf, struct netfs_group *netfs_group)
{
	struct folio *folio = page_folio(vmf->page);
	struct file *file = vmf->vma->vm_file;
	struct inode *inode = file_inode(file);
	vm_fault_t ret = VM_FAULT_RETRY;
	int err;

	_enter("%lx", folio->index);

	sb_start_pagefault(inode->i_sb);

	if (folio_wait_writeback_killable(folio))
		goto out;

	if (folio_lock_killable(folio) < 0)
		goto out;

	/* Can we see a streaming write here? */
	if (WARN_ON(!folio_test_uptodate(folio))) {
		ret = VM_FAULT_SIGBUS | VM_FAULT_LOCKED;
		goto out;
	}

	if (netfs_folio_group(folio) != netfs_group) {
		folio_unlock(folio);
		err = filemap_fdatawait_range(inode->i_mapping,
					      folio_pos(folio),
					      folio_pos(folio) + folio_size(folio));
		switch (err) {
		case 0:
			ret = VM_FAULT_RETRY;
			goto out;
		case -ENOMEM:
			ret = VM_FAULT_OOM;
			goto out;
		default:
			ret = VM_FAULT_SIGBUS;
			goto out;
		}
	}

	if (folio_test_dirty(folio))
		trace_netfs_folio(folio, netfs_folio_trace_mkwrite_plus);
	else
		trace_netfs_folio(folio, netfs_folio_trace_mkwrite);
	netfs_set_group(folio, netfs_group);
	file_update_time(file);
	ret = VM_FAULT_LOCKED;
out:
	sb_end_pagefault(inode->i_sb);
	return ret;
}
EXPORT_SYMBOL(netfs_page_mkwrite);

/*
 * Kill all the pages in the given range
 */
static void netfs_kill_pages(struct address_space *mapping,
			     loff_t start, loff_t len)
{
	struct folio *folio;
	pgoff_t index = start / PAGE_SIZE;
	pgoff_t last = (start + len - 1) / PAGE_SIZE, next;

	_enter("%llx-%llx", start, start + len - 1);

	do {
		_debug("kill %lx (to %lx)", index, last);

		folio = filemap_get_folio(mapping, index);
		if (IS_ERR(folio)) {
			next = index + 1;
			continue;
		}

		next = folio_next_index(folio);

		trace_netfs_folio(folio, netfs_folio_trace_kill);
		folio_clear_uptodate(folio);
		if (folio_test_fscache(folio))
			folio_end_fscache(folio);
		folio_end_writeback(folio);
		folio_lock(folio);
		generic_error_remove_folio(mapping, folio);
		folio_unlock(folio);
		folio_put(folio);

	} while (index = next, index <= last);

	_leave("");
}

/*
 * Redirty all the pages in a given range.
 */
static void netfs_redirty_pages(struct address_space *mapping,
				loff_t start, loff_t len)
{
	struct folio *folio;
	pgoff_t index = start / PAGE_SIZE;
	pgoff_t last = (start + len - 1) / PAGE_SIZE, next;

	_enter("%llx-%llx", start, start + len - 1);

	do {
		_debug("redirty %llx @%llx", len, start);

		folio = filemap_get_folio(mapping, index);
		if (IS_ERR(folio)) {
			next = index + 1;
			continue;
		}

		next = folio_next_index(folio);
		trace_netfs_folio(folio, netfs_folio_trace_redirty);
		filemap_dirty_folio(mapping, folio);
		if (folio_test_fscache(folio))
			folio_end_fscache(folio);
		folio_end_writeback(folio);
		folio_put(folio);
	} while (index = next, index <= last);

	balance_dirty_pages_ratelimited(mapping);

	_leave("");
}

/*
 * Completion of write to server
 */
static void netfs_pages_written_back(struct netfs_io_request *wreq)
{
	struct address_space *mapping = wreq->mapping;
	struct netfs_folio *finfo;
	struct netfs_group *group = NULL;
	struct folio *folio;
	pgoff_t last;
	int gcount = 0;

	XA_STATE(xas, &mapping->i_pages, wreq->start / PAGE_SIZE);

	_enter("%llx-%llx", wreq->start, wreq->start + wreq->len);

	rcu_read_lock();

	last = (wreq->start + wreq->len - 1) / PAGE_SIZE;
	xas_for_each(&xas, folio, last) {
		WARN(!folio_test_writeback(folio),
		     "bad %zx @%llx page %lx %lx\n",
		     wreq->len, wreq->start, folio->index, last);

		if ((finfo = netfs_folio_info(folio))) {
			/* Streaming writes cannot be redirtied whilst under
			 * writeback, so discard the streaming record.
			 */
			folio_detach_private(folio);
			group = finfo->netfs_group;
			gcount++;
			trace_netfs_folio(folio, netfs_folio_trace_clear_s);
			kfree(finfo);
		} else if ((group = netfs_folio_group(folio))) {
			/* Need to detach the group pointer if the page didn't
			 * get redirtied.  If it has been redirtied, then it
			 * must be within the same group.
			 */
			if (folio_test_dirty(folio)) {
				trace_netfs_folio(folio, netfs_folio_trace_redirtied);
				goto end_wb;
			}
			if (folio_trylock(folio)) {
				if (!folio_test_dirty(folio)) {
					folio_detach_private(folio);
					gcount++;
					trace_netfs_folio(folio, netfs_folio_trace_clear_g);
				} else {
					trace_netfs_folio(folio, netfs_folio_trace_redirtied);
				}
				folio_unlock(folio);
				goto end_wb;
			}

			xas_pause(&xas);
			rcu_read_unlock();
			folio_lock(folio);
			if (!folio_test_dirty(folio)) {
				folio_detach_private(folio);
				gcount++;
				trace_netfs_folio(folio, netfs_folio_trace_clear_g);
			} else {
				trace_netfs_folio(folio, netfs_folio_trace_redirtied);
			}
			folio_unlock(folio);
			rcu_read_lock();
		} else {
			trace_netfs_folio(folio, netfs_folio_trace_clear);
		}
	end_wb:
		if (folio_test_fscache(folio))
			folio_end_fscache(folio);
		xas_advance(&xas, folio_next_index(folio) - 1);
		folio_end_writeback(folio);
	}

	rcu_read_unlock();
	netfs_put_group_many(group, gcount);
	_leave("");
}

/*
 * Deal with the disposition of the folios that are under writeback to close
 * out the operation.
 */
static void netfs_cleanup_buffered_write(struct netfs_io_request *wreq)
{
	struct address_space *mapping = wreq->mapping;

	_enter("");

	switch (wreq->error) {
	case 0:
		netfs_pages_written_back(wreq);
		break;

	default:
		pr_notice("R=%08x Unexpected error %d\n", wreq->debug_id, wreq->error);
		fallthrough;
	case -EACCES:
	case -EPERM:
	case -ENOKEY:
	case -EKEYEXPIRED:
	case -EKEYREJECTED:
	case -EKEYREVOKED:
	case -ENETRESET:
	case -EDQUOT:
	case -ENOSPC:
		netfs_redirty_pages(mapping, wreq->start, wreq->len);
		break;

	case -EROFS:
	case -EIO:
	case -EREMOTEIO:
	case -EFBIG:
	case -ENOENT:
	case -ENOMEDIUM:
	case -ENXIO:
		netfs_kill_pages(mapping, wreq->start, wreq->len);
		break;
	}

	if (wreq->error)
		mapping_set_error(mapping, wreq->error);
	if (wreq->netfs_ops->done)
		wreq->netfs_ops->done(wreq);
}

/*
 * Extend the region to be written back to include subsequent contiguously
 * dirty pages if possible, but don't sleep while doing so.
 *
 * If this page holds new content, then we can include filler zeros in the
 * writeback.
 */
static void netfs_extend_writeback(struct address_space *mapping,
				   struct netfs_group *group,
				   struct xa_state *xas,
				   long *_count,
				   loff_t start,
				   loff_t max_len,
				   bool caching,
				   size_t *_len,
				   size_t *_top)
{
	struct netfs_folio *finfo;
	struct folio_batch fbatch;
	struct folio *folio;
	unsigned int i;
	pgoff_t index = (start + *_len) / PAGE_SIZE;
	size_t len;
	void *priv;
	bool stop = true;

	folio_batch_init(&fbatch);

	do {
		/* Firstly, we gather up a batch of contiguous dirty pages
		 * under the RCU read lock - but we can't clear the dirty flags
		 * there if any of those pages are mapped.
		 */
		rcu_read_lock();

		xas_for_each(xas, folio, ULONG_MAX) {
			stop = true;
			if (xas_retry(xas, folio))
				continue;
			if (xa_is_value(folio))
				break;
			if (folio->index != index) {
				xas_reset(xas);
				break;
			}

			if (!folio_try_get_rcu(folio)) {
				xas_reset(xas);
				continue;
			}

			/* Has the folio moved or been split? */
			if (unlikely(folio != xas_reload(xas))) {
				folio_put(folio);
				xas_reset(xas);
				break;
			}

			if (!folio_trylock(folio)) {
				folio_put(folio);
				xas_reset(xas);
				break;
			}
			if (!folio_test_dirty(folio) ||
			    folio_test_writeback(folio) ||
			    folio_test_fscache(folio)) {
				folio_unlock(folio);
				folio_put(folio);
				xas_reset(xas);
				break;
			}

			stop = false;
			len = folio_size(folio);
			priv = folio_get_private(folio);
			if ((const struct netfs_group *)priv != group) {
				stop = true;
				finfo = netfs_folio_info(folio);
				if (finfo->netfs_group != group ||
				    finfo->dirty_offset > 0) {
					folio_unlock(folio);
					folio_put(folio);
					xas_reset(xas);
					break;
				}
				len = finfo->dirty_len;
			}

			*_top += folio_size(folio);
			index += folio_nr_pages(folio);
			*_count -= folio_nr_pages(folio);
			*_len += len;
			if (*_len >= max_len || *_count <= 0)
				stop = true;

			if (!folio_batch_add(&fbatch, folio))
				break;
			if (stop)
				break;
		}

		xas_pause(xas);
		rcu_read_unlock();

		/* Now, if we obtained any folios, we can shift them to being
		 * writable and mark them for caching.
		 */
		if (!folio_batch_count(&fbatch))
			break;

		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			folio = fbatch.folios[i];
			trace_netfs_folio(folio, netfs_folio_trace_store_plus);

			if (!folio_clear_dirty_for_io(folio))
				BUG();
			folio_start_writeback(folio);
			netfs_folio_start_fscache(caching, folio);
			folio_unlock(folio);
		}

		folio_batch_release(&fbatch);
		cond_resched();
	} while (!stop);
}

/*
 * Synchronously write back the locked page and any subsequent non-locked dirty
 * pages.
 */
static ssize_t netfs_write_back_from_locked_folio(struct address_space *mapping,
						  struct writeback_control *wbc,
						  struct netfs_group *group,
						  struct xa_state *xas,
						  struct folio *folio,
						  unsigned long long start,
						  unsigned long long end)
{
	struct netfs_io_request *wreq;
	struct netfs_folio *finfo;
	struct netfs_inode *ctx = netfs_inode(mapping->host);
	unsigned long long i_size = i_size_read(&ctx->inode);
	size_t len, max_len;
	bool caching = netfs_is_cache_enabled(ctx);
	long count = wbc->nr_to_write;
	int ret;

	_enter(",%lx,%llx-%llx,%u", folio->index, start, end, caching);

	wreq = netfs_alloc_request(mapping, NULL, start, folio_size(folio),
				   NETFS_WRITEBACK);
	if (IS_ERR(wreq)) {
		folio_unlock(folio);
		return PTR_ERR(wreq);
	}

	if (!folio_clear_dirty_for_io(folio))
		BUG();
	folio_start_writeback(folio);
	netfs_folio_start_fscache(caching, folio);

	count -= folio_nr_pages(folio);

	/* Find all consecutive lockable dirty pages that have contiguous
	 * written regions, stopping when we find a page that is not
	 * immediately lockable, is not dirty or is missing, or we reach the
	 * end of the range.
	 */
	trace_netfs_folio(folio, netfs_folio_trace_store);

	len = wreq->len;
	finfo = netfs_folio_info(folio);
	if (finfo) {
		start += finfo->dirty_offset;
		if (finfo->dirty_offset + finfo->dirty_len != len) {
			len = finfo->dirty_len;
			goto cant_expand;
		}
		len = finfo->dirty_len;
	}

	if (start < i_size) {
		/* Trim the write to the EOF; the extra data is ignored.  Also
		 * put an upper limit on the size of a single storedata op.
		 */
		max_len = 65536 * 4096;
		max_len = min_t(unsigned long long, max_len, end - start + 1);
		max_len = min_t(unsigned long long, max_len, i_size - start);

		if (len < max_len)
			netfs_extend_writeback(mapping, group, xas, &count, start,
					       max_len, caching, &len, &wreq->upper_len);
	}

cant_expand:
	len = min_t(unsigned long long, len, i_size - start);

	/* We now have a contiguous set of dirty pages, each with writeback
	 * set; the first page is still locked at this point, but all the rest
	 * have been unlocked.
	 */
	folio_unlock(folio);
	wreq->start = start;
	wreq->len = len;

	if (start < i_size) {
		_debug("write back %zx @%llx [%llx]", len, start, i_size);

		/* Speculatively write to the cache.  We have to fix this up
		 * later if the store fails.
		 */
		wreq->cleanup = netfs_cleanup_buffered_write;

		iov_iter_xarray(&wreq->iter, ITER_SOURCE, &mapping->i_pages, start,
				wreq->upper_len);
		__set_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags);
		ret = netfs_begin_write(wreq, true, netfs_write_trace_writeback);
		if (ret == 0 || ret == -EIOCBQUEUED)
			wbc->nr_to_write -= len / PAGE_SIZE;
	} else {
		_debug("write discard %zx @%llx [%llx]", len, start, i_size);

		/* The dirty region was entirely beyond the EOF. */
		fscache_clear_page_bits(mapping, start, len, caching);
		netfs_pages_written_back(wreq);
		ret = 0;
	}

	netfs_put_request(wreq, false, netfs_rreq_trace_put_return);
	_leave(" = 1");
	return 1;
}

/*
 * Write a region of pages back to the server
 */
static ssize_t netfs_writepages_begin(struct address_space *mapping,
				      struct writeback_control *wbc,
				      struct netfs_group *group,
				      struct xa_state *xas,
				      unsigned long long *_start,
				      unsigned long long end)
{
	const struct netfs_folio *finfo;
	struct folio *folio;
	unsigned long long start = *_start;
	ssize_t ret;
	void *priv;
	int skips = 0;

	_enter("%llx,%llx,", start, end);

search_again:
	/* Find the first dirty page in the group. */
	rcu_read_lock();

	for (;;) {
		folio = xas_find_marked(xas, end / PAGE_SIZE, PAGECACHE_TAG_DIRTY);
		if (xas_retry(xas, folio) || xa_is_value(folio))
			continue;
		if (!folio)
			break;

		if (!folio_try_get_rcu(folio)) {
			xas_reset(xas);
			continue;
		}

		if (unlikely(folio != xas_reload(xas))) {
			folio_put(folio);
			xas_reset(xas);
			continue;
		}

		/* Skip any dirty folio that's not in the group of interest. */
		priv = folio_get_private(folio);
		if ((const struct netfs_group *)priv != group) {
			finfo = netfs_folio_info(folio);
			if (finfo->netfs_group != group) {
				folio_put(folio);
				continue;
			}
		}

		xas_pause(xas);
		break;
	}
	rcu_read_unlock();
	if (!folio)
		return 0;

	start = folio_pos(folio); /* May regress with THPs */

	_debug("wback %lx", folio->index);

	/* At this point we hold neither the i_pages lock nor the page lock:
	 * the page may be truncated or invalidated (changing page->mapping to
	 * NULL), or even swizzled back from swapper_space to tmpfs file
	 * mapping
	 */
lock_again:
	if (wbc->sync_mode != WB_SYNC_NONE) {
		ret = folio_lock_killable(folio);
		if (ret < 0)
			return ret;
	} else {
		if (!folio_trylock(folio))
			goto search_again;
	}

	if (folio->mapping != mapping ||
	    !folio_test_dirty(folio)) {
		start += folio_size(folio);
		folio_unlock(folio);
		goto search_again;
	}

	if (folio_test_writeback(folio) ||
	    folio_test_fscache(folio)) {
		folio_unlock(folio);
		if (wbc->sync_mode != WB_SYNC_NONE) {
			folio_wait_writeback(folio);
#ifdef CONFIG_FSCACHE
			folio_wait_fscache(folio);
#endif
			goto lock_again;
		}

		start += folio_size(folio);
		if (wbc->sync_mode == WB_SYNC_NONE) {
			if (skips >= 5 || need_resched()) {
				ret = 0;
				goto out;
			}
			skips++;
		}
		goto search_again;
	}

	ret = netfs_write_back_from_locked_folio(mapping, wbc, group, xas,
						 folio, start, end);
out:
	if (ret > 0)
		*_start = start + ret;
	_leave(" = %zd [%llx]", ret, *_start);
	return ret;
}

/*
 * Write a region of pages back to the server
 */
static int netfs_writepages_region(struct address_space *mapping,
				   struct writeback_control *wbc,
				   struct netfs_group *group,
				   unsigned long long *_start,
				   unsigned long long end)
{
	ssize_t ret;

	XA_STATE(xas, &mapping->i_pages, *_start / PAGE_SIZE);

	do {
		ret = netfs_writepages_begin(mapping, wbc, group, &xas,
					     _start, end);
		if (ret > 0 && wbc->nr_to_write > 0)
			cond_resched();
	} while (ret > 0 && wbc->nr_to_write > 0);

	return ret > 0 ? 0 : ret;
}

/*
 * write some of the pending data back to the server
 */
int netfs_writepages(struct address_space *mapping,
		     struct writeback_control *wbc)
{
	struct netfs_group *group = NULL;
	loff_t start, end;
	int ret;

	_enter("");

	/* We have to be careful as we can end up racing with setattr()
	 * truncating the pagecache since the caller doesn't take a lock here
	 * to prevent it.
	 */

	if (wbc->range_cyclic && mapping->writeback_index) {
		start = mapping->writeback_index * PAGE_SIZE;
		ret = netfs_writepages_region(mapping, wbc, group,
					      &start, LLONG_MAX);
		if (ret < 0)
			goto out;

		if (wbc->nr_to_write <= 0) {
			mapping->writeback_index = start / PAGE_SIZE;
			goto out;
		}

		start = 0;
		end = mapping->writeback_index * PAGE_SIZE;
		mapping->writeback_index = 0;
		ret = netfs_writepages_region(mapping, wbc, group, &start, end);
		if (ret == 0)
			mapping->writeback_index = start / PAGE_SIZE;
	} else if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX) {
		start = 0;
		ret = netfs_writepages_region(mapping, wbc, group,
					      &start, LLONG_MAX);
		if (wbc->nr_to_write > 0 && ret == 0)
			mapping->writeback_index = start / PAGE_SIZE;
	} else {
		start = wbc->range_start;
		ret = netfs_writepages_region(mapping, wbc, group,
					      &start, wbc->range_end);
	}

out:
	_leave(" = %d", ret);
	return ret;
}
EXPORT_SYMBOL(netfs_writepages);

/*
 * Deal with the disposition of a laundered folio.
 */
static void netfs_cleanup_launder_folio(struct netfs_io_request *wreq)
{
	if (wreq->error) {
		pr_notice("R=%08x Laundering error %d\n", wreq->debug_id, wreq->error);
		mapping_set_error(wreq->mapping, wreq->error);
	}
}

/**
 * netfs_launder_folio - Clean up a dirty folio that's being invalidated
 * @folio: The folio to clean
 *
 * This is called to write back a folio that's being invalidated when an inode
 * is getting torn down.  Ideally, writepages would be used instead.
 */
int netfs_launder_folio(struct folio *folio)
{
	struct netfs_io_request *wreq;
	struct address_space *mapping = folio->mapping;
	struct netfs_folio *finfo = netfs_folio_info(folio);
	struct netfs_group *group = netfs_folio_group(folio);
	struct bio_vec bvec;
	unsigned long long i_size = i_size_read(mapping->host);
	unsigned long long start = folio_pos(folio);
	size_t offset = 0, len;
	int ret = 0;

	if (finfo) {
		offset = finfo->dirty_offset;
		start += offset;
		len = finfo->dirty_len;
	} else {
		len = folio_size(folio);
	}
	len = min_t(unsigned long long, len, i_size - start);

	wreq = netfs_alloc_request(mapping, NULL, start, len, NETFS_LAUNDER_WRITE);
	if (IS_ERR(wreq)) {
		ret = PTR_ERR(wreq);
		goto out;
	}

	if (!folio_clear_dirty_for_io(folio))
		goto out_put;

	trace_netfs_folio(folio, netfs_folio_trace_launder);

	_debug("launder %llx-%llx", start, start + len - 1);

	/* Speculatively write to the cache.  We have to fix this up later if
	 * the store fails.
	 */
	wreq->cleanup = netfs_cleanup_launder_folio;

	bvec_set_folio(&bvec, folio, len, offset);
	iov_iter_bvec(&wreq->iter, ITER_SOURCE, &bvec, 1, len);
	__set_bit(NETFS_RREQ_UPLOAD_TO_SERVER, &wreq->flags);
	ret = netfs_begin_write(wreq, true, netfs_write_trace_launder);

out_put:
	folio_detach_private(folio);
	netfs_put_group(group);
	kfree(finfo);
	netfs_put_request(wreq, false, netfs_rreq_trace_put_return);
out:
	folio_wait_fscache(folio);
	_leave(" = %d", ret);
	return ret;
}
EXPORT_SYMBOL(netfs_launder_folio);
