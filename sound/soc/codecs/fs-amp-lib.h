/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * fs-amp-lib.h --- Common library for FourSemi Audio Amplifiers
 *
 * Copyright (C) 2016-2025 Shanghai FourSemi Semiconductor Co.,Ltd.
 */

#ifndef __FS_AMP_LIB_H__
#define __FS_AMP_LIB_H__

#define HI_U16(a)		(((a) >> 8) & 0xFF)
#define LO_U16(a)		((a) & 0xFF)
#define FS_TABLE_NAME_LEN	(4)
#define FS_SCENE_COUNT_MAX	(16)
#define FS_CMD_DELAY_MS_MAX	(100) /* 100ms */

#define FS_CMD_DELAY		(0xFF)
#define FS_CMD_BURST		(0xFE)
#define FS_CMD_UPDATE		(0xFD)

#define FS_SOC_ENUM_EXT(xname, xhandler_info, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = xhandler_info, \
	.get = xhandler_get, .put = xhandler_put \
}

enum fs_index_type {
	FS_INDEX_INFO = 0,
	FS_INDEX_STCOEF,
	FS_INDEX_SCENE,
	FS_INDEX_MODEL,
	FS_INDEX_REG,
	FS_INDEX_EFFECT,
	FS_INDEX_STRING,
	FS_INDEX_WOOFER,
	FS_INDEX_MAX,
};

#pragma pack(push, 1)

struct fs_reg_val {
	u8 reg;
	u16 val;
};

struct fs_reg_bits {
	u8 cmd; /* FS_CMD_UPDATE */
	u8 reg;
	u16 val;
	u16 mask;
};

struct fs_cmd_pkg {
	union {
		u8 cmd;
		struct fs_reg_val regv;
		struct fs_reg_bits regb;
	};
};

struct fs_fwm_index {
	/* Index type */
	u16 type;
	/* Offset address starting from the end of header */
	u16 offset;
};

struct fs_fwm_table {
	char name[FS_TABLE_NAME_LEN];
	u16 size; /* size of buf */
	u8 buf[];
};

struct fs_scene_index {
	/* Offset address(scene name) in string table */
	u16 name;
	/* Offset address(scene reg) in register table */
	u16 reg;
	/* Offset address(scene model) in model table */
	u16 model;
	/* Offset address(scene effect) in effect table */
	u16 effect;
};

struct fs_reg_table {
	u16 size; /* size of buf */
	u8 buf[];
};

struct fs_file_table {
	u16 name;
	u16 size; /* size of buf */
	u8 buf[];
};

struct fs_fwm_date {
	u32 year:12;
	u32 month:4;
	u32 day:5;
	u32 hour:5;
	u32 minute:6;
};

struct fs_fwm_header {
	u16 version;
	u16 project; /* Offset address(project name) in string table */
	u16 device; /* Offset address(device name) in string table */
	struct fs_fwm_date date;
	u16 crc16;
	u16 crc_size; /* Starting position for CRC checking */
	u16 chip_type;
	u16 addr; /* 7-bit i2c address */
	u16 spkid;
	u16 rsvd[6];
	u8 params[];
};

#pragma pack(pop)

struct fs_i2s_srate {
	u32 srate; /* Sample rate */
	u16 i2ssr; /* Value of Bit field[I2SSR] */
};

struct fs_pll_div {
	unsigned int bclk; /* Rate of bit clock */
	u16 pll1;
	u16 pll2;
	u16 pll3;
};

struct fs_amp_scene {
	const char *name;
	const struct fs_reg_table  *reg;
	const struct fs_file_table *model;
	const struct fs_file_table *effect;
};

struct fs_amp_lib {
	const struct fs_fwm_header *hdr;
	const struct fs_fwm_table *table[FS_INDEX_MAX];
	struct fs_amp_scene *scene;
	struct device *dev;
	int scene_count;
	u16 devid;
};

int fs_amp_load_firmware(struct fs_amp_lib *amp_lib, const char *name);

#endif // __FS_AMP_LIB_H__
