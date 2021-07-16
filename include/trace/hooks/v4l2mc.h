/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM v4l2mc

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_V4L2MC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_V4L2MC_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct media_link;
struct media_link_desc;
DECLARE_HOOK(android_vh_media_device_setup_link,
	TP_PROTO(struct media_link *link, struct media_link_desc *linkd, int *ret),
	TP_ARGS(link, linkd, ret));

DECLARE_RESTRICTED_HOOK(android_rvh_media_device_setup_link,
	TP_PROTO(struct media_link *link,
	struct media_link_desc *linkd, int *ret),
	TP_ARGS(link, linkd, ret), 1);

#endif /* _TRACE_HOOK_V4L2MC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

