// SPDX-License-Identifier: GPL-2.0+
/*
 * aw_bin_parse.c
 *
 * Copyright (c) 2020 AWINIC Technology CO., LTD
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/string.h>
#include "aw_bin_parse.h"
#include "aw_log.h"

/* "code version"-"excel version" */
#define AWINIC_CODE_VERSION "V0.0.8-V1.0.4"

#define DEBUG_LOG_LEVEL
#ifdef DEBUG_LOG_LEVEL
#define DBG(fmt, arg...)   do {\
pr_debug("AWINIC_BIN %s,line= %d,"fmt, __func__, __LINE__, ##arg);\
} while (0)
#define DBG_ERR(fmt, arg...)   do {\
pr_err("AWINIC_BIN_ERR %s,line= %d,"fmt, __func__, __LINE__, ##arg);\
} while (0)
#else
#define DBG(fmt, arg...) do {} while (0)
#define DBG_ERR(fmt, arg...) do {} while (0)
#endif

#define printing_data_code

typedef unsigned short int aw_uint16;
typedef unsigned long int aw_uint32;

#define BigLittleSwap16(A)	((((aw_uint16)(A) & 0xff00) >> 8) | \
				 (((aw_uint16)(A) & 0x00ff) << 8))

#define BigLittleSwap32(A)	((((aw_uint32)(A) & 0xff000000) >> 24) | \
				(((aw_uint32)(A) & 0x00ff0000) >> 8) | \
				(((aw_uint32)(A) & 0x0000ff00) << 8) | \
				(((aw_uint32)(A) & 0x000000ff) << 24))

static char *profile_name[AW_PROFILE_MAX] = {"Music", "Voice", "Voip", "Ringtone", "Ringtone_hs", "Lowpower",
						"Bypass", "Mmi", "Fm", "Notification", "Receiver"};

/**
 *
 * Interface function
 *
 * return value:
 *       value = 0 :success;
 *       value = -1 :check bin header version
 *       value = -2 :check bin data type
 *       value = -3 :check sum or check bin data len error
 *       value = -4 :check data version
 *       value = -5 :check register num
 *       value = -6 :check dsp reg num
 *       value = -7 :check soc app num
 *       value = -8 :bin is NULL point
 *
 */

/********************************************************
 *
 * check sum data
 *
 ********************************************************/
static int aw_check_sum(struct aw_bin *bin, int bin_num)
{
	unsigned int i = 0;
	unsigned int sum_data = 0;
	unsigned int check_sum = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");
	p_check_sum = &(bin->info.data[(bin->header_info[bin_num].
		valid_data_addr - bin->header_info[bin_num].header_len)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	check_sum = GET_32_DATA(*(p_check_sum + 3), *(p_check_sum + 2),
				*(p_check_sum + 1), *(p_check_sum));

	for (i = 4; i < bin->header_info[bin_num].bin_data_len + bin->
					header_info[bin_num].header_len; i++) {
		sum_data += *(p_check_sum + i);
	}
	DBG("aw_bin_parse bin_num = %d, check_sum = 0x%x, sum_data = 0x%x\n",
						bin_num, check_sum, sum_data);
	if (sum_data != check_sum) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse check sum or check bin data len error\n");
		DBG_ERR("aw_bin_parse bin_num = %d\n", bin_num);
		DBG_ERR("aw_bin_parse check_sum = 0x%x\n", check_sum);
		DBG_ERR("aw_bin_parse sum_data = 0x%x\n", sum_data);
		return -BIN_DATA_LEN_ERR;
	}
	p_check_sum = NULL;

	return 0;
}

static int aw_check_data_version(struct aw_bin *bin, int bin_num)
{
	int i = 0;

	DBG("enter\n");
	for (i = DATA_VERSION_V1; i < DATA_VERSION_MAX; i++) {
		if (bin->header_info[bin_num].bin_data_ver == i)
			return 0;
	}
	DBG_ERR("aw_bin_parse Unrecognized this bin data version\n");
	return -DATA_VER_ERR;
}

static int aw_check_register_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_register_num = 0;
	unsigned int parse_register_num = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");
	p_check_sum = &(bin->info.data[(bin->header_info[bin_num].
							valid_data_addr)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	parse_register_num = GET_32_DATA(*(p_check_sum + 3), *(p_check_sum + 2),
					*(p_check_sum + 1), *(p_check_sum));
	check_register_num = (bin->header_info[bin_num].bin_data_len - 4) /
				(bin->header_info[bin_num].reg_byte_len +
				bin->header_info[bin_num].data_byte_len);
	DBG("aw_bin_parse bin_num = %d\n", bin_num);
	DBG("aw_bin_parse parse_register_num = 0x%x\n", parse_register_num);
	DBG("aw_bin_parse check_register_num = 0x%x\n", check_register_num);
	if (parse_register_num != check_register_num) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse check register num error\n");
		DBG_ERR("aw_bin_parse bin_num = %d\n", bin_num);
		DBG_ERR("aw_bin_parse parse_register_num = 0x%x\n",
							parse_register_num);
		DBG_ERR("aw_bin_parse check_register_num = 0x%x\n",
							check_register_num);
		return -REG_NUM_ERR;
	}
	bin->header_info[bin_num].reg_num = parse_register_num;
	bin->header_info[bin_num].valid_data_len = bin->header_info[bin_num].
							bin_data_len - 4;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr = bin->header_info[bin_num].
							valid_data_addr + 4;
	return 0;
}

static int aw_check_dsp_reg_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_dsp_reg_num = 0;
	unsigned int parse_dsp_reg_num = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");
	p_check_sum = &(bin->info.data[(bin->header_info[bin_num].
							valid_data_addr)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	parse_dsp_reg_num = GET_32_DATA(*(p_check_sum + 7), *(p_check_sum + 6),
					*(p_check_sum + 5), *(p_check_sum + 4));
	bin->header_info[bin_num].reg_data_byte_len =
			GET_32_DATA(*(p_check_sum + 11), *(p_check_sum + 10),
					*(p_check_sum + 9), *(p_check_sum + 8));
	check_dsp_reg_num = (bin->header_info[bin_num].bin_data_len - 12) /
				bin->header_info[bin_num].reg_data_byte_len;
	DBG("aw_bin_parse bin_num = %d\n", bin_num);
	DBG("aw_bin_parse parse_dsp_reg_num = 0x%x\n", parse_dsp_reg_num);
	DBG("aw_bin_parse check_dsp_reg_num = 0x%x\n", check_dsp_reg_num);
	if (parse_dsp_reg_num != check_dsp_reg_num) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse check dsp reg num error\n");
		DBG_ERR("aw_bin_parse bin_num = %d\n", bin_num);
		DBG_ERR("aw_bin_parse parse_dsp_reg_num = 0x%x\n",
							parse_dsp_reg_num);
		DBG_ERR("aw_bin_parse check_dsp_reg_num = 0x%x\n",
							check_dsp_reg_num);
		return -DSP_REG_NUM_ERR;
	}
	bin->header_info[bin_num].download_addr =
			GET_32_DATA(*(p_check_sum + 3), *(p_check_sum + 2),
					*(p_check_sum + 1), *(p_check_sum));
	bin->header_info[bin_num].reg_num = parse_dsp_reg_num;
	bin->header_info[bin_num].valid_data_len = bin->header_info[bin_num].
							bin_data_len - 12;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr = bin->header_info[bin_num].
							valid_data_addr + 12;
	return 0;
}

static int aw_check_soc_app_num_v1(struct aw_bin *bin, int bin_num)
{
	unsigned int check_soc_app_num = 0;
	unsigned int parse_soc_app_num = 0;
	char *p_check_sum = NULL;

	DBG("enter\n");
	p_check_sum = &(bin->info.data[(bin->header_info[bin_num].
							valid_data_addr)]);
	DBG("aw_bin_parse p_check_sum = %p\n", p_check_sum);
	bin->header_info[bin_num].app_version = GET_32_DATA(*(p_check_sum + 3),
			*(p_check_sum + 2), *(p_check_sum + 1), *(p_check_sum));
	parse_soc_app_num = GET_32_DATA(*(p_check_sum + 11),
		*(p_check_sum + 10), *(p_check_sum + 9), *(p_check_sum + 8));
	check_soc_app_num = bin->header_info[bin_num].bin_data_len - 12;
	DBG("aw_bin_parse bin_num = %d\n", bin_num);
	DBG("aw_bin_parse parse_soc_app_num = 0x%x\n", parse_soc_app_num);
	DBG("aw_bin_parse check_soc_app_num = 0x%x\n", check_soc_app_num);
	if (parse_soc_app_num != check_soc_app_num) {
		p_check_sum = NULL;
		DBG_ERR("aw_bin_parse check soc app num error\n");
		DBG_ERR("aw_bin_parse bin_num = %d\n", bin_num);
		DBG_ERR("aw_bin_parse parse_soc_app_num = 0x%x\n",
							parse_soc_app_num);
		DBG_ERR("aw_bin_parse check_soc_app_num = 0x%x\n",
							check_soc_app_num);
		return -SOC_APP_NUM_ERR;
	}
	bin->header_info[bin_num].reg_num = parse_soc_app_num;
	bin->header_info[bin_num].download_addr =
			GET_32_DATA(*(p_check_sum + 7), *(p_check_sum + 6),
					*(p_check_sum + 5), *(p_check_sum + 4));
	bin->header_info[bin_num].valid_data_len = bin->header_info[bin_num].
							bin_data_len - 12;
	p_check_sum = NULL;
	bin->header_info[bin_num].valid_data_addr = bin->header_info[bin_num].
							valid_data_addr + 12;
	return 0;
}
/********************************************************
 *
 * bin header 1_0_0
 *
 ********************************************************/
static void aw_get_single_bin_header_1_0_0(struct aw_bin *bin)
{
	int i;

	DBG("enter %s\n", __func__);
	bin->header_info[bin->all_bin_parse_num].header_len = 60;
	bin->header_info[bin->all_bin_parse_num].check_sum =
		GET_32_DATA(*(bin->p_addr + 3), *(bin->p_addr + 2),
				*(bin->p_addr + 1), *(bin->p_addr));
	bin->header_info[bin->all_bin_parse_num].header_ver =
		GET_32_DATA(*(bin->p_addr + 7), *(bin->p_addr + 6),
				*(bin->p_addr + 5), *(bin->p_addr + 4));
	bin->header_info[bin->all_bin_parse_num].bin_data_type =
		GET_32_DATA(*(bin->p_addr + 11), *(bin->p_addr + 10),
				*(bin->p_addr + 9), *(bin->p_addr + 8));
	bin->header_info[bin->all_bin_parse_num].bin_data_ver =
		GET_32_DATA(*(bin->p_addr + 15), *(bin->p_addr + 14),
				*(bin->p_addr + 13), *(bin->p_addr + 12));
	bin->header_info[bin->all_bin_parse_num].bin_data_len =
		GET_32_DATA(*(bin->p_addr + 19), *(bin->p_addr + 18),
				*(bin->p_addr + 17), *(bin->p_addr + 16));
	bin->header_info[bin->all_bin_parse_num].ui_ver =
		GET_32_DATA(*(bin->p_addr + 23), *(bin->p_addr + 22),
				*(bin->p_addr + 21), *(bin->p_addr + 20));
	bin->header_info[bin->all_bin_parse_num].reg_byte_len =
		GET_32_DATA(*(bin->p_addr + 35), *(bin->p_addr + 34),
				*(bin->p_addr + 33), *(bin->p_addr + 32));
	bin->header_info[bin->all_bin_parse_num].data_byte_len =
		GET_32_DATA(*(bin->p_addr + 39), *(bin->p_addr + 38),
				*(bin->p_addr + 37), *(bin->p_addr + 36));
	bin->header_info[bin->all_bin_parse_num].device_addr =
		GET_32_DATA(*(bin->p_addr + 43), *(bin->p_addr + 42),
			*(bin->p_addr + 41), *(bin->p_addr + 40));
	for (i = 0; i < 8; i++) {
		bin->header_info[bin->all_bin_parse_num].chip_type[i] =
						*(bin->p_addr + 24 + i);
	}
	bin->header_info[bin->all_bin_parse_num].reg_num = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].reg_data_byte_len = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].download_addr = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].app_version = 0x00000000;
	bin->header_info[bin->all_bin_parse_num].valid_data_len = 0x00000000;
	bin->all_bin_parse_num += 1;
}

static int aw_parse_each_of_multi_bins_1_0_0(unsigned int bin_num, int bin_serial_num,
				      struct aw_bin *bin)
{
	int ret = 0;
	unsigned int bin_start_addr = 0;
	unsigned int valid_data_len = 0;

	DBG("aw_bin_parse enter multi bin branch -- %s\n", __func__);
	if (!bin_serial_num) {
		bin_start_addr = GET_32_DATA(*(bin->p_addr + 67), *(bin->p_addr
			+ 66), *(bin->p_addr + 65), *(bin->p_addr + 64));
		bin->p_addr += (60 + bin_start_addr);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
				bin->header_info[bin->all_bin_parse_num - 1].
				valid_data_addr + 4 + 8 * bin_num + 60;
	} else {
		valid_data_len = bin->header_info[bin->all_bin_parse_num - 1].
								bin_data_len;
		bin->p_addr += (60 + valid_data_len);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
		    bin->header_info[bin->all_bin_parse_num - 1].valid_data_addr
		    + bin->header_info[bin->all_bin_parse_num - 1].bin_data_len
		    + 60;
	}

	ret = aw_parse_bin_header_1_0_0(bin);
	return ret;
}

/* Get the number of bins in multi bins, and set a for loop,
 * loop processing each bin data
 */
static int aw_get_multi_bin_header_1_0_0(struct aw_bin *bin)
{
	int i = 0;
	int ret = 0;
	unsigned int bin_num = 0;

	DBG("aw_bin_parse enter multi bin branch -- %s\n", __func__);
	bin_num = GET_32_DATA(*(bin->p_addr + 63), *(bin->p_addr + 62),
				*(bin->p_addr + 61), *(bin->p_addr + 60));
	if (bin->multi_bin_parse_num == 1)
		bin->header_info[bin->all_bin_parse_num].valid_data_addr = 60;
	aw_get_single_bin_header_1_0_0(bin);

	for (i = 0; i < bin_num; i++) {
		DBG("aw_bin_parse enter multi bin for is %d\n", i);
		ret = aw_parse_each_of_multi_bins_1_0_0(bin_num, i, bin);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/********************************************************
 *
 * If the bin framework header version is 1.0.0,
 * determine the data type of bin, and then perform different processing
 * according to the data type
 * If it is a single bin data type, write the data directly
 * into the structure array
 * If it is a multi-bin data type, first obtain the number of bins,
 * and then recursively call the bin frame header processing function
 * according to the bin number to process the frame header information
 * of each bin separately
 *
 ********************************************************/
int aw_parse_bin_header_1_0_0(struct aw_bin *bin)
{
	int ret = 0;
	unsigned int bin_data_type;

	DBG("enter %s\n", __func__);
	bin_data_type = GET_32_DATA(*(bin->p_addr + 11), *(bin->p_addr + 10),
					*(bin->p_addr + 9), *(bin->p_addr + 8));
	DBG("aw_bin_parse bin_data_type 0x%x\n", bin_data_type);
	switch (bin_data_type) {
	case DATA_TYPE_REGISTER:
	case DATA_TYPE_DSP_REG:
	case DATA_TYPE_SOC_APP:
		/* Divided into two processing methods,
		 * one is single bin processing,
		 * and the other is single bin processing in multi bin
		 */
		DBG("aw_bin_parse enter single bin branch\n");
		bin->single_bin_parse_num += 1;
		DBG("%s bin->single_bin_parse_num is %d\n", __func__,
						bin->single_bin_parse_num);
		if (!bin->multi_bin_parse_num) {
			bin->header_info[bin->all_bin_parse_num].
							valid_data_addr = 60;
		}
		aw_get_single_bin_header_1_0_0(bin);
		break;
	case DATA_TYPE_MULTI_BINS:
		/* Get the number of times to enter multi bins */
		DBG("aw_bin_parse enter multi bin branch\n");
		bin->multi_bin_parse_num += 1;
		DBG("%s bin->multi_bin_parse_num is %d\n", __func__,
						bin->multi_bin_parse_num);
		ret = aw_get_multi_bin_header_1_0_0(bin);
		if (ret < 0)
			return ret;
		break;
	}
	return 0;
}

/* get the bin's header version */
static int aw_check_bin_header_version(struct aw_bin *bin)
{
	int ret = 0;
	unsigned int header_version = 0;

	header_version = GET_32_DATA(*(bin->p_addr + 7), *(bin->p_addr + 6),
					*(bin->p_addr + 5), *(bin->p_addr + 4));
	DBG("aw_bin_parse header_version 0x%x\n", header_version);
	/* Write data to the corresponding structure array
	 * according to different formats of the bin frame header version
	 */
	switch (header_version) {
	case HEADER_VERSION_1_0_0:
		ret = aw_parse_bin_header_1_0_0(bin);
		return ret;
	default:
		DBG_ERR("aw_bin_parse Unrecognized this bin header version\n");
		return -BIN_HEADER_VER_ERR;
	}
}

int aw_parsing_bin_file(struct aw_bin *bin)
{
	int i = 0;
	int ret = 0;

	DBG("aw_bin_parse code version:%s\n", AWINIC_CODE_VERSION);
	if (!bin) {
		DBG_ERR("aw_bin_parse bin is NULL\n");
		return -BIN_IS_NULL;
	}
	bin->p_addr = bin->info.data;
	bin->all_bin_parse_num = 0;
	bin->multi_bin_parse_num = 0;
	bin->single_bin_parse_num = 0;

	/* filling bins header info */
	ret = aw_check_bin_header_version(bin);
	if (ret < 0) {
		DBG_ERR("aw_bin_parse check bin header version error\n");
		return ret;
	}
	bin->p_addr = NULL;

	/* check bin header info */
	for (i = 0; i < bin->all_bin_parse_num; i++) {
		/* check sum */
		ret = aw_check_sum(bin, i);
		if (ret < 0) {
			DBG_ERR("aw_bin_parse check sum data error\n");
			return ret;
		}
		/* check bin data version */
		ret = aw_check_data_version(bin, i);
		if (ret < 0) {
			DBG_ERR("aw_bin_parse check data version error\n");
			return ret;
		}
		/* check valid data */
		if (bin->header_info[i].bin_data_ver == DATA_VERSION_V1) {
			/* check register num */
			if (bin->header_info[i].bin_data_type ==
							DATA_TYPE_REGISTER) {
				ret = aw_check_register_num_v1(bin, i);
				if (ret < 0)
					return ret;
				/* check dsp reg num */
			} else if (bin->header_info[i].bin_data_type ==
							DATA_TYPE_DSP_REG) {
				ret = aw_check_dsp_reg_num_v1(bin, i);
				if (ret < 0)
					return ret;
				/* check soc app num */
			} else if (bin->header_info[i].bin_data_type ==
							DATA_TYPE_SOC_APP) {
				ret = aw_check_soc_app_num_v1(bin, i);
				if (ret < 0)
					return ret;
			} else {
				bin->header_info[i].valid_data_len = bin->
						header_info[i].bin_data_len;
			}
		}
	}
	DBG("aw_bin_parse parsing success\n");

	return 0;
}

int aw_dev_dsp_data_order(struct aw_device *aw_dev,
				uint8_t *data, uint32_t data_len)
{
	int i = 0;
	uint8_t tmp_val = 0;

	aw_dev_dbg(aw_dev->dev, "enter");

	if (data_len % 2 != 0) {
		aw_dev_dbg(aw_dev->dev, "data_len:%d unsupported", data_len);
		return -EINVAL;
	}

	for (i = 0; i < data_len; i += 2) {
		tmp_val = data[i];
		data[i] = data[i + 1];
		data[i + 1] = tmp_val;
	}

	return 0;
}

static int aw_dev_parse_raw_reg(struct aw_device *aw_dev,
		uint8_t *data, uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	aw_dev_info(aw_dev->dev, "data_size:%d enter", data_len);

	prof_desc->sec_desc[AW_DATA_TYPE_REG].data = data;
	prof_desc->sec_desc[AW_DATA_TYPE_REG].len = data_len;

	prof_desc->prof_st = AW_PROFILE_OK;

	return 0;
}

static int aw_dev_parse_raw_dsp_cfg(struct aw_device *aw_dev,
		uint8_t *data, uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	int ret;

	aw_dev_info(aw_dev->dev, "data_size:%d enter", data_len);

	ret = aw_dev_dsp_data_order(aw_dev, data, data_len);
	if (ret < 0)
		return ret;

	prof_desc->sec_desc[AW_DATA_TYPE_DSP_CFG].data = data;
	prof_desc->sec_desc[AW_DATA_TYPE_DSP_CFG].len = data_len;

	prof_desc->prof_st = AW_PROFILE_OK;

	return 0;
}

static int aw_dev_parse_raw_dsp_fw(struct aw_device *aw_dev,
		uint8_t *data, uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	int ret;

	aw_dev_info(aw_dev->dev, "data_size:%d enter", data_len);

	ret = aw_dev_dsp_data_order(aw_dev, data, data_len);
	if (ret < 0)
		return ret;

	prof_desc->sec_desc[AW_DATA_TYPE_DSP_FW].data = data;
	prof_desc->sec_desc[AW_DATA_TYPE_DSP_FW].len = data_len;

	prof_desc->prof_st = AW_PROFILE_OK;

	return 0;
}

static int aw_dev_prof_parse_multi_bin(struct aw_device *aw_dev,
		uint8_t *data, uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	struct aw_bin *aw_bin = NULL;
	int i;
	int ret;

	aw_bin = devm_kzalloc(aw_dev->dev, data_len + sizeof(struct aw_bin), GFP_KERNEL);
	if (aw_bin == NULL) {
		aw_dev_err(aw_dev->dev, "kzalloc aw_bin failed");
		return -ENOMEM;
	}

	aw_bin->info.len = data_len;
	memcpy(aw_bin->info.data, data, data_len);

	ret = aw_parsing_bin_file(aw_bin);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "parse bin failed");
		goto parse_bin_failed;
	}

	for (i = 0; i < aw_bin->all_bin_parse_num; i++) {
		if (aw_bin->header_info[i].bin_data_type == DATA_TYPE_REGISTER) {
			prof_desc->sec_desc[AW_DATA_TYPE_REG].len = aw_bin->header_info[i].valid_data_len;
			prof_desc->sec_desc[AW_DATA_TYPE_REG].data = data + aw_bin->header_info[i].valid_data_addr;
		} else if (aw_bin->header_info[i].bin_data_type == DATA_TYPE_DSP_REG) {
			ret = aw_dev_dsp_data_order(aw_dev, data + aw_bin->header_info[i].valid_data_addr,
						    aw_bin->header_info[i].valid_data_len);
			if (ret < 0)
				return ret;

			prof_desc->sec_desc[AW_DATA_TYPE_DSP_CFG].len = aw_bin->header_info[i].valid_data_len;
			prof_desc->sec_desc[AW_DATA_TYPE_DSP_CFG].data = data + aw_bin->header_info[i].valid_data_addr;
		} else if (aw_bin->header_info[i].bin_data_type == DATA_TYPE_DSP_FW) {
			ret = aw_dev_dsp_data_order(aw_dev, data + aw_bin->header_info[i].valid_data_addr,
						    aw_bin->header_info[i].valid_data_len);
			if (ret < 0)
				return ret;

			prof_desc->fw_ver = aw_bin->header_info[i].app_version;
			prof_desc->sec_desc[AW_DATA_TYPE_DSP_FW].len = aw_bin->header_info[i].valid_data_len;
			prof_desc->sec_desc[AW_DATA_TYPE_DSP_FW].data = data + aw_bin->header_info[i].valid_data_addr;
		}
	}
	devm_kfree(aw_dev->dev, aw_bin);
	aw_bin = NULL;
	prof_desc->prof_st = AW_PROFILE_OK;
	return 0;

parse_bin_failed:
	devm_kfree(aw_dev->dev, aw_bin);
	aw_bin = NULL;
	return ret;
}

static int aw_dev_parse_data_by_sec_type(struct aw_device *aw_dev, struct aw_cfg_hdr *cfg_hdr,
			struct aw_cfg_dde *cfg_dde, struct aw_prof_desc *scene_prof_desc)
{

	switch (cfg_dde->data_type) {
	case ACF_SEC_TYPE_REG:
		return aw_dev_parse_raw_reg(aw_dev,
				(uint8_t *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_DSP_CFG:
		return aw_dev_parse_raw_dsp_cfg(aw_dev,
				(uint8_t *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_DSP_FW:
		return aw_dev_parse_raw_dsp_fw(aw_dev,
				(uint8_t *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_MUTLBIN:
		return aw_dev_prof_parse_multi_bin(aw_dev,
				(uint8_t *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	}
	return 0;
}

static int aw_dev_parse_dev_type(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr, struct aw_all_prof_info *all_prof_info)
{
	int i = 0;
	int ret;
	int sec_num = 0;
	struct aw_cfg_dde *cfg_dde =
		(struct aw_cfg_dde *)((char *)prof_hdr + prof_hdr->a_hdr_offset);

	aw_dev_info(aw_dev->dev, "enter");

	for (i = 0; i < prof_hdr->a_ddt_num; i++) {
		if ((aw_dev->i2c->adapter->nr == cfg_dde[i].dev_bus) &&
			(aw_dev->i2c->addr == cfg_dde[i].dev_addr) &&
			(cfg_dde[i].type == AW_DEV_TYPE_ID)) {
			if (cfg_dde[i].data_type == ACF_SEC_TYPE_MONITOR) {
				ret = aw_monitor_parse_fw(&aw_dev->monitor_desc,
						(uint8_t *)prof_hdr + cfg_dde[i].data_offset,
						cfg_dde[i].data_size);
				if (ret < 0) {
					aw_dev_err(aw_dev->dev, "parse monitor failed");
					return ret;
					}
			} else {
				if (cfg_dde[i].dev_profile >= AW_PROFILE_MAX) {
					aw_dev_err(aw_dev->dev, "dev_profile [%d] overflow",
						cfg_dde[i].dev_profile);
					return -EINVAL;
				}
				ret = aw_dev_parse_data_by_sec_type(aw_dev, prof_hdr, &cfg_dde[i],
					&all_prof_info->prof_desc[cfg_dde[i].dev_profile]);
				if (ret < 0) {
					aw_dev_err(aw_dev->dev, "parse failed");
					return ret;
				}
				sec_num++;
			}
		}
	}

	if (sec_num == 0) {
		aw_dev_info(aw_dev->dev, "get dev type num is %d, please use default",
					sec_num);
		return AW_DEV_TYPE_NONE;
	}

	return AW_DEV_TYPE_OK;
}

static int aw_dev_parse_dev_default_type(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr, struct aw_all_prof_info *all_prof_info)
{
	int i = 0;
	int ret;
	int sec_num = 0;
	struct aw_cfg_dde *cfg_dde =
		(struct aw_cfg_dde *)((char *)prof_hdr + prof_hdr->a_hdr_offset);

	aw_dev_info(aw_dev->dev, "enter");

	for (i = 0; i < prof_hdr->a_ddt_num; i++) {
		if ((aw_dev->channel == cfg_dde[i].dev_index) &&
			(cfg_dde[i].type == AW_DEV_DEFAULT_TYPE_ID)) {
			if (cfg_dde[i].data_type == ACF_SEC_TYPE_MONITOR) {
				ret = aw_monitor_parse_fw(&aw_dev->monitor_desc,
						(uint8_t *)prof_hdr + cfg_dde[i].data_offset,
						cfg_dde[i].data_size);
				if (ret < 0) {
						aw_dev_err(aw_dev->dev, "parse monitor failed");
						return ret;
					}
			} else {
				if (cfg_dde[i].dev_profile >= AW_PROFILE_MAX) {
					aw_dev_err(aw_dev->dev, "dev_profile [%d] overflow",
						cfg_dde[i].dev_profile);
					return -EINVAL;
				}
				ret = aw_dev_parse_data_by_sec_type(aw_dev, prof_hdr, &cfg_dde[i],
					&all_prof_info->prof_desc[cfg_dde[i].dev_profile]);
				if (ret < 0) {
					aw_dev_err(aw_dev->dev, "parse failed");
					return ret;
				}
				sec_num++;
			}
		}
	}

	if (sec_num == 0) {
		aw_dev_err(aw_dev->dev, "get dev default type failed, get num[%d]", sec_num);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_cfg_get_vaild_prof(struct aw_device *aw_dev,
				struct aw_all_prof_info all_prof_info)
{
	int i;
	int num = 0;
	struct aw_sec_data_desc *sec_desc = NULL;
	struct aw_prof_desc *prof_desc = all_prof_info.prof_desc;
	struct aw_prof_info *prof_info = &aw_dev->prof_info;

	for (i = 0; i < AW_PROFILE_MAX; i++) {
		if (prof_desc[i].prof_st == AW_PROFILE_OK) {
			sec_desc = prof_desc[i].sec_desc;
			if ((sec_desc[AW_DATA_TYPE_REG].data != NULL) &&
				(sec_desc[AW_DATA_TYPE_REG].len != 0) &&
				(sec_desc[AW_DATA_TYPE_DSP_CFG].data != NULL) &&
				(sec_desc[AW_DATA_TYPE_DSP_CFG].len != 0) &&
				(sec_desc[AW_DATA_TYPE_DSP_FW].data != NULL) &&
				(sec_desc[AW_DATA_TYPE_DSP_FW].len != 0)) {
				prof_info->count++;
			}
		}
	}

	aw_dev_info(aw_dev->dev, "get vaild profile:%d", aw_dev->prof_info.count);

	if (!prof_info->count) {
		aw_dev_err(aw_dev->dev, "no profile data");
		return -EPERM;
	}

	prof_info->prof_desc = devm_kzalloc(aw_dev->dev,
					prof_info->count * sizeof(struct aw_prof_desc),
					GFP_KERNEL);
	if (prof_info->prof_desc == NULL) {
		aw_dev_err(aw_dev->dev, "prof_desc kzalloc failed");
		return -ENOMEM;
	}

	for (i = 0; i < AW_PROFILE_MAX; i++) {
		if (prof_desc[i].prof_st == AW_PROFILE_OK) {
			sec_desc = prof_desc[i].sec_desc;
			if ((sec_desc[AW_DATA_TYPE_REG].data != NULL) &&
				(sec_desc[AW_DATA_TYPE_REG].len != 0) &&
				(sec_desc[AW_DATA_TYPE_DSP_CFG].data != NULL) &&
				(sec_desc[AW_DATA_TYPE_DSP_CFG].len != 0) &&
				(sec_desc[AW_DATA_TYPE_DSP_FW].data != NULL) &&
				(sec_desc[AW_DATA_TYPE_DSP_FW].len != 0)) {
				if (num >= prof_info->count) {
					aw_dev_err(aw_dev->dev, "get scene num[%d] overflow count[%d]",
						num, prof_info->count);
					return -ENOMEM;
				}
				prof_info->prof_desc[num] = prof_desc[i];
				prof_info->prof_desc[num].id = i;
				num++;
			}
		}
	}

	return 0;
}

static int aw_dev_load_cfg_by_hdr(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr)
{
	int ret;
	struct aw_all_prof_info all_prof_info;

	memset(&all_prof_info, 0, sizeof(struct aw_all_prof_info));

	ret = aw_dev_parse_dev_type(aw_dev, prof_hdr, &all_prof_info);
	if (ret < 0) {
		return ret;
	} else if (ret == AW_DEV_TYPE_NONE) {
		aw_dev_info(aw_dev->dev, "get dev type num is 0, parse default dev");
		ret = aw_dev_parse_dev_default_type(aw_dev, prof_hdr, &all_prof_info);
		if (ret < 0)
			return ret;
	}

	ret = aw_dev_cfg_get_vaild_prof(aw_dev, all_prof_info);
	if (ret < 0)
			return ret;

	aw_dev->prof_info.prof_name_list = profile_name;

	return 0;
}

static int aw_dev_create_prof_name_list_v_1_0_0_0(struct aw_device *aw_dev)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_prof_desc *prof_desc = prof_info->prof_desc;
	int i;

	if (prof_desc == NULL) {
		aw_dev_err(aw_dev->dev, "prof_desc is NULL");
		return -EINVAL;
	}

	prof_info->prof_name_list = devm_kzalloc(aw_dev->dev,
					prof_info->count * PROFILE_STR_MAX,
					GFP_KERNEL);
	if (prof_info->prof_name_list == NULL) {
		aw_dev_err(aw_dev->dev, "prof_name_list devm_kzalloc failed");
		return -ENOMEM;
	}

	for (i = 0; i < prof_info->count; i++) {
		prof_desc[i].id = i;
		prof_info->prof_name_list[i] = prof_desc[i].prf_str;
		aw_dev_info(aw_dev->dev, "prof name is %s", prof_info->prof_name_list[i]);
	}

	return 0;
}

static int aw_get_dde_type_info(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	int i;
	int dev_num = 0;
	int default_num = 0;
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_cfg_dde_v_1_0_0_0 *cfg_dde =
		(struct aw_cfg_dde_v_1_0_0_0 *)(aw_cfg->data + cfg_hdr->a_hdr_offset);

	for (i = 0; i < cfg_hdr->a_ddt_num; i++) {
		if (cfg_dde[i].type == AW_DEV_TYPE_ID)
			dev_num++;

		if (cfg_dde[i].type == AW_DEV_DEFAULT_TYPE_ID)
			default_num++;
	}

	if (!(dev_num || default_num)) {
		aw_dev_err(aw_dev->dev, "can't find scene");
		return -EINVAL;
	}

	if (dev_num != 0) {
		aw_dev->prof_info.prof_type = AW_DEV_TYPE_ID;
	} else {
		aw_dev->prof_info.prof_type = AW_DEV_DEFAULT_TYPE_ID;
	}

	return 0;
}

static int aw_get_dev_scene_count_v_1_0_0_0(struct aw_device *aw_dev,
					    struct aw_container *aw_cfg,
					    uint32_t *scene_num)
{
	int i;
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_cfg_dde_v_1_0_0_0 *cfg_dde =
		(struct aw_cfg_dde_v_1_0_0_0 *)(aw_cfg->data + cfg_hdr->a_hdr_offset);

	for (i = 0; i < cfg_hdr->a_ddt_num; ++i) {
		if ((cfg_dde[i].data_type == ACF_SEC_TYPE_MUTLBIN) &&
			(aw_dev->chip_id == cfg_dde[i].chip_id) &&
			((aw_dev->i2c->adapter->nr == cfg_dde[i].dev_bus) &&
			(aw_dev->i2c->addr == cfg_dde[i].dev_addr)))
				(*scene_num)++;
	}

	return 0;
}

static int aw_get_default_scene_count_v_1_0_0_0(struct aw_device *aw_dev,
						struct aw_container *aw_cfg,
						uint32_t *scene_num)
{
	int i;
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_cfg_dde_v_1_0_0_0 *cfg_dde =
		(struct aw_cfg_dde_v_1_0_0_0 *)(aw_cfg->data + cfg_hdr->a_hdr_offset);

	for (i = 0; i < cfg_hdr->a_ddt_num; ++i) {
		if ((cfg_dde[i].data_type == ACF_SEC_TYPE_MUTLBIN) &&
			(aw_dev->chip_id == cfg_dde[i].chip_id) &&
			(aw_dev->channel == cfg_dde[i].dev_index))
				(*scene_num)++;
	}

	return 0;
}

static int aw_dev_parse_scene_count_v_1_0_0_0(struct aw_device *aw_dev,
					      struct aw_container *aw_cfg,
					      uint32_t *count)
{
	int ret;

	ret = aw_get_dde_type_info(aw_dev, aw_cfg);
	if (ret < 0)
		return ret;

	if (aw_dev->prof_info.prof_type == AW_DEV_TYPE_ID) {
		aw_get_dev_scene_count_v_1_0_0_0(aw_dev, aw_cfg, count);
	} else if (aw_dev->prof_info.prof_type == AW_DEV_DEFAULT_TYPE_ID) {
		aw_get_default_scene_count_v_1_0_0_0(aw_dev, aw_cfg, count);
	} else {
		aw_dev_err(aw_dev->dev, "unsupported prof_type[%x]",
			aw_dev->prof_info.prof_type);
		return -EINVAL;
	}

	aw_dev_info(aw_dev->dev, "scene count is %d", (*count));
	return 0;
}

static int aw_dev_parse_data_by_sec_type_v_1_0_0_0(struct aw_device *aw_dev,
						   struct aw_cfg_hdr *prof_hdr,
						   struct aw_cfg_dde_v_1_0_0_0 *cfg_dde,
						   int *cur_scene_id)
{
	int ret;
	struct aw_prof_info *prof_info = &aw_dev->prof_info;

	switch (cfg_dde->data_type) {
	case ACF_SEC_TYPE_MUTLBIN:
		ret = aw_dev_prof_parse_multi_bin(aw_dev,
					(uint8_t *)prof_hdr + cfg_dde->data_offset,
					cfg_dde->data_size, &prof_info->prof_desc[*cur_scene_id]);
		if (ret < 0) {
			aw_dev_err(aw_dev->dev, "parse multi bin failed");
			return ret;
		}
		prof_info->prof_desc[*cur_scene_id].prf_str = cfg_dde->dev_profile_str;
		prof_info->prof_desc[*cur_scene_id].id = cfg_dde->dev_profile;
		(*cur_scene_id)++;
		break;
	case ACF_SEC_TYPE_MONITOR:
		return aw_monitor_parse_fw(&aw_dev->monitor_desc,
						(uint8_t *)prof_hdr + cfg_dde->data_offset,
						cfg_dde->data_size);
	default:
		aw_pr_err("unsupported SEC_TYPE [%d]", cfg_dde->data_type);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_dev_type_v_1_0_0_0(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr)
{
	int i = 0;
	int ret;
	int cur_scene_id = 0;
	struct aw_cfg_dde_v_1_0_0_0 *cfg_dde =
		(struct aw_cfg_dde_v_1_0_0_0 *)((char *)prof_hdr + prof_hdr->a_hdr_offset);

	aw_dev_info(aw_dev->dev, "enter");

	for (i = 0; i < prof_hdr->a_ddt_num; i++) {
		if ((aw_dev->i2c->adapter->nr == cfg_dde[i].dev_bus) &&
			(aw_dev->i2c->addr == cfg_dde[i].dev_addr) &&
			(aw_dev->chip_id == cfg_dde[i].chip_id)) {
			ret = aw_dev_parse_data_by_sec_type_v_1_0_0_0(aw_dev, prof_hdr,
							&cfg_dde[i], &cur_scene_id);
			if (ret < 0) {
				aw_dev_err(aw_dev->dev, "parse failed");
				return ret;
			}
		}
	}

	if (cur_scene_id == 0) {
		aw_dev_info(aw_dev->dev, "get dev type failed, get num [%d]", cur_scene_id);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_default_type_v_1_0_0_0(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr)
{
	int i = 0;
	int ret;
	int cur_scene_id = 0;
	struct aw_cfg_dde_v_1_0_0_0 *cfg_dde =
		(struct aw_cfg_dde_v_1_0_0_0 *)((char *)prof_hdr + prof_hdr->a_hdr_offset);

	aw_dev_info(aw_dev->dev, "enter");

	for (i = 0; i < prof_hdr->a_ddt_num; i++) {
		if ((aw_dev->channel == cfg_dde[i].dev_index) &&
			(aw_dev->chip_id == cfg_dde[i].chip_id)) {
			ret = aw_dev_parse_data_by_sec_type_v_1_0_0_0(aw_dev, prof_hdr,
							&cfg_dde[i], &cur_scene_id);
			if (ret < 0) {
				aw_dev_err(aw_dev->dev, "parse failed");
				return ret;
			}
		}
	}

	if (cur_scene_id == 0) {
		aw_dev_err(aw_dev->dev, "get dev default type failed, get num[%d]", cur_scene_id);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_by_hdr_v_1_0_0_0(struct aw_device *aw_dev,
		struct aw_cfg_hdr *cfg_hdr)
{
	int ret;

	if (aw_dev->prof_info.prof_type == AW_DEV_TYPE_ID) {
		ret = aw_dev_parse_dev_type_v_1_0_0_0(aw_dev, cfg_hdr);
		if (ret < 0)
			return ret;
	} else if (aw_dev->prof_info.prof_type == AW_DEV_DEFAULT_TYPE_ID) {
		ret = aw_dev_parse_default_type_v_1_0_0_0(aw_dev, cfg_hdr);
		if (ret < 0)
			return ret;
	} else {
		aw_dev_err(aw_dev->dev, "prof type matched failed, get num[%d]",
			aw_dev->prof_info.prof_type);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_load_cfg_by_hdr_v_1_0_0_0(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	int ret;

	ret = aw_dev_parse_scene_count_v_1_0_0_0(aw_dev, aw_cfg, &prof_info->count);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "get scene count failed");
		return ret;
	}

	prof_info->prof_desc = devm_kzalloc(aw_dev->dev,
					prof_info->count * sizeof(struct aw_prof_desc),
					GFP_KERNEL);
	if (prof_info->prof_desc == NULL) {
		aw_dev_err(aw_dev->dev, "prof_desc devm_kzalloc failed");
		return -ENOMEM;
	}

	ret = aw_dev_parse_by_hdr_v_1_0_0_0(aw_dev, cfg_hdr);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, " failed");
		return ret;
	}

	ret = aw_dev_create_prof_name_list_v_1_0_0_0(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "create prof name list failed");
		return ret;
	}

	return 0;
}

int aw_dev_cfg_load(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr = NULL;
	int ret;

	aw_dev_info(aw_dev->dev, "enter");

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	switch (cfg_hdr->a_hdr_version) {
	case AW_CFG_HDR_VER_0_0_0_1:
		ret = aw_dev_load_cfg_by_hdr(aw_dev, cfg_hdr);
		if (ret < 0) {
			aw_dev_err(aw_dev->dev, "hdr_cersion[0x%x] parse failed",
						cfg_hdr->a_hdr_version);
			return ret;
		}
		break;
	case AW_CFG_HDR_VER_1_0_0_0:
		ret = aw_dev_load_cfg_by_hdr_v_1_0_0_0(aw_dev, aw_cfg);
		if (ret < 0) {
			aw_dev_err(aw_dev->dev, "hdr_cersion[0x%x] parse failed",
						cfg_hdr->a_hdr_version);
			return ret;
		}
		break;
	default:
		aw_pr_err("unsupported hdr_version [0x%x]", cfg_hdr->a_hdr_version);
		return -EINVAL;
	}

	aw_dev->fw_status = AW_DEV_FW_OK;
	aw_dev_info(aw_dev->dev, "parse cfg success");
	return 0;
}

static uint8_t aw_dev_crc8_check(unsigned char *data, uint32_t data_size)
{
	uint8_t crc_value = 0x00;
	uint8_t pdatabuf = 0;
	int i;

	while (data_size--) {
		pdatabuf = *data++;
		for (i = 0; i < 8; i++) {
			/*if the lowest bit is 1*/
			if ((crc_value ^ (pdatabuf)) & 0x01) {
				/*Xor multinomial*/
				crc_value ^= 0x18;
				crc_value >>= 1;
				crc_value |= 0x80;
			} else {
				crc_value >>= 1;
			}
			pdatabuf >>= 1;
		}
	}
	return crc_value;
}

static int aw_dev_check_cfg_by_hdr(struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr = NULL;
	struct aw_cfg_dde *cfg_dde = NULL;
	unsigned int end_data_offset = 0;
	unsigned int act_data = 0;
	unsigned int hdr_ddt_len = 0;
	uint8_t act_crc8 = 0;
	int i;

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;

	/*check file type id is awinic acf file*/
	if (cfg_hdr->a_id != ACF_FILE_ID) {
		aw_pr_err("not acf type file");
		return -EINVAL;
	}

	hdr_ddt_len = cfg_hdr->a_hdr_offset + cfg_hdr->a_ddt_size;
	if (hdr_ddt_len > aw_cfg->len) {
		aw_pr_err("hdrlen with ddt_len [%d] overflow file size[%d]",
		cfg_hdr->a_hdr_offset, aw_cfg->len);
		return -EINVAL;
	}

	/*check data size*/
	cfg_dde = (struct aw_cfg_dde *)((char *)aw_cfg->data + cfg_hdr->a_hdr_offset);
	act_data += hdr_ddt_len;
	for (i = 0; i < cfg_hdr->a_ddt_num; i++)
		act_data += cfg_dde[i].data_size;

	if (act_data != aw_cfg->len) {
		aw_pr_err("act_data[%d] not equal to file size[%d]!",
			act_data, aw_cfg->len);
		return -EINVAL;
	}

	for (i = 0; i < cfg_hdr->a_ddt_num; i++) {
		/* data check */
		end_data_offset = cfg_dde[i].data_offset + cfg_dde[i].data_size;
		if (end_data_offset > aw_cfg->len) {
			aw_pr_err("a_ddt_num[%d] end_data_offset[%d] overflow file size[%d]",
				i, end_data_offset, aw_cfg->len);
			return -EINVAL;
		}

		/* crc check */
		act_crc8 = aw_dev_crc8_check(aw_cfg->data + cfg_dde[i].data_offset, cfg_dde[i].data_size);
		if (act_crc8 != cfg_dde[i].data_crc) {
			aw_pr_err("a_ddt_num[%d] crc8 check failed, act_crc8:0x%x != data_crc 0x%x",
				i, (uint32_t)act_crc8, cfg_dde[i].data_crc);
			return -EINVAL;
		}
	}

	aw_pr_info("project name [%s]", cfg_hdr->a_project);
	aw_pr_info("custom name [%s]", cfg_hdr->a_custom);
	aw_pr_info("version name [%d.%d.%d.%d]", cfg_hdr->a_version[3], cfg_hdr->a_version[2],
						cfg_hdr->a_version[1], cfg_hdr->a_version[0]);
	aw_pr_info("author id %d", cfg_hdr->a_author_id);

	return 0;
}

static int aw_dev_check_acf_by_hdr_v_1_0_0_0(struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr = NULL;
	struct aw_cfg_dde_v_1_0_0_0 *cfg_dde = NULL;
	unsigned int end_data_offset = 0;
	unsigned int act_data = 0;
	unsigned int hdr_ddt_len = 0;
	uint8_t act_crc8 = 0;
	int i;

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;

	/*check file type id is awinic acf file*/
	if (cfg_hdr->a_id != ACF_FILE_ID) {
		aw_pr_err("not acf type file");
		return -EINVAL;
	}

	hdr_ddt_len = cfg_hdr->a_hdr_offset + cfg_hdr->a_ddt_size;
	if (hdr_ddt_len > aw_cfg->len) {
		aw_pr_err("hdrlen with ddt_len [%d] overflow file size[%d]",
		cfg_hdr->a_hdr_offset, aw_cfg->len);
		return -EINVAL;
	}

	/*check data size*/
	cfg_dde = (struct aw_cfg_dde_v_1_0_0_0 *)((char *)aw_cfg->data + cfg_hdr->a_hdr_offset);
	act_data += hdr_ddt_len;
	for (i = 0; i < cfg_hdr->a_ddt_num; i++)
		act_data += cfg_dde[i].data_size;

	if (act_data != aw_cfg->len) {
		aw_pr_err("act_data[%d] not equal to file size[%d]!",
			act_data, aw_cfg->len);
		return -EINVAL;
	}

	for (i = 0; i < cfg_hdr->a_ddt_num; i++) {
		/* data check */
		end_data_offset = cfg_dde[i].data_offset + cfg_dde[i].data_size;
		if (end_data_offset > aw_cfg->len) {
			aw_pr_err("a_ddt_num[%d] end_data_offset[%d] overflow file size[%d]",
				i, end_data_offset, aw_cfg->len);
			return -EINVAL;
		}

		/* crc check */
		act_crc8 = aw_dev_crc8_check(aw_cfg->data + cfg_dde[i].data_offset, cfg_dde[i].data_size);
		if (act_crc8 != cfg_dde[i].data_crc) {
			aw_pr_err("a_ddt_num[%d] crc8 check failed, act_crc8:0x%x != data_crc 0x%x",
				i, (uint32_t)act_crc8, cfg_dde[i].data_crc);
			return -EINVAL;
		}
	}

	aw_pr_info("project name [%s]", cfg_hdr->a_project);
	aw_pr_info("custom name [%s]", cfg_hdr->a_custom);
	aw_pr_info("version name [%d.%d.%d.%d]", cfg_hdr->a_version[3], cfg_hdr->a_version[2],
						cfg_hdr->a_version[1], cfg_hdr->a_version[0]);
	aw_pr_info("author id %d", cfg_hdr->a_author_id);

	return 0;

}

int aw_dev_load_acf_check(struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr = NULL;

	if (aw_cfg == NULL) {
		aw_pr_err("aw_prof is NULL");
		return -ENOMEM;
	}

	if (aw_cfg->len < sizeof(struct aw_cfg_hdr)) {
		aw_pr_err("cfg hdr size[%d] overflow file size[%d]",
			aw_cfg->len, (int)sizeof(struct aw_cfg_hdr));
		return -EINVAL;
	}

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	switch (cfg_hdr->a_hdr_version) {
	case AW_CFG_HDR_VER_0_0_0_1:
		return aw_dev_check_cfg_by_hdr(aw_cfg);
	case AW_CFG_HDR_VER_1_0_0_0:
		return aw_dev_check_acf_by_hdr_v_1_0_0_0(aw_cfg);
	default:
		aw_pr_err("unsupported hdr_version [0x%x]", cfg_hdr->a_hdr_version);
		return -EINVAL;
	}

	return 0;
}

int aw_dev_get_profile_count(struct aw_device *aw_dev)
{
	if (aw_dev == NULL) {
		aw_pr_err("aw_dev is NULL");
		return -ENOMEM;
	}

	return aw_dev->prof_info.count;
}

int aw_dev_check_profile_index(struct aw_device *aw_dev, int index)
{
	if ((index >= aw_dev->prof_info.count) || (index < 0))
		return -EINVAL;
	else
		return 0;
}

int aw_dev_get_profile_index(struct aw_device *aw_dev)
{
	return aw_dev->set_prof;
}

int aw_dev_set_profile_index(struct aw_device *aw_dev, int index)
{
	struct aw_prof_desc *prof_desc = NULL;

	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		return -EINVAL;
	} else {
		aw_dev->set_prof = index;
		prof_desc = &aw_dev->prof_info.prof_desc[index];

		aw_dev_info(aw_dev->dev, "set prof[%s]",
			aw_dev->prof_info.prof_name_list[prof_desc->id]);
	}

	return 0;
}

char *aw_dev_get_prof_name(struct aw_device *aw_dev, int index)
{
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_prof_info *prof_info = &aw_dev->prof_info;

	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		aw_dev_err(aw_dev->dev, "index[%d] overflow count[%d]",
			index, aw_dev->prof_info.count);
		return NULL;
	}

	prof_desc = &aw_dev->prof_info.prof_desc[index];

	return prof_info->prof_name_list[prof_desc->id];
}

int aw_dev_get_prof_data(struct aw_device *aw_dev, int index,
			struct aw_prof_desc **prof_desc)
{
	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		aw_dev_err(aw_dev->dev, "%s: index[%d] overflow count[%d]\n",
			__func__, index, aw_dev->prof_info.count);
		return -EINVAL;
	}

	*prof_desc = &aw_dev->prof_info.prof_desc[index];

	return 0;
}

