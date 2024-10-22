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
 * [DEPRECATED] Mark page as requiring copy-to-cache using PG_private_2.  The
 * third mark in the folio queue is used to indicate that this folio needs
 * writing.
 */
void netfs_pgpriv2_mark_copy_to_cache(struct netfs_io_subrequest *subreq,
				      struct netfs_io_request *rreq,
				      struct folio_queue *folioq,
				      int slot)
{
	struct folio *folio = folioq_folio(folioq, slot);

	trace_netfs_folio(folio, netfs_folio_trace_copy_to_cache);
	folio_start_private_2(folio);
	folioq_mark3(folioq, slot);
}

/*
 * [DEPRECATED] Cancel PG_private_2 on all marked folios in the event of an
 * unrecoverable error.
 */
static void netfs_pgpriv2_cancel(struct folio_queue *folioq)
{
	struct folio *folio;
	int slot;

	while (folioq) {
		if (!folioq->marks3) {
			folioq = folioq->next;
			continue;
		}

		slot = __ffs(folioq->marks3);
		folio = folioq_folio(folioq, slot);

		trace_netfs_folio(folio, netfs_folio_trace_cancel_copy);
		folio_end_private_2(folio);
		folioq_unmark3(folioq, slot);
	}
}

/*
 * [DEPRECATED] Copy a folio to the cache with PG_private_2 set.
 */
static int netfs_pgpriv2_copy_folio(struct netfs_io_request *wreq, struct folio *folio)
{
	struct netfs_io_stream *cache  = &wreq->io_streams[1];
	size_t fsize = folio_size(folio), flen = fsize;
	loff_t fpos = folio_pos(folio), i_size;
	bool to_eof = false;

	_enter("");

	/* netfs_perform_write() may shift i_size around the page or from out
	 * of the page to beyond it, but cannot move i_size into or through the
	 * page since we have it locked.
	 */
	i_size = i_size_read(wreq->inode);

	if (fpos >= i_size) {
		/* mmap beyond eof. */
		_debug("beyond eof");
		folio_end_private_2(folio);
		return 0;
	}

	if (fpos + fsize > wreq->i_size)
		wreq->i_size = i_size;

	if (flen > i_size - fpos) {
		flen = i_size - fpos;
		to_eof = true;
	} else if (flen == i_size - fpos) {
		to_eof = true;
	}

	_debug("folio %zx %zx", flen, fsize);

	trace_netfs_folio(folio, netfs_folio_trace_store_copy);

	/* Attach the folio to the rolling buffer. */
	if (netfs_buffer_append_folio(wreq, folio, false) < 0)
		return -ENOMEM;

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

		wreq->io_iter.iov_offset = cache->submit_off;

		atomic64_set(&wreq->issued_to, fpos + cache->submit_off);
		cache->submit_extendable_to = fsize - cache->submit_off;
		part = netfs_advance_write(wreq, cache, fpos + cache->submit_off,
					   cache->submit_len, to_eof);
		cache->submit_off += part;
		if (part > cache->submit_len)
			cache->submit_len = 0;
		else
			cache->submit_len -= part;
	} while (cache->submit_len > 0);

	wreq->io_iter.iov_offset = 0;
	iov_iter_advance(&wreq->io_iter, fsize);
	atomic64_set(&wreq->issued_to, fpos + fsize);

	if (flen < fsize)
		netfs_issue_write(wreq, cache);

	_leave(" = 0");
	return 0;
}

/*
 * [DEPRECATED] Go through the buffer and write any folios that are marked with
 * the third mark to the cache.
 */
void netfs_pgpriv2_write_to_the_cache(struct netfs_io_request *rreq)
{
	struct netfs_io_request *wreq;
	struct folio_queue *folioq;
	struct folio *folio;
	int error = 0;
	int slot = 0;

	_enter("");

	if (!fscache_resources_valid(&rreq->cache_resources))
		goto couldnt_start;

	/* Need the first folio to be able to set up the op. */
	for (folioq = rreq->buffer; folioq; folioq = folioq->next) {
		if (folioq->marks3) {
			slot = __ffs(folioq->marks3);
			break;
		}
	}
	if (!folioq)
		return;
	folio = folioq_folio(folioq, slot);

	wreq = netfs_create_write_req(rreq->mapping, NULL, folio_pos(folio),
				      NETFS_PGPRIV2_COPY_TO_CACHE);
	if (IS_ERR(wreq)) {
		kleave(" [create %ld]", PTR_ERR(wreq));
		goto couldnt_start;
	}

	trace_netfs_write(wreq, netfs_write_trace_copy_to_cache);
	netfs_stat(&netfs_n_wh_copy_to_cache);

	for (;;) {
		error = netfs_pgpriv2_copy_folio(wreq, folio);
		if (error < 0)
			break;

		folioq_unmark3(folioq, slot);
		if (!folioq->marks3) {
			folioq = folioq->next;
			if (!folioq)
				break;
		}

		slot = __ffs(folioq->marks3);
		folio = folioq_folio(folioq, slot);
	}

	netfs_issue_write(wreq, &wreq->io_streams[1]);
	smp_wmb(); /* Write lists before ALL_QUEUED. */
	set_bit(NETFS_RREQ_ALL_QUEUED, &wreq->flags);

	netfs_put_request(wreq, false, netfs_rreq_trace_put_return);
	_leave(" = %d", error);
couldnt_start:
	netfs_pgpriv2_cancel(rreq->buffer);
}

/*
 * [DEPRECATED] Remove the PG_private_2 mark from any folios we've finished
 * copying.
 */
bool netfs_pgpriv2_unlock_copied_folios(struct netfs_io_request *wreq)
{
	struct folio_queue *folioq = wreq->buffer;
	unsigned long long collected_to = wreq->collected_to;
	unsigned int slot = wreq->buffer_head_slot;
	bool made_progress = false;

	if (slot >= folioq_nr_slots(folioq)) {
		folioq = netfs_delete_buffer_head(wreq);
		slot = 0;
	}

	for (;;) {
		struct folio *folio;
		unsigned long long fpos, fend;
		size_t fsize, flen;

		folio = folioq_folio(folioq, slot);
		if (WARN_ONCE(!folio_test_private_2(folio),
			      "R=%08x: folio %lx is not marked private_2\n",
			      wreq->debug_id, folio->index))
			trace_netfs_folio(folio, netfs_folio_trace_not_under_wback);

		fpos = folio_pos(folio);
		fsize = folio_size(folio);
		flen = fsize;

		fend = min_t(unsigned long long, fpos + flen, wreq->i_size);

		trace_netfs_collect_folio(wreq, folio, fend, collected_to);

		/* Unlock any folio we've transferred all of. */
		if (collected_to < fend)
			break;

		trace_netfs_folio(folio, netfs_folio_trace_end_copy);
		folio_end_private_2(folio);
		wreq->cleaned_to = fpos + fsize;
		made_progress = true;

		/* Clean up the head folioq.  If we clear an entire folioq, then
		 * we can get rid of it provided it's not also the tail folioq
		 * being filled by the issuer.
		 */
		folioq_clear(folioq, slot);
		slot++;
		if (slot >= folioq_nr_slots(folioq)) {
			if (READ_ONCE(wreq->buffer_tail) == folioq)
				break;
			folioq = netfs_delete_buffer_head(wreq);
			slot = 0;
		}

		if (fpos + fsize >= collected_to)
			break;
	}

	wreq->buffer = folioq;
	wreq->buffer_head_slot = slot;
	return made_progress;
}
