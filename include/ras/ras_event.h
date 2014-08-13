#undef TRACE_SYSTEM
#define TRACE_SYSTEM ras
#define TRACE_INCLUDE_FILE ras_event

#if !defined(_TRACE_HW_EVENT_MC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HW_EVENT_MC_H

#include <linux/tracepoint.h>
#include <linux/edac.h>
#include <linux/ktime.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/cper.h>

/*
 * MCE Extended Error Log trace event
 *
 * These events are generated when hardware detects a corrected or
 * uncorrected event.
 */

/* memory trace event */

#if defined(CONFIG_ACPI_EXTLOG) || defined(CONFIG_ACPI_EXTLOG_MODULE)
TRACE_EVENT(extlog_mem_event,
	TP_PROTO(struct cper_sec_mem_err *mem,
		 u32 err_seq,
		 const uuid_le *fru_id,
		 const char *fru_text,
		 u8 sev),

	TP_ARGS(mem, err_seq, fru_id, fru_text, sev),

	TP_STRUCT__entry(
		__field(u32, err_seq)
		__field(u8, etype)
		__field(u8, sev)
		__field(u64, pa)
		__field(u8, pa_mask_lsb)
		__field_struct(uuid_le, fru_id)
		__string(fru_text, fru_text)
		__field_struct(struct cper_mem_err_compact, data)
	),

	TP_fast_assign(
		__entry->err_seq = err_seq;
		if (mem->validation_bits & CPER_MEM_VALID_ERROR_TYPE)
			__entry->etype = mem->error_type;
		else
			__entry->etype = ~0;
		__entry->sev = sev;
		if (mem->validation_bits & CPER_MEM_VALID_PA)
			__entry->pa = mem->physical_addr;
		else
			__entry->pa = ~0ull;

		if (mem->validation_bits & CPER_MEM_VALID_PA_MASK)
			__entry->pa_mask_lsb = (u8)__ffs64(mem->physical_addr_mask);
		else
			__entry->pa_mask_lsb = ~0;
		__entry->fru_id = *fru_id;
		__assign_str(fru_text, fru_text);
		cper_mem_err_pack(mem, &__entry->data);
	),

	TP_printk("{%d} %s error: %s physical addr: %016llx (mask lsb: %x) %sFRU: %pUl %.20s",
		  __entry->err_seq,
		  cper_severity_str(__entry->sev),
		  cper_mem_err_type_str(__entry->etype),
		  __entry->pa,
		  __entry->pa_mask_lsb,
		  cper_mem_err_unpack(p, &__entry->data),
		  &__entry->fru_id,
		  __get_str(fru_text))
);
#endif

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
		  mc_event_error_type(__entry->error_type),
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
	{PCI_ERR_COR_RCVR,	"Receiver Error"},	\
	{PCI_ERR_COR_BAD_TLP,	"Bad TLP"},		\
	{PCI_ERR_COR_BAD_DLLP,	"Bad DLLP"},		\
	{PCI_ERR_COR_REP_ROLL,	"RELAY_NUM Rollover"},	\
	{PCI_ERR_COR_REP_TIMER,	"Replay Timer Timeout"},\
	{PCI_ERR_COR_ADV_NFAT,	"Advisory Non-Fatal"}

#define aer_uncorrectable_errors		\
	{PCI_ERR_UNC_DLP,	"Data Link Protocol"},		\
	{PCI_ERR_UNC_POISON_TLP,"Poisoned TLP"},		\
	{PCI_ERR_UNC_FCP,	"Flow Control Protocol"},	\
	{PCI_ERR_UNC_COMP_TIME,	"Completion Timeout"},		\
	{PCI_ERR_UNC_COMP_ABORT,"Completer Abort"},		\
	{PCI_ERR_UNC_UNX_COMP,	"Unexpected Completion"},	\
	{PCI_ERR_UNC_RX_OVER,	"Receiver Overflow"},		\
	{PCI_ERR_UNC_MALF_TLP,	"Malformed TLP"},		\
	{PCI_ERR_UNC_ECRC,	"ECRC"},			\
	{PCI_ERR_UNC_UNSUP,	"Unsupported Request"}

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
		__entry->severity == AER_CORRECTABLE ? "Corrected" :
			__entry->severity == AER_FATAL ?
			"Fatal" : "Uncorrected, non-fatal",
		__entry->severity == AER_CORRECTABLE ?
		__print_flags(__entry->status, "|", aer_correctable_errors) :
		__print_flags(__entry->status, "|", aer_uncorrectable_errors))
);

#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
