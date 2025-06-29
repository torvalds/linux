// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem read subrequest result collection, assessment and
 * retrying.
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

/* Notes made in the collector */
#define HIT_PENDING	0x01	/* A front op was still pending */
#define MADE_PROGRESS	0x04	/* Made progress cleaning up a stream or the folio set */
#define BUFFERED	0x08	/* The pagecache needs cleaning up */
#define NEED_RETRY	0x10	/* A front op requests retrying */
#define COPY_TO_CACHE	0x40	/* Need to copy subrequest to cache */
#define ABANDON_SREQ	0x80	/* Need to abandon untransferred part of subrequest */

/*
 * Clear the unread part of an I/O request.
 */
static void netfs_clear_unread(struct netfs_io_subrequest *subreq)
{
	netfs_reset_iter(subreq);
	WARN_ON_ONCE(subreq->len - subreq->transferred != iov_iter_count(&subreq->io_iter));
	iov_iter_zero(iov_iter_count(&subreq->io_iter), &subreq->io_iter);
	if (subreq->start + subreq->transferred >= subreq->rreq->i_size)
		__set_bit(NETFS_SREQ_HIT_EOF, &subreq->flags);
}

/*
 * Flush, mark and unlock a folio that's now completely read.  If we want to
 * cache the folio, we set the group to NETFS_FOLIO_COPY_TO_CACHE, mark it
 * dirty and let writeback handle it.
 */
static void netfs_unlock_read_folio(struct netfs_io_request *rreq,
				    struct folio_queue *folioq,
				    int slot)
{
	struct netfs_folio *finfo;
	struct folio *folio = folioq_folio(folioq, slot);

	if (unlikely(folio_pos(folio) < rreq->abandon_to)) {
		trace_netfs_folio(folio, netfs_folio_trace_abandon);
		goto just_unlock;
	}

	flush_dcache_folio(folio);
	folio_mark_uptodate(folio);

	if (!test_bit(NETFS_RREQ_USE_PGPRIV2, &rreq->flags)) {
		finfo = netfs_folio_info(folio);
		if (finfo) {
			trace_netfs_folio(folio, netfs_folio_trace_filled_gaps);
			if (finfo->netfs_group)
				folio_change_private(folio, finfo->netfs_group);
			else
				folio_detach_private(folio);
			kfree(finfo);
		}

		if (test_bit(NETFS_RREQ_FOLIO_COPY_TO_CACHE, &rreq->flags)) {
			if (!WARN_ON_ONCE(folio_get_private(folio) != NULL)) {
				trace_netfs_folio(folio, netfs_folio_trace_copy_to_cache);
				folio_attach_private(folio, NETFS_FOLIO_COPY_TO_CACHE);
				folio_mark_dirty(folio);
			}
		} else {
			trace_netfs_folio(folio, netfs_folio_trace_read_done);
		}

		folioq_clear(folioq, slot);
	} else {
		// TODO: Use of PG_private_2 is deprecated.
		if (test_bit(NETFS_RREQ_FOLIO_COPY_TO_CACHE, &rreq->flags))
			netfs_pgpriv2_copy_to_cache(rreq, folio);
	}

just_unlock:
	if (folio->index == rreq->no_unlock_folio &&
	    test_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags)) {
		_debug("no unlock");
	} else {
		trace_netfs_folio(folio, netfs_folio_trace_read_unlock);
		folio_unlock(folio);
	}

	folioq_clear(folioq, slot);
}

/*
 * Unlock any folios we've finished with.
 */
static void netfs_read_unlock_folios(struct netfs_io_request *rreq,
				     unsigned int *notes)
{
	struct folio_queue *folioq = rreq->buffer.tail;
	unsigned long long collected_to = rreq->collected_to;
	unsigned int slot = rreq->buffer.first_tail_slot;

	if (rreq->cleaned_to >= rreq->collected_to)
		return;

	// TODO: Begin decryption

	if (slot >= folioq_nr_slots(folioq)) {
		folioq = rolling_buffer_delete_spent(&rreq->buffer);
		if (!folioq) {
			rreq->front_folio_order = 0;
			return;
		}
		slot = 0;
	}

	for (;;) {
		struct folio *folio;
		unsigned long long fpos, fend;
		unsigned int order;
		size_t fsize;

		if (*notes & COPY_TO_CACHE)
			set_bit(NETFS_RREQ_FOLIO_COPY_TO_CACHE, &rreq->flags);

		folio = folioq_folio(folioq, slot);
		if (WARN_ONCE(!folio_test_locked(folio),
			      "R=%08x: folio %lx is not locked\n",
			      rreq->debug_id, folio->index))
			trace_netfs_folio(folio, netfs_folio_trace_not_locked);

		order = folioq_folio_order(folioq, slot);
		rreq->front_folio_order = order;
		fsize = PAGE_SIZE << order;
		fpos = folio_pos(folio);
		fend = umin(fpos + fsize, rreq->i_size);

		trace_netfs_collect_folio(rreq, folio, fend, collected_to);

		/* Unlock any folio we've transferred all of. */
		if (collected_to < fend)
			break;

		netfs_unlock_read_folio(rreq, folioq, slot);
		WRITE_ONCE(rreq->cleaned_to, fpos + fsize);
		*notes |= MADE_PROGRESS;

		clear_bit(NETFS_RREQ_FOLIO_COPY_TO_CACHE, &rreq->flags);

		/* Clean up the head folioq.  If we clear an entire folioq, then
		 * we can get rid of it provided it's not also the tail folioq
		 * being filled by the issuer.
		 */
		folioq_clear(folioq, slot);
		slot++;
		if (slot >= folioq_nr_slots(folioq)) {
			folioq = rolling_buffer_delete_spent(&rreq->buffer);
			if (!folioq)
				goto done;
			slot = 0;
			trace_netfs_folioq(folioq, netfs_trace_folioq_read_progress);
		}

		if (fpos + fsize >= collected_to)
			break;
	}

	rreq->buffer.tail = folioq;
done:
	rreq->buffer.first_tail_slot = slot;
}

/*
 * Collect and assess the results of various read subrequests.  We may need to
 * retry some of the results.
 *
 * Note that we have a sequence of subrequests, which may be drawing on
 * different sources and may or may not be the same size or starting position
 * and may not even correspond in boundary alignment.
 */
static void netfs_collect_read_results(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *front, *remove;
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	unsigned int notes;

	_enter("%llx-%llx", rreq->start, rreq->start + rreq->len);
	trace_netfs_rreq(rreq, netfs_rreq_trace_collect);
	trace_netfs_collect(rreq);

reassess:
	if (rreq->origin == NETFS_READAHEAD ||
	    rreq->origin == NETFS_READPAGE ||
	    rreq->origin == NETFS_READ_FOR_WRITE)
		notes = BUFFERED;
	else
		notes = 0;

	/* Remove completed subrequests from the front of the stream and
	 * advance the completion point.  We stop when we hit something that's
	 * in progress.  The issuer thread may be adding stuff to the tail
	 * whilst we're doing this.
	 */
	front = READ_ONCE(stream->front);
	while (front) {
		size_t transferred;

		trace_netfs_collect_sreq(rreq, front);
		_debug("sreq [%x] %llx %zx/%zx",
		       front->debug_index, front->start, front->transferred, front->len);

		if (stream->collected_to < front->start) {
			trace_netfs_collect_gap(rreq, stream, front->start, 'F');
			stream->collected_to = front->start;
		}

		if (test_bit(NETFS_SREQ_IN_PROGRESS, &front->flags))
			notes |= HIT_PENDING;
		smp_rmb(); /* Read counters after IN_PROGRESS flag. */
		transferred = READ_ONCE(front->transferred);

		/* If we can now collect the next folio, do so.  We don't want
		 * to defer this as we have to decide whether we need to copy
		 * to the cache or not, and that may differ between adjacent
		 * subreqs.
		 */
		if (notes & BUFFERED) {
			size_t fsize = PAGE_SIZE << rreq->front_folio_order;

			/* Clear the tail of a short read. */
			if (!(notes & HIT_PENDING) &&
			    front->error == 0 &&
			    transferred < front->len &&
			    (test_bit(NETFS_SREQ_HIT_EOF, &front->flags) ||
			     test_bit(NETFS_SREQ_CLEAR_TAIL, &front->flags))) {
				netfs_clear_unread(front);
				transferred = front->transferred = front->len;
				trace_netfs_sreq(front, netfs_sreq_trace_clear);
			}

			stream->collected_to = front->start + transferred;
			rreq->collected_to = stream->collected_to;

			if (test_bit(NETFS_SREQ_COPY_TO_CACHE, &front->flags))
				notes |= COPY_TO_CACHE;

			if (test_bit(NETFS_SREQ_FAILED, &front->flags)) {
				rreq->abandon_to = front->start + front->len;
				front->transferred = front->len;
				transferred = front->len;
				trace_netfs_rreq(rreq, netfs_rreq_trace_set_abandon);
			}
			if (front->start + transferred >= rreq->cleaned_to + fsize ||
			    test_bit(NETFS_SREQ_HIT_EOF, &front->flags))
				netfs_read_unlock_folios(rreq, &notes);
		} else {
			stream->collected_to = front->start + transferred;
			rreq->collected_to = stream->collected_to;
		}

		/* Stall if the front is still undergoing I/O. */
		if (notes & HIT_PENDING)
			break;

		if (test_bit(NETFS_SREQ_FAILED, &front->flags)) {
			if (!stream->failed) {
				stream->error = front->error;
				rreq->error = front->error;
				set_bit(NETFS_RREQ_FAILED, &rreq->flags);
				stream->failed = true;
			}
			notes |= MADE_PROGRESS | ABANDON_SREQ;
		} else if (test_bit(NETFS_SREQ_NEED_RETRY, &front->flags)) {
			stream->need_retry = true;
			notes |= NEED_RETRY | MADE_PROGRESS;
			break;
		} else if (test_bit(NETFS_RREQ_SHORT_TRANSFER, &rreq->flags)) {
			notes |= MADE_PROGRESS;
		} else {
			if (!stream->failed)
				stream->transferred += transferred;
			if (front->transferred < front->len)
				set_bit(NETFS_RREQ_SHORT_TRANSFER, &rreq->flags);
			notes |= MADE_PROGRESS;
		}

		/* Remove if completely consumed. */
		stream->source = front->source;
		spin_lock(&rreq->lock);

		remove = front;
		trace_netfs_sreq(front, netfs_sreq_trace_discard);
		list_del_init(&front->rreq_link);
		front = list_first_entry_or_null(&stream->subrequests,
						 struct netfs_io_subrequest, rreq_link);
		stream->front = front;
		spin_unlock(&rreq->lock);
		netfs_put_subrequest(remove,
				     notes & ABANDON_SREQ ?
				     netfs_sreq_trace_put_abandon :
				     netfs_sreq_trace_put_done);
	}

	trace_netfs_collect_stream(rreq, stream);
	trace_netfs_collect_state(rreq, rreq->collected_to, notes);

	if (!(notes & BUFFERED))
		rreq->cleaned_to = rreq->collected_to;

	if (notes & NEED_RETRY)
		goto need_retry;
	if (notes & MADE_PROGRESS) {
		netfs_wake_rreq_flag(rreq, NETFS_RREQ_PAUSE, netfs_rreq_trace_unpause);
		//cond_resched();
		goto reassess;
	}

out:
	_leave(" = %x", notes);
	return;

need_retry:
	/* Okay...  We're going to have to retry parts of the stream.  Note
	 * that any partially completed op will have had any wholly transferred
	 * folios removed from it.
	 */
	_debug("retry");
	netfs_retry_reads(rreq);
	goto out;
}

/*
 * Do page flushing and suchlike after DIO.
 */
static void netfs_rreq_assess_dio(struct netfs_io_request *rreq)
{
	unsigned int i;

	if (rreq->origin == NETFS_UNBUFFERED_READ ||
	    rreq->origin == NETFS_DIO_READ) {
		for (i = 0; i < rreq->direct_bv_count; i++) {
			flush_dcache_page(rreq->direct_bv[i].bv_page);
			// TODO: cifs marks pages in the destination buffer
			// dirty under some circumstances after a read.  Do we
			// need to do that too?
			set_page_dirty(rreq->direct_bv[i].bv_page);
		}
	}

	if (rreq->iocb) {
		rreq->iocb->ki_pos += rreq->transferred;
		if (rreq->iocb->ki_complete)
			rreq->iocb->ki_complete(
				rreq->iocb, rreq->error ? rreq->error : rreq->transferred);
	}
	if (rreq->netfs_ops->done)
		rreq->netfs_ops->done(rreq);
	if (rreq->origin == NETFS_UNBUFFERED_READ ||
	    rreq->origin == NETFS_DIO_READ)
		inode_dio_end(rreq->inode);
}

/*
 * Do processing after reading a monolithic single object.
 */
static void netfs_rreq_assess_single(struct netfs_io_request *rreq)
{
	struct netfs_io_stream *stream = &rreq->io_streams[0];

	if (!rreq->error && stream->source == NETFS_DOWNLOAD_FROM_SERVER &&
	    fscache_resources_valid(&rreq->cache_resources)) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_dirty);
		netfs_single_mark_inode_dirty(rreq->inode);
	}

	if (rreq->iocb) {
		rreq->iocb->ki_pos += rreq->transferred;
		if (rreq->iocb->ki_complete)
			rreq->iocb->ki_complete(
				rreq->iocb, rreq->error ? rreq->error : rreq->transferred);
	}
	if (rreq->netfs_ops->done)
		rreq->netfs_ops->done(rreq);
}

/*
 * Perform the collection of subrequests and folios.
 *
 * Note that we're in normal kernel thread context at this point, possibly
 * running on a workqueue.
 */
bool netfs_read_collection(struct netfs_io_request *rreq)
{
	struct netfs_io_stream *stream = &rreq->io_streams[0];

	netfs_collect_read_results(rreq);

	/* We're done when the app thread has finished posting subreqs and the
	 * queue is empty.
	 */
	if (!test_bit(NETFS_RREQ_ALL_QUEUED, &rreq->flags))
		return false;
	smp_rmb(); /* Read ALL_QUEUED before subreq lists. */

	if (!list_empty(&stream->subrequests))
		return false;

	/* Okay, declare that all I/O is complete. */
	rreq->transferred = stream->transferred;
	trace_netfs_rreq(rreq, netfs_rreq_trace_complete);

	//netfs_rreq_is_still_valid(rreq);

	switch (rreq->origin) {
	case NETFS_UNBUFFERED_READ:
	case NETFS_DIO_READ:
	case NETFS_READ_GAPS:
		netfs_rreq_assess_dio(rreq);
		break;
	case NETFS_READ_SINGLE:
		netfs_rreq_assess_single(rreq);
		break;
	default:
		break;
	}
	task_io_account_read(rreq->transferred);

	netfs_wake_rreq_flag(rreq, NETFS_RREQ_IN_PROGRESS, netfs_rreq_trace_wake_ip);
	/* As we cleared NETFS_RREQ_IN_PROGRESS, we acquired its ref. */

	trace_netfs_rreq(rreq, netfs_rreq_trace_done);
	netfs_clear_subrequests(rreq);
	netfs_unlock_abandoned_read_pages(rreq);
	if (unlikely(rreq->copy_to_cache))
		netfs_pgpriv2_end_copy_to_cache(rreq);
	return true;
}

void netfs_read_collection_worker(struct work_struct *work)
{
	struct netfs_io_request *rreq = container_of(work, struct netfs_io_request, work);

	netfs_see_request(rreq, netfs_rreq_trace_see_work);
	if (test_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags)) {
		if (netfs_read_collection(rreq))
			/* Drop the ref from the IN_PROGRESS flag. */
			netfs_put_request(rreq, netfs_rreq_trace_put_work_ip);
		else
			netfs_see_request(rreq, netfs_rreq_trace_see_work_complete);
	}
}

/**
 * netfs_read_subreq_progress - Note progress of a read operation.
 * @subreq: The read request that has terminated.
 *
 * This tells the read side of netfs lib that a contributory I/O operation has
 * made some progress and that it may be possible to unlock some folios.
 *
 * Before calling, the filesystem should update subreq->transferred to track
 * the amount of data copied into the output buffer.
 */
void netfs_read_subreq_progress(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	size_t fsize = PAGE_SIZE << rreq->front_folio_order;

	trace_netfs_sreq(subreq, netfs_sreq_trace_progress);

	/* If we are at the head of the queue, wake up the collector,
	 * getting a ref to it if we were the ones to do so.
	 */
	if (subreq->start + subreq->transferred > rreq->cleaned_to + fsize &&
	    (rreq->origin == NETFS_READAHEAD ||
	     rreq->origin == NETFS_READPAGE ||
	     rreq->origin == NETFS_READ_FOR_WRITE) &&
	    list_is_first(&subreq->rreq_link, &stream->subrequests)
	    ) {
		__set_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags);
		netfs_wake_collector(rreq);
	}
}
EXPORT_SYMBOL(netfs_read_subreq_progress);

/**
 * netfs_read_subreq_terminated - Note the termination of an I/O operation.
 * @subreq: The I/O request that has terminated.
 *
 * This tells the read helper that a contributory I/O operation has terminated,
 * one way or another, and that it should integrate the results.
 *
 * The caller indicates the outcome of the operation through @subreq->error,
 * supplying 0 to indicate a successful or retryable transfer (if
 * NETFS_SREQ_NEED_RETRY is set) or a negative error code.  The helper will
 * look after reissuing I/O operations as appropriate and writing downloaded
 * data to the cache.
 *
 * Before calling, the filesystem should update subreq->transferred to track
 * the amount of data copied into the output buffer.
 */
void netfs_read_subreq_terminated(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;

	switch (subreq->source) {
	case NETFS_READ_FROM_CACHE:
		netfs_stat(&netfs_n_rh_read_done);
		break;
	case NETFS_DOWNLOAD_FROM_SERVER:
		netfs_stat(&netfs_n_rh_download_done);
		break;
	default:
		break;
	}

	/* Deal with retry requests, short reads and errors.  If we retry
	 * but don't make progress, we abandon the attempt.
	 */
	if (!subreq->error && subreq->transferred < subreq->len) {
		if (test_bit(NETFS_SREQ_HIT_EOF, &subreq->flags)) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_hit_eof);
		} else if (test_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags)) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_need_clear);
		} else if (test_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags)) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_need_retry);
		} else if (test_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags)) {
			__set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
			trace_netfs_sreq(subreq, netfs_sreq_trace_partial_read);
		} else {
			__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
			subreq->error = -ENODATA;
			trace_netfs_sreq(subreq, netfs_sreq_trace_short);
		}
	}

	if (unlikely(subreq->error < 0)) {
		trace_netfs_failure(rreq, subreq, subreq->error, netfs_fail_read);
		if (subreq->source == NETFS_READ_FROM_CACHE) {
			netfs_stat(&netfs_n_rh_read_failed);
			__set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
		} else {
			netfs_stat(&netfs_n_rh_download_failed);
			__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
		}
		trace_netfs_rreq(rreq, netfs_rreq_trace_set_pause);
		set_bit(NETFS_RREQ_PAUSE, &rreq->flags);
	}

	trace_netfs_sreq(subreq, netfs_sreq_trace_terminated);
	netfs_subreq_clear_in_progress(subreq);
	netfs_put_subrequest(subreq, netfs_sreq_trace_put_terminated);
}
EXPORT_SYMBOL(netfs_read_subreq_terminated);

/*
 * Handle termination of a read from the cache.
 */
void netfs_cache_read_terminated(void *priv, ssize_t transferred_or_error)
{
	struct netfs_io_subrequest *subreq = priv;

	if (transferred_or_error > 0) {
		subreq->error = 0;
		if (transferred_or_error > 0) {
			subreq->transferred += transferred_or_error;
			__set_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags);
		}
	} else {
		subreq->error = transferred_or_error;
	}
	netfs_read_subreq_terminated(subreq);
}
