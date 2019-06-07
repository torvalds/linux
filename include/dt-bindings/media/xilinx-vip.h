// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Video IP Core
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __DT_BINDINGS_MEDIA_XILINX_VIP_H__
#define __DT_BINDINGS_MEDIA_XILINX_VIP_H__

/*
 * Video format codes as defined in "AXI4-Stream Video IP and System Design
 * Guide".
 */
#define XVIP_VF_YUV_422			0
#define XVIP_VF_YUV_444			1
#define XVIP_VF_RBG			2
#define XVIP_VF_YUV_420			3
#define XVIP_VF_YUVA_422		4
#define XVIP_VF_YUVA_444		5
#define XVIP_VF_RGBA			6
#define XVIP_VF_YUVA_420		7
#define XVIP_VF_YUVD_422		8
#define XVIP_VF_YUVD_444		9
#define XVIP_VF_RGBD			10
#define XVIP_VF_YUVD_420		11
#define XVIP_VF_MONO_SENSOR		12
#define XVIP_VF_CUSTOM2			13
#define XVIP_VF_CUSTOM3			14
#define XVIP_VF_CUSTOM4			15

#endif /* __DT_BINDINGS_MEDIA_XILINX_VIP_H__ */
