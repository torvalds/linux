/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel IOMMU trace support
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_iommu

#if !defined(_TRACE_INTEL_IOMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTEL_IOMMU_H

#include <linux/tracepoint.h>
#include <linux/intel-iommu.h>

TRACE_EVENT(qi_submit,
	TP_PROTO(struct intel_iommu *iommu, u64 qw0, u64 qw1, u64 qw2, u64 qw3),

	TP_ARGS(iommu, qw0, qw1, qw2, qw3),

	TP_STRUCT__entry(
		__field(u64, qw0)
		__field(u64, qw1)
		__field(u64, qw2)
		__field(u64, qw3)
		__string(iommu, iommu->name)
	),

	TP_fast_assign(
		__assign_str(iommu, iommu->name);
		__entry->qw0 = qw0;
		__entry->qw1 = qw1;
		__entry->qw2 = qw2;
		__entry->qw3 = qw3;
	),

	TP_printk("%s %s: 0x%llx 0x%llx 0x%llx 0x%llx",
		  __print_symbolic(__entry->qw0 & 0xf,
				   { QI_CC_TYPE,	"cc_inv" },
				   { QI_IOTLB_TYPE,	"iotlb_inv" },
				   { QI_DIOTLB_TYPE,	"dev_tlb_inv" },
				   { QI_IEC_TYPE,	"iec_inv" },
				   { QI_IWD_TYPE,	"inv_wait" },
				   { QI_EIOTLB_TYPE,	"p_iotlb_inv" },
				   { QI_PC_TYPE,	"pc_inv" },
				   { QI_DEIOTLB_TYPE,	"p_dev_tlb_inv" },
				   { QI_PGRP_RESP_TYPE,	"page_grp_resp" }),
		__get_str(iommu),
		__entry->qw0, __entry->qw1, __entry->qw2, __entry->qw3
	)
);
#endif /* _TRACE_INTEL_IOMMU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
