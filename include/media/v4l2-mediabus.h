/*
 * Media Bus API header
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef V4L2_MEDIABUS_H
#define V4L2_MEDIABUS_H

#include <linux/v4l2-mediabus.h>

/* Parallel flags */
/*
 * Can the client run in master or in slave mode. By "Master mode" an operation
 * mode is meant, when the client (e.g., a camera sensor) is producing
 * horizontal and vertical synchronisation. In "Slave mode" the host is
 * providing these signals to the slave.
 */
#define V4L2_MBUS_MASTER			(1 << 0)
#define V4L2_MBUS_SLAVE				(1 << 1)
/*
 * Signal polarity flags
 * Note: in BT.656 mode HSYNC, FIELD, and VSYNC are unused
 * V4L2_MBUS_[HV]SYNC* flags should be also used for specifying
 * configuration of hardware that uses [HV]REF signals
 */
#define V4L2_MBUS_HSYNC_ACTIVE_HIGH		(1 << 2)
#define V4L2_MBUS_HSYNC_ACTIVE_LOW		(1 << 3)
#define V4L2_MBUS_VSYNC_ACTIVE_HIGH		(1 << 4)
#define V4L2_MBUS_VSYNC_ACTIVE_LOW		(1 << 5)
#define V4L2_MBUS_PCLK_SAMPLE_RISING		(1 << 6)
#define V4L2_MBUS_PCLK_SAMPLE_FALLING		(1 << 7)
#define V4L2_MBUS_DATA_ACTIVE_HIGH		(1 << 8)
#define V4L2_MBUS_DATA_ACTIVE_LOW		(1 << 9)
/* FIELD = 0/1 - Field1 (odd)/Field2 (even) */
#define V4L2_MBUS_FIELD_EVEN_HIGH		(1 << 10)
/* FIELD = 1/0 - Field1 (odd)/Field2 (even) */
#define V4L2_MBUS_FIELD_EVEN_LOW		(1 << 11)
/* Active state of Sync-on-green (SoG) signal, 0/1 for LOW/HIGH respectively. */
#define V4L2_MBUS_VIDEO_SOG_ACTIVE_HIGH	(1 << 12)
#define V4L2_MBUS_VIDEO_SOG_ACTIVE_LOW		(1 << 13)

/* Serial flags */
/* How many lanes the client can use */
#define V4L2_MBUS_CSI2_1_LANE			(1 << 0)
#define V4L2_MBUS_CSI2_2_LANE			(1 << 1)
#define V4L2_MBUS_CSI2_3_LANE			(1 << 2)
#define V4L2_MBUS_CSI2_4_LANE			(1 << 3)
/* On which channels it can send video data */
#define V4L2_MBUS_CSI2_CHANNEL_0		(1 << 4)
#define V4L2_MBUS_CSI2_CHANNEL_1		(1 << 5)
#define V4L2_MBUS_CSI2_CHANNEL_2		(1 << 6)
#define V4L2_MBUS_CSI2_CHANNEL_3		(1 << 7)
/* Does it support only continuous or also non-continuous clock mode */
#define V4L2_MBUS_CSI2_CONTINUOUS_CLOCK		(1 << 8)
#define V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK	(1 << 9)

#define V4L2_MBUS_CSI2_LANES		(V4L2_MBUS_CSI2_1_LANE | V4L2_MBUS_CSI2_2_LANE | \
					 V4L2_MBUS_CSI2_3_LANE | V4L2_MBUS_CSI2_4_LANE)
#define V4L2_MBUS_CSI2_CHANNELS		(V4L2_MBUS_CSI2_CHANNEL_0 | V4L2_MBUS_CSI2_CHANNEL_1 | \
					 V4L2_MBUS_CSI2_CHANNEL_2 | V4L2_MBUS_CSI2_CHANNEL_3)

/**
 * v4l2_mbus_type - media bus type
 * @V4L2_MBUS_PARALLEL:	parallel interface with hsync and vsync
 * @V4L2_MBUS_BT656:	parallel interface with embedded synchronisation, can
 *			also be used for BT.1120
 * @V4L2_MBUS_CSI2:	MIPI CSI-2 serial interface
 */
enum v4l2_mbus_type {
	V4L2_MBUS_PARALLEL,
	V4L2_MBUS_BT656,
	V4L2_MBUS_CSI2,
};

/**
 * v4l2_mbus_config - media bus configuration
 * @type:	in: interface type
 * @flags:	in / out: configuration flags, depending on @type
 */
struct v4l2_mbus_config {
	enum v4l2_mbus_type type;
	unsigned int flags;
};

static inline void v4l2_fill_pix_format(struct v4l2_pix_format *pix_fmt,
				const struct v4l2_mbus_framefmt *mbus_fmt)
{
	pix_fmt->width = mbus_fmt->width;
	pix_fmt->height = mbus_fmt->height;
	pix_fmt->field = mbus_fmt->field;
	pix_fmt->colorspace = mbus_fmt->colorspace;
	pix_fmt->ycbcr_enc = mbus_fmt->ycbcr_enc;
	pix_fmt->quantization = mbus_fmt->quantization;
	pix_fmt->xfer_func = mbus_fmt->xfer_func;
}

static inline void v4l2_fill_mbus_format(struct v4l2_mbus_framefmt *mbus_fmt,
			   const struct v4l2_pix_format *pix_fmt,
			   u32 code)
{
	mbus_fmt->width = pix_fmt->width;
	mbus_fmt->height = pix_fmt->height;
	mbus_fmt->field = pix_fmt->field;
	mbus_fmt->colorspace = pix_fmt->colorspace;
	mbus_fmt->ycbcr_enc = pix_fmt->ycbcr_enc;
	mbus_fmt->quantization = pix_fmt->quantization;
	mbus_fmt->xfer_func = pix_fmt->xfer_func;
	mbus_fmt->code = code;
}

#endif
