/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Type definitions for the Microsoft Hypervisor.
 */
#ifndef _HV_HVGDK_EXT_H
#define _HV_HVGDK_EXT_H

#include "hvgdk_mini.h"

/* Extended hypercalls */
#define HV_EXT_CALL_QUERY_CAPABILITIES		0x8001
#define HV_EXT_CALL_MEMORY_HEAT_HINT		0x8003

/* Extended hypercalls */
enum {		/* HV_EXT_CALL */
	HV_EXTCALL_QUERY_CAPABILITIES = 0x8001,
	HV_EXTCALL_MEMORY_HEAT_HINT   = 0x8003,
};

/* HV_EXT_OUTPUT_QUERY_CAPABILITIES */
#define HV_EXT_CAPABILITY_MEMORY_COLD_DISCARD_HINT BIT(8)

enum {		/* HV_EXT_MEMORY_HEAT_HINT_TYPE */
	HV_EXTMEM_HEAT_HINT_COLD = 0,
	HV_EXTMEM_HEAT_HINT_HOT = 1,
	HV_EXTMEM_HEAT_HINT_COLD_DISCARD = 2,
	HV_EXTMEM_HEAT_HINT_MAX
};

/*
 * The whole argument should fit in a page to be able to pass to the hypervisor
 * in one hypercall.
 */
#define HV_MEMORY_HINT_MAX_GPA_PAGE_RANGES  \
	((HV_HYP_PAGE_SIZE - sizeof(struct hv_memory_hint)) / \
		sizeof(union hv_gpa_page_range))

/* HvExtCallMemoryHeatHint hypercall */
#define HV_EXT_MEMORY_HEAT_HINT_TYPE_COLD_DISCARD	2
struct hv_memory_hint {		/* HV_EXT_INPUT_MEMORY_HEAT_HINT */
	u64 heat_type : 2;	/* HV_EXTMEM_HEAT_HINT_* */
	u64 reserved : 62;
	union hv_gpa_page_range ranges[];
} __packed;

#endif /* _HV_HVGDK_EXT_H */
