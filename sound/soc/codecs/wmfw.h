/*
 * wmfw.h - Wolfson firmware format information
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __WMFW_H
#define __WMFW_H

#include <linux/types.h>

struct wmfw_header {
	char magic[4];
	__le32 len;
	__le16 rev;
	u8 core;
	u8 ver;
} __packed;

struct wmfw_footer {
	__le64 timestamp;
	__le32 checksum;
} __packed;

struct wmfw_adsp1_sizes {
	__le32 dm;
	__le32 pm;
	__le32 zm;
} __packed;

struct wmfw_region {
	union {
		__be32 type;
		__le32 offset;
	};
	__le32 len;
	u8 data[];
} __packed;

#define WMFW_ADSP1 1

#define WMFW_ABSOLUTE  0xf0
#define WMFW_NAME_TEXT 0xfe
#define WMFW_INFO_TEXT 0xff

#define WMFW_ADSP1_PM 2
#define WMFW_ADSP1_DM 3
#define WMFW_ADSP1_ZM 4

#endif
