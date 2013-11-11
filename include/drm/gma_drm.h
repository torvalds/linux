/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 * Copyright (c) 2008, Tungsten Graphics Inc.  Cedar Park, TX., USA.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#ifndef _PSB_DRM_H_
#define _PSB_DRM_H_

/*
 *	Manage the LUT for an output
 */
struct drm_psb_dpst_lut_arg {
	uint8_t lut[256];
	int output_id;
};

/*
 *	Validate modes
 */
struct drm_psb_mode_operation_arg {
	u32 obj_id;
	u16 operation;
	struct drm_mode_modeinfo mode;
	u64 data;
};

/*
 *	Query the stolen memory for smarter management of
 *	memory by the server
 */
struct drm_psb_stolen_memory_arg {
	u32 base;
	u32 size;
};

struct drm_psb_get_pipe_from_crtc_id_arg {
	/** ID of CRTC being requested **/
	u32 crtc_id;
	/** pipe of requested CRTC **/
	u32 pipe;
};

struct drm_psb_gem_create {
	__u64 size;
	__u32 handle;
	__u32 flags;
#define GMA_GEM_CREATE_STOLEN		1	/* Stolen memory can be used */
};

struct drm_psb_gem_mmap {
	__u32 handle;
	__u32 pad;
	/**
	 * Fake offset to use for subsequent mmap call
	 *
	 * This is a fixed-size type for 32/64 compatibility.
	 */
	__u64 offset;
};

/* Controlling the kernel modesetting buffers */

#define DRM_GMA_GEM_CREATE	0x00		/* Create a GEM object */
#define DRM_GMA_GEM_MMAP	0x01		/* Map GEM memory */
#define DRM_GMA_STOLEN_MEMORY	0x02		/* Report stolen memory */
#define DRM_GMA_2D_OP		0x03		/* Will be merged later */
#define DRM_GMA_GAMMA		0x04		/* Set gamma table */
#define DRM_GMA_ADB		0x05		/* Get backlight */
#define DRM_GMA_DPST_BL		0x06		/* Set backlight */
#define DRM_GMA_MODE_OPERATION	0x07		/* Mode validation/DC set */
#define 	PSB_MODE_OPERATION_MODE_VALID	0x01
#define DRM_GMA_GET_PIPE_FROM_CRTC_ID	0x08	/* CRTC to physical pipe# */


#endif
