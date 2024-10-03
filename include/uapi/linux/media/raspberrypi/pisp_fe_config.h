/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * RP1 PiSP Front End Driver Configuration structures
 *
 * Copyright (C) 2021 - Raspberry Pi Ltd.
 *
 */
#ifndef _UAPI_PISP_FE_CONFIG_
#define _UAPI_PISP_FE_CONFIG_

#include <linux/types.h>

#include "pisp_common.h"
#include "pisp_fe_statistics.h"

#define PISP_FE_NUM_OUTPUTS 2

enum pisp_fe_enable {
	PISP_FE_ENABLE_INPUT = 0x000001,
	PISP_FE_ENABLE_DECOMPRESS = 0x000002,
	PISP_FE_ENABLE_DECOMPAND = 0x000004,
	PISP_FE_ENABLE_BLA = 0x000008,
	PISP_FE_ENABLE_DPC = 0x000010,
	PISP_FE_ENABLE_STATS_CROP = 0x000020,
	PISP_FE_ENABLE_DECIMATE = 0x000040,
	PISP_FE_ENABLE_BLC = 0x000080,
	PISP_FE_ENABLE_CDAF_STATS = 0x000100,
	PISP_FE_ENABLE_AWB_STATS = 0x000200,
	PISP_FE_ENABLE_RGBY = 0x000400,
	PISP_FE_ENABLE_LSC = 0x000800,
	PISP_FE_ENABLE_AGC_STATS = 0x001000,
	PISP_FE_ENABLE_CROP0 = 0x010000,
	PISP_FE_ENABLE_DOWNSCALE0 = 0x020000,
	PISP_FE_ENABLE_COMPRESS0 = 0x040000,
	PISP_FE_ENABLE_OUTPUT0 = 0x080000,
	PISP_FE_ENABLE_CROP1 = 0x100000,
	PISP_FE_ENABLE_DOWNSCALE1 = 0x200000,
	PISP_FE_ENABLE_COMPRESS1 = 0x400000,
	PISP_FE_ENABLE_OUTPUT1 = 0x800000
};

#define PISP_FE_ENABLE_CROP(i) (PISP_FE_ENABLE_CROP0 << (4 * (i)))
#define PISP_FE_ENABLE_DOWNSCALE(i) (PISP_FE_ENABLE_DOWNSCALE0 << (4 * (i)))
#define PISP_FE_ENABLE_COMPRESS(i) (PISP_FE_ENABLE_COMPRESS0 << (4 * (i)))
#define PISP_FE_ENABLE_OUTPUT(i) (PISP_FE_ENABLE_OUTPUT0 << (4 * (i)))

/*
 * We use the enable flags to show when blocks are "dirty", but we need some
 * extra ones too.
 */
enum pisp_fe_dirty {
	PISP_FE_DIRTY_GLOBAL = 0x0001,
	PISP_FE_DIRTY_FLOATING = 0x0002,
	PISP_FE_DIRTY_OUTPUT_AXI = 0x0004
};

struct pisp_fe_global_config {
	__u32 enables;
	__u8 bayer_order;
	__u8 pad[3];
} __attribute__((packed));

struct pisp_fe_input_axi_config {
	/* burst length minus one, in the range 0..15; OR'd with flags */
	__u8 maxlen_flags;
	/* { prot[2:0], cache[3:0] } fields */
	__u8 cache_prot;
	/* QoS (only 4 LS bits are used) */
	__u16 qos;
} __attribute__((packed));

struct pisp_fe_output_axi_config {
	/* burst length minus one, in the range 0..15; OR'd with flags */
	__u8 maxlen_flags;
	/* { prot[2:0], cache[3:0] } fields */
	__u8 cache_prot;
	/* QoS (4 bitfields of 4 bits each for different panic levels) */
	__u16 qos;
	/*  For Panic mode: Output FIFO panic threshold */
	__u16 thresh;
	/*  For Panic mode: Output FIFO statistics throttle threshold */
	__u16 throttle;
} __attribute__((packed));

struct pisp_fe_input_config {
	__u8 streaming;
	__u8 pad[3];
	struct pisp_image_format_config format;
	struct pisp_fe_input_axi_config axi;
	/* Extra cycles delay before issuing each burst request */
	__u8 holdoff;
	__u8 pad2[3];
} __attribute__((packed));

struct pisp_fe_output_config {
	struct pisp_image_format_config format;
	__u16 ilines;
	__u8 pad[2];
} __attribute__((packed));

struct pisp_fe_input_buffer_config {
	__u32 addr_lo;
	__u32 addr_hi;
	__u16 frame_id;
	__u16 pad;
} __attribute__((packed));

#define PISP_FE_DECOMPAND_LUT_SIZE 65

struct pisp_fe_decompand_config {
	__u16 lut[PISP_FE_DECOMPAND_LUT_SIZE];
	__u16 pad;
} __attribute__((packed));

struct pisp_fe_dpc_config {
	__u8 coeff_level;
	__u8 coeff_range;
	__u8 coeff_range2;
#define PISP_FE_DPC_FLAG_FOLDBACK 1
#define PISP_FE_DPC_FLAG_VFLAG 2
	__u8 flags;
} __attribute__((packed));

#define PISP_FE_LSC_LUT_SIZE 16

struct pisp_fe_lsc_config {
	__u8 shift;
	__u8 pad0;
	__u16 scale;
	__u16 centre_x;
	__u16 centre_y;
	__u16 lut[PISP_FE_LSC_LUT_SIZE];
} __attribute__((packed));

struct pisp_fe_rgby_config {
	__u16 gain_r;
	__u16 gain_g;
	__u16 gain_b;
	__u8 maxflag;
	__u8 pad;
} __attribute__((packed));

struct pisp_fe_agc_stats_config {
	__u16 offset_x;
	__u16 offset_y;
	__u16 size_x;
	__u16 size_y;
	/* each weight only 4 bits */
	__u8 weights[PISP_AGC_STATS_NUM_ZONES / 2];
	__u16 row_offset_x;
	__u16 row_offset_y;
	__u16 row_size_x;
	__u16 row_size_y;
	__u8 row_shift;
	__u8 float_shift;
	__u8 pad1[2];
} __attribute__((packed));

struct pisp_fe_awb_stats_config {
	__u16 offset_x;
	__u16 offset_y;
	__u16 size_x;
	__u16 size_y;
	__u8 shift;
	__u8 pad[3];
	__u16 r_lo;
	__u16 r_hi;
	__u16 g_lo;
	__u16 g_hi;
	__u16 b_lo;
	__u16 b_hi;
} __attribute__((packed));

struct pisp_fe_floating_stats_region {
	__u16 offset_x;
	__u16 offset_y;
	__u16 size_x;
	__u16 size_y;
} __attribute__((packed));

struct pisp_fe_floating_stats_config {
	struct pisp_fe_floating_stats_region
		regions[PISP_FLOATING_STATS_NUM_ZONES];
} __attribute__((packed));

#define PISP_FE_CDAF_NUM_WEIGHTS 8

struct pisp_fe_cdaf_stats_config {
	__u16 noise_constant;
	__u16 noise_slope;
	__u16 offset_x;
	__u16 offset_y;
	__u16 size_x;
	__u16 size_y;
	__u16 skip_x;
	__u16 skip_y;
	__u32 mode;
} __attribute__((packed));

struct pisp_fe_stats_buffer_config {
	__u32 addr_lo;
	__u32 addr_hi;
} __attribute__((packed));

struct pisp_fe_crop_config {
	__u16 offset_x;
	__u16 offset_y;
	__u16 width;
	__u16 height;
} __attribute__((packed));

enum pisp_fe_downscale_flags {
	/* downscale the four Bayer components independently... */
	DOWNSCALE_BAYER = 1,
	/* ...without trying to preserve their spatial relationship */
	DOWNSCALE_BIN = 2,
};

struct pisp_fe_downscale_config {
	__u8 xin;
	__u8 xout;
	__u8 yin;
	__u8 yout;
	__u8 flags; /* enum pisp_fe_downscale_flags */
	__u8 pad[3];
	__u16 output_width;
	__u16 output_height;
} __attribute__((packed));

struct pisp_fe_output_buffer_config {
	__u32 addr_lo;
	__u32 addr_hi;
} __attribute__((packed));

/* Each of the two output channels/branches: */
struct pisp_fe_output_branch_config {
	struct pisp_fe_crop_config crop;
	struct pisp_fe_downscale_config downscale;
	struct pisp_compress_config compress;
	struct pisp_fe_output_config output;
	__u32 pad;
} __attribute__((packed));

/* And finally one to rule them all: */
struct pisp_fe_config {
	/* I/O configuration: */
	struct pisp_fe_stats_buffer_config stats_buffer;
	struct pisp_fe_output_buffer_config output_buffer[PISP_FE_NUM_OUTPUTS];
	struct pisp_fe_input_buffer_config input_buffer;
	/* processing configuration: */
	struct pisp_fe_global_config global;
	struct pisp_fe_input_config input;
	struct pisp_decompress_config decompress;
	struct pisp_fe_decompand_config decompand;
	struct pisp_bla_config bla;
	struct pisp_fe_dpc_config dpc;
	struct pisp_fe_crop_config stats_crop;
	__u32 spare1; /* placeholder for future decimate configuration */
	struct pisp_bla_config blc;
	struct pisp_fe_rgby_config rgby;
	struct pisp_fe_lsc_config lsc;
	struct pisp_fe_agc_stats_config agc_stats;
	struct pisp_fe_awb_stats_config awb_stats;
	struct pisp_fe_cdaf_stats_config cdaf_stats;
	struct pisp_fe_floating_stats_config floating_stats;
	struct pisp_fe_output_axi_config output_axi;
	struct pisp_fe_output_branch_config ch[PISP_FE_NUM_OUTPUTS];
	/* non-register fields: */
	__u32 dirty_flags; /* these use pisp_fe_enable */
	__u32 dirty_flags_extra; /* these use pisp_fe_dirty */
} __attribute__((packed));

#endif /* _UAPI_PISP_FE_CONFIG_ */
