/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AW_ACF_BIN_H__
#define __AW_ACF_BIN_H__

#include "aw_device.h"

#define AW_PROJECT_NAME_MAX		(24)
#define AW_CUSTOMER_NAME_MAX		(16)
#define AW_CFG_VERSION_MAX		(4)
#define AW_TBL_VERSION_MAX		(4)
#define AW_DDE_DEVICE_TYPE		(0)
#define AW_DDE_SKT_TYPE			(1)
#define AW_DDE_DEFAULT_TYPE		(2)

#define AW_REG_ADDR_BYTE		(1)
#define AW_REG_DATA_BYTE		(1)

#define AW_ACF_FILE_ID			(0xa15f908)
#define AW_PROFILE_STR_MAX 		(32)
#define AW_POWER_OFF_NAME_SUPPORT_COUNT	(5)

enum aw_cfg_hdr_version {
	AW_ACF_HDR_VER_0_0_0_1 = 0x00000001,
	AW_ACF_HDR_VER_1_0_0_0 = 0x01000000,
};

enum aw_acf_dde_type_id {
	AW_DEV_NONE_TYPE_ID = 0xFFFFFFFF,
	AW_DDE_DEV_TYPE_ID = 0x00000000,
	AW_DDE_SKT_TYPE_ID = 0x00000001,
	AW_DDE_DEV_DEFAULT_TYPE_ID = 0x00000002,
	AW_DDE_TYPE_MAX,
};

enum aw_raw_data_type_id {
	AW_BIN_TYPE_REG = 0x00000000,
	AW_BIN_TYPE_DSP,
	AW_BIN_TYPE_DSP_CFG,
	AW_BIN_TYPE_DSP_FW,
	AW_BIN_TYPE_HDR_REG,
	AW_BIN_TYPE_HDR_DSP_CFG,
	AW_BIN_TYPE_HDR_DSP_FW,
	AW_BIN_TYPE_MUTLBIN,
	AW_SKT_UI_PROJECT,
	AW_DSP_CFG,
	AW_MONITOR,
	AW_BIN_TYPE_MAX,
};

enum {
	AW_DEV_TYPE_OK = 0,
	AW_DEV_TYPE_NONE = 1,
};

enum aw_profile_status {
	AW_PROFILE_WAIT = 0,
	AW_PROFILE_OK,
};

enum aw_acf_load_status {
	AW_ACF_WAIT = 0,
	AW_ACF_UPDATE,
};

enum aw_bin_dev_profile_id {
	AW_PROFILE_MUSIC = 0x0000,
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
	AW_PROFILE_OFF,
	AW_PROFILE_MAX,
};

struct aw_acf_hdr {
	int32_t a_id;				/* acf file ID 0xa15f908 */
	char project[AW_PROJECT_NAME_MAX];	/* project name */
	char custom[AW_CUSTOMER_NAME_MAX];	/* custom name :huawei xiaomi vivo oppo */
	uint8_t version[AW_CFG_VERSION_MAX];	/* author update version */
	int32_t author_id;			/* author id */
	int32_t ddt_size;			/* sub section table entry size */
	int32_t dde_num;			/* sub section table entry num */
	int32_t ddt_offset;			/* sub section table offset in file */
	int32_t hdr_version;			/* sub section table version */
	int32_t reserve[3];			/* Reserved Bits */
};

struct aw_acf_dde {
	int32_t type;				/* dde type id */
	char dev_name[AW_CUSTOMER_NAME_MAX];	/* customer dev name */
	int16_t dev_index;			/* dev id */
	int16_t dev_bus;			/* dev bus id */
	int16_t dev_addr;			/* dev addr id */
	int16_t dev_profile;			/* dev profile id */
	int32_t data_type;			/* data type id */
	int32_t data_size;			/* dde data size in block */
	int32_t data_offset;			/* dde data offset in block */
	int32_t data_crc;			/* dde data crc checkout */
	int32_t reserve[5];			/* Reserved Bits */
};

struct aw_acf_dde_v_1_0_0_0 {
	uint32_t type;				/* DDE type id */
	char dev_name[AW_CUSTOMER_NAME_MAX];	/* customer dev name */
	uint16_t dev_index;			/* dev id */
	uint16_t dev_bus;			/* dev bus id */
	uint16_t dev_addr;			/* dev addr id */
	uint16_t dev_profile;			/* dev profile id*/
	uint32_t data_type;			/* data type id */
	uint32_t data_size;			/* dde data size in block */
	uint32_t data_offset;			/* dde data offset in block */
	uint32_t data_crc;			/* dde data crc checkout */
	char dev_profile_str[AW_PROFILE_STR_MAX];	/* dde custom profile name */
	uint32_t chip_id;			/* dde custom product chip id */
	uint32_t reserve[4];
};

struct aw_data_with_header {
	uint32_t check_sum;
	uint32_t header_ver;
	uint32_t bin_data_type;
	uint32_t bin_data_ver;
	uint32_t bin_data_size;
	uint32_t ui_ver;
	char product[8];
	uint32_t addr_byte_len;
	uint32_t data_byte_len;
	uint32_t device_addr;
	uint32_t reserve[4];
};

struct aw_data_container {
	uint32_t len;
	uint8_t *data;
};

struct aw_prof_desc {
	uint32_t prof_st;
	char *prof_name;
	char dev_name[AW_CUSTOMER_NAME_MAX];
	struct aw_data_container data_container;
};

struct aw_all_prof_info {
	struct aw_prof_desc prof_desc[AW_PROFILE_MAX];
};

struct aw_prof_info {
	int count;
	int status;
	int prof_type;
	char (*prof_name_list)[AW_PROFILE_STR_MAX];
	struct aw_prof_desc *prof_desc;
};

struct acf_bin_info {
	int load_count;
	int fw_size;
	int16_t dev_index;
	char *fw_data;
	int product_cnt;
	const char **product_tab;
	struct aw_device *aw_dev;

	struct aw_acf_hdr acf_hdr;
	struct aw_prof_info prof_info;
};


void aw_acf_profile_free(struct device *dev,
		struct acf_bin_info *acf_info);
int aw_acf_parse(struct device *dev, struct acf_bin_info *acf_info);
struct aw_prof_desc *aw_acf_get_prof_desc_form_name(struct device *dev,
			struct acf_bin_info *acf_info, char *profile_name);
int aw_acf_get_prof_index_form_name(struct device *dev,
			struct acf_bin_info *acf_info, char *profile_name);
char *aw_acf_get_prof_name_form_index(struct device *dev,
			struct acf_bin_info *acf_info, int index);
int aw_acf_get_profile_count(struct device *dev,
			struct acf_bin_info *acf_info);
int aw_acf_check_profile_is_off(struct device *dev,
			struct acf_bin_info *acf_info, char *profile_name);
char *aw_acf_get_prof_off_name(struct device *dev,
			struct acf_bin_info *acf_info);
void aw_acf_init(struct aw_device *aw_dev, struct acf_bin_info *acf_info, int index);


#endif
