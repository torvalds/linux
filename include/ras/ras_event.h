/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/mm.h>

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
		 const guid_t *fru_id,
		 const char *fru_text,
		 u8 sev),

	TP_ARGS(mem, err_seq, fru_id, fru_text, sev),

	TP_STRUCT__entry(
		__field(u32, err_seq)
		__field(u8, etype)
		__field(u8, sev)
		__field(u64, pa)
		__field(u8, pa_mask_lsb)
		__field_struct(guid_t, fru_id)
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
		  __get_str(msg)[0] ? " " : "",
		  __get_str(msg),
		  __get_str(label),
		  __entry->mc_index,
		  __entry->top_layer,
		  __entry->middle_layer,
		  __entry->lower_layer,
		  __entry->address,
		  1 << __entry->grain_bits,
		  __entry->syndrome,
		  __get_str(driver_detail)[0] ? " " : "",
		  __get_str(driver_detail))
);

/*
 * ARM Processor Events Report
 *
 * This event is generated when hardware detects an ARM processor error
 * has occurred. UEFI 2.6 spec section N.2.4.4.
 */
TRACE_EVENT(arm_event,

	TP_PROTO(const struct cper_sec_proc_arm *proc),

	TP_ARGS(proc),

	TP_STRUCT__entry(
		__field(u64, mpidr)
		__field(u64, midr)
		__field(u32, running_state)
		__field(u32, psci_state)
		__field(u8, affinity)
	),

	TP_fast_assign(
		if (proc->validation_bits & CPER_ARM_VALID_AFFINITY_LEVEL)
			__entry->affinity = proc->affinity_level;
		else
			__entry->affinity = ~0;
		if (proc->validation_bits & CPER_ARM_VALID_MPIDR)
			__entry->mpidr = proc->mpidr;
		else
			__entry->mpidr = 0ULL;
		__entry->midr = proc->midr;
		if (proc->validation_bits & CPER_ARM_VALID_RUNNING_STATE) {
			__entry->running_state = proc->running_state;
			__entry->psci_state = proc->psci_state;
		} else {
			__entry->running_state = ~0;
			__entry->psci_state = ~0;
		}
	),

	TP_printk("affinity level: %d; MPIDR: %016llx; MIDR: %016llx; "
		  "running state: %d; PSCI state: %d",
		  __entry->affinity, __entry->mpidr, __entry->midr,
		  __entry->running_state, __entry->psci_state)
);

/*
 * Non-Standard Section Report
 *
 * This event is generated when hardware detected a hardware
 * error event, which may be of non-standard section as defined
 * in UEFI spec appendix "Common Platform Error Record", or may
 * be of sections for which TRACE_EVENT is not defined.
 *
 */
TRACE_EVENT(non_standard_event,

	TP_PROTO(const guid_t *sec_type,
		 const guid_t *fru_id,
		 const char *fru_text,
		 const u8 sev,
		 const u8 *err,
		 const u32 len),

	TP_ARGS(sec_type, fru_id, fru_text, sev, err, len),

	TP_STRUCT__entry(
		__array(char, sec_type, UUID_SIZE)
		__array(char, fru_id, UUID_SIZE)
		__string(fru_text, fru_text)
		__field(u8, sev)
		__field(u32, len)
		__dynamic_array(u8, buf, len)
	),

	TP_fast_assign(
		memcpy(__entry->sec_type, sec_type, UUID_SIZE);
		memcpy(__entry->fru_id, fru_id, UUID_SIZE);
		__assign_str(fru_text, fru_text);
		__entry->sev = sev;
		__entry->len = len;
		memcpy(__get_dynamic_array(buf), err, len);
	),

	TP_printk("severity: %d; sec type:%pU; FRU: %pU %s; data len:%d; raw data:%s",
		  __entry->sev, __entry->sec_type,
		  __entry->fru_id, __get_str(fru_text),
		  __entry->len,
		  __print_hex(__get_dynamic_array(buf), __entry->len))
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

#define aer_correctable_errors					\
	{PCI_ERR_COR_RCVR,	"Receiver Error"},		\
	{PCI_ERR_COR_BAD_TLP,	"Bad TLP"},			\
	{PCI_ERR_COR_BAD_DLLP,	"Bad DLLP"},			\
	{PCI_ERR_COR_REP_ROLL,	"RELAY_NUM Rollover"},		\
	{PCI_ERR_COR_REP_TIMER,	"Replay Timer Timeout"},	\
	{PCI_ERR_COR_ADV_NFAT,	"Advisory Non-Fatal Error"},	\
	{PCI_ERR_COR_INTERNAL,	"Corrected Internal Error"},	\
	{PCI_ERR_COR_LOG_OVER,	"Header Log Overflow"}

#define aer_uncorrectable_errors				\
	{PCI_ERR_UNC_UND,	"Undefined"},			\
	{PCI_ERR_UNC_DLP,	"Data Link Protocol Error"},	\
	{PCI_ERR_UNC_SURPDN,	"Surprise Down Error"},		\
	{PCI_ERR_UNC_POISON_TLP,"Poisoned TLP"},		\
	{PCI_ERR_UNC_FCP,	"Flow Control Protocol Error"},	\
	{PCI_ERR_UNC_COMP_TIME,	"Completion Timeout"},		\
	{PCI_ERR_UNC_COMP_ABORT,"Completer Abort"},		\
	{PCI_ERR_UNC_UNX_COMP,	"Unexpected Completion"},	\
	{PCI_ERR_UNC_RX_OVER,	"Receiver Overflow"},		\
	{PCI_ERR_UNC_MALF_TLP,	"Malformed TLP"},		\
	{PCI_ERR_UNC_ECRC,	"ECRC Error"},			\
	{PCI_ERR_UNC_UNSUP,	"Unsupported Request Error"},	\
	{PCI_ERR_UNC_ACSV,	"ACS Violation"},		\
	{PCI_ERR_UNC_INTN,	"Uncorrectable Internal Error"},\
	{PCI_ERR_UNC_MCBTLP,	"MC Blocked TLP"},		\
	{PCI_ERR_UNC_ATOMEG,	"AtomicOp Egress Blocked"},	\
	{PCI_ERR_UNC_TLPPRE,	"TLP Prefix Blocked Error"}

TRACE_EVENT(aer_event,
	TP_PROTO(const char *dev_name,
		 const u32 status,
		 const u8 severity,
		 const u8 tlp_header_valid,
		 struct aer_header_log_regs *tlp),

	TP_ARGS(dev_name, status, severity, tlp_header_valid, tlp),

	TP_STRUCT__entry(
		__string(	dev_name,	dev_name	)
		__field(	u32,		status		)
		__field(	u8,		severity	)
		__field(	u8, 		tlp_header_valid)
		__array(	u32, 		tlp_header, 4	)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->status		= status;
		__entry->severity	= severity;
		__entry->tlp_header_valid = tlp_header_valid;
		if (tlp_header_valid) {
			__entry->tlp_header[0] = tlp->dw0;
			__entry->tlp_header[1] = tlp->dw1;
			__entry->tlp_header[2] = tlp->dw2;
			__entry->tlp_header[3] = tlp->dw3;
		}
	),

	TP_printk("%s PCIe Bus Error: severity=%s, %s, TLP Header=%s\n",
		__get_str(dev_name),
		__entry->severity == AER_CORRECTABLE ? "Corrected" :
			__entry->severity == AER_FATAL ?
			"Fatal" : "Uncorrected, non-fatal",
		__entry->severity == AER_CORRECTABLE ?
		__print_flags(__entry->status, "|", aer_correctable_errors) :
		__print_flags(__entry->status, "|", aer_uncorrectable_errors),
		__entry->tlp_header_valid ?
			__print_array(__entry->tlp_header, 4, 4) :
			"Not available")
);

/*
 * memory-failure recovery action result event
 *
 * unsigned long pfn -	Page Frame Number of the corrupted page
 * int type	-	Page types of the corrupted page
 * int result	-	Result of recovery action
 */

#ifdef CONFIG_MEMORY_FAILURE
#define MF_ACTION_RESULT	\
	EM ( MF_IGNORED, "Ignored" )	\
	EM ( MF_FAILED,  "Failed" )	\
	EM ( MF_DELAYED, "Delayed" )	\
	EMe ( MF_RECOVERED, "Recovered" )

#define MF_PAGE_TYPE		\
	EM ( MF_MSG_KERNEL, "reserved kernel page" )			\
	EM ( MF_MSG_KERNEL_HIGH_ORDER, "high-order kernel page" )	\
	EM ( MF_MSG_SLAB, "kernel slab page" )				\
	EM ( MF_MSG_DIFFERENT_COMPOUND, "different compound page after locking" ) \
	EM ( MF_MSG_HUGE, "huge page" )					\
	EM ( MF_MSG_FREE_HUGE, "free huge page" )			\
	EM ( MF_MSG_UNMAP_FAILED, "unmapping failed page" )		\
	EM ( MF_MSG_DIRTY_SWAPCACHE, "dirty swapcache page" )		\
	EM ( MF_MSG_CLEAN_SWAPCACHE, "clean swapcache page" )		\
	EM ( MF_MSG_DIRTY_MLOCKED_LRU, "dirty mlocked LRU page" )	\
	EM ( MF_MSG_CLEAN_MLOCKED_LRU, "clean mlocked LRU page" )	\
	EM ( MF_MSG_DIRTY_UNEVICTABLE_LRU, "dirty unevictable LRU page" )	\
	EM ( MF_MSG_CLEAN_UNEVICTABLE_LRU, "clean unevictable LRU page" )	\
	EM ( MF_MSG_DIRTY_LRU, "dirty LRU page" )			\
	EM ( MF_MSG_CLEAN_LRU, "clean LRU page" )			\
	EM ( MF_MSG_TRUNCATED_LRU, "already truncated LRU page" )	\
	EM ( MF_MSG_BUDDY, "free buddy page" )				\
	EM ( MF_MSG_DAX, "dax page" )					\
	EM ( MF_MSG_UNSPLIT_THP, "unsplit thp" )			\
	EMe ( MF_MSG_UNKNOWN, "unknown page" )

/*
 * First define the enums in MM_ACTION_RESULT to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

MF_ACTION_RESULT
MF_PAGE_TYPE

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)		{ a, b },
#define EMe(a, b)	{ a, b }

TRACE_EVENT(memory_failure_event,
	TP_PROTO(unsigned long pfn,
		 int type,
		 int result),

	TP_ARGS(pfn, type, result),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(int, type)
		__field(int, result)
	),

	TP_fast_assign(
		__entry->pfn	= pfn;
		__entry->type	= type;
		__entry->result	= result;
	),

	TP_printk("pfn %#lx: recovery action for %s: %s",
		__entry->pfn,
		__print_symbolic(__entry->type, MF_PAGE_TYPE),
		__print_symbolic(__entry->result, MF_ACTION_RESULT)
	)
);
#endif /* CONFIG_MEMORY_FAILURE */
#endif /* _TRACE_HW_EVENT_MC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
