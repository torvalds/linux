/* r128_drm.h -- Public header for the r128 driver -*- linux-c -*-
 * Created: Wed Apr  5 19:24:19 2000 by kevin@precisioninsight.com
 */
/*
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 *    Kevin E. Martin <martin@valinux.com>
 */

#ifndef __R128_DRM_H__
#define __R128_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the X server file (r128_sarea.h)
 */
#ifndef __R128_SAREA_DEFINES__
#define __R128_SAREA_DEFINES__

/* What needs to be changed for the current vertex buffer?
 */
#define R128_UPLOAD_CONTEXT		0x001
#define R128_UPLOAD_SETUP		0x002
#define R128_UPLOAD_TEX0		0x004
#define R128_UPLOAD_TEX1		0x008
#define R128_UPLOAD_TEX0IMAGES		0x010
#define R128_UPLOAD_TEX1IMAGES		0x020
#define R128_UPLOAD_CORE		0x040
#define R128_UPLOAD_MASKS		0x080
#define R128_UPLOAD_WINDOW		0x100
#define R128_UPLOAD_CLIPRECTS		0x200	/* handled client-side */
#define R128_REQUIRE_QUIESCENCE		0x400
#define R128_UPLOAD_ALL			0x7ff

#define R128_FRONT			0x1
#define R128_BACK			0x2
#define R128_DEPTH			0x4

/* Primitive types
 */
#define R128_POINTS			0x1
#define R128_LINES			0x2
#define R128_LINE_STRIP			0x3
#define R128_TRIANGLES			0x4
#define R128_TRIANGLE_FAN		0x5
#define R128_TRIANGLE_STRIP		0x6

/* Vertex/indirect buffer size
 */
#define R128_BUFFER_SIZE		16384

/* Byte offsets for indirect buffer data
 */
#define R128_INDEX_PRIM_OFFSET		20
#define R128_HOSTDATA_BLIT_OFFSET	32

/* Keep these small for testing.
 */
#define R128_NR_SAREA_CLIPRECTS		12

/* There are 2 heaps (local/AGP).  Each region within a heap is a
 *  minimum of 64k, and there are at most 64 of them per heap.
 */
#define R128_LOCAL_TEX_HEAP		0
#define R128_AGP_TEX_HEAP		1
#define R128_NR_TEX_HEAPS		2
#define R128_NR_TEX_REGIONS		64
#define R128_LOG_TEX_GRANULARITY	16

#define R128_NR_CONTEXT_REGS		12

#define R128_MAX_TEXTURE_LEVELS		11
#define R128_MAX_TEXTURE_UNITS		2

#endif				/* __R128_SAREA_DEFINES__ */

typedef struct {
	/* Context state - can be written in one large chunk */
	unsigned int dst_pitch_offset_c;
	unsigned int dp_gui_master_cntl_c;
	unsigned int sc_top_left_c;
	unsigned int sc_bottom_right_c;
	unsigned int z_offset_c;
	unsigned int z_pitch_c;
	unsigned int z_sten_cntl_c;
	unsigned int tex_cntl_c;
	unsigned int misc_3d_state_cntl_reg;
	unsigned int texture_clr_cmp_clr_c;
	unsigned int texture_clr_cmp_msk_c;
	unsigned int fog_color_c;

	/* Texture state */
	unsigned int tex_size_pitch_c;
	unsigned int constant_color_c;

	/* Setup state */
	unsigned int pm4_vc_fpu_setup;
	unsigned int setup_cntl;

	/* Mask state */
	unsigned int dp_write_mask;
	unsigned int sten_ref_mask_c;
	unsigned int plane_3d_mask_c;

	/* Window state */
	unsigned int window_xy_offset;

	/* Core state */
	unsigned int scale_3d_cntl;
} drm_r128_context_regs_t;

/* Setup registers for each texture unit
 */
typedef struct {
	unsigned int tex_cntl;
	unsigned int tex_combine_cntl;
	unsigned int tex_size_pitch;
	unsigned int tex_offset[R128_MAX_TEXTURE_LEVELS];
	unsigned int tex_border_color;
} drm_r128_texture_regs_t;

typedef struct drm_r128_sarea {
	/* The channel for communication of state information to the kernel
	 * on firing a vertex buffer.
	 */
	drm_r128_context_regs_t context_state;
	drm_r128_texture_regs_t tex_state[R128_MAX_TEXTURE_UNITS];
	unsigned int dirty;
	unsigned int vertsize;
	unsigned int vc_format;

	/* The current cliprects, or a subset thereof.
	 */
	struct drm_clip_rect boxes[R128_NR_SAREA_CLIPRECTS];
	unsigned int nbox;

	/* Counters for client-side throttling of rendering clients.
	 */
	unsigned int last_frame;
	unsigned int last_dispatch;

	struct drm_tex_region tex_list[R128_NR_TEX_HEAPS][R128_NR_TEX_REGIONS + 1];
	unsigned int tex_age[R128_NR_TEX_HEAPS];
	int ctx_owner;
	int pfAllowPageFlip;	/* number of 3d windows (0,1,2 or more) */
	int pfCurrentPage;	/* which buffer is being displayed? */
} drm_r128_sarea_t;

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmR128.h)
 */

/* Rage 128 specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_R128_INIT       0x00
#define DRM_R128_CCE_START  0x01
#define DRM_R128_CCE_STOP   0x02
#define DRM_R128_CCE_RESET  0x03
#define DRM_R128_CCE_IDLE   0x04
/* 0x05 not used */
#define DRM_R128_RESET      0x06
#define DRM_R128_SWAP       0x07
#define DRM_R128_CLEAR      0x08
#define DRM_R128_VERTEX     0x09
#define DRM_R128_INDICES    0x0a
#define DRM_R128_BLIT       0x0b
#define DRM_R128_DEPTH      0x0c
#define DRM_R128_STIPPLE    0x0d
/* 0x0e not used */
#define DRM_R128_INDIRECT   0x0f
#define DRM_R128_FULLSCREEN 0x10
#define DRM_R128_CLEAR2     0x11
#define DRM_R128_GETPARAM   0x12
#define DRM_R128_FLIP       0x13

#define DRM_IOCTL_R128_INIT       DRM_IOW( DRM_COMMAND_BASE + DRM_R128_INIT, drm_r128_init_t)
#define DRM_IOCTL_R128_CCE_START  DRM_IO(  DRM_COMMAND_BASE + DRM_R128_CCE_START)
#define DRM_IOCTL_R128_CCE_STOP   DRM_IOW( DRM_COMMAND_BASE + DRM_R128_CCE_STOP, drm_r128_cce_stop_t)
#define DRM_IOCTL_R128_CCE_RESET  DRM_IO(  DRM_COMMAND_BASE + DRM_R128_CCE_RESET)
#define DRM_IOCTL_R128_CCE_IDLE   DRM_IO(  DRM_COMMAND_BASE + DRM_R128_CCE_IDLE)
/* 0x05 not used */
#define DRM_IOCTL_R128_RESET      DRM_IO(  DRM_COMMAND_BASE + DRM_R128_RESET)
#define DRM_IOCTL_R128_SWAP       DRM_IO(  DRM_COMMAND_BASE + DRM_R128_SWAP)
#define DRM_IOCTL_R128_CLEAR      DRM_IOW( DRM_COMMAND_BASE + DRM_R128_CLEAR, drm_r128_clear_t)
#define DRM_IOCTL_R128_VERTEX     DRM_IOW( DRM_COMMAND_BASE + DRM_R128_VERTEX, drm_r128_vertex_t)
#define DRM_IOCTL_R128_INDICES    DRM_IOW( DRM_COMMAND_BASE + DRM_R128_INDICES, drm_r128_indices_t)
#define DRM_IOCTL_R128_BLIT       DRM_IOW( DRM_COMMAND_BASE + DRM_R128_BLIT, drm_r128_blit_t)
#define DRM_IOCTL_R128_DEPTH      DRM_IOW( DRM_COMMAND_BASE + DRM_R128_DEPTH, drm_r128_depth_t)
#define DRM_IOCTL_R128_STIPPLE    DRM_IOW( DRM_COMMAND_BASE + DRM_R128_STIPPLE, drm_r128_stipple_t)
/* 0x0e not used */
#define DRM_IOCTL_R128_INDIRECT   DRM_IOWR(DRM_COMMAND_BASE + DRM_R128_INDIRECT, drm_r128_indirect_t)
#define DRM_IOCTL_R128_FULLSCREEN DRM_IOW( DRM_COMMAND_BASE + DRM_R128_FULLSCREEN, drm_r128_fullscreen_t)
#define DRM_IOCTL_R128_CLEAR2     DRM_IOW( DRM_COMMAND_BASE + DRM_R128_CLEAR2, drm_r128_clear2_t)
#define DRM_IOCTL_R128_GETPARAM   DRM_IOWR( DRM_COMMAND_BASE + DRM_R128_GETPARAM, drm_r128_getparam_t)
#define DRM_IOCTL_R128_FLIP       DRM_IO(  DRM_COMMAND_BASE + DRM_R128_FLIP)

typedef struct drm_r128_init {
	enum {
		R128_INIT_CCE = 0x01,
		R128_CLEANUP_CCE = 0x02
	} func;
	unsigned long sarea_priv_offset;
	int is_pci;
	int cce_mode;
	int cce_secure;
	int ring_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;
	unsigned int span_offset;

	unsigned long fb_offset;
	unsigned long mmio_offset;
	unsigned long ring_offset;
	unsigned long ring_rptr_offset;
	unsigned long buffers_offset;
	unsigned long agp_textures_offset;
} drm_r128_init_t;

typedef struct drm_r128_cce_stop {
	int flush;
	int idle;
} drm_r128_cce_stop_t;

typedef struct drm_r128_clear {
	unsigned int flags;
	unsigned int clear_color;
	unsigned int clear_depth;
	unsigned int color_mask;
	unsigned int depth_mask;
} drm_r128_clear_t;

typedef struct drm_r128_vertex {
	int prim;
	int idx;		/* Index of vertex buffer */
	int count;		/* Number of vertices in buffer */
	int discard;		/* Client finished with buffer? */
} drm_r128_vertex_t;

typedef struct drm_r128_indices {
	int prim;
	int idx;
	int start;
	int end;
	int discard;		/* Client finished with buffer? */
} drm_r128_indices_t;

typedef struct drm_r128_blit {
	int idx;
	int pitch;
	int offset;
	int format;
	unsigned short x, y;
	unsigned short width, height;
} drm_r128_blit_t;

typedef struct drm_r128_depth {
	enum {
		R128_WRITE_SPAN = 0x01,
		R128_WRITE_PIXELS = 0x02,
		R128_READ_SPAN = 0x03,
		R128_READ_PIXELS = 0x04
	} func;
	int n;
	int __user *x;
	int __user *y;
	unsigned int __user *buffer;
	unsigned char __user *mask;
} drm_r128_depth_t;

typedef struct drm_r128_stipple {
	unsigned int __user *mask;
} drm_r128_stipple_t;

typedef struct drm_r128_indirect {
	int idx;
	int start;
	int end;
	int discard;
} drm_r128_indirect_t;

typedef struct drm_r128_fullscreen {
	enum {
		R128_INIT_FULLSCREEN = 0x01,
		R128_CLEANUP_FULLSCREEN = 0x02
	} func;
} drm_r128_fullscreen_t;

/* 2.3: An ioctl to get parameters that aren't available to the 3d
 * client any other way.
 */
#define R128_PARAM_IRQ_NR            1

typedef struct drm_r128_getparam {
	int param;
	void __user *value;
} drm_r128_getparam_t;

#if defined(__cplusplus)
}
#endif

#endif
