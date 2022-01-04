/*
 * aw_acf_bin.c
 *
 * Copyright (c) 2021 AWINIC Technology CO., LTD
 *
 * Author: Barry <zhaozhongbo@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include "aw87xxx.h"
#include "aw_acf_bin.h"
#include "aw_monitor.h"
#include "aw_log.h"
#include "aw_bin_parse.h"

static char *g_profile_name[] = {"Music", "Voice", "Voip",
		"Ringtone", "Ringtone_hs", "Lowpower", "Bypass", "Mmi",
		"Fm", "Notification", "Receiver", "Off"};

static char *g_power_off_name[] = {"Off", "OFF", "off", "oFF", "power_down"};

static char *aw_get_prof_name(int profile)
{
	if (profile < 0 || profile >= AW_PROFILE_MAX)
		return "NULL";
	else
		return g_profile_name[profile];
}

/*************************************************************************
 *
 *acf check
 *
 *************************************************************************/
static int aw_crc8_check(const unsigned char *data, unsigned int data_size)

{
	unsigned char crc_value = 0x00;
	unsigned char *pdata;
	int i;
	unsigned char pdatabuf = 0;

	pdata = (unsigned char *)data;

	while (data_size--) {
		pdatabuf = *pdata++;
		for (i = 0; i < 8; i++) {
			if ((crc_value ^ (pdatabuf)) & 0x01) {
				crc_value ^= 0x18;
				crc_value >>= 1;
				crc_value |= 0x80;
			} else {
				crc_value >>= 1;
			}
			pdatabuf >>= 1;
		}
	}

	return (int)crc_value;
}

static int aw_check_file_id(struct device *dev,
		char *fw_data, int32_t file_id)
{
	int32_t *acf_file_id = NULL;

	acf_file_id = (int32_t *)fw_data;
	if (*acf_file_id != file_id) {
		AW_DEV_LOGE(dev, "file id [%x] check failed", *acf_file_id);
		return -ENFILE;
	}

	return 0;
}

static int aw_check_header_size(struct device *dev,
			char *fw_data, size_t fw_size)
{
	if (fw_size < sizeof(struct aw_acf_hdr)) {
		AW_DEV_LOGE(dev, "acf size check failed,size less-than aw_acf_hdr");
		return -ENOEXEC;
	}

	return 0;
}

/***************************************************************************
 * V0.0.0.1 version acf check
 **************************************************************************/
static int aw_check_ddt_size_v_0_0_0_1(struct device *dev, char *fw_data)
{
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)fw_data;
	struct aw_acf_dde *acf_dde = NULL;

	acf_dde = (struct aw_acf_dde *)(fw_data + acf_hdr->ddt_offset);

	/* check ddt_size in acf_header is aqual to ddt_num multiply by dde_size */
	if (acf_hdr->ddt_size != acf_hdr->dde_num * sizeof(struct aw_acf_dde)) {
		AW_DEV_LOGE(dev, "acf ddt size check failed");
		return -EINVAL;
	}

	return 0;
}

static int aw_check_data_size_v_0_0_0_1(struct device *dev,
		char *fw_data, size_t fw_size)
{
	int i = 0;
	size_t data_size = 0;
	struct aw_acf_hdr *acf_hdr = NULL;
	struct aw_acf_dde *acf_dde = NULL;

	acf_hdr = (struct aw_acf_hdr *)fw_data;
	acf_dde = (struct aw_acf_dde *)(fw_data + acf_hdr->ddt_offset);

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (acf_dde[i].data_size % 2) {
			AW_DEV_LOGE(dev, "acf dde[%d].data_size[%d],dev_name[%s],data_type[%d], data_size check failed",
				i, acf_dde[i].data_size, acf_dde[i].dev_name,
				acf_dde[i].data_type);
			return -EINVAL;
		}
		data_size += acf_dde[i].data_size;
	}

	/* Verify that the file size is equal to the header size plus */
	/* the table size and data size */
	if (fw_size != data_size + sizeof(struct aw_acf_hdr) + acf_hdr->ddt_size) {
		AW_DEV_LOGE(dev, "acf size check failed");
		AW_DEV_LOGE(dev, "fw_size=%ld,hdr_size and ddt size and data size =%ld",
			(u_long)fw_size, (u_long)(data_size + sizeof(struct aw_acf_hdr) +
			acf_hdr->ddt_size));
		return -EINVAL;
	}

	return 0;
}

static int aw_check_data_crc_v_0_0_0_1(struct device *dev, char *fw_data)
{
	int i = 0;
	size_t crc_val = 0;
	char *data = NULL;
	struct aw_acf_hdr *acf_hdr = NULL;
	struct aw_acf_dde *acf_dde = NULL;

	acf_hdr = (struct aw_acf_hdr *)fw_data;
	acf_dde = (struct aw_acf_dde *)(fw_data + acf_hdr->ddt_offset);

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		data = fw_data + acf_dde[i].data_offset;
		crc_val = aw_crc8_check(data, acf_dde[i].data_size);
		if (crc_val != acf_dde[i].data_crc) {
			AW_DEV_LOGE(dev, "acf dde_crc check failed");
			return -EINVAL;
		}
	}

	return 0;
}

static int aw_check_profile_id_v_0_0_0_1(struct device *dev, char *fw_data)
{
	int i = 0;
	struct aw_acf_hdr *acf_hdr = NULL;
	struct aw_acf_dde *acf_dde = NULL;

	acf_hdr = (struct aw_acf_hdr *)fw_data;
	acf_dde = (struct aw_acf_dde *)(fw_data + acf_hdr->ddt_offset);

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (acf_dde[i].data_type == AW_MONITOR)
			continue;
		if (acf_dde[i].dev_profile > AW_PROFILE_MAX) {
			AW_DEV_LOGE(dev, "parse profile_id[%d] failed", acf_dde[i].dev_profile);
			return -EINVAL;
		}
	}

	return 0;
}
static int aw_check_data_v_0_0_0_1(struct device *dev,
			char *fw_data, size_t size)
{
	int ret = -1;

	/* check file type id is awinic acf file */
	ret = aw_check_file_id(dev, fw_data, AW_ACF_FILE_ID);
	if (ret < 0)
		return ret;

	/* check ddt_size in header is equal to all ddt aize */
	ret = aw_check_ddt_size_v_0_0_0_1(dev, fw_data);
	if (ret < 0)
		return ret;

	/* Verify that the file size is equal to the header size plus */
	/* the table size and data size */
	ret = aw_check_data_size_v_0_0_0_1(dev, fw_data, size);
	if (ret < 0)
		return ret;

	/* check crc in is equal to dde data crc */
	ret = aw_check_data_crc_v_0_0_0_1(dev, fw_data);
	if (ret < 0)
		return ret;

	/* check profile id is in profile_id_max */
	ret = aw_check_profile_id_v_0_0_0_1(dev, fw_data);
	if (ret < 0)
		return ret;

	AW_DEV_LOGI(dev, "acf fimware check succeed");

	return 0;
}

/***************************************************************************
 * V1.0.0.0 version acf chack
 **************************************************************************/
static int aw_check_ddt_size_v_1_0_0_0(struct device *dev, char *fw_data)
{
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)fw_data;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde = NULL;

	acf_dde = (struct aw_acf_dde_v_1_0_0_0 *)(fw_data + acf_hdr->ddt_offset);

	/* check ddt_size in acf_header is aqual to ddt_num multiply by dde_size */
	if (acf_hdr->ddt_size != acf_hdr->dde_num * sizeof(struct aw_acf_dde_v_1_0_0_0)) {
		AW_DEV_LOGE(dev, "acf ddt size check failed");
		return -EINVAL;
	}

	return 0;
}

static int aw_check_data_size_v_1_0_0_0(struct device *dev,
		char *fw_data, size_t fw_size)
{
	int i = 0;
	size_t data_size = 0;
	struct aw_acf_hdr *acf_hdr = NULL;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde = NULL;

	acf_hdr = (struct aw_acf_hdr *)fw_data;
	acf_dde = (struct aw_acf_dde_v_1_0_0_0 *)(fw_data + acf_hdr->ddt_offset);

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (acf_dde[i].data_size % 2) {
			AW_DEV_LOGE(dev, "acf dde[%d].data_size[%d],dev_name[%s],data_type[%d], data_size check failed",
				i, acf_dde[i].data_size, acf_dde[i].dev_name,
				acf_dde[i].data_type);
			return -EINVAL;
		}
		data_size += acf_dde[i].data_size;
	}

	/* Verify that the file size is equal to the header size plus */
	/* the table size and data size */
	if (fw_size != data_size + sizeof(struct aw_acf_hdr) + acf_hdr->ddt_size) {
		AW_DEV_LOGE(dev, "acf size check failed");
		AW_DEV_LOGE(dev, "fw_size=%ld,hdr_size and ddt size and data size =%ld",
			(u_long)fw_size, (u_long)(data_size + sizeof(struct aw_acf_hdr) +
			acf_hdr->ddt_size));
		return -EINVAL;
	}

	return 0;
}

static int aw_check_data_crc_v_1_0_0_0(struct device *dev, char *fw_data)
{
	int i = 0;
	size_t crc_val = 0;
	char *data = NULL;
	struct aw_acf_hdr *acf_hdr = NULL;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde = NULL;

	acf_hdr = (struct aw_acf_hdr *)fw_data;
	acf_dde = (struct aw_acf_dde_v_1_0_0_0 *)(fw_data + acf_hdr->ddt_offset);

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		data = fw_data + acf_dde[i].data_offset;
		crc_val = aw_crc8_check(data, acf_dde[i].data_size);
		if (crc_val != acf_dde[i].data_crc) {
			AW_DEV_LOGE(dev, "acf dde_crc check failed");
			return -EINVAL;
		}
	}

	return 0;
}

static int aw_check_data_v_1_0_0_0(struct device *dev,
			char *fw_data, size_t size)
{
	int ret = -1;

	/* check file type id is awinic acf file */
	ret = aw_check_file_id(dev, fw_data, AW_ACF_FILE_ID);
	if (ret < 0)
		return ret;

	/* check ddt_size in header is equal to all ddt aize */
	ret = aw_check_ddt_size_v_1_0_0_0(dev, fw_data);
	if (ret < 0)
		return ret;

	/* Verify that the file size is equal to the header size plus */
	/* the table size and data size */
	ret = aw_check_data_size_v_1_0_0_0(dev, fw_data, size);
	if (ret < 0)
		return ret;

	/* check crc in is equal to dde data crc */
	ret = aw_check_data_crc_v_1_0_0_0(dev, fw_data);
	if (ret < 0)
		return ret;

	AW_DEV_LOGI(dev, "acf fimware check succeed");

	return 0;
}

/***************************************************************************
 * acf chack API
 **************************************************************************/
static int aw_check_acf_firmware(struct device *dev,
			char *fw_data, size_t size)
{
	int ret = -1;
	struct aw_acf_hdr *acf_hdr = NULL;

	if (fw_data == NULL) {
		AW_DEV_LOGE(dev, "fw_data is NULL,fw_data check failed");
		return -ENODATA;
	}

	/* check file size is less-than header size */
	ret = aw_check_header_size(dev, fw_data, size);
	if (ret < 0)
		return ret;

	acf_hdr = (struct aw_acf_hdr *)fw_data;
	AW_DEV_LOGI(dev, "project name: [%s]", acf_hdr->project);
	AW_DEV_LOGI(dev, "custom name: [%s]", acf_hdr->custom);
	AW_DEV_LOGI(dev, "version name: [%s]", acf_hdr->version);
	AW_DEV_LOGI(dev, "author_id: [%d]", acf_hdr->author_id);

	switch (acf_hdr->hdr_version) {
	case AW_ACF_HDR_VER_0_0_0_1:
		return aw_check_data_v_0_0_0_1(dev, fw_data, size);
	case AW_ACF_HDR_VER_1_0_0_0:
		return aw_check_data_v_1_0_0_0(dev, fw_data, size);
	default:
		AW_DEV_LOGE(dev, "unsupported hdr_version [0x%x]",
			acf_hdr->hdr_version);
		return -EINVAL;
	}

	return ret;
}



/*************************************************************************
 *
 *acf parse
 *
 *************************************************************************/
static int aw_parse_raw_reg(struct device *dev, uint8_t *data,
		uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	AW_DEV_LOGD(dev, "data_size:%d enter", data_len);

	prof_desc->data_container.data = data;
	prof_desc->data_container.len = data_len;

	prof_desc->prof_st = AW_PROFILE_OK;

	return 0;
}

static int aw_parse_reg_with_hdr(struct device *dev, uint8_t *data,
			 uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	struct aw_bin *aw_bin = NULL;
	int ret = -1;

	AW_DEV_LOGD(dev, "data_size:%d enter", data_len);

	aw_bin = kzalloc(data_len + sizeof(struct aw_bin), GFP_KERNEL);
	if (aw_bin == NULL) {
		AW_DEV_LOGE(dev, "devm_kzalloc aw_bin failed");
		return -ENOMEM;
	}

	aw_bin->info.len = data_len;
	memcpy(aw_bin->info.data, data, data_len);

	ret = aw_parsing_bin_file(aw_bin);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "parse bin failed");
		goto parse_bin_failed;
	}

	if ((aw_bin->all_bin_parse_num != 1) ||
		(aw_bin->header_info[0].bin_data_type != DATA_TYPE_REGISTER)) {
		AW_DEV_LOGE(dev, "bin num or type error");
		goto parse_bin_failed;
	}

	prof_desc->data_container.data =
				data + aw_bin->header_info[0].valid_data_addr;
	prof_desc->data_container.len = aw_bin->header_info[0].valid_data_len;
	prof_desc->prof_st = AW_PROFILE_OK;

	kfree(aw_bin);
	aw_bin = NULL;

	return 0;

parse_bin_failed:
	kfree(aw_bin);
	aw_bin = NULL;
	return ret;
}

static int aw_parse_monitor_config(struct device *dev,
				char *monitor_data, uint32_t data_len)
{
	int ret = -1;

	if (monitor_data == NULL || data_len == 0) {
		AW_DEV_LOGE(dev, "no data to parse");
		return -EBFONT;
	}

	ret = aw_monitor_bin_parse(dev, monitor_data, data_len);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "monitor_config parse failed");
		return ret;
	}

	AW_DEV_LOGI(dev, "monitor_bin parse succeed");

	return 0;
}

static int aw_check_prof_str_is_off(char *profile_name)
{
	int i = 0;

	for (i = 0; i < AW_POWER_OFF_NAME_SUPPORT_COUNT; i++) {
		if (strnstr(profile_name, g_power_off_name[i],
				strlen(profile_name) + 1))
			return 0;
	}

	return -EINVAL;
}

/***************************************************************************
 * V0.0.0.1 version acf paese
 **************************************************************************/
static int aw_check_product_name_v_0_0_0_1(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_acf_dde *prof_hdr)
{
	int i = 0;

	for (i = 0; i < acf_info->product_cnt; i++) {
		if (0 == strcmp(acf_info->product_tab[i], prof_hdr->dev_name)) {
			AW_DEV_LOGD(dev, "bin_dev_name:%s",
				prof_hdr->dev_name);
			return 0;
		}
	}

	return -ENXIO;
}

static int aw_check_data_type_is_monitor_v_0_0_0_1(struct device *dev,
				struct aw_acf_dde *prof_hdr)
{
	if (prof_hdr->data_type == AW_MONITOR) {
		AW_DEV_LOGD(dev, "bin data is monitor");
		return 0;
	}

	return -ENXIO;
}

static int aw_parse_data_by_sec_type_v_0_0_0_1(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_acf_dde *prof_hdr,
				struct aw_prof_desc *profile_prof_desc)
{
	int ret = -1;
	char *cfg_data = acf_info->fw_data + prof_hdr->data_offset;

	switch (prof_hdr->data_type) {
	case AW_BIN_TYPE_REG:
		snprintf(profile_prof_desc->dev_name, sizeof(prof_hdr->dev_name),
			"%s", prof_hdr->dev_name);
		profile_prof_desc->prof_name = aw_get_prof_name(prof_hdr->dev_profile);
		AW_DEV_LOGD(dev, "parse reg type data enter,profile=%s",
			aw_get_prof_name(prof_hdr->dev_profile));
		ret =  aw_parse_raw_reg(dev, cfg_data, prof_hdr->data_size,
					profile_prof_desc);
		break;
	case AW_BIN_TYPE_HDR_REG:
		snprintf(profile_prof_desc->dev_name, sizeof(prof_hdr->dev_name),
			"%s", prof_hdr->dev_name);
		profile_prof_desc->prof_name = aw_get_prof_name(prof_hdr->dev_profile);
		AW_DEV_LOGD(dev, "parse hdr_reg type data enter,profile=%s",
			aw_get_prof_name(prof_hdr->dev_profile));
		ret = aw_parse_reg_with_hdr(dev, cfg_data,
					prof_hdr->data_size,
					profile_prof_desc);
		break;
	case AW_MONITOR:
		AW_DEV_LOGD(dev, "parse monitor type data enter");
		ret = aw_parse_monitor_config(dev, cfg_data,
					prof_hdr->data_size);
		break;
	}

	return ret;
}

static int aw_parse_dev_type_v_0_0_0_1(struct device *dev,
		struct acf_bin_info *acf_info, struct aw_all_prof_info *all_prof_info)
{
	int i = 0;
	int ret = -1;
	int sec_num = 0;
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_acf_dde *acf_dde =
		(struct aw_acf_dde *)(acf_info->fw_data + acf_info->acf_hdr.ddt_offset);

	AW_DEV_LOGD(dev, "enter");

	for (i = 0; i < acf_info->acf_hdr.dde_num; i++) {
		if ((acf_info->aw_dev->i2c_bus == acf_dde[i].dev_bus) &&
			(acf_info->aw_dev->i2c_addr == acf_dde[i].dev_addr) &&
			(acf_dde[i].type == AW_DDE_DEV_TYPE_ID)) {

			ret = aw_check_product_name_v_0_0_0_1(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			ret = aw_check_data_type_is_monitor_v_0_0_0_1(dev, &acf_dde[i]);
			if (ret == 0) {
				prof_desc = NULL;
			} else {
				prof_desc = &all_prof_info->prof_desc[acf_dde[i].dev_profile];
			}

			if (acf_dde[i].dev_profile == AW_PROFILE_OFF && !soft_off_enable) {
				AW_DEV_LOGE(dev, "profile off is not allowed");
				return -EINVAL;
			}

			ret = aw_parse_data_by_sec_type_v_0_0_0_1(dev, acf_info, &acf_dde[i],
				prof_desc);
			if (ret < 0) {
				AW_DEV_LOGE(dev, "parse dev type data failed");
				return ret;
			}
			sec_num++;
		}
	}

	if (sec_num == 0) {
		AW_DEV_LOGD(dev, "get dev type num is %d, please use default",
			sec_num);
		return AW_DEV_TYPE_NONE;
	}

	return AW_DEV_TYPE_OK;
}

static int aw_parse_default_type_v_0_0_0_1(struct device *dev,
	struct acf_bin_info *acf_info, struct aw_all_prof_info *all_prof_info)
{
	int i = 0;
	int ret = -1;
	int sec_num = 0;
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_acf_dde *acf_dde =
		(struct aw_acf_dde *)(acf_info->fw_data + acf_info->acf_hdr.ddt_offset);

	AW_DEV_LOGD(dev, "enter");

	for (i = 0; i < acf_info->acf_hdr.dde_num; i++) {
		if ((acf_info->dev_index == acf_dde[i].dev_index) &&
			(acf_dde[i].type == AW_DDE_DEV_DEFAULT_TYPE_ID)) {

			ret = aw_check_product_name_v_0_0_0_1(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			ret = aw_check_data_type_is_monitor_v_0_0_0_1(dev, &acf_dde[i]);
			if (ret == 0) {
				prof_desc = NULL;
			} else {
				prof_desc = &all_prof_info->prof_desc[acf_dde[i].dev_profile];
			}

			if (acf_dde[i].dev_profile == AW_PROFILE_OFF && !soft_off_enable) {
				AW_DEV_LOGE(dev, "profile off is not allowed");
				return -EINVAL;
			}

			ret = aw_parse_data_by_sec_type_v_0_0_0_1(dev, acf_info, &acf_dde[i],
				prof_desc);
			if (ret < 0) {
				AW_DEV_LOGE(dev, "parse default type data failed");
				return ret;
			}
			sec_num++;
		}
	}

	if (sec_num == 0) {
		AW_DEV_LOGE(dev, "get dev default type failed, get num[%d]",
			sec_num);
		return -EINVAL;
	}

	return 0;
}

static int aw_get_prof_count_v_0_0_0_1(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_all_prof_info *all_prof_info)
{
	int i = 0;
	int prof_count = 0;
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_prof_desc *prof_desc = all_prof_info->prof_desc;

	for (i = 0; i < AW_PROFILE_MAX; i++) {
		if (i == AW_PROFILE_OFF) {
			if (!soft_off_enable && prof_desc[i].prof_st == AW_PROFILE_OK) {
				AW_DEV_LOGE(dev, "profile_off is not allowed");
				return -EINVAL;
			} else if (soft_off_enable && prof_desc[i].prof_st == AW_PROFILE_WAIT) {
				AW_DEV_LOGE(dev, "profile [Off] is necessary,but not found");
				return -EINVAL;
			} else {
				prof_count++;
			}
		} else {
			if (prof_desc[i].prof_st == AW_PROFILE_OK)
				prof_count++;
		}
	}

	AW_DEV_LOGI(dev, "get profile count=[%d]", prof_count);
	return prof_count;
}

static int aw_set_prof_off_info_v_0_0_0_1(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_all_prof_info *all_prof_info,
				int index)
{
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_prof_desc *prof_desc = all_prof_info->prof_desc;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	if (index >= prof_info->count) {
		AW_DEV_LOGE(dev, "index[%d] is out of table,profile count[%d]",
			index, prof_info->count);
		return -EINVAL;
	}

	if (soft_off_enable && prof_desc[AW_PROFILE_OFF].prof_st == AW_PROFILE_OK) {
		prof_info->prof_desc[index] = prof_desc[AW_PROFILE_OFF];
		AW_DEV_LOGI(dev, "product=[%s]----profile=[%s]",
			prof_info->prof_desc[index].dev_name,
			aw_get_prof_name(AW_PROFILE_OFF));
	} else if (!soft_off_enable) {
		memset(&prof_info->prof_desc[index].data_container, 0,
			sizeof(struct aw_data_container));
		prof_info->prof_desc[index].prof_st = AW_PROFILE_WAIT;
		prof_info->prof_desc[index].prof_name = aw_get_prof_name(AW_PROFILE_OFF);
		AW_DEV_LOGI(dev, "set default power_off with no data to profile");
	} else {
		AW_DEV_LOGE(dev, "not init default power_off config");
		return -EINVAL;
	}

	return 0;
}


static int aw_get_vaild_prof_v_0_0_0_1(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_all_prof_info *all_prof_info)
{
	int i = 0;
	int ret = 0;
	int index = 0;
	struct aw_prof_desc *prof_desc = all_prof_info->prof_desc;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	prof_info->count = 0;
	ret = aw_get_prof_count_v_0_0_0_1(dev, acf_info, all_prof_info);
	if (ret < 0)
		return ret;
	prof_info->count = ret;
	prof_info->prof_desc = devm_kzalloc(dev,
			prof_info->count * sizeof(struct aw_prof_desc),
			GFP_KERNEL);
	if (prof_info->prof_desc == NULL) {
		AW_DEV_LOGE(dev, "prof_desc kzalloc failed");
		return -ENOMEM;
	}

	for (i = 0; i < AW_PROFILE_MAX; i++) {
		if (i != AW_PROFILE_OFF && prof_desc[i].prof_st == AW_PROFILE_OK) {
			if (index >= prof_info->count) {
				AW_DEV_LOGE(dev, "get profile index[%d] overflow count[%d]",
						index, prof_info->count);
				return -ENOMEM;
			}
			prof_info->prof_desc[index] = prof_desc[i];
			AW_DEV_LOGI(dev, "product=[%s]----profile=[%s]",
				prof_info->prof_desc[index].dev_name,
				aw_get_prof_name(i));
			index++;
		}
	}

	ret = aw_set_prof_off_info_v_0_0_0_1(dev, acf_info, all_prof_info, index);
	if (ret < 0)
		return ret;

	AW_DEV_LOGD(dev, "get vaild profile succeed");
	return 0;
}

static int aw_set_prof_name_list_v_0_0_0_1(struct device *dev,
				struct acf_bin_info *acf_info)
{
	int i = 0;
	int count = acf_info->prof_info.count;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	prof_info->prof_name_list = (char (*)[AW_PROFILE_STR_MAX])devm_kzalloc(dev,
		count * (AW_PROFILE_STR_MAX), GFP_KERNEL);
	if (prof_info->prof_name_list == NULL) {
		AW_DEV_LOGE(dev, "prof_name_list devm_kzalloc failed");
		return -ENOMEM;
	}

	for (i = 0; i < count; ++i) {
		snprintf(prof_info->prof_name_list[i], AW_PROFILE_STR_MAX, "%s",
			prof_info->prof_desc[i].prof_name);
		AW_DEV_LOGI(dev, "index=[%d], profile_name=[%s]",
				i, prof_info->prof_name_list[i]);
	}

	return 0;
}

static int aw_parse_acf_v_0_0_0_1(struct device *dev,
		struct acf_bin_info *acf_info)

{
	int ret = 0;
	struct aw_all_prof_info all_prof_info;

	AW_DEV_LOGD(dev, "enter");
	acf_info->prof_info.status = AW_ACF_WAIT;

	memset(&all_prof_info, 0, sizeof(struct aw_all_prof_info));

	ret = aw_parse_dev_type_v_0_0_0_1(dev, acf_info, &all_prof_info);
	if (ret < 0) {
		return ret;
	} else if (ret == AW_DEV_TYPE_NONE) {
		AW_DEV_LOGD(dev, "get dev type num is 0, parse default dev type");
		ret = aw_parse_default_type_v_0_0_0_1(dev, acf_info, &all_prof_info);
		if (ret < 0)
			return ret;
	}

	ret = aw_get_vaild_prof_v_0_0_0_1(dev, acf_info, &all_prof_info);
	if (ret < 0) {
		aw_acf_profile_free(dev, acf_info);
		AW_DEV_LOGE(dev,  "hdr_cersion[0x%x] parse failed",
					acf_info->acf_hdr.hdr_version);
		return ret;
	}

	ret = aw_set_prof_name_list_v_0_0_0_1(dev, acf_info);
	if (ret < 0) {
		aw_acf_profile_free(dev, acf_info);
		AW_DEV_LOGE(dev,  "creat prof_id_and_name_list failed");
		return ret;
	}

	acf_info->prof_info.status = AW_ACF_UPDATE;
	AW_DEV_LOGI(dev, "acf parse success");
	return 0;
}

/***************************************************************************
 * V1.0.0.0 version acf paese
 **************************************************************************/
static int aw_check_product_name_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_acf_dde_v_1_0_0_0 *prof_hdr)
{
	int i = 0;

	for (i = 0; i < acf_info->product_cnt; i++) {
		if (0 == strcmp(acf_info->product_tab[i], prof_hdr->dev_name)) {
			AW_DEV_LOGI(dev, "bin_dev_name:%s", prof_hdr->dev_name);
			return 0;
		}
	}

	return -ENXIO;
}

static void aw_print_prof_off_name_can_support_v_1_0_0_0(struct device *dev)
{
	int i = 0;

	for (i = 0; i < AW_POWER_OFF_NAME_SUPPORT_COUNT; i++)
		AW_DEV_LOGI(dev, "support prof_off_name have string:[%s]", g_power_off_name[i]);
}

static int aw_get_dde_type_info_v_1_0_0_0(struct device *dev,
					struct acf_bin_info *acf_info)
{
	int i;
	int dev_num = 0;
	int default_num = 0;
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)acf_info->fw_data;
	struct aw_prof_info *prof_info = &acf_info->prof_info;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_hdr->ddt_offset);

	prof_info->prof_type = AW_DEV_NONE_TYPE_ID;
	for (i = 0; i < acf_hdr->dde_num; i++) {
		if (acf_dde[i].type == AW_DDE_DEV_TYPE_ID)
			dev_num++;
		if (acf_dde[i].type == AW_DDE_DEV_DEFAULT_TYPE_ID)
			default_num++;
	}

	if (!(dev_num || default_num)) {
		AW_DEV_LOGE(dev, "can't find scene");
		return -EINVAL;
	}

	if (dev_num != 0)
		prof_info->prof_type = AW_DDE_DEV_TYPE_ID;
	else if (default_num != 0)
		prof_info->prof_type = AW_DDE_DEV_DEFAULT_TYPE_ID;

	return 0;
}


static int aw_parse_get_dev_type_prof_count_v_1_0_0_0(struct device *dev,
						struct acf_bin_info *acf_info)
{
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)acf_info->fw_data;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_hdr->ddt_offset);
	int i = 0;
	int ret = 0;
	int found_off_prof_flag = 0;
	int count = acf_info->prof_info.count;

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (((acf_dde[i].data_type == AW_BIN_TYPE_REG) ||
		(acf_dde[i].data_type == AW_BIN_TYPE_HDR_REG)) &&
		((acf_info->aw_dev->i2c_bus == acf_dde[i].dev_bus) &&
		(acf_info->aw_dev->i2c_addr == acf_dde[i].dev_addr)) &&
		(acf_info->aw_dev->chipid == acf_dde[i].chip_id)) {

			ret = aw_check_product_name_v_1_0_0_0(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			ret = aw_check_prof_str_is_off(acf_dde[i].dev_profile_str);
			if (ret == 0) {
				found_off_prof_flag = AW_PROFILE_OK;
				if (soft_off_enable) {
					count++;
				} else {
					AW_DEV_LOGE(dev, "profile_off is not allowed");
					return -EINVAL;
				}
			} else {
				count++;
			}
		}
	}

	if (count == 0) {
		AW_DEV_LOGE(dev, "can't find profile");
		return -EINVAL;
	}

	if (!found_off_prof_flag && soft_off_enable) {
		AW_DEV_LOGE(dev, "profile power off is necessary,but not found");
		aw_print_prof_off_name_can_support_v_1_0_0_0(dev);
		return -EINVAL;
	}

	if (!found_off_prof_flag && !soft_off_enable) {
		count++;
		AW_DEV_LOGD(dev, "set no config power off profile in count");
	}

	acf_info->prof_info.count = count;
	AW_DEV_LOGI(dev, "profile dev_type profile count is %d", acf_info->prof_info.count);
	return 0;
}

static int aw_parse_get_default_type_prof_count_v_1_0_0_0(struct device *dev,
						struct acf_bin_info *acf_info)
{
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)acf_info->fw_data;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_hdr->ddt_offset);
	int i = 0;
	int ret = 0;
	int found_off_prof_flag = 0;
	int count = acf_info->prof_info.count;

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (((acf_dde[i].data_type == AW_BIN_TYPE_REG) ||
		(acf_dde[i].data_type == AW_BIN_TYPE_HDR_REG)) &&
		(acf_info->dev_index == acf_dde[i].dev_index) &&
		(acf_info->aw_dev->chipid == acf_dde[i].chip_id)) {

			ret = aw_check_product_name_v_1_0_0_0(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			ret = aw_check_prof_str_is_off(acf_dde[i].dev_profile_str);
			if (ret == 0) {
				found_off_prof_flag = AW_PROFILE_OK;
				if (soft_off_enable) {
					count++;
				} else {
					AW_DEV_LOGE(dev, "profile_off is not allowed");
					return -EINVAL;
				}
			} else {
				count++;
			}
		}
	}

	if (count == 0) {
		AW_DEV_LOGE(dev, "can't find profile");
		return -EINVAL;
	}

	if (!found_off_prof_flag && soft_off_enable) {
		AW_DEV_LOGE(dev, "profile power off is necessary,but not found");
		aw_print_prof_off_name_can_support_v_1_0_0_0(dev);
		return -EINVAL;
	}

	if (!found_off_prof_flag && !soft_off_enable) {
		count++;
		AW_DEV_LOGD(dev, "set no config power off profile in count");
	}

	acf_info->prof_info.count = count;
	AW_DEV_LOGI(dev, "profile default_type profile count is %d", acf_info->prof_info.count);
	return 0;
}

static int aw_parse_get_profile_count_v_1_0_0_0(struct device *dev,
						struct acf_bin_info *acf_info)
{
	int ret = 0;

	ret = aw_get_dde_type_info_v_1_0_0_0(dev, acf_info);
	if (ret < 0)
		return ret;

	if (acf_info->prof_info.prof_type == AW_DDE_DEV_TYPE_ID) {
		ret = aw_parse_get_dev_type_prof_count_v_1_0_0_0(dev, acf_info);
		if (ret < 0) {
			AW_DEV_LOGE(dev, "parse dev_type profile count failed");
			return ret;
		}
	} else if (acf_info->prof_info.prof_type == AW_DDE_DEV_DEFAULT_TYPE_ID) {
		ret = aw_parse_get_default_type_prof_count_v_1_0_0_0(dev, acf_info);
		if (ret < 0) {
			AW_DEV_LOGE(dev, "parse default_type profile count failed");
			return ret;
		}
	} else {
		AW_DEV_LOGE(dev, "unsupport prof_type[0x%x]",
			acf_info->prof_info.prof_type);
		return -EINVAL;
	}

	AW_DEV_LOGI(dev, "profile count is %d", acf_info->prof_info.count);
	return 0;
}

static int aw_parse_dev_type_prof_name_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info)
{
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)acf_info->fw_data;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_hdr->ddt_offset);
	struct aw_prof_info *prof_info = &acf_info->prof_info;
	int i, ret, list_index = 0;

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (((acf_dde[i].data_type == AW_BIN_TYPE_REG) ||
		(acf_dde[i].data_type == AW_BIN_TYPE_HDR_REG)) &&
		(acf_info->aw_dev->i2c_bus == acf_dde[i].dev_bus) &&
		(acf_info->aw_dev->i2c_addr == acf_dde[i].dev_addr) &&
		(acf_info->aw_dev->chipid == acf_dde[i].chip_id)) {
			if (list_index > prof_info->count) {
				AW_DEV_LOGE(dev, "%s:Alrealdy set list_index [%d], redundant profile [%s]exist\n",
					__func__, list_index,
					acf_dde[i].dev_profile_str);
				return -EINVAL;
			}

			ret = aw_check_product_name_v_1_0_0_0(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			snprintf(prof_info->prof_name_list[list_index], AW_PROFILE_STR_MAX, "%s",
				acf_dde[i].dev_profile_str);
			AW_DEV_LOGI(dev, "profile_name=[%s]",
					prof_info->prof_name_list[list_index]);
			list_index++;
		}
	}

	return 0;
}

static int aw_parse_default_type_prof_name_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info)
{
	struct aw_acf_hdr *acf_hdr = (struct aw_acf_hdr *)acf_info->fw_data;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_hdr->ddt_offset);
	struct aw_prof_info *prof_info = &acf_info->prof_info;
	int i, ret, list_index = 0;

	for (i = 0; i < acf_hdr->dde_num; ++i) {
		if (((acf_dde[i].data_type == AW_BIN_TYPE_REG) ||
		(acf_dde[i].data_type == AW_BIN_TYPE_HDR_REG)) &&
		(acf_info->dev_index == acf_dde[i].dev_index) &&
		(acf_info->aw_dev->chipid == acf_dde[i].chip_id)) {
			if (list_index > prof_info->count) {
				AW_DEV_LOGE(dev, "%s:Alrealdy set list_index [%d], redundant profile [%s]exist\n",
					__func__, list_index,
					acf_dde[i].dev_profile_str);
				return -EINVAL;
			}

			ret = aw_check_product_name_v_1_0_0_0(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			snprintf(prof_info->prof_name_list[list_index], AW_PROFILE_STR_MAX, "%s",
				acf_dde[i].dev_profile_str);
			AW_DEV_LOGI(dev, "profile_name=[%s]",
					prof_info->prof_name_list[list_index]);
			list_index++;
		}
	}

	return 0;
}

static int aw_parse_prof_name_v_1_0_0_0(struct device *dev,
						struct acf_bin_info *acf_info)
{
	int ret = 0;
	int count = acf_info->prof_info.count;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	prof_info->prof_name_list = (char (*)[AW_PROFILE_STR_MAX])devm_kzalloc(dev,
		count * (AW_PROFILE_STR_MAX), GFP_KERNEL);
	if (prof_info->prof_name_list == NULL) {
		AW_DEV_LOGE(dev, "prof_name_list devm_kzalloc failed");
		return -ENOMEM;
	}

	if (acf_info->prof_info.prof_type == AW_DDE_DEV_TYPE_ID) {
		ret = aw_parse_dev_type_prof_name_v_1_0_0_0(dev, acf_info);
		if (ret < 0) {
			AW_DEV_LOGE(dev, "parse dev_type profile count failed");
			return ret;
		}
	} else if (acf_info->prof_info.prof_type == AW_DDE_DEV_DEFAULT_TYPE_ID) {
		ret = aw_parse_default_type_prof_name_v_1_0_0_0(dev, acf_info);
		if (ret < 0) {
			AW_DEV_LOGE(dev, "parse default_type profile count failed");
			return ret;
		}
	} else {
		AW_DEV_LOGE(dev, "unsupport prof_type[0x%x]",
			acf_info->prof_info.prof_type);
		return -EINVAL;
	}

	AW_DEV_LOGI(dev, "profile name parse succeed");
	return 0;
}


static int aw_search_prof_index_from_list_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_prof_desc **prof_desc,
				struct aw_acf_dde_v_1_0_0_0 *prof_hdr)
{
	int i = 0;
	int count = acf_info->prof_info.count;
	char (*prof_name_list)[AW_PROFILE_STR_MAX] = acf_info->prof_info.prof_name_list;

	for (i = 0; i < count; i++) {
		if (!strncmp(prof_name_list[i], prof_hdr->dev_profile_str, AW_PROFILE_STR_MAX)) {
			*prof_desc = &(acf_info->prof_info.prof_desc[i]);
			return 0;
		}
	}

	if (i == count)
		AW_DEV_LOGE(dev, "not find prof_id and prof_name in list");

	return -EINVAL;
}

static int aw_parse_data_by_sec_type_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info,
				struct aw_acf_dde_v_1_0_0_0 *prof_hdr)
{
	int ret = -1;
	char *cfg_data = acf_info->fw_data + prof_hdr->data_offset;
	struct aw_prof_desc *prof_desc = NULL;

	ret = aw_search_prof_index_from_list_v_1_0_0_0(dev, acf_info, &prof_desc, prof_hdr);
	if (ret < 0)
		return ret;

	switch (prof_hdr->data_type) {
	case AW_BIN_TYPE_REG:
		snprintf(prof_desc->dev_name, sizeof(prof_hdr->dev_name),
			"%s", prof_hdr->dev_name);
		AW_DEV_LOGI(dev, "parse reg type data enter,product=[%s],prof_id=[%d],prof_name=[%s]",
			prof_hdr->dev_name, prof_hdr->dev_profile,
			prof_hdr->dev_profile_str);
		prof_desc->prof_name = prof_hdr->dev_profile_str;
		ret =  aw_parse_raw_reg(dev, cfg_data, prof_hdr->data_size,
					prof_desc);
		break;
	case AW_BIN_TYPE_HDR_REG:
		snprintf(prof_desc->dev_name, sizeof(prof_hdr->dev_name),
			"%s", prof_hdr->dev_name);
		AW_DEV_LOGI(dev, "parse hdr_reg type data enter,product=[%s],prof_id=[%d],prof_name=[%s]",
			prof_hdr->dev_name, prof_hdr->dev_profile,
			prof_hdr->dev_profile_str);
		prof_desc->prof_name = prof_hdr->dev_profile_str;
		ret = aw_parse_reg_with_hdr(dev, cfg_data,
				prof_hdr->data_size, prof_desc);
		break;
	}

	return ret;
}

static int aw_parse_dev_type_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info)
{
	int i = 0;
	int ret;
	int parse_prof_count = 0;
	char *cfg_data = NULL;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_info->acf_hdr.ddt_offset);

	AW_DEV_LOGD(dev, "enter");

	for (i = 0; i < acf_info->acf_hdr.dde_num; i++) {
		if ((acf_dde[i].type == AW_DDE_DEV_TYPE_ID) &&
		(acf_info->aw_dev->i2c_bus == acf_dde[i].dev_bus) &&
		(acf_info->aw_dev->i2c_addr == acf_dde[i].dev_addr) &&
		(acf_info->aw_dev->chipid == acf_dde[i].chip_id)) {
			ret = aw_check_product_name_v_1_0_0_0(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			if (acf_dde[i].data_type == AW_MONITOR) {
				cfg_data = acf_info->fw_data + acf_dde[i].data_offset;
				AW_DEV_LOGD(dev, "parse monitor type data enter");
				ret = aw_parse_monitor_config(dev, cfg_data,
					acf_dde[i].data_size);
			} else {
				ret = aw_parse_data_by_sec_type_v_1_0_0_0(dev, acf_info,
					&acf_dde[i]);
				if (ret < 0)
					AW_DEV_LOGE(dev, "parse dev type data failed");
				else
					parse_prof_count++;
			}
		}
	}

	if (parse_prof_count == 0) {
		AW_DEV_LOGE(dev, "get dev type num is %d, parse failed", parse_prof_count);
		return -EINVAL;
	}

	return AW_DEV_TYPE_OK;
}

static int aw_parse_default_type_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info)
{
	int i = 0;
	int ret;
	int parse_prof_count = 0;
	char *cfg_data = NULL;
	struct aw_acf_dde_v_1_0_0_0 *acf_dde =
		(struct aw_acf_dde_v_1_0_0_0 *)(acf_info->fw_data + acf_info->acf_hdr.ddt_offset);

	AW_DEV_LOGD(dev, "enter");

	for (i = 0; i < acf_info->acf_hdr.dde_num; i++) {
		if ((acf_dde[i].type == AW_DDE_DEV_DEFAULT_TYPE_ID) &&
		(acf_info->dev_index == acf_dde[i].dev_index) &&
		(acf_info->aw_dev->chipid == acf_dde[i].chip_id)) {
			ret = aw_check_product_name_v_1_0_0_0(dev, acf_info, &acf_dde[i]);
			if (ret < 0)
				continue;

			if (acf_dde[i].data_type == AW_MONITOR) {
				cfg_data = acf_info->fw_data + acf_dde[i].data_offset;
				AW_DEV_LOGD(dev, "parse monitor type data enter");
				ret = aw_parse_monitor_config(dev, cfg_data,
					acf_dde[i].data_size);
			} else {
				ret = aw_parse_data_by_sec_type_v_1_0_0_0(dev, acf_info,
					&acf_dde[i]);
				if (ret < 0)
					AW_DEV_LOGE(dev, "parse default type data failed");
				else
					parse_prof_count++;
			}
		}
	}

	if (parse_prof_count == 0) {
		AW_DEV_LOGE(dev, "get default type num is %d,parse failed", parse_prof_count);
		return -EINVAL;
	}

	return AW_DEV_TYPE_OK;
}

static int aw_parse_by_hdr_v_1_0_0_0(struct device *dev,
				struct acf_bin_info *acf_info)
{
	int ret;

	if (acf_info->prof_info.prof_type == AW_DDE_DEV_TYPE_ID) {
		ret = aw_parse_dev_type_v_1_0_0_0(dev, acf_info);
		if (ret < 0)
			return ret;
	} else if (acf_info->prof_info.prof_type == AW_DDE_DEV_DEFAULT_TYPE_ID) {
		ret = aw_parse_default_type_v_1_0_0_0(dev, acf_info);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int aw_set_prof_off_info_v_1_0_0_0(struct device *dev,
						struct acf_bin_info *acf_info)
{
	uint8_t soft_off_enable = acf_info->aw_dev->soft_off_enable;
	struct aw_prof_info *prof_info = &acf_info->prof_info;
	int i = 0;
	int ret = 0;

	for (i = 0; i < prof_info->count; ++i) {
		if (!(prof_info->prof_desc[i].prof_st) && !soft_off_enable) {
			snprintf(prof_info->prof_name_list[i], AW_PROFILE_STR_MAX, "%s",
					g_power_off_name[0]);
			prof_info->prof_desc[i].prof_name = prof_info->prof_name_list[i];
			prof_info->prof_desc[i].prof_st = AW_PROFILE_WAIT;
			memset(&prof_info->prof_desc[i].data_container, 0,
					sizeof(struct aw_data_container));
			return 0;
		}

		ret = aw_check_prof_str_is_off(prof_info->prof_name_list[i]);
		if (ret == 0) {
			AW_DEV_LOGD(dev, "found profile off,data_len=[%d]",
				prof_info->prof_desc[i].data_container.len);
			return 0;
		}
	}

	AW_DEV_LOGE(dev, "index[%d] is out of table,profile count[%d]",
		i, prof_info->count);
	return -EINVAL;
}

static int aw_parse_acf_v_1_0_0_0(struct device *dev,
		struct acf_bin_info *acf_info)

{
	struct aw_prof_info *prof_info = &acf_info->prof_info;
	int ret;

	ret = aw_parse_get_profile_count_v_1_0_0_0(dev, acf_info);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "get profile count failed");
		return ret;
	}

	ret = aw_parse_prof_name_v_1_0_0_0(dev, acf_info);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "get profile count failed");
		return ret;
	}

	acf_info->prof_info.prof_desc = devm_kzalloc(dev,
		prof_info->count * sizeof(struct aw_prof_desc), GFP_KERNEL);
	if (acf_info->prof_info.prof_desc == NULL) {
		AW_DEV_LOGE(dev, "prof_desc devm_kzalloc failed");
		return -ENOMEM;
	}

	ret = aw_parse_by_hdr_v_1_0_0_0(dev, acf_info);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "parse data failed");
		return ret;
	}

	ret = aw_set_prof_off_info_v_1_0_0_0(dev, acf_info);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "set profile off info failed");
		return ret;
	}

	prof_info->status = AW_ACF_UPDATE;
	AW_DEV_LOGI(dev, "acf paese succeed");
	return 0;
}


/*************************************************************************
 *
 *acf parse API
 *
 *************************************************************************/
void aw_acf_profile_free(struct device *dev, struct acf_bin_info *acf_info)
{
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	prof_info->count = 0;
	prof_info->status = AW_ACF_WAIT;
	memset(&acf_info->acf_hdr, 0, sizeof(struct aw_acf_hdr));

	if (prof_info->prof_desc) {
		devm_kfree(dev, prof_info->prof_desc);
		prof_info->prof_desc = NULL;
	}

	if (prof_info->prof_name_list) {
		devm_kfree(dev, prof_info->prof_name_list);
		prof_info->prof_name_list = NULL;
	}

	if (acf_info->fw_data) {
		vfree(acf_info->fw_data);
		acf_info->fw_data = NULL;
	}
}

int aw_acf_parse(struct device *dev, struct acf_bin_info *acf_info)
{
	int ret = 0;

	AW_DEV_LOGD(dev, "enter");
	acf_info->prof_info.status = AW_ACF_WAIT;
	ret = aw_check_acf_firmware(dev, acf_info->fw_data,
					acf_info->fw_size);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "load firmware check failed");
		return -EINVAL;
	}

	memcpy(&acf_info->acf_hdr, acf_info->fw_data,
		sizeof(struct aw_acf_hdr));

	switch (acf_info->acf_hdr.hdr_version) {
	case AW_ACF_HDR_VER_0_0_0_1:
		return aw_parse_acf_v_0_0_0_1(dev, acf_info);
	case AW_ACF_HDR_VER_1_0_0_0:
		return aw_parse_acf_v_1_0_0_0(dev, acf_info);
	default:
		AW_DEV_LOGE(dev, "unsupported hdr_version [0x%x]",
			acf_info->acf_hdr.hdr_version);
		return -EINVAL;
	}

	return ret;
}

struct aw_prof_desc *aw_acf_get_prof_desc_form_name(struct device *dev,
			struct acf_bin_info *acf_info, char *profile_name)
{
	int i = 0;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	AW_DEV_LOGD(dev, "enter");

	if (!acf_info->prof_info.status) {
		AW_DEV_LOGE(dev, "profile_cfg not load");
		return NULL;
	}

	for (i = 0; i < prof_info->count; i++) {
		if (!strncmp(profile_name, prof_info->prof_desc[i].prof_name,
				AW_PROFILE_STR_MAX)) {
			prof_desc = &prof_info->prof_desc[i];
			break;
		}
	}

	if (i == prof_info->count) {
		AW_DEV_LOGE(dev, "profile not found");
		return NULL;
	}

	AW_DEV_LOGI(dev, "get prof desc down");
	return prof_desc;
}

int aw_acf_get_prof_index_form_name(struct device *dev,
			struct acf_bin_info *acf_info, char *profile_name)
{
	int i = 0;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	if (!acf_info->prof_info.status) {
		AW_DEV_LOGE(dev, "profile_cfg not load");
		return -EINVAL;
	}

	for (i = 0; i < prof_info->count; i++) {
		if (!strncmp(profile_name, prof_info->prof_name_list[i],
				AW_PROFILE_STR_MAX)) {
			return i;
		}
	}

	AW_DEV_LOGE(dev, "profile_index not found");
	return -EINVAL;
}

char *aw_acf_get_prof_name_form_index(struct device *dev,
			struct acf_bin_info *acf_info, int index)
{
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	if (!acf_info->prof_info.status) {
		AW_DEV_LOGE(dev, "profile_cfg not load");
		return NULL;
	}

	if (index >= prof_info->count  || index < 0) {
		AW_DEV_LOGE(dev, "profile_index out of table");
		return NULL;
	}

	return prof_info->prof_desc[index].prof_name;
}


int aw_acf_get_profile_count(struct device *dev,
			struct acf_bin_info *acf_info)
{
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	if (!acf_info->prof_info.status) {
		AW_DEV_LOGE(dev, "profile_cfg not load");
		return -EINVAL;
	}

	if (prof_info->count > 0) {
		return prof_info->count;
	}

	return -EINVAL;
}

char *aw_acf_get_prof_off_name(struct device *dev,
			struct acf_bin_info *acf_info)
{
	int i = 0;
	int ret = 0;
	struct aw_prof_info *prof_info = &acf_info->prof_info;

	if (!acf_info->prof_info.status) {
		AW_DEV_LOGE(dev, "profile_cfg not load");
		return NULL;
	}

	for (i = 0; i < prof_info->count; i++) {
		ret  = aw_check_prof_str_is_off(prof_info->prof_name_list[i]);
		if (ret == 0)
			return prof_info->prof_name_list[i];
	}

	return NULL;
}

void aw_acf_init(struct aw_device *aw_dev, struct acf_bin_info *acf_info, int index)
{

	acf_info->load_count = 0;
	acf_info->prof_info.status = AW_ACF_WAIT;
	acf_info->dev_index = index;
	acf_info->aw_dev = aw_dev;
	acf_info->product_cnt = aw_dev->product_cnt;
	acf_info->product_tab = aw_dev->product_tab;
	acf_info->prof_info.prof_desc = NULL;
	acf_info->fw_data = NULL;
	acf_info->fw_size = 0;
}

