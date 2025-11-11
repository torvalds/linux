/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ARM Mali-C55 ISP Driver - Userspace API
 *
 * Copyright (C) 2023 Ideas on Board Oy
 */

#ifndef __UAPI_MALI_C55_CONFIG_H
#define __UAPI_MALI_C55_CONFIG_H

#include <linux/v4l2-controls.h>

#define V4L2_CID_MALI_C55_CAPABILITIES	(V4L2_CID_USER_MALI_C55_BASE + 0x0)
#define MALI_C55_GPS_PONG		(1U << 0)
#define MALI_C55_GPS_WDR		(1U << 1)
#define MALI_C55_GPS_COMPRESSION	(1U << 2)
#define MALI_C55_GPS_TEMPER		(1U << 3)
#define MALI_C55_GPS_SINTER_LITE	(1U << 4)
#define MALI_C55_GPS_SINTER		(1U << 5)
#define MALI_C55_GPS_IRIDIX_LTM		(1U << 6)
#define MALI_C55_GPS_IRIDIX_GTM		(1U << 7)
#define MALI_C55_GPS_CNR		(1U << 8)
#define MALI_C55_GPS_FRSCALER		(1U << 9)
#define MALI_C55_GPS_DS_PIPE		(1U << 10)

#endif /* __UAPI_MALI_C55_CONFIG_H */
