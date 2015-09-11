/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc. All Rights Reserved
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*!
 * @file uapi/linux/mxc_v4l2.h
 *
 * @brief MXC V4L2 private header file
 *
 * @ingroup MXC V4L2
 */

#ifndef __ASM_ARCH_MXC_V4L2_H__
#define __ASM_ARCH_MXC_V4L2_H__

/*
 * For IPUv1 and IPUv3, V4L2_CID_MXC_ROT means encoder ioctl ID.
 * And V4L2_CID_MXC_VF_ROT is viewfinder ioctl ID only for IPUv1 and IPUv3.
 */
#define V4L2_CID_MXC_ROT		(V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_MXC_FLASH		(V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_MXC_VF_ROT		(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_MXC_MOTION		(V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_MXC_SWITCH_CAM		(V4L2_CID_PRIVATE_BASE + 6)

#define V4L2_MXC_ROTATE_NONE			0
#define V4L2_MXC_ROTATE_VERT_FLIP		1
#define V4L2_MXC_ROTATE_HORIZ_FLIP		2
#define V4L2_MXC_ROTATE_180			3
#define V4L2_MXC_ROTATE_90_RIGHT		4
#define V4L2_MXC_ROTATE_90_RIGHT_VFLIP		5
#define V4L2_MXC_ROTATE_90_RIGHT_HFLIP		6
#define V4L2_MXC_ROTATE_90_LEFT			7

struct v4l2_mxc_offset {
	uint32_t u_offset;
	uint32_t v_offset;
};

#endif
