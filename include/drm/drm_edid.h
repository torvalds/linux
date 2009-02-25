/*
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
#ifndef __DRM_EDID_H__
#define __DRM_EDID_H__

#include <linux/types.h>

#define EDID_LENGTH 128
#define DDC_ADDR 0x50

#ifdef BIG_ENDIAN
#error "EDID structure is little endian, need big endian versions"
#else

struct est_timings {
	u8 t1;
	u8 t2;
	u8 mfg_rsvd;
} __attribute__((packed));

struct std_timing {
	u8 hsize; /* need to multiply by 8 then add 248 */
	u8 vfreq:6; /* need to add 60 */
	u8 aspect_ratio:2; /* 00=16:10, 01=4:3, 10=5:4, 11=16:9 */
} __attribute__((packed));

/* If detailed data is pixel timing */
struct detailed_pixel_timing {
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hblank_hi:4;
	u8 hactive_hi:4;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vblank_hi:4;
	u8 vactive_hi:4;
	u8 hsync_offset_lo;
	u8 hsync_pulse_width_lo;
	u8 vsync_pulse_width_lo:4;
	u8 vsync_offset_lo:4;
	u8 vsync_pulse_width_hi:2;
	u8 vsync_offset_hi:2;
	u8 hsync_pulse_width_hi:2;
	u8 hsync_offset_hi:2;
	u8 width_mm_lo;
	u8 height_mm_lo;
	u8 height_mm_hi:4;
	u8 width_mm_hi:4;
	u8 hborder;
	u8 vborder;
	u8 unknown0:1;
	u8 vsync_positive:1;
	u8 hsync_positive:1;
	u8 separate_sync:2;
	u8 stereo:1;
	u8 unknown6:1;
	u8 interlaced:1;
} __attribute__((packed));

/* If it's not pixel timing, it'll be one of the below */
struct detailed_data_string {
	u8 str[13];
} __attribute__((packed));

struct detailed_data_monitor_range {
	u8 min_vfreq;
	u8 max_vfreq;
	u8 min_hfreq_khz;
	u8 max_hfreq_khz;
	u8 pixel_clock_mhz; /* need to multiply by 10 */
	u16 sec_gtf_toggle; /* A000=use above, 20=use below */ /* FIXME: byte order */
	u8 hfreq_start_khz; /* need to multiply by 2 */
	u8 c; /* need to divide by 2 */
	u16 m; /* FIXME: byte order */
	u8 k;
	u8 j; /* need to divide by 2 */
} __attribute__((packed));

struct detailed_data_wpindex {
	u8 white_y_lo:2;
	u8 white_x_lo:2;
	u8 pad:4;
	u8 white_x_hi;
	u8 white_y_hi;
	u8 gamma; /* need to divide by 100 then add 1 */
} __attribute__((packed));

struct detailed_data_color_point {
	u8 windex1;
	u8 wpindex1[3];
	u8 windex2;
	u8 wpindex2[3];
} __attribute__((packed));

struct detailed_non_pixel {
	u8 pad1;
	u8 type; /* ff=serial, fe=string, fd=monitor range, fc=monitor name
		    fb=color point data, fa=standard timing data,
		    f9=undefined, f8=mfg. reserved */
	u8 pad2;
	union {
		struct detailed_data_string str;
		struct detailed_data_monitor_range range;
		struct detailed_data_wpindex color;
		struct std_timing timings[5];
	} data;
} __attribute__((packed));

#define EDID_DETAIL_STD_MODES 0xfa
#define EDID_DETAIL_MONITOR_CPDATA 0xfb
#define EDID_DETAIL_MONITOR_NAME 0xfc
#define EDID_DETAIL_MONITOR_RANGE 0xfd
#define EDID_DETAIL_MONITOR_STRING 0xfe
#define EDID_DETAIL_MONITOR_SERIAL 0xff

struct detailed_timing {
	u16 pixel_clock; /* need to multiply by 10 KHz */ /* FIXME: byte order */
	union {
		struct detailed_pixel_timing pixel_data;
		struct detailed_non_pixel other_data;
	} data;
} __attribute__((packed));

struct edid {
	u8 header[8];
	/* Vendor & product info */
	u8 mfg_id[2];
	u8 prod_code[2];
	u32 serial; /* FIXME: byte order */
	u8 mfg_week;
	u8 mfg_year;
	/* EDID version */
	u8 version;
	u8 revision;
	/* Display info: */
	/*   input definition */
	u8 serration_vsync:1;
	u8 sync_on_green:1;
	u8 composite_sync:1;
	u8 separate_syncs:1;
	u8 blank_to_black:1;
	u8 video_level:2;
	u8 digital:1; /* bits below must be zero if set */
	u8 width_cm;
	u8 height_cm;
	u8 gamma;
	/*   feature support */
	u8 default_gtf:1;
	u8 preferred_timing:1;
	u8 standard_color:1;
	u8 display_type:2; /* 00=mono, 01=rgb, 10=non-rgb, 11=unknown */
	u8 pm_active_off:1;
	u8 pm_suspend:1;
	u8 pm_standby:1;
	/* Color characteristics */
	u8 red_green_lo;
	u8 black_white_lo;
	u8 red_x;
	u8 red_y;
	u8 green_x;
	u8 green_y;
	u8 blue_x;
	u8 blue_y;
	u8 white_x;
	u8 white_y;
	/* Est. timings and mfg rsvd timings*/
	struct est_timings established_timings;
	/* Standard timings 1-8*/
	struct std_timing standard_timings[8];
	/* Detailing timings 1-4 */
	struct detailed_timing detailed_timings[4];
	/* Number of 128 byte ext. blocks */
	u8 extensions;
	/* Checksum */
	u8 checksum;
} __attribute__((packed));

#endif /* little endian structs */

#define EDID_PRODUCT_ID(e) ((e)->prod_code[0] | ((e)->prod_code[1] << 8))

#endif /* __DRM_EDID_H__ */
