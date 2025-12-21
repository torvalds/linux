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

#define ARM_SPE_L1D_ACCESS		BIT(EV_L1D_ACCESS)
#define ARM_SPE_L1D_MISS		BIT(EV_L1D_REFILL)
#define ARM_SPE_LLC_ACCESS		BIT(EV_LLC_ACCESS)
#define ARM_SPE_LLC_MISS		BIT(EV_LLC_MISS)
#define ARM_SPE_TLB_ACCESS		BIT(EV_TLB_ACCESS)
#define ARM_SPE_TLB_MISS		BIT(EV_TLB_WALK)
#define ARM_SPE_BRANCH_MISS		BIT(EV_MISPRED)
#define ARM_SPE_BRANCH_NOT_TAKEN	BIT(EV_NOT_TAKEN)
#define ARM_SPE_REMOTE_ACCESS		BIT(EV_REMOTE_ACCESS)
#define ARM_SPE_SVE_PARTIAL_PRED	BIT(EV_PARTIAL_PREDICATE)
#define ARM_SPE_SVE_EMPTY_PRED		BIT(EV_EMPTY_PREDICATE)
#define ARM_SPE_IN_TXN			BIT(EV_TRANSACTIONAL)
#define ARM_SPE_L2D_ACCESS		BIT(EV_L2D_ACCESS)
#define ARM_SPE_L2D_MISS		BIT(EV_L2D_MISS)
#define ARM_SPE_RECENTLY_FETCHED	BIT(EV_RECENTLY_FETCHED)
#define ARM_SPE_DATA_SNOOPED		BIT(EV_DATA_SNOOPED)
#define ARM_SPE_HITM			BIT(EV_CACHE_DATA_MODIFIED)

enum arm_spe_op_type {
	/* First level operation type */
	ARM_SPE_OP_OTHER	= 1 << 0,
	ARM_SPE_OP_LDST		= 1 << 1,
	ARM_SPE_OP_BRANCH_ERET	= 1 << 2,
};

enum arm_spe_2nd_op_ldst {
	ARM_SPE_OP_GP_REG		= 1 << 8,
	ARM_SPE_OP_UNSPEC_REG		= 1 << 9,
	ARM_SPE_OP_NV_SYSREG		= 1 << 10,
	ARM_SPE_OP_SIMD_FP		= 1 << 11,
	ARM_SPE_OP_SVE			= 1 << 12,
	ARM_SPE_OP_MTE_TAG		= 1 << 13,
	ARM_SPE_OP_MEMCPY		= 1 << 14,
	ARM_SPE_OP_MEMSET		= 1 << 15,
	ARM_SPE_OP_GCS			= 1 << 16,
	ARM_SPE_OP_SME			= 1 << 17,
	ARM_SPE_OP_ASE			= 1 << 18,

	/* Assisted information for memory / SIMD */
	ARM_SPE_OP_LD			= 1 << 20,
	ARM_SPE_OP_ST			= 1 << 21,
	ARM_SPE_OP_ATOMIC		= 1 << 22,
	ARM_SPE_OP_EXCL			= 1 << 23,
	ARM_SPE_OP_AR			= 1 << 24,
	ARM_SPE_OP_DP			= 1 << 25,	/* Data processing */
	ARM_SPE_OP_PRED			= 1 << 26,	/* Predicated */
	ARM_SPE_OP_SG			= 1 << 27,	/* Gather/Scatter */
	ARM_SPE_OP_COMM			= 1 << 28,	/* Common */
	ARM_SPE_OP_FP			= 1 << 29,	/* Floating-point */
	ARM_SPE_OP_COND			= 1 << 30,	/* Conditional */
};

enum arm_spe_2nd_op_branch {
	ARM_SPE_OP_BR_COND		= 1 << 8,
	ARM_SPE_OP_BR_INDIRECT		= 1 << 9,
	ARM_SPE_OP_BR_GCS		= 1 << 10,
	ARM_SPE_OP_BR_CR_BL		= 1 << 11,
	ARM_SPE_OP_BR_CR_RET		= 1 << 12,
	ARM_SPE_OP_BR_CR_NON_BL_RET	= 1 << 13,
};

enum arm_spe_common_data_source {
	ARM_SPE_COMMON_DS_L1D		= 0x0,
	ARM_SPE_COMMON_DS_L2		= 0x8,
	ARM_SPE_COMMON_DS_PEER_CORE	= 0x9,
	ARM_SPE_COMMON_DS_LOCAL_CLUSTER = 0xa,
	ARM_SPE_COMMON_DS_SYS_CACHE	= 0xb,
	ARM_SPE_COMMON_DS_PEER_CLUSTER	= 0xc,
	ARM_SPE_COMMON_DS_REMOTE	= 0xd,
	ARM_SPE_COMMON_DS_DRAM		= 0xe,
};

enum arm_spe_ampereone_data_source {
	ARM_SPE_AMPEREONE_LOCAL_CHIP_CACHE_OR_DEVICE    = 0x0,
	ARM_SPE_AMPEREONE_SLC                           = 0x3,
	ARM_SPE_AMPEREONE_REMOTE_CHIP_CACHE             = 0x5,
	ARM_SPE_AMPEREONE_DDR                           = 0x7,
	ARM_SPE_AMPEREONE_L1D                           = 0x8,
	ARM_SPE_AMPEREONE_L2D                           = 0x9,
};

enum arm_spe_hisi_hip_data_source {
	ARM_SPE_HISI_HIP_PEER_CPU		= 0,
	ARM_SPE_HISI_HIP_PEER_CPU_HITM		= 1,
	ARM_SPE_HISI_HIP_L3			= 2,
	ARM_SPE_HISI_HIP_L3_HITM		= 3,
	ARM_SPE_HISI_HIP_PEER_CLUSTER		= 4,
	ARM_SPE_HISI_HIP_PEER_CLUSTER_HITM	= 5,
	ARM_SPE_HISI_HIP_REMOTE_SOCKET		= 6,
	ARM_SPE_HISI_HIP_REMOTE_SOCKET_HITM	= 7,
	ARM_SPE_HISI_HIP_LOCAL_MEM		= 8,
	ARM_SPE_HISI_HIP_REMOTE_MEM		= 9,
	ARM_SPE_HISI_HIP_NC_DEV			= 13,
	ARM_SPE_HISI_HIP_L2			= 16,
	ARM_SPE_HISI_HIP_L2_HITM		= 17,
	ARM_SPE_HISI_HIP_L1			= 18,
};

struct arm_spe_record {
	u64 type;
	int err;
	u32 op;
	u32 latency;
	u64 from_ip;
	u64 to_ip;
	u64 prev_br_tgt;
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
