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

/*
 * These pixel codes uniquely identify data formats on the media bus. Mostly
 * they correspond to similarly named V4L2_PIX_FMT_* formats, format 0 is
 * reserved, V4L2_MBUS_FMT_FIXED shall be used by host-client pairs, where the
 * data format is fixed. Additionally, "2X8" means that one pixel is transferred
 * in two 8-bit samples, "BE" or "LE" specify in which order those samples are
 * transferred over the bus: "LE" means that the least significant bits are
 * transferred first, "BE" means that the most significant bits are transferred
 * first, and "PADHI" and "PADLO" define which bits - low or high, in the
 * incomplete high byte, are filled with padding bits.
 */
enum v4l2_mbus_pixelcode {
	V4L2_MBUS_FMT_FIXED = 1,
	V4L2_MBUS_FMT_YUYV8_2X8,
	V4L2_MBUS_FMT_YVYU8_2X8,
	V4L2_MBUS_FMT_UYVY8_2X8,
	V4L2_MBUS_FMT_VYUY8_2X8,
	V4L2_MBUS_FMT_YVYU10_2X10,
	V4L2_MBUS_FMT_YUYV10_2X10,
	V4L2_MBUS_FMT_YVYU10_1X20,
	V4L2_MBUS_FMT_YUYV10_1X20,
	V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE,
	V4L2_MBUS_FMT_RGB444_2X8_PADHI_BE,
	V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE,
	V4L2_MBUS_FMT_RGB555_2X8_PADHI_BE,
	V4L2_MBUS_FMT_RGB565_2X8_LE,
	V4L2_MBUS_FMT_RGB565_2X8_BE,
	V4L2_MBUS_FMT_BGR565_2X8_LE,
	V4L2_MBUS_FMT_BGR565_2X8_BE,
	V4L2_MBUS_FMT_SBGGR8_1X8,
	V4L2_MBUS_FMT_SBGGR10_1X10,
	V4L2_MBUS_FMT_GREY8_1X8,
	V4L2_MBUS_FMT_Y10_1X10,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_LE,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_BE,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_BE,
	V4L2_MBUS_FMT_SGRBG8_1X8,
	V4L2_MBUS_FMT_SBGGR12_1X12,
	V4L2_MBUS_FMT_YUYV8_1_5X8,
	V4L2_MBUS_FMT_YVYU8_1_5X8,
	V4L2_MBUS_FMT_UYVY8_1_5X8,
	V4L2_MBUS_FMT_VYUY8_1_5X8,
};

/**
 * struct v4l2_mbus_framefmt - frame format on the media bus
 * @width:	frame width
 * @height:	frame height
 * @code:	data format code
 * @field:	used interlacing type
 * @colorspace:	colorspace of the data
 */
struct v4l2_mbus_framefmt {
	__u32				width;
	__u32				height;
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_field			field;
	enum v4l2_colorspace		colorspace;
};

static inline void v4l2_fill_pix_format(struct v4l2_pix_format *pix_fmt,
				const struct v4l2_mbus_framefmt *mbus_fmt)
{
	pix_fmt->width = mbus_fmt->width;
	pix_fmt->height = mbus_fmt->height;
	pix_fmt->field = mbus_fmt->field;
	pix_fmt->colorspace = mbus_fmt->colorspace;
}

static inline void v4l2_fill_mbus_format(struct v4l2_mbus_framefmt *mbus_fmt,
			   const struct v4l2_pix_format *pix_fmt,
			   enum v4l2_mbus_pixelcode code)
{
	mbus_fmt->width = pix_fmt->width;
	mbus_fmt->height = pix_fmt->height;
	mbus_fmt->field = pix_fmt->field;
	mbus_fmt->colorspace = pix_fmt->colorspace;
	mbus_fmt->code = code;
}

#endif
