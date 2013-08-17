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

#ifndef ANDROID_INCLUDE_HARDWARE_HWCOMPOSER_H
#define ANDROID_INCLUDE_HARDWARE_HWCOMPOSER_H

#include <stdint.h>
#include <sys/cdefs.h>

#if defined(ANDROID)
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <cutils/native_handle.h>
#endif /* ANDROID */

#include <hardware/hwcomposer_defs.h>

__BEGIN_DECLS

/*****************************************************************************/

/* for compatibility */
#define HWC_MODULE_API_VERSION      HWC_MODULE_API_VERSION_0_1
#define HWC_DEVICE_API_VERSION      HWC_DEVICE_API_VERSION_0_1
#define HWC_API_VERSION             HWC_DEVICE_API_VERSION

/* Users of this header can define HWC_REMOVE_DEPRECATED_VERSIONS to test that
 * they still work with just the current version declared, before the
 * deprecated versions are actually removed.
 *
 * To find code that still depends on the old versions, set the #define to 1
 * here. Code that explicitly sets it to zero (rather than simply not defining
 * it) will still see the old versions.
 */
#if !defined(HWC_REMOVE_DEPRECATED_VERSIONS)
#define HWC_REMOVE_DEPRECATED_VERSIONS 0
#endif

/*****************************************************************************/

/**
 * The id of this module
 */
#define HWC_HARDWARE_MODULE_ID "hwcomposer"

/**
 * Name of the sensors device to open
 */
#define HWC_HARDWARE_COMPOSER   "composer"

typedef struct hwc_rect {
    int left;
    int top;
    int right;
    int bottom;
} hwc_rect_t;

typedef struct hwc_region {
    size_t numRects;
    hwc_rect_t const* rects;
} hwc_region_t;

typedef struct hwc_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} hwc_color_t;

typedef struct hwc_layer_1 {
    /*
     * compositionType is used to specify this layer's type and is set by either
     * the hardware composer implementation, or by the caller (see below).
     *
     *  This field is always reset to HWC_BACKGROUND or HWC_FRAMEBUFFER
     *  before (*prepare)() is called when the HWC_GEOMETRY_CHANGED flag is
     *  also set, otherwise, this field is preserved between (*prepare)()
     *  calls.
     *
     * HWC_BACKGROUND
     *   Always set by the caller before calling (*prepare)(), this value
     *   indicates this is a special "background" layer. The only valid field
     *   is backgroundColor.
     *   The HWC can toggle this value to HWC_FRAMEBUFFER to indicate it CANNOT
     *   handle the background color.
     *
     *
     * HWC_FRAMEBUFFER_TARGET
     *   Always set by the caller before calling (*prepare)(), this value
     *   indicates this layer is the framebuffer surface used as the target of
     *   OpenGL ES composition. If the HWC sets all other layers to HWC_OVERLAY
     *   or HWC_BACKGROUND, then no OpenGL ES composition will be done, and
     *   this layer should be ignored during set().
     *
     *   This flag (and the framebuffer surface layer) will only be used if the
     *   HWC version is HWC_DEVICE_API_VERSION_1_1 or higher. In older versions,
     *   the OpenGL ES target surface is communicated by the (dpy, sur) fields
     *   in hwc_compositor_device_1_t.
     *
     *   This value cannot be set by the HWC implementation.
     *
     *
     * HWC_FRAMEBUFFER
     *   Set by the caller before calling (*prepare)() ONLY when the
     *   HWC_GEOMETRY_CHANGED flag is also set.
     *
     *   Set by the HWC implementation during (*prepare)(), this indicates
     *   that the layer will be drawn into the framebuffer using OpenGL ES.
     *   The HWC can toggle this value to HWC_OVERLAY to indicate it will
     *   handle the layer.
     *
     *
     * HWC_OVERLAY
     *   Set by the HWC implementation during (*prepare)(), this indicates
     *   that the layer will be handled by the HWC (ie: it must not be
     *   composited with OpenGL ES).
     *
     */
    int32_t compositionType;

    /*
     * hints is bit mask set by the HWC implementation during (*prepare)().
     * It is preserved between (*prepare)() calls, unless the
     * HWC_GEOMETRY_CHANGED flag is set, in which case it is reset to 0.
     *
     * see hwc_layer_t::hints
     */
    uint32_t hints;

    /* see hwc_layer_t::flags */
    uint32_t flags;

    union {
        /* color of the background.  hwc_color_t.a is ignored */
        hwc_color_t backgroundColor;

        struct {
            /* handle of buffer to compose. This handle is guaranteed to have been
             * allocated from gralloc using the GRALLOC_USAGE_HW_COMPOSER usage flag. If
             * the layer's handle is unchanged across two consecutive prepare calls and
             * the HWC_GEOMETRY_CHANGED flag is not set for the second call then the
             * HWComposer implementation may assume that the contents of the buffer have
             * not changed. */
            buffer_handle_t handle;

            /* transformation to apply to the buffer during composition */
            uint32_t transform;

            /* blending to apply during composition */
            int32_t blending;

            /* area of the source to consider, the origin is the top-left corner of
             * the buffer */
            hwc_rect_t sourceCrop;

            /* where to composite the sourceCrop onto the display. The sourceCrop
             * is scaled using linear filtering to the displayFrame. The origin is the
             * top-left corner of the screen.
             */
            hwc_rect_t displayFrame;

            /* visible region in screen space. The origin is the
             * top-left corner of the screen.
             * The visible region INCLUDES areas overlapped by a translucent layer.
             */
            hwc_region_t visibleRegionScreen;

            /* Sync fence object that will be signaled when the buffer's
             * contents are available. May be -1 if the contents are already
             * available. This field is only valid during set(), and should be
             * ignored during prepare(). The set() call must not wait for the
             * fence to be signaled before returning, but the HWC must wait for
             * all buffers to be signaled before reading from them.
             *
             * HWC_FRAMEBUFFER layers will never have an acquire fence, since
             * reads from them are complete before the framebuffer is ready for
             * display.
             *
             * The HWC takes ownership of the acquireFenceFd and is responsible
             * for closing it when no longer needed.
             */
            int acquireFenceFd;

            /* During set() the HWC must set this field to a file descriptor for
             * a sync fence object that will signal after the HWC has finished
             * reading from the buffer. The field is ignored by prepare(). Each
             * layer should have a unique file descriptor, even if more than one
             * refer to the same underlying fence object; this allows each to be
             * closed independently.
             *
             * If buffer reads can complete at significantly different times,
             * then using independent fences is preferred. For example, if the
             * HWC handles some layers with a blit engine and others with
             * overlays, then the blit layers can be reused immediately after
             * the blit completes, but the overlay layers can't be reused until
             * a subsequent frame has been displayed.
             *
             * Since HWC doesn't read from HWC_FRAMEBUFFER layers, it shouldn't
             * produce a release fence for them. The releaseFenceFd will be -1
             * for these layers when set() is called.
             *
             * The HWC client taks ownership of the releaseFenceFd and is
             * responsible for closing it when no longer needed.
             */
            int releaseFenceFd;
        };
    };

    /* Allow for expansion w/o breaking binary compatibility.
     * Pad layer to 96 bytes, assuming 32-bit pointers.
     */
    int32_t reserved[24 - 18];

} hwc_layer_1_t;

/* This represents a display, typically an EGLDisplay object */
typedef void* hwc_display_t;

/* This represents a surface, typically an EGLSurface object  */
typedef void* hwc_surface_t;

/*
 * hwc_display_contents_1_t::flags values
 */
enum {
    /*
     * HWC_GEOMETRY_CHANGED is set by SurfaceFlinger to indicate that the list
     * passed to (*prepare)() has changed by more than just the buffer handles
     * and acquire fences.
     */
    HWC_GEOMETRY_CHANGED = 0x00000001,
};

/*
 * Description of the contents to output on a display.
 *
 * This is the top-level structure passed to the prepare and set calls to
 * negotiate and commit the composition of a display image.
 */
typedef struct hwc_display_contents_1 {
    /* File descriptor referring to a Sync HAL fence object which will signal
     * when this composition is retired. For a physical display, a composition
     * is retired when it has been replaced on-screen by a subsequent set. For
     * a virtual display, the composition is retired when the writes to
     * outputBuffer are complete and can be read. The fence object is created
     * and returned by the set call; this field will be -1 on entry to prepare
     * and set. SurfaceFlinger will close the returned file descriptor.
     */
    int retireFenceFd;

    union {
        /* Fields only relevant for HWC_DEVICE_VERSION_1_0. */
        struct {
            /* (dpy, sur) is the target of SurfaceFlinger's OpenGL ES
             * composition for HWC_DEVICE_VERSION_1_0. They aren't relevant to
             * prepare. The set call should commit this surface atomically to
             * the display along with any overlay layers.
             */
            hwc_display_t dpy;
            hwc_surface_t sur;
        };

        /* Fields only relevant for HWC_DEVICE_VERSION_1_2 and later. */
        struct {
            /* outbuf is the buffer that receives the composed image for
             * virtual displays. Writes to the outbuf must wait until
             * outbufAcquireFenceFd signals. A fence that will signal when
             * writes to outbuf are complete should be returned in
             * retireFenceFd.
             *
             * For physical displays, outbuf will be NULL.
             */
            buffer_handle_t outbuf;

            /* File descriptor for a fence that will signal when outbuf is
             * ready to be written. The h/w composer is responsible for closing
             * this when no longer needed.
             *
             * Will be -1 whenever outbuf is NULL, or when the outbuf can be
             * written immediately.
             */
            int outbufAcquireFenceFd;
        };
    };

    /* List of layers that will be composed on the display. The buffer handles
     * in the list will be unique. If numHwLayers is 0, all composition will be
     * performed by SurfaceFlinger.
     */
    uint32_t flags;
    size_t numHwLayers;
    hwc_layer_1_t hwLayers[0];

} hwc_display_contents_1_t;

/* see hwc_composer_device::registerProcs()
 * All of the callbacks are required and non-NULL unless otherwise noted.
 */
typedef struct hwc_procs {
    /*
     * (*invalidate)() triggers a screen refresh, in particular prepare and set
     * will be called shortly after this call is made. Note that there is
     * NO GUARANTEE that the screen refresh will happen after invalidate()
     * returns (in particular, it could happen before).
     * invalidate() is GUARANTEED TO NOT CALL BACK into the h/w composer HAL and
     * it is safe to call invalidate() from any of hwc_composer_device
     * hooks, unless noted otherwise.
     */
    void (*invalidate)(const struct hwc_procs* procs);

    /*
     * (*vsync)() is called by the h/w composer HAL when a vsync event is
     * received and HWC_EVENT_VSYNC is enabled on a display
     * (see: hwc_event_control).
     *
     * the "disp" parameter indicates which display the vsync event is for.
     * the "timestamp" parameter is the system monotonic clock timestamp in
     *   nanosecond of when the vsync event happened.
     *
     * vsync() is GUARANTEED TO NOT CALL BACK into the h/w composer HAL.
     *
     * It is expected that vsync() is called from a thread of at least
     * HAL_PRIORITY_URGENT_DISPLAY with as little latency as possible,
     * typically less than 0.5 ms.
     *
     * It is a (silent) error to have HWC_EVENT_VSYNC enabled when calling
     * hwc_composer_device.set(..., 0, 0, 0) (screen off). The implementation
     * can either stop or continue to process VSYNC events, but must not
     * crash or cause other problems.
     */
    void (*vsync)(const struct hwc_procs* procs, int disp, int64_t timestamp);

    /*
     * (*hotplug)() is called by the h/w composer HAL when a display is
     * connected or disconnected. The PRIMARY display is always connected and
     * the hotplug callback should not be called for it.
     *
     * The disp parameter indicates which display type this event is for.
     * The connected parameter indicates whether the display has just been
     *   connected (1) or disconnected (0).
     *
     * The hotplug() callback may call back into the h/w composer on the same
     * thread to query refresh rate and dpi for the display. Additionally,
     * other threads may be calling into the h/w composer while the callback
     * is in progress.
     *
     * The h/w composer must serialize calls to the hotplug callback; only
     * one thread may call it at a time.
     *
     * This callback will be NULL if the h/w composer is using
     * HWC_DEVICE_API_VERSION_1_0.
     */
    void (*hotplug)(const struct hwc_procs* procs, int disp, int connected);

} hwc_procs_t;


/*****************************************************************************/

typedef struct hwc_module {
    struct hw_module_t common;
} hwc_module_t;

typedef struct hwc_composer_device_1 {
    struct hw_device_t common;

    /*
     * (*prepare)() is called for each frame before composition and is used by
     * SurfaceFlinger to determine what composition steps the HWC can handle.
     *
     * (*prepare)() can be called more than once, the last call prevails.
     *
     * The HWC responds by setting the compositionType field in each layer to
     * either HWC_FRAMEBUFFER or HWC_OVERLAY. In the former case, the
     * composition for the layer is handled by SurfaceFlinger with OpenGL ES,
     * in the later case, the HWC will have to handle the layer's composition.
     * compositionType and hints are preserved between (*prepare)() calles
     * unless the HWC_GEOMETRY_CHANGED flag is set.
     *
     * (*prepare)() is called with HWC_GEOMETRY_CHANGED to indicate that the
     * list's geometry has changed, that is, when more than just the buffer's
     * handles have been updated. Typically this happens (but is not limited to)
     * when a window is added, removed, resized or moved. In this case
     * compositionType and hints are reset to their default value.
     *
     * For HWC 1.0, numDisplays will always be one, and displays[0] will be
     * non-NULL.
     *
     * For HWC 1.1, numDisplays will always be HWC_NUM_DISPLAY_TYPES. Entries
     * for unsupported or disabled/disconnected display types will be NULL.
     *
     * For HWC 1.2 and later, numDisplays will be HWC_NUM_DISPLAY_TYPES or more.
     * The extra entries correspond to enabled virtual displays, and will be
     * non-NULL. In HWC 1.2, support for one virtual display is required, and
     * no more than one will be used. Future HWC versions might require more.
     *
     * returns: 0 on success. An negative error code on error. If an error is
     * returned, SurfaceFlinger will assume that none of the layer will be
     * handled by the HWC.
     */
    int (*prepare)(struct hwc_composer_device_1 *dev,
                    size_t numDisplays, hwc_display_contents_1_t** displays);

    /*
     * (*set)() is used in place of eglSwapBuffers(), and assumes the same
     * functionality, except it also commits the work list atomically with
     * the actual eglSwapBuffers().
     *
     * The layer lists are guaranteed to be the same as the ones returned from
     * the last call to (*prepare)().
     *
     * When this call returns the caller assumes that the displays will be
     * updated in the near future with the content of their work lists, without
     * artifacts during the transition from the previous frame.
     *
     * A display with zero layers indicates that the entire composition has
     * been handled by SurfaceFlinger with OpenGL ES. In this case, (*set)()
     * behaves just like eglSwapBuffers().
     *
     * For HWC 1.0, numDisplays will always be one, and displays[0] will be
     * non-NULL.
     *
     * For HWC 1.1, numDisplays will always be HWC_NUM_DISPLAY_TYPES. Entries
     * for unsupported or disabled/disconnected display types will be NULL.
     *
     * For HWC 1.2 and later, numDisplays will be HWC_NUM_DISPLAY_TYPES or more.
     * The extra entries correspond to enabled virtual displays, and will be
     * non-NULL. In HWC 1.2, support for one virtual display is required, and
     * no more than one will be used. Future HWC versions might require more.
     *
     * IMPORTANT NOTE: There is an implicit layer containing opaque black
     * pixels behind all the layers in the list. It is the responsibility of
     * the hwcomposer module to make sure black pixels are output (or blended
     * from).
     *
     * IMPORTANT NOTE: In the event of an error this call *MUST* still cause
     * any fences returned in the previous call to set to eventually become
     * signaled.  The caller may have already issued wait commands on these
     * fences, and having set return without causing those fences to signal
     * will likely result in a deadlock.
     *
     * returns: 0 on success. A negative error code on error:
     *    HWC_EGL_ERROR: eglGetError() will provide the proper error code (only
     *        allowed prior to HWComposer 1.1)
     *    Another code for non EGL errors.
     */
    int (*set)(struct hwc_composer_device_1 *dev,
                size_t numDisplays, hwc_display_contents_1_t** displays);

    /*
     * eventControl(..., event, enabled)
     * Enables or disables h/w composer events for a display.
     *
     * eventControl can be called from any thread and takes effect
     * immediately.
     *
     *  Supported events are:
     *      HWC_EVENT_VSYNC
     *
     * returns -EINVAL if the "event" parameter is not one of the value above
     * or if the "enabled" parameter is not 0 or 1.
     */
    int (*eventControl)(struct hwc_composer_device_1* dev, int disp,
            int event, int enabled);

    /*
     * blank(..., blank)
     * Blanks or unblanks a display's screen.
     *
     * Turns the screen off when blank is nonzero, on when blank is zero.
     * Multiple sequential calls with the same blank value must be supported.
     * The screen state transition must be be complete when the function
     * returns.
     *
     * returns 0 on success, negative on error.
     */
    int (*blank)(struct hwc_composer_device_1* dev, int disp, int blank);

    /*
     * Used to retrieve information about the h/w composer
     *
     * Returns 0 on success or -errno on error.
     */
    int (*query)(struct hwc_composer_device_1* dev, int what, int* value);

    /*
     * (*registerProcs)() registers callbacks that the h/w composer HAL can
     * later use. It will be called immediately after the composer device is
     * opened with non-NULL procs. It is FORBIDDEN to call any of the callbacks
     * from within registerProcs(). registerProcs() must save the hwc_procs_t
     * pointer which is needed when calling a registered callback.
     */
    void (*registerProcs)(struct hwc_composer_device_1* dev,
            hwc_procs_t const* procs);

    /*
     * This field is OPTIONAL and can be NULL.
     *
     * If non NULL it will be called by SurfaceFlinger on dumpsys
     */
    void (*dump)(struct hwc_composer_device_1* dev, char *buff, int buff_len);

    /*
     * (*getDisplayConfigs)() returns handles for the configurations available
     * on the connected display. These handles must remain valid as long as the
     * display is connected.
     *
     * Configuration handles are written to configs. The number of entries
     * allocated by the caller is passed in *numConfigs; getDisplayConfigs must
     * not try to write more than this number of config handles. On return, the
     * total number of configurations available for the display is returned in
     * *numConfigs. If *numConfigs is zero on entry, then configs may be NULL.
     *
     * HWC_DEVICE_API_VERSION_1_1 does not provide a way to choose a config.
     * For displays that support multiple configurations, the h/w composer
     * implementation should choose one and report it as the first config in
     * the list. Reporting the not-chosen configs is not required.
     *
     * Returns 0 on success or -errno on error. If disp is a hotpluggable
     * display type and no display is connected, an error should be returned.
     *
     * This field is REQUIRED for HWC_DEVICE_API_VERSION_1_1 and later.
     * It should be NULL for previous versions.
     */
    int (*getDisplayConfigs)(struct hwc_composer_device_1* dev, int disp,
            uint32_t* configs, size_t* numConfigs);

    /*
     * (*getDisplayAttributes)() returns attributes for a specific config of a
     * connected display. The config parameter is one of the config handles
     * returned by getDisplayConfigs.
     *
     * The list of attributes to return is provided in the attributes
     * parameter, terminated by HWC_DISPLAY_NO_ATTRIBUTE. The value for each
     * requested attribute is written in order to the values array. The
     * HWC_DISPLAY_NO_ATTRIBUTE attribute does not have a value, so the values
     * array will have one less value than the attributes array.
     *
     * This field is REQUIRED for HWC_DEVICE_API_VERSION_1_1 and later.
     * It should be NULL for previous versions.
     *
     * If disp is a hotpluggable display type and no display is connected,
     * or if config is not a valid configuration for the display, a negative
     * value should be returned.
     */
    int (*getDisplayAttributes)(struct hwc_composer_device_1* dev, int disp,
            uint32_t config, const uint32_t* attributes, int32_t* values);

    /*
     * Reserved for future use. Must be NULL.
     */
    void* reserved_proc[4];

} hwc_composer_device_1_t;

/** convenience API for opening and closing a device */

#if defined(ANDROID)
static inline int hwc_open_1(const struct hw_module_t* module,
        hwc_composer_device_1_t** device) {
    return module->methods->open(module,
            HWC_HARDWARE_COMPOSER, (struct hw_device_t**)device);
}

static inline int hwc_close_1(hwc_composer_device_1_t* device) {
    return device->common.close(&device->common);
}
#endif

/*****************************************************************************/

#if defined(ANDROID)
#if !HWC_REMOVE_DEPRECATED_VERSIONS
#include <hardware/hwcomposer_v0.h>
#endif
#endif

__END_DECLS

#endif /* ANDROID_INCLUDE_HARDWARE_HWCOMPOSER_H */
