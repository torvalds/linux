/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These are the FWHT state controls for use with stateless FWHT
 * codec drivers.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _FWHT_CTRLS_H_
#define _FWHT_CTRLS_H_

#define V4L2_CTRL_TYPE_FWHT_PARAMS 0x0105

#define V4L2_CID_MPEG_VIDEO_FWHT_PARAMS	(V4L2_CID_MPEG_BASE + 292)

struct v4l2_ctrl_fwht_params {
	__u64 backward_ref_ts;
	__u32 version;
	__u32 width;
	__u32 height;
	__u32 flags;
	__u32 colorspace;
	__u32 xfer_func;
	__u32 ycbcr_enc;
	__u32 quantization;
};


#endif
