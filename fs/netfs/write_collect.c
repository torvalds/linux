// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem write subrequest result collection, assessment
 * and retrying.
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include "internal.h"

/* Notes made in the collector */
#define HIT_PENDING		0x01	/* A front op was still pending */
#define SOME_EMPTY		0x02	/* One of more streams are empty */
#define ALL_EMPTY		0x04	/* All streams are empty */
#define MAYBE_DISCONTIG		0x08	/* A front op may be discontiguous (rounded to PAGE_SIZE) */
#define NEED_REASSESS		0x10	/* Need to loop round and reassess */
#define REASSESS_DISCONTIG	0x20	/* Reassess discontiguity if contiguity advances */
#define MADE_PROGRESS		0x40	/* Made progress cleaning up a stream or the folio set */
#define BUFFERED		0x80	/* The pagecache needs cleaning up */
#define NEED_RETRY		0x100	/* A front op requests retrying */
#define SAW_FAILURE		0x200	/* One stream or hit a permanent failure */

/*
 * Successful completion of write of a folio to the server and/or cache.  Note
 * that we are not allowed to lock the folio here on pain of deadlocking with
 * truncate.
 */
int netfs_folio_written_back(struct folio *folio)
{
	enum netfs_folio_trace why = netfs_folio_trace_clear;
	struct netfs_folio *finfo;
	struct netfs_group *group = NULL;
	int gcount = 0;

	if ((finfo = netfs_folio_info(folio))) {
		/* Streaming writes cannot be redirtied whilst under writeback,
		 * so discard the streaming record.
		 */
		folio_detach_private(folio);
		group = finfo->netfs_group;
		gcount++;
		kfree(finfo);
		why = netfs_folio_trace_clear_s;
		goto end_wb;
	}

	if ((group = netfs_folio_group(folio))) {
		if (group == NETFS_FOLIO_COPY_TO_CACHE) {
			why = netfs_folio_trace_clear_cc;
			folio_detach_private(folio);
			goto end_wb;
		}

		/* Need to detach the group pointer if the page didn't get
		 * redirtied.  If it has been redirtied, then it must be within
		 * the same group.
		 */
		why = netfs_folio_trace_redirtied;
		if (!folio_test_dirty(folio)) {
			folio_detach_private(folio);
			gcount++;
			why = netfs_folio_trace_clear_g;
		}
	}

end_wb:
	trace_netfs_folio(folio, why);
	folio_end_writeback(folio);
	return gcount;
}

/*
 * Get hold of a folio we have under writeback.  We don't want to get the
 * refcount on it.
 */
static struct folio *netfs_writeback_lookup_folio(struct netfs_io_request *wreq, loff_t pos)
{
	XA_STATE(xas, &wreq->mapping->i_pages, pos / PAGE_SIZE);
	struct folio *folio;

	rcu_read_lock();

	for (;;) {
		xas_reset(&xas);
		folio = xas_load(&xas);
		if (xas_retry(&xas, folio))
			continue;

		if (!folio || xa_is_value(folio))
			kdebug("R=%08x: folio %lx (%llx) not present",
			       wreq->debug_id, xas.xa_index, pos / PAGE_SIZE);
		BUG_ON(!folio || xa_is_value(folio));

		if (folio == xas_reload(&xas))
			break;
	}

	rcu_read_unlock();

	if (WARN_ONCE(!folio_test_writeback(folio),
		      "R=%08x: folio %lx is not under writeback\n",
		      wreq->debug_id, folio->index)) {
		trace_netfs_folio(folio, netfs_folio_trace_not_under_wback);
	}
	return folio;
}

/*
 * Unlock any folios we've finished with.
 */
static void netfs_writeback_unlock_folios(struct netfs_io_request *wreq,
					  unsigned long long collected_to,
					  unsigned int *notes)
{
	for (;;) {
		struct folio *folio;
		struct netfs_folio *finfo;
		unsigned long long fpos, fend;
		size_t fsize, flen;

		folio = netfs_writeback_lookup_folio(wreq, wreq->cleaned_to);

		fpos = folio_pos(folio);
		fsize = folio_size(folio);
		finfo = netfs_folio_info(folio);
		flen = finfo ? finfo->dirty_offset + finfo->dirty_len : fsize;

		fend = min_t(unsigned long long, fpos + flen, wreq->i_size);

		trace_netfs_collect_folio(wreq, folio, fend, collected_to);

		if (fpos + fsize > wreq->contiguity) {
			trace_netfs_collect_contig(wreq, fpos + fsize,
						   netfs_contig_trace_unlock);
			wreq->contiguity = fpos + fsize;
		}

		/* Unlock any folio we've transferred all of. */
		if (collected_to < fend)
			break;

		wreq->nr_group_rel += netfs_folio_written_back(folio);
		wreq->cleaned_to = fpos + fsize;
		*notes |= MADE_PROGRESS;

		if (fpos + fsize >= collected_to)
			break;
	}
}

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
				__set_bit(NETFS_SREQ_RETRYING, &subreq->flags);
				netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
				netfs_reissue_write(stream, subreq);
			}
		}
		return;
	}

	next = stream->subrequests.next;

	do {
		struct netfs_io_subrequest *subreq = NULL, *from, *to, *tmp;
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

		/* Work through the sublist. */
		subreq = from;
		list_for_each_entry_from(subreq, &stream->subrequests, rreq_link) {
			if (!len)
				break;
			/* Renegotiate max_len (wsize) */
			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);
			__clear_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
			__set_bit(NETFS_SREQ_RETRYING, &subreq->flags);
			stream->prepare_write(subreq);

			part = min(len, subreq->max_len);
			subreq->len = part;
			subreq->start = start;
			subreq->transferred = 0;
			len -= part;
			start += part;
			if (len && subreq == to &&
			    __test_and_clear_bit(NETFS_SREQ_BOUNDARY, &to->flags))
				boundary = true;

			netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);
			netfs_reissue_write(stream, subreq);
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
				netfs_put_subrequest(subreq, false, netfs_sreq_trace_put_done);
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
			subreq->max_len		= len;
			subreq->max_nr_segs	= INT_MAX;
			subreq->debug_index	= atomic_inc_return(&wreq->subreq_counter);
			subreq->stream_nr	= to->stream_nr;
			__set_bit(NETFS_SREQ_RETRYING, &subreq->flags);

			trace_netfs_sreq_ref(wreq->debug_id, subreq->debug_index,
					     refcount_read(&subreq->ref),
					     netfs_sreq_trace_new);
			netfs_get_subrequest(subreq, netfs_sreq_trace_get_resubmit);

			list_add(&subreq->rreq_link, &to->rreq_link);
			to = list_next_entry(to, rreq_link);
			trace_netfs_sreq(subreq, netfs_sreq_trace_retry);

			switch (stream->source) {
			case NETFS_UPLOAD_TO_SERVER:
				netfs_stat(&netfs_n_wh_upload);
				subreq->max_len = min(len, wreq->wsize);
				break;
			case NETFS_WRITE_TO_CACHE:
				netfs_stat(&netfs_n_wh_write);
				break;
			default:
				WARN_ON_ONCE(1);
			}

			stream->prepare_write(subreq);

			part = min(len, subreq->max_len);
			subreq->len = subreq->transferred + part;
			len -= part;
			start += part;
			if (!len && boundary) {
				__set_bit(NETFS_SREQ_BOUNDARY, &to->flags);
				boundary = false;
			}

			netfs_reissue_write(stream, subreq);
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
static void netfs_retry_writes(struct netfs_io_request *wreq)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream;
	int s;

	/* Wait for all outstanding I/O to quiesce before performing retries as
	 * we may need to renegotiate the I/O sizes.
	 */
	for (s = 0; s < NR_IO_STREAMS; s++) {
		stream = &wreq->io_streams[s];
		if (!stream->active)
			continue;

		list_for_each_entry(subreq, &stream->subrequests, rreq_link) {
			wait_on_bit(&subreq->flags, NETFS_SREQ_IN_PROGRESS,
				    TASK_UNINTERRUPTIBLE);
		}
	}

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

/*
 * Collect and assess the results of various write subrequests.  We may need to
 * retry some of the results - or even do an RMW cycle for content crypto.
 *
 * Note that we have a number of parallel, overlapping lists of subrequests,
 * one to the server and one to the local cache for example, which may not be
 * the same size or starting position and may not even correspond in boundary
 * alignment.
 */
static void netfs_collect_write_results(struct netfs_io_request *wreq)
{
	struct netfs_io_subrequest *front, *remove;
	struct netfs_io_stream *stream;
	unsigned long long collected_to;
	unsigned int notes;
	int s;

	_enter("%llx-%llx", wreq->start, wreq->start + wreq->len);
	trace_netfs_collect(wreq);
	trace_netfs_rreq(wreq, netfs_rreq_trace_collect);

reassess_streams:
	smp_rmb();
	collected_to = ULLONG_MAX;
	if (wreq->origin == NETFS_WRITEBACK)
		notes = ALL_EMPTY | BUFFERED | MAYBE_DISCONTIG;
	else if (wreq->origin == NETFS_WRITETHROUGH)
		notes = ALL_EMPTY | BUFFERED;
	else
		notes = ALL_EMPTY;

	/* Remove completed subrequests from the front of the streams and
	 * advance the completion point on each stream.  We stop when we hit
	 * something that's in progress.  The issuer thread may be adding stuff
	 * to the tail whilst we're doing this.
	 *
	 * We must not, however, merge in discontiguities that span whole
	 * folios that aren't under writeback.  This is made more complicated
	 * by the folios in the gap being of unpredictable sizes - if they even
	 * exist - but we don't want to look them up.
	 */
	for (s = 0; s < NR_IO_STREAMS; s++) {
		loff_t rstart, rend;

		stream = &wreq->io_streams[s];
		/* Read active flag before list pointers */
		if (!smp_load_acquire(&stream->active))
			continue;

		front = stream->front;
		while (front) {
			trace_netfs_collect_sreq(wreq, front);
			//_debug("sreq [%x] %llx %zx/%zx",
			//       front->debug_index, front->start, front->transferred, front->len);

			/* Stall if there may be a discontinuity. */
			rstart = round_down(front->start, PAGE_SIZE);
			if (rstart > wreq->contiguity) {
				if (wreq->contiguity > stream->collected_to) {
					trace_netfs_collect_gap(wreq, stream,
								wreq->contiguity, 'D');
					stream->collected_to = wreq->contiguity;
				}
				notes |= REASSESS_DISCONTIG;
				break;
			}
			rend = round_up(front->start + front->len, PAGE_SIZE);
			if (rend > wreq->contiguity) {
				trace_netfs_collect_contig(wreq, rend,
							   netfs_contig_trace_collect);
				wreq->contiguity = rend;
				if (notes & REASSESS_DISCONTIG)
					notes |= NEED_REASSESS;
			}
			notes &= ~MAYBE_DISCONTIG;

			/* Stall if the front is still undergoing I/O. */
			if (test_bit(NETFS_SREQ_IN_PROGRESS, &front->flags)) {
				notes |= HIT_PENDING;
				break;
			}
			smp_rmb(); /* Read counters after I-P flag. */

			if (stream->failed) {
				stream->collected_to = front->start + front->len;
				notes |= MADE_PROGRESS | SAW_FAILURE;
				goto cancel;
			}
			if (front->start + front->transferred > stream->collected_to) {
				stream->collected_to = front->start + front->transferred;
				stream->transferred = stream->collected_to - wreq->start;
				notes |= MADE_PROGRESS;
			}
			if (test_bit(NETFS_SREQ_FAILED, &front->flags)) {
				stream->failed = true;
				stream->error = front->error;
				if (stream->source == NETFS_UPLOAD_TO_SERVER)
					mapping_set_error(wreq->mapping, front->error);
				notes |= NEED_REASSESS | SAW_FAILURE;
				break;
			}
			if (front->transferred < front->len) {
				stream->need_retry = true;
				notes |= NEED_RETRY | MADE_PROGRESS;
				break;
			}

		cancel:
			/* Remove if completely consumed. */
			spin_lock(&wreq->lock);

			remove = front;
			list_del_init(&front->rreq_link);
			front = list_first_entry_or_null(&stream->subrequests,
							 struct netfs_io_subrequest, rreq_link);
			stream->front = front;
			if (!front) {
				unsigned long long jump_to = atomic64_read(&wreq->issued_to);

				if (stream->collected_to < jump_to) {
					trace_netfs_collect_gap(wreq, stream, jump_to, 'A');
					stream->collected_to = jump_to;
				}
			}

			spin_unlock(&wreq->lock);
			netfs_put_subrequest(remove, false,
					     notes & SAW_FAILURE ?
					     netfs_sreq_trace_put_cancel :
					     netfs_sreq_trace_put_done);
		}

		if (front)
			notes &= ~ALL_EMPTY;
		else
			notes |= SOME_EMPTY;

		if (stream->collected_to < collected_to)
			collected_to = stream->collected_to;
	}

	if (collected_to != ULLONG_MAX && collected_to > wreq->collected_to)
		wreq->collected_to = collected_to;

	/* If we have an empty stream, we need to jump it forward over any gap
	 * otherwise the collection point will never advance.
	 *
	 * Note that the issuer always adds to the stream with the lowest
	 * so-far submitted start, so if we see two consecutive subreqs in one
	 * stream with nothing between then in another stream, then the second
	 * stream has a gap that can be jumped.
	 */
	if (notes & SOME_EMPTY) {
		unsigned long long jump_to = wreq->start + wreq->len;

		for (s = 0; s < NR_IO_STREAMS; s++) {
			stream = &wreq->io_streams[s];
			if (stream->active &&
			    stream->front &&
			    stream->front->start < jump_to)
				jump_to = stream->front->start;
		}

		for (s = 0; s < NR_IO_STREAMS; s++) {
			stream = &wreq->io_streams[s];
			if (stream->active &&
			    !stream->front &&
			    stream->collected_to < jump_to) {
				trace_netfs_collect_gap(wreq, stream, jump_to, 'B');
				stream->collected_to = jump_to;
			}
		}
	}

	for (s = 0; s < NR_IO_STREAMS; s++) {
		stream = &wreq->io_streams[s];
		if (stream->active)
			trace_netfs_collect_stream(wreq, stream);
	}

	trace_netfs_collect_state(wreq, wreq->collected_to, notes);

	/* Unlock any folios that we have now finished with. */
	if (notes & BUFFERED) {
		unsigned long long clean_to = min(wreq->collected_to, wreq->contiguity);

		if (wreq->cleaned_to < clean_to)
			netfs_writeback_unlock_folios(wreq, clean_to, &notes);
	} else {
		wreq->cleaned_to = wreq->collected_to;
	}

	// TODO: Discard encryption buffers

	/* If all streams are discontiguous with the last folio we cleared, we
	 * may need to skip a set of folios.
	 */
	if ((notes & (MAYBE_DISCONTIG | ALL_EMPTY)) == MAYBE_DISCONTIG) {
		unsigned long long jump_to = ULLONG_MAX;

		for (s = 0; s < NR_IO_STREAMS; s++) {
			stream = &wreq->io_streams[s];
			if (stream->active && stream->front &&
			    stream->front->start < jump_to)
				jump_to = stream->front->start;
		}

		trace_netfs_collect_contig(wreq, jump_to, netfs_contig_trace_jump);
		wreq->contiguity = jump_to;
		wreq->cleaned_to = jump_to;
		wreq->collected_to = jump_to;
		for (s = 0; s < NR_IO_STREAMS; s++) {
			stream = &wreq->io_streams[s];
			if (stream->collected_to < jump_to)
				stream->collected_to = jump_to;
		}
		//cond_resched();
		notes |= MADE_PROGRESS;
		goto reassess_streams;
	}

	if (notes & NEED_RETRY)
		goto need_retry;
	if ((notes & MADE_PROGRESS) && test_bit(NETFS_RREQ_PAUSE, &wreq->flags)) {
		trace_netfs_rreq(wreq, netfs_rreq_trace_unpause);
		clear_bit_unlock(NETFS_RREQ_PAUSE, &wreq->flags);
		wake_up_bit(&wreq->flags, NETFS_RREQ_PAUSE);
	}

	if (notes & NEED_REASSESS) {
		//cond_resched();
		goto reassess_streams;
	}
	if (notes & MADE_PROGRESS) {
		//cond_resched();
		goto reassess_streams;
	}

out:
	netfs_put_group_many(wreq->group, wreq->nr_group_rel);
	wreq->nr_group_rel = 0;
	_leave(" = %x", notes);
	return;

need_retry:
	/* Okay...  We're going to have to retry one or both streams.  Note
	 * that any partially completed op will have had any wholly transferred
	 * folios removed from it.
	 */
	_debug("retry");
	netfs_retry_writes(wreq);
	goto out;
}

/*
 * Perform the collection of subrequests, folios and encryption buffers.
 */
void netfs_write_collection_worker(struct work_struct *work)
{
	struct netfs_io_request *wreq = container_of(work, struct netfs_io_request, work);
	struct netfs_inode *ictx = netfs_inode(wreq->inode);
	size_t transferred;
	int s;

	_enter("R=%x", wreq->debug_id);

	netfs_see_request(wreq, netfs_rreq_trace_see_work);
	if (!test_bit(NETFS_RREQ_IN_PROGRESS, &wreq->flags)) {
		netfs_put_request(wreq, false, netfs_rreq_trace_put_work);
		return;
	}

	netfs_collect_write_results(wreq);

	/* We're done when the app thread has finished posting subreqs and all
	 * the queues in all the streams are empty.
	 */
	if (!test_bit(NETFS_RREQ_ALL_QUEUED, &wreq->flags)) {
		netfs_put_request(wreq, false, netfs_rreq_trace_put_work);
		return;
	}
	smp_rmb(); /* Read ALL_QUEUED before lists. */

	transferred = LONG_MAX;
	for (s = 0; s < NR_IO_STREAMS; s++) {
		struct netfs_io_stream *stream = &wreq->io_streams[s];
		if (!stream->active)
			continue;
		if (!list_empty(&stream->subrequests)) {
			netfs_put_request(wreq, false, netfs_rreq_trace_put_work);
			return;
		}
		if (stream->transferred < transferred)
			transferred = stream->transferred;
	}

	/* Okay, declare that all I/O is complete. */
	wreq->transferred = transferred;
	trace_netfs_rreq(wreq, netfs_rreq_trace_write_done);

	if (wreq->io_streams[1].active &&
	    wreq->io_streams[1].failed) {
		/* Cache write failure doesn't prevent writeback completion
		 * unless we're in disconnected mode.
		 */
		ictx->ops->invalidate_cache(wreq);
	}

	if (wreq->cleanup)
		wreq->cleanup(wreq);

	if (wreq->origin == NETFS_DIO_WRITE &&
	    wreq->mapping->nrpages) {
		/* mmap may have got underfoot and we may now have folios
		 * locally covering the region we just wrote.  Attempt to
		 * discard the folios, but leave in place any modified locally.
		 * ->write_iter() is prevented from interfering by the DIO
		 * counter.
		 */
		pgoff_t first = wreq->start >> PAGE_SHIFT;
		pgoff_t last = (wreq->start + wreq->transferred - 1) >> PAGE_SHIFT;
		invalidate_inode_pages2_range(wreq->mapping, first, last);
	}

	if (wreq->origin == NETFS_DIO_WRITE)
		inode_dio_end(wreq->inode);

	_debug("finished");
	trace_netfs_rreq(wreq, netfs_rreq_trace_wake_ip);
	clear_bit_unlock(NETFS_RREQ_IN_PROGRESS, &wreq->flags);
	wake_up_bit(&wreq->flags, NETFS_RREQ_IN_PROGRESS);

	if (wreq->iocb) {
		wreq->iocb->ki_pos += wreq->transferred;
		if (wreq->iocb->ki_complete)
			wreq->iocb->ki_complete(
				wreq->iocb, wreq->error ? wreq->error : wreq->transferred);
		wreq->iocb = VFS_PTR_POISON;
	}

	netfs_clear_subrequests(wreq, false);
	netfs_put_request(wreq, false, netfs_rreq_trace_put_work_complete);
}

/*
 * Wake the collection work item.
 */
void netfs_wake_write_collector(struct netfs_io_request *wreq, bool was_async)
{
	if (!work_pending(&wreq->work)) {
		netfs_get_request(wreq, netfs_rreq_trace_get_work);
		if (!queue_work(system_unbound_wq, &wreq->work))
			netfs_put_request(wreq, was_async, netfs_rreq_trace_put_work_nq);
	}
}

/**
 * netfs_write_subrequest_terminated - Note the termination of a write operation.
 * @_op: The I/O request that has terminated.
 * @transferred_or_error: The amount of data transferred or an error code.
 * @was_async: The termination was asynchronous
 *
 * This tells the library that a contributory write I/O operation has
 * terminated, one way or another, and that it should collect the results.
 *
 * The caller indicates in @transferred_or_error the outcome of the operation,
 * supplying a positive value to indicate the number of bytes transferred or a
 * negative error code.  The library will look after reissuing I/O operations
 * as appropriate and writing downloaded data to the cache.
 *
 * If @was_async is true, the caller might be running in softirq or interrupt
 * context and we can't sleep.
 *
 * When this is called, ownership of the subrequest is transferred back to the
 * library, along with a ref.
 *
 * Note that %_op is a void* so that the function can be passed to
 * kiocb::term_func without the need for a casting wrapper.
 */
void netfs_write_subrequest_terminated(void *_op, ssize_t transferred_or_error,
				       bool was_async)
{
	struct netfs_io_subrequest *subreq = _op;
	struct netfs_io_request *wreq = subreq->rreq;
	struct netfs_io_stream *stream = &wreq->io_streams[subreq->stream_nr];

	_enter("%x[%x] %zd", wreq->debug_id, subreq->debug_index, transferred_or_error);

	switch (subreq->source) {
	case NETFS_UPLOAD_TO_SERVER:
		netfs_stat(&netfs_n_wh_upload_done);
		break;
	case NETFS_WRITE_TO_CACHE:
		netfs_stat(&netfs_n_wh_write_done);
		break;
	case NETFS_INVALID_WRITE:
		break;
	default:
		BUG();
	}

	if (IS_ERR_VALUE(transferred_or_error)) {
		subreq->error = transferred_or_error;
		if (subreq->error == -EAGAIN)
			set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
		else
			set_bit(NETFS_SREQ_FAILED, &subreq->flags);
		trace_netfs_failure(wreq, subreq, transferred_or_error, netfs_fail_write);

		switch (subreq->source) {
		case NETFS_WRITE_TO_CACHE:
			netfs_stat(&netfs_n_wh_write_failed);
			break;
		case NETFS_UPLOAD_TO_SERVER:
			netfs_stat(&netfs_n_wh_upload_failed);
			break;
		default:
			break;
		}
		trace_netfs_rreq(wreq, netfs_rreq_trace_set_pause);
		set_bit(NETFS_RREQ_PAUSE, &wreq->flags);
	} else {
		if (WARN(transferred_or_error > subreq->len - subreq->transferred,
			 "Subreq excess write: R=%x[%x] %zd > %zu - %zu",
			 wreq->debug_id, subreq->debug_index,
			 transferred_or_error, subreq->len, subreq->transferred))
			transferred_or_error = subreq->len - subreq->transferred;

		subreq->error = 0;
		subreq->transferred += transferred_or_error;

		if (subreq->transferred < subreq->len)
			set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
	}

	trace_netfs_sreq(subreq, netfs_sreq_trace_terminated);

	clear_bit_unlock(NETFS_SREQ_IN_PROGRESS, &subreq->flags);
	wake_up_bit(&subreq->flags, NETFS_SREQ_IN_PROGRESS);

	/* If we are at the head of the queue, wake up the collector,
	 * transferring a ref to it if we were the ones to do so.
	 */
	if (list_is_first(&subreq->rreq_link, &stream->subrequests))
		netfs_wake_write_collector(wreq, was_async);

	netfs_put_subrequest(subreq, was_async, netfs_sreq_trace_put_terminated);
}
EXPORT_SYMBOL(netfs_write_subrequest_terminated);
