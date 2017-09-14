/*
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __UAPI_LINUX_RK_VPU_SERVICE_H__
#define __UAPI_LINUX_RK_VPU_SERVICE_H__

#include <linux/types.h>
#include <asm/ioctl.h>

/*
 * Ioctl definitions
 */

/* Use 'l' as magic number */
#define VPU_IOC_MAGIC			'l'

#define VPU_IOC_SET_CLIENT_TYPE		_IOW(VPU_IOC_MAGIC, 1, __u32)
#define VPU_IOC_GET_HW_FUSE_STATUS	_IOW(VPU_IOC_MAGIC, 2, unsigned long)

#define VPU_IOC_SET_REG			_IOW(VPU_IOC_MAGIC, 3, unsigned long)
#define VPU_IOC_GET_REG			_IOW(VPU_IOC_MAGIC, 4, unsigned long)

#define VPU_IOC_PROBE_IOMMU_STATUS	_IOR(VPU_IOC_MAGIC, 5, __u32)

struct vpu_request {
	__u32 *req;
	__u32 size;
};

/* Hardware decoder configuration description */
struct vpu_dec_config {
	/* Maximum video decoding width supported  */
	__u32 max_dec_pic_width;
	/* Maximum output width of Post-Processor */
	__u32 max_pp_out_pic_width;
	/* HW supports h.264 */
	__u32 h264_support;
	/* HW supports JPEG */
	__u32 jpeg_support;
	/* HW supports MPEG-4 */
	__u32 mpeg4_support;
	/* HW supports custom MPEG-4 features */
	__u32 custom_mpeg4_support;
	/* HW supports VC-1 Simple */
	__u32 vc1_support;
	/* HW supports MPEG-2 */
	__u32 mpeg2_support;
	/* HW supports post-processor */
	__u32 pp_support;
	/* HW post-processor functions bitmask */
	__u32 pp_config;
	/* HW supports Sorenson Spark */
	__u32 sorenson_support;
	/* HW supports reference picture buffering */
	__u32 ref_buf_support;
	/* HW supports VP6 */
	__u32 vp6_support;
	/* HW supports VP7 */
	__u32 vp7_support;
	/* HW supports VP8 */
	__u32 vp8_support;
	/* HW supports AVS */
	__u32 avs_support;
	/* HW supports JPEG extensions */
	__u32 jpeg_ext_support;
	__u32 reserve;
	/* HW supports H264 MVC extension */
	__u32 mvc_support;
};

/* Hardware encoder configuration description */
struct vpu_enc_config {
	/* Maximum supported width for video encoding (not JPEG) */
	__u32 max_encoded_width;
	/* HW supports H.264 */
	__u32 h264_enabled;
	/* HW supports JPEG */
	__u32 jpeg_enabled;
	/* HW supports MPEG-4 */
	__u32 mpeg4_enabled;
	/* HW supports video stabilization */
	__u32 vs_enabled;
	/* HW supports RGB input */
	__u32 rgb_enabled;
	__u32 reg_size;
	__u32 reserv[2];
};

#endif
