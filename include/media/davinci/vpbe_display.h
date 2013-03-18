/*
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef VPBE_DISPLAY_H
#define VPBE_DISPLAY_H

/* Header files */
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-dma-contig.h>
#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe_osd.h>
#include <media/davinci/vpbe.h>

#define VPBE_DISPLAY_MAX_DEVICES 2

enum vpbe_display_device_id {
	VPBE_DISPLAY_DEVICE_0,
	VPBE_DISPLAY_DEVICE_1
};

#define VPBE_DISPLAY_DRV_NAME	"vpbe-display"

#define VPBE_DISPLAY_MAJOR_RELEASE              1
#define VPBE_DISPLAY_MINOR_RELEASE              0
#define VPBE_DISPLAY_BUILD                      1
#define VPBE_DISPLAY_VERSION_CODE ((VPBE_DISPLAY_MAJOR_RELEASE << 16) | \
	(VPBE_DISPLAY_MINOR_RELEASE << 8)  | \
	VPBE_DISPLAY_BUILD)

#define VPBE_DISPLAY_VALID_FIELD(field)   ((V4L2_FIELD_NONE == field) || \
	 (V4L2_FIELD_ANY == field) || (V4L2_FIELD_INTERLACED == field))

/* Exp ratio numerator and denominator constants */
#define VPBE_DISPLAY_H_EXP_RATIO_N	9
#define VPBE_DISPLAY_H_EXP_RATIO_D	8
#define VPBE_DISPLAY_V_EXP_RATIO_N	6
#define VPBE_DISPLAY_V_EXP_RATIO_D	5

/* Zoom multiplication factor */
#define VPBE_DISPLAY_ZOOM_4X	4
#define VPBE_DISPLAY_ZOOM_2X	2

/* Structures */
struct display_layer_info {
	int enable;
	/* Layer ID used by Display Manager */
	enum osd_layer id;
	struct osd_layer_config config;
	enum osd_zoom_factor h_zoom;
	enum osd_zoom_factor v_zoom;
	enum osd_h_exp_ratio h_exp;
	enum osd_v_exp_ratio v_exp;
};

struct vpbe_disp_buffer {
	struct vb2_buffer vb;
	struct list_head list;
};

/* vpbe display object structure */
struct vpbe_layer {
	/* number of buffers in fbuffers */
	unsigned int numbuffers;
	/* Pointer to the vpbe_display */
	struct vpbe_display *disp_dev;
	/* Pointer pointing to current v4l2_buffer */
	struct vpbe_disp_buffer *cur_frm;
	/* Pointer pointing to next v4l2_buffer */
	struct vpbe_disp_buffer *next_frm;
	/* videobuf specific parameters
	 * Buffer queue used in video-buf
	 */
	struct vb2_queue buffer_queue;
	/* allocator-specific contexts for each plane */
	struct vb2_alloc_ctx *alloc_ctx;
	/* Queue of filled frames */
	struct list_head dma_queue;
	/* Used in video-buf */
	spinlock_t irqlock;
	/* V4l2 specific parameters */
	/* Identifies video device for this layer */
	struct video_device video_dev;
	/* This field keeps track of type of buffer exchange mechanism user
	 * has selected
	 */
	enum v4l2_memory memory;
	/* Used to keep track of state of the priority */
	struct v4l2_prio_state prio;
	/* Used to store pixel format */
	struct v4l2_pix_format pix_fmt;
	enum v4l2_field buf_field;
	/* Video layer configuration params */
	struct display_layer_info layer_info;
	/* vpbe specific parameters
	 * enable window for display
	 */
	unsigned char window_enable;
	/* number of open instances of the layer */
	unsigned int usrs;
	/* number of users performing IO */
	unsigned int io_usrs;
	/* Indicates id of the field which is being displayed */
	unsigned int field_id;
	/* Indicates whether streaming started */
	unsigned char started;
	/* Identifies device object */
	enum vpbe_display_device_id device_id;
	/* facilitation of ioctl ops lock by v4l2*/
	struct mutex opslock;
	u8 layer_first_int;
};

/* vpbe device structure */
struct vpbe_display {
	/* layer specific parameters */
	/* lock for isr updates to buf layers*/
	spinlock_t dma_queue_lock;
	/* C-Plane offset from start of y-plane */
	unsigned int cbcr_ofst;
	struct vpbe_layer *dev[VPBE_DISPLAY_MAX_DEVICES];
	struct vpbe_device *vpbe_dev;
	struct osd_state *osd_device;
};

/* File handle structure */
struct vpbe_fh {
	/* vpbe device structure */
	struct vpbe_display *disp_dev;
	/* pointer to layer object for opened device */
	struct vpbe_layer *layer;
	/* Indicates whether this file handle is doing IO */
	unsigned char io_allowed;
	/* Used to keep track priority of this instance */
	enum v4l2_priority prio;
};

struct buf_config_params {
	unsigned char min_numbuffers;
	unsigned char numbuffers[VPBE_DISPLAY_MAX_DEVICES];
	unsigned int min_bufsize[VPBE_DISPLAY_MAX_DEVICES];
	unsigned int layer_bufsize[VPBE_DISPLAY_MAX_DEVICES];
};

#endif	/* VPBE_DISPLAY_H */
