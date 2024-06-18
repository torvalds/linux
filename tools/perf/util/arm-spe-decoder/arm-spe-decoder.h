/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arm_spe_decoder.h: Arm Statistical Profiling Extensions support
 * Copyright (c) 2019-2020, Arm Ltd.
 */

#ifndef INCLUDE__ARM_SPE_DECODER_H__
#define INCLUDE__ARM_SPE_DECODER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arm-spe-pkt-decoder.h"

enum arm_spe_sample_type {
	ARM_SPE_L1D_ACCESS		= 1 << 0,
	ARM_SPE_L1D_MISS		= 1 << 1,
	ARM_SPE_LLC_ACCESS		= 1 << 2,
	ARM_SPE_LLC_MISS		= 1 << 3,
	ARM_SPE_TLB_ACCESS		= 1 << 4,
	ARM_SPE_TLB_MISS		= 1 << 5,
	ARM_SPE_BRANCH_MISS		= 1 << 6,
	ARM_SPE_REMOTE_ACCESS		= 1 << 7,
	ARM_SPE_SVE_PARTIAL_PRED	= 1 << 8,
	ARM_SPE_SVE_EMPTY_PRED		= 1 << 9,
};

enum arm_spe_op_type {
	/* First level operation type */
	ARM_SPE_OP_OTHER	= 1 << 0,
	ARM_SPE_OP_LDST		= 1 << 1,
	ARM_SPE_OP_BRANCH_ERET	= 1 << 2,

	/* Second level operation type for OTHER */
	ARM_SPE_OP_SVE_OTHER		= 1 << 16,
	ARM_SPE_OP_SVE_FP		= 1 << 17,
	ARM_SPE_OP_SVE_PRED_OTHER	= 1 << 18,

	/* Second level operation type for LDST */
	ARM_SPE_OP_LD			= 1 << 16,
	ARM_SPE_OP_ST			= 1 << 17,
	ARM_SPE_OP_ATOMIC		= 1 << 18,
	ARM_SPE_OP_EXCL			= 1 << 19,
	ARM_SPE_OP_AR			= 1 << 20,
	ARM_SPE_OP_SIMD_FP		= 1 << 21,
	ARM_SPE_OP_GP_REG		= 1 << 22,
	ARM_SPE_OP_UNSPEC_REG		= 1 << 23,
	ARM_SPE_OP_NV_SYSREG		= 1 << 24,
	ARM_SPE_OP_SVE_LDST		= 1 << 25,
	ARM_SPE_OP_SVE_PRED_LDST	= 1 << 26,
	ARM_SPE_OP_SVE_SG		= 1 << 27,

	/* Second level operation type for BRANCH_ERET */
	ARM_SPE_OP_BR_COND	= 1 << 16,
	ARM_SPE_OP_BR_INDIRECT	= 1 << 17,
};

enum arm_spe_neoverse_data_source {
	ARM_SPE_NV_L1D		 = 0x0,
	ARM_SPE_NV_L2		 = 0x8,
	ARM_SPE_NV_PEER_CORE	 = 0x9,
	ARM_SPE_NV_LOCAL_CLUSTER = 0xa,
	ARM_SPE_NV_SYS_CACHE	 = 0xb,
	ARM_SPE_NV_PEER_CLUSTER	 = 0xc,
	ARM_SPE_NV_REMOTE	 = 0xd,
	ARM_SPE_NV_DRAM		 = 0xe,
};

struct arm_spe_record {
	enum arm_spe_sample_type type;
	int err;
	u32 op;
	u32 latency;
	u64 from_ip;
	u64 to_ip;
	u64 timestamp;
	u64 virt_addr;
	u64 phys_addr;
	u64 context_id;
	u16 source;
};

struct arm_spe_insn;

struct arm_spe_buffer {
	const unsigned char *buf;
	size_t len;
	u64 offset;
	u64 trace_nr;
};

struct arm_spe_params {
	int (*get_trace)(struct arm_spe_buffer *buffer, void *data);
	void *data;
};

struct arm_spe_decoder {
	int (*get_trace)(struct arm_spe_buffer *buffer, void *data);
	void *data;
	struct arm_spe_record record;

	const unsigned char *buf;
	size_t len;

	struct arm_spe_pkt packet;
};

struct arm_spe_decoder *arm_spe_decoder_new(struct arm_spe_params *params);
void arm_spe_decoder_free(struct arm_spe_decoder *decoder);

int arm_spe_decode(struct arm_spe_decoder *decoder);

#endif
