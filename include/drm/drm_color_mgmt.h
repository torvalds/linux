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

void drm_crtc_enable_color_mgmt(struct drm_crtc *crtc,
				uint degamma_lut_size,
				bool has_ctm,
				uint gamma_lut_size);

int drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
				 int gamma_size);

/*
 * Extract a degamma/gamma LUT value provided by user and round it to the
 * precision supported by the hardware.
 */
static inline uint32_t drm_color_lut_extract(uint32_t user_input,
					     uint32_t bit_precision)
{
	uint32_t val = user_input;
	uint32_t max = 0xffff >> (16 - bit_precision);

	/* Round only if we're not using full precision. */
	if (bit_precision < 16) {
		val += 1UL << (16 - bit_precision - 1);
		val >>= 16 - bit_precision;
	}

	return clamp_val(val, 0, max);
}


#endif
