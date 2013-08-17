/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef ANDROID_INCLUDE_HARDWARE_HWCOMPOSER_DEFS_H
#define ANDROID_INCLUDE_HARDWARE_HWCOMPOSER_DEFS_H

#include <stdint.h>
#include <sys/cdefs.h>

#if defined(ANDROID)
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <cutils/native_handle.h>
#endif /* ANDROID */

__BEGIN_DECLS

/*****************************************************************************/

#define HWC_HEADER_VERSION          1

#define HWC_MODULE_API_VERSION_0_1  HARDWARE_MODULE_API_VERSION(0, 1)

#define HWC_DEVICE_API_VERSION_0_1  HARDWARE_DEVICE_API_VERSION_2(0, 1, HWC_HEADER_VERSION)
#define HWC_DEVICE_API_VERSION_0_2  HARDWARE_DEVICE_API_VERSION_2(0, 2, HWC_HEADER_VERSION)
#define HWC_DEVICE_API_VERSION_0_3  HARDWARE_DEVICE_API_VERSION_2(0, 3, HWC_HEADER_VERSION)
#define HWC_DEVICE_API_VERSION_1_0  HARDWARE_DEVICE_API_VERSION_2(1, 0, HWC_HEADER_VERSION)
#define HWC_DEVICE_API_VERSION_1_1  HARDWARE_DEVICE_API_VERSION_2(1, 1, HWC_HEADER_VERSION)
#define HWC_DEVICE_API_VERSION_1_2  HARDWARE_DEVICE_API_VERSION_2(1, 2, HWC_HEADER_VERSION)

enum {
    /* hwc_composer_device_t::set failed in EGL */
    HWC_EGL_ERROR = -1
};

/*
 * hwc_layer_t::hints values
 * Hints are set by the HAL and read by SurfaceFlinger
 */
enum {
    /*
     * HWC can set the HWC_HINT_TRIPLE_BUFFER hint to indicate to SurfaceFlinger
     * that it should triple buffer this layer. Typically HWC does this when
     * the layer will be unavailable for use for an extended period of time,
     * e.g. if the display will be fetching data directly from the layer and
     * the layer can not be modified until after the next set().
     */
    HWC_HINT_TRIPLE_BUFFER  = 0x00000001,

    /*
     * HWC sets HWC_HINT_CLEAR_FB to tell SurfaceFlinger that it should clear the
     * framebuffer with transparent pixels where this layer would be.
     * SurfaceFlinger will only honor this flag when the layer has no blending
     *
     */
    HWC_HINT_CLEAR_FB       = 0x00000002
};

/*
 * hwc_layer_t::flags values
 * Flags are set by SurfaceFlinger and read by the HAL
 */
enum {
    /*
     * HWC_SKIP_LAYER is set by SurfaceFlnger to indicate that the HAL
     * shall not consider this layer for composition as it will be handled
     * by SurfaceFlinger (just as if compositionType was set to HWC_OVERLAY).
     */
    HWC_SKIP_LAYER = 0x00000001,
};

/*
 * hwc_layer_t::compositionType values
 */
enum {
    /* this layer is to be drawn into the framebuffer by SurfaceFlinger */
    HWC_FRAMEBUFFER = 0,

    /* this layer will be handled in the HWC */
    HWC_OVERLAY = 1,

    /* this is the background layer. it's used to set the background color.
     * there is only a single background layer */
    HWC_BACKGROUND = 2,

    /* this layer holds the result of compositing the HWC_FRAMEBUFFER layers.
     * Added in HWC_DEVICE_API_VERSION_1_1. */
    HWC_FRAMEBUFFER_TARGET = 3,
};

/*
 * hwc_layer_t::blending values
 */
enum {
    /* no blending */
    HWC_BLENDING_NONE     = 0x0100,

    /* ONE / ONE_MINUS_SRC_ALPHA */
    HWC_BLENDING_PREMULT  = 0x0105,

    /* SRC_ALPHA / ONE_MINUS_SRC_ALPHA */
    HWC_BLENDING_COVERAGE = 0x0405
};

/*
 * hwc_layer_t::transform values
 */
enum {
    /* flip source image horizontally */
    HWC_TRANSFORM_FLIP_H = HAL_TRANSFORM_FLIP_H,
    /* flip source image vertically */
    HWC_TRANSFORM_FLIP_V = HAL_TRANSFORM_FLIP_V,
    /* rotate source image 90 degrees clock-wise */
    HWC_TRANSFORM_ROT_90 = HAL_TRANSFORM_ROT_90,
    /* rotate source image 180 degrees */
    HWC_TRANSFORM_ROT_180 = HAL_TRANSFORM_ROT_180,
    /* rotate source image 270 degrees clock-wise */
    HWC_TRANSFORM_ROT_270 = HAL_TRANSFORM_ROT_270,
};

/* attributes queriable with query() */
enum {
    /*
     * Availability: HWC_DEVICE_API_VERSION_0_2
     * Must return 1 if the background layer is supported, 0 otherwise.
     */
    HWC_BACKGROUND_LAYER_SUPPORTED      = 0,

    /*
     * Availability: HWC_DEVICE_API_VERSION_0_3
     * Returns the vsync period in nanoseconds.
     *
     * This query is not used for HWC_DEVICE_API_VERSION_1_1 and later.
     * Instead, the per-display attribute HWC_DISPLAY_VSYNC_PERIOD is used.
     */
    HWC_VSYNC_PERIOD                    = 1,

    /*
     * Availability: HWC_DEVICE_API_VERSION_1_1
     * Returns a mask of supported display types.
     */
    HWC_DISPLAY_TYPES_SUPPORTED         = 2,
};

/* display attributes returned by getDisplayAttributes() */
enum {
    /* Indicates the end of an attribute list */
    HWC_DISPLAY_NO_ATTRIBUTE                = 0,

    /* The vsync period in nanoseconds */
    HWC_DISPLAY_VSYNC_PERIOD                = 1,

    /* The number of pixels in the horizontal and vertical directions. */
    HWC_DISPLAY_WIDTH                       = 2,
    HWC_DISPLAY_HEIGHT                      = 3,

    /* The number of pixels per thousand inches of this configuration.
     *
     * Scaling DPI by 1000 allows it to be stored in an int without losing
     * too much precision.
     *
     * If the DPI for a configuration is unavailable or the HWC implementation
     * considers it unreliable, it should set these attributes to zero.
     */
    HWC_DISPLAY_DPI_X                       = 4,
    HWC_DISPLAY_DPI_Y                       = 5,
};

/* Allowed events for hwc_methods::eventControl() */
enum {
    HWC_EVENT_VSYNC     = 0
};

/* Display types and associated mask bits. */
enum {
    HWC_DISPLAY_PRIMARY     = 0,
    HWC_DISPLAY_EXTERNAL    = 1,    // HDMI, DP, etc.
    HWC_NUM_DISPLAY_TYPES
};

enum {
    HWC_DISPLAY_PRIMARY_BIT     = 1 << HWC_DISPLAY_PRIMARY,
    HWC_DISPLAY_EXTERNAL_BIT    = 1 << HWC_DISPLAY_EXTERNAL,
};

/*****************************************************************************/

__END_DECLS

#endif /* ANDROID_INCLUDE_HARDWARE_HWCOMPOSER_DEFS_H */
