/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */
#include <linux/v4l2-controls.h>
#include <media/rk-isp10-config.h>
#include <media/v4l2-controls_rockchip.h>

#ifndef _RK_ISP10_IOCTL_H
#define _RK_ISP10_IOCTL_H

/* Private IOCTLs */
/* DPCC */
#define CIFISP_IOC_G_DPCC \
	_IOR('v', BASE_VIDIOC_PRIVATE + 0, struct cifisp_dpcc_config)
#define CIFISP_IOC_S_DPCC \
	_IOW('v', BASE_VIDIOC_PRIVATE + 1, struct cifisp_dpcc_config)
/* Black Level Subtraction */
#define CIFISP_IOC_G_BLS \
	_IOR('v', BASE_VIDIOC_PRIVATE + 2, struct cifisp_bls_config)
#define CIFISP_IOC_S_BLS \
	_IOW('v', BASE_VIDIOC_PRIVATE + 3, struct cifisp_bls_config)
/* Sensor DeGamma */
#define CIFISP_IOC_G_SDG \
	_IOR('v', BASE_VIDIOC_PRIVATE + 4, struct cifisp_sdg_config)
#define CIFISP_IOC_S_SDG \
	_IOW('v', BASE_VIDIOC_PRIVATE + 5, struct cifisp_sdg_config)
/* Lens Shading Correction */
#define CIFISP_IOC_G_LSC \
	_IOR('v', BASE_VIDIOC_PRIVATE + 6, struct cifisp_lsc_config)
#define CIFISP_IOC_S_LSC \
	_IOW('v', BASE_VIDIOC_PRIVATE + 7, struct cifisp_lsc_config)
/* Auto White Balance */
#define CIFISP_IOC_G_AWB_MEAS \
	_IOR('v', BASE_VIDIOC_PRIVATE + 8, struct cifisp_awb_meas_config)
#define CIFISP_IOC_S_AWB_MEAS \
	_IOW('v', BASE_VIDIOC_PRIVATE + 9, struct cifisp_awb_meas_config)
/* ISP Filtering( Sharpening & Noise reduction */
#define CIFISP_IOC_G_FLT \
	_IOR('v', BASE_VIDIOC_PRIVATE + 10, struct cifisp_flt_config)
#define CIFISP_IOC_S_FLT \
	_IOW('v', BASE_VIDIOC_PRIVATE + 11, struct cifisp_flt_config)
/* Bayer Demosaic */
#define CIFISP_IOC_G_BDM \
	_IOR('v', BASE_VIDIOC_PRIVATE + 12, struct cifisp_bdm_config)
#define CIFISP_IOC_S_BDM \
	_IOW('v', BASE_VIDIOC_PRIVATE + 13, struct cifisp_bdm_config)
/* Cross Talk correction */
#define CIFISP_IOC_G_CTK \
	_IOR('v', BASE_VIDIOC_PRIVATE + 14, struct cifisp_ctk_config)
#define CIFISP_IOC_S_CTK \
	_IOW('v', BASE_VIDIOC_PRIVATE + 15, struct cifisp_ctk_config)
/* Gamma Out Correction */
#define CIFISP_IOC_G_GOC \
	_IOR('v', BASE_VIDIOC_PRIVATE + 16, struct cifisp_goc_config)
#define CIFISP_IOC_S_GOC \
	_IOW('v', BASE_VIDIOC_PRIVATE + 17, struct cifisp_goc_config)
/* Histogram Measurement */
#define CIFISP_IOC_G_HST \
	_IOR('v', BASE_VIDIOC_PRIVATE + 18, struct cifisp_hst_config)
#define CIFISP_IOC_S_HST \
	_IOW('v', BASE_VIDIOC_PRIVATE + 19, struct cifisp_hst_config)
/* Auto Exposure Measurements */
#define CIFISP_IOC_G_AEC \
	_IOR('v', BASE_VIDIOC_PRIVATE + 20, struct cifisp_aec_config)
#define CIFISP_IOC_S_AEC \
	_IOW('v', BASE_VIDIOC_PRIVATE + 21, struct cifisp_aec_config)
#define CIFISP_IOC_G_BPL \
	_IOR('v', BASE_VIDIOC_PRIVATE + 22, struct cifisp_aec_config)
#define CIFISP_IOC_G_AWB_GAIN \
	_IOR('v', BASE_VIDIOC_PRIVATE + 23, struct cifisp_awb_gain_config)
#define CIFISP_IOC_S_AWB_GAIN \
	_IOW('v', BASE_VIDIOC_PRIVATE + 24, struct cifisp_awb_gain_config)
#define CIFISP_IOC_G_CPROC \
	_IOR('v', BASE_VIDIOC_PRIVATE + 25, struct cifisp_cproc_config)
#define CIFISP_IOC_S_CPROC \
	_IOW('v', BASE_VIDIOC_PRIVATE + 26, struct cifisp_cproc_config)
#define CIFISP_IOC_G_AFC \
	_IOR('v', BASE_VIDIOC_PRIVATE + 27, struct cifisp_afc_config)
#define CIFISP_IOC_S_AFC \
	_IOW('v', BASE_VIDIOC_PRIVATE + 28, struct cifisp_afc_config)
#define CIFISP_IOC_G_IE \
	_IOR('v', BASE_VIDIOC_PRIVATE + 29, struct cifisp_ie_config)
#define CIFISP_IOC_S_IE \
	_IOW('v', BASE_VIDIOC_PRIVATE + 30, struct cifisp_ie_config)
#define CIFISP_IOC_G_DPF \
	_IOR('v', BASE_VIDIOC_PRIVATE + 31, struct cifisp_dpf_config)
#define CIFISP_IOC_S_DPF \
	_IOW('v', BASE_VIDIOC_PRIVATE + 32, struct cifisp_dpf_config)
#define CIFISP_IOC_G_DPF_STRENGTH \
	_IOR('v', BASE_VIDIOC_PRIVATE + 33, struct cifisp_dpf_strength_config)
#define CIFISP_IOC_S_DPF_STRENGTH \
	_IOW('v', BASE_VIDIOC_PRIVATE + 34, struct cifisp_dpf_strength_config)
#define CIFISP_IOC_G_LAST_CONFIG \
	_IOR('v', BASE_VIDIOC_PRIVATE + 35, struct cifisp_last_capture_config)

/* CIF-ISP Private control IDs */
#define V4L2_CID_CIFISP_DPCC    (V4L2_CID_PRIVATE_BASE + CIFISP_DPCC_ID)
#define V4L2_CID_CIFISP_BLS    (V4L2_CID_PRIVATE_BASE + CIFISP_BLS_ID)
#define V4L2_CID_CIFISP_SDG    (V4L2_CID_PRIVATE_BASE + CIFISP_SDG_ID)
#define V4L2_CID_CIFISP_HST    (V4L2_CID_PRIVATE_BASE + CIFISP_HST_ID)
#define V4L2_CID_CIFISP_LSC    (V4L2_CID_PRIVATE_BASE + CIFISP_LSC_ID)
#define V4L2_CID_CIFISP_AWB_GAIN    (V4L2_CID_PRIVATE_BASE + CIFISP_AWB_GAIN_ID)
#define V4L2_CID_CIFISP_FLT    (V4L2_CID_PRIVATE_BASE + CIFISP_FLT_ID)
#define V4L2_CID_CIFISP_BDM    (V4L2_CID_PRIVATE_BASE + CIFISP_BDM_ID)
#define V4L2_CID_CIFISP_CTK    (V4L2_CID_PRIVATE_BASE + CIFISP_CTK_ID)
#define V4L2_CID_CIFISP_GOC    (V4L2_CID_PRIVATE_BASE + CIFISP_GOC_ID)
#define V4L2_CID_CIFISP_CPROC    (V4L2_CID_PRIVATE_BASE + CIFISP_CPROC_ID)
#define V4L2_CID_CIFISP_AFC    (V4L2_CID_PRIVATE_BASE + CIFISP_AFC_ID)
#define V4L2_CID_CIFISP_AWB_MEAS    (V4L2_CID_PRIVATE_BASE + CIFISP_AWB_ID)
#define V4L2_CID_CIFISP_IE    (V4L2_CID_PRIVATE_BASE + CIFISP_IE_ID)
#define V4L2_CID_CIFISP_AEC    (V4L2_CID_PRIVATE_BASE + CIFISP_AEC_ID)
#define V4L2_CID_CIFISP_DPF    (V4L2_CID_PRIVATE_BASE + CIFISP_DPF_ID)

/* Camera Sensors' running modes */
#define CI_MODE_PREVIEW	0x8000
#define CI_MODE_VIDEO	0x4000
#define CI_MODE_STILL_CAPTURE	0x2000
#define CI_MODE_CONTINUOUS	0x1000
#define CI_MODE_NONE	0x0000

/* Kernel API */
void cif_isp11_v4l2_s_frame_interval(
	unsigned int numerator,
	unsigned int denominator);
int cif_isp11_v4l2_g_frame_interval(
	unsigned int *numerator,
	unsigned int *denominator);
#endif
