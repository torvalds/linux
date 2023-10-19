#undef TRACE_SYSTEM
#define TRACE_SYSTEM compaction

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_COMPACTION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_COMPACTION_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_compaction_exit,
	TP_PROTO(int node_id, int order, const int highest_zoneidx),
	TP_ARGS(node_id, order, highest_zoneidx));
enum compact_result;
DECLARE_HOOK(android_vh_compaction_try_to_compact_pages_exit,
        TP_PROTO(enum compact_result *compact_result),
        TP_ARGS(compact_result));
#endif /* _TRACE_HOOK_COMPACTION_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
