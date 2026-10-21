/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */
#ifndef _NV_FALCON_HW_H_
#define _NV_FALCON_HW_H_

#include <linux/types.h>

/* PMC (Power Management Controller) Registers */
#define NV_PMC_BOOT_0					0x00000000
#define NV_PMC_ENABLE					0x00000200
#define NV_PMC_ENABLE_PWR				0x00002000
#define NV_PMC_ENABLE_HUB				0x20000000

/* Falcon Base Pages for Different Engines */
#define NV_PPWR_FALCON_BASE				0x10a000
#define NV_PGSP_FALCON_BASE				0x110000

/* Falcon Common Register Offsets (relative to base_page) */
#define NV_FALCON_DMACTL_OFFSET				0x010c
#define NV_FALCON_ENGINE_RESET_OFFSET			0x03c0

/* DMEM Control Register Flags */
#define NV_PPWR_FALCON_DMEMC_AINCR_TRUE			0x01000000
#define NV_PPWR_FALCON_DMEMC_AINCW_TRUE			0x02000000

/* Falcon DMEM port offsets (for port 0) */
#define NV_FALCON_DMEMC_OFFSET				0x1c0
#define NV_FALCON_DMEMD_OFFSET				0x1c4

/* DMA Register Offsets (relative to base_page) */
#define NV_FALCON_DMA_ADDR_LOW_OFFSET			0x110
#define NV_FALCON_DMA_MEM_OFFSET			0x114
#define NV_FALCON_DMA_CMD_OFFSET			0x118
#define NV_FALCON_DMA_BLOCK_OFFSET			0x11c
#define NV_FALCON_DMA_ADDR_HIGH_OFFSET			0x128

/* DMA Global Address Top Bits Register */
#define NV_GPU_DMA_ADDR_TOP_BITS_REG			0x100f04

/* DMA Command Register Bit Definitions */
#define NV_FALCON_DMA_CMD_WRITE_BIT			0x20
#define NV_FALCON_DMA_CMD_SIZE_SHIFT			8
#define NV_FALCON_DMA_CMD_DONE_BIT			0x2

/*
 * Falcon DMA is synchronous, so a transfer size and count larger than
 * its per-operation maximum adds no value.
 */

/* DMA block size and alignment */
#define NV_FALCON_DMA_MIN_TRANSFER_SIZE			4
#define NV_FALCON_DMA_MAX_TRANSFER_SIZE			256
#define NV_FALCON_DMA_BLOCK_SIZE			256
#define NV_FALCON_DMA_MAX_TRANSFER_COUNT		1

/* DMACTL register bits */
#define NV_FALCON_DMACTL_DMEM_SCRUBBING			0x1
#define NV_FALCON_DMACTL_READY_MASK			0x6

/* Falcon Core Selection Register */
#define NV_FALCON_CORE_SELECT_OFFSET			0x1668
#define NV_FALCON_CORE_SELECT_MASK			0x30

/* Falcon mailbox register (for Ada+ reset check) */
#define NV_FALCON_MAILBOX_TEST_OFFSET			0x40c
#define NV_FALCON_MAILBOX_RESET_MAGIC			0xbadf5620

/* Falcon Message Queue Register Offsets (relative to base_page) */
#define NV_FALCON_QUEUE_HEAD_BASE_OFFSET		0x2c00
#define NV_FALCON_QUEUE_TAIL_BASE_OFFSET		0x2c04
#define NV_FALCON_QUEUE_STRIDE				0x8
#define NV_FALCON_MSG_QUEUE_HEAD_BASE_OFFSET		0x2c80
#define NV_FALCON_MSG_QUEUE_TAIL_BASE_OFFSET		0x2c84

/* FSP Falcon Base Pages */
#define NV_FSP_FALCON_BASE				0x8f0100
/* base_page = cpuctl & ~0xfff */
#define NV_FSP_FALCON_BASE_PAGE				0x8f0000
#define NV_FSP_EMEM_BASE				0x8f2000

/* FSP EMEM Port Offsets (relative to FSP EMEM base) */
#define NV_FSP_EMEMC_OFFSET				0xac0
#define NV_FSP_EMEMD_OFFSET				0xac4
#define NV_FSP_EMEM_PORT_STRIDE				0x8

/* EMEM Control Register Flags (same as DMEM) */
#define NV_FALCON_EMEMC_AINCR				0x01000000
#define NV_FALCON_EMEMC_AINCW				0x02000000

/* FSP RPC channel configuration */
#define NV_FSP_RPC_CHANNEL_SIZE				1024
#define NV_FSP_RPC_MAX_PACKET_SIZE			1024
#define NV_FSP_RPC_CHANNEL_HOPPER			2
#define NV_FSP_RPC_EMEM_BASE					\
	(NV_FSP_RPC_CHANNEL_HOPPER * NV_FSP_RPC_CHANNEL_SIZE)

/* FSP EMEM port 2 registers (pre-computed for Hopper channel 2) */
#define NV_FSP_EMEM_PORT2_CTRL		(NV_FSP_EMEM_BASE + NV_FSP_EMEMC_OFFSET + \
					 NV_FSP_RPC_CHANNEL_HOPPER * NV_FSP_EMEM_PORT_STRIDE)
#define NV_FSP_EMEM_PORT2_DATA		(NV_FSP_EMEM_BASE + NV_FSP_EMEMD_OFFSET + \
					 NV_FSP_RPC_CHANNEL_HOPPER * NV_FSP_EMEM_PORT_STRIDE)

/* FSP queue register offsets (pre-computed for Hopper channel 2) */
#define NV_FSP_QUEUE_HEAD	\
	(NV_FSP_FALCON_BASE_PAGE + NV_FALCON_QUEUE_HEAD_BASE_OFFSET + \
	 NV_FSP_RPC_CHANNEL_HOPPER * NV_FALCON_QUEUE_STRIDE)
#define NV_FSP_QUEUE_TAIL	\
	(NV_FSP_FALCON_BASE_PAGE + NV_FALCON_QUEUE_TAIL_BASE_OFFSET + \
	 NV_FSP_RPC_CHANNEL_HOPPER * NV_FALCON_QUEUE_STRIDE)
#define NV_FSP_MSG_QUEUE_HEAD	\
	(NV_FSP_FALCON_BASE_PAGE + NV_FALCON_MSG_QUEUE_HEAD_BASE_OFFSET + \
	 NV_FSP_RPC_CHANNEL_HOPPER * NV_FALCON_QUEUE_STRIDE)
#define NV_FSP_MSG_QUEUE_TAIL	\
	(NV_FSP_FALCON_BASE_PAGE + NV_FALCON_MSG_QUEUE_TAIL_BASE_OFFSET + \
	 NV_FSP_RPC_CHANNEL_HOPPER * NV_FALCON_QUEUE_STRIDE)

/* MCTP Header */
#define NV_MCTP_HDR_SEID_SHIFT				16
#define NV_MCTP_HDR_SEID_MASK				0xff
#define NV_MCTP_HDR_SEQ_SHIFT				28
#define NV_MCTP_HDR_SEQ_MASK				0x3
#define NV_MCTP_HDR_EOM_BIT				0x40000000
#define NV_MCTP_HDR_SOM_BIT				0x80000000

/* MCTP Message Header */
#define NV_MCTP_MSG_TYPE_SHIFT				0
#define NV_MCTP_MSG_TYPE_MASK				0x7f
#define NV_MCTP_MSG_TYPE_VENDOR_DEFINED			0x7e
#define NV_MCTP_MSG_VENDOR_ID_SHIFT			8
#define NV_MCTP_MSG_VENDOR_ID_MASK			0xffff
#define NV_MCTP_MSG_VENDOR_ID_NVIDIA			0x10de
#define NV_MCTP_MSG_NVDM_TYPE_SHIFT			24
#define NV_MCTP_MSG_NVDM_TYPE_MASK			0xff

/* NVDM response type */
#define NV_NVDM_TYPE_RESPONSE				0x15

/* Minimum response size: mctp_hdr + msg_hdr + status_hdr + type + status */
#define NV_FSP_RPC_MIN_RESPONSE_WORDS			5

/* FBIF (Frame Buffer Interface) Registers */
/* Legacy PMU FBIF offsets (Kepler, Maxwell Gen1) */
#define NV_PMU_LEGACY_FBIF_CTL_OFFSET			0x624
#define NV_PMU_LEGACY_FBIF_TRANSCFG_OFFSET		0x600

/* PMU FBIF offsets */
#define NV_PMU_FBIF_CTL_OFFSET				0xe24
#define NV_PMU_FBIF_TRANSCFG_OFFSET			0xe00

/* GSP FBIF offsets */
#define NV_GSP_FBIF_CTL_OFFSET				0x624
#define NV_GSP_FBIF_TRANSCFG_OFFSET			0x600

/* OFA Falcon Base Page and FBIF offsets (used for Hopper+ DMA) */
#define NV_OFA_FALCON_BASE				0x844000
#define NV_OFA_FBIF_CTL_OFFSET				0x424
#define NV_OFA_FBIF_TRANSCFG_OFFSET			0x400

/* OFA DMA support check register (Hopper+) */
#define NV_OFA_DMA_SUPPORT_CHECK_REG			0x8443c0

/* FSP NVDM command types */
#define NV_NVDM_TYPE_FBDMA				0x22
#define NV_FBDMA_SUBCMD_ENABLE				0x1

/* FBIF CTL2 offset (relative to fbif_ctl) */
#define NV_FBIF_CTL2_OFFSET				0x60

/* FBIF TRANSCFG register bits */
#define NV_FBIF_TRANSCFG_TARGET_MASK			0x3
#define NV_FBIF_TRANSCFG_SYSMEM_DEFAULT			0x5

/* FBIF CTL register bits */
#define NV_FBIF_CTL_ALLOW_PHYS_MODE			0x10
#define NV_FBIF_CTL_ALLOW_FULL_PHYS_MODE		0x80

/* Memory clear register offsets */
#define NV_MEM_CLEAR_OFFSET				0x100b20
#define NV_BOOT_COMPLETE_OFFSET				0x118234
#define NV_BOOT_COMPLETE_SUCCESS			0x3ff

/* FSP boot complete register (Hopper+) */
#define NV_FSP_BOOT_COMPLETE_OFFSET			0x200bc
#define NV_FSP_BOOT_COMPLETE_SUCCESS			0xff

enum gpu_arch {
	GPU_ARCH_UNKNOWN = -1,
	GPU_ARCH_KEPLER = 0,
	GPU_ARCH_MAXWELL_GEN1,
	GPU_ARCH_MAXWELL_GEN2,
	GPU_ARCH_PASCAL,
	GPU_ARCH_PASCAL_10X,
	GPU_ARCH_VOLTA,
	GPU_ARCH_TURING,
	GPU_ARCH_AMPERE,
	GPU_ARCH_ADA,
	GPU_ARCH_HOPPER,
};

enum falcon_type {
	FALCON_TYPE_PMU_LEGACY = 0,
	FALCON_TYPE_PMU,
	FALCON_TYPE_GSP,
	FALCON_TYPE_OFA,
};

struct falcon {
	u32 base_page;
	u32 dmactl;
	u32 engine_reset;
	u32 fbif_ctl;
	u32 fbif_ctl2;
	u32 fbif_transcfg;
	u32 dmem_control_reg;
	u32 dmem_data_reg;
	bool no_outside_reset;
};

struct gpu_properties {
	u32 pmc_enable_mask;
	bool memory_clear_supported;
	enum falcon_type falcon_type;
};

static const u32 verified_gpu_map[] = {
	0x0e40a0a2,	/* K520 */
	0x0e6000a1,	/* GTX660 */
	0x0e63a0a1,	/* K4000 */
	0x0f22d0a1,	/* K80 */
	0x108000a1,	/* GT635 */
	0x117010a2,	/* GTX750 */
	0x117020a2,	/* GTX745 */
	0x124320a1,	/* M60 */
	0x130000a1,	/* P100 */
	0x134000a1,	/* P4 */
	0x132000a1,	/* P40 */
	0x140000a1,	/* V100 */
	0x164000a1,	/* T4 */
	0xb77000a1,	/* A16 */
	0x170000a1,	/* A100 */
	0xb72000a1,	/* A10 */
	0x180000a1,	/* H100 */
	0x194000a1,	/* L4 */
	0x192000a1,	/* L40S */
};

#define VERIFIED_GPU_MAP_SIZE ARRAY_SIZE(verified_gpu_map)

static const struct gpu_properties gpu_properties_map[] = {
	[GPU_ARCH_KEPLER] = {
		.pmc_enable_mask = NV_PMC_ENABLE_PWR | NV_PMC_ENABLE_HUB,
		.memory_clear_supported = false,
		.falcon_type = FALCON_TYPE_PMU_LEGACY,
	},
	[GPU_ARCH_MAXWELL_GEN1] = {
		.pmc_enable_mask = NV_PMC_ENABLE_PWR | NV_PMC_ENABLE_HUB,
		.memory_clear_supported = false,
		.falcon_type = FALCON_TYPE_PMU_LEGACY,
	},
	[GPU_ARCH_MAXWELL_GEN2] = {
		.pmc_enable_mask = NV_PMC_ENABLE_PWR,
		.memory_clear_supported = false,
		.falcon_type = FALCON_TYPE_PMU,
	},
	[GPU_ARCH_PASCAL] = {
		.pmc_enable_mask = NV_PMC_ENABLE_PWR,
		.memory_clear_supported = false,
		.falcon_type = FALCON_TYPE_PMU,
	},
	[GPU_ARCH_PASCAL_10X] = {
		.pmc_enable_mask = 0,
		.memory_clear_supported = false,
		.falcon_type = FALCON_TYPE_PMU,
	},
	[GPU_ARCH_VOLTA] = {
		.pmc_enable_mask = 0,
		.memory_clear_supported = false,
		.falcon_type = FALCON_TYPE_GSP,
	},
	[GPU_ARCH_TURING] = {
		.pmc_enable_mask = 0,
		.memory_clear_supported = true,
		.falcon_type = FALCON_TYPE_GSP,
	},
	[GPU_ARCH_AMPERE] = {
		.pmc_enable_mask = 0,
		.memory_clear_supported = true,
		.falcon_type = FALCON_TYPE_GSP,
	},
	[GPU_ARCH_ADA] = {
		.pmc_enable_mask = 0,
		.memory_clear_supported = true,
		.falcon_type = FALCON_TYPE_PMU,
	},
	[GPU_ARCH_HOPPER] = {
		.pmc_enable_mask = 0,
		.memory_clear_supported = true,
		.falcon_type = FALCON_TYPE_OFA,
	},
};

static const struct falcon falcon_map[] = {
	[FALCON_TYPE_PMU_LEGACY] = {
		.base_page = NV_PPWR_FALCON_BASE,
		.dmactl = NV_PPWR_FALCON_BASE + NV_FALCON_DMACTL_OFFSET,
		.engine_reset = NV_PPWR_FALCON_BASE + NV_FALCON_ENGINE_RESET_OFFSET,
		.fbif_ctl = NV_PPWR_FALCON_BASE + NV_PMU_LEGACY_FBIF_CTL_OFFSET,
		.fbif_ctl2 = NV_PPWR_FALCON_BASE +
			     NV_PMU_LEGACY_FBIF_CTL_OFFSET + NV_FBIF_CTL2_OFFSET,
		.fbif_transcfg = NV_PPWR_FALCON_BASE + NV_PMU_LEGACY_FBIF_TRANSCFG_OFFSET,
		.dmem_control_reg = NV_PPWR_FALCON_BASE + NV_FALCON_DMEMC_OFFSET,
		.dmem_data_reg = NV_PPWR_FALCON_BASE + NV_FALCON_DMEMD_OFFSET,
		.no_outside_reset = false,
	},
	[FALCON_TYPE_PMU] = {
		.base_page = NV_PPWR_FALCON_BASE,
		.dmactl = NV_PPWR_FALCON_BASE + NV_FALCON_DMACTL_OFFSET,
		.engine_reset = NV_PPWR_FALCON_BASE + NV_FALCON_ENGINE_RESET_OFFSET,
		.fbif_ctl = NV_PPWR_FALCON_BASE + NV_PMU_FBIF_CTL_OFFSET,
		.fbif_ctl2 = NV_PPWR_FALCON_BASE + NV_PMU_FBIF_CTL_OFFSET + NV_FBIF_CTL2_OFFSET,
		.fbif_transcfg = NV_PPWR_FALCON_BASE + NV_PMU_FBIF_TRANSCFG_OFFSET,
		.dmem_control_reg = NV_PPWR_FALCON_BASE + NV_FALCON_DMEMC_OFFSET,
		.dmem_data_reg = NV_PPWR_FALCON_BASE + NV_FALCON_DMEMD_OFFSET,
		.no_outside_reset = false,
	},
	[FALCON_TYPE_GSP] = {
		.base_page = NV_PGSP_FALCON_BASE,
		.dmactl = NV_PGSP_FALCON_BASE + NV_FALCON_DMACTL_OFFSET,
		.engine_reset = NV_PGSP_FALCON_BASE + NV_FALCON_ENGINE_RESET_OFFSET,
		.fbif_ctl = NV_PGSP_FALCON_BASE + NV_GSP_FBIF_CTL_OFFSET,
		.fbif_ctl2 = NV_PGSP_FALCON_BASE + NV_GSP_FBIF_CTL_OFFSET + NV_FBIF_CTL2_OFFSET,
		.fbif_transcfg = NV_PGSP_FALCON_BASE + NV_GSP_FBIF_TRANSCFG_OFFSET,
		.dmem_control_reg = NV_PGSP_FALCON_BASE + NV_FALCON_DMEMC_OFFSET,
		.dmem_data_reg = NV_PGSP_FALCON_BASE + NV_FALCON_DMEMD_OFFSET,
		.no_outside_reset = false,
	},
	[FALCON_TYPE_OFA] = {
		.base_page = NV_OFA_FALCON_BASE,
		.dmactl = NV_OFA_FALCON_BASE + NV_FALCON_DMACTL_OFFSET,
		.engine_reset = NV_OFA_FALCON_BASE + NV_FALCON_ENGINE_RESET_OFFSET,
		.fbif_ctl = NV_OFA_FALCON_BASE + NV_OFA_FBIF_CTL_OFFSET,
		.fbif_ctl2 = NV_OFA_FALCON_BASE + NV_OFA_FBIF_CTL_OFFSET + NV_FBIF_CTL2_OFFSET,
		.fbif_transcfg = NV_OFA_FALCON_BASE + NV_OFA_FBIF_TRANSCFG_OFFSET,
		.dmem_control_reg = NV_OFA_FALCON_BASE + NV_FALCON_DMEMC_OFFSET,
		.dmem_data_reg = NV_OFA_FALCON_BASE + NV_FALCON_DMEMD_OFFSET,
		.no_outside_reset = true,
	},
};

#endif /* _NV_FALCON_HW_H_ */
