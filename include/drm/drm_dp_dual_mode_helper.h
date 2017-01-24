/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef DRM_DP_DUAL_MODE_HELPER_H
#define DRM_DP_DUAL_MODE_HELPER_H

#include <linux/types.h>

/*
 * Optional for type 1 DVI adaptors
 * Mandatory for type 1 HDMI and type 2 adaptors
 */
#define DP_DUAL_MODE_HDMI_ID 0x00 /* 00-0f */
#define  DP_DUAL_MODE_HDMI_ID_LEN 16
/*
 * Optional for type 1 adaptors
 * Mandatory for type 2 adaptors
 */
#define DP_DUAL_MODE_ADAPTOR_ID 0x10
#define  DP_DUAL_MODE_REV_MASK 0x07
#define  DP_DUAL_MODE_REV_TYPE2 0x00
#define  DP_DUAL_MODE_TYPE_MASK 0xf0
#define  DP_DUAL_MODE_TYPE_TYPE2 0xa0
/* This field is marked reserved in dual mode spec, used in LSPCON */
#define  DP_DUAL_MODE_TYPE_HAS_DPCD 0x08
#define DP_DUAL_MODE_IEEE_OUI 0x11 /* 11-13*/
#define  DP_DUAL_IEEE_OUI_LEN 3
#define DP_DUAL_DEVICE_ID 0x14 /* 14-19 */
#define  DP_DUAL_DEVICE_ID_LEN 6
#define DP_DUAL_MODE_HARDWARE_REV 0x1a
#define DP_DUAL_MODE_FIRMWARE_MAJOR_REV 0x1b
#define DP_DUAL_MODE_FIRMWARE_MINOR_REV 0x1c
#define DP_DUAL_MODE_MAX_TMDS_CLOCK 0x1d
#define DP_DUAL_MODE_I2C_SPEED_CAP 0x1e
#define DP_DUAL_MODE_TMDS_OEN 0x20
#define  DP_DUAL_MODE_TMDS_DISABLE 0x01
#define DP_DUAL_MODE_HDMI_PIN_CTRL 0x21
#define  DP_DUAL_MODE_CEC_ENABLE 0x01
#define DP_DUAL_MODE_I2C_SPEED_CTRL 0x22

/* LSPCON specific registers, defined by MCA */
#define DP_DUAL_MODE_LSPCON_MODE_CHANGE		0x40
#define DP_DUAL_MODE_LSPCON_CURRENT_MODE		0x41
#define  DP_DUAL_MODE_LSPCON_MODE_PCON			0x1

struct i2c_adapter;

ssize_t drm_dp_dual_mode_read(struct i2c_adapter *adapter,
			      u8 offset, void *buffer, size_t size);
ssize_t drm_dp_dual_mode_write(struct i2c_adapter *adapter,
			       u8 offset, const void *buffer, size_t size);

/**
 * enum drm_lspcon_mode
 * @DRM_LSPCON_MODE_INVALID: No LSPCON.
 * @DRM_LSPCON_MODE_LS: Level shifter mode of LSPCON
 *	which drives DP++ to HDMI 1.4 conversion.
 * @DRM_LSPCON_MODE_PCON: Protocol converter mode of LSPCON
 *	which drives DP++ to HDMI 2.0 active conversion.
 */
enum drm_lspcon_mode {
	DRM_LSPCON_MODE_INVALID,
	DRM_LSPCON_MODE_LS,
	DRM_LSPCON_MODE_PCON,
};

/**
 * enum drm_dp_dual_mode_type - Type of the DP dual mode adaptor
 * @DRM_DP_DUAL_MODE_NONE: No DP dual mode adaptor
 * @DRM_DP_DUAL_MODE_UNKNOWN: Could be either none or type 1 DVI adaptor
 * @DRM_DP_DUAL_MODE_TYPE1_DVI: Type 1 DVI adaptor
 * @DRM_DP_DUAL_MODE_TYPE1_HDMI: Type 1 HDMI adaptor
 * @DRM_DP_DUAL_MODE_TYPE2_DVI: Type 2 DVI adaptor
 * @DRM_DP_DUAL_MODE_TYPE2_HDMI: Type 2 HDMI adaptor
 * @DRM_DP_DUAL_MODE_LSPCON: Level shifter / protocol converter
 */
enum drm_dp_dual_mode_type {
	DRM_DP_DUAL_MODE_NONE,
	DRM_DP_DUAL_MODE_UNKNOWN,
	DRM_DP_DUAL_MODE_TYPE1_DVI,
	DRM_DP_DUAL_MODE_TYPE1_HDMI,
	DRM_DP_DUAL_MODE_TYPE2_DVI,
	DRM_DP_DUAL_MODE_TYPE2_HDMI,
	DRM_DP_DUAL_MODE_LSPCON,
};

enum drm_dp_dual_mode_type drm_dp_dual_mode_detect(struct i2c_adapter *adapter);
int drm_dp_dual_mode_max_tmds_clock(enum drm_dp_dual_mode_type type,
				    struct i2c_adapter *adapter);
int drm_dp_dual_mode_get_tmds_output(enum drm_dp_dual_mode_type type,
				     struct i2c_adapter *adapter, bool *enabled);
int drm_dp_dual_mode_set_tmds_output(enum drm_dp_dual_mode_type type,
				     struct i2c_adapter *adapter, bool enable);
const char *drm_dp_get_dual_mode_type_name(enum drm_dp_dual_mode_type type);

int drm_lspcon_get_mode(struct i2c_adapter *adapter,
			enum drm_lspcon_mode *current_mode);
int drm_lspcon_set_mode(struct i2c_adapter *adapter,
			enum drm_lspcon_mode reqd_mode);
#endif
