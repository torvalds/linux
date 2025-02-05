/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2023 Intel Corporation
 */

#ifndef __SOUND_SOC_SOF_IPC4_TELEMETRY_H
#define __SOUND_SOC_SOF_IPC4_TELEMETRY_H

/* Target code */
enum sof_ipc4_coredump_tgt_code {
	COREDUMP_TGT_UNKNOWN = 0,
	COREDUMP_TGT_X86,
	COREDUMP_TGT_X86_64,
	COREDUMP_TGT_ARM_CORTEX_M,
	COREDUMP_TGT_RISC_V,
	COREDUMP_TGT_XTENSA,
};

#define COREDUMP_ARCH_HDR_ID 'A'
#define COREDUMP_HDR_ID0 'Z'
#define COREDUMP_HDR_ID1 'E'

#define XTENSA_BLOCK_HDR_VER		2
#define XTENSA_CORE_DUMP_SEPARATOR	0x0DEC0DEB
#define XTENSA_CORE_AR_REGS_COUNT	16
#define XTENSA_SOC_INTEL_ADSP		3
#define XTENSA_TOOL_CHAIN_ZEPHYR	1
#define XTENSA_TOOL_CHAIN_XCC		2

/* Coredump header */
struct sof_ipc4_coredump_hdr {
	/* 'Z', 'E' as identifier of file */
	char id[2];

	/* Identify the version of the header */
	u16 hdr_version;

	/* Indicate which target (e.g. architecture or SoC) */
	u16 tgt_code;

	/* Size of uintptr_t in power of 2. (e.g. 5 for 32-bit, 6 for 64-bit) */
	u8 ptr_size_bits;

	u8 flag;

	/* Reason for the fatal error */
	u32 reason;
} __packed;

/* Architecture-specific block header */
struct sof_ipc4_coredump_arch_hdr {
	/* COREDUMP_ARCH_HDR_ID to indicate this is a architecture-specific block */
	char id;

	/* Identify the version of this block */
	u16 hdr_version;

	/* Number of bytes following the header */
	u16 num_bytes;
} __packed;

struct sof_ipc4_telemetry_slot_data {
	u32 separator;
	struct sof_ipc4_coredump_hdr hdr;
	struct sof_ipc4_coredump_arch_hdr arch_hdr;
	u32 arch_data[];
} __packed;

void sof_ipc4_create_exception_debugfs_node(struct snd_sof_dev *sdev);
#endif
