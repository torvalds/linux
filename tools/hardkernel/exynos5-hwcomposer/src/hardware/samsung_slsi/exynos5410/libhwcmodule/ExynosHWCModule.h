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

#ifndef ANDROID_EXYNOS_HWC_MODULE_H_
#define ANDROID_EXYNOS_HWC_MODULE_H_
#include <hardware/hwcomposer.h>
const size_t GSC_DST_W_ALIGNMENT_RGB888 = 16;
const size_t GSC_DST_CROP_W_ALIGNMENT_RGB888 = 1;
#define VSYNC_DEV_NAME  "/sys/devices/platform/exynos-sysmmu.11/exynos5-fb.1/vsync"
#define WAIT_FOR_RENDER_FINISH
#define EXYNOS_SUPPORT_BGRX_8888
#define HWC_DYNAMIC_RECOMPOSITION
#define MIXER_UPDATE
#define USE_NORMAL_DRM
#define SKIP_STATIC_LAYER_COMP
#define DUAL_VIDEO_OVERLAY_SUPPORT
#define TV_BLANK

inline int ExynosWaitForRenderFinish(const private_module_t  *gralloc_module,
                                                        buffer_handle_t *handle, int num_buffers)
{
    if (gralloc_module) {
        if (gralloc_module->FinishPVRRender(gralloc_module, handle, num_buffers) < 0)
            return -1;
    }
    return 0;
}
#endif
