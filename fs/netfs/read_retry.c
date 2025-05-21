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
	__clear_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags);
	__set_bit(NETFS_SREQ_IN_PROGRESS, &subreq->flags);
	netfs_stat(&netfs_n_rh_retry_read_subreq);
	subreq->rreq->netfs_ops->issue_read(subreq);
}

/*
 * Go through the list of failed/short reads, retrying all retryable ones.  We
 * need to switch failed cache reads to network downloads.
 */
static void netfs_retry_read_subrequests(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	struct list_head *next;

	_enter("R=%x", rreq->debug_id);

	if (list_empty(&stream->subrequests))
		return;

	if (rreq->netfs_ops->retry_request)
		rreq->netfs_ops->retry_request(rreq, NULL);

	/* If there's no renegotiation to do, just resend each retryable subreq
	 * up to the first permanently failed one.
	 */
	if (!rreq->netfs_ops->prepare_read &&
	    !rreq->cache_resources.ops) {
		list_for_each_entry(subreq, &stream->subrequests, rreq_link) {
			if (test_bit(NETFS_SREQ_FAILED, &subreq->flags))
				break;
			if (__test_and_clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags)) {
				__clear_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags);
				subreq->retry_count++;
				netfs_reset_iter(subreq);
				netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
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
	 *
	 * Note: Alternatively, we could split the tail subrequest right before
	 * we reissue it and fix up the donations under lock.
	 */
	next = stream->subrequests.next;

	do {
		struct netfs_io_subrequest *from, *to, *tmp;
		struct iov_iter source;
		unsigned long long start, len;
		size_t part;
		bool boundary = false, subreq_superfluous = false;

		/* Go through the subreqs and find the next span of contiguous
		 * buffer that we then rejig (cifs, for example, needs the
		 * rsize renegotiating) and reissue.
		 */
		from = list_entry(next, struct netfs_io_subrequest, rreq_link);
		to = from;
		start = from->start + from->transferred;
		len   = from->len   - from->transferred;

		_debug("from R=%08x[%x] s=%llx ctl=%zx/%zx",
		       rreq->debug_id, from->debug_index,
		       from->start, from->transferred, from->len);

		if (test_bit(NETFS_SREQ_FAILED, &from->flags) ||
		    !test_bit(NETFS_SREQ_NEED_RETRY, &from->flags))
			goto abandon;

		list_for_each_continue(next, &stream->subrequests) {
			subreq = list_entry(next, struct netfs_io_subrequest, rreq_link);
			if (subreq->start + subreq->transferred != start + len ||
			    test_bit(NETFS_SREQ_BOUNDARY, &subreq->flags) ||
			    !test_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags))
				break;
			to = subreq;
			len += to->len;
		}

		_debug(" - range: %llx-%llx %llx", start, start + len - 1, len);

		/* Determine the set of buffers we're going to use.  Each
		 * subreq gets a subset of a single overall contiguous buffer.
		 */
		netfs_reset_iter(from);
		source = from->io_iter;
		source.count = len;

		/* Work through the sublist. */
		subreq = from;
		list_for_each_entry_from(subreq, &stream->subrequests, rreq_link) {
			if (!len) {
				subreq_superfluous = true;
				break;
			}
			subreq->source	= NETFS_DOWNLOAD_FROM_SERVER;
			subreq->start	= start - subreq->transferred;
			subreq->len	= len   + subreq->transferred;
			__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
			__clear_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags);
			subreq->retry_count++;

			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);

			/* Renegotiate max_len (rsize) */
			stream->sreq_max_len = subreq->len;
			if (rreq->netfs_ops->prepare_read &&
			    rreq->netfs_ops->prepare_read(subreq) < 0) {
				trace_netfs_sreq(subreq, netfs_sreq_trace_reprep_failed);
				__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
				goto abandon;
			}

			part = umin(len, stream->sreq_max_len);
			if (unlikely(stream->sreq_max_segs))
				part = netfs_limit_iter(&source, 0, part, stream->sreq_max_segs);
			subreq->len = subreq->transferred + part;
			subreq->io_iter = source;
			iov_iter_truncate(&subreq->io_iter, part);
			iov_iter_advance(&source, part);
			len -= part;
			start += part;
			if (!len) {
				if (boundary)
					__set_bit(NETFS_SREQ_BOUNDARY, &subreq->flags);
			} else {
				__clear_bit(NETFS_SREQ_BOUNDARY, &subreq->flags);
			}

			netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
			netfs_reissue_read(rreq, subreq);
			if (subreq == to) {
				subreq_superfluous = false;
				break;
			}
		}

		/* If we managed to use fewer subreqs, we can discard the
		 * excess; if we used the same number, then we're done.
		 */
		if (!len) {
			if (!subreq_superfluous)
				continue;
			list_for_each_entry_safe_from(subreq, tmp,
						      &stream->subrequests, rreq_link) {
				trace_netfs_sreq(subreq, netfs_sreq_trace_superfluous);
				list_del(&subreq->rreq_link);
				netfs_put_subrequest(subreq, netfs_sreq_trace_put_done);
				if (subreq == to)
					break;
			}
			continue;
		}

		/* We ran out of subrequests, so we need to allocate some more
		 * and insert them after.
		 */
		do {
			subreq = netfs_alloc_subrequest(rreq);
			if (!subreq) {
				subreq = to;
				goto abandon_after;
			}
			subreq->source		= NETFS_DOWNLOAD_FROM_SERVER;
			subreq->start		= start;
			subreq->len		= len;
			subreq->stream_nr	= stream->stream_nr;
			subreq->retry_count	= 1;

			trace_netfs_sreq_ref(rreq->debug_id, subreq->debug_index,
					     refcount_read(&subreq->ref),
					     netfs_sreq_trace_new);

			list_add(&subreq->rreq_link, &to->rreq_link);
			to = list_next_entry(to, rreq_link);
			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);

			stream->sreq_max_len	= umin(len, rreq->rsize);
			stream->sreq_max_segs	= 0;
			if (unlikely(stream->sreq_max_segs))
				part = netfs_limit_iter(&source, 0, part, stream->sreq_max_segs);

			netfs_stat(&netfs_n_rh_download);
			if (rreq->netfs_ops->prepare_read(subreq) < 0) {
				trace_netfs_sreq(subreq, netfs_sreq_trace_reprep_failed);
				__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
				goto abandon;
			}

			part = umin(len, stream->sreq_max_len);
			subreq->len = subreq->transferred + part;
			subreq->io_iter = source;
			iov_iter_truncate(&subreq->io_iter, part);
			iov_iter_advance(&source, part);

			len -= part;
			start += part;
			if (!len && boundary) {
				__set_bit(NETFS_SREQ_BOUNDARY, &to->flags);
				boundary = false;
			}

			netfs_reissue_read(rreq, subreq);
		} while (len);

	} while (!list_is_head(next, &stream->subrequests));

	return;

	/* If we hit an error, fail all remaining incomplete subrequests */
abandon_after:
	if (list_is_last(&subreq->rreq_link, &stream->subrequests))
		return;
	subreq = list_next_entry(subreq, rreq_link);
abandon:
	list_for_each_entry_from(subreq, &stream->subrequests, rreq_link) {
		if (!subreq->error &&
		    !test_bit(NETFS_SREQ_FAILED, &subreq->flags) &&
		    !test_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags))
			continue;
		subreq->error = -ENOMEM;
		__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
		__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
	}
}

/*
 * Retry reads.
 */
void netfs_retry_reads(struct netfs_io_request *rreq)
{
	struct netfs_io_stream *stream = &rreq->io_streams[0];

	netfs_stat(&netfs_n_rh_retry_read_req);

	/* Wait for all outstanding I/O to quiesce before performing retries as
	 * we may need to renegotiate the I/O sizes.
	 */
	set_bit(NETFS_RREQ_RETRYING, &rreq->flags);
	netfs_wait_for_in_progress_stream(rreq, stream);
	clear_bit(NETFS_RREQ_RETRYING, &rreq->flags);

	trace_netfs_rreq(rreq, netfs_rreq_trace_resubmit);
	netfs_retry_read_subrequests(rreq);
}

/*
 * Unlock any the pages that haven't been unlocked yet due to abandoned
 * subrequests.
 */
void netfs_unlock_abandoned_read_pages(struct netfs_io_request *rreq)
{
	struct folio_queue *p;

	for (p = rreq->buffer.tail; p; p = p->next) {
		for (int slot = 0; slot < folioq_count(p); slot++) {
			struct folio *folio = folioq_folio(p, slot);

			if (folio && !folioq_is_marked2(p, slot)) {
				trace_netfs_folio(folio, netfs_folio_trace_abandon);
				folio_unlock(folio);
			}
		}
	}
}
