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

DECLARE_EVENT_CLASS(xprtrdma_rxprt,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt
	),

	TP_ARGS(r_xprt),

	TP_STRUCT__entry(
		__field(const void *, r_xprt)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__assign_str(addr, rpcrdma_addrstr(r_xprt));
		__assign_str(port, rpcrdma_portstr(r_xprt));
	),

	TP_printk("peer=[%s]:%s r_xprt=%p",
		__get_str(addr), __get_str(port), __entry->r_xprt
	)
);

#define DEFINE_RXPRT_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_rxprt, name,			\
				TP_PROTO(				\
					const struct rpcrdma_xprt *r_xprt \
				),					\
				TP_ARGS(r_xprt))

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

	TP_printk("task:%u@%u pos=%u %u@0x%016llx:0x%08x (%s)",
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

	TP_printk("task:%u@%u %u@0x%016llx:0x%08x (%s)",
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

TRACE_DEFINE_ENUM(FRWR_IS_INVALID);
TRACE_DEFINE_ENUM(FRWR_IS_VALID);
TRACE_DEFINE_ENUM(FRWR_FLUSHED_FR);
TRACE_DEFINE_ENUM(FRWR_FLUSHED_LI);

#define xprtrdma_show_frwr_state(x)					\
		__print_symbolic(x,					\
				{ FRWR_IS_INVALID, "INVALID" },		\
				{ FRWR_IS_VALID, "VALID" },		\
				{ FRWR_FLUSHED_FR, "FLUSHED_FR" },	\
				{ FRWR_FLUSHED_LI, "FLUSHED_LI" })

DECLARE_EVENT_CLASS(xprtrdma_frwr_done,
	TP_PROTO(
		const struct ib_wc *wc,
		const struct rpcrdma_frwr *frwr
	),

	TP_ARGS(wc, frwr),

	TP_STRUCT__entry(
		__field(const void *, mr)
		__field(unsigned int, state)
		__field(unsigned int, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->mr = container_of(frwr, struct rpcrdma_mr, frwr);
		__entry->state = frwr->fr_state;
		__entry->status = wc->status;
		__entry->vendor_err = __entry->status ? wc->vendor_err : 0;
	),

	TP_printk(
		"mr=%p state=%s: %s (%u/0x%x)",
		__entry->mr, xprtrdma_show_frwr_state(__entry->state),
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

#define DEFINE_FRWR_DONE_EVENT(name)					\
		DEFINE_EVENT(xprtrdma_frwr_done, name,			\
				TP_PROTO(				\
					const struct ib_wc *wc,		\
					const struct rpcrdma_frwr *frwr	\
				),					\
				TP_ARGS(wc, frwr))

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

DECLARE_EVENT_CLASS(xprtrdma_mr,
	TP_PROTO(
		const struct rpcrdma_mr *mr
	),

	TP_ARGS(mr),

	TP_STRUCT__entry(
		__field(const void *, mr)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(u32, dir)
	),

	TP_fast_assign(
		__entry->mr = mr;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->dir    = mr->mr_dir;
	),

	TP_printk("mr=%p %u@0x%016llx:0x%08x (%s)",
		__entry->mr, __entry->length,
		(unsigned long long)__entry->offset, __entry->handle,
		xprtrdma_show_direction(__entry->dir)
	)
);

#define DEFINE_MR_EVENT(name) \
		DEFINE_EVENT(xprtrdma_mr, xprtrdma_mr_##name, \
				TP_PROTO( \
					const struct rpcrdma_mr *mr \
				), \
				TP_ARGS(mr))

DECLARE_EVENT_CLASS(xprtrdma_cb_event,
	TP_PROTO(
		const struct rpc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(const void *, rqst)
		__field(const void *, rep)
		__field(const void *, req)
		__field(u32, xid)
	),

	TP_fast_assign(
		__entry->rqst = rqst;
		__entry->req = rpcr_to_rdmar(rqst);
		__entry->rep = rpcr_to_rdmar(rqst)->rl_reply;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
	),

	TP_printk("xid=0x%08x, rqst=%p req=%p rep=%p",
		__entry->xid, __entry->rqst, __entry->req, __entry->rep
	)
);

#define DEFINE_CB_EVENT(name)						\
		DEFINE_EVENT(xprtrdma_cb_event, name,			\
				TP_PROTO(				\
					const struct rpc_rqst *rqst	\
				),					\
				TP_ARGS(rqst))

/**
 ** Connection events
 **/

TRACE_EVENT(xprtrdma_cm_event,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		struct rdma_cm_event *event
	),

	TP_ARGS(r_xprt, event),

	TP_STRUCT__entry(
		__field(const void *, r_xprt)
		__field(unsigned int, event)
		__field(int, status)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__entry->event = event->event;
		__entry->status = event->status;
		__assign_str(addr, rpcrdma_addrstr(r_xprt));
		__assign_str(port, rpcrdma_portstr(r_xprt));
	),

	TP_printk("peer=[%s]:%s r_xprt=%p: %s (%u/%d)",
		__get_str(addr), __get_str(port),
		__entry->r_xprt, rdma_show_cm_event(__entry->event),
		__entry->event, __entry->status
	)
);

TRACE_EVENT(xprtrdma_disconnect,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		int status
	),

	TP_ARGS(r_xprt, status),

	TP_STRUCT__entry(
		__field(const void *, r_xprt)
		__field(int, status)
		__field(int, connected)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__entry->status = status;
		__entry->connected = r_xprt->rx_ep.rep_connected;
		__assign_str(addr, rpcrdma_addrstr(r_xprt));
		__assign_str(port, rpcrdma_portstr(r_xprt));
	),

	TP_printk("peer=[%s]:%s r_xprt=%p: status=%d %sconnected",
		__get_str(addr), __get_str(port),
		__entry->r_xprt, __entry->status,
		__entry->connected == 1 ? "still " : "dis"
	)
);

DEFINE_RXPRT_EVENT(xprtrdma_conn_start);
DEFINE_RXPRT_EVENT(xprtrdma_conn_tout);
DEFINE_RXPRT_EVENT(xprtrdma_create);
DEFINE_RXPRT_EVENT(xprtrdma_op_destroy);
DEFINE_RXPRT_EVENT(xprtrdma_remove);
DEFINE_RXPRT_EVENT(xprtrdma_reinsert);
DEFINE_RXPRT_EVENT(xprtrdma_reconnect);
DEFINE_RXPRT_EVENT(xprtrdma_op_inject_dsc);
DEFINE_RXPRT_EVENT(xprtrdma_op_close);
DEFINE_RXPRT_EVENT(xprtrdma_op_connect);

TRACE_EVENT(xprtrdma_qp_event,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		const struct ib_event *event
	),

	TP_ARGS(r_xprt, event),

	TP_STRUCT__entry(
		__field(const void *, r_xprt)
		__field(unsigned int, event)
		__string(name, event->device->name)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__entry->event = event->event;
		__assign_str(name, event->device->name);
		__assign_str(addr, rpcrdma_addrstr(r_xprt));
		__assign_str(port, rpcrdma_portstr(r_xprt));
	),

	TP_printk("peer=[%s]:%s r_xprt=%p: dev %s: %s (%u)",
		__get_str(addr), __get_str(port), __entry->r_xprt,
		__get_str(name), rdma_show_ib_event(__entry->event),
		__entry->event
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
		__field(const void *, r_xprt)
		__field(unsigned int, count)
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__entry->count = count;
	),

	TP_printk("r_xprt=%p: created %u MRs",
		__entry->r_xprt, __entry->count
	)
);

DEFINE_RXPRT_EVENT(xprtrdma_nomrs);

DEFINE_RDCH_EVENT(read);
DEFINE_WRCH_EVENT(write);
DEFINE_WRCH_EVENT(reply);

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
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, num_sge)
		__field(int, signaled)
		__field(int, status)
	),

	TP_fast_assign(
		const struct rpc_rqst *rqst = &req->rl_slot;

		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->req = req;
		__entry->num_sge = req->rl_sendctx->sc_wr.num_sge;
		__entry->signaled = req->rl_sendctx->sc_wr.send_flags &
				    IB_SEND_SIGNALED;
		__entry->status = status;
	),

	TP_printk("task:%u@%u req=%p (%d SGE%s) %sstatus=%d",
		__entry->task_id, __entry->client_id,
		__entry->req, __entry->num_sge,
		(__entry->num_sge == 1 ? "" : "s"),
		(__entry->signaled ? "signaled " : ""),
		__entry->status
	)
);

TRACE_EVENT(xprtrdma_post_recv,
	TP_PROTO(
		const struct ib_cqe *cqe
	),

	TP_ARGS(cqe),

	TP_STRUCT__entry(
		__field(const void *, cqe)
	),

	TP_fast_assign(
		__entry->cqe = cqe;
	),

	TP_printk("cqe=%p",
		__entry->cqe
	)
);

TRACE_EVENT(xprtrdma_post_recvs,
	TP_PROTO(
		const struct rpcrdma_xprt *r_xprt,
		unsigned int count,
		int status
	),

	TP_ARGS(r_xprt, count, status),

	TP_STRUCT__entry(
		__field(const void *, r_xprt)
		__field(unsigned int, count)
		__field(int, status)
		__field(int, posted)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__entry->count = count;
		__entry->status = status;
		__entry->posted = r_xprt->rx_ep.rep_receive_count;
		__assign_str(addr, rpcrdma_addrstr(r_xprt));
		__assign_str(port, rpcrdma_portstr(r_xprt));
	),

	TP_printk("peer=[%s]:%s r_xprt=%p: %u new recvs, %d active (rc %d)",
		__get_str(addr), __get_str(port), __entry->r_xprt,
		__entry->count, __entry->posted, __entry->status
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
		const struct ib_wc *wc
	),

	TP_ARGS(wc),

	TP_STRUCT__entry(
		__field(const void *, cqe)
		__field(u32, byte_len)
		__field(unsigned int, status)
		__field(u32, vendor_err)
	),

	TP_fast_assign(
		__entry->cqe = wc->wr_cqe;
		__entry->status = wc->status;
		if (wc->status) {
			__entry->byte_len = 0;
			__entry->vendor_err = wc->vendor_err;
		} else {
			__entry->byte_len = wc->byte_len;
			__entry->vendor_err = 0;
		}
	),

	TP_printk("cqe=%p %u bytes: %s (%u/0x%x)",
		__entry->cqe, __entry->byte_len,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

DEFINE_FRWR_DONE_EVENT(xprtrdma_wc_fastreg);
DEFINE_FRWR_DONE_EVENT(xprtrdma_wc_li);
DEFINE_FRWR_DONE_EVENT(xprtrdma_wc_li_wake);

TRACE_EVENT(xprtrdma_frwr_alloc,
	TP_PROTO(
		const struct rpcrdma_mr *mr,
		int rc
	),

	TP_ARGS(mr, rc),

	TP_STRUCT__entry(
		__field(const void *, mr)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->mr = mr;
		__entry->rc	= rc;
	),

	TP_printk("mr=%p: rc=%d",
		__entry->mr, __entry->rc
	)
);

TRACE_EVENT(xprtrdma_frwr_dereg,
	TP_PROTO(
		const struct rpcrdma_mr *mr,
		int rc
	),

	TP_ARGS(mr, rc),

	TP_STRUCT__entry(
		__field(const void *, mr)
		__field(u32, handle)
		__field(u32, length)
		__field(u64, offset)
		__field(u32, dir)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->mr = mr;
		__entry->handle = mr->mr_handle;
		__entry->length = mr->mr_length;
		__entry->offset = mr->mr_offset;
		__entry->dir    = mr->mr_dir;
		__entry->rc	= rc;
	),

	TP_printk("mr=%p %u@0x%016llx:0x%08x (%s): rc=%d",
		__entry->mr, __entry->length,
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
		__field(const void *, mr)
		__field(u64, addr)
		__field(u32, dir)
		__field(int, nents)
	),

	TP_fast_assign(
		__entry->mr = mr;
		__entry->addr = mr->mr_sg->dma_address;
		__entry->dir = mr->mr_dir;
		__entry->nents = sg_nents;
	),

	TP_printk("mr=%p dma addr=0x%llx (%s) sg_nents=%d",
		__entry->mr, __entry->addr,
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
		__field(const void *, mr)
		__field(u64, addr)
		__field(u32, dir)
		__field(int, num_mapped)
		__field(int, nents)
	),

	TP_fast_assign(
		__entry->mr = mr;
		__entry->addr = mr->mr_sg->dma_address;
		__entry->dir = mr->mr_dir;
		__entry->num_mapped = num_mapped;
		__entry->nents = mr->mr_nents;
	),

	TP_printk("mr=%p dma addr=0x%llx (%s) nents=%d of %d",
		__entry->mr, __entry->addr,
		xprtrdma_show_direction(__entry->dir),
		__entry->num_mapped, __entry->nents
	)
);

DEFINE_MR_EVENT(localinv);
DEFINE_MR_EVENT(map);
DEFINE_MR_EVENT(unmap);
DEFINE_MR_EVENT(remoteinv);
DEFINE_MR_EVENT(recycle);

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

TRACE_EVENT(xprtrdma_fixup,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		int len,
		int hdrlen
	),

	TP_ARGS(rqst, len, hdrlen),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, base)
		__field(int, len)
		__field(int, hdrlen)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->base = rqst->rq_rcv_buf.head[0].iov_base;
		__entry->len = len;
		__entry->hdrlen = hdrlen;
	),

	TP_printk("task:%u@%u base=%p len=%d hdrlen=%d",
		__entry->task_id, __entry->client_id,
		__entry->base, __entry->len, __entry->hdrlen
	)
);

TRACE_EVENT(xprtrdma_fixup_pg,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		int pageno,
		const void *pos,
		int len,
		int curlen
	),

	TP_ARGS(rqst, pageno, pos, len, curlen),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, pos)
		__field(int, pageno)
		__field(int, len)
		__field(int, curlen)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->pos = pos;
		__entry->pageno = pageno;
		__entry->len = len;
		__entry->curlen = curlen;
	),

	TP_printk("task:%u@%u pageno=%d pos=%p len=%d curlen=%d",
		__entry->task_id, __entry->client_id,
		__entry->pageno, __entry->pos, __entry->len, __entry->curlen
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

/**
 ** Allocation/release of rpcrdma_reqs and rpcrdma_reps
 **/

TRACE_EVENT(xprtrdma_op_allocate,
	TP_PROTO(
		const struct rpc_task *task,
		const struct rpcrdma_req *req
	),

	TP_ARGS(task, req),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, req)
		__field(size_t, callsize)
		__field(size_t, rcvsize)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->req = req;
		__entry->callsize = task->tk_rqstp->rq_callsize;
		__entry->rcvsize = task->tk_rqstp->rq_rcvsize;
	),

	TP_printk("task:%u@%u req=%p (%zu, %zu)",
		__entry->task_id, __entry->client_id,
		__entry->req, __entry->callsize, __entry->rcvsize
	)
);

TRACE_EVENT(xprtrdma_op_free,
	TP_PROTO(
		const struct rpc_task *task,
		const struct rpcrdma_req *req
	),

	TP_ARGS(task, req),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, req)
		__field(const void *, rep)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->req = req;
		__entry->rep = req->rl_reply;
	),

	TP_printk("task:%u@%u req=%p rep=%p",
		__entry->task_id, __entry->client_id,
		__entry->req, __entry->rep
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
		__field(const void *, r_xprt)
		__field(unsigned int, reqs)
		__string(addr, rpcrdma_addrstr(r_xprt))
		__string(port, rpcrdma_portstr(r_xprt))
	),

	TP_fast_assign(
		__entry->r_xprt = r_xprt;
		__entry->reqs = reqs;
		__assign_str(addr, rpcrdma_addrstr(r_xprt));
		__assign_str(port, rpcrdma_portstr(r_xprt));
	),

	TP_printk("peer=[%s]:%s r_xprt=%p: %u reqs",
		__get_str(addr), __get_str(port),
		__entry->r_xprt, __entry->reqs
	)
);

DEFINE_CB_EVENT(xprtrdma_cb_call);
DEFINE_CB_EVENT(xprtrdma_cb_reply);

TRACE_EVENT(xprtrdma_leaked_rep,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		const struct rpcrdma_rep *rep
	),

	TP_ARGS(rqst, rep),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(const void *, rep)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->rep = rep;
	),

	TP_printk("task:%u@%u xid=0x%08x rep=%p",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->rep
	)
);

/**
 ** Server-side RPC/RDMA events
 **/

DECLARE_EVENT_CLASS(svcrdma_xprt_event,
	TP_PROTO(
		const struct svc_xprt *xprt
	),

	TP_ARGS(xprt),

	TP_STRUCT__entry(
		__field(const void *, xprt)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("xprt=%p addr=%s",
		__entry->xprt, __get_str(addr)
	)
);

#define DEFINE_XPRT_EVENT(name)						\
		DEFINE_EVENT(svcrdma_xprt_event, svcrdma_xprt_##name,	\
				TP_PROTO(				\
					const struct svc_xprt *xprt	\
				),					\
				TP_ARGS(xprt))

DEFINE_XPRT_EVENT(accept);
DEFINE_XPRT_EVENT(fail);
DEFINE_XPRT_EVENT(free);

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
		__be32 *p,
		unsigned int hdrlen
	),

	TP_ARGS(p, hdrlen),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__field(u32, credits)
		__field(unsigned int, hdrlen)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpup(p++);
		__entry->vers = be32_to_cpup(p++);
		__entry->credits = be32_to_cpup(p++);
		__entry->proc = be32_to_cpup(p);
		__entry->hdrlen = hdrlen;
	),

	TP_printk("xid=0x%08x vers=%u credits=%u proc=%s hdrlen=%u",
		__entry->xid, __entry->vers, __entry->credits,
		show_rpcrdma_proc(__entry->proc), __entry->hdrlen)
);

TRACE_EVENT(svcrdma_decode_short,
	TP_PROTO(
		unsigned int hdrlen
	),

	TP_ARGS(hdrlen),

	TP_STRUCT__entry(
		__field(unsigned int, hdrlen)
	),

	TP_fast_assign(
		__entry->hdrlen = hdrlen;
	),

	TP_printk("hdrlen=%u", __entry->hdrlen)
);

DECLARE_EVENT_CLASS(svcrdma_badreq_event,
	TP_PROTO(
		__be32 *p
	),

	TP_ARGS(p),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__field(u32, credits)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpup(p++);
		__entry->vers = be32_to_cpup(p++);
		__entry->credits = be32_to_cpup(p++);
		__entry->proc = be32_to_cpup(p);
	),

	TP_printk("xid=0x%08x vers=%u credits=%u proc=%u",
		__entry->xid, __entry->vers, __entry->credits, __entry->proc)
);

#define DEFINE_BADREQ_EVENT(name)					\
		DEFINE_EVENT(svcrdma_badreq_event, svcrdma_decode_##name,\
				TP_PROTO(				\
					__be32 *p			\
				),					\
				TP_ARGS(p))

DEFINE_BADREQ_EVENT(badvers);
DEFINE_BADREQ_EVENT(drop);
DEFINE_BADREQ_EVENT(badproc);
DEFINE_BADREQ_EVENT(parse);

DECLARE_EVENT_CLASS(svcrdma_segment_event,
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

#define DEFINE_SEGMENT_EVENT(name)					\
		DEFINE_EVENT(svcrdma_segment_event, svcrdma_encode_##name,\
				TP_PROTO(				\
					u32 handle,			\
					u32 length,			\
					u64 offset			\
				),					\
				TP_ARGS(handle, length, offset))

DEFINE_SEGMENT_EVENT(rseg);
DEFINE_SEGMENT_EVENT(wseg);

DECLARE_EVENT_CLASS(svcrdma_chunk_event,
	TP_PROTO(
		u32 length
	),

	TP_ARGS(length),

	TP_STRUCT__entry(
		__field(u32, length)
	),

	TP_fast_assign(
		__entry->length = length;
	),

	TP_printk("length=%u",
		__entry->length
	)
);

#define DEFINE_CHUNK_EVENT(name)					\
		DEFINE_EVENT(svcrdma_chunk_event, svcrdma_encode_##name,\
				TP_PROTO(				\
					u32 length			\
				),					\
				TP_ARGS(length))

DEFINE_CHUNK_EVENT(pzr);
DEFINE_CHUNK_EVENT(write);
DEFINE_CHUNK_EVENT(reply);

TRACE_EVENT(svcrdma_encode_read,
	TP_PROTO(
		u32 length,
		u32 position
	),

	TP_ARGS(length, position),

	TP_STRUCT__entry(
		__field(u32, length)
		__field(u32, position)
	),

	TP_fast_assign(
		__entry->length = length;
		__entry->position = position;
	),

	TP_printk("length=%u position=%u",
		__entry->length, __entry->position
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

TRACE_EVENT(svcrdma_dma_map_page,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		const void *page
	),

	TP_ARGS(rdma, page),

	TP_STRUCT__entry(
		__field(const void *, page);
		__string(device, rdma->sc_cm_id->device->name)
		__string(addr, rdma->sc_xprt.xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->page = page;
		__assign_str(device, rdma->sc_cm_id->device->name);
		__assign_str(addr, rdma->sc_xprt.xpt_remotebuf);
	),

	TP_printk("addr=%s device=%s page=%p",
		__get_str(addr), __get_str(device), __entry->page
	)
);

TRACE_EVENT(svcrdma_dma_map_rwctx,
	TP_PROTO(
		const struct svcxprt_rdma *rdma,
		int status
	),

	TP_ARGS(rdma, status),

	TP_STRUCT__entry(
		__field(int, status)
		__string(device, rdma->sc_cm_id->device->name)
		__string(addr, rdma->sc_xprt.xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->status = status;
		__assign_str(device, rdma->sc_cm_id->device->name);
		__assign_str(addr, rdma->sc_xprt.xpt_remotebuf);
	),

	TP_printk("addr=%s device=%s status=%d",
		__get_str(addr), __get_str(device), __entry->status
	)
);

TRACE_EVENT(svcrdma_send_failed,
	TP_PROTO(
		const struct svc_rqst *rqst,
		int status
	),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(int, status)
		__field(u32, xid)
		__field(const void *, xprt)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->xid = __be32_to_cpu(rqst->rq_xid);
		__entry->xprt = rqst->rq_xprt;
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("xprt=%p addr=%s xid=0x%08x status=%d",
		__entry->xprt, __get_str(addr),
		__entry->xid, __entry->status
	)
);

DECLARE_EVENT_CLASS(svcrdma_sendcomp_event,
	TP_PROTO(
		const struct ib_wc *wc
	),

	TP_ARGS(wc),

	TP_STRUCT__entry(
		__field(const void *, cqe)
		__field(unsigned int, status)
		__field(unsigned int, vendor_err)
	),

	TP_fast_assign(
		__entry->cqe = wc->wr_cqe;
		__entry->status = wc->status;
		if (wc->status)
			__entry->vendor_err = wc->vendor_err;
		else
			__entry->vendor_err = 0;
	),

	TP_printk("cqe=%p status=%s (%u/0x%x)",
		__entry->cqe, rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

#define DEFINE_SENDCOMP_EVENT(name)					\
		DEFINE_EVENT(svcrdma_sendcomp_event, svcrdma_wc_##name,	\
				TP_PROTO(				\
					const struct ib_wc *wc		\
				),					\
				TP_ARGS(wc))

TRACE_EVENT(svcrdma_post_send,
	TP_PROTO(
		const struct ib_send_wr *wr,
		int status
	),

	TP_ARGS(wr, status),

	TP_STRUCT__entry(
		__field(const void *, cqe)
		__field(unsigned int, num_sge)
		__field(u32, inv_rkey)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->cqe = wr->wr_cqe;
		__entry->num_sge = wr->num_sge;
		__entry->inv_rkey = (wr->opcode == IB_WR_SEND_WITH_INV) ?
					wr->ex.invalidate_rkey : 0;
		__entry->status = status;
	),

	TP_printk("cqe=%p num_sge=%u inv_rkey=0x%08x status=%d",
		__entry->cqe, __entry->num_sge,
		__entry->inv_rkey, __entry->status
	)
);

DEFINE_SENDCOMP_EVENT(send);

TRACE_EVENT(svcrdma_post_recv,
	TP_PROTO(
		const struct ib_recv_wr *wr,
		int status
	),

	TP_ARGS(wr, status),

	TP_STRUCT__entry(
		__field(const void *, cqe)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->cqe = wr->wr_cqe;
		__entry->status = status;
	),

	TP_printk("cqe=%p status=%d",
		__entry->cqe, __entry->status
	)
);

TRACE_EVENT(svcrdma_wc_receive,
	TP_PROTO(
		const struct ib_wc *wc
	),

	TP_ARGS(wc),

	TP_STRUCT__entry(
		__field(const void *, cqe)
		__field(u32, byte_len)
		__field(unsigned int, status)
		__field(u32, vendor_err)
	),

	TP_fast_assign(
		__entry->cqe = wc->wr_cqe;
		__entry->status = wc->status;
		if (wc->status) {
			__entry->byte_len = 0;
			__entry->vendor_err = wc->vendor_err;
		} else {
			__entry->byte_len = wc->byte_len;
			__entry->vendor_err = 0;
		}
	),

	TP_printk("cqe=%p byte_len=%u status=%s (%u/0x%x)",
		__entry->cqe, __entry->byte_len,
		rdma_show_wc_status(__entry->status),
		__entry->status, __entry->vendor_err
	)
);

TRACE_EVENT(svcrdma_post_rw,
	TP_PROTO(
		const void *cqe,
		int sqecount,
		int status
	),

	TP_ARGS(cqe, sqecount, status),

	TP_STRUCT__entry(
		__field(const void *, cqe)
		__field(int, sqecount)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->cqe = cqe;
		__entry->sqecount = sqecount;
		__entry->status = status;
	),

	TP_printk("cqe=%p sqecount=%d status=%d",
		__entry->cqe, __entry->sqecount, __entry->status
	)
);

DEFINE_SENDCOMP_EVENT(read);
DEFINE_SENDCOMP_EVENT(write);

TRACE_EVENT(svcrdma_cm_event,
	TP_PROTO(
		const struct rdma_cm_event *event,
		const struct sockaddr *sap
	),

	TP_ARGS(event, sap),

	TP_STRUCT__entry(
		__field(unsigned int, event)
		__field(int, status)
		__array(__u8, addr, INET6_ADDRSTRLEN + 10)
	),

	TP_fast_assign(
		__entry->event = event->event;
		__entry->status = event->status;
		snprintf(__entry->addr, sizeof(__entry->addr) - 1,
			 "%pISpc", sap);
	),

	TP_printk("addr=%s event=%s (%u/%d)",
		__entry->addr,
		rdma_show_cm_event(__entry->event),
		__entry->event, __entry->status
	)
);

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
		__assign_str(device, event->device->name);
		snprintf(__entry->addr, sizeof(__entry->addr) - 1,
			 "%pISpc", sap);
	),

	TP_printk("addr=%s dev=%s event=%s (%u)",
		__entry->addr, __get_str(device),
		rdma_show_ib_event(__entry->event), __entry->event
	)
);

DECLARE_EVENT_CLASS(svcrdma_sendqueue_event,
	TP_PROTO(
		const struct svcxprt_rdma *rdma
	),

	TP_ARGS(rdma),

	TP_STRUCT__entry(
		__field(int, avail)
		__field(int, depth)
		__string(addr, rdma->sc_xprt.xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->avail = atomic_read(&rdma->sc_sq_avail);
		__entry->depth = rdma->sc_sq_depth;
		__assign_str(addr, rdma->sc_xprt.xpt_remotebuf);
	),

	TP_printk("addr=%s sc_sq_avail=%d/%d",
		__get_str(addr), __entry->avail, __entry->depth
	)
);

#define DEFINE_SQ_EVENT(name)						\
		DEFINE_EVENT(svcrdma_sendqueue_event, svcrdma_sq_##name,\
				TP_PROTO(				\
					const struct svcxprt_rdma *rdma \
				),					\
				TP_ARGS(rdma))

DEFINE_SQ_EVENT(full);
DEFINE_SQ_EVENT(retry);

#endif /* _TRACE_RPCRDMA_H */

#include <trace/define_trace.h>
