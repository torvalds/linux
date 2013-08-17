/* Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HAL_PUBLIC_H
#define HAL_PUBLIC_H

/* Authors of third party hardware composer (HWC) modules will need to include
 * this header to access functionality in the gralloc and framebuffer HALs.
 */
#if defined(ANDROID)
#include <hardware/gralloc.h>
#endif

#define ALIGN(x,a)	(((x) + (a) - 1L) & ~((a) - 1L))
#define HW_ALIGN	32

/* This can be tuned down as appropriate for the SOC.
 *
 * IMG formats are usually a single sub-alloc.
 * Some OEM video formats are two sub-allocs (Y, UV planes).
 * Future OEM video formats might be three sub-allocs (Y, U, V planes).
 */
#define MAX_YUV_PER_PLANE_ALLOCS 3
#define MAX_SUB_ALLOCS 3
#define HAL_PIXEL_FORMAT_BGRX_8888 0x1FF

typedef struct IMG_native_handle_t
{
	native_handle_t base;

	/* These fields can be sent cross process. They are also valid
	 * to duplicate within the same process.
	 *
	 * A table is stored within psPrivateData on gralloc_module_t (this
	 * is obviously per-process) which maps stamps to a mapped
	 * PVRSRV_CLIENT_MEM_INFO in that process. Each map entry has a lock
	 * count associated with it, satisfying the requirements of the
	 * Android API. This also prevents us from leaking maps/allocations.
	 *
	 * This table has entries inserted either by alloc()
	 * (alloc_device_t) or map() (gralloc_module_t). Entries are removed
	 * by free() (alloc_device_t) and unmap() (gralloc_module_t).
	 *
	 * As a special case for framebuffer_device_t, framebuffer_open()
	 * will add and framebuffer_close() will remove from this table.
	 */

	/* S.LSI 12.05.23
	 * IMG_NATIVE_HANDLE_NUMFDS = ion_fd[MAX_YUV_PER_PLANE_ALLOCS] + fd[MAX_SUB_ALLOCS]
	 * NUMFDS increased for ion_fd
	 */
#define IMG_NATIVE_HANDLE_NUMFDS (MAX_SUB_ALLOCS + MAX_YUV_PER_PLANE_ALLOCS)
	/* The `fd' field is used to "export" a meminfo to another process.
	 * Therefore, it is allocated by alloc_device_t, and consumed by
	 * gralloc_module_t. The framebuffer_device_t does not need a handle,
	 * and the special value IMG_FRAMEBUFFER_FD is used instead.
	 */
	/* S.LSI 12.05.23 added ion_fd for HWC */
	int fd;
	int fd1;
	int fd2; // TODO : need to support
	int fd_pvr[MAX_SUB_ALLOCS];

	/* S.LSI 12.05.23 added 6 integer type variables */
#define IMG_NATIVE_HANDLE_NUMINTS \
	 ((sizeof(unsigned long long) / sizeof(int)) + 5 + 7 + MAX_SUB_ALLOCS + 1)
	/* A KERNEL unique identifier for any exported kernel meminfo. Each
	 * exported kernel meminfo will have a unique stamp, but note that in
	 * userspace, several meminfos across multiple processes could have
	 * the same stamp. As the native_handle can be dup(2)'d, there could be
	 * multiple handles with the same stamp but different file descriptors.
	 */
	unsigned long long ui64Stamp;

	/* This is used for buffer usage validation when locking a buffer,
	 * and also in WSEGL (for the composition bypass feature).
	 */
	int flags;

	/* In order to do efficient cache flushes we need the buffer dimensions
	 * and format. These are available on the ANativeWindowBuffer,
	 * but the platform doesn't pass them down to the graphics HAL.
	 *
	 * These fields are also used in the composition bypass. In this
	 * capacity, these are the "real" values for the backing allocation.
	 */
	int width;
	int height;
	int format;
	unsigned int uiBpp;

	/* The ion allocation path doesn't allow for the allocation size and
	 * mapping flags to be communicated cross-process automatically.
	 * Cache these here so we can map buffers in client processes.
	 */
	unsigned int uiAllocSize[MAX_SUB_ALLOCS];
	unsigned int uiFlags;

	/* S.LSI 12.05.23 added below variables for HWC */
	int stride;
	int vstride;
	int baseaddr;
	int uaddr;
	int vaddr;
	/* S.LSI 12.07.20 added below variables for yuv per plane ion */
	int iNumIonFds;

	/* S.LSI 12.08.10 added below flags variable for store exynos specific misc informations */
	unsigned int uiExynosMiscFlags;
#ifdef __cplusplus
        static struct IMG_native_handle_t* dynamicCast(const native_handle* in)
        {
            if (in)
                return (struct IMG_native_handle_t *) in;

            return NULL;
        }
#endif
}
__attribute__((aligned(sizeof(int)),packed)) IMG_native_handle_t;

typedef IMG_native_handle_t private_handle_t;
#if defined(SUPPORT_ANDROID_FRAMEBUFFER_HAL)

typedef struct
{
	framebuffer_device_t base;

	/* The HWC was loaded. post() is no longer responsible for presents */
	int bBypassPost;

	/* HWC path for present posts */
	int (*Post2)(framebuffer_device_t *fb, buffer_handle_t *buffers,
				 int num_buffers, void *data, int data_length);
}
IMG_framebuffer_device_public_t;

#endif /* defined(SUPPORT_ANDROID_FRAMEBUFFER_HAL) */

typedef struct IMG_gralloc_module_public_t
{
	gralloc_module_t base;

#if defined(SUPPORT_ANDROID_FRAMEBUFFER_HAL)
	/* If the framebuffer has been opened, this will point to the
	 * framebuffer device data required by the allocator, WSEGL
	 * modules and composerhal.
	 */
	IMG_framebuffer_device_public_t *psFrameBufferDevice;
#endif /* defined(SUPPORT_ANDROID_FRAMEBUFFER_HAL) */

	int (*GetPhyAddrs)(struct IMG_gralloc_module_public_t const* module,
					   buffer_handle_t handle,
					   unsigned int auiPhyAddr[MAX_SUB_ALLOCS]);

	/* Custom-blit components in lieu of overlay hardware */
	int (*Blit)(struct IMG_gralloc_module_public_t const *module,
				buffer_handle_t src,
				void *dest[MAX_SUB_ALLOCS], int format);

	int (*Blit2)(struct IMG_gralloc_module_public_t const *module,
				 buffer_handle_t src, buffer_handle_t dest,
				 int w, int h, int x, int y);

	int (*Blit3)(struct IMG_gralloc_module_public_t const *module,
				 unsigned long long ui64SrcStamp,
				 unsigned long long ui64DestStamp,
				 int eSrcRotation, int eDestRotation);

	/*S.LSI*/
	int (*FinishPVRRender)(struct IMG_gralloc_module_public_t const *module,
				buffer_handle_t *buffers, int num_buffers);
}
IMG_gralloc_module_public_t;

typedef IMG_gralloc_module_public_t private_module_t;

typedef struct
{
	int l, t, w, h;
}
IMG_write_lock_rect_t;
#if !defined(CONFIG_EXYNOS_ION)
typedef int (*IMG_buffer_format_compute_params_pfn)(
	unsigned int uiPlane, int *piWidth, int *piHeight, int *piStride,
	unsigned long *pulPlaneOffset);
#else
typedef int (*IMG_buffer_format_compute_params_pfn)(
	unsigned int uiPlane, int *piWidth, int *piHeight, int *piStride, int *pVStride,
	unsigned long *pulPlaneOffset);
#endif

typedef struct IMG_buffer_format_public_t
{
	/* Buffer formats are returned as a linked list */
	struct IMG_buffer_format_public_t *psNext;

	/* HAL_PIXEL_FORMAT_... enumerant */
	int iHalPixelFormat;

	/* WSEGL_PIXELFORMAT_... enumerant */
	int iWSEGLPixelFormat;

	/* Friendly name for format */
	const char *const szName;

	/* Bits (not bytes) per pixel */
	unsigned int uiBpp;

	/* GPU output format (creates EGLConfig for format) */
	int bGPURenderable;

	/* Preferred framebuffer format */
	int bPreferred;

	/* Utility function for adjusting YUV per-plane parameters */
	IMG_buffer_format_compute_params_pfn pfnComputeParams;
}
IMG_buffer_format_public_t;

#endif /* HAL_PUBLIC_H */
