/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AW_BIN_PARSE_H__
#define __AW_BIN_PARSE_H__

#include "aw_device.h"

#define NULL				((void *)0)
#define GET_32_DATA(w, x, y, z)		((unsigned int)((((uint8_t)w) << 24) | (((uint8_t)x) << 16) | (((uint8_t)y) << 8) | ((uint8_t)z)))
#define BIN_NUM_MAX			100
#define HEADER_LEN			60
/*********************************************************
 *
 * header information
 *
 ********************************************************/
enum return_enum {
	BIN_HEADER_VER_ERR = 1,
	BIN_DATA_TYPE_ERR = 2,
	BIN_DATA_LEN_ERR = 3,
	DATA_VER_ERR = 4,
	REG_NUM_ERR = 5,
	DSP_REG_NUM_ERR = 6,
	SOC_APP_NUM_ERR = 7,
	BIN_IS_NULL = 8,
};

enum bin_header_version_enum {
	HEADER_VERSION_1_0_0 = 0x01000000,
};

enum data_type_enum {
	DATA_TYPE_REGISTER = 0x00000000,
	DATA_TYPE_DSP_REG = 0x00000010,
	DATA_TYPE_DSP_CFG = 0x00000011,
	DATA_TYPE_SOC_REG = 0x00000020,
	DATA_TYPE_SOC_APP = 0x00000021,
	DATA_TYPE_DSP_FW = DATA_TYPE_SOC_APP,
	DATA_TYPE_MULTI_BINS = 0x00002000,
};

/**
 * @DATA_VERSION_V1：default little edian
 */
enum data_version_enum {
	DATA_VERSION_V1 = 0X00000001,
	DATA_VERSION_MAX,
};

/**
 * @header_len: Frame header length
 * @check_sum: Frame header information-Checksum
 * @header_ver: Frame header information-Frame header version
 * @bin_data_type: Frame header information-Data type
 * @bin_data_ver: Frame header information-Data version
 * @bin_data_len: Frame header information-Data length
 * @ui_ver: Frame header information-ui version
 * @chip_type[8]: Frame header information-chip type
 * @reg_byte_len: Frame header information-reg byte len
 * @data_byte_len: Frame header information-data byte len
 * @device_addr: Frame header information-device addr
 * @valid_data_len: Length of valid data obtained after parsing
 * @valid_data_addr: The offset address of the valid data obtained
 *                   after parsing relative to info
 * @reg_num: The number of registers obtained after parsing
 * @reg_data_byte_len: The byte length of the register obtained after parsing
 * @download_addr: The starting address or download address obtained
 *                 after parsing
 * @app_version: The software version number obtained after parsing
 */
struct bin_header_info {
	unsigned int header_len;
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
};

/************************************************************
*
* function define
*
************************************************************/
/**
 * @len: The size of the bin file obtained from the firmware
 * @data[]: Store the bin file obtained from the firmware
 */
struct bin_container {
	unsigned int len;
	unsigned char data[];
};

/**
 * @p_addr: Offset pointer (backward offset pointer to obtain frame header
 *          information and important information)
 * @all_bin_parse_num: The number of all bin files
 * @multi_bin_parse_num: The number of single bin files
 * @single_bin_parse_num: The number of multiple bin files
 * @header_info[BIN_NUM_MAX]: Frame header information and other important data
 *                            obtained after parsing
 * @info: Obtained bin file data that needs to be parsed
 */
struct aw_bin {
	unsigned char *p_addr;
	unsigned int all_bin_parse_num;
	unsigned int multi_bin_parse_num;
	unsigned int single_bin_parse_num;
	struct bin_header_info header_info[BIN_NUM_MAX];
	struct bin_container info;
};

int aw_parsing_bin_file(struct aw_bin *bin);
int aw_parse_bin_header_1_0_0(struct aw_bin *bin);

/*******************awinic audio parse acf***********************/
int aw_dev_dsp_data_order(struct aw_device *aw_dev,
				uint8_t *data, uint32_t data_len);
int aw_dev_get_prof_data(struct aw_device *aw_dev, int index,
			struct aw_prof_desc **prof_desc);
char *aw_dev_get_prof_name(struct aw_device *aw_dev, int index);
int aw_dev_set_profile_index(struct aw_device *aw_dev, int index);
int aw_dev_get_profile_index(struct aw_device *aw_dev);
int aw_dev_check_profile_index(struct aw_device *aw_dev, int index);
int aw_dev_get_profile_count(struct aw_device *aw_dev);
int aw_dev_cfg_load(struct aw_device *aw_dev, struct aw_container *aw_cfg);
int aw_dev_load_acf_check(struct aw_container *aw_cfg);

#endif
