/*
 * SoC-camera Media Bus API extensions
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SOC_MEDIABUS_H
#define SOC_MEDIABUS_H

#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>

/**
 * enum soc_mbus_packing - data packing types on the media-bus
 * @SOC_MBUS_PACKING_NONE:	no packing, bit-for-bit transfer to RAM, one
 *				sample represents one pixel
 * @SOC_MBUS_PACKING_2X8_PADHI:	16 bits transferred in 2 8-bit samples, in the
 *				possibly incomplete byte high bits are padding
 * @SOC_MBUS_PACKING_2X8_PADLO:	as above, but low bits are padding
 * @SOC_MBUS_PACKING_EXTEND16:	sample width (e.g., 10 bits) has to be extended
 *				to 16 bits
 * @SOC_MBUS_PACKING_VARIABLE:	compressed formats with variable packing
 * @SOC_MBUS_PACKING_1_5X8:	used for packed YUV 4:2:0 formats, where 4
 *				pixels occupy 6 bytes in RAM
 */
enum soc_mbus_packing {
	SOC_MBUS_PACKING_NONE,
	SOC_MBUS_PACKING_2X8_PADHI,
	SOC_MBUS_PACKING_2X8_PADLO,
	SOC_MBUS_PACKING_EXTEND16,
	SOC_MBUS_PACKING_VARIABLE,
	SOC_MBUS_PACKING_1_5X8,
};

/**
 * enum soc_mbus_order - sample order on the media bus
 * @SOC_MBUS_ORDER_LE:		least significant sample first
 * @SOC_MBUS_ORDER_BE:		most significant sample first
 */
enum soc_mbus_order {
	SOC_MBUS_ORDER_LE,
	SOC_MBUS_ORDER_BE,
};

/**
 * enum soc_mbus_layout - planes layout in memory
 * @SOC_MBUS_LAYOUT_PACKED:		color components packed
 * @SOC_MBUS_LAYOUT_PLANAR_2Y_U_V:	YUV components stored in 3 planes (4:2:2)
 * @SOC_MBUS_LAYOUT_PLANAR_2Y_C:	YUV components stored in a luma and a
 *					chroma plane (C plane is half the size
 *					of Y plane)
 * @SOC_MBUS_LAYOUT_PLANAR_Y_C:		YUV components stored in a luma and a
 *					chroma plane (C plane is the same size
 *					as Y plane)
 */
enum soc_mbus_layout {
	SOC_MBUS_LAYOUT_PACKED = 0,
	SOC_MBUS_LAYOUT_PLANAR_2Y_U_V,
	SOC_MBUS_LAYOUT_PLANAR_2Y_C,
	SOC_MBUS_LAYOUT_PLANAR_Y_C,
};

/**
 * struct soc_mbus_pixelfmt - Data format on the media bus
 * @name:		Name of the format
 * @fourcc:		Fourcc code, that will be obtained if the data is
 *			stored in memory in the following way:
 * @packing:		Type of sample-packing, that has to be used
 * @order:		Sample order when storing in memory
 * @bits_per_sample:	How many bits the bridge has to sample
 */
struct soc_mbus_pixelfmt {
	const char		*name;
	u32			fourcc;
	enum soc_mbus_packing	packing;
	enum soc_mbus_order	order;
	enum soc_mbus_layout	layout;
	u8			bits_per_sample;
};

/**
 * struct soc_mbus_lookup - Lookup FOURCC IDs by mediabus codes for pass-through
 * @code:	mediabus pixel-code
 * @fmt:	pixel format description
 */
struct soc_mbus_lookup {
	enum v4l2_mbus_pixelcode	code;
	struct soc_mbus_pixelfmt	fmt;
};

const struct soc_mbus_pixelfmt *soc_mbus_find_fmtdesc(
	enum v4l2_mbus_pixelcode code,
	const struct soc_mbus_lookup *lookup,
	int n);
const struct soc_mbus_pixelfmt *soc_mbus_get_fmtdesc(
	enum v4l2_mbus_pixelcode code);
s32 soc_mbus_bytes_per_line(u32 width, const struct soc_mbus_pixelfmt *mf);
s32 soc_mbus_image_size(const struct soc_mbus_pixelfmt *mf,
			u32 bytes_per_line, u32 height);
int soc_mbus_samples_per_pixel(const struct soc_mbus_pixelfmt *mf,
			unsigned int *numerator, unsigned int *denominator);
unsigned int soc_mbus_config_compatible(const struct v4l2_mbus_config *cfg,
					unsigned int flags);

#endif
