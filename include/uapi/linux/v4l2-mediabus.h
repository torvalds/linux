/*
 * Media Bus API header
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_V4L2_MEDIABUS_H
#define __LINUX_V4L2_MEDIABUS_H

#include <linux/types.h>
#include <linux/videodev2.h>

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
 *
 * The pixel codes are grouped by type, bus_width, bits per component, samples
 * per pixel and order of subsamples. Numerical values are sorted using generic
 * numerical sort order (8 thus comes before 10).
 *
 * As their value can't change when a new pixel code is inserted in the
 * enumeration, the pixel codes are explicitly given a numerical value. The next
 * free values for each category are listed below, update them when inserting
 * new pixel codes.
 */
enum v4l2_mbus_pixelcode {
	V4L2_MBUS_FMT_FIXED = 0x0001,

	/* RGB - next is 0x100e */
	V4L2_MBUS_FMT_RGB444_2X8_PADHI_BE = 0x1001,
	V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE = 0x1002,
	V4L2_MBUS_FMT_RGB555_2X8_PADHI_BE = 0x1003,
	V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE = 0x1004,
	V4L2_MBUS_FMT_BGR565_2X8_BE = 0x1005,
	V4L2_MBUS_FMT_BGR565_2X8_LE = 0x1006,
	V4L2_MBUS_FMT_RGB565_2X8_BE = 0x1007,
	V4L2_MBUS_FMT_RGB565_2X8_LE = 0x1008,
	V4L2_MBUS_FMT_RGB666_1X18 = 0x1009,
	V4L2_MBUS_FMT_RGB888_1X24 = 0x100a,
	V4L2_MBUS_FMT_RGB888_2X12_BE = 0x100b,
	V4L2_MBUS_FMT_RGB888_2X12_LE = 0x100c,
	V4L2_MBUS_FMT_ARGB8888_1X32 = 0x100d,

	/* YUV (including grey) - next is 0x2018 */
	V4L2_MBUS_FMT_Y8_1X8 = 0x2001,
	V4L2_MBUS_FMT_UV8_1X8 = 0x2015,
	V4L2_MBUS_FMT_UYVY8_1_5X8 = 0x2002,
	V4L2_MBUS_FMT_VYUY8_1_5X8 = 0x2003,
	V4L2_MBUS_FMT_YUYV8_1_5X8 = 0x2004,
	V4L2_MBUS_FMT_YVYU8_1_5X8 = 0x2005,
	V4L2_MBUS_FMT_UYVY8_2X8 = 0x2006,
	V4L2_MBUS_FMT_VYUY8_2X8 = 0x2007,
	V4L2_MBUS_FMT_YUYV8_2X8 = 0x2008,
	V4L2_MBUS_FMT_YVYU8_2X8 = 0x2009,
	V4L2_MBUS_FMT_Y10_1X10 = 0x200a,
	V4L2_MBUS_FMT_YUYV10_2X10 = 0x200b,
	V4L2_MBUS_FMT_YVYU10_2X10 = 0x200c,
	V4L2_MBUS_FMT_Y12_1X12 = 0x2013,
	V4L2_MBUS_FMT_UYVY8_1X16 = 0x200f,
	V4L2_MBUS_FMT_VYUY8_1X16 = 0x2010,
	V4L2_MBUS_FMT_YUYV8_1X16 = 0x2011,
	V4L2_MBUS_FMT_YVYU8_1X16 = 0x2012,
	V4L2_MBUS_FMT_YDYUYDYV8_1X16 = 0x2014,
	V4L2_MBUS_FMT_YUYV10_1X20 = 0x200d,
	V4L2_MBUS_FMT_YVYU10_1X20 = 0x200e,
	V4L2_MBUS_FMT_YUV10_1X30 = 0x2016,
	V4L2_MBUS_FMT_AYUV8_1X32 = 0x2017,

	/* Bayer - next is 0x3019 */
	V4L2_MBUS_FMT_SBGGR8_1X8 = 0x3001,
	V4L2_MBUS_FMT_SGBRG8_1X8 = 0x3013,
	V4L2_MBUS_FMT_SGRBG8_1X8 = 0x3002,
	V4L2_MBUS_FMT_SRGGB8_1X8 = 0x3014,
	V4L2_MBUS_FMT_SBGGR10_ALAW8_1X8 = 0x3015,
	V4L2_MBUS_FMT_SGBRG10_ALAW8_1X8 = 0x3016,
	V4L2_MBUS_FMT_SGRBG10_ALAW8_1X8 = 0x3017,
	V4L2_MBUS_FMT_SRGGB10_ALAW8_1X8 = 0x3018,
	V4L2_MBUS_FMT_SBGGR10_DPCM8_1X8 = 0x300b,
	V4L2_MBUS_FMT_SGBRG10_DPCM8_1X8 = 0x300c,
	V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8 = 0x3009,
	V4L2_MBUS_FMT_SRGGB10_DPCM8_1X8 = 0x300d,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_BE = 0x3003,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE = 0x3004,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_BE = 0x3005,
	V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_LE = 0x3006,
	V4L2_MBUS_FMT_SBGGR10_1X10 = 0x3007,
	V4L2_MBUS_FMT_SGBRG10_1X10 = 0x300e,
	V4L2_MBUS_FMT_SGRBG10_1X10 = 0x300a,
	V4L2_MBUS_FMT_SRGGB10_1X10 = 0x300f,
	V4L2_MBUS_FMT_SBGGR12_1X12 = 0x3008,
	V4L2_MBUS_FMT_SGBRG12_1X12 = 0x3010,
	V4L2_MBUS_FMT_SGRBG12_1X12 = 0x3011,
	V4L2_MBUS_FMT_SRGGB12_1X12 = 0x3012,

	/* JPEG compressed formats - next is 0x4002 */
	V4L2_MBUS_FMT_JPEG_1X8 = 0x4001,

	/* Vendor specific formats - next is 0x5002 */

	/* S5C73M3 sensor specific interleaved UYVY and JPEG */
	V4L2_MBUS_FMT_S5C_UYVY_JPEG_1X8 = 0x5001,
};

/**
 * struct v4l2_mbus_framefmt - frame format on the media bus
 * @width:	frame width
 * @height:	frame height
 * @code:	data format code (from enum v4l2_mbus_pixelcode)
 * @field:	used interlacing type (from enum v4l2_field)
 * @colorspace:	colorspace of the data (from enum v4l2_colorspace)
 */
struct v4l2_mbus_framefmt {
	__u32			width;
	__u32			height;
	__u32			code;
	__u32			field;
	__u32			colorspace;
	__u32			reserved[7];
};

#endif
