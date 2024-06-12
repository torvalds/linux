/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM qrtr

#if !defined(_TRACE_QRTR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QRTR_H

#include <linux/qrtr.h>
#include <linux/tracepoint.h>

TRACE_EVENT(qrtr_ns_service_announce_new,

	TP_PROTO(unsigned int service, unsigned int instance,
		 unsigned int node, unsigned int port),

	TP_ARGS(service, instance, node, port),

	TP_STRUCT__entry(
		__field(unsigned int, service)
		__field(unsigned int, instance)
		__field(unsigned int, node)
		__field(unsigned int, port)
	),

	TP_fast_assign(
		__entry->service = service;
		__entry->instance = instance;
		__entry->node = node;
		__entry->port = port;
	),

	TP_printk("advertising new server [%d:%x]@[%d:%d]",
		  __entry->service, __entry->instance, __entry->node,
		  __entry->port
	)
);

TRACE_EVENT(qrtr_ns_service_announce_del,

	TP_PROTO(unsigned int service, unsigned int instance,
		 unsigned int node, unsigned int port),

	TP_ARGS(service, instance, node, port),

	TP_STRUCT__entry(
		__field(unsigned int, service)
		__field(unsigned int, instance)
		__field(unsigned int, node)
		__field(unsigned int, port)
	),

	TP_fast_assign(
		__entry->service = service;
		__entry->instance = instance;
		__entry->node = node;
		__entry->port = port;
	),

	TP_printk("advertising removal of server [%d:%x]@[%d:%d]",
		  __entry->service, __entry->instance, __entry->node,
		  __entry->port
	)
);

TRACE_EVENT(qrtr_ns_server_add,

	TP_PROTO(unsigned int service, unsigned int instance,
		 unsigned int node, unsigned int port),

	TP_ARGS(service, instance, node, port),

	TP_STRUCT__entry(
		__field(unsigned int, service)
		__field(unsigned int, instance)
		__field(unsigned int, node)
		__field(unsigned int, port)
	),

	TP_fast_assign(
		__entry->service = service;
		__entry->instance = instance;
		__entry->node = node;
		__entry->port = port;
	),

	TP_printk("add server [%d:%x]@[%d:%d]",
		  __entry->service, __entry->instance, __entry->node,
		  __entry->port
	)
);

TRACE_EVENT(qrtr_ns_message,

	TP_PROTO(const char * const ctrl_pkt_str, __u32 sq_node, __u32 sq_port),

	TP_ARGS(ctrl_pkt_str, sq_node, sq_port),

	TP_STRUCT__entry(
		__string(ctrl_pkt_str, ctrl_pkt_str)
		__field(__u32, sq_node)
		__field(__u32, sq_port)
	),

	TP_fast_assign(
		__assign_str(ctrl_pkt_str);
		__entry->sq_node = sq_node;
		__entry->sq_port = sq_port;
	),

	TP_printk("%s from %d:%d",
		  __get_str(ctrl_pkt_str), __entry->sq_node, __entry->sq_port
	)
);

#endif /* _TRACE_QRTR_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
