/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef __VMWGFX_DRM_H__
#define __VMWGFX_DRM_H__

#define DRM_VMW_MAX_SURFACE_FACES 6
#define DRM_VMW_MAX_MIP_LEVELS 24


#define DRM_VMW_GET_PARAM            0
#define DRM_VMW_ALLOC_DMABUF         1
#define DRM_VMW_UNREF_DMABUF         2
#define DRM_VMW_CURSOR_BYPASS        3
/* guarded by DRM_VMW_PARAM_NUM_STREAMS != 0*/
#define DRM_VMW_CONTROL_STREAM       4
#define DRM_VMW_CLAIM_STREAM         5
#define DRM_VMW_UNREF_STREAM         6
/* guarded by DRM_VMW_PARAM_3D == 1 */
#define DRM_VMW_CREATE_CONTEXT       7
#define DRM_VMW_UNREF_CONTEXT        8
#define DRM_VMW_CREATE_SURFACE       9
#define DRM_VMW_UNREF_SURFACE        10
#define DRM_VMW_REF_SURFACE          11
#define DRM_VMW_EXECBUF              12
#define DRM_VMW_GET_3D_CAP           13
#define DRM_VMW_FENCE_WAIT           14
#define DRM_VMW_FENCE_SIGNALED       15
#define DRM_VMW_FENCE_UNREF          16
#define DRM_VMW_FENCE_EVENT          17
#define DRM_VMW_PRESENT              18
#define DRM_VMW_PRESENT_READBACK     19
#define DRM_VMW_UPDATE_LAYOUT        20

/*************************************************************************/
/**
 * DRM_VMW_GET_PARAM - get device information.
 *
 * DRM_VMW_PARAM_FIFO_OFFSET:
 * Offset to use to map the first page of the FIFO read-only.
 * The fifo is mapped using the mmap() system call on the drm device.
 *
 * DRM_VMW_PARAM_OVERLAY_IOCTL:
 * Does the driver support the overlay ioctl.
 */

#define DRM_VMW_PARAM_NUM_STREAMS      0
#define DRM_VMW_PARAM_NUM_FREE_STREAMS 1
#define DRM_VMW_PARAM_3D               2
#define DRM_VMW_PARAM_HW_CAPS          3
#define DRM_VMW_PARAM_FIFO_CAPS        4
#define DRM_VMW_PARAM_MAX_FB_SIZE      5
#define DRM_VMW_PARAM_FIFO_HW_VERSION  6

/**
 * struct drm_vmw_getparam_arg
 *
 * @value: Returned value. //Out
 * @param: Parameter to query. //In.
 *
 * Argument to the DRM_VMW_GET_PARAM Ioctl.
 */

struct drm_vmw_getparam_arg {
	uint64_t value;
	uint32_t param;
	uint32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_CREATE_CONTEXT - Create a host context.
 *
 * Allocates a device unique context id, and queues a create context command
 * for the host. Does not wait for host completion.
 */

/**
 * struct drm_vmw_context_arg
 *
 * @cid: Device unique context ID.
 *
 * Output argument to the DRM_VMW_CREATE_CONTEXT Ioctl.
 * Input argument to the DRM_VMW_UNREF_CONTEXT Ioctl.
 */

struct drm_vmw_context_arg {
	int32_t cid;
	uint32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_UNREF_CONTEXT - Create a host context.
 *
 * Frees a global context id, and queues a destroy host command for the host.
 * Does not wait for host completion. The context ID can be used directly
 * in the command stream and shows up as the same context ID on the host.
 */

/*************************************************************************/
/**
 * DRM_VMW_CREATE_SURFACE - Create a host suface.
 *
 * Allocates a device unique surface id, and queues a create surface command
 * for the host. Does not wait for host completion. The surface ID can be
 * used directly in the command stream and shows up as the same surface
 * ID on the host.
 */

/**
 * struct drm_wmv_surface_create_req
 *
 * @flags: Surface flags as understood by the host.
 * @format: Surface format as understood by the host.
 * @mip_levels: Number of mip levels for each face.
 * An unused face should have 0 encoded.
 * @size_addr: Address of a user-space array of sruct drm_vmw_size
 * cast to an uint64_t for 32-64 bit compatibility.
 * The size of the array should equal the total number of mipmap levels.
 * @shareable: Boolean whether other clients (as identified by file descriptors)
 * may reference this surface.
 * @scanout: Boolean whether the surface is intended to be used as a
 * scanout.
 *
 * Input data to the DRM_VMW_CREATE_SURFACE Ioctl.
 * Output data from the DRM_VMW_REF_SURFACE Ioctl.
 */

struct drm_vmw_surface_create_req {
	uint32_t flags;
	uint32_t format;
	uint32_t mip_levels[DRM_VMW_MAX_SURFACE_FACES];
	uint64_t size_addr;
	int32_t shareable;
	int32_t scanout;
};

/**
 * struct drm_wmv_surface_arg
 *
 * @sid: Surface id of created surface or surface to destroy or reference.
 *
 * Output data from the DRM_VMW_CREATE_SURFACE Ioctl.
 * Input argument to the DRM_VMW_UNREF_SURFACE Ioctl.
 * Input argument to the DRM_VMW_REF_SURFACE Ioctl.
 */

struct drm_vmw_surface_arg {
	int32_t sid;
	uint32_t pad64;
};

/**
 * struct drm_vmw_size ioctl.
 *
 * @width - mip level width
 * @height - mip level height
 * @depth - mip level depth
 *
 * Description of a mip level.
 * Input data to the DRM_WMW_CREATE_SURFACE Ioctl.
 */

struct drm_vmw_size {
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t pad64;
};

/**
 * union drm_vmw_surface_create_arg
 *
 * @rep: Output data as described above.
 * @req: Input data as described above.
 *
 * Argument to the DRM_VMW_CREATE_SURFACE Ioctl.
 */

union drm_vmw_surface_create_arg {
	struct drm_vmw_surface_arg rep;
	struct drm_vmw_surface_create_req req;
};

/*************************************************************************/
/**
 * DRM_VMW_REF_SURFACE - Reference a host surface.
 *
 * Puts a reference on a host surface with a give sid, as previously
 * returned by the DRM_VMW_CREATE_SURFACE ioctl.
 * A reference will make sure the surface isn't destroyed while we hold
 * it and will allow the calling client to use the surface ID in the command
 * stream.
 *
 * On successful return, the Ioctl returns the surface information given
 * in the DRM_VMW_CREATE_SURFACE ioctl.
 */

/**
 * union drm_vmw_surface_reference_arg
 *
 * @rep: Output data as described above.
 * @req: Input data as described above.
 *
 * Argument to the DRM_VMW_REF_SURFACE Ioctl.
 */

union drm_vmw_surface_reference_arg {
	struct drm_vmw_surface_create_req rep;
	struct drm_vmw_surface_arg req;
};

/*************************************************************************/
/**
 * DRM_VMW_UNREF_SURFACE - Unreference a host surface.
 *
 * Clear a reference previously put on a host surface.
 * When all references are gone, including the one implicitly placed
 * on creation,
 * a destroy surface command will be queued for the host.
 * Does not wait for completion.
 */

/*************************************************************************/
/**
 * DRM_VMW_EXECBUF
 *
 * Submit a command buffer for execution on the host, and return a
 * fence seqno that when signaled, indicates that the command buffer has
 * executed.
 */

/**
 * struct drm_vmw_execbuf_arg
 *
 * @commands: User-space address of a command buffer cast to an uint64_t.
 * @command-size: Size in bytes of the command buffer.
 * @throttle-us: Sleep until software is less than @throttle_us
 * microseconds ahead of hardware. The driver may round this value
 * to the nearest kernel tick.
 * @fence_rep: User-space address of a struct drm_vmw_fence_rep cast to an
 * uint64_t.
 * @version: Allows expanding the execbuf ioctl parameters without breaking
 * backwards compatibility, since user-space will always tell the kernel
 * which version it uses.
 * @flags: Execbuf flags. None currently.
 *
 * Argument to the DRM_VMW_EXECBUF Ioctl.
 */

#define DRM_VMW_EXECBUF_VERSION 1

struct drm_vmw_execbuf_arg {
	uint64_t commands;
	uint32_t command_size;
	uint32_t throttle_us;
	uint64_t fence_rep;
	uint32_t version;
	uint32_t flags;
};

/**
 * struct drm_vmw_fence_rep
 *
 * @handle: Fence object handle for fence associated with a command submission.
 * @mask: Fence flags relevant for this fence object.
 * @seqno: Fence sequence number in fifo. A fence object with a lower
 * seqno will signal the EXEC flag before a fence object with a higher
 * seqno. This can be used by user-space to avoid kernel calls to determine
 * whether a fence has signaled the EXEC flag. Note that @seqno will
 * wrap at 32-bit.
 * @passed_seqno: The highest seqno number processed by the hardware
 * so far. This can be used to mark user-space fence objects as signaled, and
 * to determine whether a fence seqno might be stale.
 * @error: This member should've been set to -EFAULT on submission.
 * The following actions should be take on completion:
 * error == -EFAULT: Fence communication failed. The host is synchronized.
 * Use the last fence id read from the FIFO fence register.
 * error != 0 && error != -EFAULT:
 * Fence submission failed. The host is synchronized. Use the fence_seq member.
 * error == 0: All is OK, The host may not be synchronized.
 * Use the fence_seq member.
 *
 * Input / Output data to the DRM_VMW_EXECBUF Ioctl.
 */

struct drm_vmw_fence_rep {
	uint32_t handle;
	uint32_t mask;
	uint32_t seqno;
	uint32_t passed_seqno;
	uint32_t pad64;
	int32_t error;
};

/*************************************************************************/
/**
 * DRM_VMW_ALLOC_DMABUF
 *
 * Allocate a DMA buffer that is visible also to the host.
 * NOTE: The buffer is
 * identified by a handle and an offset, which are private to the guest, but
 * useable in the command stream. The guest kernel may translate these
 * and patch up the command stream accordingly. In the future, the offset may
 * be zero at all times, or it may disappear from the interface before it is
 * fixed.
 *
 * The DMA buffer may stay user-space mapped in the guest at all times,
 * and is thus suitable for sub-allocation.
 *
 * DMA buffers are mapped using the mmap() syscall on the drm device.
 */

/**
 * struct drm_vmw_alloc_dmabuf_req
 *
 * @size: Required minimum size of the buffer.
 *
 * Input data to the DRM_VMW_ALLOC_DMABUF Ioctl.
 */

struct drm_vmw_alloc_dmabuf_req {
	uint32_t size;
	uint32_t pad64;
};

/**
 * struct drm_vmw_dmabuf_rep
 *
 * @map_handle: Offset to use in the mmap() call used to map the buffer.
 * @handle: Handle unique to this buffer. Used for unreferencing.
 * @cur_gmr_id: GMR id to use in the command stream when this buffer is
 * referenced. See not above.
 * @cur_gmr_offset: Offset to use in the command stream when this buffer is
 * referenced. See note above.
 *
 * Output data from the DRM_VMW_ALLOC_DMABUF Ioctl.
 */

struct drm_vmw_dmabuf_rep {
	uint64_t map_handle;
	uint32_t handle;
	uint32_t cur_gmr_id;
	uint32_t cur_gmr_offset;
	uint32_t pad64;
};

/**
 * union drm_vmw_dmabuf_arg
 *
 * @req: Input data as described above.
 * @rep: Output data as described above.
 *
 * Argument to the DRM_VMW_ALLOC_DMABUF Ioctl.
 */

union drm_vmw_alloc_dmabuf_arg {
	struct drm_vmw_alloc_dmabuf_req req;
	struct drm_vmw_dmabuf_rep rep;
};

/*************************************************************************/
/**
 * DRM_VMW_UNREF_DMABUF - Free a DMA buffer.
 *
 */

/**
 * struct drm_vmw_unref_dmabuf_arg
 *
 * @handle: Handle indicating what buffer to free. Obtained from the
 * DRM_VMW_ALLOC_DMABUF Ioctl.
 *
 * Argument to the DRM_VMW_UNREF_DMABUF Ioctl.
 */

struct drm_vmw_unref_dmabuf_arg {
	uint32_t handle;
	uint32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_CONTROL_STREAM - Control overlays, aka streams.
 *
 * This IOCTL controls the overlay units of the svga device.
 * The SVGA overlay units does not work like regular hardware units in
 * that they do not automaticaly read back the contents of the given dma
 * buffer. But instead only read back for each call to this ioctl, and
 * at any point between this call being made and a following call that
 * either changes the buffer or disables the stream.
 */

/**
 * struct drm_vmw_rect
 *
 * Defines a rectangle. Used in the overlay ioctl to define
 * source and destination rectangle.
 */

struct drm_vmw_rect {
	int32_t x;
	int32_t y;
	uint32_t w;
	uint32_t h;
};

/**
 * struct drm_vmw_control_stream_arg
 *
 * @stream_id: Stearm to control
 * @enabled: If false all following arguments are ignored.
 * @handle: Handle to buffer for getting data from.
 * @format: Format of the overlay as understood by the host.
 * @width: Width of the overlay.
 * @height: Height of the overlay.
 * @size: Size of the overlay in bytes.
 * @pitch: Array of pitches, the two last are only used for YUV12 formats.
 * @offset: Offset from start of dma buffer to overlay.
 * @src: Source rect, must be within the defined area above.
 * @dst: Destination rect, x and y may be negative.
 *
 * Argument to the DRM_VMW_CONTROL_STREAM Ioctl.
 */

struct drm_vmw_control_stream_arg {
	uint32_t stream_id;
	uint32_t enabled;

	uint32_t flags;
	uint32_t color_key;

	uint32_t handle;
	uint32_t offset;
	int32_t format;
	uint32_t size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch[3];

	uint32_t pad64;
	struct drm_vmw_rect src;
	struct drm_vmw_rect dst;
};

/*************************************************************************/
/**
 * DRM_VMW_CURSOR_BYPASS - Give extra information about cursor bypass.
 *
 */

#define DRM_VMW_CURSOR_BYPASS_ALL    (1 << 0)
#define DRM_VMW_CURSOR_BYPASS_FLAGS       (1)

/**
 * struct drm_vmw_cursor_bypass_arg
 *
 * @flags: Flags.
 * @crtc_id: Crtc id, only used if DMR_CURSOR_BYPASS_ALL isn't passed.
 * @xpos: X position of cursor.
 * @ypos: Y position of cursor.
 * @xhot: X hotspot.
 * @yhot: Y hotspot.
 *
 * Argument to the DRM_VMW_CURSOR_BYPASS Ioctl.
 */

struct drm_vmw_cursor_bypass_arg {
	uint32_t flags;
	uint32_t crtc_id;
	int32_t xpos;
	int32_t ypos;
	int32_t xhot;
	int32_t yhot;
};

/*************************************************************************/
/**
 * DRM_VMW_CLAIM_STREAM - Claim a single stream.
 */

/**
 * struct drm_vmw_context_arg
 *
 * @stream_id: Device unique context ID.
 *
 * Output argument to the DRM_VMW_CREATE_CONTEXT Ioctl.
 * Input argument to the DRM_VMW_UNREF_CONTEXT Ioctl.
 */

struct drm_vmw_stream_arg {
	uint32_t stream_id;
	uint32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_UNREF_STREAM - Unclaim a stream.
 *
 * Return a single stream that was claimed by this process. Also makes
 * sure that the stream has been stopped.
 */

/*************************************************************************/
/**
 * DRM_VMW_GET_3D_CAP
 *
 * Read 3D capabilities from the FIFO
 *
 */

/**
 * struct drm_vmw_get_3d_cap_arg
 *
 * @buffer: Pointer to a buffer for capability data, cast to an uint64_t
 * @size: Max size to copy
 *
 * Input argument to the DRM_VMW_GET_3D_CAP_IOCTL
 * ioctls.
 */

struct drm_vmw_get_3d_cap_arg {
	uint64_t buffer;
	uint32_t max_size;
	uint32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_FENCE_WAIT
 *
 * Waits for a fence object to signal. The wait is interruptible, so that
 * signals may be delivered during the interrupt. The wait may timeout,
 * in which case the calls returns -EBUSY. If the wait is restarted,
 * that is restarting without resetting @cookie_valid to zero,
 * the timeout is computed from the first call.
 *
 * The flags argument to the DRM_VMW_FENCE_WAIT ioctl indicates what to wait
 * on:
 * DRM_VMW_FENCE_FLAG_EXEC: All commands ahead of the fence in the command
 * stream
 * have executed.
 * DRM_VMW_FENCE_FLAG_QUERY: All query results resulting from query finish
 * commands
 * in the buffer given to the EXECBUF ioctl returning the fence object handle
 * are available to user-space.
 *
 * DRM_VMW_WAIT_OPTION_UNREF: If this wait option is given, and the
 * fenc wait ioctl returns 0, the fence object has been unreferenced after
 * the wait.
 */

#define DRM_VMW_FENCE_FLAG_EXEC   (1 << 0)
#define DRM_VMW_FENCE_FLAG_QUERY  (1 << 1)

#define DRM_VMW_WAIT_OPTION_UNREF (1 << 0)

/**
 * struct drm_vmw_fence_wait_arg
 *
 * @handle: Fence object handle as returned by the DRM_VMW_EXECBUF ioctl.
 * @cookie_valid: Must be reset to 0 on first call. Left alone on restart.
 * @kernel_cookie: Set to 0 on first call. Left alone on restart.
 * @timeout_us: Wait timeout in microseconds. 0 for indefinite timeout.
 * @lazy: Set to 1 if timing is not critical. Allow more than a kernel tick
 * before returning.
 * @flags: Fence flags to wait on.
 * @wait_options: Options that control the behaviour of the wait ioctl.
 *
 * Input argument to the DRM_VMW_FENCE_WAIT ioctl.
 */

struct drm_vmw_fence_wait_arg {
	uint32_t handle;
	int32_t  cookie_valid;
	uint64_t kernel_cookie;
	uint64_t timeout_us;
	int32_t lazy;
	int32_t flags;
	int32_t wait_options;
	int32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_FENCE_SIGNALED
 *
 * Checks if a fence object is signaled..
 */

/**
 * struct drm_vmw_fence_signaled_arg
 *
 * @handle: Fence object handle as returned by the DRM_VMW_EXECBUF ioctl.
 * @flags: Fence object flags input to DRM_VMW_FENCE_SIGNALED ioctl
 * @signaled: Out: Flags signaled.
 * @sequence: Out: Highest sequence passed so far. Can be used to signal the
 * EXEC flag of user-space fence objects.
 *
 * Input/Output argument to the DRM_VMW_FENCE_SIGNALED and DRM_VMW_FENCE_UNREF
 * ioctls.
 */

struct drm_vmw_fence_signaled_arg {
	 uint32_t handle;
	 uint32_t flags;
	 int32_t signaled;
	 uint32_t passed_seqno;
	 uint32_t signaled_flags;
	 uint32_t pad64;
};

/*************************************************************************/
/**
 * DRM_VMW_FENCE_UNREF
 *
 * Unreferences a fence object, and causes it to be destroyed if there are no
 * other references to it.
 *
 */

/**
 * struct drm_vmw_fence_arg
 *
 * @handle: Fence object handle as returned by the DRM_VMW_EXECBUF ioctl.
 *
 * Input/Output argument to the DRM_VMW_FENCE_UNREF ioctl..
 */

struct drm_vmw_fence_arg {
	 uint32_t handle;
	 uint32_t pad64;
};


/*************************************************************************/
/**
 * DRM_VMW_FENCE_EVENT
 *
 * Queues an event on a fence to be delivered on the drm character device
 * when the fence has signaled the DRM_VMW_FENCE_FLAG_EXEC flag.
 * Optionally the approximate time when the fence signaled is
 * given by the event.
 */

/*
 * The event type
 */
#define DRM_VMW_EVENT_FENCE_SIGNALED 0x80000000

struct drm_vmw_event_fence {
	struct drm_event base;
	uint64_t user_data;
	uint32_t tv_sec;
	uint32_t tv_usec;
};

/*
 * Flags that may be given to the command.
 */
/* Request fence signaled time on the event. */
#define DRM_VMW_FE_FLAG_REQ_TIME (1 << 0)

/**
 * struct drm_vmw_fence_event_arg
 *
 * @fence_rep: Pointer to fence_rep structure cast to uint64_t or 0 if
 * the fence is not supposed to be referenced by user-space.
 * @user_info: Info to be delivered with the event.
 * @handle: Attach the event to this fence only.
 * @flags: A set of flags as defined above.
 */
struct drm_vmw_fence_event_arg {
	uint64_t fence_rep;
	uint64_t user_data;
	uint32_t handle;
	uint32_t flags;
};


/*************************************************************************/
/**
 * DRM_VMW_PRESENT
 *
 * Executes an SVGA present on a given fb for a given surface. The surface
 * is placed on the framebuffer. Cliprects are given relative to the given
 * point (the point disignated by dest_{x|y}).
 *
 */

/**
 * struct drm_vmw_present_arg
 * @fb_id: framebuffer id to present / read back from.
 * @sid: Surface id to present from.
 * @dest_x: X placement coordinate for surface.
 * @dest_y: Y placement coordinate for surface.
 * @clips_ptr: Pointer to an array of clip rects cast to an uint64_t.
 * @num_clips: Number of cliprects given relative to the framebuffer origin,
 * in the same coordinate space as the frame buffer.
 * @pad64: Unused 64-bit padding.
 *
 * Input argument to the DRM_VMW_PRESENT ioctl.
 */

struct drm_vmw_present_arg {
	uint32_t fb_id;
	uint32_t sid;
	int32_t dest_x;
	int32_t dest_y;
	uint64_t clips_ptr;
	uint32_t num_clips;
	uint32_t pad64;
};


/*************************************************************************/
/**
 * DRM_VMW_PRESENT_READBACK
 *
 * Executes an SVGA present readback from a given fb to the dma buffer
 * currently bound as the fb. If there is no dma buffer bound to the fb,
 * an error will be returned.
 *
 */

/**
 * struct drm_vmw_present_arg
 * @fb_id: fb_id to present / read back from.
 * @num_clips: Number of cliprects.
 * @clips_ptr: Pointer to an array of clip rects cast to an uint64_t.
 * @fence_rep: Pointer to a struct drm_vmw_fence_rep, cast to an uint64_t.
 * If this member is NULL, then the ioctl should not return a fence.
 */

struct drm_vmw_present_readback_arg {
	 uint32_t fb_id;
	 uint32_t num_clips;
	 uint64_t clips_ptr;
	 uint64_t fence_rep;
};

/*************************************************************************/
/**
 * DRM_VMW_UPDATE_LAYOUT - Update layout
 *
 * Updates the preferred modes and connection status for connectors. The
 * command consists of one drm_vmw_update_layout_arg pointing to an array
 * of num_outputs drm_vmw_rect's.
 */

/**
 * struct drm_vmw_update_layout_arg
 *
 * @num_outputs: number of active connectors
 * @rects: pointer to array of drm_vmw_rect cast to an uint64_t
 *
 * Input argument to the DRM_VMW_UPDATE_LAYOUT Ioctl.
 */
struct drm_vmw_update_layout_arg {
	uint32_t num_outputs;
	uint32_t pad64;
	uint64_t rects;
};

#endif
