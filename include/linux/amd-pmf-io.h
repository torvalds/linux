/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Platform Management Framework Interface
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *          Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#ifndef AMD_PMF_IO_H
#define AMD_PMF_IO_H

#include <linux/types.h>

/**
 * enum sfh_message_type - Query the SFH message type
 * @MT_HPD: Message ID to know the Human presence info from MP2 FW
 * @MT_ALS: Message ID to know the Ambient light info from MP2 FW
 * @MT_SRA: Message ID to know the SRA data from MP2 FW
 * @MT_OP_MODE: Message ID to know the operating-mode (tablet/laptop) info
 */
enum sfh_message_type {
	MT_HPD,
	MT_ALS,
	MT_SRA,
	MT_OP_MODE,
};

/**
 * enum sfh_hpd_info - Query the Human presence information
 * @SFH_NOT_DETECTED: Check the HPD connection information from MP2 FW
 * @SFH_USER_PRESENT: Check if the user is present from HPD sensor
 * @SFH_USER_AWAY: Check if the user is away from HPD sensor
 */
enum sfh_hpd_info {
	SFH_NOT_DETECTED,
	SFH_USER_PRESENT,
	SFH_USER_AWAY,
};

/**
 * struct amd_sfh_info - get HPD sensor info from MP2 FW
 * @ambient_light: Populates the ambient light information
 * @user_present: Populates the user presence information
 * @platform_type: Operating modes (clamshell, flat, tent, etc.)
 * @laptop_placement: Device states (ontable, onlap, outbag)
 * @op_mode: Operating-mode field (see enum sfh_dev_mode); used for tablet detection
 */
struct amd_sfh_info {
	u32 ambient_light;
	u8 user_present;
	u32 platform_type;
	u32 laptop_placement;
	u32 op_mode;
};

/**
 * enum sfh_dev_mode - SFH operating-mode field (sfh_op_mode.mode, bits 0-2)
 * @SFH_MODE_LAPTOP: Device is in laptop/clamshell posture
 * @SFH_MODE_TABLET: Device is in tablet posture
 */
enum sfh_dev_mode {
	SFH_MODE_LAPTOP	= 1,
	SFH_MODE_TABLET	= 3,
};

/**
 * struct amd_pmf_npu_metrics: Get NPU metrics data from PMF driver
 * @npuclk_freq: NPU clock frequency [MHz]
 * @npu_busy: NPU busy % [0-100]
 * @npu_power: NPU power [mW]
 * @mpnpuclk_freq: MPNPU [MHz]
 * @npu_reads: NPU read bandwidth [MB/sec]
 * @npu_writes: NPU write bandwidth [MB/sec]
 * @npu_temp: NPU temperature [C]
 */
struct amd_pmf_npu_metrics {
	u16 npuclk_freq;
	u16 npu_busy[8];
	u16 npu_power;
	u16 mpnpuclk_freq;
	u16 npu_reads;
	u16 npu_writes;
	u16 npu_temp;
};

int amd_get_sfh_info(struct amd_sfh_info *sfh_info, enum sfh_message_type op);

/* AMD PMF and NPU interface */
int amd_pmf_get_npu_data(struct amd_pmf_npu_metrics *info);
#endif
