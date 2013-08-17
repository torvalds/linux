/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_EXYNOS_HWC_H_
#define ANDROID_EXYNOS_HWC_H_
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <s3c-fb.h>

#if defined(ANDROID)
#include <EGL/egl.h>
#endif

#define HWC_REMOVE_DEPRECATED_VERSIONS 1

#if defined(ANDROID)
#include <cutils/compiler.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#else
#include <hardware/gralloc.h>
#endif
#include <hardware/hwcomposer.h>
#include <hardware_legacy/uevent.h>
#if defined(ANDROID)
#include <utils/String8.h>
#include <utils/Vector.h>

#include <sync/sync.h>

#include "ion.h"
#include "gralloc_priv.h"
#include "exynos_gscaler.h"
#include "exynos_format.h"
#include "exynos_v4l2.h"
#include "s5p_tvout_v4l2.h"
#include "ExynosHWCModule.h"
#include "ExynosRect.h"
#include "videodev2.h"
#else
#include <sys/types.h>
#include <system/graphics.h>
#include <hardware/hwcomposer.h>
#include <exynos_gscaler.h>
#include <gralloc_priv.h>
#include <libhwcmodule/ExynosHWCModule.h>
#endif

#ifdef USE_FB_PHY_LINEAR
const size_t NUM_HW_WIN_FB_PHY = 2;
#undef DUAL_VIDEO_OVERLAY_SUPPORT
#endif
const size_t NUM_HW_WINDOWS = 5;
const size_t NO_FB_NEEDED = NUM_HW_WINDOWS + 1;
const size_t MAX_PIXELS = 2560 * 1600 * 2;
const size_t GSC_W_ALIGNMENT = 16;
const size_t GSC_H_ALIGNMENT = 16;
const size_t GSC_DST_H_ALIGNMENT_RGB888 = 1;
#ifdef DUAL_VIDEO_OVERLAY_SUPPORT
const size_t FIMD_GSC_IDX = 0;
const size_t FIMD_GSC_SEC_IDX = 1;
const size_t FIMD_GSC_SBS_IDX = 2;
const size_t FIMD_GSC_TB_IDX = 3;
const size_t FIMD_GSC_FINAL_INDEX = 3;
const size_t HDMI_GSC_IDX = 4;
const size_t HDMI_GSC_SBS_IDX = 5;
const size_t HDMI_GSC_TB_IDX = 6;
const int FIMD_GSC_USAGE_IDX[] = {FIMD_GSC_IDX, FIMD_GSC_SEC_IDX,
                                                    FIMD_GSC_SBS_IDX, FIMD_GSC_TB_IDX};
const int AVAILABLE_GSC_UNITS[] = { 0, 3, 0, 0, 3, 3, 3 };
#else
const size_t FIMD_GSC_IDX = 0;
const size_t HDMI_GSC_IDX = 1;
const size_t FIMD_GSC_SBS_IDX = 2;
const size_t FIMD_GSC_TB_IDX = 3;
const size_t HDMI_GSC_SBS_IDX = 4;
const size_t HDMI_GSC_TB_IDX = 5;
const int AVAILABLE_GSC_UNITS[] = { 0, 3, 0, 0, 3, 3 };
#endif
const size_t NUM_GSC_UNITS = sizeof(AVAILABLE_GSC_UNITS) /
        sizeof(AVAILABLE_GSC_UNITS[0]);
const size_t BURSTLEN_BYTES = 16 * 8;
const size_t NUM_HDMI_BUFFERS = 3;

#ifdef SKIP_STATIC_LAYER_COMP
#define NUM_VIRT_OVER   5
#endif

#ifdef GSC_VIDEO
#define NUM_VIRT_OVER_HDMI 5
#endif

#ifdef HWC_SERVICES
#if defined(ANDROID)
#include "../libhwcService/ExynosHWCService.h"
namespace android {
class ExynosHWCService;
}
#endif
#endif

#define GSC_SKIP_DUPLICATE_FRAME_PROCESSING

#ifdef HWC_DYNAMIC_RECOMPOSITION
#define HWC_FIMD_BW_TH  1   /* valid range 1 to 5 */
#define HWC_FPS_TH          3    /* valid range 1 to 60 */
#define VSYNC_INTERVAL (1000000000.0 / 60)
typedef enum _COMPOS_MODE_SWITCH {
    NO_MODE_SWITCH,
    HWC_2_GLES = 1,
    GLES_2_HWC,
} HWC_COMPOS_MODE_SWITCH;
#endif

#ifdef USES_WFD
/* This value will be changed to 1080p if needed */
#define EXYNOS5_WFD_DEFAULT_WIDTH       1280
#define EXYNOS5_WFD_DEFAULT_HEIGHT      720
#define EXYNOS5_WFD_FORMAT              HAL_PIXEL_FORMAT_YCbCr_420_SP
#define EXYNOS5_WFD_OUTPUT_ALIGNMENT    16

#endif

struct exynos5_hwc_composer_device_1_t;

#ifdef SUPPORT_GSC_LOCAL_PATH
#define GSC_OUT_WA /* sequence change */
#define FORCEFB_YUVLAYER /* video or camera preview */
#define NUM_CONFIG_STABLE   100
#endif
#ifdef FORCEFB_YUVLAYER
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t fw;
    uint32_t fh;
    uint32_t format;
    uint32_t rot;
    uint32_t cacheable;
    uint32_t drmMode;
} video_layer_config;
#endif

struct exynos5_gsc_map_t {
    enum {
        GSC_NONE = 0,
        GSC_M2M,
        // TODO: GSC_LOCAL_PATH
#ifdef SUPPORT_GSC_LOCAL_PATH
        GSC_LOCAL,
#endif
    } mode;
    int idx;
};

struct exynos5_hwc_post_data_t {
    int                 overlay_map[NUM_HW_WINDOWS];
    exynos5_gsc_map_t   gsc_map[NUM_HW_WINDOWS];
    size_t              fb_window;
};

const size_t NUM_GSC_DST_BUFS = 3;
struct exynos5_gsc_data_t {
    void            *gsc;
    exynos_gsc_img  src_cfg;
    exynos_gsc_img  mid_cfg;
    exynos_gsc_img  dst_cfg;
    buffer_handle_t dst_buf[NUM_GSC_DST_BUFS];
    buffer_handle_t mid_buf[NUM_GSC_DST_BUFS];
    int             dst_buf_fence[NUM_GSC_DST_BUFS];
    int             mid_buf_fence[NUM_GSC_DST_BUFS];
    size_t          current_buf;
#ifdef SUPPORT_GSC_LOCAL_PATH
    int             gsc_mode;
#endif
#ifdef GSC_SKIP_DUPLICATE_FRAME_PROCESSING
    uint32_t    last_gsc_lay_hnd;
#endif
};

struct hdmi_layer_t {
    int     id;
    int     fd;
    bool    enabled;
    exynos_gsc_img  cfg;

    bool    streaming;
    size_t  current_buf;
    size_t  queued_buf;
};

#if defined(USES_WFD)
#include "FimgApi.h"
#endif

struct exynos5_hwc_composer_device_1_t {
    hwc_composer_device_1_t base;

    int                     fd;
    int                     vsync_fd;
    exynos5_hwc_post_data_t bufs;

    const private_module_t  *gralloc_module;
    alloc_device_t          *alloc_device;
    const hwc_procs_t       *procs;
    pthread_t               vsync_thread;
    int                     force_gpu;

    int32_t                 xres;
    int32_t                 yres;
    int32_t                 xdpi;
    int32_t                 ydpi;
    int32_t                 vsync_period;

    int  hdmi_mixer0;
    bool hdmi_hpd;
    bool hdmi_enabled;
    bool hdmi_blanked;
    int  hdmi_w;
    int  hdmi_h;

#ifdef USES_WFD
    bool wfd_hpd;
    bool wfd_enabled;
    bool wfd_blanked;
    int  wfd_w;
    int  wfd_h;
    int  wfd_disp_w;
    int  wfd_disp_h;
    int  wfd_buf_fd[3];
    struct wfd_layer_t      wfd_info;
    int  wfd_locked_fd;
    bool mPresentationMode;
    int wfd_skipping;
    int wfd_sleepctrl;
    int wfd_force_transform;
    struct v4l2_rect wfd_disp_rect;
#endif

    hdmi_layer_t            hdmi_layers[2];

    exynos5_gsc_data_t      gsc[NUM_GSC_UNITS];

    struct s3c_fb_win_config last_config[NUM_HW_WINDOWS];
    size_t                  last_fb_window;
    const void              *last_handles[NUM_HW_WINDOWS];
    exynos5_gsc_map_t       last_gsc_map[NUM_HW_WINDOWS];
#ifdef SKIP_STATIC_LAYER_COMP
    const void              *last_lay_hnd[NUM_VIRT_OVER];
    int                     last_ovly_win_idx;
    int                     last_ovly_lay_idx;
    int                     virtual_ovly_flag;
#endif

#ifdef HWC_SERVICES

#define S3D_ERROR -1
#define HDMI_PRESET_DEFAULT V4L2_DV_1080P60
#define HDMI_PRESET_ERROR -1

#if defined(ANDROID)
    android::ExynosHWCService   *mHWCService;
#endif
    int mHdmiPreset;
    int mHdmiCurrentPreset;
    bool mHdmiResolutionChanged;
    bool mHdmiResolutionHandled;
#if defined(S3D_SUPPORT)
    int mS3DMode;
#endif
    bool mUseSubtitles;
    int video_playback_status;
#endif

#ifdef HWC_DYNAMIC_RECOMPOSITION
    int VsyncInterruptStatus;
    int CompModeSwitch;
    uint64_t LastVsyncTimeStamp;
    uint64_t LastModeSwitchTimeStamp;
    int invalidateStatus;
    int needInvalidate;
    int totPixels;
    int setCallCnt;
    pthread_t   vsync_stat_thread;
    int vsyn_event_cnt;
    int invalid_trigger;
    volatile bool vsync_stat_thread_flag;
#endif

#if defined(USES_CEC)
    int mCecFd;
    int mCecPaddr;
    int mCecLaddr;
#endif

#ifdef GSC_OUT_WA
    bool                    need_reqbufs;
    int                     wait_vsync_cnt;
#endif

#ifdef FORCEFB_YUVLAYER
    bool                    forcefb_yuvlayer;
    int                     count_sameconfig;
    /* g3d = 0, gsc = 1 */
    int                     configmode;
    int                     gsc_use;
    video_layer_config      prev_src_config;
    video_layer_config      prev_dst_config;
#endif

    int                     gsc_layers;

    bool                    force_mirror_mode;
    int                     ext_fbt_transform;                  /* HAL_TRANSFORM_ROT_XXX */
    bool                    external_display_pause;
    bool                    local_external_display_pause;
    bool                    popup_play_drm_contents;
    bool                    contents_has_drm_surface;

    int  is_fb_layer;
    int  is_video_layer;
    int  fb_started;
    int  video_started;

#ifdef GSC_VIDEO
    const void              *last_lay_hnd_hdmi[NUM_VIRT_OVER_HDMI];
    int                     virtual_ovly_flag_hdmi;
#endif

    bool                    need_gsc_op_twice;
    bool                    is_3layer_overlapped;
};

#if defined(HWC_SERVICES)
enum {
    S3D_MODE_DISABLED = 0,
    S3D_MODE_READY,
    S3D_MODE_RUNNING,
    S3D_MODE_STOPPING,
};

enum {
    S3D_FB = 0,
    S3D_SBS,
    S3D_TB,
    S3D_NONE,
};
#endif
enum {
    NO_DRM = 0,
    NORMAL_DRM,
    SECURE_DRM,
};
#endif
