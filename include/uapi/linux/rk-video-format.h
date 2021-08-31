/* SPDX-License-Identifier: (GPL-2.0+ WITH Linux-syscall-note) OR MIT
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */
#ifndef _UAPI_RK_VIDEO_FORMAT_H
#define _UAPI_RK_VIDEO_FORMAT_H

/*  Four-character-code (FOURCC) */
#define v4l2_fourcc(a, b, c, d)\
	((__u32)(a) | ((__u32)(b) << 8) | ((__u32)(c) << 16) | ((__u32)(d) << 24))
#define v4l2_fourcc_be(a, b, c, d)	(v4l2_fourcc(a, b, c, d) | (1U << 31))

/* Rockchip yuv422sp frame buffer compression encoder */
#define V4L2_PIX_FMT_FBC2     v4l2_fourcc('F', 'B', 'C', '2')
/* Rockchip yuv420sp frame buffer compression encoder */
#define V4L2_PIX_FMT_FBC0     v4l2_fourcc('F', 'B', 'C', '0')
#define V4L2_PIX_FMT_FBCG     v4l2_fourcc('F', 'B', 'C', 'G')
/* embedded data 8-bit */
#define V4l2_PIX_FMT_EBD8     v4l2_fourcc('E', 'B', 'D', '8')
/* shield pix data 16-bit */
#define V4l2_PIX_FMT_SPD16    v4l2_fourcc('S', 'P', 'D', '6')

/* Vendor specific - used for Rockchip ISP1 camera sub-system */
#define V4L2_META_FMT_RK_ISP1_PARAMS	v4l2_fourcc('R', 'K', '1', 'P') /* Rockchip ISP1 params */
#define V4L2_META_FMT_RK_ISP1_STAT_3A	v4l2_fourcc('R', 'K', '1', 'S') /* Rockchip ISP1 3A statistics */
#define V4L2_META_FMT_RK_ISP1_STAT_LUMA	v4l2_fourcc('R', 'K', '1', 'L') /* Rockchip ISP1 luma statistics */
#define V4L2_META_FMT_RK_ISPP_PARAMS	v4l2_fourcc('R', 'K', 'P', 'P') /* Rockchip ISPP params */
#define V4L2_META_FMT_RK_ISPP_STAT	v4l2_fourcc('R', 'K', 'P', 'S') /* Rockchip ISPP statistics */

/* sensor embedded data format */
#define MEDIA_BUS_FMT_EBD_1X8		0x5002
/* sensor shield pix data format */
#define MEDIA_BUS_FMT_SPD_2X8		0x5003

#endif /* _UAPI_RK_VIDEO_FORMAT_H */
