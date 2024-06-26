/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * RP1 PiSP common definitions.
 *
 * Copyright (C) 2021 - Raspberry Pi Ltd.
 *
 */
#ifndef _UAPI_PISP_COMMON_H_
#define _UAPI_PISP_COMMON_H_

#include <linux/types.h>

struct pisp_image_format_config {
	/* size in pixels */
	__u16 width;
	__u16 height;
	/* must match struct pisp_image_format below */
	__u32 format;
	__s32 stride;
	/* some planar image formats will need a second stride */
	__s32 stride2;
} __attribute__((packed));

enum pisp_bayer_order {
	/*
	 * Note how bayer_order&1 tells you if G is on the even pixels of the
	 * checkerboard or not, and bayer_order&2 tells you if R is on the even
	 * rows or is swapped with B. Note that if the top (of the 8) bits is
	 * set, this denotes a monochrome or greyscale image, and the lower bits
	 * should all be ignored.
	 */
	PISP_BAYER_ORDER_RGGB = 0,
	PISP_BAYER_ORDER_GBRG = 1,
	PISP_BAYER_ORDER_BGGR = 2,
	PISP_BAYER_ORDER_GRBG = 3,
	PISP_BAYER_ORDER_GREYSCALE = 128
};

enum pisp_image_format {
	/*
	 * Precise values are mostly tbd. Generally these will be portmanteau
	 * values comprising bit fields and flags. This format must be shared
	 * throughout the PiSP.
	 */
	PISP_IMAGE_FORMAT_BPS_8 = 0x00000000,
	PISP_IMAGE_FORMAT_BPS_10 = 0x00000001,
	PISP_IMAGE_FORMAT_BPS_12 = 0x00000002,
	PISP_IMAGE_FORMAT_BPS_16 = 0x00000003,
	PISP_IMAGE_FORMAT_BPS_MASK = 0x00000003,

	PISP_IMAGE_FORMAT_PLANARITY_INTERLEAVED = 0x00000000,
	PISP_IMAGE_FORMAT_PLANARITY_SEMI_PLANAR = 0x00000010,
	PISP_IMAGE_FORMAT_PLANARITY_PLANAR = 0x00000020,
	PISP_IMAGE_FORMAT_PLANARITY_MASK = 0x00000030,

	PISP_IMAGE_FORMAT_SAMPLING_444 = 0x00000000,
	PISP_IMAGE_FORMAT_SAMPLING_422 = 0x00000100,
	PISP_IMAGE_FORMAT_SAMPLING_420 = 0x00000200,
	PISP_IMAGE_FORMAT_SAMPLING_MASK = 0x00000300,

	PISP_IMAGE_FORMAT_ORDER_NORMAL = 0x00000000,
	PISP_IMAGE_FORMAT_ORDER_SWAPPED = 0x00001000,

	PISP_IMAGE_FORMAT_SHIFT_0 = 0x00000000,
	PISP_IMAGE_FORMAT_SHIFT_1 = 0x00010000,
	PISP_IMAGE_FORMAT_SHIFT_2 = 0x00020000,
	PISP_IMAGE_FORMAT_SHIFT_3 = 0x00030000,
	PISP_IMAGE_FORMAT_SHIFT_4 = 0x00040000,
	PISP_IMAGE_FORMAT_SHIFT_5 = 0x00050000,
	PISP_IMAGE_FORMAT_SHIFT_6 = 0x00060000,
	PISP_IMAGE_FORMAT_SHIFT_7 = 0x00070000,
	PISP_IMAGE_FORMAT_SHIFT_8 = 0x00080000,
	PISP_IMAGE_FORMAT_SHIFT_MASK = 0x000f0000,

	PISP_IMAGE_FORMAT_UNCOMPRESSED = 0x00000000,
	PISP_IMAGE_FORMAT_COMPRESSION_MODE_1 = 0x01000000,
	PISP_IMAGE_FORMAT_COMPRESSION_MODE_2 = 0x02000000,
	PISP_IMAGE_FORMAT_COMPRESSION_MODE_3 = 0x03000000,
	PISP_IMAGE_FORMAT_COMPRESSION_MASK = 0x03000000,

	PISP_IMAGE_FORMAT_HOG_SIGNED = 0x04000000,
	PISP_IMAGE_FORMAT_HOG_UNSIGNED = 0x08000000,
	PISP_IMAGE_FORMAT_INTEGRAL_IMAGE = 0x10000000,
	PISP_IMAGE_FORMAT_WALLPAPER_ROLL = 0x20000000,
	PISP_IMAGE_FORMAT_THREE_CHANNEL = 0x40000000,

	/* Lastly a few specific instantiations of the above. */
	PISP_IMAGE_FORMAT_SINGLE_16 = PISP_IMAGE_FORMAT_BPS_16,
	PISP_IMAGE_FORMAT_THREE_16 = PISP_IMAGE_FORMAT_BPS_16 |
				     PISP_IMAGE_FORMAT_THREE_CHANNEL
};

#define PISP_IMAGE_FORMAT_bps_8(fmt)                                           \
	(((fmt) & PISP_IMAGE_FORMAT_BPS_MASK) == PISP_IMAGE_FORMAT_BPS_8)
#define PISP_IMAGE_FORMAT_bps_10(fmt)                                          \
	(((fmt) & PISP_IMAGE_FORMAT_BPS_MASK) == PISP_IMAGE_FORMAT_BPS_10)
#define PISP_IMAGE_FORMAT_bps_12(fmt)                                          \
	(((fmt) & PISP_IMAGE_FORMAT_BPS_MASK) == PISP_IMAGE_FORMAT_BPS_12)
#define PISP_IMAGE_FORMAT_bps_16(fmt)                                          \
	(((fmt) & PISP_IMAGE_FORMAT_BPS_MASK) == PISP_IMAGE_FORMAT_BPS_16)
#define PISP_IMAGE_FORMAT_bps(fmt)                                             \
	(((fmt) & PISP_IMAGE_FORMAT_BPS_MASK) ?                                \
	       8 + (2 << (((fmt) & PISP_IMAGE_FORMAT_BPS_MASK) - 1)) : 8)
#define PISP_IMAGE_FORMAT_shift(fmt)                                           \
	(((fmt) & PISP_IMAGE_FORMAT_SHIFT_MASK) / PISP_IMAGE_FORMAT_SHIFT_1)
#define PISP_IMAGE_FORMAT_three_channel(fmt)                                   \
	((fmt) & PISP_IMAGE_FORMAT_THREE_CHANNEL)
#define PISP_IMAGE_FORMAT_single_channel(fmt)                                  \
	(!((fmt) & PISP_IMAGE_FORMAT_THREE_CHANNEL))
#define PISP_IMAGE_FORMAT_compressed(fmt)                                      \
	(((fmt) & PISP_IMAGE_FORMAT_COMPRESSION_MASK) !=                       \
	 PISP_IMAGE_FORMAT_UNCOMPRESSED)
#define PISP_IMAGE_FORMAT_sampling_444(fmt)                                    \
	(((fmt) & PISP_IMAGE_FORMAT_SAMPLING_MASK) ==                          \
	 PISP_IMAGE_FORMAT_SAMPLING_444)
#define PISP_IMAGE_FORMAT_sampling_422(fmt)                                    \
	(((fmt) & PISP_IMAGE_FORMAT_SAMPLING_MASK) ==                          \
	 PISP_IMAGE_FORMAT_SAMPLING_422)
#define PISP_IMAGE_FORMAT_sampling_420(fmt)                                    \
	(((fmt) & PISP_IMAGE_FORMAT_SAMPLING_MASK) ==                          \
	 PISP_IMAGE_FORMAT_SAMPLING_420)
#define PISP_IMAGE_FORMAT_order_normal(fmt)                                    \
	(!((fmt) & PISP_IMAGE_FORMAT_ORDER_SWAPPED))
#define PISP_IMAGE_FORMAT_order_swapped(fmt)                                   \
	((fmt) & PISP_IMAGE_FORMAT_ORDER_SWAPPED)
#define PISP_IMAGE_FORMAT_interleaved(fmt)                                     \
	(((fmt) & PISP_IMAGE_FORMAT_PLANARITY_MASK) ==                         \
	 PISP_IMAGE_FORMAT_PLANARITY_INTERLEAVED)
#define PISP_IMAGE_FORMAT_semiplanar(fmt)                                      \
	(((fmt) & PISP_IMAGE_FORMAT_PLANARITY_MASK) ==                         \
	 PISP_IMAGE_FORMAT_PLANARITY_SEMI_PLANAR)
#define PISP_IMAGE_FORMAT_planar(fmt)                                          \
	(((fmt) & PISP_IMAGE_FORMAT_PLANARITY_MASK) ==                         \
	 PISP_IMAGE_FORMAT_PLANARITY_PLANAR)
#define PISP_IMAGE_FORMAT_wallpaper(fmt)                                       \
	((fmt) & PISP_IMAGE_FORMAT_WALLPAPER_ROLL)
#define PISP_IMAGE_FORMAT_HOG(fmt)                                             \
	((fmt) &                                                               \
	 (PISP_IMAGE_FORMAT_HOG_SIGNED | PISP_IMAGE_FORMAT_HOG_UNSIGNED))

#define PISP_WALLPAPER_WIDTH 128 /* in bytes */

struct pisp_bla_config {
	__u16 black_level_r;
	__u16 black_level_gr;
	__u16 black_level_gb;
	__u16 black_level_b;
	__u16 output_black_level;
	__u8 pad[2];
} __attribute__((packed));

struct pisp_wbg_config {
	__u16 gain_r;
	__u16 gain_g;
	__u16 gain_b;
	__u8 pad[2];
} __attribute__((packed));

struct pisp_compress_config {
	/* value subtracted from incoming data */
	__u16 offset;
	__u8 pad;
	/* 1 => Companding; 2 => Delta (recommended); 3 => Combined (for HDR) */
	__u8 mode;
} __attribute__((packed));

struct pisp_decompress_config {
	/* value added to reconstructed data */
	__u16 offset;
	__u8 pad;
	/* 1 => Companding; 2 => Delta (recommended); 3 => Combined (for HDR) */
	__u8 mode;
} __attribute__((packed));

enum pisp_axi_flags {
	/*
	 * round down bursts to end at a 32-byte boundary, to align following
	 * bursts
	 */
	PISP_AXI_FLAG_ALIGN = 128,
	/* for FE writer: force WSTRB high, to pad output to 16-byte boundary */
	PISP_AXI_FLAG_PAD = 64,
	/* for FE writer: Use Output FIFO level to trigger "panic" */
	PISP_AXI_FLAG_PANIC = 32,
};

struct pisp_axi_config {
	/*
	 * burst length minus one, which must be in the range 0:15; OR'd with
	 * flags
	 */
	__u8 maxlen_flags;
	/* { prot[2:0], cache[3:0] } fields, echoed on AXI bus */
	__u8 cache_prot;
	/* QoS field(s) (4x4 bits for FE writer; 4 bits for other masters) */
	__u16 qos;
} __attribute__((packed));

#endif /* _UAPI_PISP_COMMON_H_ */
