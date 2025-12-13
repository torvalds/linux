// SPDX-License-Identifier: GPL-2.0-or-later
/* Single, monolithic object support (e.g. AFS directory).
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/sched/mm.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/netfs.h>
#include "internal.h"

/**
 * netfs_single_mark_inode_dirty - Mark a single, monolithic object inode dirty
 * @inode: The inode to mark
 *
 * Mark an inode that contains a single, monolithic object as dirty so that its
 * writepages op will get called.  If set, the SINGLE_NO_UPLOAD flag indicates
 * that the object will only be written to the cache and not uploaded (e.g. AFS
 * directory contents).
 */
void netfs_single_mark_inode_dirty(struct inode *inode)
{
	struct netfs_inode *ictx = netfs_inode(inode);
	bool cache_only = test_bit(NETFS_ICTX_SINGLE_NO_UPLOAD, &ictx->flags);
	bool caching = fscache_cookie_enabled(netfs_i_cookie(netfs_inode(inode)));

	if (cache_only && !caching)
		return;

	mark_inode_dirty(inode);

	if (caching && !(inode->i_state & I_PINNING_NETFS_WB)) {
		bool need_use = false;

		spin_lock(&inode->i_lock);
		if (!(inode->i_state & I_PINNING_NETFS_WB)) {
			inode->i_state |= I_PINNING_NETFS_WB;
			need_use = true;
		}
		spin_unlock(&inode->i_lock);

		if (need_use)
			fscache_use_cookie(netfs_i_cookie(ictx), true);
	}

}
EXPORT_SYMBOL(netfs_single_mark_inode_dirty);

static int netfs_single_begin_cache_read(struct netfs_io_request *rreq, struct netfs_inode *ctx)
{
	return fscache_begin_read_operation(&rreq->cache_resources, netfs_i_cookie(ctx));
}

static void netfs_single_cache_prepare_read(struct netfs_io_request *rreq,
					    struct netfs_io_subrequest *subreq)
{
	struct netfs_cache_resources *cres = &rreq->cache_resources;

	if (!cres->ops) {
		subreq->source = NETFS_DOWNLOAD_FROM_SERVER;
		return;
	}
	subreq->source = cres->ops->prepare_read(subreq, rreq->i_size);
	trace_netfs_sreq(subreq, netfs_sreq_trace_prepare);

}

static void netfs_single_read_cache(struct netfs_io_request *rreq,
				    struct netfs_io_subrequest *subreq)
{
	struct netfs_cache_resources *cres = &rreq->cache_resources;

	_enter("R=%08x[%x]", rreq->debug_id, subreq->debug_index);
	netfs_stat(&netfs_n_rh_read);
	cres->ops->read(cres, subreq->start, &subreq->io_iter, NETFS_READ_HOLE_FAIL,
			netfs_cache_read_terminated, subreq);
}

/*
 * Perform a read to a buffer from the cache or the server.  Only a single
 * subreq is permitted as the object must be fetched in a single transaction.
 */
static int netfs_single_dispatch_read(struct netfs_io_request *rreq)
{
	struct netfs_io_stream *stream = &rreq->io_streams[0];
	struct netfs_io_subrequest *subreq;
	int ret = 0;

	subreq = netfs_alloc_subrequest(rreq);
	if (!subreq)
		return -ENOMEM;

	subreq->source	= NETFS_SOURCE_UNKNOWN;
	subreq->start	= 0;
	subreq->len	= rreq->len;
	subreq->io_iter	= rreq->buffer.iter;

	__set_bit(NETFS_SREQ_IN_PROGRESS, &subreq->flags);

	spin_lock(&rreq->lock);
	list_add_tail(&subreq->rreq_link, &stream->subrequests);
	trace_netfs_sreq(subreq, netfs_sreq_trace_added);
	stream->front = subreq;
	/* Store list pointers before active flag */
	smp_store_release(&stream->active, true);
	spin_unlock(&rreq->lock);

	netfs_single_cache_prepare_read(rreq, subreq);
	switch (subreq->source) {
	case NETFS_DOWNLOAD_FROM_SERVER:
		netfs_stat(&netfs_n_rh_download);
		if (rreq->netfs_ops->prepare_read) {
			ret = rreq->netfs_ops->prepare_read(subreq);
			if (ret < 0)
				goto cancel;
		}

		rreq->netfs_ops->issue_read(subreq);
		rreq->submitted += subreq->len;
		break;
	case NETFS_READ_FROM_CACHE:
		trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
		netfs_single_read_cache(rreq, subreq);
		rreq->submitted += subreq->len;
		ret = 0;
		break;
	default:
		pr_warn("Unexpected single-read source %u\n", subreq->source);
		WARN_ON_ONCE(true);
		ret = -EIO;
		break;
	}

	smp_wmb(); /* Write lists before ALL_QUEUED. */
	set_bit(NETFS_RREQ_ALL_QUEUED, &rreq->flags);
	return ret;
cancel:
	netfs_put_subrequest(subreq, netfs_sreq_trace_put_cancel);
	return ret;
}

/**
 * netfs_read_single - Synchronously read a single blob of pages.
 * @inode: The inode to read from.
 * @file: The file we're using to read or NULL.
 * @iter: The buffer we're reading into.
 *
 * Fulfil a read request for a single monolithic object by drawing data from
 * the cache if possible, or the netfs if not.  The buffer may be larger than
 * the file content; unused beyond the EOF will be zero-filled.  The content
 * will be read with a single I/O request (though this may be retried).
 *
 * The calling netfs must initialise a netfs context contiguous to the vfs
 * inode before calling this.
 *
 * This is usable whether or not caching is enabled.  If caching is enabled,
 * the data will be stored as a single object into the cache.
 */
ssize_t netfs_read_single(struct inode *inode, struct file *file, struct iov_iter *iter)
{
	struct netfs_io_request *rreq;
	struct netfs_inode *ictx = netfs_inode(inode);
	ssize_t ret;

	rreq = netfs_alloc_request(inode->i_mapping, file, 0, iov_iter_count(iter),
				   NETFS_READ_SINGLE);
	if (IS_ERR(rreq))
		return PTR_ERR(rreq);

	ret = netfs_single_begin_cache_read(rreq, ictx);
	if (ret == -ENOMEM || ret == -EINTR || ret == -ERESTARTSYS)
		goto cleanup_free;

	netfs_stat(&netfs_n_rh_read_single);
	trace_netfs_read(rreq, 0, rreq->len, netfs_read_trace_read_single);

	rreq->buffer.iter = *iter;
	netfs_single_dispatch_read(rreq);

	ret = netfs_wait_for_read(rreq);
	netfs_put_request(rreq, netfs_rreq_trace_put_return);
	return ret;

cleanup_free:
	netfs_put_failed_request(rreq);
	return ret;
}
EXPORT_SYMBOL(netfs_read_single);
