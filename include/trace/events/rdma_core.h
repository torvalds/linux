/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace point definitions for core RDMA functions.
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rdma_core

#if !defined(_TRACE_RDMA_CORE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RDMA_CORE_H

#include <linux/tracepoint.h>
#include <rdma/ib_verbs.h>

/*
 * enum ib_poll_context, from include/rdma/ib_verbs.h
 */
#define IB_POLL_CTX_LIST			\
	ib_poll_ctx(DIRECT)			\
	ib_poll_ctx(SOFTIRQ)			\
	ib_poll_ctx(WORKQUEUE)			\
	ib_poll_ctx_end(UNBOUND_WORKQUEUE)

#undef ib_poll_ctx
#undef ib_poll_ctx_end

#define ib_poll_ctx(x)		TRACE_DEFINE_ENUM(IB_POLL_##x);
#define ib_poll_ctx_end(x)	TRACE_DEFINE_ENUM(IB_POLL_##x);

IB_POLL_CTX_LIST

#undef ib_poll_ctx
#undef ib_poll_ctx_end

#define ib_poll_ctx(x)		{ IB_POLL_##x, #x },
#define ib_poll_ctx_end(x)	{ IB_POLL_##x, #x }

#define rdma_show_ib_poll_ctx(x) \
		__print_symbolic(x, IB_POLL_CTX_LIST)

/**
 ** Completion Queue events
 **/

TRACE_EVENT(cq_schedule,
	TP_PROTO(
		struct ib_cq *cq
	),

	TP_ARGS(cq),

	TP_STRUCT__entry(
		__field(u32, cq_id)
	),

	TP_fast_assign(
		cq->timestamp = ktime_get();
		cq->interrupt = true;

		__entry->cq_id = cq->res.id;
	),

	TP_printk("cq.id=%u", __entry->cq_id)
);

TRACE_EVENT(cq_reschedule,
	TP_PROTO(
		struct ib_cq *cq
	),

	TP_ARGS(cq),

	TP_STRUCT__entry(
		__field(u32, cq_id)
	),

	TP_fast_assign(
		cq->timestamp = ktime_get();
		cq->interrupt = false;

		__entry->cq_id = cq->res.id;
	),

	TP_printk("cq.id=%u", __entry->cq_id)
);

TRACE_EVENT(cq_process,
	TP_PROTO(
		const struct ib_cq *cq
	),

	TP_ARGS(cq),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(bool, interrupt)
		__field(s64, latency)
	),

	TP_fast_assign(
		ktime_t latency = ktime_sub(ktime_get(), cq->timestamp);

		__entry->cq_id = cq->res.id;
		__entry->latency = ktime_to_us(latency);
		__entry->interrupt = cq->interrupt;
	),

	TP_printk("cq.id=%u wake-up took %lld [us] from %s",
		__entry->cq_id, __entry->latency,
		__entry->interrupt ? "interrupt" : "reschedule"
	)
);

TRACE_EVENT(cq_poll,
	TP_PROTO(
		const struct ib_cq *cq,
		int requested,
		int rc
	),

	TP_ARGS(cq, requested, rc),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, requested)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->cq_id = cq->res.id;
		__entry->requested = requested;
		__entry->rc = rc;
	),

	TP_printk("cq.id=%u requested %d, returned %d",
		__entry->cq_id, __entry->requested, __entry->rc
	)
);

TRACE_EVENT(cq_drain_complete,
	TP_PROTO(
		const struct ib_cq *cq
	),

	TP_ARGS(cq),

	TP_STRUCT__entry(
		__field(u32, cq_id)
	),

	TP_fast_assign(
		__entry->cq_id = cq->res.id;
	),

	TP_printk("cq.id=%u",
		__entry->cq_id
	)
);


TRACE_EVENT(cq_modify,
	TP_PROTO(
		const struct ib_cq *cq,
		u16 comps,
		u16 usec
	),

	TP_ARGS(cq, comps, usec),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(unsigned int, comps)
		__field(unsigned int, usec)
	),

	TP_fast_assign(
		__entry->cq_id = cq->res.id;
		__entry->comps = comps;
		__entry->usec = usec;
	),

	TP_printk("cq.id=%u comps=%u usec=%u",
		__entry->cq_id, __entry->comps, __entry->usec
	)
);

TRACE_EVENT(cq_alloc,
	TP_PROTO(
		const struct ib_cq *cq,
		int nr_cqe,
		int comp_vector,
		enum ib_poll_context poll_ctx
	),

	TP_ARGS(cq, nr_cqe, comp_vector, poll_ctx),

	TP_STRUCT__entry(
		__field(u32, cq_id)
		__field(int, nr_cqe)
		__field(int, comp_vector)
		__field(unsigned long, poll_ctx)
	),

	TP_fast_assign(
		__entry->cq_id = cq->res.id;
		__entry->nr_cqe = nr_cqe;
		__entry->comp_vector = comp_vector;
		__entry->poll_ctx = poll_ctx;
	),

	TP_printk("cq.id=%u nr_cqe=%d comp_vector=%d poll_ctx=%s",
		__entry->cq_id, __entry->nr_cqe, __entry->comp_vector,
		rdma_show_ib_poll_ctx(__entry->poll_ctx)
	)
);

TRACE_EVENT(cq_alloc_error,
	TP_PROTO(
		int nr_cqe,
		int comp_vector,
		enum ib_poll_context poll_ctx,
		int rc
	),

	TP_ARGS(nr_cqe, comp_vector, poll_ctx, rc),

	TP_STRUCT__entry(
		__field(int, rc)
		__field(int, nr_cqe)
		__field(int, comp_vector)
		__field(unsigned long, poll_ctx)
	),

	TP_fast_assign(
		__entry->rc = rc;
		__entry->nr_cqe = nr_cqe;
		__entry->comp_vector = comp_vector;
		__entry->poll_ctx = poll_ctx;
	),

	TP_printk("nr_cqe=%d comp_vector=%d poll_ctx=%s rc=%d",
		__entry->nr_cqe, __entry->comp_vector,
		rdma_show_ib_poll_ctx(__entry->poll_ctx), __entry->rc
	)
);

TRACE_EVENT(cq_free,
	TP_PROTO(
		const struct ib_cq *cq
	),

	TP_ARGS(cq),

	TP_STRUCT__entry(
		__field(u32, cq_id)
	),

	TP_fast_assign(
		__entry->cq_id = cq->res.id;
	),

	TP_printk("cq.id=%u", __entry->cq_id)
);

/**
 ** Memory Region events
 **/

/*
 * enum ib_mr_type, from include/rdma/ib_verbs.h
 */
#define IB_MR_TYPE_LIST				\
	ib_mr_type_item(MEM_REG)		\
	ib_mr_type_item(SG_GAPS)		\
	ib_mr_type_item(DM)			\
	ib_mr_type_item(USER)			\
	ib_mr_type_item(DMA)			\
	ib_mr_type_end(INTEGRITY)

#undef ib_mr_type_item
#undef ib_mr_type_end

#define ib_mr_type_item(x)	TRACE_DEFINE_ENUM(IB_MR_TYPE_##x);
#define ib_mr_type_end(x)	TRACE_DEFINE_ENUM(IB_MR_TYPE_##x);

IB_MR_TYPE_LIST

#undef ib_mr_type_item
#undef ib_mr_type_end

#define ib_mr_type_item(x)	{ IB_MR_TYPE_##x, #x },
#define ib_mr_type_end(x)	{ IB_MR_TYPE_##x, #x }

#define rdma_show_ib_mr_type(x) \
		__print_symbolic(x, IB_MR_TYPE_LIST)

TRACE_EVENT(mr_alloc,
	TP_PROTO(
		const struct ib_pd *pd,
		enum ib_mr_type mr_type,
		u32 max_num_sg,
		const struct ib_mr *mr
	),

	TP_ARGS(pd, mr_type, max_num_sg, mr),

	TP_STRUCT__entry(
		__field(u32, pd_id)
		__field(u32, mr_id)
		__field(u32, max_num_sg)
		__field(int, rc)
		__field(unsigned long, mr_type)
	),

	TP_fast_assign(
		__entry->pd_id = pd->res.id;
		if (IS_ERR(mr)) {
			__entry->mr_id = 0;
			__entry->rc = PTR_ERR(mr);
		} else {
			__entry->mr_id = mr->res.id;
			__entry->rc = 0;
		}
		__entry->max_num_sg = max_num_sg;
		__entry->mr_type = mr_type;
	),

	TP_printk("pd.id=%u mr.id=%u type=%s max_num_sg=%u rc=%d",
		__entry->pd_id, __entry->mr_id,
		rdma_show_ib_mr_type(__entry->mr_type),
		__entry->max_num_sg, __entry->rc)
);

TRACE_EVENT(mr_integ_alloc,
	TP_PROTO(
		const struct ib_pd *pd,
		u32 max_num_data_sg,
		u32 max_num_meta_sg,
		const struct ib_mr *mr
	),

	TP_ARGS(pd, max_num_data_sg, max_num_meta_sg, mr),

	TP_STRUCT__entry(
		__field(u32, pd_id)
		__field(u32, mr_id)
		__field(u32, max_num_data_sg)
		__field(u32, max_num_meta_sg)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->pd_id = pd->res.id;
		if (IS_ERR(mr)) {
			__entry->mr_id = 0;
			__entry->rc = PTR_ERR(mr);
		} else {
			__entry->mr_id = mr->res.id;
			__entry->rc = 0;
		}
		__entry->max_num_data_sg = max_num_data_sg;
		__entry->max_num_meta_sg = max_num_meta_sg;
	),

	TP_printk("pd.id=%u mr.id=%u max_num_data_sg=%u max_num_meta_sg=%u rc=%d",
		__entry->pd_id, __entry->mr_id, __entry->max_num_data_sg,
		__entry->max_num_meta_sg, __entry->rc)
);

TRACE_EVENT(mr_dereg,
	TP_PROTO(
		const struct ib_mr *mr
	),

	TP_ARGS(mr),

	TP_STRUCT__entry(
		__field(u32, id)
	),

	TP_fast_assign(
		__entry->id = mr->res.id;
	),

	TP_printk("mr.id=%u", __entry->id)
);

#endif /* _TRACE_RDMA_CORE_H */

#include <trace/define_trace.h>
