#undef TRACE_SYSTEM
#define TRACE_SYSTEM napi

#if !defined(_TRACE_NAPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NAPI_H_

#include <linux/netdevice.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>

#define NO_DEV "(no_device)"

TRACE_EVENT(napi_poll,

	TP_PROTO(struct napi_struct *napi),

	TP_ARGS(napi),

	TP_STRUCT__entry(
		__field(	struct napi_struct *,	napi)
		__string(	dev_name, napi->dev ? napi->dev->name : NO_DEV)
	),

	TP_fast_assign(
		__entry->napi = napi;
		__assign_str(dev_name, napi->dev ? napi->dev->name : NO_DEV);
	),

	TP_printk("napi poll on napi struct %p for device %s",
		__entry->napi, __get_str(dev_name))
);

#undef NO_DEV

#endif /* _TRACE_NAPI_H_ */

/* This part must be outside protection */
#include <trace/define_trace.h>
