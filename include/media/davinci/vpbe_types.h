/*
 * Copyright (C) 2010 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef _VPBE_TYPES_H
#define _VPBE_TYPES_H

enum vpbe_version {
	VPBE_VERSION_1 = 1,
	VPBE_VERSION_2,
	VPBE_VERSION_3,
};

/* vpbe_timing_type - Timing types used in vpbe device */
enum vpbe_enc_timings_type {
	VPBE_ENC_STD = 0x1,
	VPBE_ENC_DV_TIMINGS = 0x4,
	/* Used when set timings through FB device interface */
	VPBE_ENC_TIMINGS_INVALID = 0x8,
};

/*
 * struct vpbe_enc_mode_info
 * @name: ptr to name string of the standard, "NTSC", "PAL" etc
 * @std: standard or non-standard mode. 1 - standard, 0 - nonstandard
 * @interlaced: 1 - interlaced, 0 - non interlaced/progressive
 * @xres: x or horizontal resolution of the display
 * @yres: y or vertical resolution of the display
 * @fps: frame per second
 * @left_margin: left margin of the display
 * @right_margin: right margin of the display
 * @upper_margin: upper margin of the display
 * @lower_margin: lower margin of the display
 * @hsync_len: h-sync length
 * @vsync_len: v-sync length
 * @flags: bit field: bit usage is documented below
 *
 * Description:
 *  Structure holding timing and resolution information of a standard.
 * Used by vpbe_device to set required non-standard timing in the
 * venc when lcd controller output is connected to a external encoder.
 * A table of timings is maintained in vpbe device to set this in
 * venc when external encoder is connected to lcd controller output.
 * Encoder may provide a g_dv_timings() API to override these values
 * as needed.
 *
 *  Notes
 *  ------
 *  if_type should be used only by encoder manager and encoder.
 *  flags usage
 *     b0 (LSB) - hsync polarity, 0 - negative, 1 - positive
 *     b1       - vsync polarity, 0 - negative, 1 - positive
 *     b2       - field id polarity, 0 - negative, 1  - positive
 */
struct vpbe_enc_mode_info {
	unsigned char *name;
	enum vpbe_enc_timings_type timings_type;
	v4l2_std_id std_id;
	struct v4l2_dv_timings dv_timings;
	unsigned int interlaced;
	unsigned int xres;
	unsigned int yres;
	struct v4l2_fract aspect;
	struct v4l2_fract fps;
	unsigned int left_margin;
	unsigned int right_margin;
	unsigned int upper_margin;
	unsigned int lower_margin;
	unsigned int hsync_len;
	unsigned int vsync_len;
	unsigned int flags;
};

#endif
