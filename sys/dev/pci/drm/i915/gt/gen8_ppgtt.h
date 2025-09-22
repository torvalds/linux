/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __GEN8_PPGTT_H__
#define __GEN8_PPGTT_H__

#include <linux/kernel.h>

struct i915_address_space;
struct intel_gt;

struct i915_ppgtt *gen8_ppgtt_create(struct intel_gt *gt,
				     unsigned long lmem_pt_obj_flags);

u64 gen8_ggtt_pte_encode(dma_addr_t addr,
			 unsigned int pat_index,
			 u32 flags);

#endif
