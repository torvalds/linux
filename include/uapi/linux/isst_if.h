/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel Speed Select Interface: OS to hardware Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef __ISST_IF_H
#define __ISST_IF_H

#include <linux/types.h>

/**
 * struct isst_if_platform_info - Define platform information
 * @api_version:	Version of the firmware document, which this driver
 *			can communicate
 * @driver_version:	Driver version, which will help user to send right
 *			commands. Even if the firmware is capable, driver may
 *			not be ready
 * @max_cmds_per_ioctl:	Returns the maximum number of commands driver will
 *			accept in a single ioctl
 * @mbox_supported:	Support of mail box interface
 * @mmio_supported:	Support of mmio interface for core-power feature
 *
 * Used to return output of IOCTL ISST_IF_GET_PLATFORM_INFO. This
 * information can be used by the user space, to get the driver, firmware
 * support and also number of commands to send in a single IOCTL request.
 */
struct isst_if_platform_info {
	__u16 api_version;
	__u16 driver_version;
	__u16 max_cmds_per_ioctl;
	__u8 mbox_supported;
	__u8 mmio_supported;
};

#define ISST_IF_MAGIC			0xFE
#define ISST_IF_GET_PLATFORM_INFO	_IOR(ISST_IF_MAGIC, 0, struct isst_if_platform_info *)
#endif
