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

#define WMFW_MAX_ALG_NAME         256
#define WMFW_MAX_ALG_DESCR_NAME   256

#define WMFW_MAX_COEFF_NAME       256
#define WMFW_MAX_COEFF_DESCR_NAME 256

#define WMFW_CTL_FLAG_SYS         0x8000
#define WMFW_CTL_FLAG_VOLATILE    0x0004
#define WMFW_CTL_FLAG_WRITEABLE   0x0002
#define WMFW_CTL_FLAG_READABLE    0x0001

/* Non-ALSA coefficient types start at 0x1000 */
#define WMFW_CTL_TYPE_ACKED       0x1000 /* acked control */
#define WMFW_CTL_TYPE_HOSTEVENT   0x1001 /* event control */

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

struct wmfw_adsp2_sizes {
	__le32 xm;
	__le32 ym;
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

struct wmfw_id_hdr {
	__be32 core_id;
	__be32 core_rev;
	__be32 id;
	__be32 ver;
} __packed;

struct wmfw_adsp1_id_hdr {
	struct wmfw_id_hdr fw;
	__be32 zm;
	__be32 dm;
	__be32 n_algs;
} __packed;

struct wmfw_adsp2_id_hdr {
	struct wmfw_id_hdr fw;
	__be32 zm;
	__be32 xm;
	__be32 ym;
	__be32 n_algs;
} __packed;

struct wmfw_alg_hdr {
	__be32 id;
	__be32 ver;
} __packed;

struct wmfw_adsp1_alg_hdr {
	struct wmfw_alg_hdr alg;
	__be32 zm;
	__be32 dm;
} __packed;

struct wmfw_adsp2_alg_hdr {
	struct wmfw_alg_hdr alg;
	__be32 zm;
	__be32 xm;
	__be32 ym;
} __packed;

struct wmfw_adsp_alg_data {
	__le32 id;
	u8 name[WMFW_MAX_ALG_NAME];
	u8 descr[WMFW_MAX_ALG_DESCR_NAME];
	__le32 ncoeff;
	u8 data[];
} __packed;

struct wmfw_adsp_coeff_data {
	struct {
		__le16 offset;
		__le16 type;
		__le32 size;
	} hdr;
	u8 name[WMFW_MAX_COEFF_NAME];
	u8 descr[WMFW_MAX_COEFF_DESCR_NAME];
	__le16 ctl_type;
	__le16 flags;
	__le32 len;
	u8 data[];
} __packed;

struct wmfw_coeff_hdr {
	u8 magic[4];
	__le32 len;
	union {
		__be32 rev;
		__le32 ver;
	};
	union {
		__be32 core;
		__le32 core_ver;
	};
	u8 data[];
} __packed;

struct wmfw_coeff_item {
	__le16 offset;
	__le16 type;
	__le32 id;
	__le32 ver;
	__le32 sr;
	__le32 len;
	u8 data[];
} __packed;

#define WMFW_ADSP1 1
#define WMFW_ADSP2 2

#define WMFW_ABSOLUTE         0xf0
#define WMFW_ALGORITHM_DATA   0xf2
#define WMFW_NAME_TEXT        0xfe
#define WMFW_INFO_TEXT        0xff

#define WMFW_ADSP1_PM 2
#define WMFW_ADSP1_DM 3
#define WMFW_ADSP1_ZM 4

#define WMFW_ADSP2_PM 2
#define WMFW_ADSP2_ZM 4
#define WMFW_ADSP2_XM 5
#define WMFW_ADSP2_YM 6

#endif
