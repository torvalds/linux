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

static void netfs_set_group(struct folio *folio, struct netfs_group *netfs_group)
{
	if (netfs_group && !folio_get_private(folio))
		folio_attach_private(folio, netfs_get_group(netfs_group));
}

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

	if (pos >= ctx->remote_i_size)
		return NETFS_MODIFY_AND_CLEAR;

	if (!maybe_trouble && offset == 0 && len >= flen)
		return NETFS_WHOLE_FOLIO_MODIFY;

	if (file->f_mode & FMODE_READ)
		return NETFS_JUST_PREFETCH;

	if (netfs_is_cache_enabled(ctx))
		return NETFS_JUST_PREFETCH;

	if (!finfo)
		return NETFS_STREAMING_WRITE;

	/* We can continue a streaming write only if it continues on from the
	 * previous.  If it overlaps, we must flush lest we suffer a partial
	 * copy and disjoint dirty regions.
	 */
	if (offset == finfo->dirty_offset + finfo->dirty_len)
		return NETFS_STREAMING_WRITE_CONT;
	return NETFS_FLUSH_CONTENT;
}

/*
 * Grab a folio for writing and lock it.
 */
static struct folio *netfs_grab_folio_for_write(struct address_space *mapping,
						loff_t pos, size_t part)
{
	pgoff_t index = pos / PAGE_SIZE;

	return __filemap_get_folio(mapping, index, FGP_WRITEBEGIN,
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
	struct netfs_folio *finfo;
	struct folio *folio;
	enum netfs_how_to_modify howto;
	enum netfs_folio_trace trace;
	unsigned int bdp_flags = (iocb->ki_flags & IOCB_SYNC) ? 0: BDP_ASYNC;
	ssize_t written = 0, ret;
	loff_t i_size, pos = iocb->ki_pos, from, to;
	size_t max_chunk = PAGE_SIZE << MAX_PAGECACHE_ORDER;
	bool maybe_trouble = false;

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

		ret = -ENOMEM;
		folio = netfs_grab_folio_for_write(mapping, pos, part);
		if (!folio)
			break;

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
			     howto, folio_index(folio));
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

		folio_mark_dirty(folio);
	retry:
		folio_unlock(folio);
		folio_put(folio);
		folio = NULL;

		cond_resched();
	} while (iov_iter_count(iter));

out:
	if (likely(written)) {
		/* Flush and wait for a write that requires immediate synchronisation. */
		if (iocb->ki_flags & (IOCB_DSYNC | IOCB_SYNC)) {
			_debug("dsync");
			ret = filemap_fdatawait_range(mapping, iocb->ki_pos,
						      iocb->ki_pos + written);
		}

		iocb->ki_pos += written;
	}

	_leave(" = %zd [%zd]", written, ret);
	return written ? written : ret;

error_folio_unlock:
	folio_unlock(folio);
	folio_put(folio);
	goto out;
}
EXPORT_SYMBOL(netfs_perform_write);
