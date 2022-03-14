/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DRM_H__
#define __VS_DRM_H__

#include "drm.h"

enum drm_vs_degamma_mode {
    VS_DEGAMMA_DISABLE = 0,
    VS_DEGAMMA_BT709 = 1,
    VS_DEGAMMA_BT2020 = 2,
};

enum drm_vs_sync_dc_mode {
    VS_SINGLE_DC = 0,
    VS_MULTI_DC_PRIMARY = 1,
    VS_MULTI_DC_SECONDARY = 2,
};

enum drm_vs_mmu_prefetch_mode {
    VS_MMU_PREFETCH_DISABLE = 0,
    VS_MMU_PREFETCH_ENABLE = 1,
};

struct drm_vs_watermark {
    __u32 watermark;
    __u8 qos_low;
    __u8 qos_high;
};

struct drm_vs_color_mgmt {
    __u32 colorkey;
    __u32 colorkey_high;
    __u32 clear_value;
    bool  clear_enable;
    bool  transparency;
};

struct drm_vs_roi {
    bool enable;
    __u16 roi_x;
    __u16 roi_y;
    __u16 roi_w;
    __u16 roi_h;
};

#endif /* __VS_DRM_H__ */
