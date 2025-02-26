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
	if (!test_bit(NETFS_RREQ_DONT_UNLOCK_FOLIOS, &rreq->flags)) {
		if (folio->index == rreq->no_unlock_folio &&
		    test_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags)) {
			_debug("no unlock");
		} else {
			trace_netfs_folio(folio, netfs_folio_trace_read_unlock);
			folio_unlock(folio);
		}
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
		} else {
			if (!stream->failed)
				stream->transferred = stream->collected_to - rreq->start;
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
		netfs_put_subrequest(remove, false,
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
	if ((notes & MADE_PROGRESS) && test_bit(NETFS_RREQ_PAUSE, &rreq->flags)) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_unpause);
		clear_bit_unlock(NETFS_RREQ_PAUSE, &rreq->flags);
		smp_mb__after_atomic(); /* Set PAUSE before task state */
		wake_up(&rreq->waitq);
	}

	if (notes & MADE_PROGRESS) {
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
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	unsigned int i;

	/* Collect unbuffered reads and direct reads, adding up the transfer
	 * sizes until we find the first short or failed subrequest.
	 */
	list_for_each_entry(subreq, &stream->subrequests, rreq_link) {
		rreq->transferred += subreq->transferred;

		if (subreq->transferred < subreq->len ||
		    test_bit(NETFS_SREQ_FAILED, &subreq->flags)) {
			rreq->error = subreq->error;
			break;
		}
	}

	if (rreq->origin == NETFS_DIO_READ) {
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
	if (rreq->origin == NETFS_DIO_READ)
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
static void netfs_read_collection(struct netfs_io_request *rreq)
{
	struct netfs_io_stream *stream = &rreq->io_streams[0];

	netfs_collect_read_results(rreq);

	/* We're done when the app thread has finished posting subreqs and the
	 * queue is empty.
	 */
	if (!test_bit(NETFS_RREQ_ALL_QUEUED, &rreq->flags))
		return;
	smp_rmb(); /* Read ALL_QUEUED before subreq lists. */

	if (!list_empty(&stream->subrequests))
		return;

	/* Okay, declare that all I/O is complete. */
	rreq->transferred = stream->transferred;
	trace_netfs_rreq(rreq, netfs_rreq_trace_complete);

	//netfs_rreq_is_still_valid(rreq);

	switch (rreq->origin) {
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

	trace_netfs_rreq(rreq, netfs_rreq_trace_wake_ip);
	clear_and_wake_up_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags);

	trace_netfs_rreq(rreq, netfs_rreq_trace_done);
	netfs_clear_subrequests(rreq, false);
	netfs_unlock_abandoned_read_pages(rreq);
	if (unlikely(rreq->copy_to_cache))
		netfs_pgpriv2_end_copy_to_cache(rreq);
}

void netfs_read_collection_worker(struct work_struct *work)
{
	struct netfs_io_request *rreq = container_of(work, struct netfs_io_request, work);

	netfs_see_request(rreq, netfs_rreq_trace_see_work);
	if (test_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags))
		netfs_read_collection(rreq);
	netfs_put_request(rreq, false, netfs_rreq_trace_put_work);
}

/*
 * Wake the collection work item.
 */
void netfs_wake_read_collector(struct netfs_io_request *rreq)
{
	if (test_bit(NETFS_RREQ_OFFLOAD_COLLECTION, &rreq->flags) &&
	    !test_bit(NETFS_RREQ_RETRYING, &rreq->flags)) {
		if (!work_pending(&rreq->work)) {
			netfs_get_request(rreq, netfs_rreq_trace_get_work);
			if (!queue_work(system_unbound_wq, &rreq->work))
				netfs_put_request(rreq, true, netfs_rreq_trace_put_work_nq);
		}
	} else {
		trace_netfs_rreq(rreq, netfs_rreq_trace_wake_queue);
		wake_up(&rreq->waitq);
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
		netfs_wake_read_collector(rreq);
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
	struct netfs_io_stream *stream = &rreq->io_streams[0];

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

	clear_bit_unlock(NETFS_SREQ_IN_PROGRESS, &subreq->flags);
	smp_mb__after_atomic(); /* Clear IN_PROGRESS before task state */

	/* If we are at the head of the queue, wake up the collector. */
	if (list_is_first(&subreq->rreq_link, &stream->subrequests) ||
	    test_bit(NETFS_RREQ_RETRYING, &rreq->flags))
		netfs_wake_read_collector(rreq);

	netfs_put_subrequest(subreq, true, netfs_sreq_trace_put_terminated);
}
EXPORT_SYMBOL(netfs_read_subreq_terminated);

/*
 * Handle termination of a read from the cache.
 */
void netfs_cache_read_terminated(void *priv, ssize_t transferred_or_error, bool was_async)
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

/*
 * Wait for the read operation to complete, successfully or otherwise.
 */
ssize_t netfs_wait_for_read(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	DEFINE_WAIT(myself);
	ssize_t ret;

	for (;;) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_wait_queue);
		prepare_to_wait(&rreq->waitq, &myself, TASK_UNINTERRUPTIBLE);

		subreq = list_first_entry_or_null(&stream->subrequests,
						  struct netfs_io_subrequest, rreq_link);
		if (subreq &&
		    (!test_bit(NETFS_SREQ_IN_PROGRESS, &subreq->flags) ||
		     test_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags))) {
			__set_current_state(TASK_RUNNING);
			netfs_read_collection(rreq);
			continue;
		}

		if (!test_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags))
			break;

		schedule();
		trace_netfs_rreq(rreq, netfs_rreq_trace_woke_queue);
	}

	finish_wait(&rreq->waitq, &myself);

	ret = rreq->error;
	if (ret == 0) {
		ret = rreq->transferred;
		switch (rreq->origin) {
		case NETFS_DIO_READ:
		case NETFS_READ_SINGLE:
			ret = rreq->transferred;
			break;
		default:
			if (rreq->submitted < rreq->len) {
				trace_netfs_failure(rreq, NULL, ret, netfs_fail_short_read);
				ret = -EIO;
			}
			break;
		}
	}

	return ret;
}

/*
 * Wait for a paused read operation to unpause or complete in some manner.
 */
void netfs_wait_for_pause(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	DEFINE_WAIT(myself);

	trace_netfs_rreq(rreq, netfs_rreq_trace_wait_pause);

	for (;;) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_wait_queue);
		prepare_to_wait(&rreq->waitq, &myself, TASK_UNINTERRUPTIBLE);

		subreq = list_first_entry_or_null(&stream->subrequests,
						  struct netfs_io_subrequest, rreq_link);
		if (subreq &&
		    (!test_bit(NETFS_SREQ_IN_PROGRESS, &subreq->flags) ||
		     test_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags))) {
			__set_current_state(TASK_RUNNING);
			netfs_read_collection(rreq);
			continue;
		}

		if (!test_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags) ||
		    !test_bit(NETFS_RREQ_PAUSE, &rreq->flags))
			break;

		schedule();
		trace_netfs_rreq(rreq, netfs_rreq_trace_woke_queue);
	}

	finish_wait(&rreq->waitq, &myself);
}
