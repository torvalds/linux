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

#ifndef ANDROID_EXYNOS_HWC_SERVICE_H_
#define ANDROID_EXYNOS_HWC_SERVICE_H_

#include <utils/Errors.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <binder/IServiceManager.h>
#include <utils/Singleton.h>
#include <utils/StrongPointer.h>
#include "IExynosHWC.h"
#include "ExynosHWC.h"

typedef struct exynos5_hwc_composer_device_1_t ExynosHWCCtx;

namespace android {

    enum {
        HDMI_RESOLUTION_BASE = 0,
        HDMI_480P_59_94,
        HDMI_576P_50,
        HDMI_720P_24,
        HDMI_720P_25,
        HDMI_720P_30,
        HDMI_720P_50,
        HDMI_720P_59_94,
        HDMI_720P_60,
        HDMI_1080I_29_97,
        HDMI_1080I_30,
        HDMI_1080I_25,
        HDMI_1080I_50,
        HDMI_1080I_60,
        HDMI_1080P_24,
        HDMI_1080P_25,
        HDMI_1080P_30,
        HDMI_1080P_50,
        HDMI_1080P_60,
        HDMI_480P_60,
        HDMI_1080I_59_94,
        HDMI_1080P_59_94,
        HDMI_1080P_23_98,
    };

#define S3D_720P_60_BASE        22
#define S3D_720P_59_94_BASE     25
#define S3D_720P_50_BASE        28
#define S3D_1080P_24_BASE       31
#define S3D_1080P_23_98_BASE    34
#define S3D_1080P_60_BASE       39
#define S3D_1080P_30_BASE       42

class ExynosHWCService
    : public BnExynosHWCService,  Singleton<ExynosHWCService> {

public:
    static ExynosHWCService* getExynosHWCService();
    ~ExynosHWCService();

    virtual int setWFDMode(unsigned int mode);
    virtual int setWFDOutputResolution(unsigned int width, unsigned int height,
                                      unsigned int disp_w, unsigned int disp_h);
    virtual int setExtraFBMode(unsigned int mode);
    virtual int setCameraMode(unsigned int mode);
    virtual int setForceMirrorMode(unsigned int mode);
    virtual int setVideoPlayStatus(unsigned int mode);
    virtual int setExternalDisplayPause(bool onoff);
    virtual int setDispOrientation(unsigned int transform);
    virtual int setProtectionMode(unsigned int mode);
    virtual int setExternalDispLayerNum(unsigned int num);
    virtual int setForceGPU(unsigned int on);
    virtual int setExternalUITransform(unsigned int transform);
    virtual int getExternalUITransform(void);
    virtual int setWFDOutputTransform(unsigned int transform);
    virtual int getWFDOutputTransform(void);

    virtual void setHdmiResolution(int resolution, int s3dMode);
    virtual void setHdmiCableStatus(int status);
    virtual void setHdmiHdcp(int status);
    virtual void setHdmiAudioChannel(uint32_t channels);
    virtual void setHdmiSubtitles(bool use);
    virtual void setPresentationMode(bool use);
    virtual void setWFDSleepCtrl(bool black);

    virtual int getWFDMode();
    virtual void getWFDOutputResolution(unsigned int *width, unsigned int *height);
    virtual int getWFDOutputInfo(int *fd1, int *fd2, struct wfd_layer_t *wfd_info);
    virtual int getPresentationMode(void);
    virtual void getHdmiResolution(uint32_t *width, uint32_t *height);
    virtual uint32_t getHdmiCableStatus();
    virtual uint32_t getHdmiAudioChannel();

    void setExynosHWCCtx(ExynosHWCCtx *);
private:
    friend class Singleton<ExynosHWCService>;
    ExynosHWCService();
    int createServiceLocked();
    ExynosHWCService *mHWCService;
    Mutex mLock;
    ExynosHWCCtx *mHWCCtx;
};

}
#endif
