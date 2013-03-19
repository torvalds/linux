#undef TRACE_SYSTEM
#define TRACE_SYSTEM ras

#if !defined(_TRACE_AER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_AER_H

#include <linux/tracepoint.h>
#include <linux/edac.h>


/*
 * PCIe AER Trace event
 *
 * These events are generated when hardware detects a corrected or
 * uncorrected event on a PCIe device. The event report has
 * the following structure:
 *
 * char * dev_name -	The name of the slot where the device resides
 *			([domain:]bus:device.function).
 * u32 status -		Either the correctable or uncorrectable register
 *			indicating what error or errors have been seen
 * u8 severity -	error severity 0:NONFATAL 1:FATAL 2:CORRECTED
 */

#define aer_correctable_errors		\
	{BIT(0),	"Receiver Error"},		\
	{BIT(6),	"Bad TLP"},			\
	{BIT(7),	"Bad DLLP"},			\
	{BIT(8),	"RELAY_NUM Rollover"},		\
	{BIT(12),	"Replay Timer Timeout"},	\
	{BIT(13),	"Advisory Non-Fatal"}

#define aer_uncorrectable_errors		\
	{BIT(4),	"Data Link Protocol"},		\
	{BIT(12),	"Poisoned TLP"},		\
	{BIT(13),	"Flow Control Protocol"},	\
	{BIT(14),	"Completion Timeout"},		\
	{BIT(15),	"Completer Abort"},		\
	{BIT(16),	"Unexpected Completion"},	\
	{BIT(17),	"Receiver Overflow"},		\
	{BIT(18),	"Malformed TLP"},		\
	{BIT(19),	"ECRC"},			\
	{BIT(20),	"Unsupported Request"}

TRACE_EVENT(aer_event,
	TP_PROTO(const char *dev_name,
		 const u32 status,
		 const u8 severity),

	TP_ARGS(dev_name, status, severity),

	TP_STRUCT__entry(
		__string(	dev_name,	dev_name	)
		__field(	u32,		status		)
		__field(	u8,		severity	)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->status		= status;
		__entry->severity	= severity;
	),

	TP_printk("%s PCIe Bus Error: severity=%s, %s\n",
		__get_str(dev_name),
		__entry->severity == HW_EVENT_ERR_CORRECTED ? "Corrected" :
			__entry->severity == HW_EVENT_ERR_FATAL ?
			"Fatal" : "Uncorrected",
		__entry->severity == HW_EVENT_ERR_CORRECTED ?
		__print_flags(__entry->status, "|", aer_correctable_errors) :
		__print_flags(__entry->status, "|", aer_uncorrectable_errors))
);

#endif /* _TRACE_AER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
