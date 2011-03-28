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
 * @SOC_MBUS_PACKING_NONE:	no packing, bit-for-bit transfer to RAM
 * @SOC_MBUS_PACKING_2X8_PADHI:	16 bits transferred in 2 8-bit samples, in the
 *				possibly incomplete byte high bits are padding
 * @SOC_MBUS_PACKING_2X8_PADLO:	as above, but low bits are padding
 * @SOC_MBUS_PACKING_EXTEND16:	sample width (e.g., 10 bits) has to be extended
 *				to 16 bits
 */
enum soc_mbus_packing {
	SOC_MBUS_PACKING_NONE,
	SOC_MBUS_PACKING_2X8_PADHI,
	SOC_MBUS_PACKING_2X8_PADLO,
	SOC_MBUS_PACKING_EXTEND16,
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
	u8			bits_per_sample;
};

const struct soc_mbus_pixelfmt *soc_mbus_get_fmtdesc(
	enum v4l2_mbus_pixelcode code);
s32 soc_mbus_bytes_per_line(u32 width, const struct soc_mbus_pixelfmt *mf);
int soc_mbus_samples_per_pixel(const struct soc_mbus_pixelfmt *mf);

#endif
