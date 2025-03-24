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
 */
enum sfh_message_type {
	MT_HPD,
	MT_ALS,
	MT_SRA,
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
 */
struct amd_sfh_info {
	u32 ambient_light;
	u8 user_present;
	u32 platform_type;
	u32 laptop_placement;
};

enum laptop_placement {
	LP_UNKNOWN = 0,
	ON_TABLE,
	ON_LAP_MOTION,
	IN_BAG,
	OUT_OF_BAG,
	LP_UNDEFINED,
};

int amd_get_sfh_info(struct amd_sfh_info *sfh_info, enum sfh_message_type op);
#endif
