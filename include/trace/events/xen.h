#undef TRACE_SYSTEM
#define TRACE_SYSTEM xen

#if !defined(_TRACE_XEN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XEN_H

#include <linux/tracepoint.h>
#include <asm/paravirt_types.h>

#endif /*  _TRACE_XEN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
