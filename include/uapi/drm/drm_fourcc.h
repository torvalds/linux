/*
 * Copyright 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef DRM_FOURCC_H
#define DRM_FOURCC_H

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
				 ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define DRM_FORMAT_BIG_ENDIAN (1<<31) /* format is big endian instead of little endian */

/* color index */
#define DRM_FORMAT_C8		fourcc_code('C', '8', ' ', ' ') /* [7:0] C */

/* 8 bpp Red */
#define DRM_FORMAT_R8		fourcc_code('R', '8', ' ', ' ') /* [7:0] R */

/* 16 bpp Red */
#define DRM_FORMAT_R16		fourcc_code('R', '1', '6', ' ') /* [15:0] R little endian */

/* 16 bpp RG */
#define DRM_FORMAT_RG88		fourcc_code('R', 'G', '8', '8') /* [15:0] R:G 8:8 little endian */
#define DRM_FORMAT_GR88		fourcc_code('G', 'R', '8', '8') /* [15:0] G:R 8:8 little endian */

/* 32 bpp RG */
#define DRM_FORMAT_RG1616	fourcc_code('R', 'G', '3', '2') /* [31:0] R:G 16:16 little endian */
#define DRM_FORMAT_GR1616	fourcc_code('G', 'R', '3', '2') /* [31:0] G:R 16:16 little endian */

/* 8 bpp RGB */
#define DRM_FORMAT_RGB332	fourcc_code('R', 'G', 'B', '8') /* [7:0] R:G:B 3:3:2 */
#define DRM_FORMAT_BGR233	fourcc_code('B', 'G', 'R', '8') /* [7:0] B:G:R 2:3:3 */

/* 16 bpp RGB */
#define DRM_FORMAT_XRGB4444	fourcc_code('X', 'R', '1', '2') /* [15:0] x:R:G:B 4:4:4:4 little endian */
#define DRM_FORMAT_XBGR4444	fourcc_code('X', 'B', '1', '2') /* [15:0] x:B:G:R 4:4:4:4 little endian */
#define DRM_FORMAT_RGBX4444	fourcc_code('R', 'X', '1', '2') /* [15:0] R:G:B:x 4:4:4:4 little endian */
#define DRM_FORMAT_BGRX4444	fourcc_code('B', 'X', '1', '2') /* [15:0] B:G:R:x 4:4:4:4 little endian */

#define DRM_FORMAT_ARGB4444	fourcc_code('A', 'R', '1', '2') /* [15:0] A:R:G:B 4:4:4:4 little endian */
#define DRM_FORMAT_ABGR4444	fourcc_code('A', 'B', '1', '2') /* [15:0] A:B:G:R 4:4:4:4 little endian */
#define DRM_FORMAT_RGBA4444	fourcc_code('R', 'A', '1', '2') /* [15:0] R:G:B:A 4:4:4:4 little endian */
#define DRM_FORMAT_BGRA4444	fourcc_code('B', 'A', '1', '2') /* [15:0] B:G:R:A 4:4:4:4 little endian */

#define DRM_FORMAT_XRGB1555	fourcc_code('X', 'R', '1', '5') /* [15:0] x:R:G:B 1:5:5:5 little endian */
#define DRM_FORMAT_XBGR1555	fourcc_code('X', 'B', '1', '5') /* [15:0] x:B:G:R 1:5:5:5 little endian */
#define DRM_FORMAT_RGBX5551	fourcc_code('R', 'X', '1', '5') /* [15:0] R:G:B:x 5:5:5:1 little endian */
#define DRM_FORMAT_BGRX5551	fourcc_code('B', 'X', '1', '5') /* [15:0] B:G:R:x 5:5:5:1 little endian */

#define DRM_FORMAT_ARGB1555	fourcc_code('A', 'R', '1', '5') /* [15:0] A:R:G:B 1:5:5:5 little endian */
#define DRM_FORMAT_ABGR1555	fourcc_code('A', 'B', '1', '5') /* [15:0] A:B:G:R 1:5:5:5 little endian */
#define DRM_FORMAT_RGBA5551	fourcc_code('R', 'A', '1', '5') /* [15:0] R:G:B:A 5:5:5:1 little endian */
#define DRM_FORMAT_BGRA5551	fourcc_code('B', 'A', '1', '5') /* [15:0] B:G:R:A 5:5:5:1 little endian */

#define DRM_FORMAT_RGB565	fourcc_code('R', 'G', '1', '6') /* [15:0] R:G:B 5:6:5 little endian */
#define DRM_FORMAT_BGR565	fourcc_code('B', 'G', '1', '6') /* [15:0] B:G:R 5:6:5 little endian */

/* 24 bpp RGB */
#define DRM_FORMAT_RGB888	fourcc_code('R', 'G', '2', '4') /* [23:0] R:G:B little endian */
#define DRM_FORMAT_BGR888	fourcc_code('B', 'G', '2', '4') /* [23:0] B:G:R little endian */

/* 32 bpp RGB */
#define DRM_FORMAT_XRGB8888	fourcc_code('X', 'R', '2', '4') /* [31:0] x:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_XBGR8888	fourcc_code('X', 'B', '2', '4') /* [31:0] x:B:G:R 8:8:8:8 little endian */
#define DRM_FORMAT_RGBX8888	fourcc_code('R', 'X', '2', '4') /* [31:0] R:G:B:x 8:8:8:8 little endian */
#define DRM_FORMAT_BGRX8888	fourcc_code('B', 'X', '2', '4') /* [31:0] B:G:R:x 8:8:8:8 little endian */

#define DRM_FORMAT_ARGB8888	fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_ABGR8888	fourcc_code('A', 'B', '2', '4') /* [31:0] A:B:G:R 8:8:8:8 little endian */
#define DRM_FORMAT_RGBA8888	fourcc_code('R', 'A', '2', '4') /* [31:0] R:G:B:A 8:8:8:8 little endian */
#define DRM_FORMAT_BGRA8888	fourcc_code('B', 'A', '2', '4') /* [31:0] B:G:R:A 8:8:8:8 little endian */

#define DRM_FORMAT_XRGB2101010	fourcc_code('X', 'R', '3', '0') /* [31:0] x:R:G:B 2:10:10:10 little endian */
#define DRM_FORMAT_XBGR2101010	fourcc_code('X', 'B', '3', '0') /* [31:0] x:B:G:R 2:10:10:10 little endian */
#define DRM_FORMAT_RGBX1010102	fourcc_code('R', 'X', '3', '0') /* [31:0] R:G:B:x 10:10:10:2 little endian */
#define DRM_FORMAT_BGRX1010102	fourcc_code('B', 'X', '3', '0') /* [31:0] B:G:R:x 10:10:10:2 little endian */

#define DRM_FORMAT_ARGB2101010	fourcc_code('A', 'R', '3', '0') /* [31:0] A:R:G:B 2:10:10:10 little endian */
#define DRM_FORMAT_ABGR2101010	fourcc_code('A', 'B', '3', '0') /* [31:0] A:B:G:R 2:10:10:10 little endian */
#define DRM_FORMAT_RGBA1010102	fourcc_code('R', 'A', '3', '0') /* [31:0] R:G:B:A 10:10:10:2 little endian */
#define DRM_FORMAT_BGRA1010102	fourcc_code('B', 'A', '3', '0') /* [31:0] B:G:R:A 10:10:10:2 little endian */

/* packed YCbCr */
#define DRM_FORMAT_YUYV		fourcc_code('Y', 'U', 'Y', 'V') /* [31:0] Cr0:Y1:Cb0:Y0 8:8:8:8 little endian */
#define DRM_FORMAT_YVYU		fourcc_code('Y', 'V', 'Y', 'U') /* [31:0] Cb0:Y1:Cr0:Y0 8:8:8:8 little endian */
#define DRM_FORMAT_UYVY		fourcc_code('U', 'Y', 'V', 'Y') /* [31:0] Y1:Cr0:Y0:Cb0 8:8:8:8 little endian */
#define DRM_FORMAT_VYUY		fourcc_code('V', 'Y', 'U', 'Y') /* [31:0] Y1:Cb0:Y0:Cr0 8:8:8:8 little endian */

#define DRM_FORMAT_AYUV		fourcc_code('A', 'Y', 'U', 'V') /* [31:0] A:Y:Cb:Cr 8:8:8:8 little endian */

/*
 * 2 plane RGB + A
 * index 0 = RGB plane, same format as the corresponding non _A8 format has
 * index 1 = A plane, [7:0] A
 */
#define DRM_FORMAT_XRGB8888_A8	fourcc_code('X', 'R', 'A', '8')
#define DRM_FORMAT_XBGR8888_A8	fourcc_code('X', 'B', 'A', '8')
#define DRM_FORMAT_RGBX8888_A8	fourcc_code('R', 'X', 'A', '8')
#define DRM_FORMAT_BGRX8888_A8	fourcc_code('B', 'X', 'A', '8')
#define DRM_FORMAT_RGB888_A8	fourcc_code('R', '8', 'A', '8')
#define DRM_FORMAT_BGR888_A8	fourcc_code('B', '8', 'A', '8')
#define DRM_FORMAT_RGB565_A8	fourcc_code('R', '5', 'A', '8')
#define DRM_FORMAT_BGR565_A8	fourcc_code('B', '5', 'A', '8')

/*
 * 2 plane YCbCr
 * index 0 = Y plane, [7:0] Y
 * index 1 = Cr:Cb plane, [15:0] Cr:Cb little endian
 * or
 * index 1 = Cb:Cr plane, [15:0] Cb:Cr little endian
 */
#define DRM_FORMAT_NV12		fourcc_code('N', 'V', '1', '2') /* 2x2 subsampled Cr:Cb plane */
#define DRM_FORMAT_NV21		fourcc_code('N', 'V', '2', '1') /* 2x2 subsampled Cb:Cr plane */
#define DRM_FORMAT_NV16		fourcc_code('N', 'V', '1', '6') /* 2x1 subsampled Cr:Cb plane */
#define DRM_FORMAT_NV61		fourcc_code('N', 'V', '6', '1') /* 2x1 subsampled Cb:Cr plane */
#define DRM_FORMAT_NV24		fourcc_code('N', 'V', '2', '4') /* non-subsampled Cr:Cb plane */
#define DRM_FORMAT_NV42		fourcc_code('N', 'V', '4', '2') /* non-subsampled Cb:Cr plane */

/*
 * 3 plane YCbCr
 * index 0: Y plane, [7:0] Y
 * index 1: Cb plane, [7:0] Cb
 * index 2: Cr plane, [7:0] Cr
 * or
 * index 1: Cr plane, [7:0] Cr
 * index 2: Cb plane, [7:0] Cb
 */
#define DRM_FORMAT_YUV410	fourcc_code('Y', 'U', 'V', '9') /* 4x4 subsampled Cb (1) and Cr (2) planes */
#define DRM_FORMAT_YVU410	fourcc_code('Y', 'V', 'U', '9') /* 4x4 subsampled Cr (1) and Cb (2) planes */
#define DRM_FORMAT_YUV411	fourcc_code('Y', 'U', '1', '1') /* 4x1 subsampled Cb (1) and Cr (2) planes */
#define DRM_FORMAT_YVU411	fourcc_code('Y', 'V', '1', '1') /* 4x1 subsampled Cr (1) and Cb (2) planes */
#define DRM_FORMAT_YUV420	fourcc_code('Y', 'U', '1', '2') /* 2x2 subsampled Cb (1) and Cr (2) planes */
#define DRM_FORMAT_YVU420	fourcc_code('Y', 'V', '1', '2') /* 2x2 subsampled Cr (1) and Cb (2) planes */
#define DRM_FORMAT_YUV422	fourcc_code('Y', 'U', '1', '6') /* 2x1 subsampled Cb (1) and Cr (2) planes */
#define DRM_FORMAT_YVU422	fourcc_code('Y', 'V', '1', '6') /* 2x1 subsampled Cr (1) and Cb (2) planes */
#define DRM_FORMAT_YUV444	fourcc_code('Y', 'U', '2', '4') /* non-subsampled Cb (1) and Cr (2) planes */
#define DRM_FORMAT_YVU444	fourcc_code('Y', 'V', '2', '4') /* non-subsampled Cr (1) and Cb (2) planes */


/*
 * Format Modifiers:
 *
 * Format modifiers describe, typically, a re-ordering or modification
 * of the data in a plane of an FB.  This can be used to express tiled/
 * swizzled formats, or compression, or a combination of the two.
 *
 * The upper 8 bits of the format modifier are a vendor-id as assigned
 * below.  The lower 56 bits are assigned as vendor sees fit.
 */

/* Vendor Ids: */
#define DRM_FORMAT_MOD_NONE           0
#define DRM_FORMAT_MOD_VENDOR_NONE    0
#define DRM_FORMAT_MOD_VENDOR_INTEL   0x01
#define DRM_FORMAT_MOD_VENDOR_AMD     0x02
#define DRM_FORMAT_MOD_VENDOR_NV      0x03
#define DRM_FORMAT_MOD_VENDOR_SAMSUNG 0x04
#define DRM_FORMAT_MOD_VENDOR_QCOM    0x05
#define DRM_FORMAT_MOD_VENDOR_VIVANTE 0x06
#define DRM_FORMAT_MOD_VENDOR_BROADCOM 0x07
/* add more to the end as needed */

#define DRM_FORMAT_RESERVED	      ((1ULL << 56) - 1)

#define fourcc_mod_code(vendor, val) \
	((((__u64)DRM_FORMAT_MOD_VENDOR_## vendor) << 56) | (val & 0x00ffffffffffffffULL))

/*
 * Format Modifier tokens:
 *
 * When adding a new token please document the layout with a code comment,
 * similar to the fourcc codes above. drm_fourcc.h is considered the
 * authoritative source for all of these.
 */

/*
 * Invalid Modifier
 *
 * This modifier can be used as a sentinel to terminate the format modifiers
 * list, or to initialize a variable with an invalid modifier. It might also be
 * used to report an error back to userspace for certain APIs.
 */
#define DRM_FORMAT_MOD_INVALID	fourcc_mod_code(NONE, DRM_FORMAT_RESERVED)

/*
 * Linear Layout
 *
 * Just plain linear layout. Note that this is different from no specifying any
 * modifier (e.g. not setting DRM_MODE_FB_MODIFIERS in the DRM_ADDFB2 ioctl),
 * which tells the driver to also take driver-internal information into account
 * and so might actually result in a tiled framebuffer.
 */
#define DRM_FORMAT_MOD_LINEAR	fourcc_mod_code(NONE, 0)

/* Intel framebuffer modifiers */

/*
 * Intel X-tiling layout
 *
 * This is a tiled layout using 4Kb tiles (except on gen2 where the tiles 2Kb)
 * in row-major layout. Within the tile bytes are laid out row-major, with
 * a platform-dependent stride. On top of that the memory can apply
 * platform-depending swizzling of some higher address bits into bit6.
 *
 * This format is highly platforms specific and not useful for cross-driver
 * sharing. It exists since on a given platform it does uniquely identify the
 * layout in a simple way for i915-specific userspace.
 */
#define I915_FORMAT_MOD_X_TILED	fourcc_mod_code(INTEL, 1)

/*
 * Intel Y-tiling layout
 *
 * This is a tiled layout using 4Kb tiles (except on gen2 where the tiles 2Kb)
 * in row-major layout. Within the tile bytes are laid out in OWORD (16 bytes)
 * chunks column-major, with a platform-dependent height. On top of that the
 * memory can apply platform-depending swizzling of some higher address bits
 * into bit6.
 *
 * This format is highly platforms specific and not useful for cross-driver
 * sharing. It exists since on a given platform it does uniquely identify the
 * layout in a simple way for i915-specific userspace.
 */
#define I915_FORMAT_MOD_Y_TILED	fourcc_mod_code(INTEL, 2)

/*
 * Intel Yf-tiling layout
 *
 * This is a tiled layout using 4Kb tiles in row-major layout.
 * Within the tile pixels are laid out in 16 256 byte units / sub-tiles which
 * are arranged in four groups (two wide, two high) with column-major layout.
 * Each group therefore consits out of four 256 byte units, which are also laid
 * out as 2x2 column-major.
 * 256 byte units are made out of four 64 byte blocks of pixels, producing
 * either a square block or a 2:1 unit.
 * 64 byte blocks of pixels contain four pixel rows of 16 bytes, where the width
 * in pixel depends on the pixel depth.
 */
#define I915_FORMAT_MOD_Yf_TILED fourcc_mod_code(INTEL, 3)

/*
 * Intel color control surface (CCS) for render compression
 *
 * The framebuffer format must be one of the 8:8:8:8 RGB formats.
 * The main surface will be plane index 0 and must be Y/Yf-tiled,
 * the CCS will be plane index 1.
 *
 * Each CCS tile matches a 1024x512 pixel area of the main surface.
 * To match certain aspects of the 3D hardware the CCS is
 * considered to be made up of normal 128Bx32 Y tiles, Thus
 * the CCS pitch must be specified in multiples of 128 bytes.
 *
 * In reality the CCS tile appears to be a 64Bx64 Y tile, composed
 * of QWORD (8 bytes) chunks instead of OWORD (16 bytes) chunks.
 * But that fact is not relevant unless the memory is accessed
 * directly.
 */
#define I915_FORMAT_MOD_Y_TILED_CCS	fourcc_mod_code(INTEL, 4)
#define I915_FORMAT_MOD_Yf_TILED_CCS	fourcc_mod_code(INTEL, 5)

/*
 * Tiled, NV12MT, grouped in 64 (pixels) x 32 (lines) -sized macroblocks
 *
 * Macroblocks are laid in a Z-shape, and each pixel data is following the
 * standard NV12 style.
 * As for NV12, an image is the result of two frame buffers: one for Y,
 * one for the interleaved Cb/Cr components (1/2 the height of the Y buffer).
 * Alignment requirements are (for each buffer):
 * - multiple of 128 pixels for the width
 * - multiple of  32 pixels for the height
 *
 * For more information: see https://linuxtv.org/downloads/v4l-dvb-apis/re32.html
 */
#define DRM_FORMAT_MOD_SAMSUNG_64_32_TILE	fourcc_mod_code(SAMSUNG, 1)

/* Vivante framebuffer modifiers */

/*
 * Vivante 4x4 tiling layout
 *
 * This is a simple tiled layout using tiles of 4x4 pixels in a row-major
 * layout.
 */
#define DRM_FORMAT_MOD_VIVANTE_TILED		fourcc_mod_code(VIVANTE, 1)

/*
 * Vivante 64x64 super-tiling layout
 *
 * This is a tiled layout using 64x64 pixel super-tiles, where each super-tile
 * contains 8x4 groups of 2x4 tiles of 4x4 pixels (like above) each, all in row-
 * major layout.
 *
 * For more information: see
 * https://github.com/etnaviv/etna_viv/blob/master/doc/hardware.md#texture-tiling
 */
#define DRM_FORMAT_MOD_VIVANTE_SUPER_TILED	fourcc_mod_code(VIVANTE, 2)

/*
 * Vivante 4x4 tiling layout for dual-pipe
 *
 * Same as the 4x4 tiling layout, except every second 4x4 pixel tile starts at a
 * different base address. Offsets from the base addresses are therefore halved
 * compared to the non-split tiled layout.
 */
#define DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED	fourcc_mod_code(VIVANTE, 3)

/*
 * Vivante 64x64 super-tiling layout for dual-pipe
 *
 * Same as the 64x64 super-tiling layout, except every second 4x4 pixel tile
 * starts at a different base address. Offsets from the base addresses are
 * therefore halved compared to the non-split super-tiled layout.
 */
#define DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED fourcc_mod_code(VIVANTE, 4)

/* NVIDIA Tegra frame buffer modifiers */

/*
 * Some modifiers take parameters, for example the number of vertical GOBs in
 * a block. Reserve the lower 32 bits for parameters
 */
#define __fourcc_mod_tegra_mode_shift 32
#define fourcc_mod_tegra_code(val, params) \
	fourcc_mod_code(NV, ((((__u64)val) << __fourcc_mod_tegra_mode_shift) | params))
#define fourcc_mod_tegra_mod(m) \
	(m & ~((1ULL << __fourcc_mod_tegra_mode_shift) - 1))
#define fourcc_mod_tegra_param(m) \
	(m & ((1ULL << __fourcc_mod_tegra_mode_shift) - 1))

/*
 * Tegra Tiled Layout, used by Tegra 2, 3 and 4.
 *
 * Pixels are arranged in simple tiles of 16 x 16 bytes.
 */
#define NV_FORMAT_MOD_TEGRA_TILED fourcc_mod_tegra_code(1, 0)

/*
 * Tegra 16Bx2 Block Linear layout, used by TK1/TX1
 *
 * Pixels are arranged in 64x8 Groups Of Bytes (GOBs). GOBs are then stacked
 * vertically by a power of 2 (1 to 32 GOBs) to form a block.
 *
 * Within a GOB, data is ordered as 16B x 2 lines sectors laid in Z-shape.
 *
 * Parameter 'v' is the log2 encoding of the number of GOBs stacked vertically.
 * Valid values are:
 *
 * 0 == ONE_GOB
 * 1 == TWO_GOBS
 * 2 == FOUR_GOBS
 * 3 == EIGHT_GOBS
 * 4 == SIXTEEN_GOBS
 * 5 == THIRTYTWO_GOBS
 *
 * Chapter 20 "Pixel Memory Formats" of the Tegra X1 TRM describes this format
 * in full detail.
 */
#define NV_FORMAT_MOD_TEGRA_16BX2_BLOCK(v) fourcc_mod_tegra_code(2, v)

/*
 * Broadcom VC4 "T" format
 *
 * This is the primary layout that the V3D GPU can texture from (it
 * can't do linear).  The T format has:
 *
 * - 64b utiles of pixels in a raster-order grid according to cpp.  It's 4x4
 *   pixels at 32 bit depth.
 *
 * - 1k subtiles made of a 4x4 raster-order grid of 64b utiles (so usually
 *   16x16 pixels).
 *
 * - 4k tiles made of a 2x2 grid of 1k subtiles (so usually 32x32 pixels).  On
 *   even 4k tile rows, they're arranged as (BL, TL, TR, BR), and on odd rows
 *   they're (TR, BR, BL, TL), where bottom left is start of memory.
 *
 * - an image made of 4k tiles in rows either left-to-right (even rows of 4k
 *   tiles) or right-to-left (odd rows of 4k tiles).
 */
#define DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED fourcc_mod_code(BROADCOM, 1)

#if defined(__cplusplus)
}
#endif

#endif /* DRM_FOURCC_H */
