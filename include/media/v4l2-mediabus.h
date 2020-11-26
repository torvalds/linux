/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Media Bus API header
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#ifndef V4L2_MEDIABUS_H
#define V4L2_MEDIABUS_H

#include <linux/v4l2-mediabus.h>
#include <linux/bitops.h>

/*
 * How to use the V4L2_MBUS_* flags:
 * Flags are defined for each of the possible states and values of a media
 * bus configuration parameter. One and only one bit of each group of flags
 * shall be set by the users of the v4l2_subdev_pad_ops.get_mbus_config and
 * v4l2_subdev_pad_ops.set_mbus_config operations to ensure that no
 * conflicting settings are specified when reporting and setting the media bus
 * configuration with the two operations respectively. For example, it is
 * invalid to set or clear both the V4L2_MBUS_HSYNC_ACTIVE_HIGH and the
 * V4L2_MBUS_HSYNC_ACTIVE_LOW flag at the same time. Instead either flag
 * V4L2_MBUS_HSYNC_ACTIVE_HIGH or flag V4L2_MBUS_HSYNC_ACTIVE_LOW shall be
 * set. The same is true for the V4L2_MBUS_CSI2_1/2/3/4_LANE flags group: only
 * one of these four bits shall be set.
 *
 * TODO: replace the existing V4L2_MBUS_* flags with structures of fields
 * to avoid conflicting settings.
 *
 * In example:
 *     #define V4L2_MBUS_HSYNC_ACTIVE_HIGH             BIT(2)
 *     #define V4L2_MBUS_HSYNC_ACTIVE_LOW              BIT(3)
 * will be replaced by a field whose value reports the intended active state of
 * the signal:
 *     unsigned int v4l2_mbus_hsync_active : 1;
 */

/* Parallel flags */
/*
 * The client runs in master or in slave mode. By "Master mode" an operation
 * mode is meant, when the client (e.g., a camera sensor) is producing
 * horizontal and vertical synchronisation. In "Slave mode" the host is
 * providing these signals to the slave.
 */
#define V4L2_MBUS_MASTER			BIT(0)
#define V4L2_MBUS_SLAVE				BIT(1)
/*
 * Signal polarity flags
 * Note: in BT.656 mode HSYNC, FIELD, and VSYNC are unused
 * V4L2_MBUS_[HV]SYNC* flags should be also used for specifying
 * configuration of hardware that uses [HV]REF signals
 */
#define V4L2_MBUS_HSYNC_ACTIVE_HIGH		BIT(2)
#define V4L2_MBUS_HSYNC_ACTIVE_LOW		BIT(3)
#define V4L2_MBUS_VSYNC_ACTIVE_HIGH		BIT(4)
#define V4L2_MBUS_VSYNC_ACTIVE_LOW		BIT(5)
#define V4L2_MBUS_PCLK_SAMPLE_RISING		BIT(6)
#define V4L2_MBUS_PCLK_SAMPLE_FALLING		BIT(7)
#define V4L2_MBUS_DATA_ACTIVE_HIGH		BIT(8)
#define V4L2_MBUS_DATA_ACTIVE_LOW		BIT(9)
/* FIELD = 0/1 - Field1 (odd)/Field2 (even) */
#define V4L2_MBUS_FIELD_EVEN_HIGH		BIT(10)
/* FIELD = 1/0 - Field1 (odd)/Field2 (even) */
#define V4L2_MBUS_FIELD_EVEN_LOW		BIT(11)
/* Active state of Sync-on-green (SoG) signal, 0/1 for LOW/HIGH respectively. */
#define V4L2_MBUS_VIDEO_SOG_ACTIVE_HIGH		BIT(12)
#define V4L2_MBUS_VIDEO_SOG_ACTIVE_LOW		BIT(13)
#define V4L2_MBUS_DATA_ENABLE_HIGH		BIT(14)
#define V4L2_MBUS_DATA_ENABLE_LOW		BIT(15)

/* Serial flags */
/* CSI-2 D-PHY number of data lanes. */
#define V4L2_MBUS_CSI2_1_LANE			BIT(0)
#define V4L2_MBUS_CSI2_2_LANE			BIT(1)
#define V4L2_MBUS_CSI2_3_LANE			BIT(2)
#define V4L2_MBUS_CSI2_4_LANE			BIT(3)
/* CSI-2 Virtual Channel identifiers. */
#define V4L2_MBUS_CSI2_CHANNEL_0		BIT(4)
#define V4L2_MBUS_CSI2_CHANNEL_1		BIT(5)
#define V4L2_MBUS_CSI2_CHANNEL_2		BIT(6)
#define V4L2_MBUS_CSI2_CHANNEL_3		BIT(7)
/* Clock non-continuous mode support. */
#define V4L2_MBUS_CSI2_CONTINUOUS_CLOCK		BIT(8)
#define V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK	BIT(9)

#define V4L2_MBUS_CSI2_LANES		(V4L2_MBUS_CSI2_1_LANE | \
					 V4L2_MBUS_CSI2_2_LANE | \
					 V4L2_MBUS_CSI2_3_LANE | \
					 V4L2_MBUS_CSI2_4_LANE)
#define V4L2_MBUS_CSI2_CHANNELS		(V4L2_MBUS_CSI2_CHANNEL_0 | \
					 V4L2_MBUS_CSI2_CHANNEL_1 | \
					 V4L2_MBUS_CSI2_CHANNEL_2 | \
					 V4L2_MBUS_CSI2_CHANNEL_3)

/**
 * enum v4l2_mbus_type - media bus type
 * @V4L2_MBUS_UNKNOWN:	unknown bus type, no V4L2 mediabus configuration
 * @V4L2_MBUS_PARALLEL:	parallel interface with hsync and vsync
 * @V4L2_MBUS_BT656:	parallel interface with embedded synchronisation, can
 *			also be used for BT.1120
 * @V4L2_MBUS_CSI1:	MIPI CSI-1 serial interface
 * @V4L2_MBUS_CCP2:	CCP2 (Compact Camera Port 2)
 * @V4L2_MBUS_CSI2_DPHY: MIPI CSI-2 serial interface, with D-PHY
 * @V4L2_MBUS_CSI2_CPHY: MIPI CSI-2 serial interface, with C-PHY
 */
enum v4l2_mbus_type {
	V4L2_MBUS_UNKNOWN,
	V4L2_MBUS_PARALLEL,
	V4L2_MBUS_BT656,
	V4L2_MBUS_CSI1,
	V4L2_MBUS_CCP2,
	V4L2_MBUS_CSI2_DPHY,
	V4L2_MBUS_CSI2_CPHY,
};

/**
 * struct v4l2_mbus_config - media bus configuration
 * @type:	in: interface type
 * @flags:	in / out: configuration flags, depending on @type
 */
struct v4l2_mbus_config {
	enum v4l2_mbus_type type;
	unsigned int flags;
};

/**
 * v4l2_fill_pix_format - Ancillary routine that fills a &struct
 *	v4l2_pix_format fields from a &struct v4l2_mbus_framefmt.
 *
 * @pix_fmt:	pointer to &struct v4l2_pix_format to be filled
 * @mbus_fmt:	pointer to &struct v4l2_mbus_framefmt to be used as model
 */
static inline void
v4l2_fill_pix_format(struct v4l2_pix_format *pix_fmt,
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

/**
 * v4l2_fill_pix_format - Ancillary routine that fills a &struct
 *	v4l2_mbus_framefmt from a &struct v4l2_pix_format and a
 *	data format code.
 *
 * @mbus_fmt:	pointer to &struct v4l2_mbus_framefmt to be filled
 * @pix_fmt:	pointer to &struct v4l2_pix_format to be used as model
 * @code:	data format code (from &enum v4l2_mbus_pixelcode)
 */
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

/**
 * v4l2_fill_pix_format - Ancillary routine that fills a &struct
 *	v4l2_pix_format_mplane fields from a media bus structure.
 *
 * @pix_mp_fmt:	pointer to &struct v4l2_pix_format_mplane to be filled
 * @mbus_fmt:	pointer to &struct v4l2_mbus_framefmt to be used as model
 */
static inline void
v4l2_fill_pix_format_mplane(struct v4l2_pix_format_mplane *pix_mp_fmt,
			    const struct v4l2_mbus_framefmt *mbus_fmt)
{
	pix_mp_fmt->width = mbus_fmt->width;
	pix_mp_fmt->height = mbus_fmt->height;
	pix_mp_fmt->field = mbus_fmt->field;
	pix_mp_fmt->colorspace = mbus_fmt->colorspace;
	pix_mp_fmt->ycbcr_enc = mbus_fmt->ycbcr_enc;
	pix_mp_fmt->quantization = mbus_fmt->quantization;
	pix_mp_fmt->xfer_func = mbus_fmt->xfer_func;
}

/**
 * v4l2_fill_pix_format - Ancillary routine that fills a &struct
 *	v4l2_mbus_framefmt from a &struct v4l2_pix_format_mplane.
 *
 * @mbus_fmt:	pointer to &struct v4l2_mbus_framefmt to be filled
 * @pix_mp_fmt:	pointer to &struct v4l2_pix_format_mplane to be used as model
 */
static inline void
v4l2_fill_mbus_format_mplane(struct v4l2_mbus_framefmt *mbus_fmt,
			     const struct v4l2_pix_format_mplane *pix_mp_fmt)
{
	mbus_fmt->width = pix_mp_fmt->width;
	mbus_fmt->height = pix_mp_fmt->height;
	mbus_fmt->field = pix_mp_fmt->field;
	mbus_fmt->colorspace = pix_mp_fmt->colorspace;
	mbus_fmt->ycbcr_enc = pix_mp_fmt->ycbcr_enc;
	mbus_fmt->quantization = pix_mp_fmt->quantization;
	mbus_fmt->xfer_func = pix_mp_fmt->xfer_func;
}

#endif
