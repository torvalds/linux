// SPDX-License-Identifier: GPL-2.0-only
//
// aw883_data_type.h --  The data type of the AW88395 chip
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
//

#ifndef __AW88395_DATA_TYPE_H__
#define __AW88395_DATA_TYPE_H__

#define PROJECT_NAME_MAX		(24)
#define CUSTOMER_NAME_MAX		(16)
#define CFG_VERSION_MAX		(4)
#define DEV_NAME_MAX			(16)
#define PROFILE_STR_MAX		(32)

#define ACF_FILE_ID			(0xa15f908)

enum aw_cfg_hdr_version {
	AW88395_CFG_HDR_VER	= 0x00000001,
	AW88395_CFG_HDR_VER_V1	= 0x01000000,
};

enum aw_cfg_dde_type {
	AW88395_DEV_NONE_TYPE_ID	= 0xFFFFFFFF,
	AW88395_DEV_TYPE_ID		= 0x00000000,
	AW88395_SKT_TYPE_ID		= 0x00000001,
	AW88395_DEV_DEFAULT_TYPE_ID	= 0x00000002,
};

enum aw_sec_type {
	ACF_SEC_TYPE_REG = 0,
	ACF_SEC_TYPE_DSP,
	ACF_SEC_TYPE_DSP_CFG,
	ACF_SEC_TYPE_DSP_FW,
	ACF_SEC_TYPE_HDR_REG,
	ACF_SEC_TYPE_HDR_DSP_CFG,
	ACF_SEC_TYPE_HDR_DSP_FW,
	ACF_SEC_TYPE_MULTIPLE_BIN,
	ACF_SEC_TYPE_SKT_PROJECT,
	ACF_SEC_TYPE_DSP_PROJECT,
	ACF_SEC_TYPE_MONITOR,
	ACF_SEC_TYPE_MAX,
};

enum profile_data_type {
	AW88395_DATA_TYPE_REG = 0,
	AW88395_DATA_TYPE_DSP_CFG,
	AW88395_DATA_TYPE_DSP_FW,
	AW88395_DATA_TYPE_MAX,
};

enum aw_prof_type {
	AW88395_PROFILE_MUSIC = 0,
	AW88395_PROFILE_VOICE,
	AW88395_PROFILE_VOIP,
	AW88395_PROFILE_RINGTONE,
	AW88395_PROFILE_RINGTONE_HS,
	AW88395_PROFILE_LOWPOWER,
	AW88395_PROFILE_BYPASS,
	AW88395_PROFILE_MMI,
	AW88395_PROFILE_FM,
	AW88395_PROFILE_NOTIFICATION,
	AW88395_PROFILE_RECEIVER,
	AW88395_PROFILE_MAX,
};

enum aw_profile_status {
	AW88395_PROFILE_WAIT = 0,
	AW88395_PROFILE_OK,
};

struct aw_cfg_hdr {
	u32 id;
	char project[PROJECT_NAME_MAX];
	char custom[CUSTOMER_NAME_MAX];
	char version[CFG_VERSION_MAX];
	u32 author_id;
	u32 ddt_size;
	u32 ddt_num;
	u32 hdr_offset;
	u32 hdr_version;
	u32 reserved[3];
};

struct aw_cfg_dde {
	u32 type;
	char dev_name[DEV_NAME_MAX];
	u16 dev_index;
	u16 dev_bus;
	u16 dev_addr;
	u16 dev_profile;
	u32 data_type;
	u32 data_size;
	u32 data_offset;
	u32 data_crc;
	u32 reserved[5];
};

struct aw_cfg_dde_v1 {
	u32 type;
	char dev_name[DEV_NAME_MAX];
	u16 dev_index;
	u16 dev_bus;
	u16 dev_addr;
	u16 dev_profile;
	u32 data_type;
	u32 data_size;
	u32 data_offset;
	u32 data_crc;
	char dev_profile_str[PROFILE_STR_MAX];
	u32 chip_id;
	u32 reserved[4];
};

struct aw_sec_data_desc {
	u32 len;
	u8 *data;
};

struct aw_prof_desc {
	u32 id;
	u32 prof_st;
	char *prf_str;
	u32 fw_ver;
	struct aw_sec_data_desc sec_desc[AW88395_DATA_TYPE_MAX];
};

struct aw_all_prof_info {
	struct aw_prof_desc prof_desc[AW88395_PROFILE_MAX];
};

struct aw_prof_info {
	int count;
	int prof_type;
	char **prof_name_list;
	struct aw_prof_desc *prof_desc;
};

#endif
