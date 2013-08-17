#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory_bus

#if !defined(_TRACE_BUS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEMORY_BUS_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(memory_bus_usage,

	TP_PROTO(const char *name, unsigned long long rw_bytes,
			unsigned long long r_bytes,
			unsigned long long w_bytes,
			unsigned long long cycles,
			unsigned long long ns),

	TP_ARGS(name, rw_bytes, r_bytes, w_bytes, cycles, ns),

	TP_STRUCT__entry(
		__string(       	name,	name)
		__field(unsigned long long, 	rw_bytes)
		__field(unsigned long long, 	r_bytes)
		__field(unsigned long long, 	w_bytes)
		__field(unsigned long long,	cycles)
		__field(unsigned long long, 	ns)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->rw_bytes = rw_bytes;
		__entry->r_bytes = r_bytes;
		__entry->w_bytes = w_bytes;
		__entry->cycles = cycles;
		__entry->ns = ns;
	),

	TP_printk("bus=%s rw_bytes=%llu r_bytes=%llu w_bytes=%llu cycles=%llu ns=%llu",
		__get_str(name),
		__entry->rw_bytes,
		__entry->r_bytes,
		__entry->w_bytes,
		__entry->cycles,
		__entry->ns)
);


#endif /* _TRACE_MEMORY_BUS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
