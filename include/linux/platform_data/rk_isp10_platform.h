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
#ifndef _CIF_ISP10_PLATFORM_H
#define _CIF_ISP10_PLATFORM_H
#include <linux/videodev2.h>

#define CIF_ISP10_SOC_RK3288	"rk3288"
#define CIF_ISP10_SOC_RK3368	"rk3368"
#define CIF_ISP10_SOC_RK3399	"rk3399"

#define DRIVER_NAME "rkisp10"
#define ISP_VDEV_NAME DRIVER_NAME  "_ispdev"
#define SP_VDEV_NAME DRIVER_NAME   "_selfpath"
#define MP_VDEV_NAME DRIVER_NAME   "_mainpath"
#define DMA_VDEV_NAME DRIVER_NAME  "_dmapath"

enum pltfrm_cam_signal_polarity {
	PLTFRM_CAM_SIGNAL_HIGH_LEVEL = 0,
	PLTFRM_CAM_SIGNAL_LOW_LEVEL = 1,
};

enum pltfrm_cam_sample_type {
	PLTFRM_CAM_SDR_NEG_EDG = 0x10000001,
	PLTFRM_CAM_SDR_POS_EDG = 0x10000002,
	PLTFRM_CAM_DDR         = 0x20000000
};

enum pltfrm_cam_itf_type {
	PLTFRM_CAM_ITF_MIPI     = 0x10000000,
	PLTFRM_CAM_ITF_BT601_8  = 0x20000071,
	PLTFRM_CAM_ITF_BT656_8  = 0x20000072,
	PLTFRM_CAM_ITF_BT601_10 = 0x20000091,
	PLTFRM_CAM_ITF_BT656_10 = 0x20000092,
	PLTFRM_CAM_ITF_BT601_12 = 0x200000B1,
	PLTFRM_CAM_ITF_BT656_12 = 0x200000B2,
	PLTFRM_CAM_ITF_BT601_16 = 0x200000F1,
	PLTFRM_CAM_ITF_BT656_16 = 0x200000F2,
	PLTFRM_CAM_ITF_BT656_8I = 0x20000172
};

#define PLTFRM_CAM_ITF_MAIN_MASK   0xf0000000
#define PLTFRM_CAM_ITF_SUB_MASK    0x0000000f
#define PLTFRM_CAM_ITF_DVP_BW_MASK 0x000000f0
#define PLTFRM_CAM_ITF_INTERLACE_MASK	0x00000100

#define PLTFRM_CAM_ITF_IS_MIPI(a)    \
		(((a) & PLTFRM_CAM_ITF_MAIN_MASK) == 0x10000000)
#define PLTFRM_CAM_ITF_IS_DVP(a)    \
		(((a) & PLTFRM_CAM_ITF_MAIN_MASK) == 0x20000000)
#define PLTFRM_CAM_ITF_IS_BT656(a)	(PLTFRM_CAM_ITF_IS_DVP(a) &&\
		(((a) & PLTFRM_CAM_ITF_SUB_MASK) == 0x02))
#define PLTFRM_CAM_ITF_IS_BT601(a)	(PLTFRM_CAM_ITF_IS_DVP(a) &&\
		(((a) & PLTFRM_CAM_ITF_SUB_MASK) == 0x01))
#define PLTFRM_CAM_ITF_DVP_BW(a)    \
		((((a) & PLTFRM_CAM_ITF_DVP_BW_MASK) >> 4) + 1)
#define PLTFRM_CAM_ITF_INTERLACE(a)   \
		(((a) & PLTFRM_CAM_ITF_INTERLACE_MASK) == 0x00000100)

struct pltfrm_cam_mipi_config {
	u32 dphy_index;
	u32 vc;
	u32 nb_lanes;
	u32 bit_rate;
};

struct pltfrm_cam_dvp_config {
	enum pltfrm_cam_signal_polarity vsync;
	enum pltfrm_cam_signal_polarity hsync;
	enum pltfrm_cam_sample_type pclk;
};

struct pltfrm_cam_itf {
	enum pltfrm_cam_itf_type type;

	union {
		struct pltfrm_cam_mipi_config mipi;
		struct pltfrm_cam_dvp_config dvp;
	} cfg;
	unsigned int mclk_hz;
};

#define PLTFRM_CAM_ITF_MIPI_CFG(v, nb, br, mk)\
	.itf_cfg = {\
		.type =  PLTFRM_CAM_ITF_MIPI,\
			.cfg = {\
				.mipi = {\
					.dphy_index = 0,\
					.vc = v,\
					.nb_lanes = nb,\
					.bit_rate = br,\
				} \
			},\
		.mclk_hz = mk\
	}
#define PLTFRM_CAM_ITF_DVP_CFG(ty, vs, hs, ck, mk)\
	.itf_cfg = {\
		.type =  ty,\
		.cfg = {\
			.dvp = {\
				.vsync = vs,\
				.hsync = hs,\
				.pclk = ck,\
			} \
		},\
		.mclk_hz = mk\
	}

#define PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE    0x00
#define PLTFRM_CIFCAM_G_ITF_CFG    \
				(PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE + 1)
#define PLTFRM_CIFCAM_G_DEFRECT    \
				(PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE + 2)
#define PLTFRM_CIFCAM_ATTACH    \
				(PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE + 3)
#define PLTFRM_CIFCAM_SET_VCM_POS    \
				(PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE + 4)
#define PLTFRM_CIFCAM_GET_VCM_POS    \
				(PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE + 5)
#define PLTFRM_CIFCAM_GET_VCM_MOVE_RES    \
				(PLTFRM_CIFCAM_IOCTL_INTERNAL_BASE + 6)

struct pltfrm_cam_vcm_tim {
	struct timeval vcm_start_t;
	struct timeval vcm_end_t;
};

struct pltfrm_cam_defrect {
	unsigned int width;
	unsigned int height;
	struct v4l2_rect defrect;
};

enum pltfrm_soc_cfg_cmd {
	PLTFRM_MCLK_CFG = 0,
	PLTFRM_MIPI_DPHY_CFG,

	PLTFRM_CLKEN,
	PLTFRM_CLKDIS,
	PLTFRM_CLKRST,

	PLTFRM_SOC_INIT
};

enum pltfrm_soc_io_voltage {
	PLTFRM_IO_1V8 = 0,
	PLTFRM_IO_3V3 = 1
};

enum pltfrm_soc_drv_strength {
	PLTFRM_DRV_STRENGTH_0 = 0,
	PLTFRM_DRV_STRENGTH_1 = 1,
	PLTFRM_DRV_STRENGTH_2 = 2,
	PLTFRM_DRV_STRENGTH_3 = 3

};

struct pltfrm_soc_init_para {
	struct platform_device *pdev;
	void __iomem *isp_base;
};

struct pltfrm_soc_mclk_para {
	enum pltfrm_soc_io_voltage io_voltage;
	enum pltfrm_soc_drv_strength drv_strength;
};

struct pltfrm_soc_cfg_para {
	enum pltfrm_soc_cfg_cmd cmd;
	void **isp_config;
	void *cfg_para;
};

struct pltfrm_soc_cfg {
	char name[32];
	void *isp_config;
	int (*soc_cfg)(struct pltfrm_soc_cfg_para *cfg);
};

int pltfrm_rk3288_cfg(struct pltfrm_soc_cfg_para *cfg);
int pltfrm_rk3399_cfg(struct pltfrm_soc_cfg_para *cfg);


#endif
