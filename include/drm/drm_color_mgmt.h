/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_COLOR_MGMT_H__
#define __DRM_COLOR_MGMT_H__

#include <linux/ctype.h>
#include <linux/math64.h>
#include <drm/drm_property.h>

struct drm_crtc;
struct drm_plane;

/**
 * drm_color_lut_extract - clamp and round LUT entries
 * @user_input: input value
 * @bit_precision: number of bits the hw LUT supports
 *
 * Extract a degamma/gamma LUT value provided by user (in the form of
 * &drm_color_lut entries) and round it to the precision supported by the
 * hardware.
 */
static inline u32 drm_color_lut_extract(u32 user_input, int bit_precision)
{
	u32 val = user_input;
	u32 max = 0xffff >> (16 - bit_precision);

	/* Round only if we're not using full precision. */
	if (bit_precision < 16) {
		val += 1UL << (16 - bit_precision - 1);
		val >>= 16 - bit_precision;
	}

	return clamp_val(val, 0, max);
}

u64 drm_color_ctm_s31_32_to_qm_n(u64 user_input, u32 m, u32 n);

void drm_crtc_enable_color_mgmt(struct drm_crtc *crtc,
				uint degamma_lut_size,
				bool has_ctm,
				uint gamma_lut_size);

int drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
				 int gamma_size);

/**
 * drm_color_lut_size - calculate the number of entries in the LUT
 * @blob: blob containing the LUT
 *
 * Returns:
 * The number of entries in the color LUT stored in @blob.
 */
static inline int drm_color_lut_size(const struct drm_property_blob *blob)
{
	return blob->length / sizeof(struct drm_color_lut);
}

enum drm_color_encoding {
	DRM_COLOR_YCBCR_BT601,
	DRM_COLOR_YCBCR_BT709,
	DRM_COLOR_YCBCR_BT2020,
	DRM_COLOR_ENCODING_MAX,
};

enum drm_color_range {
	DRM_COLOR_YCBCR_LIMITED_RANGE,
	DRM_COLOR_YCBCR_FULL_RANGE,
	DRM_COLOR_RANGE_MAX,
};

int drm_plane_create_color_properties(struct drm_plane *plane,
				      u32 supported_encodings,
				      u32 supported_ranges,
				      enum drm_color_encoding default_encoding,
				      enum drm_color_range default_range);

/**
 * enum drm_color_lut_tests - hw-specific LUT tests to perform
 *
 * The drm_color_lut_check() function takes a bitmask of the values here to
 * determine which tests to apply to a userspace-provided LUT.
 */
enum drm_color_lut_tests {
	/**
	 * @DRM_COLOR_LUT_EQUAL_CHANNELS:
	 *
	 * Checks whether the entries of a LUT all have equal values for the
	 * red, green, and blue channels.  Intended for hardware that only
	 * accepts a single value per LUT entry and assumes that value applies
	 * to all three color components.
	 */
	DRM_COLOR_LUT_EQUAL_CHANNELS = BIT(0),

	/**
	 * @DRM_COLOR_LUT_NON_DECREASING:
	 *
	 * Checks whether the entries of a LUT are always flat or increasing
	 * (never decreasing).
	 */
	DRM_COLOR_LUT_NON_DECREASING = BIT(1),
};

int drm_color_lut_check(const struct drm_property_blob *lut, u32 tests);
#endif
