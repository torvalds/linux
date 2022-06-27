/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef RK_VCM_HEAD_H
#define RK_VCM_HEAD_H

#define RK_VCM_HEAD_VERSION	KERNEL_VERSION(0, 0x02, 0x0)
/*
 * Focus position values:
 * 65 logical positions ( 0 - 64 )
 * where 64 is the setting for infinity and 0 for macro
 */
#define VCMDRV_MAX_LOG			64U
#define VCMDRV_SETZOOM_MAXCNT	300U

#define OF_CAMERA_VCMDRV_MAX_CURRENT	"rockchip,vcm-max-current"
#define OF_CAMERA_VCMDRV_START_CURRENT	"rockchip,vcm-start-current"
#define OF_CAMERA_VCMDRV_RATED_CURRENT	"rockchip,vcm-rated-current"
#define OF_CAMERA_VCMDRV_STEP_MODE	"rockchip,vcm-step-mode"
#define OF_CAMERA_VCMDRV_DLC_ENABLE	"rockchip,vcm-dlc-enable"
#define OF_CAMERA_VCMDRV_MCLK		"rockchip,vcm-mclk"
#define OF_CAMERA_VCMDRV_T_SRC		"rockchip,vcm-t-src"
#define OF_CAMERA_VCMDRV_T_DIV		"rockchip,vcm-t-div"
#define OF_CAMERA_VCMDRV_ADVANCED_MODE	"rockchip,vcm-adcanced-mode"
#define OF_CAMERA_VCMDRV_SAC_MODE	"rockchip,vcm-sac-mode"
#define OF_CAMERA_VCMDRV_SAC_TIME	"rockchip,vcm-sac-time"
#define OF_CAMERA_VCMDRV_PRESC		"rockchip,vcm-prescl"
#define OF_CAMERA_VCMDRV_NRC_EN		"rockchip,vcm-nrc-en"
#define OF_CAMERA_VCMDRV_NRC_MODE	"rockchip,vcm-nrc-mode"
#define OF_CAMERA_VCMDRV_NRC_PRESET	"rockchip,vcm-nrc-preset"
#define OF_CAMERA_VCMDRV_NRC_INFL	"rockchip,vcm-nrc-infl"
#define OF_CAMERA_VCMDRV_NRC_TIME	"rockchip,vcm-nrc-time"
#define VCMDRV_SETZOOM_MAXCNT		300U

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

#define RK_VIDIOC_FOCUS_SET_BACKLASH \
	_IOR('V', BASE_VIDIOC_PRIVATE + 8, unsigned int)
#define RK_VIDIOC_IRIS_SET_BACKLASH \
	_IOR('V', BASE_VIDIOC_PRIVATE + 9, unsigned int)
#define RK_VIDIOC_ZOOM_SET_BACKLASH \
	_IOR('V', BASE_VIDIOC_PRIVATE + 10, unsigned int)

#define RK_VIDIOC_ZOOM1_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 11, struct rk_cam_vcm_tim)
#define RK_VIDIOC_ZOOM1_CORRECTION \
	_IOR('V', BASE_VIDIOC_PRIVATE + 12, unsigned int)
#define RK_VIDIOC_ZOOM1_SET_BACKLASH \
	_IOR('V', BASE_VIDIOC_PRIVATE + 13, unsigned int)

#define RK_VIDIOC_ZOOM_SET_POSITION \
	_IOW('V', BASE_VIDIOC_PRIVATE + 14, struct rk_cam_set_zoom)
#define RK_VIDIOC_FOCUS_SET_POSITION \
	_IOW('V', BASE_VIDIOC_PRIVATE + 15, struct rk_cam_set_focus)
#define RK_VIDIOC_MODIFY_POSITION \
	_IOW('V', BASE_VIDIOC_PRIVATE + 16, struct rk_cam_modify_pos)

#define RK_VIDIOC_SET_VCM_MAX_LOGICALPOS \
	_IOW('V', BASE_VIDIOC_PRIVATE + 17, unsigned int)

#define RK_VIDIOC_COMPAT_VCM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct rk_cam_compat_vcm_tim)
#define RK_VIDIOC_COMPAT_IRIS_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 1, struct rk_cam_compat_vcm_tim)
#define RK_VIDIOC_COMPAT_ZOOM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, struct rk_cam_compat_vcm_tim)
#define RK_VIDIOC_COMPAT_ZOOM1_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 11, struct rk_cam_compat_vcm_tim)

struct rk_cam_modify_pos {
	s32 focus_pos;
	s32 zoom_pos;
	s32 zoom1_pos;
};

struct rk_cam_set_focus {
	bool is_need_reback;
	s32 focus_pos;
};

struct rk_cam_zoom_pos {
	s32 zoom_pos;
	s32 focus_pos;
};

struct rk_cam_set_zoom {
	bool is_need_zoom_reback;
	bool is_need_focus_reback;
	u32 setzoom_cnt;
	struct rk_cam_zoom_pos zoom_pos[VCMDRV_SETZOOM_MAXCNT];
};

struct rk_cam_vcm_tim {
	struct __kernel_old_timeval vcm_start_t;
	struct __kernel_old_timeval vcm_end_t;
};

struct rk_cam_compat_vcm_tim {
	struct old_timeval32 vcm_start_t;
	struct old_timeval32 vcm_end_t;
};

struct rk_cam_vcm_cfg {
	int start_ma;
	int rated_ma;
	int step_mode;
};

#endif /* RK_VCM_HEAD_H */

