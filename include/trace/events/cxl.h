/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cxl

#if !defined(_CXL_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CXL_EVENTS_H

#include <linux/tracepoint.h>

#define CXL_HEADERLOG_SIZE		SZ_512
#define CXL_HEADERLOG_SIZE_U32		SZ_512 / sizeof(u32)

#define CXL_RAS_UC_CACHE_DATA_PARITY	BIT(0)
#define CXL_RAS_UC_CACHE_ADDR_PARITY	BIT(1)
#define CXL_RAS_UC_CACHE_BE_PARITY	BIT(2)
#define CXL_RAS_UC_CACHE_DATA_ECC	BIT(3)
#define CXL_RAS_UC_MEM_DATA_PARITY	BIT(4)
#define CXL_RAS_UC_MEM_ADDR_PARITY	BIT(5)
#define CXL_RAS_UC_MEM_BE_PARITY	BIT(6)
#define CXL_RAS_UC_MEM_DATA_ECC		BIT(7)
#define CXL_RAS_UC_REINIT_THRESH	BIT(8)
#define CXL_RAS_UC_RSVD_ENCODE		BIT(9)
#define CXL_RAS_UC_POISON		BIT(10)
#define CXL_RAS_UC_RECV_OVERFLOW	BIT(11)
#define CXL_RAS_UC_INTERNAL_ERR		BIT(14)
#define CXL_RAS_UC_IDE_TX_ERR		BIT(15)
#define CXL_RAS_UC_IDE_RX_ERR		BIT(16)

#define show_uc_errs(status)	__print_flags(status, " | ",		  \
	{ CXL_RAS_UC_CACHE_DATA_PARITY, "Cache Data Parity Error" },	  \
	{ CXL_RAS_UC_CACHE_ADDR_PARITY, "Cache Address Parity Error" },	  \
	{ CXL_RAS_UC_CACHE_BE_PARITY, "Cache Byte Enable Parity Error" }, \
	{ CXL_RAS_UC_CACHE_DATA_ECC, "Cache Data ECC Error" },		  \
	{ CXL_RAS_UC_MEM_DATA_PARITY, "Memory Data Parity Error" },	  \
	{ CXL_RAS_UC_MEM_ADDR_PARITY, "Memory Address Parity Error" },	  \
	{ CXL_RAS_UC_MEM_BE_PARITY, "Memory Byte Enable Parity Error" },  \
	{ CXL_RAS_UC_MEM_DATA_ECC, "Memory Data ECC Error" },		  \
	{ CXL_RAS_UC_REINIT_THRESH, "REINIT Threshold Hit" },		  \
	{ CXL_RAS_UC_RSVD_ENCODE, "Received Unrecognized Encoding" },	  \
	{ CXL_RAS_UC_POISON, "Received Poison From Peer" },		  \
	{ CXL_RAS_UC_RECV_OVERFLOW, "Receiver Overflow" },		  \
	{ CXL_RAS_UC_INTERNAL_ERR, "Component Specific Error" },	  \
	{ CXL_RAS_UC_IDE_TX_ERR, "IDE Tx Error" },			  \
	{ CXL_RAS_UC_IDE_RX_ERR, "IDE Rx Error" }			  \
)

TRACE_EVENT(cxl_aer_uncorrectable_error,
	TP_PROTO(const struct device *dev, u32 status, u32 fe, u32 *hl),
	TP_ARGS(dev, status, fe, hl),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u32, status)
		__field(u32, first_error)
		__array(u32, header_log, CXL_HEADERLOG_SIZE_U32)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->status = status;
		__entry->first_error = fe;
		/*
		 * Embed the 512B headerlog data for user app retrieval and
		 * parsing, but no need to print this in the trace buffer.
		 */
		memcpy(__entry->header_log, hl, CXL_HEADERLOG_SIZE);
	),
	TP_printk("%s: status: '%s' first_error: '%s'",
		  __get_str(dev_name),
		  show_uc_errs(__entry->status),
		  show_uc_errs(__entry->first_error)
	)
);

#define CXL_RAS_CE_CACHE_DATA_ECC	BIT(0)
#define CXL_RAS_CE_MEM_DATA_ECC		BIT(1)
#define CXL_RAS_CE_CRC_THRESH		BIT(2)
#define CLX_RAS_CE_RETRY_THRESH		BIT(3)
#define CXL_RAS_CE_CACHE_POISON		BIT(4)
#define CXL_RAS_CE_MEM_POISON		BIT(5)
#define CXL_RAS_CE_PHYS_LAYER_ERR	BIT(6)

#define show_ce_errs(status)	__print_flags(status, " | ",			\
	{ CXL_RAS_CE_CACHE_DATA_ECC, "Cache Data ECC Error" },			\
	{ CXL_RAS_CE_MEM_DATA_ECC, "Memory Data ECC Error" },			\
	{ CXL_RAS_CE_CRC_THRESH, "CRC Threshold Hit" },				\
	{ CLX_RAS_CE_RETRY_THRESH, "Retry Threshold" },				\
	{ CXL_RAS_CE_CACHE_POISON, "Received Cache Poison From Peer" },		\
	{ CXL_RAS_CE_MEM_POISON, "Received Memory Poison From Peer" },		\
	{ CXL_RAS_CE_PHYS_LAYER_ERR, "Received Error From Physical Layer" }	\
)

TRACE_EVENT(cxl_aer_correctable_error,
	TP_PROTO(const struct device *dev, u32 status),
	TP_ARGS(dev, status),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u32, status)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->status = status;
	),
	TP_printk("%s: status: '%s'",
		  __get_str(dev_name), show_ce_errs(__entry->status)
	)
);

#endif /* _CXL_EVENTS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cxl
#include <trace/define_trace.h>
