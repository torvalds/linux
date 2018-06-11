/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#ifndef _V4L2_CONFIG_ROCKCHIP_H
#define _V4L2_CONFIG_ROCKCHIP_H

#define CAMERA_STRLEN         32
#define CAMERA_METADATA_LEN   (2 * PAGE_SIZE)
#define VALID_FR_EXP_T_INDEX   0
#define VALID_FR_EXP_G_INDEX   1
#define SENSOR_CONFIG_NUM      4
#define SENSOR_READ_MODE       0
#define SENSOR_WRITE_MODE      1

/* Sensor resolution specific data for AE calculation.*/
struct isp_supplemental_sensor_mode_data {
	unsigned int coarse_integration_time_min;
	unsigned int coarse_integration_time_max_margin;
	unsigned int fine_integration_time_min;
	unsigned int fine_integration_time_max_margin;
	unsigned int frame_length_lines;
	unsigned int line_length_pck;
	unsigned int vt_pix_clk_freq_hz;
	unsigned int crop_horizontal_start; /* Sensor crop start cord. (x0,y0)*/
	unsigned int crop_vertical_start;
	unsigned int crop_horizontal_end; /* Sensor crop end cord. (x1,y1)*/
	unsigned int crop_vertical_end;
	unsigned int sensor_output_width; /* input size to ISP */
	unsigned int sensor_output_height;
	unsigned int isp_input_horizontal_start;	/* cif isp input */
	unsigned int isp_input_vertical_start;
	unsigned int isp_input_width;
	unsigned int isp_input_height;
	unsigned int isp_output_width;	/* cif isp output */
	unsigned int isp_output_height;
	unsigned char binning_factor_x; /* horizontal binning factor used */
	unsigned char binning_factor_y; /* vertical binning factor used */
	/*
	*0: Exposure time valid fileds;
	*1: Exposure gain valid fileds;
	*(2 fileds == 1 frames)
	*/
	unsigned char exposure_valid_frame[2];
	int exp_time;
	unsigned short gain;
	unsigned char max_exp_gain_h;
	unsigned char max_exp_gain_l;
};

struct camera_module_info_s {
	char sensor_name[CAMERA_STRLEN];
	char module_name[CAMERA_STRLEN];
	char len_name[CAMERA_STRLEN];
	char fov_h[CAMERA_STRLEN];
	char fov_v[CAMERA_STRLEN];
	char focal_length[CAMERA_STRLEN];
	char focus_distance[CAMERA_STRLEN];
	int facing;
	int orientation;
	bool iq_mirror;
	bool iq_flip;
	int flash_support;
	int flash_exp_percent;
	int af_support;
};

struct sensor_resolution_s {
	unsigned short width;
	unsigned short height;
};

struct sensor_config_info_s {
	unsigned char config_num;
	unsigned char sensor_fmt[SENSOR_CONFIG_NUM];
	struct sensor_resolution_s reso[SENSOR_CONFIG_NUM];
};

struct sensor_reg_rw_s {
	unsigned char reg_access_mode;
	unsigned char reg_addr_len;
	unsigned char reg_data_len;
	unsigned short addr;
	unsigned short data;
};

struct flash_timeinfo_s {
	struct timeval preflash_start_t;
	struct timeval preflash_end_t;
	struct timeval mainflash_start_t;
	struct timeval mainflash_end_t;
	int flash_turn_on_time;
	int flash_on_timeout;
};

struct frame_timeinfo_s {
	struct timeval vs_t;
	struct timeval fi_t;
};

struct sensor_metadata_s {
	unsigned int exp_time;
	unsigned int gain;
};

struct v4l2_buffer_metadata_s {
	unsigned int frame_id;
	struct frame_timeinfo_s frame_t;
	struct flash_timeinfo_s flash_t;
	struct sensor_metadata_s sensor;
	unsigned char isp[CAMERA_METADATA_LEN - 512];
};

#endif

