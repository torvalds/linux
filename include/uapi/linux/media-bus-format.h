/*
 * Media Bus API header
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MEDIA_BUS_FORMAT_H
#define __LINUX_MEDIA_BUS_FORMAT_H

/*
 * These bus formats uniquely identify data formats on the data bus. Format 0
 * is reserved, MEDIA_BUS_FMT_FIXED shall be used by host-client pairs, where
 * the data format is fixed. Additionally, "2X8" means that one pixel is
 * transferred in two 8-bit samples, "BE" or "LE" specify in which order those
 * samples are transferred over the bus: "LE" means that the least significant
 * bits are transferred first, "BE" means that the most significant bits are
 * transferred first, and "PADHI" and "PADLO" define which bits - low or high,
 * in the incomplete high byte, are filled with padding bits.
 *
 * The bus formats are grouped by type, bus_width, bits per component, samples
 * per pixel and order of subsamples. Numerical values are sorted using generic
 * numerical sort order (8 thus comes before 10).
 *
 * As their value can't change when a new bus format is inserted in the
 * enumeration, the bus formats are explicitly given a numerical value. The next
 * free values for each category are listed below, update them when inserting
 * new pixel codes.
 */

#define MEDIA_BUS_FMT_FIXED			0x0001

/* RGB - next is	0x1018 */
#define MEDIA_BUS_FMT_RGB444_1X12		0x1016
#define MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE	0x1001
#define MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE	0x1002
#define MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE	0x1003
#define MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE	0x1004
#define MEDIA_BUS_FMT_RGB565_1X16		0x1017
#define MEDIA_BUS_FMT_BGR565_2X8_BE		0x1005
#define MEDIA_BUS_FMT_BGR565_2X8_LE		0x1006
#define MEDIA_BUS_FMT_RGB565_2X8_BE		0x1007
#define MEDIA_BUS_FMT_RGB565_2X8_LE		0x1008
#define MEDIA_BUS_FMT_RGB666_1X18		0x1009
#define MEDIA_BUS_FMT_RBG888_1X24		0x100e
#define MEDIA_BUS_FMT_RGB666_1X24_CPADHI	0x1015
#define MEDIA_BUS_FMT_RGB666_1X7X3_SPWG		0x1010
#define MEDIA_BUS_FMT_BGR888_1X24		0x1013
#define MEDIA_BUS_FMT_GBR888_1X24		0x1014
#define MEDIA_BUS_FMT_RGB888_1X24		0x100a
#define MEDIA_BUS_FMT_RGB888_2X12_BE		0x100b
#define MEDIA_BUS_FMT_RGB888_2X12_LE		0x100c
#define MEDIA_BUS_FMT_RGB888_1X7X4_SPWG		0x1011
#define MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA	0x1012
#define MEDIA_BUS_FMT_ARGB8888_1X32		0x100d
#define MEDIA_BUS_FMT_RGB888_1X32_PADHI		0x100f

/* YUV (including grey) - next is	0x2026 */
#define MEDIA_BUS_FMT_Y8_1X8			0x2001
#define MEDIA_BUS_FMT_UV8_1X8			0x2015
#define MEDIA_BUS_FMT_UYVY8_1_5X8		0x2002
#define MEDIA_BUS_FMT_VYUY8_1_5X8		0x2003
#define MEDIA_BUS_FMT_YUYV8_1_5X8		0x2004
#define MEDIA_BUS_FMT_YVYU8_1_5X8		0x2005
#define MEDIA_BUS_FMT_UYVY8_2X8			0x2006
#define MEDIA_BUS_FMT_VYUY8_2X8			0x2007
#define MEDIA_BUS_FMT_YUYV8_2X8			0x2008
#define MEDIA_BUS_FMT_YVYU8_2X8			0x2009
#define MEDIA_BUS_FMT_Y10_1X10			0x200a
#define MEDIA_BUS_FMT_UYVY10_2X10		0x2018
#define MEDIA_BUS_FMT_VYUY10_2X10		0x2019
#define MEDIA_BUS_FMT_YUYV10_2X10		0x200b
#define MEDIA_BUS_FMT_YVYU10_2X10		0x200c
#define MEDIA_BUS_FMT_Y12_1X12			0x2013
#define MEDIA_BUS_FMT_UYVY12_2X12		0x201c
#define MEDIA_BUS_FMT_VYUY12_2X12		0x201d
#define MEDIA_BUS_FMT_YUYV12_2X12		0x201e
#define MEDIA_BUS_FMT_YVYU12_2X12		0x201f
#define MEDIA_BUS_FMT_UYVY8_1X16		0x200f
#define MEDIA_BUS_FMT_VYUY8_1X16		0x2010
#define MEDIA_BUS_FMT_YUYV8_1X16		0x2011
#define MEDIA_BUS_FMT_YVYU8_1X16		0x2012
#define MEDIA_BUS_FMT_YDYUYDYV8_1X16		0x2014
#define MEDIA_BUS_FMT_UYVY10_1X20		0x201a
#define MEDIA_BUS_FMT_VYUY10_1X20		0x201b
#define MEDIA_BUS_FMT_YUYV10_1X20		0x200d
#define MEDIA_BUS_FMT_YVYU10_1X20		0x200e
#define MEDIA_BUS_FMT_VUY8_1X24			0x2024
#define MEDIA_BUS_FMT_YUV8_1X24			0x2025
#define MEDIA_BUS_FMT_UYVY12_1X24		0x2020
#define MEDIA_BUS_FMT_VYUY12_1X24		0x2021
#define MEDIA_BUS_FMT_YUYV12_1X24		0x2022
#define MEDIA_BUS_FMT_YVYU12_1X24		0x2023
#define MEDIA_BUS_FMT_YUV10_1X30		0x2016
#define MEDIA_BUS_FMT_AYUV8_1X32		0x2017

/* Bayer - next is	0x3019 */
#define MEDIA_BUS_FMT_SBGGR8_1X8		0x3001
#define MEDIA_BUS_FMT_SGBRG8_1X8		0x3013
#define MEDIA_BUS_FMT_SGRBG8_1X8		0x3002
#define MEDIA_BUS_FMT_SRGGB8_1X8		0x3014
#define MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8		0x3015
#define MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8		0x3016
#define MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8		0x3017
#define MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8		0x3018
#define MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8		0x300b
#define MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8		0x300c
#define MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8		0x3009
#define MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8		0x300d
#define MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE	0x3003
#define MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE	0x3004
#define MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE	0x3005
#define MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE	0x3006
#define MEDIA_BUS_FMT_SBGGR10_1X10		0x3007
#define MEDIA_BUS_FMT_SGBRG10_1X10		0x300e
#define MEDIA_BUS_FMT_SGRBG10_1X10		0x300a
#define MEDIA_BUS_FMT_SRGGB10_1X10		0x300f
#define MEDIA_BUS_FMT_SBGGR12_1X12		0x3008
#define MEDIA_BUS_FMT_SGBRG12_1X12		0x3010
#define MEDIA_BUS_FMT_SGRBG12_1X12		0x3011
#define MEDIA_BUS_FMT_SRGGB12_1X12		0x3012

/* JPEG compressed formats - next is	0x4002 */
#define MEDIA_BUS_FMT_JPEG_1X8			0x4001

/* Vendor specific formats - next is	0x5002 */

/* S5C73M3 sensor specific interleaved UYVY and JPEG */
#define MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8		0x5001

/* HSV - next is	0x6002 */
#define MEDIA_BUS_FMT_AHSV8888_1X32		0x6001

#endif /* __LINUX_MEDIA_BUS_FORMAT_H */
