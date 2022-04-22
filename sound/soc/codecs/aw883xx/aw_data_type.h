/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AWINIC_DATA_TYPE_H__
#define __AWINIC_DATA_TYPE_H__

#define AW_NAME_BUF_MAX (50)


/******************************************************************
* aw profile
*******************************************************************/
#define PROJECT_NAME_MAX (24)
#define CUSTOMER_NAME_MAX (16)
#define CFG_VERSION_MAX (4)
#define DEV_NAME_MAX (16)
#define PROFILE_STR_MAX (32)

#define ACF_FILE_ID (0xa15f908)

struct aw_msg_hdr {
	int32_t type;
	int32_t opcode_id;
	int32_t version;
	int32_t reseriver[3];
};

enum aw_cfg_hdr_version {
	AW_CFG_HDR_VER_0_0_0_1 = 0x00000001,
	AW_CFG_HDR_VER_1_0_0_0 = 0x01000000,
};

enum aw_cfg_dde_type {
	AW_DEV_NONE_TYPE_ID = 0xFFFFFFFF,
	AW_DEV_TYPE_ID = 0x00000000,
	AW_SKT_TYPE_ID = 0x00000001,
	AW_DEV_DEFAULT_TYPE_ID = 0x00000002,
};

enum aw_sec_type {
	ACF_SEC_TYPE_REG = 0,
	ACF_SEC_TYPE_DSP,
	ACF_SEC_TYPE_DSP_CFG,
	ACF_SEC_TYPE_DSP_FW,
	ACF_SEC_TYPE_HDR_REG,
	ACF_SEC_TYPE_HDR_DSP_CFG,
	ACF_SEC_TYPE_HDR_DSP_FW,
	ACF_SEC_TYPE_MUTLBIN,
	ACF_SEC_TYPE_SKT_PROJECT,
	ACF_SEC_TYPE_DSP_PROJECT,
	ACF_SEC_TYPE_MONITOR,
	ACF_SEC_TYPE_MAX,
};

enum profile_data_type {
	AW_DATA_TYPE_REG = 0,
	AW_DATA_TYPE_DSP_CFG,
	AW_DATA_TYPE_DSP_FW,
	AW_DATA_TYPE_MAX,
};

enum aw_prof_type {
	AW_PROFILE_MUSIC = 0,
	AW_PROFILE_VOICE,
	AW_PROFILE_VOIP,
	AW_PROFILE_RINGTONE,
	AW_PROFILE_RINGTONE_HS,
	AW_PROFILE_LOWPOWER,
	AW_PROFILE_BYPASS,
	AW_PROFILE_MMI,
	AW_PROFILE_FM,
	AW_PROFILE_NOTIFICATION,
	AW_PROFILE_RECEIVER,
	AW_PROFILE_MAX,
};

enum aw_profile_status {
	AW_PROFILE_WAIT = 0,
	AW_PROFILE_OK,
};

struct aw_cfg_hdr {
	uint32_t a_id;					/*acf file ID 0xa15f908*/
	char a_project[PROJECT_NAME_MAX];		/*project name*/
	char a_custom[CUSTOMER_NAME_MAX];		/*custom name :huawei xiaomi vivo oppo*/
	char a_version[CFG_VERSION_MAX];		/*author update version*/
	uint32_t a_author_id;				/*author id*/
	uint32_t a_ddt_size;				/*sub section table entry size*/
	uint32_t a_ddt_num;				/*sub section table entry num*/
	uint32_t a_hdr_offset;				/*sub section table offset in file*/
	uint32_t a_hdr_version;				/*sub section table version*/
	uint32_t reserve[3];
};

struct aw_cfg_dde {
	uint32_t type;					/*DDE type id*/
	char dev_name[DEV_NAME_MAX];
	uint16_t dev_index;				/*dev id*/
	uint16_t dev_bus;				/*dev bus id*/
	uint16_t dev_addr;				/*dev addr id*/
	uint16_t dev_profile;				/*dev profile id*/
	uint32_t data_type;				/*data type id*/
	uint32_t data_size;
	uint32_t data_offset;
	uint32_t data_crc;
	uint32_t reserve[5];
};

struct aw_cfg_dde_v_1_0_0_0 {
	uint32_t type;					/*DDE type id*/
	char dev_name[DEV_NAME_MAX];
	uint16_t dev_index;				/*dev id*/
	uint16_t dev_bus;				/*dev bus id*/
	uint16_t dev_addr;				/*dev addr id*/
	uint16_t dev_profile;				/*dev profile id*/
	uint32_t data_type;				/*data type id*/
	uint32_t data_size;
	uint32_t data_offset;
	uint32_t data_crc;
	char dev_profile_str[PROFILE_STR_MAX];
	uint32_t chip_id;
	uint32_t reserve[4];
};

struct aw_sec_data_desc {
	uint32_t len;
	unsigned char *data;
};

struct aw_prof_desc {
	uint32_t id;
	uint32_t prof_st;
	char *prf_str;
	uint32_t fw_ver;
	struct aw_sec_data_desc sec_desc[AW_DATA_TYPE_MAX];
};

struct aw_all_prof_info {
	struct aw_prof_desc prof_desc[AW_PROFILE_MAX];
};

struct aw_prof_info {
	int count;
	int prof_type;
	char **prof_name_list;
	struct aw_prof_desc *prof_desc;
};


#endif

