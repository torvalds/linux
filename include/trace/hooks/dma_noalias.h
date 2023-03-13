/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dma_noalias

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMA_NOALIAS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMA_NOALIAS_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_setup_dma_ops,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev), 1);

#endif /*_TRACE_HOOK_DMA_NOALIAS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
