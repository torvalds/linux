/* SPDX-License-Identifier: MIT */
/* Copyright © 2022 Advanced Micro Devices, Inc. All rights reserved. */

#ifndef __DAL_CURSOR_CACHE_H__
#define __DAL_CURSOR_CACHE_H__

union reg_cursor_control_cfg {
	struct {
		uint32_t     cur_enable: 1;
		uint32_t         reser0: 3;
		uint32_t cur_2x_magnify: 1;
		uint32_t         reser1: 3;
		uint32_t           mode: 3;
		uint32_t         reser2: 5;
		uint32_t          pitch: 2;
		uint32_t         reser3: 6;
		uint32_t line_per_chunk: 5;
		uint32_t         reser4: 3;
	} bits;
	uint32_t raw;
};
struct cursor_position_cache_hubp {
	union reg_cursor_control_cfg cur_ctl;
	union reg_position_cfg {
		struct {
			uint32_t x_pos: 16;
			uint32_t y_pos: 16;
		} bits;
		uint32_t raw;
	} position;
	union reg_hot_spot_cfg {
		struct {
			uint32_t x_hot: 16;
			uint32_t y_hot: 16;
		} bits;
		uint32_t raw;
	} hot_spot;
	union reg_dst_offset_cfg {
		struct {
			uint32_t dst_x_offset: 13;
			uint32_t     reserved: 19;
		} bits;
		uint32_t raw;
	} dst_offset;
};

struct cursor_attribute_cache_hubp {
	uint32_t SURFACE_ADDR_HIGH;
	uint32_t SURFACE_ADDR;
	union    reg_cursor_control_cfg  cur_ctl;
	union    reg_cursor_size_cfg {
		struct {
			uint32_t  width: 16;
			uint32_t height: 16;
		} bits;
		uint32_t raw;
	} size;
	union    reg_cursor_settings_cfg {
		struct {
			uint32_t     dst_y_offset: 8;
			uint32_t chunk_hdl_adjust: 2;
			uint32_t         reserved: 22;
		} bits;
		uint32_t raw;
	} settings;
};

struct cursor_rect {
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
};

union reg_cur0_control_cfg {
	struct {
		uint32_t     cur0_enable: 1;
		uint32_t  expansion_mode: 1;
		uint32_t          reser0: 1;
		uint32_t     cur0_rom_en: 1;
		uint32_t            mode: 3;
		uint32_t        reserved: 25;
	} bits;
	uint32_t raw;
};
struct cursor_position_cache_dpp {
	union reg_cur0_control_cfg cur0_ctl;
};

struct cursor_attribute_cache_dpp {
	union reg_cur0_control_cfg cur0_ctl;
};

struct cursor_attributes_cfg {
	struct  cursor_attribute_cache_hubp aHubp;
	struct  cursor_attribute_cache_dpp  aDpp;
};

#endif
