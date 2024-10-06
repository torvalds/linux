// SPDX-License-Identifier: GPL-2.0-only
/* Miscellaneous routines.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/swap.h>
#include "internal.h"

/*
 * Make sure there's space in the rolling queue.
 */
struct folio_queue *netfs_buffer_make_space(struct netfs_io_request *rreq)
{
	struct folio_queue *tail = rreq->buffer_tail, *prev;
	unsigned int prev_nr_slots = 0;

	if (WARN_ON_ONCE(!rreq->buffer && tail) ||
	    WARN_ON_ONCE(rreq->buffer && !tail))
		return ERR_PTR(-EIO);

	prev = tail;
	if (prev) {
		if (!folioq_full(tail))
			return tail;
		prev_nr_slots = folioq_nr_slots(tail);
	}

	tail = kmalloc(sizeof(*tail), GFP_NOFS);
	if (!tail)
		return ERR_PTR(-ENOMEM);
	netfs_stat(&netfs_n_folioq);
	folioq_init(tail);
	tail->prev = prev;
	if (prev)
		/* [!] NOTE: After we set prev->next, the consumer is entirely
		 * at liberty to delete prev.
		 */
		WRITE_ONCE(prev->next, tail);

	rreq->buffer_tail = tail;
	if (!rreq->buffer) {
		rreq->buffer = tail;
		iov_iter_folio_queue(&rreq->io_iter, ITER_SOURCE, tail, 0, 0, 0);
	} else {
		/* Make sure we don't leave the master iterator pointing to a
		 * block that might get immediately consumed.
		 */
		if (rreq->io_iter.folioq == prev &&
		    rreq->io_iter.folioq_slot == prev_nr_slots) {
			rreq->io_iter.folioq = tail;
			rreq->io_iter.folioq_slot = 0;
		}
	}
	rreq->buffer_tail_slot = 0;
	return tail;
}

/*
 * Append a folio to the rolling queue.
 */
int netfs_buffer_append_folio(struct netfs_io_request *rreq, struct folio *folio,
			      bool needs_put)
{
	struct folio_queue *tail;
	unsigned int slot, order = folio_order(folio);

	tail = netfs_buffer_make_space(rreq);
	if (IS_ERR(tail))
		return PTR_ERR(tail);

	rreq->io_iter.count += PAGE_SIZE << order;

	slot = folioq_append(tail, folio);
	/* Store the counter after setting the slot. */
	smp_store_release(&rreq->buffer_tail_slot, slot);
	return 0;
}

/*
 * Delete the head of a rolling queue.
 */
struct folio_queue *netfs_delete_buffer_head(struct netfs_io_request *wreq)
{
	struct folio_queue *head = wreq->buffer, *next = head->next;

	if (next)
		next->prev = NULL;
	netfs_stat_d(&netfs_n_folioq);
	kfree(head);
	wreq->buffer = next;
	return next;
}

/*
 * Clear out a rolling queue.
 */
void netfs_clear_buffer(struct netfs_io_request *rreq)
{
	struct folio_queue *p;

	while ((p = rreq->buffer)) {
		rreq->buffer = p->next;
		for (int slot = 0; slot < folioq_nr_slots(p); slot++) {
			struct folio *folio = folioq_folio(p, slot);
			if (!folio)
				continue;
			if (folioq_is_marked(p, slot)) {
				trace_netfs_folio(folio, netfs_folio_trace_put);
				folio_put(folio);
			}
		}
		netfs_stat_d(&netfs_n_folioq);
		kfree(p);
	}
}

/*
 * Reset the subrequest iterator to refer just to the region remaining to be
 * read.  The iterator may or may not have been advanced by socket ops or
 * extraction ops to an extent that may or may not match the amount actually
 * read.
 */
void netfs_reset_iter(struct netfs_io_subrequest *subreq)
{
	struct iov_iter *io_iter = &subreq->io_iter;
	size_t remain = subreq->len - subreq->transferred;

	if (io_iter->count > remain)
		iov_iter_advance(io_iter, io_iter->count - remain);
	else if (io_iter->count < remain)
		iov_iter_revert(io_iter, remain - io_iter->count);
	iov_iter_truncate(&subreq->io_iter, remain);
}

/**
 * netfs_dirty_folio - Mark folio dirty and pin a cache object for writeback
 * @mapping: The mapping the folio belongs to.
 * @folio: The folio being dirtied.
 *
 * Set the dirty flag on a folio and pin an in-use cache object in memory so
 * that writeback can later write to it.  This is intended to be called from
 * the filesystem's ->dirty_folio() method.
 *
 * Return: true if the dirty flag was set on the folio, false otherwise.
 */
bool netfs_dirty_folio(struct address_space *mapping, struct folio *folio)
{
	struct inode *inode = mapping->host;
	struct netfs_inode *ictx = netfs_inode(inode);
	struct fscache_cookie *cookie = netfs_i_cookie(ictx);
	bool need_use = false;

	_enter("");

	if (!filemap_dirty_folio(mapping, folio))
		return false;
	if (!fscache_cookie_valid(cookie))
		return true;

	if (!(inode->i_state & I_PINNING_NETFS_WB)) {
		spin_lock(&inode->i_lock);
		if (!(inode->i_state & I_PINNING_NETFS_WB)) {
			inode->i_state |= I_PINNING_NETFS_WB;
			need_use = true;
		}
		spin_unlock(&inode->i_lock);

		if (need_use)
			fscache_use_cookie(cookie, true);
	}
	return true;
}
EXPORT_SYMBOL(netfs_dirty_folio);

/**
 * netfs_unpin_writeback - Unpin writeback resources
 * @inode: The inode on which the cookie resides
 * @wbc: The writeback control
 *
 * Unpin the writeback resources pinned by netfs_dirty_folio().  This is
 * intended to be called as/by the netfs's ->write_inode() method.
 */
int netfs_unpin_writeback(struct inode *inode, struct writeback_control *wbc)
{
	struct fscache_cookie *cookie = netfs_i_cookie(netfs_inode(inode));

	if (wbc->unpinned_netfs_wb)
		fscache_unuse_cookie(cookie, NULL, NULL);
	return 0;
}
EXPORT_SYMBOL(netfs_unpin_writeback);

/**
 * netfs_clear_inode_writeback - Clear writeback resources pinned by an inode
 * @inode: The inode to clean up
 * @aux: Auxiliary data to apply to the inode
 *
 * Clear any writeback resources held by an inode when the inode is evicted.
 * This must be called before clear_inode() is called.
 */
void netfs_clear_inode_writeback(struct inode *inode, const void *aux)
{
	struct fscache_cookie *cookie = netfs_i_cookie(netfs_inode(inode));

	if (inode->i_state & I_PINNING_NETFS_WB) {
		loff_t i_size = i_size_read(inode);
		fscache_unuse_cookie(cookie, aux, &i_size);
	}
}
EXPORT_SYMBOL(netfs_clear_inode_writeback);

/**
 * netfs_invalidate_folio - Invalidate or partially invalidate a folio
 * @folio: Folio proposed for release
 * @offset: Offset of the invalidated region
 * @length: Length of the invalidated region
 *
 * Invalidate part or all of a folio for a network filesystem.  The folio will
 * be removed afterwards if the invalidated region covers the entire folio.
 */
void netfs_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	struct netfs_folio *finfo;
	struct netfs_inode *ctx = netfs_inode(folio_inode(folio));
	size_t flen = folio_size(folio);

	_enter("{%lx},%zx,%zx", folio->index, offset, length);

	if (offset == 0 && length == flen) {
		unsigned long long i_size = i_size_read(&ctx->inode);
		unsigned long long fpos = folio_pos(folio), end;

		end = umin(fpos + flen, i_size);
		if (fpos < i_size && end > ctx->zero_point)
			ctx->zero_point = end;
	}

	folio_wait_private_2(folio); /* [DEPRECATED] */

	if (!folio_test_private(folio))
		return;

	finfo = netfs_folio_info(folio);

	if (offset == 0 && length >= flen)
		goto erase_completely;

	if (finfo) {
		/* We have a partially uptodate page from a streaming write. */
		unsigned int fstart = finfo->dirty_offset;
		unsigned int fend = fstart + finfo->dirty_len;
		unsigned int iend = offset + length;

		if (offset >= fend)
			return;
		if (iend <= fstart)
			return;

		/* The invalidation region overlaps the data.  If the region
		 * covers the start of the data, we either move along the start
		 * or just erase the data entirely.
		 */
		if (offset <= fstart) {
			if (iend >= fend)
				goto erase_completely;
			/* Move the start of the data. */
			finfo->dirty_len = fend - iend;
			finfo->dirty_offset = offset;
			return;
		}

		/* Reduce the length of the data if the invalidation region
		 * covers the tail part.
		 */
		if (iend >= fend) {
			finfo->dirty_len = offset - fstart;
			return;
		}

		/* A partial write was split.  The caller has already zeroed
		 * it, so just absorb the hole.
		 */
	}
	return;

erase_completely:
	netfs_put_group(netfs_folio_group(folio));
	folio_detach_private(folio);
	folio_clear_uptodate(folio);
	kfree(finfo);
	return;
}
EXPORT_SYMBOL(netfs_invalidate_folio);

/**
 * netfs_release_folio - Try to release a folio
 * @folio: Folio proposed for release
 * @gfp: Flags qualifying the release
 *
 * Request release of a folio and clean up its private state if it's not busy.
 * Returns true if the folio can now be released, false if not
 */
bool netfs_release_folio(struct folio *folio, gfp_t gfp)
{
	struct netfs_inode *ctx = netfs_inode(folio_inode(folio));
	unsigned long long end;

	if (folio_test_dirty(folio))
		return false;

	end = umin(folio_pos(folio) + folio_size(folio), i_size_read(&ctx->inode));
	if (end > ctx->zero_point)
		ctx->zero_point = end;

	if (folio_test_private(folio))
		return false;
	if (unlikely(folio_test_private_2(folio))) { /* [DEPRECATED] */
		if (current_is_kswapd() || !(gfp & __GFP_FS))
			return false;
		folio_wait_private_2(folio);
	}
	fscache_note_page_release(netfs_i_cookie(ctx));
	return true;
}
EXPORT_SYMBOL(netfs_release_folio);
