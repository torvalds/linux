/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MIPI CSI-2 Data Types
 *
 * Copyright (C) 2022 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _MEDIA_MIPI_CSI2_H
#define _MEDIA_MIPI_CSI2_H

/* Short packet data types */
#define MIPI_CSI2_DT_FS			0x00
#define MIPI_CSI2_DT_FE			0x01
#define MIPI_CSI2_DT_LS			0x02
#define MIPI_CSI2_DT_LE			0x03
#define MIPI_CSI2_DT_GENERIC_SHORT(n)	(0x08 + (n))	/* 0..7 */

/* Long packet data types */
#define MIPI_CSI2_DT_NULL		0x10
#define MIPI_CSI2_DT_BLANKING		0x11
#define MIPI_CSI2_DT_EMBEDDED_8B	0x12
#define MIPI_CSI2_DT_YUV420_8B		0x18
#define MIPI_CSI2_DT_YUV420_10B		0x19
#define MIPI_CSI2_DT_YUV420_8B_LEGACY	0x1a
#define MIPI_CSI2_DT_YUV420_8B_CS	0x1c
#define MIPI_CSI2_DT_YUV420_10B_CS	0x1d
#define MIPI_CSI2_DT_YUV422_8B		0x1e
#define MIPI_CSI2_DT_YUV422_10B		0x1f
#define MIPI_CSI2_DT_RGB444		0x20
#define MIPI_CSI2_DT_RGB555		0x21
#define MIPI_CSI2_DT_RGB565		0x22
#define MIPI_CSI2_DT_RGB666		0x23
#define MIPI_CSI2_DT_RGB888		0x24
#define MIPI_CSI2_DT_RAW24		0x27
#define MIPI_CSI2_DT_RAW6		0x28
#define MIPI_CSI2_DT_RAW7		0x29
#define MIPI_CSI2_DT_RAW8		0x2a
#define MIPI_CSI2_DT_RAW10		0x2b
#define MIPI_CSI2_DT_RAW12		0x2c
#define MIPI_CSI2_DT_RAW14		0x2d
#define MIPI_CSI2_DT_RAW16		0x2e
#define MIPI_CSI2_DT_RAW20		0x2f
#define MIPI_CSI2_DT_USER_DEFINED(n)	(0x30 + (n))	/* 0..7 */

#endif /* _MEDIA_MIPI_CSI2_H */
