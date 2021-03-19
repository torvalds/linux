/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef RK_VCM_HEAD_H
#define RK_VCM_HEAD_H

#define RK_VCM_HEAD_VERSION	KERNEL_VERSION(0, 0x01, 0x0)
/*
 * Focus position values:
 * 65 logical positions ( 0 - 64 )
 * where 64 is the setting for infinity and 0 for macro
 */
#define VCMDRV_MAX_LOG			64U

#define OF_CAMERA_VCMDRV_MAX_CURRENT	"rockchip,vcm-max-current"
#define OF_CAMERA_VCMDRV_START_CURRENT	"rockchip,vcm-start-current"
#define OF_CAMERA_VCMDRV_RATED_CURRENT	"rockchip,vcm-rated-current"
#define OF_CAMERA_VCMDRV_STEP_MODE	"rockchip,vcm-step-mode"
#define OF_CAMERA_VCMDRV_DLC_ENABLE	"rockchip,vcm-dlc-enable"
#define OF_CAMERA_VCMDRV_MCLK		"rockchip,vcm-mclk"
#define OF_CAMERA_VCMDRV_T_SRC		"rockchip,vcm-t-src"

#define RK_VIDIOC_VCM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct rk_cam_vcm_tim)
#define RK_VIDIOC_IRIS_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 1, struct rk_cam_vcm_tim)
#define RK_VIDIOC_ZOOM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, struct rk_cam_vcm_tim)

#define RK_VIDIOC_GET_VCM_CFG \
	_IOR('V', BASE_VIDIOC_PRIVATE + 3, struct rk_cam_vcm_cfg)
#define RK_VIDIOC_SET_VCM_CFG \
	_IOW('V', BASE_VIDIOC_PRIVATE + 4, struct rk_cam_vcm_cfg)

#define RK_VIDIOC_FOCUS_CORRECTION \
	_IOR('V', BASE_VIDIOC_PRIVATE + 5, unsigned int)
#define RK_VIDIOC_IRIS_CORRECTION \
	_IOR('V', BASE_VIDIOC_PRIVATE + 6, unsigned int)
#define RK_VIDIOC_ZOOM_CORRECTION \
	_IOR('V', BASE_VIDIOC_PRIVATE + 7, unsigned int)

#ifdef CONFIG_COMPAT
#define RK_VIDIOC_COMPAT_VCM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct rk_cam_compat_vcm_tim)
#define RK_VIDIOC_COMPAT_IRIS_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 1, struct rk_cam_compat_vcm_tim)
#define RK_VIDIOC_COMPAT_ZOOM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, struct rk_cam_compat_vcm_tim)
#endif

struct rk_cam_vcm_tim {
	struct timeval vcm_start_t;
	struct timeval vcm_end_t;
};

#ifdef CONFIG_COMPAT
struct rk_cam_compat_vcm_tim {
	struct compat_timeval vcm_start_t;
	struct compat_timeval vcm_end_t;
};
#endif

struct rk_cam_vcm_cfg {
	int start_ma;
	int rated_ma;
	int step_mode;
};

#endif /* RK_VCM_HEAD_H */

