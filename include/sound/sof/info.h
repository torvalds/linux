/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_INFO_H__
#define __INCLUDE_SOUND_SOF_INFO_H__

#include <sound/sof/header.h>
#include <sound/sof/stream.h>

/*
 * Firmware boot and version
 */

#define SOF_IPC_MAX_ELEMS	16

/*
 * Firmware boot info flag bits (64-bit)
 */
#define SOF_IPC_INFO_BUILD		BIT(0)
#define SOF_IPC_INFO_LOCKS		BIT(1)
#define SOF_IPC_INFO_LOCKSV		BIT(2)
#define SOF_IPC_INFO_GDB		BIT(3)

/* extended data types that can be appended onto end of sof_ipc_fw_ready */
enum sof_ipc_ext_data {
	SOF_IPC_EXT_UNUSED		= 0,
	SOF_IPC_EXT_WINDOW		= 1,
	SOF_IPC_EXT_CC_INFO		= 2,
	SOF_IPC_EXT_PROBE_INFO		= 3,
	SOF_IPC_EXT_USER_ABI_INFO	= 4,
};

/* FW version - SOF_IPC_GLB_VERSION */
struct sof_ipc_fw_version {
	struct sof_ipc_hdr hdr;
	uint16_t major;
	uint16_t minor;
	uint16_t micro;
	uint16_t build;
	uint8_t date[12];
	uint8_t time[10];
	uint8_t tag[6];
	uint32_t abi_version;

	/* reserved for future use */
	uint32_t reserved[4];
} __packed;

/* FW ready Message - sent by firmware when boot has completed */
struct sof_ipc_fw_ready {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t dspbox_offset;	 /* dsp initiated IPC mailbox */
	uint32_t hostbox_offset; /* host initiated IPC mailbox */
	uint32_t dspbox_size;
	uint32_t hostbox_size;
	struct sof_ipc_fw_version version;

	/* Miscellaneous flags */
	uint64_t flags;

	/* reserved for future use */
	uint32_t reserved[4];
} __packed;

/*
 * Extended Firmware data. All optional, depends on platform/arch.
 */
enum sof_ipc_region {
	SOF_IPC_REGION_DOWNBOX	= 0,
	SOF_IPC_REGION_UPBOX,
	SOF_IPC_REGION_TRACE,
	SOF_IPC_REGION_DEBUG,
	SOF_IPC_REGION_STREAM,
	SOF_IPC_REGION_REGS,
	SOF_IPC_REGION_EXCEPTION,
};

struct sof_ipc_ext_data_hdr {
	struct sof_ipc_cmd_hdr hdr;
	uint32_t type;		/**< SOF_IPC_EXT_ */
} __packed;

struct sof_ipc_window_elem {
	struct sof_ipc_hdr hdr;
	uint32_t type;		/**< SOF_IPC_REGION_ */
	uint32_t id;		/**< platform specific - used to map to host memory */
	uint32_t flags;		/**< R, W, RW, etc - to define */
	uint32_t size;		/**< size of region in bytes */
	/* offset in window region as windows can be partitioned */
	uint32_t offset;
} __packed;

/* extended data memory windows for IPC, trace and debug */
struct sof_ipc_window {
	struct sof_ipc_ext_data_hdr ext_hdr;
	uint32_t num_windows;
	struct sof_ipc_window_elem window[];
}  __packed;

struct sof_ipc_cc_version {
	struct sof_ipc_ext_data_hdr ext_hdr;
	uint32_t major;
	uint32_t minor;
	uint32_t micro;

	/* reserved for future use */
	uint32_t reserved[4];

	char name[16]; /* null terminated compiler name */
	char optim[4]; /* null terminated compiler -O flag value */
	char desc[]; /* null terminated compiler description */
} __packed;

/* extended data: Probe setup */
struct sof_ipc_probe_support {
	struct sof_ipc_ext_data_hdr ext_hdr;

	uint32_t probe_points_max;
	uint32_t injection_dmas_max;

	/* reserved for future use */
	uint32_t reserved[2];
} __packed;

/* extended data: user abi version(s) */
struct sof_ipc_user_abi_version {
	struct sof_ipc_ext_data_hdr ext_hdr;

	uint32_t abi_dbg_version;
}  __packed;

#endif
