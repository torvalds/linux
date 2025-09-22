/* SPDX-License-Identifier: GPL-2.0 or MIT */

#ifndef DRM_FORMAT_INTERNAL_H
#define DRM_FORMAT_INTERNAL_H

#include <linux/bits.h>
#include <linux/types.h>

/*
 * Each pixel-format conversion helper takes a raw pixel in a
 * specific input format and returns a raw pixel in a specific
 * output format. All pixels are in little-endian byte order.
 *
 * Function names are
 *
 *   drm_pixel_<input>_to_<output>_<algorithm>()
 *
 * where <input> and <output> refer to pixel formats. The
 * <algorithm> is optional and hints to the method used for the
 * conversion. Helpers with no algorithm given apply pixel-bit
 * shifting.
 *
 * The argument type is u32. We expect this to be wide enough to
 * hold all conversion input from 32-bit RGB to any output format.
 * The Linux kernel should avoid format conversion for anything
 * but XRGB8888 input data. Converting from other format can still
 * be acceptable in some cases.
 *
 * The return type is u32. It is wide enough to hold all conversion
 * output from XRGB8888. For output formats wider than 32 bit, a
 * return type of u64 would be acceptable.
 */

/*
 * Conversions from XRGB8888
 */

static inline u32 drm_pixel_xrgb8888_to_rgb565(u32 pix)
{
	return ((pix & 0x00f80000) >> 8) |
	       ((pix & 0x0000fc00) >> 5) |
	       ((pix & 0x000000f8) >> 3);
}

static inline u32 drm_pixel_xrgb8888_to_rgbx5551(u32 pix)
{
	return ((pix & 0x00f80000) >> 8) |
	       ((pix & 0x0000f800) >> 5) |
	       ((pix & 0x000000f8) >> 2);
}

static inline u32 drm_pixel_xrgb8888_to_rgba5551(u32 pix)
{
	return drm_pixel_xrgb8888_to_rgbx5551(pix) |
	       BIT(0); /* set alpha bit */
}

static inline u32 drm_pixel_xrgb8888_to_xrgb1555(u32 pix)
{
	return ((pix & 0x00f80000) >> 9) |
	       ((pix & 0x0000f800) >> 6) |
	       ((pix & 0x000000f8) >> 3);
}

static inline u32 drm_pixel_xrgb8888_to_argb1555(u32 pix)
{
	return BIT(15) | /* set alpha bit */
	       drm_pixel_xrgb8888_to_xrgb1555(pix);
}

static inline u32 drm_pixel_xrgb8888_to_argb8888(u32 pix)
{
	return GENMASK(31, 24) | /* fill alpha bits */
	       pix;
}

static inline u32 drm_pixel_xrgb8888_to_xbgr8888(u32 pix)
{
	return ((pix & 0xff000000)) | /* also copy filler bits */
	       ((pix & 0x00ff0000) >> 16) |
	       ((pix & 0x0000ff00)) |
	       ((pix & 0x000000ff) << 16);
}

static inline u32 drm_pixel_xrgb8888_to_bgrx8888(u32 pix)
{
	return ((pix & 0xff000000) >> 24) | /* also copy filler bits */
	       ((pix & 0x00ff0000) >> 8) |
	       ((pix & 0x0000ff00) << 8) |
	       ((pix & 0x000000ff) << 24);
}

static inline u32 drm_pixel_xrgb8888_to_abgr8888(u32 pix)
{
	return GENMASK(31, 24) | /* fill alpha bits */
	       drm_pixel_xrgb8888_to_xbgr8888(pix);
}

static inline u32 drm_pixel_xrgb8888_to_xrgb2101010(u32 pix)
{
	pix = ((pix & 0x000000ff) << 2) |
	      ((pix & 0x0000ff00) << 4) |
	      ((pix & 0x00ff0000) << 6);
	return pix | ((pix >> 8) & 0x00300c03);
}

static inline u32 drm_pixel_xrgb8888_to_argb2101010(u32 pix)
{
	return GENMASK(31, 30) | /* set alpha bits */
	       drm_pixel_xrgb8888_to_xrgb2101010(pix);
}

static inline u32 drm_pixel_xrgb8888_to_xbgr2101010(u32 pix)
{
	pix = ((pix & 0x00ff0000) >> 14) |
	      ((pix & 0x0000ff00) << 4) |
	      ((pix & 0x000000ff) << 22);
	return pix | ((pix >> 8) & 0x00300c03);
}

static inline u32 drm_pixel_xrgb8888_to_abgr2101010(u32 pix)
{
	return GENMASK(31, 30) | /* set alpha bits */
	       drm_pixel_xrgb8888_to_xbgr2101010(pix);
}

#endif
