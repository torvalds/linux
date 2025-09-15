// SPDX-License-Identifier: GPL-2.0-only
/* Read with PG_private_2 [DEPRECATED].
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/task_io_accounting_ops.h>
#include "internal.h"

/*
 * [DEPRECATED] Copy a folio to the cache with PG_private_2 set.
 */
static void netfs_pgpriv2_copy_folio(struct netfs_io_request *creq, struct folio *folio)
{
	struct netfs_io_stream *cache = &creq->io_streams[1];
	size_t fsize = folio_size(folio), flen = fsize;
	loff_t fpos = folio_pos(folio), i_size;
	bool to_eof = false;

	_enter("");

	/* netfs_perform_write() may shift i_size around the page or from out
	 * of the page to beyond it, but cannot move i_size into or through the
	 * page since we have it locked.
	 */
	i_size = i_size_read(creq->inode);

	if (fpos >= i_size) {
		/* mmap beyond eof. */
		_debug("beyond eof");
		folio_end_private_2(folio);
		return;
	}

	if (fpos + fsize > creq->i_size)
		creq->i_size = i_size;

	if (flen > i_size - fpos) {
		flen = i_size - fpos;
		to_eof = true;
	} else if (flen == i_size - fpos) {
		to_eof = true;
	}

	_debug("folio %zx %zx", flen, fsize);

	trace_netfs_folio(folio, netfs_folio_trace_store_copy);

	/* Attach the folio to the rolling buffer. */
	if (rolling_buffer_append(&creq->buffer, folio, 0) < 0) {
		clear_bit(NETFS_RREQ_FOLIO_COPY_TO_CACHE, &creq->flags);
		return;
	}

	cache->submit_extendable_to = fsize;
	cache->submit_off = 0;
	cache->submit_len = flen;

	/* Attach the folio to one or more subrequests.  For a big folio, we
	 * could end up with thousands of subrequests if the wsize is small -
	 * but we might need to wait during the creation of subrequests for
	 * network resources (eg. SMB credits).
	 */
	do {
		ssize_t part;

		creq->buffer.iter.iov_offset = cache->submit_off;

		atomic64_set(&creq->issued_to, fpos + cache->submit_off);
		cache->submit_extendable_to = fsize - cache->submit_off;
		part = netfs_advance_write(creq, cache, fpos + cache->submit_off,
					   cache->submit_len, to_eof);
		cache->submit_off += part;
		if (part > cache->submit_len)
			cache->submit_len = 0;
		else
			cache->submit_len -= part;
	} while (cache->submit_len > 0);

	creq->buffer.iter.iov_offset = 0;
	rolling_buffer_advance(&creq->buffer, fsize);
	atomic64_set(&creq->issued_to, fpos + fsize);

	if (flen < fsize)
		netfs_issue_write(creq, cache);
}

/*
 * [DEPRECATED] Set up copying to the cache.
 */
static struct netfs_io_request *netfs_pgpriv2_begin_copy_to_cache(
	struct netfs_io_request *rreq, struct folio *folio)
{
	struct netfs_io_request *creq;

	if (!fscache_resources_valid(&rreq->cache_resources))
		goto cancel;

	creq = netfs_create_write_req(rreq->mapping, NULL, folio_pos(folio),
				      NETFS_PGPRIV2_COPY_TO_CACHE);
	if (IS_ERR(creq))
		goto cancel;

	if (!creq->io_streams[1].avail)
		goto cancel_put;

	__set_bit(NETFS_RREQ_OFFLOAD_COLLECTION, &creq->flags);
	trace_netfs_copy2cache(rreq, creq);
	trace_netfs_write(creq, netfs_write_trace_copy_to_cache);
	netfs_stat(&netfs_n_wh_copy_to_cache);
	rreq->copy_to_cache = creq;
	return creq;

cancel_put:
	netfs_put_request(creq, netfs_rreq_trace_put_return);
cancel:
	rreq->copy_to_cache = ERR_PTR(-ENOBUFS);
	clear_bit(NETFS_RREQ_FOLIO_COPY_TO_CACHE, &rreq->flags);
	return ERR_PTR(-ENOBUFS);
}

/*
 * [DEPRECATED] Mark page as requiring copy-to-cache using PG_private_2 and add
 * it to the copy write request.
 */
void netfs_pgpriv2_copy_to_cache(struct netfs_io_request *rreq, struct folio *folio)
{
	struct netfs_io_request *creq = rreq->copy_to_cache;

	if (!creq)
		creq = netfs_pgpriv2_begin_copy_to_cache(rreq, folio);
	if (IS_ERR(creq))
		return;

	trace_netfs_folio(folio, netfs_folio_trace_copy_to_cache);
	folio_start_private_2(folio);
	netfs_pgpriv2_copy_folio(creq, folio);
}

/*
 * [DEPRECATED] End writing to the cache, flushing out any outstanding writes.
 */
void netfs_pgpriv2_end_copy_to_cache(struct netfs_io_request *rreq)
{
	struct netfs_io_request *creq = rreq->copy_to_cache;

	if (IS_ERR_OR_NULL(creq))
		return;

	netfs_issue_write(creq, &creq->io_streams[1]);
	smp_wmb(); /* Write lists before ALL_QUEUED. */
	set_bit(NETFS_RREQ_ALL_QUEUED, &creq->flags);
	trace_netfs_rreq(rreq, netfs_rreq_trace_end_copy_to_cache);
	if (list_empty_careful(&creq->io_streams[1].subrequests))
		netfs_wake_collector(creq);

	netfs_put_request(creq, netfs_rreq_trace_put_return);
	creq->copy_to_cache = NULL;
}

/*
 * [DEPRECATED] Remove the PG_private_2 mark from any folios we've finished
 * copying.
 */
bool netfs_pgpriv2_unlock_copied_folios(struct netfs_io_request *creq)
{
	struct folio_queue *folioq = creq->buffer.tail;
	unsigned long long collected_to = creq->collected_to;
	unsigned int slot = creq->buffer.first_tail_slot;
	bool made_progress = false;

	if (slot >= folioq_nr_slots(folioq)) {
		folioq = rolling_buffer_delete_spent(&creq->buffer);
		slot = 0;
	}

	for (;;) {
		struct folio *folio;
		unsigned long long fpos, fend;
		size_t fsize, flen;

		folio = folioq_folio(folioq, slot);
		if (WARN_ONCE(!folio_test_private_2(folio),
			      "R=%08x: folio %lx is not marked private_2\n",
			      creq->debug_id, folio->index))
			trace_netfs_folio(folio, netfs_folio_trace_not_under_wback);

		fpos = folio_pos(folio);
		fsize = folio_size(folio);
		flen = fsize;

		fend = min_t(unsigned long long, fpos + flen, creq->i_size);

		trace_netfs_collect_folio(creq, folio, fend, collected_to);

		/* Unlock any folio we've transferred all of. */
		if (collected_to < fend)
			break;

		trace_netfs_folio(folio, netfs_folio_trace_end_copy);
		folio_end_private_2(folio);
		creq->cleaned_to = fpos + fsize;
		made_progress = true;

		/* Clean up the head folioq.  If we clear an entire folioq, then
		 * we can get rid of it provided it's not also the tail folioq
		 * being filled by the issuer.
		 */
		folioq_clear(folioq, slot);
		slot++;
		if (slot >= folioq_nr_slots(folioq)) {
			folioq = rolling_buffer_delete_spent(&creq->buffer);
			if (!folioq)
				goto done;
			slot = 0;
		}

		if (fpos + fsize >= collected_to)
			break;
	}

	creq->buffer.tail = folioq;
done:
	creq->buffer.first_tail_slot = slot;
	return made_progress;
}
