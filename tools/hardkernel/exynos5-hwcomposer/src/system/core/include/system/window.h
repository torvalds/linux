/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef SYSTEM_CORE_INCLUDE_ANDROID_WINDOW_H
#define SYSTEM_CORE_INCLUDE_ANDROID_WINDOW_H

#include <cutils/native_handle.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sync/sync.h>
#include <sys/cdefs.h>
#include <system/graphics.h>
#include <unistd.h>

__BEGIN_DECLS

/*****************************************************************************/

#define ANDROID_NATIVE_MAKE_CONSTANT(a,b,c,d) \
    (((unsigned)(a)<<24)|((unsigned)(b)<<16)|((unsigned)(c)<<8)|(unsigned)(d))

#define ANDROID_NATIVE_WINDOW_MAGIC \
    ANDROID_NATIVE_MAKE_CONSTANT('_','w','n','d')

#define ANDROID_NATIVE_BUFFER_MAGIC \
    ANDROID_NATIVE_MAKE_CONSTANT('_','b','f','r')

// ---------------------------------------------------------------------------

// This #define may be used to conditionally compile device-specific code to
// support either the prior ANativeWindow interface, which did not pass libsync
// fences around, or the new interface that does.  This #define is only present
// when the ANativeWindow interface does include libsync support.
#define ANDROID_NATIVE_WINDOW_HAS_SYNC 1

// ---------------------------------------------------------------------------

typedef const native_handle_t* buffer_handle_t;

// ---------------------------------------------------------------------------

typedef struct android_native_rect_t
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} android_native_rect_t;

// ---------------------------------------------------------------------------

typedef struct android_native_base_t
{
    /* a magic value defined by the actual EGL native type */
    int magic;

    /* the sizeof() of the actual EGL native type */
    int version;

    void* reserved[4];

    /* reference-counting interface */
    void (*incRef)(struct android_native_base_t* base);
    void (*decRef)(struct android_native_base_t* base);
} android_native_base_t;

typedef struct ANativeWindowBuffer
{
#ifdef __cplusplus
    ANativeWindowBuffer() {
        common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
        common.version = sizeof(ANativeWindowBuffer);
        memset(common.reserved, 0, sizeof(common.reserved));
    }

    // Implement the methods that sp<ANativeWindowBuffer> expects so that it
    // can be used to automatically refcount ANativeWindowBuffer's.
    void incStrong(const void* id) const {
        common.incRef(const_cast<android_native_base_t*>(&common));
    }
    void decStrong(const void* id) const {
        common.decRef(const_cast<android_native_base_t*>(&common));
    }
#endif

    struct android_native_base_t common;

    int width;
    int height;
    int stride;
    int format;
    int usage;

    void* reserved[2];

    buffer_handle_t handle;

    void* reserved_proc[8];
} ANativeWindowBuffer_t;

// Old typedef for backwards compatibility.
typedef ANativeWindowBuffer_t android_native_buffer_t;

// ---------------------------------------------------------------------------

/* attributes queriable with query() */
enum {
    NATIVE_WINDOW_WIDTH     = 0,
    NATIVE_WINDOW_HEIGHT    = 1,
    NATIVE_WINDOW_FORMAT    = 2,

    /* The minimum number of buffers that must remain un-dequeued after a buffer
     * has been queued.  This value applies only if set_buffer_count was used to
     * override the number of buffers and if a buffer has since been queued.
     * Users of the set_buffer_count ANativeWindow method should query this
     * value before calling set_buffer_count.  If it is necessary to have N
     * buffers simultaneously dequeued as part of the steady-state operation,
     * and this query returns M then N+M buffers should be requested via
     * native_window_set_buffer_count.
     *
     * Note that this value does NOT apply until a single buffer has been
     * queued.  In particular this means that it is possible to:
     *
     * 1. Query M = min undequeued buffers
     * 2. Set the buffer count to N + M
     * 3. Dequeue all N + M buffers
     * 4. Cancel M buffers
     * 5. Queue, dequeue, queue, dequeue, ad infinitum
     */
    NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS = 3,

    /* Check whether queueBuffer operations on the ANativeWindow send the buffer
     * to the window compositor.  The query sets the returned 'value' argument
     * to 1 if the ANativeWindow DOES send queued buffers directly to the window
     * compositor and 0 if the buffers do not go directly to the window
     * compositor.
     *
     * This can be used to determine whether protected buffer content should be
     * sent to the ANativeWindow.  Note, however, that a result of 1 does NOT
     * indicate that queued buffers will be protected from applications or users
     * capturing their contents.  If that behavior is desired then some other
     * mechanism (e.g. the GRALLOC_USAGE_PROTECTED flag) should be used in
     * conjunction with this query.
     */
    NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER = 4,

    /* Get the concrete type of a ANativeWindow.  See below for the list of
     * possible return values.
     *
     * This query should not be used outside the Android framework and will
     * likely be removed in the near future.
     */
    NATIVE_WINDOW_CONCRETE_TYPE = 5,


    /*
     * Default width and height of ANativeWindow buffers, these are the
     * dimensions of the window buffers irrespective of the
     * NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS call and match the native window
     * size unless overridden by NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS.
     */
    NATIVE_WINDOW_DEFAULT_WIDTH = 6,
    NATIVE_WINDOW_DEFAULT_HEIGHT = 7,

    /*
     * transformation that will most-likely be applied to buffers. This is only
     * a hint, the actual transformation applied might be different.
     *
     * INTENDED USE:
     *
     * The transform hint can be used by a producer, for instance the GLES
     * driver, to pre-rotate the rendering such that the final transformation
     * in the composer is identity. This can be very useful when used in
     * conjunction with the h/w composer HAL, in situations where it
     * cannot handle arbitrary rotations.
     *
     * 1. Before dequeuing a buffer, the GL driver (or any other ANW client)
     *    queries the ANW for NATIVE_WINDOW_TRANSFORM_HINT.
     *
     * 2. The GL driver overrides the width and height of the ANW to
     *    account for NATIVE_WINDOW_TRANSFORM_HINT. This is done by querying
     *    NATIVE_WINDOW_DEFAULT_{WIDTH | HEIGHT}, swapping the dimensions
     *    according to NATIVE_WINDOW_TRANSFORM_HINT and calling
     *    native_window_set_buffers_dimensions().
     *
     * 3. The GL driver dequeues a buffer of the new pre-rotated size.
     *
     * 4. The GL driver renders to the buffer such that the image is
     *    already transformed, that is applying NATIVE_WINDOW_TRANSFORM_HINT
     *    to the rendering.
     *
     * 5. The GL driver calls native_window_set_transform to apply
     *    inverse transformation to the buffer it just rendered.
     *    In order to do this, the GL driver needs
     *    to calculate the inverse of NATIVE_WINDOW_TRANSFORM_HINT, this is
     *    done easily:
     *
     *        int hintTransform, inverseTransform;
     *        query(..., NATIVE_WINDOW_TRANSFORM_HINT, &hintTransform);
     *        inverseTransform = hintTransform;
     *        if (hintTransform & HAL_TRANSFORM_ROT_90)
     *            inverseTransform ^= HAL_TRANSFORM_ROT_180;
     *
     *
     * 6. The GL driver queues the pre-transformed buffer.
     *
     * 7. The composer combines the buffer transform with the display
     *    transform.  If the buffer transform happens to cancel out the
     *    display transform then no rotation is needed.
     *
     */
    NATIVE_WINDOW_TRANSFORM_HINT = 8,

    /*
     * Boolean that indicates whether the consumer is running more than
     * one buffer behind the producer.
     */
    NATIVE_WINDOW_CONSUMER_RUNNING_BEHIND = 9
};

/* Valid operations for the (*perform)() hook.
 *
 * Values marked as 'deprecated' are supported, but have been superceded by
 * other functionality.
 *
 * Values marked as 'private' should be considered private to the framework.
 * HAL implementation code with access to an ANativeWindow should not use these,
 * as it may not interact properly with the framework's use of the
 * ANativeWindow.
 */
enum {
    NATIVE_WINDOW_SET_USAGE                 =  0,
    NATIVE_WINDOW_CONNECT                   =  1,   /* deprecated */
    NATIVE_WINDOW_DISCONNECT                =  2,   /* deprecated */
    NATIVE_WINDOW_SET_CROP                  =  3,   /* private */
    NATIVE_WINDOW_SET_BUFFER_COUNT          =  4,
    NATIVE_WINDOW_SET_BUFFERS_GEOMETRY      =  5,   /* deprecated */
    NATIVE_WINDOW_SET_BUFFERS_TRANSFORM     =  6,
    NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP     =  7,
    NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS    =  8,
    NATIVE_WINDOW_SET_BUFFERS_FORMAT        =  9,
    NATIVE_WINDOW_SET_SCALING_MODE          = 10,   /* private */
    NATIVE_WINDOW_LOCK                      = 11,   /* private */
    NATIVE_WINDOW_UNLOCK_AND_POST           = 12,   /* private */
    NATIVE_WINDOW_API_CONNECT               = 13,   /* private */
    NATIVE_WINDOW_API_DISCONNECT            = 14,   /* private */
    NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS = 15, /* private */
    NATIVE_WINDOW_SET_POST_TRANSFORM_CROP   = 16,   /* private */
};

/* parameter for NATIVE_WINDOW_[API_][DIS]CONNECT */
enum {
    /* Buffers will be queued by EGL via eglSwapBuffers after being filled using
     * OpenGL ES.
     */
    NATIVE_WINDOW_API_EGL = 1,

    /* Buffers will be queued after being filled using the CPU
     */
    NATIVE_WINDOW_API_CPU = 2,

    /* Buffers will be queued by Stagefright after being filled by a video
     * decoder.  The video decoder can either be a software or hardware decoder.
     */
    NATIVE_WINDOW_API_MEDIA = 3,

    /* Buffers will be queued by the the camera HAL.
     */
    NATIVE_WINDOW_API_CAMERA = 4,
};

/* parameter for NATIVE_WINDOW_SET_BUFFERS_TRANSFORM */
enum {
    /* flip source image horizontally */
    NATIVE_WINDOW_TRANSFORM_FLIP_H = HAL_TRANSFORM_FLIP_H ,
    /* flip source image vertically */
    NATIVE_WINDOW_TRANSFORM_FLIP_V = HAL_TRANSFORM_FLIP_V,
    /* rotate source image 90 degrees clock-wise */
    NATIVE_WINDOW_TRANSFORM_ROT_90 = HAL_TRANSFORM_ROT_90,
    /* rotate source image 180 degrees */
    NATIVE_WINDOW_TRANSFORM_ROT_180 = HAL_TRANSFORM_ROT_180,
    /* rotate source image 270 degrees clock-wise */
    NATIVE_WINDOW_TRANSFORM_ROT_270 = HAL_TRANSFORM_ROT_270,
};

/* parameter for NATIVE_WINDOW_SET_SCALING_MODE */
enum {
    /* the window content is not updated (frozen) until a buffer of
     * the window size is received (enqueued)
     */
    NATIVE_WINDOW_SCALING_MODE_FREEZE           = 0,
    /* the buffer is scaled in both dimensions to match the window size */
    NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW  = 1,
    /* the buffer is scaled uniformly such that the smaller dimension
     * of the buffer matches the window size (cropping in the process)
     */
    NATIVE_WINDOW_SCALING_MODE_SCALE_CROP       = 2,
    /* the window is clipped to the size of the buffer's crop rectangle; pixels
     * outside the crop rectangle are treated as if they are completely
     * transparent.
     */
    NATIVE_WINDOW_SCALING_MODE_NO_SCALE_CROP    = 3,
};

/* values returned by the NATIVE_WINDOW_CONCRETE_TYPE query */
enum {
    NATIVE_WINDOW_FRAMEBUFFER               = 0, /* FramebufferNativeWindow */
    NATIVE_WINDOW_SURFACE                   = 1, /* Surface */
    NATIVE_WINDOW_SURFACE_TEXTURE_CLIENT    = 2, /* SurfaceTextureClient */
};

/* parameter for NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP
 *
 * Special timestamp value to indicate that timestamps should be auto-generated
 * by the native window when queueBuffer is called.  This is equal to INT64_MIN,
 * defined directly to avoid problems with C99/C++ inclusion of stdint.h.
 */
static const int64_t NATIVE_WINDOW_TIMESTAMP_AUTO = (-9223372036854775807LL-1);

struct ANativeWindow
{
#ifdef __cplusplus
    ANativeWindow()
        : flags(0), minSwapInterval(0), maxSwapInterval(0), xdpi(0), ydpi(0)
    {
        common.magic = ANDROID_NATIVE_WINDOW_MAGIC;
        common.version = sizeof(ANativeWindow);
        memset(common.reserved, 0, sizeof(common.reserved));
    }

    /* Implement the methods that sp<ANativeWindow> expects so that it
       can be used to automatically refcount ANativeWindow's. */
    void incStrong(const void* id) const {
        common.incRef(const_cast<android_native_base_t*>(&common));
    }
    void decStrong(const void* id) const {
        common.decRef(const_cast<android_native_base_t*>(&common));
    }
#endif

    struct android_native_base_t common;

    /* flags describing some attributes of this surface or its updater */
    const uint32_t flags;

    /* min swap interval supported by this updated */
    const int   minSwapInterval;

    /* max swap interval supported by this updated */
    const int   maxSwapInterval;

    /* horizontal and vertical resolution in DPI */
    const float xdpi;
    const float ydpi;

    /* Some storage reserved for the OEM's driver. */
    intptr_t    oem[4];

    /*
     * Set the swap interval for this surface.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*setSwapInterval)(struct ANativeWindow* window,
                int interval);

    /*
     * Hook called by EGL to acquire a buffer. After this call, the buffer
     * is not locked, so its content cannot be modified. This call may block if
     * no buffers are available.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * Returns 0 on success or -errno on error.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but the new dequeueBuffer function that
     * outputs a fence file descriptor should be used in its place.
     */
    int     (*dequeueBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer** buffer);

    /*
     * hook called by EGL to lock a buffer. This MUST be called before modifying
     * the content of a buffer. The buffer must have been acquired with
     * dequeueBuffer first.
     *
     * Returns 0 on success or -errno on error.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but it is essentially a no-op, and calls
     * to it should be removed.
     */
    int     (*lockBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer);

    /*
     * Hook called by EGL when modifications to the render buffer are done.
     * This unlocks and post the buffer.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * Buffers MUST be queued in the same order than they were dequeued.
     *
     * Returns 0 on success or -errno on error.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but the new queueBuffer function that
     * takes a fence file descriptor should be used in its place (pass a value
     * of -1 for the fence file descriptor if there is no valid one to pass).
     */
    int     (*queueBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer);

    /*
     * hook used to retrieve information about the native window.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*query)(const struct ANativeWindow* window,
                int what, int* value);

    /*
     * hook used to perform various operations on the surface.
     * (*perform)() is a generic mechanism to add functionality to
     * ANativeWindow while keeping backward binary compatibility.
     *
     * DO NOT CALL THIS HOOK DIRECTLY.  Instead, use the helper functions
     * defined below.
     *
     *  (*perform)() returns -ENOENT if the 'what' parameter is not supported
     *  by the surface's implementation.
     *
     * The valid operations are:
     *     NATIVE_WINDOW_SET_USAGE
     *     NATIVE_WINDOW_CONNECT               (deprecated)
     *     NATIVE_WINDOW_DISCONNECT            (deprecated)
     *     NATIVE_WINDOW_SET_CROP              (private)
     *     NATIVE_WINDOW_SET_BUFFER_COUNT
     *     NATIVE_WINDOW_SET_BUFFERS_GEOMETRY  (deprecated)
     *     NATIVE_WINDOW_SET_BUFFERS_TRANSFORM
     *     NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP
     *     NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS
     *     NATIVE_WINDOW_SET_BUFFERS_FORMAT
     *     NATIVE_WINDOW_SET_SCALING_MODE       (private)
     *     NATIVE_WINDOW_LOCK                   (private)
     *     NATIVE_WINDOW_UNLOCK_AND_POST        (private)
     *     NATIVE_WINDOW_API_CONNECT            (private)
     *     NATIVE_WINDOW_API_DISCONNECT         (private)
     *     NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS (private)
     *     NATIVE_WINDOW_SET_POST_TRANSFORM_CROP (private)
     *
     */

    int     (*perform)(struct ANativeWindow* window,
                int operation, ... );

    /*
     * Hook used to cancel a buffer that has been dequeued.
     * No synchronization is performed between dequeue() and cancel(), so
     * either external synchronization is needed, or these functions must be
     * called from the same thread.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but the new cancelBuffer function that
     * takes a fence file descriptor should be used in its place (pass a value
     * of -1 for the fence file descriptor if there is no valid one to pass).
     */
    int     (*cancelBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer);

    /*
     * Hook called by EGL to acquire a buffer. This call may block if no
     * buffers are available.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * The libsync fence file descriptor returned in the int pointed to by the
     * fenceFd argument will refer to the fence that must signal before the
     * dequeued buffer may be written to.  A value of -1 indicates that the
     * caller may access the buffer immediately without waiting on a fence.  If
     * a valid file descriptor is returned (i.e. any value except -1) then the
     * caller is responsible for closing the file descriptor.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*dequeueBuffer)(struct ANativeWindow* window,
                struct ANativeWindowBuffer** buffer, int* fenceFd);

    /*
     * Hook called by EGL when modifications to the render buffer are done.
     * This unlocks and post the buffer.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * The fenceFd argument specifies a libsync fence file descriptor for a
     * fence that must signal before the buffer can be accessed.  If the buffer
     * can be accessed immediately then a value of -1 should be used.  The
     * caller must not use the file descriptor after it is passed to
     * queueBuffer, and the ANativeWindow implementation is responsible for
     * closing it.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*queueBuffer)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer, int fenceFd);

    /*
     * Hook used to cancel a buffer that has been dequeued.
     * No synchronization is performed between dequeue() and cancel(), so
     * either external synchronization is needed, or these functions must be
     * called from the same thread.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * The fenceFd argument specifies a libsync fence file decsriptor for a
     * fence that must signal before the buffer can be accessed.  If the buffer
     * can be accessed immediately then a value of -1 should be used.
     *
     * Note that if the client has not waited on the fence that was returned
     * from dequeueBuffer, that same fence should be passed to cancelBuffer to
     * ensure that future uses of the buffer are preceded by a wait on that
     * fence.  The caller must not use the file descriptor after it is passed
     * to cancelBuffer, and the ANativeWindow implementation is responsible for
     * closing it.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*cancelBuffer)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer, int fenceFd);
};

 /* Backwards compatibility: use ANativeWindow (struct ANativeWindow in C).
  * android_native_window_t is deprecated.
  */
typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeWindow android_native_window_t;

/*
 *  native_window_set_usage(..., usage)
 *  Sets the intended usage flags for the next buffers
 *  acquired with (*lockBuffer)() and on.
 *  By default (if this function is never called), a usage of
 *      GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE
 *  is assumed.
 *  Calling this function will usually cause following buffers to be
 *  reallocated.
 */

static inline int native_window_set_usage(
        struct ANativeWindow* window, int usage)
{
    return window->perform(window, NATIVE_WINDOW_SET_USAGE, usage);
}

/* deprecated. Always returns 0. Don't call. */
static inline int native_window_connect(
        struct ANativeWindow* window, int api) {
    return 0;
}

/* deprecated. Always returns 0. Don't call. */
static inline int native_window_disconnect(
        struct ANativeWindow* window, int api) {
    return 0;
}

/*
 * native_window_set_crop(..., crop)
 * Sets which region of the next queued buffers needs to be considered.
 * Depending on the scaling mode, a buffer's crop region is scaled and/or
 * cropped to match the surface's size.  This function sets the crop in
 * pre-transformed buffer pixel coordinates.
 *
 * The specified crop region applies to all buffers queued after it is called.
 *
 * If 'crop' is NULL, subsequently queued buffers won't be cropped.
 *
 * An error is returned if for instance the crop region is invalid, out of the
 * buffer's bound or if the window is invalid.
 */
static inline int native_window_set_crop(
        struct ANativeWindow* window,
        android_native_rect_t const * crop)
{
    return window->perform(window, NATIVE_WINDOW_SET_CROP, crop);
}

/*
 * native_window_set_post_transform_crop(..., crop)
 * Sets which region of the next queued buffers needs to be considered.
 * Depending on the scaling mode, a buffer's crop region is scaled and/or
 * cropped to match the surface's size.  This function sets the crop in
 * post-transformed pixel coordinates.
 *
 * The specified crop region applies to all buffers queued after it is called.
 *
 * If 'crop' is NULL, subsequently queued buffers won't be cropped.
 *
 * An error is returned if for instance the crop region is invalid, out of the
 * buffer's bound or if the window is invalid.
 */
static inline int native_window_set_post_transform_crop(
        struct ANativeWindow* window,
        android_native_rect_t const * crop)
{
    return window->perform(window, NATIVE_WINDOW_SET_POST_TRANSFORM_CROP, crop);
}

/*
 * native_window_set_active_rect(..., active_rect)
 *
 * This function is deprecated and will be removed soon.  For now it simply
 * sets the post-transform crop for compatibility while multi-project commits
 * get checked.
 */
static inline int native_window_set_active_rect(
        struct ANativeWindow* window,
        android_native_rect_t const * active_rect)
{
    return native_window_set_post_transform_crop(window, active_rect);
}

/*
 * native_window_set_buffer_count(..., count)
 * Sets the number of buffers associated with this native window.
 */
static inline int native_window_set_buffer_count(
        struct ANativeWindow* window,
        size_t bufferCount)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFER_COUNT, bufferCount);
}

/*
 * native_window_set_buffers_geometry(..., int w, int h, int format)
 * All buffers dequeued after this call will have the dimensions and format
 * specified.  A successful call to this function has the same effect as calling
 * native_window_set_buffers_size and native_window_set_buffers_format.
 *
 * XXX: This function is deprecated.  The native_window_set_buffers_dimensions
 * and native_window_set_buffers_format functions should be used instead.
 */
static inline int native_window_set_buffers_geometry(
        struct ANativeWindow* window,
        int w, int h, int format)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_GEOMETRY,
            w, h, format);
}

/*
 * native_window_set_buffers_dimensions(..., int w, int h)
 * All buffers dequeued after this call will have the dimensions specified.
 * In particular, all buffers will have a fixed-size, independent from the
 * native-window size. They will be scaled according to the scaling mode
 * (see native_window_set_scaling_mode) upon window composition.
 *
 * If w and h are 0, the normal behavior is restored. That is, dequeued buffers
 * following this call will be sized to match the window's size.
 *
 * Calling this function will reset the window crop to a NULL value, which
 * disables cropping of the buffers.
 */
static inline int native_window_set_buffers_dimensions(
        struct ANativeWindow* window,
        int w, int h)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS,
            w, h);
}

/*
 * native_window_set_buffers_user_dimensions(..., int w, int h)
 *
 * Sets the user buffer size for the window, which overrides the
 * window's size.  All buffers dequeued after this call will have the
 * dimensions specified unless overridden by
 * native_window_set_buffers_dimensions.  All buffers will have a
 * fixed-size, independent from the native-window size. They will be
 * scaled according to the scaling mode (see
 * native_window_set_scaling_mode) upon window composition.
 *
 * If w and h are 0, the normal behavior is restored. That is, the
 * default buffer size will match the windows's size.
 *
 * Calling this function will reset the window crop to a NULL value, which
 * disables cropping of the buffers.
 */
static inline int native_window_set_buffers_user_dimensions(
        struct ANativeWindow* window,
        int w, int h)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS,
            w, h);
}

/*
 * native_window_set_buffers_format(..., int format)
 * All buffers dequeued after this call will have the format specified.
 *
 * If the specified format is 0, the default buffer format will be used.
 */
static inline int native_window_set_buffers_format(
        struct ANativeWindow* window,
        int format)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_FORMAT, format);
}

/*
 * native_window_set_buffers_transform(..., int transform)
 * All buffers queued after this call will be displayed transformed according
 * to the transform parameter specified.
 */
static inline int native_window_set_buffers_transform(
        struct ANativeWindow* window,
        int transform)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TRANSFORM,
            transform);
}

/*
 * native_window_set_buffers_timestamp(..., int64_t timestamp)
 * All buffers queued after this call will be associated with the timestamp
 * parameter specified. If the timestamp is set to NATIVE_WINDOW_TIMESTAMP_AUTO
 * (the default), timestamps will be generated automatically when queueBuffer is
 * called. The timestamp is measured in nanoseconds, and is normally monotonically
 * increasing. The timestamp should be unaffected by time-of-day adjustments,
 * and for a camera should be strictly monotonic but for a media player may be
 * reset when the position is set.
 */
static inline int native_window_set_buffers_timestamp(
        struct ANativeWindow* window,
        int64_t timestamp)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP,
            timestamp);
}

/*
 * native_window_set_scaling_mode(..., int mode)
 * All buffers queued after this call will be associated with the scaling mode
 * specified.
 */
static inline int native_window_set_scaling_mode(
        struct ANativeWindow* window,
        int mode)
{
    return window->perform(window, NATIVE_WINDOW_SET_SCALING_MODE,
            mode);
}

/*
 * native_window_api_connect(..., int api)
 * connects an API to this window. only one API can be connected at a time.
 * Returns -EINVAL if for some reason the window cannot be connected, which
 * can happen if it's connected to some other API.
 */
static inline int native_window_api_connect(
        struct ANativeWindow* window, int api)
{
    return window->perform(window, NATIVE_WINDOW_API_CONNECT, api);
}

/*
 * native_window_api_disconnect(..., int api)
 * disconnect the API from this window.
 * An error is returned if for instance the window wasn't connected in the
 * first place.
 */
static inline int native_window_api_disconnect(
        struct ANativeWindow* window, int api)
{
    return window->perform(window, NATIVE_WINDOW_API_DISCONNECT, api);
}

/*
 * native_window_dequeue_buffer_and_wait(...)
 * Dequeue a buffer and wait on the fence associated with that buffer.  The
 * buffer may safely be accessed immediately upon this function returning.  An
 * error is returned if either of the dequeue or the wait operations fail.
 */
static inline int native_window_dequeue_buffer_and_wait(ANativeWindow *anw,
        struct ANativeWindowBuffer** anb) {
    return anw->dequeueBuffer_DEPRECATED(anw, anb);
}


__END_DECLS

#endif /* SYSTEM_CORE_INCLUDE_ANDROID_WINDOW_H */
