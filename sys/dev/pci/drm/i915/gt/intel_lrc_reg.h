/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _INTEL_LRC_REG_H_
#define _INTEL_LRC_REG_H_

#include <linux/types.h>

#define CTX_DESC_FORCE_RESTORE BIT_ULL(2)

/* GEN8 to GEN12 Reg State Context */
#define CTX_CONTEXT_CONTROL		(0x02 + 1)
#define CTX_RING_HEAD			(0x04 + 1)
#define CTX_RING_TAIL			(0x06 + 1)
#define CTX_RING_START			(0x08 + 1)
#define CTX_RING_CTL			(0x0a + 1)
#define CTX_BB_STATE			(0x10 + 1)
#define CTX_TIMESTAMP			(0x22 + 1)
#define CTX_PDP3_UDW			(0x24 + 1)
#define CTX_PDP3_LDW			(0x26 + 1)
#define CTX_PDP2_UDW			(0x28 + 1)
#define CTX_PDP2_LDW			(0x2a + 1)
#define CTX_PDP1_UDW			(0x2c + 1)
#define CTX_PDP1_LDW			(0x2e + 1)
#define CTX_PDP0_UDW			(0x30 + 1)
#define CTX_PDP0_LDW			(0x32 + 1)
#define CTX_R_PWR_CLK_STATE		(0x42 + 1)

#define GEN9_CTX_RING_MI_MODE		0x54

#define ASSIGN_CTX_PDP(ppgtt, reg_state, n) do { \
	u32 *reg_state__ = (reg_state); \
	const u64 addr__ = i915_page_dir_dma_addr((ppgtt), (n)); \
	(reg_state__)[CTX_PDP ## n ## _UDW] = upper_32_bits(addr__); \
	(reg_state__)[CTX_PDP ## n ## _LDW] = lower_32_bits(addr__); \
} while (0)

#define ASSIGN_CTX_PML4(ppgtt, reg_state) do { \
	u32 *reg_state__ = (reg_state); \
	const u64 addr__ = px_dma((ppgtt)->pd); \
	(reg_state__)[CTX_PDP0_UDW] = upper_32_bits(addr__); \
	(reg_state__)[CTX_PDP0_LDW] = lower_32_bits(addr__); \
} while (0)

#define GEN8_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x17
#define GEN9_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x26
#define GEN10_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x19
#define GEN11_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x1A
#define GEN12_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0xD

#define GEN8_EXECLISTS_STATUS_BUF 0x370
#define GEN11_EXECLISTS_STATUS_BUF2 0x3c0

/*
 * The docs specify that the write pointer wraps around after 5h, "After status
 * is written out to the last available status QW at offset 5h, this pointer
 * wraps to 0."
 *
 * Therefore, one must infer than even though there are 3 bits available, 6 and
 * 7 appear to be * reserved.
 */
#define GEN8_CSB_ENTRIES 6
#define GEN8_CSB_PTR_MASK 0x7
#define GEN8_CSB_READ_PTR_MASK	(GEN8_CSB_PTR_MASK << 8)
#define GEN8_CSB_WRITE_PTR_MASK	(GEN8_CSB_PTR_MASK << 0)

#define GEN11_CSB_ENTRIES 12
#define GEN11_CSB_PTR_MASK 0xf
#define GEN11_CSB_READ_PTR_MASK		(GEN11_CSB_PTR_MASK << 8)
#define GEN11_CSB_WRITE_PTR_MASK	(GEN11_CSB_PTR_MASK << 0)

#define MAX_CONTEXT_HW_ID	(1 << 21) /* exclusive */
#define GEN11_MAX_CONTEXT_HW_ID	(1 << 11) /* exclusive */
/* in Gen12 ID 0x7FF is reserved to indicate idle */
#define GEN12_MAX_CONTEXT_HW_ID	(GEN11_MAX_CONTEXT_HW_ID - 1)
/* in Xe_HP ID 0xFFFF is reserved to indicate "invalid context" */
#define XEHP_MAX_CONTEXT_HW_ID	0xFFFF

#endif /* _INTEL_LRC_REG_H_ */
