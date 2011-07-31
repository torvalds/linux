#undef TRACE_SYSTEM
#define TRACE_SYSTEM napi

#if !defined(_TRACE_NAPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NAPI_H_

#include <linux/netdevice.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(napi_poll,
	TP_PROTO(struct napi_struct *napi),
	TP_ARGS(napi));

#endif /* _TRACE_NAPI_H_ */

/* This part must be outside protection */
#include <trace/define_trace.h>
