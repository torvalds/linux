// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem write retrying.
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include "internal.h"

/*
 * Perform retries on the streams that need it.
 */
static void netfs_retry_write_stream(struct netfs_io_request *wreq,
				     struct netfs_io_stream *stream)
{
	struct list_head *next;

	_enter("R=%x[%x:]", wreq->debug_id, stream->stream_nr);

	if (list_empty(&stream->subrequests))
		return;

	if (stream->source == NETFS_UPLOAD_TO_SERVER &&
	    wreq->netfs_ops->retry_request)
		wreq->netfs_ops->retry_request(wreq, stream);

	if (unlikely(stream->failed))
		return;

	/* If there's no renegotiation to do, just resend each failed subreq. */
	if (!stream->prepare_write) {
		struct netfs_io_subrequest *subreq;

		list_for_each_entry(subreq, &stream->subrequests, rreq_link) {
			if (test_bit(NETFS_SREQ_FAILED, &subreq->flags))
				break;
			if (__test_and_clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags)) {
				struct iov_iter source;

				netfs_reset_iter(subreq);
				source = subreq->io_iter;
				netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
				netfs_reissue_write(stream, subreq, &source);
			}
		}
		return;
	}

	next = stream->subrequests.next;

	do {
		struct netfs_io_subrequest *subreq = NULL, *from, *to, *tmp;
		struct iov_iter source;
		unsigned long long start, len;
		size_t part;
		bool boundary = false;

		/* Go through the stream and find the next span of contiguous
		 * data that we then rejig (cifs, for example, needs the wsize
		 * renegotiating) and reissue.
		 */
		from = list_entry(next, struct netfs_io_subrequest, rreq_link);
		to = from;
		start = from->start + from->transferred;
		len   = from->len   - from->transferred;

		if (test_bit(NETFS_SREQ_FAILED, &from->flags) ||
		    !test_bit(NETFS_SREQ_NEED_RETRY, &from->flags))
			return;

		list_for_each_continue(next, &stream->subrequests) {
			subreq = list_entry(next, struct netfs_io_subrequest, rreq_link);
			if (subreq->start + subreq->transferred != start + len ||
			    test_bit(NETFS_SREQ_BOUNDARY, &subreq->flags) ||
			    !test_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags))
				break;
			to = subreq;
			len += to->len;
		}

		/* Determine the set of buffers we're going to use.  Each
		 * subreq gets a subset of a single overall contiguous buffer.
		 */
		netfs_reset_iter(from);
		source = from->io_iter;
		source.count = len;

		/* Work through the sublist. */
		subreq = from;
		list_for_each_entry_from(subreq, &stream->subrequests, rreq_link) {
			if (!len)
				break;

			subreq->start	= start;
			subreq->len	= len;
			__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
			subreq->retry_count++;
			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);

			/* Renegotiate max_len (wsize) */
			stream->sreq_max_len = len;
			stream->prepare_write(subreq);

			part = umin(len, stream->sreq_max_len);
			if (unlikely(stream->sreq_max_segs))
				part = netfs_limit_iter(&source, 0, part, stream->sreq_max_segs);
			subreq->len = part;
			subreq->transferred = 0;
			len -= part;
			start += part;
			if (len && subreq == to &&
			    __test_and_clear_bit(NETFS_SREQ_BOUNDARY, &to->flags))
				boundary = true;

			netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
			netfs_reissue_write(stream, subreq, &source);
			if (subreq == to)
				break;
		}

		/* If we managed to use fewer subreqs, we can discard the
		 * excess; if we used the same number, then we're done.
		 */
		if (!len) {
			if (subreq == to)
				continue;
			list_for_each_entry_safe_from(subreq, tmp,
						      &stream->subrequests, rreq_link) {
				trace_netfs_sreq(subreq, netfs_sreq_trace_discard);
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
			subreq = netfs_alloc_subrequest(wreq);
			subreq->source		= to->source;
			subreq->start		= start;
			subreq->stream_nr	= to->stream_nr;
			subreq->retry_count	= 1;

			trace_netfs_sreq_ref(wreq->debug_id, subreq->debug_index,
					     refcount_read(&subreq->ref),
					     netfs_sreq_trace_new);
			trace_netfs_sreq(subreq, netfs_sreq_trace_split);

			list_add(&subreq->rreq_link, &to->rreq_link);
			to = list_next_entry(to, rreq_link);
			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);

			stream->sreq_max_len	= len;
			stream->sreq_max_segs	= INT_MAX;
			switch (stream->source) {
			case NETFS_UPLOAD_TO_SERVER:
				netfs_stat(&netfs_n_wh_upload);
				stream->sreq_max_len = umin(len, wreq->wsize);
				break;
			case NETFS_WRITE_TO_CACHE:
				netfs_stat(&netfs_n_wh_write);
				break;
			default:
				WARN_ON_ONCE(1);
			}

			stream->prepare_write(subreq);

			part = umin(len, stream->sreq_max_len);
			subreq->len = subreq->transferred + part;
			len -= part;
			start += part;
			if (!len && boundary) {
				__set_bit(NETFS_SREQ_BOUNDARY, &to->flags);
				boundary = false;
			}

			netfs_reissue_write(stream, subreq, &source);
			if (!len)
				break;

		} while (len);

	} while (!list_is_head(next, &stream->subrequests));
}

/*
 * Perform retries on the streams that need it.  If we're doing content
 * encryption and the server copy changed due to a third-party write, we may
 * need to do an RMW cycle and also rewrite the data to the cache.
 */
void netfs_retry_writes(struct netfs_io_request *wreq)
{
	struct netfs_io_stream *stream;
	int s;

	netfs_stat(&netfs_n_wh_retry_write_req);

	/* Wait for all outstanding I/O to quiesce before performing retries as
	 * we may need to renegotiate the I/O sizes.
	 */
	set_bit(NETFS_RREQ_RETRYING, &wreq->flags);
	for (s = 0; s < NR_IO_STREAMS; s++) {
		stream = &wreq->io_streams[s];
		if (stream->active)
			netfs_wait_for_in_progress_stream(wreq, stream);
	}
	clear_bit(NETFS_RREQ_RETRYING, &wreq->flags);

	// TODO: Enc: Fetch changed partial pages
	// TODO: Enc: Reencrypt content if needed.
	// TODO: Enc: Wind back transferred point.
	// TODO: Enc: Mark cache pages for retry.

	for (s = 0; s < NR_IO_STREAMS; s++) {
		stream = &wreq->io_streams[s];
		if (stream->need_retry) {
			stream->need_retry = false;
			netfs_retry_write_stream(wreq, stream);
		}
	}
}
