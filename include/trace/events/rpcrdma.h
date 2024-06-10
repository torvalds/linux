/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017, 2018 Oracle.  All rights reserved.
 *
 * Trace point definitions for the "rpcrdma" subsystem.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rpcrdma

#if !defined(_TRACE_RPCRDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RPCRDMA_H

#include <linux/scatterlist.h>
#include <linux/sunrpc/rpc_rdma_cid.h>
#include <linux/tracepoint.h>
#include <rdma/ib_cm.h>

#include <trace/misc/rdma.h>
#include <trace/misc/sunrpc.h>

/**
 ** Event classes
 **/

DECLARE_EVENT_CLASS(rpcrdma_simple_cid_class,
	TP_PROTO(
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
	),

	TP_printk("cq.id=%d cid=%d",
		__entry->cq_id, __entry->completion_id
	)
);

#define DEFINE_SIMPLE_CID_EVENT(name)					\
		DEFINE_EVENT(rpcrdma_simple_cid_class, name,		\
				TP_PROTO(				\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(cid)				\
		)

DECLARE_EVENT_CLASS(rpcrdma_completion_class,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(wc, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned long, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->status = wc->status;
		if (wc->status)
			__entry->vendor_err = wc->vendor_err;
		else
			__entry->vendor_err = 0;
	),

	TP_printk("cq.id=%u cid=%d status=%s (%lu/0x%x)",
		__entry->cq_id, __entry->completion_id,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

#define DEFINE_COMPLETION_EVENT(name)					\
		DEFINE_EVENT(rpcrdma_completion_class, name,		\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(wc, cid))

DECLARE_EVENT_CLASS(rpcrdma_send_flush_class,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(wc, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned long, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->status = wc->status;
		__entry->vendor_err = wc->vendor_err;
	),

	TP_printk("cq.id=%u cid=%d status=%s (%lu/0x%x)",
		__entry->cq_id, __entry->completion_id,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

#define DEFINE_SEND_FLUSH_EVENT(name)					\
		DEFINE_EVENT(rpcrdma_send_flush_class, name,		\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(wc, cid))

DECLARE_EVENT_CLASS(rpcrdma_mr_completion_class,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(wc, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned long, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->status = wc->status;
		if (wc->status)
			__entry->vendor_err = wc->vendor_err;
		else
			__entry->vendor_err = 0;
	),

	TP_printk("cq.id=%u mr.id=%d status=%s (%lu/0x%x)",
		__entry->cq_id, __entry->completion_id,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

#define DEFINE_MR_COMPLETION_EVENT(name)				\
		DEFINE_EVENT(rpcrdma_mr_completion_class, name,		\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(wc, cid))

DECLARE_EVENT_CLASS(rpcrdma_receive_completion_class,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(wc, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, received)
		__field(unsigned long, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->status = wc->status;
		if (wc->status) {
			__entry->received = 0;
			__entry->vendor_err = wc->vendor_err;
		} else {
			__entry->received = wc->byte_len;
			__entry->vendor_err = 0;
		}
	),

	TP_printk("cq.id=%u cid=%d status=%s (%lu/0x%x) received=%u",
		__entry->cq_id, __entry->completion_id,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err,
		__entry->received
	)
);

#define DEFINE_RECEIVE_COMPLETION_EVENT(name)				\
		DEFINE_EVENT(rpcrdma_receive_completion_class, name,	\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(wc, cid))

DECLARE_EVENT_CLASS(rpcrdma_receive_success_class,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(wc, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, received)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->received = wc->byte_len;
	),

	TP_printk("cq.id=%u cid=%d received=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->received
	)
);

#define DEFINE_RECEIVE_SUCCESS_EVENT(name)				\
		DEFINE_EVENT(rpcrdma_receive_success_class, name,	\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(wc, cid))

DECLARE_EVENT_CLASS(rpcrdma_receive_flush_class,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(wc, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned long, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->status = wc->status;
		__entry->vendor_err = wc->vendor_err;
	),

	TP_printk("cq.id=%u cid=%d status=%s (%lu/0x%x)",
		__entry->cq_id, __entry->completion_id,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

#define DEFINE_RECEIVE_FLUSH_EVENT(name)				\
		DEFINE_EVENT(rpcrdma_receive_flush_class, name,		\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpc_rdma_cid *cid	\
				),					\
				TP_ARGS(wc, cid))

DECLARE_EVENT_CLASS(xprtrdma_reply_class,
	TP_PROTO(
		const struct rpcrdma_rep *rep
	),

	TP_ARGS(rep),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, version)
		__field(u32, proc)
		__string(addr, rpcrdma_addrstr(rep->rr_rxprt))
		__string(port, rpcrdma_portstr(rep->rr_rxprt))
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rep->rr_xid);
		__entry->version = be32_to_cpu(rep->rr_vers);
		__entry->proc = be32_to_cpu(rep->rr_proc);
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s xid=0x%08x version=%u proc=%u",
		__get_str(addr), __get_str(port),
		__entry->xid, __entry->version, __entry->proc
	)
);

#define DEFINE_REPLY_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_reply_class,			\
				xprtrdma_reply_##name##_err,		\
				TP_PROTO(				\
					const struct rpcrdma_rep *rep	\
				),					\
				TP_ARGS(rep))

DECLARE_EVENT_CLASS(xprtrdma_rxprt,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt
	),

	TP_ARGS(r_xprt),

	TP_STRUCT__entry(
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s",
		__get_str(addr), __get_str(port)
	)
);

#define DEFINE_RXPRT_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_rxprt, name,			\
				TP_PROTO(				\
					const struct rpcrdma_xprt *r_xprt \
				),					\
				TP_ARGS(r_xprt))

DECLARE_EVENT_CLASS(xprtrdma_connect_class,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		int rc
	),

	TP_ARGS(r_xprt, rc),

	TP_STRUCT__entry(
		__field(int, rc)
		__field(int, connect_status)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->rc = rc;
		__entry->connect_status = r_xprt->rx_ep->re_connect_status;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s rc=%d connection status=%d",
		__get_str(addr), __get_str(port),
		__entry->rc, __entry->connect_status
	)
);

#define DEFINE_CONN_EVENT(name)						\
		DEFINE_EVENT(xprtrdma_connect_class, xprtrdma_##name,	\
				TP_PROTO(				\
					const struct rpcrdma_xprt *r_xprt, \
					int rc				\
				),					\
				TP_ARGS(r_xprt, rc))

DECLARE_EVENT_CLASS(xprtrdma_rdch_event,
	TP_PROTO(
		const struct rpc_task *task,
		unsigned int pos,
		struct rpcrdma_mr *mr,
		int nsegs
	),

	TP_ARGS(task, pos, mr, nsegs),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned int, pos)
		__field(int, nents)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(int, nsegs)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->pos = pos;
		__entry->nents = mr->mr_nents;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->nsegs = nsegs;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " pos=%u %u@0x%016llx:0x%08x (%s)",
		__entry->task_id, __entry->client_id,
		__entry->pos, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle,
		__entry->nents < __entry->nsegs ? "more" : "last"
	)
);

#define DEFINE_RDCH_EVENT(name)						\
		DEFINE_EVENT(xprtrdma_rdch_event, xprtrdma_chunk_##name,\
				TP_PROTO(				\
					const struct rpc_task *task,	\
					unsigned int pos,		\
					struct rpcrdma_mr *mr,		\
					int nsegs			\
				),					\
				TP_ARGS(task, pos, mr, nsegs))

DECLARE_EVENT_CLASS(xprtrdma_wrch_event,
	TP_PROTO(
		const struct rpc_task *task,
		struct rpcrdma_mr *mr,
		int nsegs
	),

	TP_ARGS(task, mr, nsegs),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, nents)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(int, nsegs)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->nents = mr->mr_nents;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->nsegs = nsegs;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " %u@0x%016llx:0x%08x (%s)",
		__entry->task_id, __entry->client_id,
		__entry->length, (unsigned long long)__entry->offset,
		__entry->handle,
		__entry->nents < __entry->nsegs ? "more" : "last"
	)
);

#define DEFINE_WRCH_EVENT(name)						\
		DEFINE_EVENT(xprtrdma_wrch_event, xprtrdma_chunk_##name,\
				TP_PROTO(				\
					const struct rpc_task *task,	\
					struct rpcrdma_mr *mr,		\
					int nsegs			\
				),					\
				TP_ARGS(task, mr, nsegs))

TRACE_DEFINE_ENUM(DMA_BIDIRECTIONAL);
TRACE_DEFINE_ENUM(DMA_TO_DEVICE);
TRACE_DEFINE_ENUM(DMA_FROM_DEVICE);
TRACE_DEFINE_ENUM(DMA_NONE);

#define xprtrdma_show_direction(x)					\
		__print_symbolic(x,					\
				{ DMA_BIDIRECTIONAL, "BIDIR" },		\
				{ DMA_TO_DEVICE, "TO_DEVICE" },		\
				{ DMA_FROM_DEVICE, "FROM_DEVICE" },	\
				{ DMA_NONE, "NONE" })

DECLARE_EVENT_CLASS(xprtrdma_mr_class,
	TP_PROTO(
		const struct rpcrdma_mr *mr
	),

	TP_ARGS(mr),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, mr_id)
		__field(int, nents)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(u32, dir)
	),

	TP_fast_assign(
		const struct rpcrdma_req *req = mr->mr_req;

		if (req) {
			const struct rpc_task *task = req->rl_slot.rq_task;

			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client->cl_clid;
		} else {
			__entry->task_id = 0;
			__entry->client_id = -1;
		}
		__entry->mr_id  = mr->mr_ibmr->res.id;
		__entry->nents  = mr->mr_nents;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->dir    = mr->mr_dir;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " mr.id=%u nents=%d %u@0x%016llx:0x%08x (%s)",
		__entry->task_id, __entry->client_id,
		__entry->mr_id, __entry->nents, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle,
		xprtrdma_show_direction(__entry->dir)
	)
);

#define DEFINE_MR_EVENT(name)						\
		DEFINE_EVENT(xprtrdma_mr_class,				\
				xprtrdma_mr_##name,			\
				TP_PROTO(				\
					const struct rpcrdma_mr *mr	\
				),					\
				TP_ARGS(mr))

DECLARE_EVENT_CLASS(xprtrdma_anonymous_mr_class,
	TP_PROTO(
		const struct rpcrdma_mr *mr
	),

	TP_ARGS(mr),

	TP_STRUCT__entry(
		__field(u32, mr_id)
		__field(int, nents)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(u32, dir)
	),

	TP_fast_assign(
		__entry->mr_id  = mr->mr_ibmr->res.id;
		__entry->nents  = mr->mr_nents;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->dir    = mr->mr_dir;
	),

	TP_printk("mr.id=%u nents=%d %u@0x%016llx:0x%08x (%s)",
		__entry->mr_id, __entry->nents, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle,
		xprtrdma_show_direction(__entry->dir)
	)
);

#define DEFINE_ANON_MR_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_anonymous_mr_class,		\
				xprtrdma_mr_##name,			\
				TP_PROTO(				\
					const struct rpcrdma_mr *mr	\
				),					\
				TP_ARGS(mr))

DECLARE_EVENT_CLASS(xprtrdma_callback_class,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		const struct rpc_rqst *rqst
	),

	TP_ARGS(r_xprt, rqst),

	TP_STRUCT__entry(
		__field(u32, xid)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s xid=0x%08x",
		__get_str(addr), __get_str(port), __entry->xid
	)
);

#define DEFINE_CALLBACK_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_callback_class,			\
				xprtrdma_cb_##name,			\
				TP_PROTO(				\
					const struct rpcrdma_xprt *r_xprt, \
					const struct rpc_rqst *rqst	\
				),					\
				TP_ARGS(r_xprt, rqst))

/**
 ** Connection events
 **/

TRACE_EVENT(xprtrdma_inline_thresh,
	TP_PROTO(
		const struct rpcrdma_ep *ep
	),

	TP_ARGS(ep),

	TP_STRUCT__entry(
		__field(unsigned int, inline_send)
		__field(unsigned int, inline_recv)
		__field(unsigned int, max_send)
		__field(unsigned int, max_recv)
		__array(unsigned char, srcaddr, sizeof(struct sockaddr_in6))
		__array(unsigned char, dstaddr, sizeof(struct sockaddr_in6))
	),

	TP_fast_assign(
		const struct rdma_cm_id *id = ep->re_id;

		__entry->inline_send = ep->re_inline_send;
		__entry->inline_recv = ep->re_inline_recv;
		__entry->max_send = ep->re_max_inline_send;
		__entry->max_recv = ep->re_max_inline_recv;
		memcpy(__entry->srcaddr, &id->route.addr.src_addr,
		       sizeof(struct sockaddr_in6));
		memcpy(__entry->dstaddr, &id->route.addr.dst_addr,
		       sizeof(struct sockaddr_in6));
	),

	TP_printk("%pISpc -> %pISpc neg send/recv=%u/%u, calc send/recv=%u/%u",
		__entry->srcaddr, __entry->dstaddr,
		__entry->inline_send, __entry->inline_recv,
		__entry->max_send, __entry->max_recv
	)
);

DEFINE_CONN_EVENT(connect);
DEFINE_CONN_EVENT(disconnect);

DEFINE_RXPRT_EVENT(xprtrdma_op_inject_dsc);

TRACE_EVENT(xprtrdma_op_connect,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		unsigned long delay
	),

	TP_ARGS(r_xprt, delay),

	TP_STRUCT__entry(
		__field(unsigned long, delay)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->delay = delay;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s delay=%lu",
		__get_str(addr), __get_str(port), __entry->delay
	)
);


TRACE_EVENT(xprtrdma_op_set_cto,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		unsigned long connect,
		unsigned long reconnect
	),

	TP_ARGS(r_xprt, connect, reconnect),

	TP_STRUCT__entry(
		__field(unsigned long, connect)
		__field(unsigned long, reconnect)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->connect = connect;
		__entry->reconnect = reconnect;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s connect=%lu reconnect=%lu",
		__get_str(addr), __get_str(port),
		__entry->connect / HZ, __entry->reconnect / HZ
	)
);

/**
 ** Call events
 **/

TRACE_EVENT(xprtrdma_createmrs,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		unsigned int count
	),

	TP_ARGS(r_xprt, count),

	TP_STRUCT__entry(
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
		__field(unsigned int, count)
	),

	TP_fast_assign(
		__entry->count = count;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s created %u MRs",
		__get_str(addr), __get_str(port), __entry->count
	)
);

TRACE_EVENT(xprtrdma_nomrs_err,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		const struct rpcrdma_req *req
	),

	TP_ARGS(r_xprt, req),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		const struct rpc_rqst *rqst = &req->rl_slot;

		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " peer=[%s]:%s",
		__entry->task_id, __entry->client_id,
		__get_str(addr), __get_str(port)
	)
);

DEFINE_RDCH_EVENT(read);
DEFINE_WRCH_EVENT(write);
DEFINE_WRCH_EVENT(reply);
DEFINE_WRCH_EVENT(wp);

TRACE_DEFINE_ENUM(rpcrdma_noch);
TRACE_DEFINE_ENUM(rpcrdma_noch_pullup);
TRACE_DEFINE_ENUM(rpcrdma_noch_mapped);
TRACE_DEFINE_ENUM(rpcrdma_readch);
TRACE_DEFINE_ENUM(rpcrdma_areadch);
TRACE_DEFINE_ENUM(rpcrdma_writech);
TRACE_DEFINE_ENUM(rpcrdma_replych);

#define xprtrdma_show_chunktype(x)					\
		__print_symbolic(x,					\
				{ rpcrdma_noch, "inline" },		\
				{ rpcrdma_noch_pullup, "pullup" },	\
				{ rpcrdma_noch_mapped, "mapped" },	\
				{ rpcrdma_readch, "read list" },	\
				{ rpcrdma_areadch, "*read list" },	\
				{ rpcrdma_writech, "write list" },	\
				{ rpcrdma_replych, "reply chunk" })

TRACE_EVENT(xprtrdma_marshal,
	TP_PROTO(
		const struct rpcrdma_req *req,
		unsigned int rtype,
		unsigned int wtype
	),

	TP_ARGS(req, rtype, wtype),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(unsigned int, hdrlen)
		__field(unsigned int, headlen)
		__field(unsigned int, pagelen)
		__field(unsigned int, taillen)
		__field(unsigned int, rtype)
		__field(unsigned int, wtype)
	),

	TP_fast_assign(
		const struct rpc_rqst *rqst = &req->rl_slot;

		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->hdrlen = req->rl_hdrbuf.len;
		__entry->headlen = rqst->rq_snd_buf.head[0].iov_len;
		__entry->pagelen = rqst->rq_snd_buf.page_len;
		__entry->taillen = rqst->rq_snd_buf.tail[0].iov_len;
		__entry->rtype = rtype;
		__entry->wtype = wtype;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " xid=0x%08x hdr=%u xdr=%u/%u/%u %s/%s",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->hdrlen,
		__entry->headlen, __entry->pagelen, __entry->taillen,
		xprtrdma_show_chunktype(__entry->rtype),
		xprtrdma_show_chunktype(__entry->wtype)
	)
);

TRACE_EVENT(xprtrdma_marshal_failed,
	TP_PROTO(const struct rpc_rqst *rqst,
		 int ret
	),

	TP_ARGS(rqst, ret),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->ret = ret;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x ret=%d",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->ret
	)
);

TRACE_EVENT(xprtrdma_prepsend_failed,
	TP_PROTO(const struct rpc_rqst *rqst,
		 int ret
	),

	TP_ARGS(rqst, ret),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->ret = ret;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x ret=%d",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->ret
	)
);

TRACE_EVENT(xprtrdma_post_send,
	TP_PROTO(
		const struct rpcrdma_req *req
	),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, num_sge)
		__field(int, signaled)
	),

	TP_fast_assign(
		const struct rpc_rqst *rqst = &req->rl_slot;
		const struct rpcrdma_sendctx *sc = req->rl_sendctx;

		__entry->cq_id = sc->sc_cid.ci_queue_id;
		__entry->completion_id = sc->sc_cid.ci_completion_id;
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client ?
				     rqst->rq_task->tk_client->cl_clid : -1;
		__entry->num_sge = req->rl_wr.num_sge;
		__entry->signaled = req->rl_wr.send_flags & IB_SEND_SIGNALED;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " cq.id=%u cid=%d (%d SGE%s) %s",
		__entry->task_id, __entry->client_id,
		__entry->cq_id, __entry->completion_id,
		__entry->num_sge, (__entry->num_sge == 1 ? "" : "s"),
		(__entry->signaled ? "signaled" : "")
	)
);

TRACE_EVENT(xprtrdma_post_send_err,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		const struct rpcrdma_req *req,
		int rc
	),

	TP_ARGS(r_xprt, req, rc),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, rc)
	),

	TP_fast_assign(
		const struct rpc_rqst *rqst = &req->rl_slot;
		const struct rpcrdma_ep *ep = r_xprt->rx_ep;

		__entry->cq_id = ep ? ep->re_attr.recv_cq->res.id : 0;
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client ?
				     rqst->rq_task->tk_client->cl_clid : -1;
		__entry->rc = rc;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " cq.id=%u rc=%d",
		__entry->task_id, __entry->client_id,
		__entry->cq_id, __entry->rc
	)
);

DEFINE_SIMPLE_CID_EVENT(xprtrdma_post_recv);

TRACE_EVENT(xprtrdma_post_recvs,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		unsigned int count
	),

	TP_ARGS(r_xprt, count),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(unsigned int, count)
		__field(int, posted)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		const struct rpcrdma_ep *ep = r_xprt->rx_ep;

		__entry->cq_id = ep->re_attr.recv_cq->res.id;
		__entry->count = count;
		__entry->posted = ep->re_receive_count;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s cq.id=%d %u new recvs, %d active",
		__get_str(addr), __get_str(port), __entry->cq_id,
		__entry->count, __entry->posted
	)
);

TRACE_EVENT(xprtrdma_post_recvs_err,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		int status
	),

	TP_ARGS(r_xprt, status),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, status)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		const struct rpcrdma_ep *ep = r_xprt->rx_ep;

		__entry->cq_id = ep->re_attr.recv_cq->res.id;
		__entry->status = status;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s cq.id=%d rc=%d",
		__get_str(addr), __get_str(port), __entry->cq_id,
		__entry->status
	)
);

TRACE_EVENT(xprtrdma_post_linv_err,
	TP_PROTO(
		const struct rpcrdma_req *req,
		int status
	),

	TP_ARGS(req, status),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, status)
	),

	TP_fast_assign(
		const struct rpc_task *task = req->rl_slot.rq_task;

		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->status = status;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " status=%d",
		__entry->task_id, __entry->client_id, __entry->status
	)
);

/**
 ** Completion events
 **/

DEFINE_RECEIVE_COMPLETION_EVENT(xprtrdma_wc_receive);

DEFINE_COMPLETION_EVENT(xprtrdma_wc_send);
DEFINE_MR_COMPLETION_EVENT(xprtrdma_wc_fastreg);
DEFINE_MR_COMPLETION_EVENT(xprtrdma_wc_li);
DEFINE_MR_COMPLETION_EVENT(xprtrdma_wc_li_wake);
DEFINE_MR_COMPLETION_EVENT(xprtrdma_wc_li_done);

TRACE_EVENT(xprtrdma_frwr_alloc,
	TP_PROTO(
		const struct rpcrdma_mr *mr,
		int rc
	),

	TP_ARGS(mr, rc),

	TP_STRUCT__entry(
		__field(u32, mr_id)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->mr_id = mr->mr_ibmr->res.id;
		__entry->rc = rc;
	),

	TP_printk("mr.id=%u: rc=%d",
		__entry->mr_id, __entry->rc
	)
);

TRACE_EVENT(xprtrdma_frwr_dereg,
	TP_PROTO(
		const struct rpcrdma_mr *mr,
		int rc
	),

	TP_ARGS(mr, rc),

	TP_STRUCT__entry(
		__field(u32, mr_id)
		__field(int, nents)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(u32, dir)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->mr_id  = mr->mr_ibmr->res.id;
		__entry->nents  = mr->mr_nents;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->dir    = mr->mr_dir;
		__entry->rc	= rc;
	),

	TP_printk("mr.id=%u nents=%d %u@0x%016llx:0x%08x (%s): rc=%d",
		__entry->mr_id, __entry->nents, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle,
		xprtrdma_show_direction(__entry->dir),
		__entry->rc
	)
);

TRACE_EVENT(xprtrdma_frwr_sgerr,
	TP_PROTO(
		const struct rpcrdma_mr *mr,
		int sg_nents
	),

	TP_ARGS(mr, sg_nents),

	TP_STRUCT__entry(
		__field(u32, mr_id)
		__field(u64, addr)
		__field(u32, dir)
		__field(int, nents)
	),

	TP_fast_assign(
		__entry->mr_id = mr->mr_ibmr->res.id;
		__entry->addr = mr->mr_sg->dma_address;
		__entry->dir = mr->mr_dir;
		__entry->nents = sg_nents;
	),

	TP_printk("mr.id=%u DMA addr=0x%llx (%s) sg_nents=%d",
		__entry->mr_id, __entry->addr,
		xprtrdma_show_direction(__entry->dir),
		__entry->nents
	)
);

TRACE_EVENT(xprtrdma_frwr_maperr,
	TP_PROTO(
		const struct rpcrdma_mr *mr,
		int num_mapped
	),

	TP_ARGS(mr, num_mapped),

	TP_STRUCT__entry(
		__field(u32, mr_id)
		__field(u64, addr)
		__field(u32, dir)
		__field(int, num_mapped)
		__field(int, nents)
	),

	TP_fast_assign(
		__entry->mr_id = mr->mr_ibmr->res.id;
		__entry->addr = mr->mr_sg->dma_address;
		__entry->dir = mr->mr_dir;
		__entry->num_mapped = num_mapped;
		__entry->nents = mr->mr_nents;
	),

	TP_printk("mr.id=%u DMA addr=0x%llx (%s) nents=%d of %d",
		__entry->mr_id, __entry->addr,
		xprtrdma_show_direction(__entry->dir),
		__entry->num_mapped, __entry->nents
	)
);

DEFINE_MR_EVENT(fastreg);
DEFINE_MR_EVENT(localinv);
DEFINE_MR_EVENT(reminv);
DEFINE_MR_EVENT(map);

DEFINE_ANON_MR_EVENT(unmap);

TRACE_EVENT(xprtrdma_dma_maperr,
	TP_PROTO(
		u64 addr
	),

	TP_ARGS(addr),

	TP_STRUCT__entry(
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->addr = addr;
	),

	TP_printk("dma addr=0x%llx\n", __entry->addr)
);

/**
 ** Reply events
 **/

TRACE_EVENT(xprtrdma_reply,
	TP_PROTO(
		const struct rpc_task *task,
		const struct rpcrdma_rep *rep,
		unsigned int credits
	),

	TP_ARGS(task, rep, credits),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(unsigned int, credits)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rep->rr_xid);
		__entry->credits = credits;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x credits=%u",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->credits
	)
);

DEFINE_REPLY_EVENT(vers);
DEFINE_REPLY_EVENT(rqst);
DEFINE_REPLY_EVENT(short);
DEFINE_REPLY_EVENT(hdr);

TRACE_EVENT(xprtrdma_err_vers,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		__be32 *min,
		__be32 *max
	),

	TP_ARGS(rqst, min, max),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(u32, min)
		__field(u32, max)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->min = be32_to_cpup(min);
		__entry->max = be32_to_cpup(max);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x versions=[%u, %u]",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->min, __entry->max
	)
);

TRACE_EVENT(xprtrdma_err_chunk,
	TP_PROTO(
		const struct rpc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x",
		__entry->task_id, __entry->client_id, __entry->xid
	)
);

TRACE_EVENT(xprtrdma_err_unrecognized,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		__be32 *procedure
	),

	TP_ARGS(rqst, procedure),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(u32, procedure)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->procedure = be32_to_cpup(procedure);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x procedure=%u",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->procedure
	)
);

TRACE_EVENT(xprtrdma_fixup,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		unsigned long fixup
	),

	TP_ARGS(rqst, fixup),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned long, fixup)
		__field(size_t, headlen)
		__field(unsigned int, pagelen)
		__field(size_t, taillen)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->fixup = fixup;
		__entry->headlen = rqst->rq_rcv_buf.head[0].iov_len;
		__entry->pagelen = rqst->rq_rcv_buf.page_len;
		__entry->taillen = rqst->rq_rcv_buf.tail[0].iov_len;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " fixup=%lu xdr=%zu/%u/%zu",
		__entry->task_id, __entry->client_id, __entry->fixup,
		__entry->headlen, __entry->pagelen, __entry->taillen
	)
);

TRACE_EVENT(xprtrdma_decode_seg,
	TP_PROTO(
		u32 handle,
		u32 length,
		u64 offset
	),

	TP_ARGS(handle, length, offset),

	TP_STRUCT__entry(
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
	),

	TP_fast_assign(
		__entry->handle = handle;
		__entry->length = length;
		__entry->offset = offset;
	),

	TP_printk("%u@0x%016llx:0x%08x",
		__entry->length, (unsigned long long)__entry->offset,
		__entry->handle
	)
);

TRACE_EVENT(xprtrdma_mrs_zap,
	TP_PROTO(
		const struct rpc_task *task
	),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER,
		__entry->task_id, __entry->client_id
	)
);

/**
 ** Callback events
 **/

TRACE_EVENT(xprtrdma_cb_setup,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		unsigned int reqs
	),

	TP_ARGS(r_xprt, reqs),

	TP_STRUCT__entry(
		__field(unsigned int, reqs)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->reqs = reqs;
		__assign_str(addr);
		__assign_str(port);
	),

	TP_printk("peer=[%s]:%s %u reqs",
		__get_str(addr), __get_str(port), __entry->reqs
	)
);

DEFINE_CALLBACK_EVENT(call);
DEFINE_CALLBACK_EVENT(reply);

/**
 ** Server-side RPC/RDMA events
 **/

DECLARE_EVENT_CLASS(svcrdma_accept_class,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		long status
	),

	TP_ARGS(rdma, status),

	TP_STRUCT__entry(
		__field(long, status)
		__string(addr, rdma->sc_xprt.xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->status = status;
		__assign_str(addr);
	),

	TP_printk("addr=%s status=%ld",
		__get_str(addr), __entry->status
	)
);

#define DEFINE_ACCEPT_EVENT(name) \
		DEFINE_EVENT(svcrdma_accept_class, svcrdma_##name##_err, \
				TP_PROTO( \
					const struct svcxprt_rdma *rdma, \
					long status \
				), \
				TP_ARGS(rdma, status))

DEFINE_ACCEPT_EVENT(pd);
DEFINE_ACCEPT_EVENT(qp);
DEFINE_ACCEPT_EVENT(fabric);
DEFINE_ACCEPT_EVENT(initdepth);
DEFINE_ACCEPT_EVENT(accept);

TRACE_DEFINE_ENUM(RDMA_MSG);
TRACE_DEFINE_ENUM(RDMA_NOMSG);
TRACE_DEFINE_ENUM(RDMA_MSGP);
TRACE_DEFINE_ENUM(RDMA_DONE);
TRACE_DEFINE_ENUM(RDMA_ERROR);

#define show_rpcrdma_proc(x)						\
		__print_symbolic(x,					\
				{ RDMA_MSG, "RDMA_MSG" },		\
				{ RDMA_NOMSG, "RDMA_NOMSG" },		\
				{ RDMA_MSGP, "RDMA_MSGP" },		\
				{ RDMA_DONE, "RDMA_DONE" },		\
				{ RDMA_ERROR, "RDMA_ERROR" })

TRACE_EVENT(svcrdma_decode_rqst,
	TP_PROTO(
		const struct svc_rdma_recv_ctxt *ctxt,
		__be32 *p,
		unsigned int hdrlen
	),

	TP_ARGS(ctxt, p, hdrlen),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__field(u32, credits)
		__field(unsigned int, hdrlen)
	),

	TP_fast_assign(
		__entry->cq_id = ctxt->rc_cid.ci_queue_id;
		__entry->completion_id = ctxt->rc_cid.ci_completion_id;
		__entry->xid = be32_to_cpup(p++);
		__entry->vers = be32_to_cpup(p++);
		__entry->credits = be32_to_cpup(p++);
		__entry->proc = be32_to_cpup(p);
		__entry->hdrlen = hdrlen;
	),

	TP_printk("cq.id=%u cid=%d xid=0x%08x vers=%u credits=%u proc=%s hdrlen=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->xid, __entry->vers, __entry->credits,
		show_rpcrdma_proc(__entry->proc), __entry->hdrlen)
);

TRACE_EVENT(svcrdma_decode_short_err,
	TP_PROTO(
		const struct svc_rdma_recv_ctxt *ctxt,
		unsigned int hdrlen
	),

	TP_ARGS(ctxt, hdrlen),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned int, hdrlen)
	),

	TP_fast_assign(
		__entry->cq_id = ctxt->rc_cid.ci_queue_id;
		__entry->completion_id = ctxt->rc_cid.ci_completion_id;
		__entry->hdrlen = hdrlen;
	),

	TP_printk("cq.id=%u cid=%d hdrlen=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->hdrlen)
);

DECLARE_EVENT_CLASS(svcrdma_badreq_event,
	TP_PROTO(
		const struct svc_rdma_recv_ctxt *ctxt,
		__be32 *p
	),

	TP_ARGS(ctxt, p),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__field(u32, credits)
	),

	TP_fast_assign(
		__entry->cq_id = ctxt->rc_cid.ci_queue_id;
		__entry->completion_id = ctxt->rc_cid.ci_completion_id;
		__entry->xid = be32_to_cpup(p++);
		__entry->vers = be32_to_cpup(p++);
		__entry->credits = be32_to_cpup(p++);
		__entry->proc = be32_to_cpup(p);
	),

	TP_printk("cq.id=%u cid=%d xid=0x%08x vers=%u credits=%u proc=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->xid, __entry->vers, __entry->credits, __entry->proc)
);

#define DEFINE_BADREQ_EVENT(name)					\
		DEFINE_EVENT(svcrdma_badreq_event,			\
			     svcrdma_decode_##name##_err,		\
				TP_PROTO(				\
					const struct svc_rdma_recv_ctxt *ctxt,	\
					__be32 *p			\
				),					\
				TP_ARGS(ctxt, p))

DEFINE_BADREQ_EVENT(badvers);
DEFINE_BADREQ_EVENT(drop);
DEFINE_BADREQ_EVENT(badproc);
DEFINE_BADREQ_EVENT(parse);

TRACE_EVENT(svcrdma_encode_wseg,
	TP_PROTO(
		const struct svc_rdma_send_ctxt *ctxt,
		u32 segno,
		u32 handle,
		u32 length,
		u64 offset
	),

	TP_ARGS(ctxt, segno, handle, length, offset),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, segno)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
	),

	TP_fast_assign(
		__entry->cq_id = ctxt->sc_cid.ci_queue_id;
		__entry->completion_id = ctxt->sc_cid.ci_completion_id;
		__entry->segno = segno;
		__entry->handle = handle;
		__entry->length = length;
		__entry->offset = offset;
	),

	TP_printk("cq.id=%u cid=%d segno=%u %u@0x%016llx:0x%08x",
		__entry->cq_id, __entry->completion_id,
		__entry->segno, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle
	)
);

TRACE_EVENT(svcrdma_decode_rseg,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		const struct svc_rdma_chunk *chunk,
		const struct svc_rdma_segment *segment
	),

	TP_ARGS(cid, chunk, segment),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, segno)
		__field(u32, position)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->segno = chunk->ch_segcount;
		__entry->position = chunk->ch_position;
		__entry->handle = segment->rs_handle;
		__entry->length = segment->rs_length;
		__entry->offset = segment->rs_offset;
	),

	TP_printk("cq.id=%u cid=%d segno=%u position=%u %u@0x%016llx:0x%08x",
		__entry->cq_id, __entry->completion_id,
		__entry->segno, __entry->position, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle
	)
);

TRACE_EVENT(svcrdma_decode_wseg,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		const struct svc_rdma_chunk *chunk,
		u32 segno
	),

	TP_ARGS(cid, chunk, segno),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u32, segno)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
	),

	TP_fast_assign(
		const struct svc_rdma_segment *segment =
			&chunk->ch_segments[segno];

		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->segno = segno;
		__entry->handle = segment->rs_handle;
		__entry->length = segment->rs_length;
		__entry->offset = segment->rs_offset;
	),

	TP_printk("cq.id=%u cid=%d segno=%u %u@0x%016llx:0x%08x",
		__entry->cq_id, __entry->completion_id,
		__entry->segno, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle
	)
);

DECLARE_EVENT_CLASS(svcrdma_error_event,
	TP_PROTO(
		__be32 xid
	),

	TP_ARGS(xid),

	TP_STRUCT__entry(
		__field(u32, xid)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(xid);
	),

	TP_printk("xid=0x%08x",
		__entry->xid
	)
);

#define DEFINE_ERROR_EVENT(name)					\
		DEFINE_EVENT(svcrdma_error_event, svcrdma_err_##name,	\
				TP_PROTO(				\
					__be32 xid			\
				),					\
				TP_ARGS(xid))

DEFINE_ERROR_EVENT(vers);
DEFINE_ERROR_EVENT(chunk);

/**
 ** Server-side RDMA API events
 **/

DECLARE_EVENT_CLASS(svcrdma_dma_map_class,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		u64 dma_addr,
		u32 length
	),

	TP_ARGS(cid, dma_addr, length),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(u64, dma_addr)
		__field(u32, length)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->dma_addr = dma_addr;
		__entry->length = length;
	),

	TP_printk("cq.id=%u cid=%d dma_addr=%llu length=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->dma_addr, __entry->length
	)
);

#define DEFINE_SVC_DMA_EVENT(name)					\
		DEFINE_EVENT(svcrdma_dma_map_class, svcrdma_##name,	\
				TP_PROTO(				\
					const struct rpc_rdma_cid *cid, \
					u64 dma_addr,			\
					u32 length			\
				),					\
				TP_ARGS(cid, dma_addr, length)		\
		)

DEFINE_SVC_DMA_EVENT(dma_map_page);
DEFINE_SVC_DMA_EVENT(dma_map_err);
DEFINE_SVC_DMA_EVENT(dma_unmap_page);

TRACE_EVENT(svcrdma_dma_map_rw_err,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		u64 offset,
		u32 handle,
		unsigned int nents,
		int status
	),

	TP_ARGS(rdma, offset, handle, nents, status),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(u32, handle)
		__field(u64, offset)
		__field(unsigned int, nents)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->cq_id = rdma->sc_sq_cq->res.id;
		__entry->handle = handle;
		__entry->offset = offset;
		__entry->nents = nents;
		__entry->status = status;
	),

	TP_printk("cq.id=%u 0x%016llx:0x%08x nents=%u status=%d",
		__entry->cq_id, (unsigned long long)__entry->offset,
		__entry->handle, __entry->nents, __entry->status
	)
);

TRACE_EVENT(svcrdma_rwctx_empty,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		unsigned int num_sges
	),

	TP_ARGS(rdma, num_sges),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(unsigned int, num_sges)
	),

	TP_fast_assign(
		__entry->cq_id = rdma->sc_sq_cq->res.id;
		__entry->num_sges = num_sges;
	),

	TP_printk("cq.id=%u num_sges=%d",
		__entry->cq_id, __entry->num_sges
	)
);

TRACE_EVENT(svcrdma_page_overrun_err,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		unsigned int pageno
	),

	TP_ARGS(cid, pageno),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned int, pageno)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->pageno = pageno;
	),

	TP_printk("cq.id=%u cid=%d pageno=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->pageno
	)
);

TRACE_EVENT(svcrdma_small_wrch_err,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		unsigned int remaining,
		unsigned int seg_no,
		unsigned int num_segs
	),

	TP_ARGS(cid, remaining, seg_no, num_segs),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned int, remaining)
		__field(unsigned int, seg_no)
		__field(unsigned int, num_segs)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->remaining = remaining;
		__entry->seg_no = seg_no;
		__entry->num_segs = num_segs;
	),

	TP_printk("cq.id=%u cid=%d remaining=%u seg_no=%u num_segs=%u",
		__entry->cq_id, __entry->completion_id,
		__entry->remaining, __entry->seg_no, __entry->num_segs
	)
);

TRACE_EVENT(svcrdma_send_pullup,
	TP_PROTO(
		const struct svc_rdma_send_ctxt *ctxt,
		unsigned int msglen
	),

	TP_ARGS(ctxt, msglen),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned int, hdrlen)
		__field(unsigned int, msglen)
	),

	TP_fast_assign(
		__entry->cq_id = ctxt->sc_cid.ci_queue_id;
		__entry->completion_id = ctxt->sc_cid.ci_completion_id;
		__entry->hdrlen = ctxt->sc_hdrbuf.len,
		__entry->msglen = msglen;
	),

	TP_printk("cq.id=%u cid=%d hdr=%u msg=%u (total %u)",
		__entry->cq_id, __entry->completion_id,
		__entry->hdrlen, __entry->msglen,
		__entry->hdrlen + __entry->msglen)
);

TRACE_EVENT(svcrdma_send_err,
	TP_PROTO(
		const struct svc_rqst *rqst,
		int status
	),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(int, status)
		__field(u32, xid)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->xid = __be32_to_cpu(rqst->rq_xid);
		__assign_str(addr);
	),

	TP_printk("addr=%s xid=0x%08x status=%d", __get_str(addr),
		__entry->xid, __entry->status
	)
);

TRACE_EVENT(svcrdma_post_send,
	TP_PROTO(
		const struct svc_rdma_send_ctxt *ctxt
	),

	TP_ARGS(ctxt),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(unsigned int, num_sge)
		__field(u32, inv_rkey)
	),

	TP_fast_assign(
		const struct ib_send_wr *wr = &ctxt->sc_send_wr;

		__entry->cq_id = ctxt->sc_cid.ci_queue_id;
		__entry->completion_id = ctxt->sc_cid.ci_completion_id;
		__entry->num_sge = wr->num_sge;
		__entry->inv_rkey = (wr->opcode == IB_WR_SEND_WITH_INV) ?
					wr->ex.invalidate_rkey : 0;
	),

	TP_printk("cq.id=%u cid=%d num_sge=%u inv_rkey=0x%08x",
		__entry->cq_id, __entry->completion_id,
		__entry->num_sge, __entry->inv_rkey
	)
);

DEFINE_SIMPLE_CID_EVENT(svcrdma_wc_send);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_send_flush);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_send_err);

DEFINE_SIMPLE_CID_EVENT(svcrdma_post_recv);

DEFINE_RECEIVE_SUCCESS_EVENT(svcrdma_wc_recv);
DEFINE_RECEIVE_FLUSH_EVENT(svcrdma_wc_recv_flush);
DEFINE_RECEIVE_FLUSH_EVENT(svcrdma_wc_recv_err);

TRACE_EVENT(svcrdma_rq_post_err,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		int status
	),

	TP_ARGS(rdma, status),

	TP_STRUCT__entry(
		__field(int, status)
		__string(addr, rdma->sc_xprt.xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->status = status;
		__assign_str(addr);
	),

	TP_printk("addr=%s status=%d",
		__get_str(addr), __entry->status
	)
);

DECLARE_EVENT_CLASS(svcrdma_post_chunk_class,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		int sqecount
	),

	TP_ARGS(cid, sqecount),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(int, sqecount)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->sqecount = sqecount;
	),

	TP_printk("cq.id=%u cid=%d sqecount=%d",
		__entry->cq_id, __entry->completion_id,
		__entry->sqecount
	)
);

#define DEFINE_POST_CHUNK_EVENT(name)					\
		DEFINE_EVENT(svcrdma_post_chunk_class,			\
				svcrdma_post_##name##_chunk,		\
				TP_PROTO(				\
					const struct rpc_rdma_cid *cid,	\
					int sqecount			\
				),					\
				TP_ARGS(cid, sqecount))

DEFINE_POST_CHUNK_EVENT(read);
DEFINE_POST_CHUNK_EVENT(write);
DEFINE_POST_CHUNK_EVENT(reply);

DEFINE_EVENT(svcrdma_post_chunk_class, svcrdma_cc_release,
	TP_PROTO(
		const struct rpc_rdma_cid *cid,
		int sqecount
	),
	TP_ARGS(cid, sqecount)
);

TRACE_EVENT(svcrdma_wc_read,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpc_rdma_cid *cid,
		unsigned int totalbytes,
		const ktime_t posttime
	),

	TP_ARGS(wc, cid, totalbytes, posttime),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(s64, read_latency)
		__field(unsigned int, totalbytes)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->totalbytes = totalbytes;
		__entry->read_latency = ktime_us_delta(ktime_get(), posttime);
	),

	TP_printk("cq.id=%u cid=%d totalbytes=%u latency-us=%lld",
		__entry->cq_id, __entry->completion_id,
		__entry->totalbytes, __entry->read_latency
	)
);

DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_read_flush);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_read_err);
DEFINE_SIMPLE_CID_EVENT(svcrdma_read_finished);

DEFINE_SIMPLE_CID_EVENT(svcrdma_wc_write);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_write_flush);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_write_err);

DEFINE_SIMPLE_CID_EVENT(svcrdma_wc_reply);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_reply_flush);
DEFINE_SEND_FLUSH_EVENT(svcrdma_wc_reply_err);

TRACE_EVENT(svcrdma_qp_error,
	TP_PROTO(
		const struct ib_event *event,
		const struct sockaddr *sap
	),

	TP_ARGS(event, sap),

	TP_STRUCT__entry(
		__field(unsigned int, event)
		__string(device, event->device->name)
		__array(__u8, addr, INET6_ADDRSTRLEN + 10)
	),

	TP_fast_assign(
		__entry->event = event->event;
		__assign_str(device);
		snprintf(__entry->addr, sizeof(__entry->addr) - 1,
			 "%pISpc", sap);
	),

	TP_printk("addr=%s dev=%s event=%s (%u)",
		__entry->addr, __get_str(device),
		rdma_show_ib_event(__entry->event), __entry->event
	)
);

DECLARE_EVENT_CLASS(svcrdma_sendqueue_class,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		const struct rpc_rdma_cid *cid
	),

	TP_ARGS(rdma, cid),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(int, avail)
		__field(int, depth)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->avail = atomic_read(&rdma->sc_sq_avail);
		__entry->depth = rdma->sc_sq_depth;
	),

	TP_printk("cq.id=%u cid=%d sc_sq_avail=%d/%d",
		__entry->cq_id, __entry->completion_id,
		__entry->avail, __entry->depth
	)
);

#define DEFINE_SQ_EVENT(name)						\
		DEFINE_EVENT(svcrdma_sendqueue_class, name,		\
			TP_PROTO(					\
				const struct svcxprt_rdma *rdma,	\
				const struct rpc_rdma_cid *cid		\
			),						\
			TP_ARGS(rdma, cid)				\
		)

DEFINE_SQ_EVENT(svcrdma_sq_full);
DEFINE_SQ_EVENT(svcrdma_sq_retry);

TRACE_EVENT(svcrdma_sq_post_err,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		const struct rpc_rdma_cid *cid,
		int status
	),

	TP_ARGS(rdma, cid, status),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, completion_id)
		__field(int, avail)
		__field(int, depth)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->cq_id = cid->ci_queue_id;
		__entry->completion_id = cid->ci_completion_id;
		__entry->avail = atomic_read(&rdma->sc_sq_avail);
		__entry->depth = rdma->sc_sq_depth;
		__entry->status = status;
	),

	TP_printk("cq.id=%u cid=%d sc_sq_avail=%d/%d status=%d",
		__entry->cq_id, __entry->completion_id,
		__entry->avail, __entry->depth, __entry->status
	)
);

#endif /* _TRACE_RPCRDMA_H */

#include <trace/define_trace.h>
