/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gic

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GIC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GIC_H


#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
struct gic_chip_data;

DECLARE_HOOK(android_vh_gic_resume,
       TP_PROTO(struct gic_chip_data *gd),
       TP_ARGS(gd));
/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_GIC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
