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
#include "ExynosHWCService.h"
#include "exynos_v4l2.h"
#include "videodev2_exynos_media.h"

#define HWC_SERVICE_DEBUG 1

namespace android {

ANDROID_SINGLETON_STATIC_INSTANCE(ExynosHWCService);

ExynosHWCService::ExynosHWCService() :
    mHWCService(NULL),
    mHWCCtx(NULL)
{
   ALOGD_IF(HWC_SERVICE_DEBUG, "ExynosHWCService Constructor is called");
}

ExynosHWCService::~ExynosHWCService()
{
   ALOGD_IF(HWC_SERVICE_DEBUG, "ExynosHWCService Destructor is called");
}

int ExynosHWCService::setWFDMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
#ifdef TV_PRIMARY
#ifdef USES_WFD
    mHWCCtx->wfd_hpd = !!mode;
    mHWCCtx->procs->invalidate(mHWCCtx->procs);

    return NO_ERROR;
#else
    return INVALID_OPERATION;
#endif
#else
#ifdef USES_WFD
    if (mHWCCtx->hdmi_hpd != true) {
        mHWCCtx->wfd_hpd = !!mode;
        mHWCCtx->procs->invalidate(mHWCCtx->procs);
        mHWCCtx->wfd_sleepctrl = true;
    } else {
        /* HDMI and WFD runs exclusively */
        ALOGE_IF(HWC_SERVICE_DEBUG, "External Display was already enabled as HDMI.");
        mHWCCtx->wfd_hpd = false;
        return INVALID_OPERATION;
    }
    return NO_ERROR;
#else
    return INVALID_OPERATION;
#endif
#endif
}

int ExynosHWCService::setWFDOutputResolution(unsigned int width, unsigned int height,
                                             unsigned int disp_w, unsigned int disp_h)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::width=%d, height=%d", __func__, width, height);

#ifdef USES_WFD
    mHWCCtx->wfd_w = width;
    mHWCCtx->wfd_h = height;
    mHWCCtx->wfd_disp_w = disp_w;
    mHWCCtx->wfd_disp_h = disp_h;
    return NO_ERROR;
#else
    return INVALID_OPERATION;
#endif
}

void ExynosHWCService::setWFDSleepCtrl(bool black)
{
#ifdef USES_WFD
    if (mHWCCtx->wfd_enabled)
        mHWCCtx->wfd_sleepctrl = !!black;
#endif
}

int ExynosHWCService::setExtraFBMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
    return NO_ERROR;
}

int ExynosHWCService::setCameraMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
    return NO_ERROR;
}

int ExynosHWCService::setForceMirrorMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
    mHWCCtx->force_mirror_mode = mode;
    mHWCCtx->procs->invalidate(mHWCCtx->procs);
    return NO_ERROR;
}

int ExynosHWCService::setVideoPlayStatus(unsigned int status)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::status=%d", __func__, status);
    if (mHWCCtx)
        mHWCCtx->video_playback_status = status;

    return NO_ERROR;
}

int ExynosHWCService::setExternalDisplayPause(bool onoff)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::onoff=%d", __func__, onoff);
    if (mHWCCtx)
        mHWCCtx->external_display_pause = onoff;

    return NO_ERROR;
}

int ExynosHWCService::setDispOrientation(unsigned int transform)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%x", __func__, transform);
    return NO_ERROR;
}

int ExynosHWCService::setProtectionMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
    return NO_ERROR;
}

int ExynosHWCService::setExternalDispLayerNum(unsigned int num)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, num);
    return NO_ERROR;
}

int ExynosHWCService::setForceGPU(unsigned int on)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::on/off=%d", __func__, on);
    mHWCCtx->force_gpu = on;
    mHWCCtx->procs->invalidate(mHWCCtx->procs);
    return NO_ERROR;
}

int ExynosHWCService::setExternalUITransform(unsigned int transform)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::transform=%d", __func__, transform);
    mHWCCtx->ext_fbt_transform = transform;
    mHWCCtx->procs->invalidate(mHWCCtx->procs);
    return NO_ERROR;
}

int ExynosHWCService::getExternalUITransform(void)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    return mHWCCtx->ext_fbt_transform;
}

int ExynosHWCService::setWFDOutputTransform(unsigned int transform)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::transform=%d", __func__, transform);
#ifdef USES_WFD
    mHWCCtx->wfd_force_transform = transform;
    mHWCCtx->procs->invalidate(mHWCCtx->procs);
    return NO_ERROR;
#else
    return INVALID_OPERATION;
#endif
}

int ExynosHWCService::getWFDOutputTransform(void)
{
#ifdef USES_WFD
    return mHWCCtx->wfd_force_transform;
#else
    return INVALID_OPERATION;
#endif
}

void ExynosHWCService::setHdmiResolution(int resolution, int s3dMode)
{
    if (resolution == 0)
        resolution = mHWCCtx->mHdmiCurrentPreset;
#if defined(S3D_SUPPORT)
    if (s3dMode == S3D_NONE) {
#endif
        if (mHWCCtx->mHdmiCurrentPreset == resolution)
            return;
        mHWCCtx->mHdmiPreset = resolution;
        mHWCCtx->mHdmiResolutionChanged = true;
        mHWCCtx->procs->invalidate(mHWCCtx->procs);
        return;
#if defined(S3D_SUPPORT)
    }

    switch (resolution) {
    case HDMI_720P_60:
        resolution = S3D_720P_60_BASE + s3dMode;
        break;
    case HDMI_720P_59_94:
        resolution = S3D_720P_59_94_BASE + s3dMode;
        break;
    case HDMI_720P_50:
        resolution = S3D_720P_50_BASE + s3dMode;
        break;
    case HDMI_1080P_24:
        resolution = S3D_1080P_24_BASE + s3dMode;
        break;
    case HDMI_1080P_23_98:
        resolution = S3D_1080P_23_98_BASE + s3dMode;
        break;
    case HDMI_1080P_30:
        resolution = S3D_1080P_30_BASE + s3dMode;
        break;
    case HDMI_1080I_60:
        if (s3dMode != S3D_SBS)
            return;
        resolution = V4L2_DV_1080I60_SB_HALF;
        break;
    case HDMI_1080I_59_94:
        if (s3dMode != S3D_SBS)
            return;
        resolution = V4L2_DV_1080I59_94_SB_HALF;
        break;
    case HDMI_1080P_60:
        if (s3dMode != S3D_SBS && s3dMode != S3D_TB)
            return;
        resolution = S3D_1080P_60_BASE + s3dMode;
        break;
    default:
        return;
    }
    mHWCCtx->mHdmiPreset = resolution;
    mHWCCtx->mHdmiResolutionChanged = true;
    mHWCCtx->mS3DMode = S3D_MODE_READY;
    mHWCCtx->procs->invalidate(mHWCCtx->procs);
#endif
}

void ExynosHWCService::setHdmiCableStatus(int status)
{
    mHWCCtx->hdmi_hpd = !!status;
}

void ExynosHWCService::setHdmiHdcp(int status)
{
    if (exynos_v4l2_s_ctrl(mHWCCtx->hdmi_layers[1].fd, V4L2_CID_TV_HDCP_ENABLE,
                           !!status) < 0)
        ALOGE("%s: s_ctrl(CID_TV_HDCP_ENABLE) failed %d", __func__, errno);
}

void ExynosHWCService::setHdmiAudioChannel(uint32_t channels)
{
    if (exynos_v4l2_s_ctrl(mHWCCtx->hdmi_layers[0].fd,
            V4L2_CID_TV_SET_NUM_CHANNELS, channels) < 0)
        ALOGE("%s: failed to set audio channels", __func__);
}

void ExynosHWCService::setHdmiSubtitles(bool use)
{
    mHWCCtx->mUseSubtitles = use;
}

void ExynosHWCService::setPresentationMode(bool use)
{
#ifdef USES_WFD
    mHWCCtx->mPresentationMode = !!use;
#endif
}

int ExynosHWCService::getWFDMode()
{
#ifdef USES_WFD
    return !!mHWCCtx->wfd_hpd;
#else
    return INVALID_OPERATION;
#endif
}

void ExynosHWCService::getWFDOutputResolution(unsigned int *width, unsigned int *height)
{
#ifdef USES_WFD
    *width  = ALIGN(mHWCCtx->wfd_w, EXYNOS5_WFD_OUTPUT_ALIGNMENT);
    *height = mHWCCtx->wfd_h;
#else
    *width  = 0;
    *height = 0;
#endif
}

int ExynosHWCService::getWFDOutputInfo(int *fd1, int *fd2, struct wfd_layer_t *wfd_info)
{
#ifdef USES_WFD
    if (mHWCCtx->wfd_buf_fd[0] && mHWCCtx->wfd_buf_fd[1]) {
        *fd1 = mHWCCtx->wfd_locked_fd = mHWCCtx->wfd_buf_fd[0];
        *fd2 = mHWCCtx->wfd_buf_fd[1];
        memcpy(wfd_info, &mHWCCtx->wfd_info, sizeof(struct wfd_layer_t));
        return NO_ERROR;
    } else {
        *fd1 = *fd2 = 0;
        ALOGE("WFD Status FD=%d, w=%d, h=%d, disp_w=%d, disp_h=%d, \
                   hpd=%d, enabled=%d, blanked=%d",
                   *fd1, mHWCCtx->wfd_w, mHWCCtx->wfd_h,
                   mHWCCtx->wfd_disp_w, mHWCCtx->wfd_disp_h,
                   mHWCCtx->wfd_hpd, mHWCCtx->wfd_enabled, mHWCCtx->wfd_blanked);
        return BAD_VALUE;
    }
#endif
    return INVALID_OPERATION;
}

int ExynosHWCService::getPresentationMode()
{
#ifdef USES_WFD
    return !!mHWCCtx->mPresentationMode;
#else
    return INVALID_OPERATION;
#endif
}

void ExynosHWCService::getHdmiResolution(uint32_t *width, uint32_t *height)
{
    switch (mHWCCtx->mHdmiCurrentPreset) {
    case V4L2_DV_480P59_94:
    case V4L2_DV_480P60:
        *width = 640;
        *height = 480;
        break;
    case V4L2_DV_576P50:
        *width = 720;
        *height = 576;
        break;
    case V4L2_DV_720P24:
    case V4L2_DV_720P25:
    case V4L2_DV_720P30:
    case V4L2_DV_720P50:
    case V4L2_DV_720P59_94:
    case V4L2_DV_720P60:
    case V4L2_DV_720P60_FP:
    case V4L2_DV_720P60_SB_HALF:
    case V4L2_DV_720P60_TB:
    case V4L2_DV_720P59_94_FP:
    case V4L2_DV_720P59_94_SB_HALF:
    case V4L2_DV_720P59_94_TB:
    case V4L2_DV_720P50_FP:
    case V4L2_DV_720P50_SB_HALF:
    case V4L2_DV_720P50_TB:
        *width = 1280;
        *height = 720;
        break;
    case V4L2_DV_1080I29_97:
    case V4L2_DV_1080I30:
    case V4L2_DV_1080I25:
    case V4L2_DV_1080I50:
    case V4L2_DV_1080I60:
    case V4L2_DV_1080P24:
    case V4L2_DV_1080P25:
    case V4L2_DV_1080P30:
    case V4L2_DV_1080P50:
    case V4L2_DV_1080P60:
    case V4L2_DV_1080I59_94:
    case V4L2_DV_1080P59_94:
    case V4L2_DV_1080P24_FP:
    case V4L2_DV_1080P24_SB_HALF:
    case V4L2_DV_1080P24_TB:
    case V4L2_DV_1080P23_98_FP:
    case V4L2_DV_1080P23_98_SB_HALF:
    case V4L2_DV_1080P23_98_TB:
    case V4L2_DV_1080I60_SB_HALF:
    case V4L2_DV_1080I59_94_SB_HALF:
    case V4L2_DV_1080I50_SB_HALF:
    case V4L2_DV_1080P60_SB_HALF:
    case V4L2_DV_1080P60_TB:
    case V4L2_DV_1080P30_FP:
    case V4L2_DV_1080P30_SB_HALF:
    case V4L2_DV_1080P30_TB:
        *width = 1920;
        *height = 1080;
        break;
    }
}

uint32_t ExynosHWCService::getHdmiCableStatus()
{
    return !!mHWCCtx->hdmi_hpd;
}

uint32_t ExynosHWCService::getHdmiAudioChannel()
{
    int channels;
    if (exynos_v4l2_g_ctrl(mHWCCtx->hdmi_layers[0].fd,
            V4L2_CID_TV_MAX_AUDIO_CHANNELS, &channels) < 0)
        ALOGE("%s: failed to get audio channels", __func__);
    return channels;
}

int ExynosHWCService::createServiceLocked()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::", __func__);
    sp<IServiceManager> sm = defaultServiceManager();
    sm->addService(String16("Exynos.HWCService"), mHWCService);
    if (sm->checkService(String16("Exynos.HWCService")) != NULL) {
        ALOGD_IF(HWC_SERVICE_DEBUG, "adding Exynos.HWCService succeeded");
        return 0;
    } else {
        ALOGE_IF(HWC_SERVICE_DEBUG, "adding Exynos.HWCService failed");
        return -1;
    }
}

ExynosHWCService *ExynosHWCService::getExynosHWCService()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::", __func__);
    ExynosHWCService& instance = ExynosHWCService::getInstance();
    Mutex::Autolock _l(instance.mLock);
    if (instance.mHWCService == NULL) {
        instance.mHWCService = &instance;

        int status = ExynosHWCService::getInstance().createServiceLocked();
        if (status != 0) {
            ALOGE_IF(HWC_SERVICE_DEBUG, "getExynosHWCService failed");
        }
    }
    return instance.mHWCService;
}

void ExynosHWCService::setExynosHWCCtx(ExynosHWCCtx *HWCCtx)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "HWCCtx=0x%x", (int)HWCCtx);
    if(HWCCtx) {
        mHWCCtx = HWCCtx;
    }
}

}
