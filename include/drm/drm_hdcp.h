/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#ifndef _DRM_HDCP_H_INCLUDED_
#define _DRM_HDCP_H_INCLUDED_

/* Period of hdcp checks (to ensure we're still authenticated) */
#define DRM_HDCP_CHECK_PERIOD_MS		(128 * 16)

/* Shared lengths/masks between HDMI/DVI/DisplayPort */
#define DRM_HDCP_AN_LEN				8
#define DRM_HDCP_BSTATUS_LEN			2
#define DRM_HDCP_KSV_LEN			5
#define DRM_HDCP_RI_LEN				2
#define DRM_HDCP_V_PRIME_PART_LEN		4
#define DRM_HDCP_V_PRIME_NUM_PARTS		5
#define DRM_HDCP_NUM_DOWNSTREAM(x)		(x & 0x7f)
#define DRM_HDCP_MAX_CASCADE_EXCEEDED(x)	(x & BIT(3))
#define DRM_HDCP_MAX_DEVICE_EXCEEDED(x)		(x & BIT(7))

/* Slave address for the HDCP registers in the receiver */
#define DRM_HDCP_DDC_ADDR			0x3A

/* HDCP register offsets for HDMI/DVI devices */
#define DRM_HDCP_DDC_BKSV			0x00
#define DRM_HDCP_DDC_RI_PRIME			0x08
#define DRM_HDCP_DDC_AKSV			0x10
#define DRM_HDCP_DDC_AN				0x18
#define DRM_HDCP_DDC_V_PRIME(h)			(0x20 + h * 4)
#define DRM_HDCP_DDC_BCAPS			0x40
#define  DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT	BIT(6)
#define  DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY	BIT(5)
#define DRM_HDCP_DDC_BSTATUS			0x41
#define DRM_HDCP_DDC_KSV_FIFO			0x43

#endif
