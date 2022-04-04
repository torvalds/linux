/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT)
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKCIF_CONFIG_H
#define _UAPI_RKCIF_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define RKCIF_API_VERSION		KERNEL_VERSION(0, 1, 0xa)

#define RKCIF_CMD_GET_CSI_MEMORY_MODE \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, int)

#define RKCIF_CMD_SET_CSI_MEMORY_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, int)

#define RKCIF_CMD_GET_SCALE_BLC \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, struct bayer_blc)

#define RKCIF_CMD_SET_SCALE_BLC \
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, struct bayer_blc)

#define RKCIF_CMD_SET_FPS \
	_IOW('V', BASE_VIDIOC_PRIVATE + 4, struct rkcif_fps)

/* cif memory mode
 * 0: raw12/raw10/raw8 8bit memory compact
 * 1: raw12/raw10 16bit memory one pixel
 *    low align for rv1126/rv1109/rk356x
 *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 *    | -| -| -| -|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 * 2: raw12/raw10 16bit memory one pixel
 *    high align for rv1126/rv1109/rk356x
 *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 *    |11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0| -| -| -| -|
 *
 * note: rv1109/rv1126/rk356x dvp only support uncompact mode,
 *       and can be set low align or high align
 */

enum cif_csi_lvds_memory {
	CSI_LVDS_MEM_COMPACT = 0,
	CSI_LVDS_MEM_WORD_LOW_ALIGN = 1,
	CSI_LVDS_MEM_WORD_HIGH_ALIGN = 2,
};

/* black level for scale image
 * The sequence of pattern00~03 is the same as the output of sensor bayer
 */

struct bayer_blc {
	u8 pattern00;
	u8 pattern01;
	u8 pattern02;
	u8 pattern03;
};

struct rkcif_fps {
	int ch_num;
	int fps;
};

#endif
