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

#include <linux/i2c.h>
#include <linux/types.h>

#define SCDC_SINK_VERSION 0x01

#define SCDC_SOURCE_VERSION 0x02

#define SCDC_UPDATE_0 0x10
#define  SCDC_READ_REQUEST_TEST (1 << 2)
#define  SCDC_CED_UPDATE (1 << 1)
#define  SCDC_STATUS_UPDATE (1 << 0)

#define SCDC_UPDATE_1 0x11

#define SCDC_TMDS_CONFIG 0x20
#define  SCDC_TMDS_BIT_CLOCK_RATIO_BY_40 (1 << 1)
#define  SCDC_TMDS_BIT_CLOCK_RATIO_BY_10 (0 << 1)
#define  SCDC_SCRAMBLING_ENABLE (1 << 0)

#define SCDC_SCRAMBLER_STATUS 0x21
#define  SCDC_SCRAMBLING_STATUS (1 << 0)

#define SCDC_CONFIG_0 0x30
#define  SCDC_READ_REQUEST_ENABLE (1 << 0)

#define SCDC_STATUS_FLAGS_0 0x40
#define  SCDC_CH2_LOCK (1 < 3)
#define  SCDC_CH1_LOCK (1 < 2)
#define  SCDC_CH0_LOCK (1 < 1)
#define  SCDC_CH_LOCK_MASK (SCDC_CH2_LOCK | SCDC_CH1_LOCK | SCDC_CH0_LOCK)
#define  SCDC_CLOCK_DETECT (1 << 0)

#define SCDC_STATUS_FLAGS_1 0x41

#define SCDC_ERR_DET_0_L 0x50
#define SCDC_ERR_DET_0_H 0x51
#define SCDC_ERR_DET_1_L 0x52
#define SCDC_ERR_DET_1_H 0x53
#define SCDC_ERR_DET_2_L 0x54
#define SCDC_ERR_DET_2_H 0x55
#define  SCDC_CHANNEL_VALID (1 << 7)

#define SCDC_ERR_DET_CHECKSUM 0x56

#define SCDC_TEST_CONFIG_0 0xc0
#define  SCDC_TEST_READ_REQUEST (1 << 7)
#define  SCDC_TEST_READ_REQUEST_DELAY(x) ((x) & 0x7f)

#define SCDC_MANUFACTURER_IEEE_OUI 0xd0
#define SCDC_MANUFACTURER_IEEE_OUI_SIZE 3

#define SCDC_DEVICE_ID 0xd3
#define SCDC_DEVICE_ID_SIZE 8

#define SCDC_DEVICE_HARDWARE_REVISION 0xdb
#define  SCDC_GET_DEVICE_HARDWARE_REVISION_MAJOR(x) (((x) >> 4) & 0xf)
#define  SCDC_GET_DEVICE_HARDWARE_REVISION_MINOR(x) (((x) >> 0) & 0xf)

#define SCDC_DEVICE_SOFTWARE_MAJOR_REVISION 0xdc
#define SCDC_DEVICE_SOFTWARE_MINOR_REVISION 0xdd

#define SCDC_MANUFACTURER_SPECIFIC 0xde
#define SCDC_MANUFACTURER_SPECIFIC_SIZE 34

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
