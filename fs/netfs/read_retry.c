// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem read subrequest retrying.
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include "internal.h"

static void netfs_reissue_read(struct netfs_io_request *rreq,
			       struct netfs_io_subrequest *subreq)
{
	struct iov_iter *io_iter = &subreq->io_iter;

	if (iov_iter_is_folioq(io_iter)) {
		subreq->curr_folioq = (struct folio_queue *)io_iter->folioq;
		subreq->curr_folioq_slot = io_iter->folioq_slot;
		subreq->curr_folio_order = subreq->curr_folioq->orders[subreq->curr_folioq_slot];
	}

	atomic_inc(&rreq->nr_outstanding);
	__set_bit(NETFS_SREQ_IN_PROGRESS, &subreq->flags);
	netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
	subreq->rreq->netfs_ops->issue_read(subreq);
}

/*
 * Go through the list of failed/short reads, retrying all retryable ones.  We
 * need to switch failed cache reads to network downloads.
 */
static void netfs_retry_read_subrequests(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream0 = &rreq->io_streams[0];
	LIST_HEAD(sublist);
	LIST_HEAD(queue);

	_enter("R=%x", rreq->debug_id);

	if (list_empty(&rreq->subrequests))
		return;

	if (rreq->netfs_ops->retry_request)
		rreq->netfs_ops->retry_request(rreq, NULL);

	/* If there's no renegotiation to do, just resend each retryable subreq
	 * up to the first permanently failed one.
	 */
	if (!rreq->netfs_ops->prepare_read &&
	    !rreq->cache_resources.ops) {
		struct netfs_io_subrequest *subreq;

		list_for_each_entry(subreq, &rreq->subrequests, rreq_link) {
			if (test_bit(NETFS_SREQ_FAILED, &subreq->flags))
				break;
			if (__test_and_clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags)) {
				netfs_reset_iter(subreq);
				netfs_reissue_read(rreq, subreq);
			}
		}
		return;
	}

	/* Okay, we need to renegotiate all the download requests and flip any
	 * failed cache reads over to being download requests and negotiate
	 * those also.  All fully successful subreqs have been removed from the
	 * list and any spare data from those has been donated.
	 *
	 * What we do is decant the list and rebuild it one subreq at a time so
	 * that we don't end up with donations jumping over a gap we're busy
	 * populating with smaller subrequests.  In the event that the subreq
	 * we just launched finishes before we insert the next subreq, it'll
	 * fill in rreq->prev_donated instead.

	 * Note: Alternatively, we could split the tail subrequest right before
	 * we reissue it and fix up the donations under lock.
	 */
	list_splice_init(&rreq->subrequests, &queue);

	do {
		struct netfs_io_subrequest *from;
		struct iov_iter source;
		unsigned long long start, len;
		size_t part, deferred_next_donated = 0;
		bool boundary = false;

		/* Go through the subreqs and find the next span of contiguous
		 * buffer that we then rejig (cifs, for example, needs the
		 * rsize renegotiating) and reissue.
		 */
		from = list_first_entry(&queue, struct netfs_io_subrequest, rreq_link);
		list_move_tail(&from->rreq_link, &sublist);
		start = from->start + from->transferred;
		len   = from->len   - from->transferred;

		_debug("from R=%08x[%x] s=%llx ctl=%zx/%zx/%zx",
		       rreq->debug_id, from->debug_index,
		       from->start, from->consumed, from->transferred, from->len);

		if (test_bit(NETFS_SREQ_FAILED, &from->flags) ||
		    !test_bit(NETFS_SREQ_NEED_RETRY, &from->flags))
			goto abandon;

		deferred_next_donated = from->next_donated;
		while ((subreq = list_first_entry_or_null(
				&queue, struct netfs_io_subrequest, rreq_link))) {
			if (subreq->start != start + len ||
			    subreq->transferred > 0 ||
			    !test_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags))
				break;
			list_move_tail(&subreq->rreq_link, &sublist);
			len += subreq->len;
			deferred_next_donated = subreq->next_donated;
			if (test_bit(NETFS_SREQ_BOUNDARY, &subreq->flags))
				break;
		}

		_debug(" - range: %llx-%llx %llx", start, start + len - 1, len);

		/* Determine the set of buffers we're going to use.  Each
		 * subreq gets a subset of a single overall contiguous buffer.
		 */
		netfs_reset_iter(from);
		source = from->io_iter;
		source.count = len;

		/* Work through the sublist. */
		while ((subreq = list_first_entry_or_null(
				&sublist, struct netfs_io_subrequest, rreq_link))) {
			list_del(&subreq->rreq_link);

			subreq->source	= NETFS_DOWNLOAD_FROM_SERVER;
			subreq->start	= start - subreq->transferred;
			subreq->len	= len   + subreq->transferred;
			stream0->sreq_max_len = subreq->len;

			__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
			__set_bit(NETFS_SREQ_RETRYING, &subreq->flags);

			spin_lock_bh(&rreq->lock);
			list_add_tail(&subreq->rreq_link, &rreq->subrequests);
			subreq->prev_donated += rreq->prev_donated;
			rreq->prev_donated = 0;
			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);
			spin_unlock_bh(&rreq->lock);

			BUG_ON(!len);

			/* Renegotiate max_len (rsize) */
			if (rreq->netfs_ops->prepare_read &&
			    rreq->netfs_ops->prepare_read(subreq) < 0) {
				trace_netfs_sreq(subreq, netfs_sreq_trace_reprep_failed);
				__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
			}

			part = umin(len, stream0->sreq_max_len);
			if (unlikely(rreq->io_streams[0].sreq_max_segs))
				part = netfs_limit_iter(&source, 0, part, stream0->sreq_max_segs);
			subreq->len = subreq->transferred + part;
			subreq->io_iter = source;
			iov_iter_truncate(&subreq->io_iter, part);
			iov_iter_advance(&source, part);
			len -= part;
			start += part;
			if (!len) {
				if (boundary)
					__set_bit(NETFS_SREQ_BOUNDARY, &subreq->flags);
				subreq->next_donated = deferred_next_donated;
			} else {
				__clear_bit(NETFS_SREQ_BOUNDARY, &subreq->flags);
				subreq->next_donated = 0;
			}

			netfs_reissue_read(rreq, subreq);
			if (!len)
				break;

			/* If we ran out of subrequests, allocate another. */
			if (list_empty(&sublist)) {
				subreq = netfs_alloc_subrequest(rreq);
				if (!subreq)
					goto abandon;
				subreq->source = NETFS_DOWNLOAD_FROM_SERVER;
				subreq->start = start;

				/* We get two refs, but need just one. */
				netfs_put_subrequest(subreq, false, netfs_sreq_trace_new);
				trace_netfs_sreq(subreq, netfs_sreq_trace_split);
				list_add_tail(&subreq->rreq_link, &sublist);
			}
		}

		/* If we managed to use fewer subreqs, we can discard the
		 * excess.
		 */
		while ((subreq = list_first_entry_or_null(
				&sublist, struct netfs_io_subrequest, rreq_link))) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_discard);
			list_del(&subreq->rreq_link);
			netfs_put_subrequest(subreq, false, netfs_sreq_trace_put_done);
		}

	} while (!list_empty(&queue));

	return;

	/* If we hit ENOMEM, fail all remaining subrequests */
abandon:
	list_splice_init(&sublist, &queue);
	list_for_each_entry(subreq, &queue, rreq_link) {
		if (!subreq->error)
			subreq->error = -ENOMEM;
		__clear_bit(NETFS_SREQ_FAILED, &subreq->flags);
		__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
		__clear_bit(NETFS_SREQ_RETRYING, &subreq->flags);
	}
	spin_lock_bh(&rreq->lock);
	list_splice_tail_init(&queue, &rreq->subrequests);
	spin_unlock_bh(&rreq->lock);
}

/*
 * Retry reads.
 */
void netfs_retry_reads(struct netfs_io_request *rreq)
{
	trace_netfs_rreq(rreq, netfs_rreq_trace_resubmit);

	atomic_inc(&rreq->nr_outstanding);

	netfs_retry_read_subrequests(rreq);

	if (atomic_dec_and_test(&rreq->nr_outstanding))
		netfs_rreq_terminated(rreq, false);
}

/*
 * Unlock any the pages that haven't been unlocked yet due to abandoned
 * subrequests.
 */
void netfs_unlock_abandoned_read_pages(struct netfs_io_request *rreq)
{
	struct folio_queue *p;

	for (p = rreq->buffer; p; p = p->next) {
		for (int slot = 0; slot < folioq_count(p); slot++) {
			struct folio *folio = folioq_folio(p, slot);

			if (folio && !folioq_is_marked2(p, slot)) {
				trace_netfs_folio(folio, netfs_folio_trace_abandon);
				folio_unlock(folio);
			}
		}
	}
}
