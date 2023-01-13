// SPDX-License-Identifier: GPL-2.0-only
//
// aw88395_lib.h  -- ACF bin parsing and check library file for aw88395
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
//

#ifndef __AW88395_LIB_H__
#define __AW88395_LIB_H__

#define CHECK_REGISTER_NUM_OFFSET	(4)
#define VALID_DATA_LEN			(4)
#define VALID_DATA_ADDR		(4)
#define PARSE_DSP_REG_NUM		(4)
#define REG_DATA_BYTP_LEN		(8)
#define CHECK_DSP_REG_NUM		(12)
#define DSP_VALID_DATA_LEN		(12)
#define DSP_VALID_DATA_ADDR		(12)
#define PARSE_SOC_APP_NUM		(8)
#define CHECK_SOC_APP_NUM		(12)
#define APP_DOWNLOAD_ADDR		(4)
#define APP_VALID_DATA_LEN		(12)
#define APP_VALID_DATA_ADDR		(12)
#define BIN_NUM_MAX			(100)
#define HEADER_LEN			(60)
#define BIN_DATA_TYPE_OFFSET		(8)
#define DATA_LEN			(44)
#define VALID_DATA_ADDR_OFFSET		(60)
#define START_ADDR_OFFSET		(64)

#define AW88395_FW_CHECK_PART		(10)
#define HDADER_LEN			(60)

#define HEADER_VERSION_OFFSET		(4)

enum bin_header_version_enum {
	HEADER_VERSION_V1 = 0x01000000,
};

enum data_type_enum {
	DATA_TYPE_REGISTER   = 0x00000000,
	DATA_TYPE_DSP_REG    = 0x00000010,
	DATA_TYPE_DSP_CFG    = 0x00000011,
	DATA_TYPE_SOC_REG    = 0x00000020,
	DATA_TYPE_SOC_APP    = 0x00000021,
	DATA_TYPE_DSP_FW     = 0x00000022,
	DATA_TYPE_MULTI_BINS = 0x00002000,
};

enum data_version_enum {
	DATA_VERSION_V1 = 0x00000001,
	DATA_VERSION_MAX,
};

struct bin_header_info {
	unsigned int check_sum;
	unsigned int header_ver;
	unsigned int bin_data_type;
	unsigned int bin_data_ver;
	unsigned int bin_data_len;
	unsigned int ui_ver;
	unsigned char chip_type[8];
	unsigned int reg_byte_len;
	unsigned int data_byte_len;
	unsigned int device_addr;
	unsigned int valid_data_len;
	unsigned int valid_data_addr;

	unsigned int reg_num;
	unsigned int reg_data_byte_len;
	unsigned int download_addr;
	unsigned int app_version;
	unsigned int header_len;
};

struct bin_container {
	unsigned int len;
	unsigned char data[];
};

struct aw_bin {
	unsigned char *p_addr;
	unsigned int all_bin_parse_num;
	unsigned int multi_bin_parse_num;
	unsigned int single_bin_parse_num;
	struct bin_header_info header_info[BIN_NUM_MAX];
	struct bin_container info;
};

#endif
