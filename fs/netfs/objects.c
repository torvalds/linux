// SPDX-License-Identifier: GPL-2.0-only
/* Object lifetime handling and tracing.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include "internal.h"

/*
 * Allocate an I/O request and initialise it.
 */
struct netfs_io_request *netfs_alloc_request(
	const struct netfs_request_ops *ops, void *netfs_priv,
	struct file *file)
{
	static atomic_t debug_ids;
	struct netfs_io_request *rreq;

	rreq = kzalloc(sizeof(struct netfs_io_request), GFP_KERNEL);
	if (rreq) {
		rreq->netfs_ops	= ops;
		rreq->netfs_priv = netfs_priv;
		rreq->inode	= file_inode(file);
		rreq->i_size	= i_size_read(rreq->inode);
		rreq->debug_id	= atomic_inc_return(&debug_ids);
		INIT_LIST_HEAD(&rreq->subrequests);
		INIT_WORK(&rreq->work, netfs_rreq_work);
		refcount_set(&rreq->ref, 1);
		__set_bit(NETFS_RREQ_IN_PROGRESS, &rreq->flags);
		if (ops->init_request)
			ops->init_request(rreq, file);
		netfs_stat(&netfs_n_rh_rreq);
	}

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

	while (!list_empty(&rreq->subrequests)) {
		subreq = list_first_entry(&rreq->subrequests,
					  struct netfs_io_subrequest, rreq_link);
		list_del(&subreq->rreq_link);
		netfs_put_subrequest(subreq, was_async,
				     netfs_sreq_trace_put_clear);
	}
}

static void netfs_free_request(struct work_struct *work)
{
	struct netfs_io_request *rreq =
		container_of(work, struct netfs_io_request, work);
	netfs_clear_subrequests(rreq, false);
	if (rreq->netfs_priv)
		rreq->netfs_ops->cleanup(rreq->mapping, rreq->netfs_priv);
	trace_netfs_rreq(rreq, netfs_rreq_trace_free);
	if (rreq->cache_resources.ops)
		rreq->cache_resources.ops->end_operation(&rreq->cache_resources);
	kfree(rreq);
	netfs_stat_d(&netfs_n_rh_rreq);
}

void netfs_put_request(struct netfs_io_request *rreq, bool was_async,
		       enum netfs_rreq_ref_trace what)
{
	unsigned int debug_id = rreq->debug_id;
	bool dead;
	int r;

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

/*
 * Allocate and partially initialise an I/O request structure.
 */
struct netfs_io_subrequest *netfs_alloc_subrequest(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;

	subreq = kzalloc(sizeof(struct netfs_io_subrequest), GFP_KERNEL);
	if (subreq) {
		INIT_LIST_HEAD(&subreq->rreq_link);
		refcount_set(&subreq->ref, 2);
		subreq->rreq = rreq;
		netfs_get_request(rreq, netfs_rreq_trace_get_subreq);
		netfs_stat(&netfs_n_rh_sreq);
	}

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
	kfree(subreq);
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
