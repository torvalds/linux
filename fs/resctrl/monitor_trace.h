/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM resctrl

#if !defined(_FS_RESCTRL_MONITOR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _FS_RESCTRL_MONITOR_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(mon_llc_occupancy_limbo,
	    TP_PROTO(u32 ctrl_hw_id, u32 mon_hw_id, int domain_id, u64 llc_occupancy_bytes),
	    TP_ARGS(ctrl_hw_id, mon_hw_id, domain_id, llc_occupancy_bytes),
	    TP_STRUCT__entry(__field(u32, ctrl_hw_id)
			     __field(u32, mon_hw_id)
			     __field(int, domain_id)
			     __field(u64, llc_occupancy_bytes)),
	    TP_fast_assign(__entry->ctrl_hw_id = ctrl_hw_id;
			   __entry->mon_hw_id = mon_hw_id;
			   __entry->domain_id = domain_id;
			   __entry->llc_occupancy_bytes = llc_occupancy_bytes;),
	    TP_printk("ctrl_hw_id=%u mon_hw_id=%u domain_id=%d llc_occupancy_bytes=%llu",
		      __entry->ctrl_hw_id, __entry->mon_hw_id, __entry->domain_id,
		      __entry->llc_occupancy_bytes)
	   );

#endif /* _FS_RESCTRL_MONITOR_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#define TRACE_INCLUDE_FILE monitor_trace

#include <trace/define_trace.h>
