// SPDX-License-Identifier: GPL-2.0-only
/* Object lifetime handling and tracing.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include "internal.h"

/*
 * Allocate an I/O request and initialise it.
 */
struct netfs_io_request *netfs_alloc_request(struct address_space *mapping,
					     struct file *file,
					     loff_t start, size_t len,
					     enum netfs_io_origin origin)
{
	static atomic_t debug_ids;
	struct inode *inode = file ? file_inode(file) : mapping->host;
	struct netfs_inode *ctx = netfs_inode(inode);
	struct netfs_io_request *rreq;
	mempool_t *mempool = ctx->ops->request_pool ?: &netfs_request_pool;
	struct kmem_cache *cache = mempool->pool_data;
	bool is_unbuffered = (origin == NETFS_UNBUFFERED_WRITE ||
			      origin == NETFS_DIO_READ ||
			      origin == NETFS_DIO_WRITE);
	bool cached = !is_unbuffered && netfs_is_cache_enabled(ctx);
	int ret;

	for (;;) {
		rreq = mempool_alloc(mempool, GFP_KERNEL);
		if (rreq)
			break;
		msleep(10);
	}

	memset(rreq, 0, kmem_cache_size(cache));
	rreq->start	= start;
	rreq->len	= len;
	rreq->upper_len	= len;
	rreq->origin	= origin;
	rreq->netfs_ops	= ctx->ops;
	rreq->mapping	= mapping;
	rreq->inode	= inode;
	rreq->i_size	= i_size_read(inode);
	rreq->debug_id	= atomic_inc_return(&debug_ids);
	rreq->wsize	= INT_MAX;
	spin_lock_init(&rreq->lock);
	INIT_LIST_HEAD(&rreq->io_streams[0].subrequests);
	INIT_LIST_HEAD(&rreq->io_streams[1].subrequests);
	INIT_LIST_HEAD(&rreq->subrequests);
	INIT_WORK(&rreq->work, NULL);
	refcount_set(&rreq->ref, 1);

	__set_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags);
	if (cached) {
		__set_bit(NETFS_RREQ_WRITE_TO_CACHE, &rreq->flags);
		if (test_bit(NETFS_ICTX_USE_PGPRIV2, &ctx->flags))
			/* Filesystem uses deprecated PG_private_2 marking. */
			__set_bit(NETFS_RREQ_USE_PGPRIV2, &rreq->flags);
	}
	if (file && file->f_flags & O_NONBLOCK)
		__set_bit(NETFS_RREQ_NONBLOCK, &rreq->flags);
	if (rreq->netfs_ops->init_request) {
		ret = rreq->netfs_ops->init_request(rreq, file);
		if (ret < 0) {
			mempool_free(rreq, rreq->netfs_ops->request_pool ?: &netfs_request_pool);
			return ERR_PTR(ret);
		}
	}

	trace_netfs_rreq_ref(rreq->debug_id, 1, netfs_rreq_trace_new);
	netfs_proc_add_rreq(rreq);
	netfs_stat(&netfs_n_rh_rreq);
	return rreq;
}

void netfs_get_request(struct netfs_io_request *rreq, enum netfs_rreq_ref_trace what)
{
	int r;

	__refcount_inc(&rreq->ref, &r);
	trace_netfs_rreq_ref(rreq->debug_id, r + 1, what);
}

void netfs_clear_subrequests(struct netfs_io_request *rreq, bool was_async)
{
	struct netfs_io_subrequest *subreq;
	struct netfs_io_stream *stream;
	int s;

	while (!list_empty(&rreq->subrequests)) {
		subreq = list_first_entry(&rreq->subrequests,
					  struct netfs_io_subrequest, rreq_link);
		list_del(&subreq->rreq_link);
		netfs_put_subrequest(subreq, was_async,
				     netfs_sreq_trace_put_clear);
	}

	for (s = 0; s < ARRAY_SIZE(rreq->io_streams); s++) {
		stream = &rreq->io_streams[s];
		while (!list_empty(&stream->subrequests)) {
			subreq = list_first_entry(&stream->subrequests,
						  struct netfs_io_subrequest, rreq_link);
			list_del(&subreq->rreq_link);
			netfs_put_subrequest(subreq, was_async,
					     netfs_sreq_trace_put_clear);
		}
	}
}

static void netfs_free_request_rcu(struct rcu_head *rcu)
{
	struct netfs_io_request *rreq = container_of(rcu, struct netfs_io_request, rcu);

	mempool_free(rreq, rreq->netfs_ops->request_pool ?: &netfs_request_pool);
	netfs_stat_d(&netfs_n_rh_rreq);
}

static void netfs_free_request(struct work_struct *work)
{
	struct netfs_io_request *rreq =
		container_of(work, struct netfs_io_request, work);
	unsigned int i;

	trace_netfs_rreq(rreq, netfs_rreq_trace_free);
	netfs_proc_del_rreq(rreq);
	netfs_clear_subrequests(rreq, false);
	if (rreq->netfs_ops->free_request)
		rreq->netfs_ops->free_request(rreq);
	if (rreq->cache_resources.ops)
		rreq->cache_resources.ops->end_operation(&rreq->cache_resources);
	if (rreq->direct_bv) {
		for (i = 0; i < rreq->direct_bv_count; i++) {
			if (rreq->direct_bv[i].bv_page) {
				if (rreq->direct_bv_unpin)
					unpin_user_page(rreq->direct_bv[i].bv_page);
			}
		}
		kvfree(rreq->direct_bv);
	}
	call_rcu(&rreq->rcu, netfs_free_request_rcu);
}

void netfs_put_request(struct netfs_io_request *rreq, bool was_async,
		       enum netfs_rreq_ref_trace what)
{
	unsigned int debug_id;
	bool dead;
	int r;

	if (rreq) {
		debug_id = rreq->debug_id;
		dead = __refcount_dec_and_test(&rreq->ref, &r);
		trace_netfs_rreq_ref(debug_id, r - 1, what);
		if (dead) {
			if (was_async) {
				rreq->work.func = netfs_free_request;
				if (!queue_work(system_unbound_wq, &rreq->work))
					BUG();
			} else {
				netfs_free_request(&rreq->work);
			}
		}
	}
}

/*
 * Allocate and partially initialise an I/O request structure.
 */
struct netfs_io_subrequest *netfs_alloc_subrequest(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	mempool_t *mempool = rreq->netfs_ops->subrequest_pool ?: &netfs_subrequest_pool;
	struct kmem_cache *cache = mempool->pool_data;

	for (;;) {
		subreq = mempool_alloc(rreq->netfs_ops->subrequest_pool ?: &netfs_subrequest_pool,
				       GFP_KERNEL);
		if (subreq)
			break;
		msleep(10);
	}

	memset(subreq, 0, kmem_cache_size(cache));
	INIT_WORK(&subreq->work, NULL);
	INIT_LIST_HEAD(&subreq->rreq_link);
	refcount_set(&subreq->ref, 2);
	subreq->rreq = rreq;
	subreq->debug_index = atomic_inc_return(&rreq->subreq_counter);
	netfs_get_request(rreq, netfs_rreq_trace_get_subreq);
	netfs_stat(&netfs_n_rh_sreq);
	return subreq;
}

void netfs_get_subrequest(struct netfs_io_subrequest *subreq,
			  enum netfs_sreq_ref_trace what)
{
	int r;

	__refcount_inc(&subreq->ref, &r);
	trace_netfs_sreq_ref(subreq->rreq->debug_id, subreq->debug_index, r + 1,
			     what);
}

static void netfs_free_subrequest(struct netfs_io_subrequest *subreq,
				  bool was_async)
{
	struct netfs_io_request *rreq = subreq->rreq;

	trace_netfs_sreq(subreq, netfs_sreq_trace_free);
	if (rreq->netfs_ops->free_subrequest)
		rreq->netfs_ops->free_subrequest(subreq);
	mempool_free(subreq, rreq->netfs_ops->subrequest_pool ?: &netfs_subrequest_pool);
	netfs_stat_d(&netfs_n_rh_sreq);
	netfs_put_request(rreq, was_async, netfs_rreq_trace_put_subreq);
}

void netfs_put_subrequest(struct netfs_io_subrequest *subreq, bool was_async,
			  enum netfs_sreq_ref_trace what)
{
	unsigned int debug_index = subreq->debug_index;
	unsigned int debug_id = subreq->rreq->debug_id;
	bool dead;
	int r;

	dead = __refcount_dec_and_test(&subreq->ref, &r);
	trace_netfs_sreq_ref(debug_id, debug_index, r - 1, what);
	if (dead)
		netfs_free_subrequest(subreq, was_async);
}
