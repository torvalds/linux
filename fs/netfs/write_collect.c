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
#define NEED_REASSESS		0x02	/* Need to loop round and reassess */
#define MADE_PROGRESS		0x04	/* Made progress cleaning up a stream or the folio set */
#define NEED_UNLOCK		0x08	/* The pagecache needs unlocking */
#define NEED_RETRY		0x10	/* A front op requests retrying */
#define SAW_FAILURE		0x20	/* One stream or hit a permanent failure */

static void netfs_dump_request(const struct netfs_io_request *rreq)
{
	pr_err("Request R=%08x r=%d fl=%lx or=%x e=%ld\n",
	       rreq->debug_id, refcount_read(&rreq->ref), rreq->flags,
	       rreq->origin, rreq->error);
	pr_err("  st=%llx tsl=%zx/%llx/%llx\n",
	       rreq->start, rreq->transferred, rreq->submitted, rreq->len);
	pr_err("  cci=%llx/%llx/%llx\n",
	       rreq->cleaned_to, rreq->collected_to, atomic64_read(&rreq->issued_to));
	pr_err("  iw=%pSR\n", rreq->netfs_ops->issue_write);
	for (int i = 0; i < NR_IO_STREAMS; i++) {
		const struct netfs_io_subrequest *sreq;
		const struct netfs_io_stream *s = &rreq->io_streams[i];

		pr_err("  str[%x] s=%x e=%d acnf=%u,%u,%u,%u\n",
		       s->stream_nr, s->source, s->error,
		       s->avail, s->active, s->need_retry, s->failed);
		pr_err("  str[%x] ct=%llx t=%zx\n",
		       s->stream_nr, s->collected_to, s->transferred);
		list_for_each_entry(sreq, &s->subrequests, rreq_link) {
			pr_err("  sreq[%x:%x] sc=%u s=%llx t=%zx/%zx r=%d f=%lx\n",
			       sreq->stream_nr, sreq->debug_index, sreq->source,
			       sreq->start, sreq->transferred, sreq->len,
			       refcount_read(&sreq->ref), sreq->flags);
		}
	}
}

/*
 * Successful completion of write of a folio to the server and/or cache.  Note
 * that we are not allowed to lock the folio here on pain of deadlocking with
 * truncate.
 */
int netfs_folio_written_back(struct folio *folio)
{
	enum netfs_folio_trace why = netfs_folio_trace_clear;
	struct netfs_inode *ictx = netfs_inode(folio->mapping->host);
	struct netfs_folio *finfo;
	struct netfs_group *group = NULL;
	int gcount = 0;

	if ((finfo = netfs_folio_info(folio))) {
		/* Streaming writes cannot be redirtied whilst under writeback,
		 * so discard the streaming record.
		 */
		unsigned long long fend;

		fend = folio_pos(folio) + finfo->dirty_offset + finfo->dirty_len;
		if (fend > ictx->zero_point)
			ictx->zero_point = fend;

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
 * Unlock any folios we've finished with.
 */
static void netfs_writeback_unlock_folios(struct netfs_io_request *wreq,
					  unsigned int *notes)
{
	struct folio_queue *folioq = wreq->buffer.tail;
	unsigned long long collected_to = wreq->collected_to;
	unsigned int slot = wreq->buffer.first_tail_slot;

	if (WARN_ON_ONCE(!folioq)) {
		pr_err("[!] Writeback unlock found empty rolling buffer!\n");
		netfs_dump_request(wreq);
		return;
	}

	if (wreq->origin == NETFS_PGPRIV2_COPY_TO_CACHE) {
		if (netfs_pgpriv2_unlock_copied_folios(wreq))
			*notes |= MADE_PROGRESS;
		return;
	}

	if (slot >= folioq_nr_slots(folioq)) {
		folioq = rolling_buffer_delete_spent(&wreq->buffer);
		if (!folioq)
			return;
		slot = 0;
	}

	for (;;) {
		struct folio *folio;
		struct netfs_folio *finfo;
		unsigned long long fpos, fend;
		size_t fsize, flen;

		folio = folioq_folio(folioq, slot);
		if (WARN_ONCE(!folio_test_writeback(folio),
			      "R=%08x: folio %lx is not under writeback\n",
			      wreq->debug_id, folio->index))
			trace_netfs_folio(folio, netfs_folio_trace_not_under_wback);

		fpos = folio_pos(folio);
		fsize = folio_size(folio);
		finfo = netfs_folio_info(folio);
		flen = finfo ? finfo->dirty_offset + finfo->dirty_len : fsize;

		fend = min_t(unsigned long long, fpos + flen, wreq->i_size);

		trace_netfs_collect_folio(wreq, folio, fend, collected_to);

		/* Unlock any folio we've transferred all of. */
		if (collected_to < fend)
			break;

		wreq->nr_group_rel += netfs_folio_written_back(folio);
		wreq->cleaned_to = fpos + fsize;
		*notes |= MADE_PROGRESS;

		/* Clean up the head folioq.  If we clear an entire folioq, then
		 * we can get rid of it provided it's not also the tail folioq
		 * being filled by the issuer.
		 */
		folioq_clear(folioq, slot);
		slot++;
		if (slot >= folioq_nr_slots(folioq)) {
			folioq = rolling_buffer_delete_spent(&wreq->buffer);
			if (!folioq)
				goto done;
			slot = 0;
		}

		if (fpos + fsize >= collected_to)
			break;
	}

	wreq->buffer.tail = folioq;
done:
	wreq->buffer.first_tail_slot = slot;
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
	unsigned long long collected_to, issued_to;
	unsigned int notes;
	int s;

	_enter("%llx-%llx", wreq->start, wreq->start + wreq->len);
	trace_netfs_collect(wreq);
	trace_netfs_rreq(wreq, netfs_rreq_trace_collect);

reassess_streams:
	issued_to = atomic64_read(&wreq->issued_to);
	smp_rmb();
	collected_to = ULLONG_MAX;
	if (wreq->origin == NETFS_WRITEBACK ||
	    wreq->origin == NETFS_WRITETHROUGH ||
	    wreq->origin == NETFS_PGPRIV2_COPY_TO_CACHE)
		notes = NEED_UNLOCK;
	else
		notes = 0;

	/* Remove completed subrequests from the front of the streams and
	 * advance the completion point on each stream.  We stop when we hit
	 * something that's in progress.  The issuer thread may be adding stuff
	 * to the tail whilst we're doing this.
	 */
	for (s = 0; s < NR_IO_STREAMS; s++) {
		stream = &wreq->io_streams[s];
		/* Read active flag before list pointers */
		if (!smp_load_acquire(&stream->active))
			continue;

		front = stream->front;
		while (front) {
			trace_netfs_collect_sreq(wreq, front);
			//_debug("sreq [%x] %llx %zx/%zx",
			//       front->debug_index, front->start, front->transferred, front->len);

			if (stream->collected_to < front->start) {
				trace_netfs_collect_gap(wreq, stream, issued_to, 'F');
				stream->collected_to = front->start;
			}

			/* Stall if the front is still undergoing I/O. */
			if (netfs_check_subreq_in_progress(front)) {
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
			spin_unlock(&wreq->lock);
			netfs_put_subrequest(remove,
					     notes & SAW_FAILURE ?
					     netfs_sreq_trace_put_cancel :
					     netfs_sreq_trace_put_done);
		}

		/* If we have an empty stream, we need to jump it forward
		 * otherwise the collection point will never advance.
		 */
		if (!front && issued_to > stream->collected_to) {
			trace_netfs_collect_gap(wreq, stream, issued_to, 'E');
			stream->collected_to = issued_to;
		}

		if (stream->collected_to < collected_to)
			collected_to = stream->collected_to;
	}

	if (collected_to != ULLONG_MAX && collected_to > wreq->collected_to)
		wreq->collected_to = collected_to;

	for (s = 0; s < NR_IO_STREAMS; s++) {
		stream = &wreq->io_streams[s];
		if (stream->active)
			trace_netfs_collect_stream(wreq, stream);
	}

	trace_netfs_collect_state(wreq, wreq->collected_to, notes);

	/* Unlock any folios that we have now finished with. */
	if (notes & NEED_UNLOCK) {
		if (wreq->cleaned_to < wreq->collected_to)
			netfs_writeback_unlock_folios(wreq, &notes);
	} else {
		wreq->cleaned_to = wreq->collected_to;
	}

	// TODO: Discard encryption buffers

	if (notes & NEED_RETRY)
		goto need_retry;

	if (notes & MADE_PROGRESS) {
		netfs_wake_rreq_flag(wreq, NETFS_RREQ_PAUSE, netfs_rreq_trace_unpause);
		//cond_resched();
		goto reassess_streams;
	}

	if (notes & NEED_REASSESS) {
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
bool netfs_write_collection(struct netfs_io_request *wreq)
{
	struct netfs_inode *ictx = netfs_inode(wreq->inode);
	size_t transferred;
	int s;

	_enter("R=%x", wreq->debug_id);

	netfs_collect_write_results(wreq);

	/* We're done when the app thread has finished posting subreqs and all
	 * the queues in all the streams are empty.
	 */
	if (!test_bit(NETFS_RREQ_ALL_QUEUED, &wreq->flags))
		return false;
	smp_rmb(); /* Read ALL_QUEUED before lists. */

	transferred = LONG_MAX;
	for (s = 0; s < NR_IO_STREAMS; s++) {
		struct netfs_io_stream *stream = &wreq->io_streams[s];
		if (!stream->active)
			continue;
		if (!list_empty(&stream->subrequests))
			return false;
		if (stream->transferred < transferred)
			transferred = stream->transferred;
	}

	/* Okay, declare that all I/O is complete. */
	wreq->transferred = transferred;
	trace_netfs_rreq(wreq, netfs_rreq_trace_write_done);

	if (wreq->io_streams[1].active &&
	    wreq->io_streams[1].failed &&
	    ictx->ops->invalidate_cache) {
		/* Cache write failure doesn't prevent writeback completion
		 * unless we're in disconnected mode.
		 */
		ictx->ops->invalidate_cache(wreq);
	}

	if ((wreq->origin == NETFS_UNBUFFERED_WRITE ||
	     wreq->origin == NETFS_DIO_WRITE) &&
	    !wreq->error)
		netfs_update_i_size(ictx, &ictx->inode, wreq->start, wreq->transferred);

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
	netfs_wake_rreq_flag(wreq, NETFS_RREQ_IN_PROGRESS, netfs_rreq_trace_wake_ip);
	/* As we cleared NETFS_RREQ_IN_PROGRESS, we acquired its ref. */

	if (wreq->iocb) {
		size_t written = min(wreq->transferred, wreq->len);
		wreq->iocb->ki_pos += written;
		if (wreq->iocb->ki_complete) {
			trace_netfs_rreq(wreq, netfs_rreq_trace_ki_complete);
			wreq->iocb->ki_complete(
				wreq->iocb, wreq->error ? wreq->error : written);
		}
		wreq->iocb = VFS_PTR_POISON;
	}

	netfs_clear_subrequests(wreq);
	return true;
}

void netfs_write_collection_worker(struct work_struct *work)
{
	struct netfs_io_request *rreq = container_of(work, struct netfs_io_request, work);

	netfs_see_request(rreq, netfs_rreq_trace_see_work);
	if (netfs_check_rreq_in_progress(rreq)) {
		if (netfs_write_collection(rreq))
			/* Drop the ref from the IN_PROGRESS flag. */
			netfs_put_request(rreq, netfs_rreq_trace_put_work_ip);
		else
			netfs_see_request(rreq, netfs_rreq_trace_see_work_complete);
	}
}

/**
 * netfs_write_subrequest_terminated - Note the termination of a write operation.
 * @_op: The I/O request that has terminated.
 * @transferred_or_error: The amount of data transferred or an error code.
 *
 * This tells the library that a contributory write I/O operation has
 * terminated, one way or another, and that it should collect the results.
 *
 * The caller indicates in @transferred_or_error the outcome of the operation,
 * supplying a positive value to indicate the number of bytes transferred or a
 * negative error code.  The library will look after reissuing I/O operations
 * as appropriate and writing downloaded data to the cache.
 *
 * When this is called, ownership of the subrequest is transferred back to the
 * library, along with a ref.
 *
 * Note that %_op is a void* so that the function can be passed to
 * kiocb::term_func without the need for a casting wrapper.
 */
void netfs_write_subrequest_terminated(void *_op, ssize_t transferred_or_error)
{
	struct netfs_io_subrequest *subreq = _op;
	struct netfs_io_request *wreq = subreq->rreq;

	_enter("%x[%x] %zd", wreq->debug_id, subreq->debug_index, transferred_or_error);

	switch (subreq->source) {
	case NETFS_UPLOAD_TO_SERVER:
		netfs_stat(&netfs_n_wh_upload_done);
		break;
	case NETFS_WRITE_TO_CACHE:
		netfs_stat(&netfs_n_wh_write_done);
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
	netfs_subreq_clear_in_progress(subreq);
	netfs_put_subrequest(subreq, netfs_sreq_trace_put_terminated);
}
EXPORT_SYMBOL(netfs_write_subrequest_terminated);
