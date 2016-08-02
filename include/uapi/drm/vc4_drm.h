/*
 * Copyright © 2014-2015 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _UAPI_VC4_DRM_H_
#define _UAPI_VC4_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_VC4_SUBMIT_CL                         0x00
#define DRM_VC4_WAIT_SEQNO                        0x01
#define DRM_VC4_WAIT_BO                           0x02
#define DRM_VC4_CREATE_BO                         0x03
#define DRM_VC4_MMAP_BO                           0x04
#define DRM_VC4_CREATE_SHADER_BO                  0x05
#define DRM_VC4_GET_HANG_STATE                    0x06
#define DRM_VC4_GET_PARAM                         0x07

#define DRM_IOCTL_VC4_SUBMIT_CL           DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_SUBMIT_CL, struct drm_vc4_submit_cl)
#define DRM_IOCTL_VC4_WAIT_SEQNO          DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_WAIT_SEQNO, struct drm_vc4_wait_seqno)
#define DRM_IOCTL_VC4_WAIT_BO             DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_WAIT_BO, struct drm_vc4_wait_bo)
#define DRM_IOCTL_VC4_CREATE_BO           DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_CREATE_BO, struct drm_vc4_create_bo)
#define DRM_IOCTL_VC4_MMAP_BO             DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_MMAP_BO, struct drm_vc4_mmap_bo)
#define DRM_IOCTL_VC4_CREATE_SHADER_BO    DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_CREATE_SHADER_BO, struct drm_vc4_create_shader_bo)
#define DRM_IOCTL_VC4_GET_HANG_STATE      DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_GET_HANG_STATE, struct drm_vc4_get_hang_state)
#define DRM_IOCTL_VC4_GET_PARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_GET_PARAM, struct drm_vc4_get_param)

struct drm_vc4_submit_rcl_surface {
	__u32 hindex; /* Handle index, or ~0 if not present. */
	__u32 offset; /* Offset to start of buffer. */
	/*
	 * Bits for either render config (color_write) or load/store packet.
	 * Bits should all be 0 for MSAA load/stores.
	 */
	__u16 bits;

#define VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES		(1 << 0)
	__u16 flags;
};

/**
 * struct drm_vc4_submit_cl - ioctl argument for submitting commands to the 3D
 * engine.
 *
 * Drivers typically use GPU BOs to store batchbuffers / command lists and
 * their associated state.  However, because the VC4 lacks an MMU, we have to
 * do validation of memory accesses by the GPU commands.  If we were to store
 * our commands in BOs, we'd need to do uncached readback from them to do the
 * validation process, which is too expensive.  Instead, userspace accumulates
 * commands and associated state in plain memory, then the kernel copies the
 * data to its own address space, and then validates and stores it in a GPU
 * BO.
 */
struct drm_vc4_submit_cl {
	/* Pointer to the binner command list.
	 *
	 * This is the first set of commands executed, which runs the
	 * coordinate shader to determine where primitives land on the screen,
	 * then writes out the state updates and draw calls necessary per tile
	 * to the tile allocation BO.
	 */
	__u64 bin_cl;

	/* Pointer to the shader records.
	 *
	 * Shader records are the structures read by the hardware that contain
	 * pointers to uniforms, shaders, and vertex attributes.  The
	 * reference to the shader record has enough information to determine
	 * how many pointers are necessary (fixed number for shaders/uniforms,
	 * and an attribute count), so those BO indices into bo_handles are
	 * just stored as __u32s before each shader record passed in.
	 */
	__u64 shader_rec;

	/* Pointer to uniform data and texture handles for the textures
	 * referenced by the shader.
	 *
	 * For each shader state record, there is a set of uniform data in the
	 * order referenced by the record (FS, VS, then CS).  Each set of
	 * uniform data has a __u32 index into bo_handles per texture
	 * sample operation, in the order the QPU_W_TMUn_S writes appear in
	 * the program.  Following the texture BO handle indices is the actual
	 * uniform data.
	 *
	 * The individual uniform state blocks don't have sizes passed in,
	 * because the kernel has to determine the sizes anyway during shader
	 * code validation.
	 */
	__u64 uniforms;
	__u64 bo_handles;

	/* Size in bytes of the binner command list. */
	__u32 bin_cl_size;
	/* Size in bytes of the set of shader records. */
	__u32 shader_rec_size;
	/* Number of shader records.
	 *
	 * This could just be computed from the contents of shader_records and
	 * the address bits of references to them from the bin CL, but it
	 * keeps the kernel from having to resize some allocations it makes.
	 */
	__u32 shader_rec_count;
	/* Size in bytes of the uniform state. */
	__u32 uniforms_size;

	/* Number of BO handles passed in (size is that times 4). */
	__u32 bo_handle_count;

	/* RCL setup: */
	__u16 width;
	__u16 height;
	__u8 min_x_tile;
	__u8 min_y_tile;
	__u8 max_x_tile;
	__u8 max_y_tile;
	struct drm_vc4_submit_rcl_surface color_read;
	struct drm_vc4_submit_rcl_surface color_write;
	struct drm_vc4_submit_rcl_surface zs_read;
	struct drm_vc4_submit_rcl_surface zs_write;
	struct drm_vc4_submit_rcl_surface msaa_color_write;
	struct drm_vc4_submit_rcl_surface msaa_zs_write;
	__u32 clear_color[2];
	__u32 clear_z;
	__u8 clear_s;

	__u32 pad:24;

#define VC4_SUBMIT_CL_USE_CLEAR_COLOR			(1 << 0)
	__u32 flags;

	/* Returned value of the seqno of this render job (for the
	 * wait ioctl).
	 */
	__u64 seqno;
};

/**
 * struct drm_vc4_wait_seqno - ioctl argument for waiting for
 * DRM_VC4_SUBMIT_CL completion using its returned seqno.
 *
 * timeout_ns is the timeout in nanoseconds, where "0" means "don't
 * block, just return the status."
 */
struct drm_vc4_wait_seqno {
	__u64 seqno;
	__u64 timeout_ns;
};

/**
 * struct drm_vc4_wait_bo - ioctl argument for waiting for
 * completion of the last DRM_VC4_SUBMIT_CL on a BO.
 *
 * This is useful for cases where multiple processes might be
 * rendering to a BO and you want to wait for all rendering to be
 * completed.
 */
struct drm_vc4_wait_bo {
	__u32 handle;
	__u32 pad;
	__u64 timeout_ns;
};

/**
 * struct drm_vc4_create_bo - ioctl argument for creating VC4 BOs.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_vc4_create_bo {
	__u32 size;
	__u32 flags;
	/** Returned GEM handle for the BO. */
	__u32 handle;
	__u32 pad;
};

/**
 * struct drm_vc4_mmap_bo - ioctl argument for mapping VC4 BOs.
 *
 * This doesn't actually perform an mmap.  Instead, it returns the
 * offset you need to use in an mmap on the DRM device node.  This
 * means that tools like valgrind end up knowing about the mapped
 * memory.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_vc4_mmap_bo {
	/** Handle for the object being mapped. */
	__u32 handle;
	__u32 flags;
	/** offset into the drm node to use for subsequent mmap call. */
	__u64 offset;
};

/**
 * struct drm_vc4_create_shader_bo - ioctl argument for creating VC4
 * shader BOs.
 *
 * Since allowing a shader to be overwritten while it's also being
 * executed from would allow privlege escalation, shaders must be
 * created using this ioctl, and they can't be mmapped later.
 */
struct drm_vc4_create_shader_bo {
	/* Size of the data argument. */
	__u32 size;
	/* Flags, currently must be 0. */
	__u32 flags;

	/* Pointer to the data. */
	__u64 data;

	/** Returned GEM handle for the BO. */
	__u32 handle;
	/* Pad, must be 0. */
	__u32 pad;
};

struct drm_vc4_get_hang_state_bo {
	__u32 handle;
	__u32 paddr;
	__u32 size;
	__u32 pad;
};

/**
 * struct drm_vc4_hang_state - ioctl argument for collecting state
 * from a GPU hang for analysis.
*/
struct drm_vc4_get_hang_state {
	/** Pointer to array of struct drm_vc4_get_hang_state_bo. */
	__u64 bo;
	/**
	 * On input, the size of the bo array.  Output is the number
	 * of bos to be returned.
	 */
	__u32 bo_count;

	__u32 start_bin, start_render;

	__u32 ct0ca, ct0ea;
	__u32 ct1ca, ct1ea;
	__u32 ct0cs, ct1cs;
	__u32 ct0ra0, ct1ra0;

	__u32 bpca, bpcs;
	__u32 bpoa, bpos;

	__u32 vpmbase;

	__u32 dbge;
	__u32 fdbgo;
	__u32 fdbgb;
	__u32 fdbgr;
	__u32 fdbgs;
	__u32 errstat;

	/* Pad that we may save more registers into in the future. */
	__u32 pad[16];
};

#define DRM_VC4_PARAM_V3D_IDENT0		0
#define DRM_VC4_PARAM_V3D_IDENT1		1
#define DRM_VC4_PARAM_V3D_IDENT2		2
#define DRM_VC4_PARAM_SUPPORTS_BRANCHES		3

struct drm_vc4_get_param {
	__u32 param;
	__u32 pad;
	__u64 value;
};

#if defined(__cplusplus)
}
#endif

#endif /* _UAPI_VC4_DRM_H_ */
