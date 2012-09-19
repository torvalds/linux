#undef TRACE_SYSTEM
#define TRACE_SYSTEM ras
#define TRACE_INCLUDE_FILE ras_event

#if !defined(_TRACE_HW_EVENT_MC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HW_EVENT_MC_H

#include <linux/tracepoint.h>
#include <linux/edac.h>
#include <linux/ktime.h>

/*
 * Hardware Events Report
 *
 * Those events are generated when hardware detected a corrected or
 * uncorrected event, and are meant to replace the current API to report
 * errors defined on both EDAC and MCE subsystems.
 *
 * FIXME: Add events for handling memory errors originated from the
 *        MCE subsystem.
 */

/*
 * Hardware-independent Memory Controller specific events
 */

/*
 * Default error mechanisms for Memory Controller errors (CE and UE)
 */
TRACE_EVENT(mc_event,

	TP_PROTO(const unsigned int err_type,
		 const char *error_msg,
		 const char *label,
		 const int error_count,
		 const u8 mc_index,
		 const s8 top_layer,
		 const s8 mid_layer,
		 const s8 low_layer,
		 unsigned long address,
		 const u8 grain_bits,
		 unsigned long syndrome,
		 const char *driver_detail),

	TP_ARGS(err_type, error_msg, label, error_count, mc_index,
		top_layer, mid_layer, low_layer, address, grain_bits,
		syndrome, driver_detail),

	TP_STRUCT__entry(
		__field(	unsigned int,	error_type		)
		__string(	msg,		error_msg		)
		__string(	label,		label			)
		__field(	u16,		error_count		)
		__field(	u8,		mc_index		)
		__field(	s8,		top_layer		)
		__field(	s8,		middle_layer		)
		__field(	s8,		lower_layer		)
		__field(	long,		address			)
		__field(	u8,		grain_bits		)
		__field(	long,		syndrome		)
		__string(	driver_detail,	driver_detail		)
	),

	TP_fast_assign(
		__entry->error_type		= err_type;
		__assign_str(msg, error_msg);
		__assign_str(label, label);
		__entry->error_count		= error_count;
		__entry->mc_index		= mc_index;
		__entry->top_layer		= top_layer;
		__entry->middle_layer		= mid_layer;
		__entry->lower_layer		= low_layer;
		__entry->address		= address;
		__entry->grain_bits		= grain_bits;
		__entry->syndrome		= syndrome;
		__assign_str(driver_detail, driver_detail);
	),

	TP_printk("%d %s error%s:%s%s on %s (mc:%d location:%d:%d:%d address:0x%08lx grain:%d syndrome:0x%08lx%s%s)",
		  __entry->error_count,
		  (__entry->error_type == HW_EVENT_ERR_CORRECTED) ? "Corrected" :
			((__entry->error_type == HW_EVENT_ERR_FATAL) ?
			"Fatal" : "Uncorrected"),
		  __entry->error_count > 1 ? "s" : "",
		  ((char *)__get_str(msg))[0] ? " " : "",
		  __get_str(msg),
		  __get_str(label),
		  __entry->mc_index,
		  __entry->top_layer,
		  __entry->middle_layer,
		  __entry->lower_layer,
		  __entry->address,
		  1 << __entry->grain_bits,
		  __entry->syndrome,
		  ((char *)__get_str(driver_detail))[0] ? " " : "",
		  __get_str(driver_detail))
);

#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
