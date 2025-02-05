/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nbd

#if !defined(_TRACE_NBD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NBD_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(nbd_transport_event,

	TP_PROTO(struct request *req, u64 handle),

	TP_ARGS(req, handle),

	TP_STRUCT__entry(
		__field(struct request *, req)
		__field(u64, handle)
	),

	TP_fast_assign(
		__entry->req = req;
		__entry->handle = handle;
	),

	TP_printk(
		"nbd transport event: request %p, handle 0x%016llx",
		__entry->req,
		__entry->handle
	)
);

DEFINE_EVENT(nbd_transport_event, nbd_header_sent,

	TP_PROTO(struct request *req, u64 handle),

	TP_ARGS(req, handle)
);

DEFINE_EVENT(nbd_transport_event, nbd_payload_sent,

	TP_PROTO(struct request *req, u64 handle),

	TP_ARGS(req, handle)
);

DEFINE_EVENT(nbd_transport_event, nbd_header_received,

	TP_PROTO(struct request *req, u64 handle),

	TP_ARGS(req, handle)
);

DEFINE_EVENT(nbd_transport_event, nbd_payload_received,

	TP_PROTO(struct request *req, u64 handle),

	TP_ARGS(req, handle)
);

DECLARE_EVENT_CLASS(nbd_send_request,

	TP_PROTO(struct nbd_request *nbd_request, int index,
		 struct request *rq),

	TP_ARGS(nbd_request, index, rq),

	TP_STRUCT__entry(
		__field(struct nbd_request *, nbd_request)
		__field(u64, dev_index)
		__field(struct request *, request)
	),

	TP_fast_assign(
		__entry->nbd_request = NULL;
		__entry->dev_index = index;
		__entry->request = rq;
	),

	TP_printk("nbd%lld: request %p", __entry->dev_index, __entry->request)
);

#ifdef DEFINE_EVENT_WRITABLE
#undef NBD_DEFINE_EVENT
#define NBD_DEFINE_EVENT(template, call, proto, args, size)		\
	DEFINE_EVENT_WRITABLE(template, call, PARAMS(proto),		\
			      PARAMS(args), size)
#else
#undef NBD_DEFINE_EVENT
#define NBD_DEFINE_EVENT(template, call, proto, args, size)		\
	DEFINE_EVENT(template, call, PARAMS(proto), PARAMS(args))
#endif

NBD_DEFINE_EVENT(nbd_send_request, nbd_send_request,

	TP_PROTO(struct nbd_request *nbd_request, int index,
		 struct request *rq),

	TP_ARGS(nbd_request, index, rq),

	sizeof(struct nbd_request)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
