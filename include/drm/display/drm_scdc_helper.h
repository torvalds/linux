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

#include <linux/types.h>

#include <drm/display/drm_scdc.h>

struct i2c_adapter;

ssize_t drm_scdc_read(struct i2c_adapter *adapter, u8 offset, void *buffer,
		      size_t size);
ssize_t drm_scdc_write(struct i2c_adapter *adapter, u8 offset,
		       const void *buffer, size_t size);

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

bool drm_scdc_get_scrambling_status(struct i2c_adapter *adapter);

bool drm_scdc_set_scrambling(struct i2c_adapter *adapter, bool enable);
bool drm_scdc_set_high_tmds_clock_ratio(struct i2c_adapter *adapter, bool set);

#endif
