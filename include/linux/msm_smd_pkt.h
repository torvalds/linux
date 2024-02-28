/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2010,2017,2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_MSM_SMD_PKT_H
#define __LINUX_MSM_SMD_PKT_H

#include <linux/ioctl.h>

#define SMD_PKT_IOCTL_MAGIC (0xC2)

#define SMD_PKT_IOCTL_BLOCKING_WRITE \
	_IOR(SMD_PKT_IOCTL_MAGIC, 0, unsigned int)

static int smd_pkt_rpdev_sigs(struct rpmsg_device *rpdev, void *priv, u32 old, u32 new);

#endif /* __LINUX_MSM_SMD_PKT_H */
