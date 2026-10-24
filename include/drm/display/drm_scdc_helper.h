/*
 * Copyright (c) 2015 NVIDIA Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DRM_SCDC_HELPER_H
#define DRM_SCDC_HELPER_H

#include <linux/errno.h>
#include <linux/types.h>

#include <drm/display/drm_scdc.h>

struct drm_connector;
struct i2c_adapter;
struct dentry;

struct drm_scdc_status_flags {
	/* Status Register 0 */
	bool clock_detected;
	bool ch0_locked;
	bool ch1_locked;
	bool ch2_locked;
	bool ln3_locked;
	bool flt_ready;
	bool dsc_fail;

	/* Status Register 1 */
	u8 ln0_training_pattern : 4;
	u8 ln1_training_pattern : 4;

	/* Status Register 2 */
	u8 ln2_training_pattern : 4;
	u8 ln3_training_pattern : 4;
};

enum drm_scdc_frl_rate {
	SCDC_FRL_RATE_OFF = 0,
	SCDC_FRL_RATE_3X3 = 1,
	SCDC_FRL_RATE_6X3 = 2,
	SCDC_FRL_RATE_6X4 = 3,
	SCDC_FRL_RATE_8X4 = 4,
	SCDC_FRL_RATE_10X4 = 5,
	SCDC_FRL_RATE_12X4 = 6,
	SCDC_FRL_RATE_RESV_7 = 7,
	SCDC_FRL_RATE_RESV_8 = 8,
	SCDC_FRL_RATE_RESV_9 = 9,
	SCDC_FRL_RATE_RESV_10 = 10,
	SCDC_FRL_RATE_RESV_11 = 11,
	SCDC_FRL_RATE_RESV_12 = 12,
	SCDC_FRL_RATE_RESV_13 = 13,
	SCDC_FRL_RATE_RESV_14 = 14,
	SCDC_FRL_RATE_RESV_15 = 15
};

/**
 * drm_scdc_num_frl_lanes - get number of lanes for a given FRL rate
 * @rate: one of &enum drm_scdc_frl_rate
 *
 * For a given @rate, return the number of lanes it uses.
 *
 * Returns: %-EINVAL if @rate is not a valid FRL rate, or the number of lanes
 * for a given &enum drm_scdc_frl_rate on success (including %0 for "off")
 */
static inline __pure int drm_scdc_num_frl_lanes(enum drm_scdc_frl_rate rate)
{
	switch (rate) {
	case SCDC_FRL_RATE_OFF:
		return 0;
	case SCDC_FRL_RATE_3X3:
	case SCDC_FRL_RATE_6X3:
		return 3;
	case SCDC_FRL_RATE_6X4:
	case SCDC_FRL_RATE_8X4:
	case SCDC_FRL_RATE_10X4:
	case SCDC_FRL_RATE_12X4:
		return 4;
	default:
		return -EINVAL;
	}
}

struct drm_scdc_state {
	/** @stf: contents of the status flag registers */
	struct drm_scdc_status_flags stf;
	/** @scramling_enabled: true if TMDS scrambling is on */
	bool scrambling_enabled;
	/** @scrambling_detected: true if the sink actually detected scrambling */
	bool scrambling_detected;
	/**
	 * @tmds_bclk_x40: true if TMDS bit period is 1/40th of the TMDS
	 * clock period, false if it's 1/10th of the clock period.
	 */
	bool tmds_bclk_x40;
	/** @rate: FRL rate set by the source */
	enum drm_scdc_frl_rate rate : 4;
	/** @ffe_levels: The FFE levels for @rate set by the source */
	u8 ffe_levels : 4;
	/** @error_count: character error counts for each channel/link */
	u16 error_count[4];
	/** @rs_corrections: number of Reed-Solomon Corrections */
	u16 rs_corrections;
	/** @scdc: raw SCDC data buffer */
	u8 scdc[256];
};

int drm_scdc_read(struct i2c_adapter *adapter, u8 offset, void *buffer,
		  size_t size);
int drm_scdc_write(struct i2c_adapter *adapter, u8 offset, const void *buffer,
		   size_t size);

/**
 * drm_scdc_readb - read a single byte from SCDC
 * @adapter: I2C adapter
 * @offset: offset of register to read
 * @value: return location for the register value
 *
 * Reads a single byte from SCDC. This is a convenience wrapper around the
 * drm_scdc_read() function.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
static inline int drm_scdc_readb(struct i2c_adapter *adapter, u8 offset,
				 u8 *value)
{
	return drm_scdc_read(adapter, offset, value, sizeof(*value));
}

/**
 * drm_scdc_writeb - write a single byte to SCDC
 * @adapter: I2C adapter
 * @offset: offset of register to read
 * @value: return location for the register value
 *
 * Writes a single byte to SCDC. This is a convenience wrapper around the
 * drm_scdc_write() function.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
static inline int drm_scdc_writeb(struct i2c_adapter *adapter, u8 offset,
				  u8 value)
{
	return drm_scdc_write(adapter, offset, &value, sizeof(value));
}

bool drm_scdc_get_scrambling_status(struct drm_connector *connector);

bool drm_scdc_set_scrambling(struct drm_connector *connector, bool enable);
bool drm_scdc_set_high_tmds_clock_ratio(struct drm_connector *connector, bool set);

int drm_scdc_set_source_version(struct drm_connector *connector, u8 ver);

int drm_scdc_read_state(struct drm_connector *connector,
			struct drm_scdc_state *state);
void drm_scdc_debugfs_init(struct drm_connector *connector, struct dentry *root);

#endif
