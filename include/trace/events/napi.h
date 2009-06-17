#ifndef _TRACE_NAPI_H_
#define _TRACE_NAPI_H_

#include <linux/netdevice.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(napi_poll,
	TP_PROTO(struct napi_struct *napi),
	TP_ARGS(napi));

#endif
