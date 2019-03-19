/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */

/*
 * Copyright (c) 2018 Intel Corporation.  All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ib_mad

#if !defined(_TRACE_IB_MAD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IB_MAD_H

#include <linux/tracepoint.h>
#include <rdma/ib_mad.h>

#ifdef CONFIG_TRACEPOINTS
struct trace_event_raw_ib_mad_send_template;
static void create_mad_addr_info(struct ib_mad_send_wr_private *mad_send_wr,
			  struct ib_mad_qp_info *qp_info,
			  struct trace_event_raw_ib_mad_send_template *entry);
#endif

DECLARE_EVENT_CLASS(ib_mad_send_template,
	TP_PROTO(struct ib_mad_send_wr_private *wr,
		 struct ib_mad_qp_info *qp_info),
	TP_ARGS(wr, qp_info),

	TP_STRUCT__entry(
		__field(u8,             base_version)
		__field(u8,             mgmt_class)
		__field(u8,             class_version)
		__field(u8,             port_num)
		__field(u32,            qp_num)
		__field(u8,             method)
		__field(u8,             sl)
		__field(u16,            attr_id)
		__field(u32,            attr_mod)
		__field(u64,            wrtid)
		__field(u64,            tid)
		__field(u16,            status)
		__field(u16,            class_specific)
		__field(u32,            length)
		__field(u32,            dlid)
		__field(u32,            rqpn)
		__field(u32,            rqkey)
		__field(u32,            dev_index)
		__field(void *,         agent_priv)
		__field(unsigned long,  timeout)
		__field(int,            retries_left)
		__field(int,            max_retries)
		__field(int,            retry)
		__field(u16,            pkey)
	),

	TP_fast_assign(
		__entry->dev_index = wr->mad_agent_priv->agent.device->index;
		__entry->port_num = wr->mad_agent_priv->agent.port_num;
		__entry->qp_num = wr->mad_agent_priv->qp_info->qp->qp_num;
		__entry->agent_priv = wr->mad_agent_priv;
		__entry->wrtid = wr->tid;
		__entry->max_retries = wr->max_retries;
		__entry->retries_left = wr->retries_left;
		__entry->retry = wr->retry;
		__entry->timeout = wr->timeout;
		__entry->length = wr->send_buf.hdr_len +
				  wr->send_buf.data_len;
		__entry->base_version =
			((struct ib_mad_hdr *)wr->send_buf.mad)->base_version;
		__entry->mgmt_class =
			((struct ib_mad_hdr *)wr->send_buf.mad)->mgmt_class;
		__entry->class_version =
			((struct ib_mad_hdr *)wr->send_buf.mad)->class_version;
		__entry->method =
			((struct ib_mad_hdr *)wr->send_buf.mad)->method;
		__entry->status =
			((struct ib_mad_hdr *)wr->send_buf.mad)->status;
		__entry->class_specific =
			((struct ib_mad_hdr *)wr->send_buf.mad)->class_specific;
		__entry->tid = ((struct ib_mad_hdr *)wr->send_buf.mad)->tid;
		__entry->attr_id =
			((struct ib_mad_hdr *)wr->send_buf.mad)->attr_id;
		__entry->attr_mod =
			((struct ib_mad_hdr *)wr->send_buf.mad)->attr_mod;
		create_mad_addr_info(wr, qp_info, __entry);
	),

	TP_printk("%d:%d QP%d agent %p: " \
		  "wrtid 0x%llx; %d/%d retries(%d); timeout %lu length %d : " \
		  "hdr : base_ver 0x%x class 0x%x class_ver 0x%x " \
		  "method 0x%x status 0x%x class_specific 0x%x tid 0x%llx " \
		  "attr_id 0x%x attr_mod 0x%x  => dlid 0x%08x sl %d "\
		  "pkey 0x%x rpqn 0x%x rqpkey 0x%x",
		__entry->dev_index, __entry->port_num, __entry->qp_num,
		__entry->agent_priv, be64_to_cpu(__entry->wrtid),
		__entry->retries_left, __entry->max_retries,
		__entry->retry, __entry->timeout, __entry->length,
		__entry->base_version, __entry->mgmt_class,
		__entry->class_version,
		__entry->method, be16_to_cpu(__entry->status),
		be16_to_cpu(__entry->class_specific),
		be64_to_cpu(__entry->tid), be16_to_cpu(__entry->attr_id),
		be32_to_cpu(__entry->attr_mod),
		be32_to_cpu(__entry->dlid), __entry->sl, __entry->pkey,
		__entry->rqpn, __entry->rqkey
	)
);

DEFINE_EVENT(ib_mad_send_template, ib_mad_error_handler,
	TP_PROTO(struct ib_mad_send_wr_private *wr,
		 struct ib_mad_qp_info *qp_info),
	TP_ARGS(wr, qp_info));
DEFINE_EVENT(ib_mad_send_template, ib_mad_ib_send_mad,
	TP_PROTO(struct ib_mad_send_wr_private *wr,
		 struct ib_mad_qp_info *qp_info),
	TP_ARGS(wr, qp_info));
DEFINE_EVENT(ib_mad_send_template, ib_mad_send_done_resend,
	TP_PROTO(struct ib_mad_send_wr_private *wr,
		 struct ib_mad_qp_info *qp_info),
	TP_ARGS(wr, qp_info));

TRACE_EVENT(ib_mad_send_done_handler,
	TP_PROTO(struct ib_mad_send_wr_private *wr, struct ib_wc *wc),
	TP_ARGS(wr, wc),

	TP_STRUCT__entry(
		__field(u8,             port_num)
		__field(u8,             base_version)
		__field(u8,             mgmt_class)
		__field(u8,             class_version)
		__field(u32,            qp_num)
		__field(u64,            wrtid)
		__field(u16,            status)
		__field(u16,            wc_status)
		__field(u32,            length)
		__field(void *,         agent_priv)
		__field(unsigned long,  timeout)
		__field(u32,            dev_index)
		__field(int,            retries_left)
		__field(int,            max_retries)
		__field(int,            retry)
		__field(u8,             method)
	),

	TP_fast_assign(
		__entry->dev_index = wr->mad_agent_priv->agent.device->index;
		__entry->port_num = wr->mad_agent_priv->agent.port_num;
		__entry->qp_num = wr->mad_agent_priv->qp_info->qp->qp_num;
		__entry->agent_priv = wr->mad_agent_priv;
		__entry->wrtid = wr->tid;
		__entry->max_retries = wr->max_retries;
		__entry->retries_left = wr->retries_left;
		__entry->retry = wr->retry;
		__entry->timeout = wr->timeout;
		__entry->base_version =
			((struct ib_mad_hdr *)wr->send_buf.mad)->base_version;
		__entry->mgmt_class =
			((struct ib_mad_hdr *)wr->send_buf.mad)->mgmt_class;
		__entry->class_version =
			((struct ib_mad_hdr *)wr->send_buf.mad)->class_version;
		__entry->method =
			((struct ib_mad_hdr *)wr->send_buf.mad)->method;
		__entry->status =
			((struct ib_mad_hdr *)wr->send_buf.mad)->status;
		__entry->wc_status = wc->status;
		__entry->length = wc->byte_len;
	),

	TP_printk("%d:%d QP%d : SEND WC Status %d : agent %p: " \
		  "wrtid 0x%llx %d/%d retries(%d) timeout %lu length %d: " \
		  "hdr : base_ver 0x%x class 0x%x class_ver 0x%x " \
		  "method 0x%x status 0x%x",
		__entry->dev_index, __entry->port_num, __entry->qp_num,
		__entry->wc_status,
		__entry->agent_priv, be64_to_cpu(__entry->wrtid),
		__entry->retries_left, __entry->max_retries,
		__entry->retry, __entry->timeout,
		__entry->length,
		__entry->base_version, __entry->mgmt_class,
		__entry->class_version, __entry->method,
		be16_to_cpu(__entry->status)
	)
);


#endif /* _TRACE_IB_MAD_H */

#include <trace/define_trace.h>
