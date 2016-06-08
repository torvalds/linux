/*
 * Copyright © 2014 Red Hat Inc.
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
#ifndef DRM_DISPLAYID_H
#define DRM_DISPLAYID_H

#define DATA_BLOCK_PRODUCT_ID 0x00
#define DATA_BLOCK_DISPLAY_PARAMETERS 0x01
#define DATA_BLOCK_COLOR_CHARACTERISTICS 0x02
#define DATA_BLOCK_TYPE_1_DETAILED_TIMING 0x03
#define DATA_BLOCK_TYPE_2_DETAILED_TIMING 0x04
#define DATA_BLOCK_TYPE_3_SHORT_TIMING 0x05
#define DATA_BLOCK_TYPE_4_DMT_TIMING 0x06
#define DATA_BLOCK_VESA_TIMING 0x07
#define DATA_BLOCK_CEA_TIMING 0x08
#define DATA_BLOCK_VIDEO_TIMING_RANGE 0x09
#define DATA_BLOCK_PRODUCT_SERIAL_NUMBER 0x0a
#define DATA_BLOCK_GP_ASCII_STRING 0x0b
#define DATA_BLOCK_DISPLAY_DEVICE_DATA 0x0c
#define DATA_BLOCK_INTERFACE_POWER_SEQUENCING 0x0d
#define DATA_BLOCK_TRANSFER_CHARACTERISTICS 0x0e
#define DATA_BLOCK_DISPLAY_INTERFACE 0x0f
#define DATA_BLOCK_STEREO_DISPLAY_INTERFACE 0x10
#define DATA_BLOCK_TILED_DISPLAY 0x12

#define DATA_BLOCK_VENDOR_SPECIFIC 0x7f

#define PRODUCT_TYPE_EXTENSION 0
#define PRODUCT_TYPE_TEST 1
#define PRODUCT_TYPE_PANEL 2
#define PRODUCT_TYPE_MONITOR 3
#define PRODUCT_TYPE_TV 4
#define PRODUCT_TYPE_REPEATER 5
#define PRODUCT_TYPE_DIRECT_DRIVE 6

struct displayid_hdr {
	u8 rev;
	u8 bytes;
	u8 prod_id;
	u8 ext_count;
} __packed;

struct displayid_block {
	u8 tag;
	u8 rev;
	u8 num_bytes;
} __packed;

struct displayid_tiled_block {
	struct displayid_block base;
	u8 tile_cap;
	u8 topo[3];
	u8 tile_size[4];
	u8 tile_pixel_bezel[5];
	u8 topology_id[8];
} __packed;

struct displayid_detailed_timings_1 {
	u8 pixel_clock[3];
	u8 flags;
	u8 hactive[2];
	u8 hblank[2];
	u8 hsync[2];
	u8 hsw[2];
	u8 vactive[2];
	u8 vblank[2];
	u8 vsync[2];
	u8 vsw[2];
} __packed;

struct displayid_detailed_timing_block {
	struct displayid_block base;
	struct displayid_detailed_timings_1 timings[0];
};
#endif
