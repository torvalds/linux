/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017 Oracle.  All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rpcrdma

#if !defined(_TRACE_RPCRDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RPCRDMA_H

#include <linux/tracepoint.h>
#include <trace/events/rdma.h>

/**
 ** Event classes
 **/

DECLARE_EVENT_CLASS(xprtrdma_reply_event,
	TP_PROTO(
		const struct rpcrdma_rep *rep
	),

	TP_ARGS(rep),

	TP_STRUCT__entry(
		__field(const void *, rep)
		__field(const void *, r_xprt)
		__field(u32, xid)
		__field(u32, version)
		__field(u32, proc)
	),

	TP_fast_assign(
		__entry->rep = rep;
		__entry->r_xprt = rep->rr_rxprt;
		__entry->xid = be32_to_cpu(rep->rr_xid);
		__entry->version = be32_to_cpu(rep->rr_vers);
		__entry->proc = be32_to_cpu(rep->rr_proc);
	),

	TP_printk("rxprt %p xid=0x%08x rep=%p: version %u proc %u",
		__entry->r_xprt, __entry->xid, __entry->rep,
		__entry->version, __entry->proc
	)
);

#define DEFINE_REPLY_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_reply_event, name,		\
				TP_PROTO(				\
					const struct rpcrdma_rep *rep	\
				),					\
				TP_ARGS(rep))

/**
 ** Call events
 **/

TRACE_DEFINE_ENUM(rpcrdma_noch);
TRACE_DEFINE_ENUM(rpcrdma_readch);
TRACE_DEFINE_ENUM(rpcrdma_areadch);
TRACE_DEFINE_ENUM(rpcrdma_writech);
TRACE_DEFINE_ENUM(rpcrdma_replych);

#define xprtrdma_show_chunktype(x)					\
		__print_symbolic(x,					\
				{ rpcrdma_noch, "inline" },		\
				{ rpcrdma_readch, "read list" },	\
				{ rpcrdma_areadch, "*read list" },	\
				{ rpcrdma_writech, "write list" },	\
				{ rpcrdma_replych, "reply chunk" })

TRACE_EVENT(xprtrdma_marshal,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		unsigned int hdrlen,
		unsigned int rtype,
		unsigned int wtype
	),

	TP_ARGS(rqst, hdrlen, rtype, wtype),

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
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->hdrlen = hdrlen;
		__entry->headlen = rqst->rq_snd_buf.head[0].iov_len;
		__entry->pagelen = rqst->rq_snd_buf.page_len;
		__entry->taillen = rqst->rq_snd_buf.tail[0].iov_len;
		__entry->rtype = rtype;
		__entry->wtype = wtype;
	),

	TP_printk("task:%u@%u xid=0x%08x: hdr=%u xdr=%u/%u/%u %s/%s",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->hdrlen,
		__entry->headlen, __entry->pagelen, __entry->taillen,
		xprtrdma_show_chunktype(__entry->rtype),
		xprtrdma_show_chunktype(__entry->wtype)
	)
);

TRACE_EVENT(xprtrdma_post_send,
	TP_PROTO(
		const struct rpcrdma_req *req,
		int status
	),

	TP_ARGS(req, status),

	TP_STRUCT__entry(
		__field(const void *, req)
		__field(int, num_sge)
		__field(bool, signaled)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->req = req;
		__entry->num_sge = req->rl_sendctx->sc_wr.num_sge;
		__entry->signaled = req->rl_sendctx->sc_wr.send_flags &
				    IB_SEND_SIGNALED;
		__entry->status = status;
	),

	TP_printk("req=%p, %d SGEs%s, status=%d",
		__entry->req, __entry->num_sge,
		(__entry->signaled ? ", signaled" : ""),
		__entry->status
	)
);

TRACE_EVENT(xprtrdma_post_recv,
	TP_PROTO(
		const struct rpcrdma_rep *rep,
		int status
	),

	TP_ARGS(rep, status),

	TP_STRUCT__entry(
		__field(const void *, rep)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->rep = rep;
		__entry->status = status;
	),

	TP_printk("rep=%p status=%d",
		__entry->rep, __entry->status
	)
);

/**
 ** Completion events
 **/

TRACE_EVENT(xprtrdma_wc_send,
	TP_PROTO(
		const struct rpcrdma_sendctx *sc,
		const struct ib_wc *wc
	),

	TP_ARGS(sc, wc),

	TP_STRUCT__entry(
		__field(const void *, req)
		__field(unsigned int, unmap_count)
		__field(unsigned int, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->req = sc->sc_req;
		__entry->unmap_count = sc->sc_unmap_count;
		__entry->status = wc->status;
		__entry->vendor_err = __entry->status ? wc->vendor_err : 0;
	),

	TP_printk("req=%p, unmapped %u pages: %s (%u/0x%x)",
		__entry->req, __entry->unmap_count,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

TRACE_EVENT(xprtrdma_wc_receive,
	TP_PROTO(
		const struct rpcrdma_rep *rep,
		const struct ib_wc *wc
	),

	TP_ARGS(rep, wc),

	TP_STRUCT__entry(
		__field(const void *, rep)
		__field(unsigned int, byte_len)
		__field(unsigned int, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->rep = rep;
		__entry->byte_len = wc->byte_len;
		__entry->status = wc->status;
		__entry->vendor_err = __entry->status ? wc->vendor_err : 0;
	),

	TP_printk("rep=%p, %u bytes: %s (%u/0x%x)",
		__entry->rep, __entry->byte_len,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

/**
 ** Reply events
 **/

TRACE_EVENT(xprtrdma_reply,
	TP_PROTO(
		const struct rpc_task *task,
		const struct rpcrdma_rep *rep,
		const struct rpcrdma_req *req,
		unsigned int credits
	),

	TP_ARGS(task, rep, req, credits),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, rep)
		__field(const void *, req)
		__field(u32, xid)
		__field(unsigned int, credits)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->rep = rep;
		__entry->req = req;
		__entry->xid = be32_to_cpu(rep->rr_xid);
		__entry->credits = credits;
	),

	TP_printk("task:%u@%u xid=0x%08x, %u credits, rep=%p -> req=%p",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->credits, __entry->rep, __entry->req
	)
);

TRACE_EVENT(xprtrdma_defer_cmp,
	TP_PROTO(
		const struct rpcrdma_rep *rep
	),

	TP_ARGS(rep),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, rep)
		__field(u32, xid)
	),

	TP_fast_assign(
		__entry->task_id = rep->rr_rqst->rq_task->tk_pid;
		__entry->client_id = rep->rr_rqst->rq_task->tk_client->cl_clid;
		__entry->rep = rep;
		__entry->xid = be32_to_cpu(rep->rr_xid);
	),

	TP_printk("task:%u@%u xid=0x%08x rep=%p",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->rep
	)
);

DEFINE_REPLY_EVENT(xprtrdma_reply_vers);
DEFINE_REPLY_EVENT(xprtrdma_reply_rqst);
DEFINE_REPLY_EVENT(xprtrdma_reply_short);
DEFINE_REPLY_EVENT(xprtrdma_reply_hdr);

#endif /* _TRACE_RPCRDMA_H */

#include <trace/define_trace.h>
