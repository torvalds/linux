// SPDX-License-Identifier: GPL-2.0-only
//
// aw88395_lib.c  -- ACF bin parsing and check library file for aw88395
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
//

#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/i2c.h>
#include "aw88395_lib.h"
#include "aw88395_device.h"

#define AW88395_CRC8_POLYNOMIAL 0x8C
DECLARE_CRC8_TABLE(aw_crc8_table);

static char *profile_name[AW88395_PROFILE_MAX] = {
	"Music", "Voice", "Voip", "Ringtone",
	"Ringtone_hs", "Lowpower", "Bypass",
	"Mmi", "Fm", "Notification", "Receiver"
};

static int aw_parse_bin_header(struct aw_device *aw_dev, struct aw_bin *bin);

static int aw_check_sum(struct aw_device *aw_dev, struct aw_bin *bin, int bin_num)
{
	unsigned char *p_check_sum;
	unsigned int sum_data = 0;
	unsigned int check_sum;
	unsigned int i, len;

	p_check_sum = &(bin->info.data[(bin->header_info[bin_num].valid_data_addr -
						bin->header_info[bin_num].header_len)]);
	len = bin->header_info[bin_num].bin_data_len + bin->header_info[bin_num].header_len;
	check_sum = le32_to_cpup((void *)p_check_sum);

	for (i = 4; i < len; i++)
		sum_data += *(p_check_sum + i);

	dev_dbg(aw_dev->dev, "%s -- check_sum = %p, check_sum = 0x%x, sum_data = 0x%x",
					__func__, p_check_sum, check_sum, sum_data);
	if (sum_data != check_sum) {
		dev_err(aw_dev->dev, "%s. CheckSum Fail.bin_num=%d, CheckSum:0x%x, SumData:0x%x",
				__func__, bin_num, check_sum, sum_data);
		return -EINVAL;
	}

	return 0;
}

static int aw_check_data_version(struct aw_device *aw_dev, struct aw_bin *bin, int bin_num)
{
	if (bin->header_info[bin_num].bin_data_ver < DATA_VERSION_V1 ||
		bin->header_info[bin_num].bin_data_ver > DATA_VERSION_MAX) {
		dev_err(aw_dev->dev, "aw_bin_parse Unrecognized this bin data version\n");
		return -EINVAL;
	}

	return 0;
}

static int aw_check_register_num(struct aw_device *aw_dev, struct aw_bin *bin, int bin_num)
{
	struct bin_header_info temp_info = bin->header_info[bin_num];
	unsigned int check_register_num, parse_register_num;
	unsigned char *p_check_sum;

	p_check_sum = &(bin->info.data[(temp_info.valid_data_addr)]);

	parse_register_num = le32_to_cpup((void *)p_check_sum);
	check_register_num = (bin->header_info[bin_num].bin_data_len - CHECK_REGISTER_NUM_OFFSET) /
				(bin->header_info[bin_num].reg_byte_len +
				bin->header_info[bin_num].data_byte_len);
	dev_dbg(aw_dev->dev, "%s,parse_register_num = 0x%x,check_register_num = 0x%x\n",
				__func__, parse_register_num, check_register_num);
	if (parse_register_num != check_register_num) {
		dev_err(aw_dev->dev, "%s parse_register_num = 0x%x,check_register_num = 0x%x\n",
				__func__, parse_register_num, check_register_num);
		return -EINVAL;
	}

	bin->header_info[bin_num].reg_num = parse_register_num;
	bin->header_info[bin_num].valid_data_len = temp_info.bin_data_len - VALID_DATA_LEN;
	bin->header_info[bin_num].valid_data_addr = temp_info.valid_data_addr + VALID_DATA_ADDR;

	return 0;
}

static int aw_check_dsp_reg_num(struct aw_device *aw_dev, struct aw_bin *bin, int bin_num)
{
	struct bin_header_info temp_info = bin->header_info[bin_num];
	unsigned int check_dsp_reg_num, parse_dsp_reg_num;
	unsigned char *p_check_sum;

	p_check_sum = &(bin->info.data[(temp_info.valid_data_addr)]);

	parse_dsp_reg_num = le32_to_cpup((void *)(p_check_sum + PARSE_DSP_REG_NUM));
	bin->header_info[bin_num].reg_data_byte_len =
			le32_to_cpup((void *)(p_check_sum + REG_DATA_BYTP_LEN));
	check_dsp_reg_num = (bin->header_info[bin_num].bin_data_len - CHECK_DSP_REG_NUM) /
				bin->header_info[bin_num].reg_data_byte_len;
	dev_dbg(aw_dev->dev, "%s bin_num = %d, parse_dsp_reg_num = 0x%x, check_dsp_reg_num = 0x%x",
					__func__, bin_num, check_dsp_reg_num, check_dsp_reg_num);
	if (parse_dsp_reg_num != check_dsp_reg_num) {
		dev_err(aw_dev->dev, "aw_bin_parse check dsp reg num error\n");
		dev_err(aw_dev->dev, "%s parse_dsp_reg_num = 0x%x, check_dsp_reg_num = 0x%x",
					__func__, check_dsp_reg_num, check_dsp_reg_num);
		return -EINVAL;
	}

	bin->header_info[bin_num].download_addr = le32_to_cpup((void *)p_check_sum);
	bin->header_info[bin_num].reg_num = parse_dsp_reg_num;
	bin->header_info[bin_num].valid_data_len = temp_info.bin_data_len - DSP_VALID_DATA_LEN;
	bin->header_info[bin_num].valid_data_addr = temp_info.valid_data_addr +
								DSP_VALID_DATA_ADDR;

	return 0;
}

static int aw_check_soc_app_num(struct aw_device *aw_dev, struct aw_bin *bin, int bin_num)
{
	struct bin_header_info temp_info = bin->header_info[bin_num];
	unsigned int check_soc_app_num, parse_soc_app_num;
	unsigned char *p_check_sum;

	p_check_sum = &(bin->info.data[(temp_info.valid_data_addr)]);

	bin->header_info[bin_num].app_version = le32_to_cpup((void *)p_check_sum);
	parse_soc_app_num = le32_to_cpup((void *)(p_check_sum + PARSE_SOC_APP_NUM));
	check_soc_app_num = bin->header_info[bin_num].bin_data_len - CHECK_SOC_APP_NUM;
	dev_dbg(aw_dev->dev, "%s bin_num = %d, parse_soc_app_num=0x%x, check_soc_app_num = 0x%x\n",
					__func__, bin_num, parse_soc_app_num, check_soc_app_num);
	if (parse_soc_app_num != check_soc_app_num) {
		dev_err(aw_dev->dev, "%s parse_soc_app_num=0x%x, check_soc_app_num = 0x%x\n",
					__func__, parse_soc_app_num, check_soc_app_num);
		return -EINVAL;
	}

	bin->header_info[bin_num].reg_num = parse_soc_app_num;
	bin->header_info[bin_num].download_addr = le32_to_cpup((void *)(p_check_sum +
								APP_DOWNLOAD_ADDR));
	bin->header_info[bin_num].valid_data_len = temp_info.bin_data_len - APP_VALID_DATA_LEN;
	bin->header_info[bin_num].valid_data_addr = temp_info.valid_data_addr +
								APP_VALID_DATA_ADDR;

	return 0;
}

static void aw_get_single_bin_header(struct aw_bin *bin)
{
	memcpy((void *)&bin->header_info[bin->all_bin_parse_num], bin->p_addr, DATA_LEN);

	bin->header_info[bin->all_bin_parse_num].header_len = HEADER_LEN;
	bin->all_bin_parse_num += 1;
}

static int aw_parse_one_of_multi_bins(struct aw_device *aw_dev, unsigned int bin_num,
					int bin_serial_num, struct aw_bin *bin)
{
	struct bin_header_info aw_bin_header_info;
	unsigned int bin_start_addr;
	unsigned int valid_data_len;

	if (bin->info.len < sizeof(struct bin_header_info)) {
		dev_err(aw_dev->dev, "bin_header_info size[%d] overflow file size[%d]\n",
				(int)sizeof(struct bin_header_info), bin->info.len);
		return -EINVAL;
	}

	aw_bin_header_info = bin->header_info[bin->all_bin_parse_num - 1];
	if (!bin_serial_num) {
		bin_start_addr = le32_to_cpup((void *)(bin->p_addr + START_ADDR_OFFSET));
		bin->p_addr += (HEADER_LEN + bin_start_addr);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
			aw_bin_header_info.valid_data_addr + VALID_DATA_ADDR + 8 * bin_num +
			VALID_DATA_ADDR_OFFSET;
	} else {
		valid_data_len = aw_bin_header_info.bin_data_len;
		bin->p_addr += (HDADER_LEN + valid_data_len);
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
		    aw_bin_header_info.valid_data_addr + aw_bin_header_info.bin_data_len +
		    VALID_DATA_ADDR_OFFSET;
	}

	return aw_parse_bin_header(aw_dev, bin);
}

static int aw_get_multi_bin_header(struct aw_device *aw_dev, struct aw_bin *bin)
{
	unsigned int bin_num, i;
	int ret;

	bin_num = le32_to_cpup((void *)(bin->p_addr + VALID_DATA_ADDR_OFFSET));
	if (bin->multi_bin_parse_num == 1)
		bin->header_info[bin->all_bin_parse_num].valid_data_addr =
							VALID_DATA_ADDR_OFFSET;

	aw_get_single_bin_header(bin);

	for (i = 0; i < bin_num; i++) {
		dev_dbg(aw_dev->dev, "aw_bin_parse enter multi bin for is %d\n", i);
		ret = aw_parse_one_of_multi_bins(aw_dev, bin_num, i, bin);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int aw_parse_bin_header(struct aw_device *aw_dev, struct aw_bin *bin)
{
	unsigned int bin_data_type;

	if (bin->info.len < sizeof(struct bin_header_info)) {
		dev_err(aw_dev->dev, "bin_header_info size[%d] overflow file size[%d]\n",
				(int)sizeof(struct bin_header_info), bin->info.len);
		return -EINVAL;
	}

	bin_data_type = le32_to_cpup((void *)(bin->p_addr + BIN_DATA_TYPE_OFFSET));
	dev_dbg(aw_dev->dev, "aw_bin_parse bin_data_type 0x%x\n", bin_data_type);
	switch (bin_data_type) {
	case DATA_TYPE_REGISTER:
	case DATA_TYPE_DSP_REG:
	case DATA_TYPE_SOC_APP:
		bin->single_bin_parse_num += 1;
		dev_dbg(aw_dev->dev, "%s bin->single_bin_parse_num is %d\n", __func__,
						bin->single_bin_parse_num);
		if (!bin->multi_bin_parse_num)
			bin->header_info[bin->all_bin_parse_num].valid_data_addr =
								VALID_DATA_ADDR_OFFSET;
		aw_get_single_bin_header(bin);
		return 0;
	case DATA_TYPE_MULTI_BINS:
		bin->multi_bin_parse_num += 1;
		dev_dbg(aw_dev->dev, "%s bin->multi_bin_parse_num is %d\n", __func__,
						bin->multi_bin_parse_num);
		return aw_get_multi_bin_header(aw_dev, bin);
	default:
		dev_dbg(aw_dev->dev, "%s There is no corresponding type\n", __func__);
		return 0;
	}
}

static int aw_check_bin_header_version(struct aw_device *aw_dev, struct aw_bin *bin)
{
	unsigned int header_version;

	header_version = le32_to_cpup((void *)(bin->p_addr + HEADER_VERSION_OFFSET));
	dev_dbg(aw_dev->dev, "aw_bin_parse header_version 0x%x\n", header_version);

	switch (header_version) {
	case HEADER_VERSION_V1:
		return aw_parse_bin_header(aw_dev, bin);
	default:
		dev_err(aw_dev->dev, "aw_bin_parse Unrecognized this bin header version\n");
		return -EINVAL;
	}
}

static int aw_parsing_bin_file(struct aw_device *aw_dev, struct aw_bin *bin)
{
	int ret = -EINVAL;
	int i;

	if (!bin) {
		dev_err(aw_dev->dev, "aw_bin_parse bin is NULL\n");
		return ret;
	}
	bin->p_addr = bin->info.data;
	bin->all_bin_parse_num = 0;
	bin->multi_bin_parse_num = 0;
	bin->single_bin_parse_num = 0;

	ret = aw_check_bin_header_version(aw_dev, bin);
	if (ret < 0) {
		dev_err(aw_dev->dev, "aw_bin_parse check bin header version error\n");
		return ret;
	}

	for (i = 0; i < bin->all_bin_parse_num; i++) {
		ret = aw_check_sum(aw_dev, bin, i);
		if (ret < 0) {
			dev_err(aw_dev->dev, "aw_bin_parse check sum data error\n");
			return ret;
		}
		ret = aw_check_data_version(aw_dev, bin, i);
		if (ret < 0) {
			dev_err(aw_dev->dev, "aw_bin_parse check data version error\n");
			return ret;
		}
		if (bin->header_info[i].bin_data_ver == DATA_VERSION_V1) {
			switch (bin->header_info[i].bin_data_type) {
			case DATA_TYPE_REGISTER:
				ret = aw_check_register_num(aw_dev, bin, i);
				break;
			case DATA_TYPE_DSP_REG:
				ret = aw_check_dsp_reg_num(aw_dev, bin, i);
				break;
			case DATA_TYPE_SOC_APP:
				ret = aw_check_soc_app_num(aw_dev, bin, i);
				break;
			default:
				bin->header_info[i].valid_data_len =
						bin->header_info[i].bin_data_len;
				ret = 0;
				break;
			}
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int aw_dev_parse_raw_reg(unsigned char *data, unsigned int data_len,
		struct aw_prof_desc *prof_desc)
{
	prof_desc->sec_desc[AW88395_DATA_TYPE_REG].data = data;
	prof_desc->sec_desc[AW88395_DATA_TYPE_REG].len = data_len;

	prof_desc->prof_st = AW88395_PROFILE_OK;

	return 0;
}

static int aw_dev_parse_raw_dsp_cfg(unsigned char *data, unsigned int data_len,
		struct aw_prof_desc *prof_desc)
{
	if (data_len & 0x01)
		return -EINVAL;

	swab16_array((u16 *)data, data_len >> 1);

	prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_CFG].data = data;
	prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_CFG].len = data_len;

	prof_desc->prof_st = AW88395_PROFILE_OK;

	return 0;
}

static int aw_dev_parse_raw_dsp_fw(unsigned char *data,	unsigned int data_len,
		struct aw_prof_desc *prof_desc)
{
	if (data_len & 0x01)
		return -EINVAL;

	swab16_array((u16 *)data, data_len >> 1);

	prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_FW].data = data;
	prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_FW].len = data_len;

	prof_desc->prof_st = AW88395_PROFILE_OK;

	return 0;
}

static int aw_dev_prof_parse_multi_bin(struct aw_device *aw_dev, unsigned char *data,
				unsigned int data_len, struct aw_prof_desc *prof_desc)
{
	int ret;
	int i;

	struct aw_bin *aw_bin __free(kfree) = kzalloc(data_len + sizeof(struct aw_bin),
						     GFP_KERNEL);
	if (!aw_bin)
		return -ENOMEM;

	aw_bin->info.len = data_len;
	memcpy(aw_bin->info.data, data, data_len);

	ret = aw_parsing_bin_file(aw_dev, aw_bin);
	if (ret < 0) {
		dev_err(aw_dev->dev, "parse bin failed");
		return ret;
	}

	for (i = 0; i < aw_bin->all_bin_parse_num; i++) {
		switch (aw_bin->header_info[i].bin_data_type) {
		case DATA_TYPE_REGISTER:
			prof_desc->sec_desc[AW88395_DATA_TYPE_REG].len =
					aw_bin->header_info[i].valid_data_len;
			prof_desc->sec_desc[AW88395_DATA_TYPE_REG].data =
					data + aw_bin->header_info[i].valid_data_addr;
			break;
		case DATA_TYPE_DSP_REG:
			if (aw_bin->header_info[i].valid_data_len & 0x01)
				return -EINVAL;

			swab16_array((u16 *)(data + aw_bin->header_info[i].valid_data_addr),
					aw_bin->header_info[i].valid_data_len >> 1);

			prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_CFG].len =
					aw_bin->header_info[i].valid_data_len;
			prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_CFG].data =
					data + aw_bin->header_info[i].valid_data_addr;
			break;
		case DATA_TYPE_DSP_FW:
		case DATA_TYPE_SOC_APP:
			if (aw_bin->header_info[i].valid_data_len & 0x01)
				return -EINVAL;

			swab16_array((u16 *)(data + aw_bin->header_info[i].valid_data_addr),
					aw_bin->header_info[i].valid_data_len >> 1);

			prof_desc->fw_ver = aw_bin->header_info[i].app_version;
			prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_FW].len =
					aw_bin->header_info[i].valid_data_len;
			prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_FW].data =
					data + aw_bin->header_info[i].valid_data_addr;
			break;
		default:
			dev_dbg(aw_dev->dev, "bin_data_type not found");
			break;
		}
	}
	prof_desc->prof_st = AW88395_PROFILE_OK;

	return 0;
}

static int aw_dev_parse_reg_bin_with_hdr(struct aw_device *aw_dev,
			uint8_t *data, uint32_t data_len, struct aw_prof_desc *prof_desc)
{
	int ret;

	struct aw_bin *aw_bin __free(kfree) = kzalloc(data_len + sizeof(*aw_bin),
						      GFP_KERNEL);
	if (!aw_bin)
		return -ENOMEM;

	aw_bin->info.len = data_len;
	memcpy(aw_bin->info.data, data, data_len);

	ret = aw_parsing_bin_file(aw_dev, aw_bin);
	if (ret < 0) {
		dev_err(aw_dev->dev, "parse bin failed");
		return ret;
	}

	if ((aw_bin->all_bin_parse_num != 1) ||
		(aw_bin->header_info[0].bin_data_type != DATA_TYPE_REGISTER)) {
		dev_err(aw_dev->dev, "bin num or type error");
		return -EINVAL;
	}

	prof_desc->sec_desc[AW88395_DATA_TYPE_REG].data =
				data + aw_bin->header_info[0].valid_data_addr;
	prof_desc->sec_desc[AW88395_DATA_TYPE_REG].len =
				aw_bin->header_info[0].valid_data_len;
	prof_desc->prof_st = AW88395_PROFILE_OK;

	return 0;
}

static int aw_dev_parse_data_by_sec_type(struct aw_device *aw_dev, struct aw_cfg_hdr *cfg_hdr,
			struct aw_cfg_dde *cfg_dde, struct aw_prof_desc *scene_prof_desc)
{
	switch (cfg_dde->data_type) {
	case ACF_SEC_TYPE_REG:
		return aw_dev_parse_raw_reg((u8 *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_DSP_CFG:
		return aw_dev_parse_raw_dsp_cfg((u8 *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_DSP_FW:
		return aw_dev_parse_raw_dsp_fw(
				(u8 *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_MULTIPLE_BIN:
		return aw_dev_prof_parse_multi_bin(
				aw_dev, (u8 *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	case ACF_SEC_TYPE_HDR_REG:
		return aw_dev_parse_reg_bin_with_hdr(aw_dev, (u8 *)cfg_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, scene_prof_desc);
	default:
		dev_err(aw_dev->dev, "%s cfg_dde->data_type = %d\n", __func__, cfg_dde->data_type);
		break;
	}

	return 0;
}

static int aw_dev_parse_dev_type(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr, struct aw_all_prof_info *all_prof_info)
{
	struct aw_cfg_dde *cfg_dde =
		(struct aw_cfg_dde *)((char *)prof_hdr + prof_hdr->hdr_offset);
	int sec_num = 0;
	int ret, i;

	for (i = 0; i < prof_hdr->ddt_num; i++) {
		if ((aw_dev->i2c->adapter->nr == cfg_dde[i].dev_bus) &&
		    (aw_dev->i2c->addr == cfg_dde[i].dev_addr) &&
		    (cfg_dde[i].type == AW88395_DEV_TYPE_ID) &&
		    (cfg_dde[i].data_type != ACF_SEC_TYPE_MONITOR)) {
			if (cfg_dde[i].dev_profile >= AW88395_PROFILE_MAX) {
				dev_err(aw_dev->dev, "dev_profile [%d] overflow",
							cfg_dde[i].dev_profile);
				return -EINVAL;
			}
			aw_dev->prof_data_type = cfg_dde[i].data_type;
			ret = aw_dev_parse_data_by_sec_type(aw_dev, prof_hdr, &cfg_dde[i],
					&all_prof_info->prof_desc[cfg_dde[i].dev_profile]);
			if (ret < 0) {
				dev_err(aw_dev->dev, "parse failed");
				return ret;
			}
			sec_num++;
		}
	}

	if (sec_num == 0) {
		dev_dbg(aw_dev->dev, "get dev type num is %d, please use default", sec_num);
		return AW88395_DEV_TYPE_NONE;
	}

	return AW88395_DEV_TYPE_OK;
}

static int aw_dev_parse_dev_default_type(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr, struct aw_all_prof_info *all_prof_info)
{
	struct aw_cfg_dde *cfg_dde =
		(struct aw_cfg_dde *)((char *)prof_hdr + prof_hdr->hdr_offset);
	int sec_num = 0;
	int ret, i;

	for (i = 0; i < prof_hdr->ddt_num; i++) {
		if ((aw_dev->channel == cfg_dde[i].dev_index) &&
		    (cfg_dde[i].type == AW88395_DEV_DEFAULT_TYPE_ID) &&
		    (cfg_dde[i].data_type != ACF_SEC_TYPE_MONITOR)) {
			if (cfg_dde[i].dev_profile >= AW88395_PROFILE_MAX) {
				dev_err(aw_dev->dev, "dev_profile [%d] overflow",
					cfg_dde[i].dev_profile);
				return -EINVAL;
			}
			aw_dev->prof_data_type = cfg_dde[i].data_type;
			ret = aw_dev_parse_data_by_sec_type(aw_dev, prof_hdr, &cfg_dde[i],
					&all_prof_info->prof_desc[cfg_dde[i].dev_profile]);
			if (ret < 0) {
				dev_err(aw_dev->dev, "parse failed");
				return ret;
			}
			sec_num++;
		}
	}

	if (sec_num == 0) {
		dev_err(aw_dev->dev, "get dev default type failed, get num[%d]", sec_num);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_cfg_get_reg_valid_prof(struct aw_device *aw_dev,
				struct aw_all_prof_info *all_prof_info)
{
	struct aw_prof_desc *prof_desc = all_prof_info->prof_desc;
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	int num = 0;
	int i;

	for (i = 0; i < AW88395_PROFILE_MAX; i++) {
		if (prof_desc[i].prof_st == AW88395_PROFILE_OK)
			prof_info->count++;
	}

	dev_dbg(aw_dev->dev, "get valid profile:%d", aw_dev->prof_info.count);

	if (!prof_info->count) {
		dev_err(aw_dev->dev, "no profile data");
		return -EPERM;
	}

	prof_info->prof_desc = devm_kcalloc(aw_dev->dev,
					prof_info->count, sizeof(struct aw_prof_desc),
					GFP_KERNEL);
	if (!prof_info->prof_desc)
		return -ENOMEM;

	for (i = 0; i < AW88395_PROFILE_MAX; i++) {
		if (prof_desc[i].prof_st == AW88395_PROFILE_OK) {
			if (num >= prof_info->count) {
				dev_err(aw_dev->dev, "overflow count[%d]",
						prof_info->count);
				return -EINVAL;
			}
			prof_info->prof_desc[num] = prof_desc[i];
			prof_info->prof_desc[num].id = i;
			num++;
		}
	}

	return 0;
}

static int aw_dev_cfg_get_multiple_valid_prof(struct aw_device *aw_dev,
				struct aw_all_prof_info *all_prof_info)
{
	struct aw_prof_desc *prof_desc = all_prof_info->prof_desc;
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_sec_data_desc *sec_desc;
	int num = 0;
	int i;

	for (i = 0; i < AW88395_PROFILE_MAX; i++) {
		if (prof_desc[i].prof_st == AW88395_PROFILE_OK) {
			sec_desc = prof_desc[i].sec_desc;
			if ((sec_desc[AW88395_DATA_TYPE_REG].data != NULL) &&
			    (sec_desc[AW88395_DATA_TYPE_REG].len != 0) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_CFG].data != NULL) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_CFG].len != 0) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_FW].data != NULL) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_FW].len != 0))
				prof_info->count++;
		}
	}

	dev_dbg(aw_dev->dev, "get valid profile:%d", aw_dev->prof_info.count);

	if (!prof_info->count) {
		dev_err(aw_dev->dev, "no profile data");
		return -EPERM;
	}

	prof_info->prof_desc = devm_kcalloc(aw_dev->dev,
					prof_info->count, sizeof(struct aw_prof_desc),
					GFP_KERNEL);
	if (!prof_info->prof_desc)
		return -ENOMEM;

	for (i = 0; i < AW88395_PROFILE_MAX; i++) {
		if (prof_desc[i].prof_st == AW88395_PROFILE_OK) {
			sec_desc = prof_desc[i].sec_desc;
			if ((sec_desc[AW88395_DATA_TYPE_REG].data != NULL) &&
			    (sec_desc[AW88395_DATA_TYPE_REG].len != 0) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_CFG].data != NULL) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_CFG].len != 0) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_FW].data != NULL) &&
			    (sec_desc[AW88395_DATA_TYPE_DSP_FW].len != 0)) {
				if (num >= prof_info->count) {
					dev_err(aw_dev->dev, "overflow count[%d]",
							prof_info->count);
					return -EINVAL;
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

	struct aw_all_prof_info *all_prof_info __free(kfree) = kzalloc(sizeof(*all_prof_info),
								       GFP_KERNEL);
	if (!all_prof_info)
		return -ENOMEM;

	ret = aw_dev_parse_dev_type(aw_dev, prof_hdr, all_prof_info);
	if (ret < 0) {
		return ret;
	} else if (ret == AW88395_DEV_TYPE_NONE) {
		dev_dbg(aw_dev->dev, "get dev type num is 0, parse default dev");
		ret = aw_dev_parse_dev_default_type(aw_dev, prof_hdr, all_prof_info);
		if (ret < 0)
			return ret;
	}

	switch (aw_dev->prof_data_type) {
	case ACF_SEC_TYPE_MULTIPLE_BIN:
		ret = aw_dev_cfg_get_multiple_valid_prof(aw_dev, all_prof_info);
		break;
	case ACF_SEC_TYPE_HDR_REG:
		ret = aw_dev_cfg_get_reg_valid_prof(aw_dev, all_prof_info);
		break;
	default:
		dev_err(aw_dev->dev, "unsupport data type\n");
		ret = -EINVAL;
		break;
	}
	if (!ret)
		aw_dev->prof_info.prof_name_list = profile_name;

	return ret;
}

static int aw_dev_create_prof_name_list_v1(struct aw_device *aw_dev)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_prof_desc *prof_desc = prof_info->prof_desc;
	int i;

	if (!prof_desc) {
		dev_err(aw_dev->dev, "prof_desc is NULL");
		return -EINVAL;
	}

	prof_info->prof_name_list = devm_kzalloc(aw_dev->dev,
					prof_info->count * PROFILE_STR_MAX,
					GFP_KERNEL);
	if (!prof_info->prof_name_list)
		return -ENOMEM;

	for (i = 0; i < prof_info->count; i++) {
		prof_desc[i].id = i;
		prof_info->prof_name_list[i] = prof_desc[i].prf_str;
		dev_dbg(aw_dev->dev, "prof name is %s", prof_info->prof_name_list[i]);
	}

	return 0;
}

static int aw_get_dde_type_info(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_cfg_dde_v1 *cfg_dde =
		(struct aw_cfg_dde_v1 *)(aw_cfg->data + cfg_hdr->hdr_offset);
	int default_num = 0;
	int dev_num = 0;
	unsigned int i;

	for (i = 0; i < cfg_hdr->ddt_num; i++) {
		if (cfg_dde[i].type == AW88395_DEV_TYPE_ID)
			dev_num++;

		if (cfg_dde[i].type == AW88395_DEV_DEFAULT_TYPE_ID)
			default_num++;
	}

	if (dev_num != 0) {
		aw_dev->prof_info.prof_type = AW88395_DEV_TYPE_ID;
	} else if (default_num != 0) {
		aw_dev->prof_info.prof_type = AW88395_DEV_DEFAULT_TYPE_ID;
	} else {
		dev_err(aw_dev->dev, "can't find scene");
		return -EINVAL;
	}

	return 0;
}

static int aw_get_dev_scene_count_v1(struct aw_device *aw_dev, struct aw_container *aw_cfg,
						unsigned int *scene_num)
{
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_cfg_dde_v1 *cfg_dde =
		(struct aw_cfg_dde_v1 *)(aw_cfg->data + cfg_hdr->hdr_offset);
	unsigned int i;

	for (i = 0; i < cfg_hdr->ddt_num; ++i) {
		if (((cfg_dde[i].data_type == ACF_SEC_TYPE_REG) ||
		     (cfg_dde[i].data_type == ACF_SEC_TYPE_HDR_REG) ||
		     (cfg_dde[i].data_type == ACF_SEC_TYPE_MULTIPLE_BIN)) &&
		    (aw_dev->chip_id == cfg_dde[i].chip_id) &&
		    (aw_dev->i2c->adapter->nr == cfg_dde[i].dev_bus) &&
		    (aw_dev->i2c->addr == cfg_dde[i].dev_addr))
			(*scene_num)++;
	}

	if ((*scene_num) == 0) {
		dev_err(aw_dev->dev, "failed to obtain scene, scenu_num = %d\n", (*scene_num));
		return -EINVAL;
	}

	return 0;
}

static int aw_get_default_scene_count_v1(struct aw_device *aw_dev,
						struct aw_container *aw_cfg,
						unsigned int *scene_num)
{
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_cfg_dde_v1 *cfg_dde =
		(struct aw_cfg_dde_v1 *)(aw_cfg->data + cfg_hdr->hdr_offset);
	unsigned int i;


	for (i = 0; i < cfg_hdr->ddt_num; ++i) {
		if (((cfg_dde[i].data_type == ACF_SEC_TYPE_MULTIPLE_BIN) ||
		     (cfg_dde[i].data_type == ACF_SEC_TYPE_REG) ||
		     (cfg_dde[i].data_type == ACF_SEC_TYPE_HDR_REG)) &&
		    (aw_dev->chip_id == cfg_dde[i].chip_id) &&
		    (aw_dev->channel == cfg_dde[i].dev_index))
			(*scene_num)++;
	}

	if ((*scene_num) == 0) {
		dev_err(aw_dev->dev, "failed to obtain scene, scenu_num = %d\n", (*scene_num));
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_scene_count_v1(struct aw_device *aw_dev,
							struct aw_container *aw_cfg,
							unsigned int *count)
{
	int ret;

	ret = aw_get_dde_type_info(aw_dev, aw_cfg);
	if (ret < 0)
		return ret;

	switch (aw_dev->prof_info.prof_type) {
	case AW88395_DEV_TYPE_ID:
		ret = aw_get_dev_scene_count_v1(aw_dev, aw_cfg, count);
		break;
	case AW88395_DEV_DEFAULT_TYPE_ID:
		ret = aw_get_default_scene_count_v1(aw_dev, aw_cfg, count);
		break;
	default:
		dev_err(aw_dev->dev, "unsupported prof_type[%x]", aw_dev->prof_info.prof_type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int aw_dev_parse_data_by_sec_type_v1(struct aw_device *aw_dev,
							struct aw_cfg_hdr *prof_hdr,
							struct aw_cfg_dde_v1 *cfg_dde,
							int *cur_scene_id)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	int ret;

	switch (cfg_dde->data_type) {
	case ACF_SEC_TYPE_MULTIPLE_BIN:
		ret = aw_dev_prof_parse_multi_bin(aw_dev, (u8 *)prof_hdr + cfg_dde->data_offset,
					cfg_dde->data_size, &prof_info->prof_desc[*cur_scene_id]);
		if (ret < 0) {
			dev_err(aw_dev->dev, "parse multi bin failed");
			return ret;
		}
		prof_info->prof_desc[*cur_scene_id].prf_str = cfg_dde->dev_profile_str;
		prof_info->prof_desc[*cur_scene_id].id = cfg_dde->dev_profile;
		(*cur_scene_id)++;
		break;
	case ACF_SEC_TYPE_HDR_REG:
		ret =  aw_dev_parse_reg_bin_with_hdr(aw_dev,
				(uint8_t *)prof_hdr + cfg_dde->data_offset,
				cfg_dde->data_size, &prof_info->prof_desc[*cur_scene_id]);
		if (ret < 0) {
			dev_err(aw_dev->dev, "parse reg bin with hdr failed");
			return ret;
		}
		prof_info->prof_desc[*cur_scene_id].prf_str = cfg_dde->dev_profile_str;
		prof_info->prof_desc[*cur_scene_id].id = cfg_dde->dev_profile;
		(*cur_scene_id)++;
		break;
	default:
		dev_err(aw_dev->dev, "unsupported SEC_TYPE [%d]", cfg_dde->data_type);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_dev_type_v1(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr)
{
	struct aw_cfg_dde_v1 *cfg_dde =
		(struct aw_cfg_dde_v1 *)((char *)prof_hdr + prof_hdr->hdr_offset);
	int cur_scene_id = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < prof_hdr->ddt_num; i++) {
		if ((aw_dev->i2c->adapter->nr == cfg_dde[i].dev_bus) &&
		    (aw_dev->i2c->addr == cfg_dde[i].dev_addr) &&
		    (aw_dev->chip_id == cfg_dde[i].chip_id)) {
			ret = aw_dev_parse_data_by_sec_type_v1(aw_dev, prof_hdr,
							&cfg_dde[i], &cur_scene_id);
			if (ret < 0) {
				dev_err(aw_dev->dev, "parse failed");
				return ret;
			}
		}
	}

	if (cur_scene_id == 0) {
		dev_err(aw_dev->dev, "get dev type failed, get num [%d]", cur_scene_id);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_default_type_v1(struct aw_device *aw_dev,
		struct aw_cfg_hdr *prof_hdr)
{
	struct aw_cfg_dde_v1 *cfg_dde =
		(struct aw_cfg_dde_v1 *)((char *)prof_hdr + prof_hdr->hdr_offset);
	int cur_scene_id = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < prof_hdr->ddt_num; i++) {
		if ((aw_dev->channel == cfg_dde[i].dev_index) &&
			(aw_dev->chip_id == cfg_dde[i].chip_id)) {
			ret = aw_dev_parse_data_by_sec_type_v1(aw_dev, prof_hdr,
							&cfg_dde[i], &cur_scene_id);
			if (ret < 0) {
				dev_err(aw_dev->dev, "parse failed");
				return ret;
			}
		}
	}

	if (cur_scene_id == 0) {
		dev_err(aw_dev->dev, "get dev default type failed, get num[%d]", cur_scene_id);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_parse_by_hdr_v1(struct aw_device *aw_dev,
		struct aw_cfg_hdr *cfg_hdr)
{
	int ret;

	switch (aw_dev->prof_info.prof_type) {
	case AW88395_DEV_TYPE_ID:
		ret = aw_dev_parse_dev_type_v1(aw_dev, cfg_hdr);
		break;
	case AW88395_DEV_DEFAULT_TYPE_ID:
		ret = aw_dev_parse_default_type_v1(aw_dev, cfg_hdr);
		break;
	default:
		dev_err(aw_dev->dev, "prof type matched failed, get num[%d]",
			aw_dev->prof_info.prof_type);
		ret =  -EINVAL;
		break;
	}

	return ret;
}

static int aw_dev_load_cfg_by_hdr_v1(struct aw_device *aw_dev,
						struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	int ret;

	ret = aw_dev_parse_scene_count_v1(aw_dev, aw_cfg, &prof_info->count);
	if (ret < 0) {
		dev_err(aw_dev->dev, "get scene count failed");
		return ret;
	}

	prof_info->prof_desc = devm_kcalloc(aw_dev->dev,
					prof_info->count, sizeof(struct aw_prof_desc),
					GFP_KERNEL);
	if (!prof_info->prof_desc)
		return -ENOMEM;

	ret = aw_dev_parse_by_hdr_v1(aw_dev, cfg_hdr);
	if (ret < 0) {
		dev_err(aw_dev->dev, "parse hdr failed");
		return ret;
	}

	ret = aw_dev_create_prof_name_list_v1(aw_dev);
	if (ret < 0) {
		dev_err(aw_dev->dev, "create prof name list failed");
		return ret;
	}

	return 0;
}

int aw88395_dev_cfg_load(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr;
	int ret;

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;

	switch (cfg_hdr->hdr_version) {
	case AW88395_CFG_HDR_VER:
		ret = aw_dev_load_cfg_by_hdr(aw_dev, cfg_hdr);
		if (ret < 0) {
			dev_err(aw_dev->dev, "hdr_version[0x%x] parse failed",
						cfg_hdr->hdr_version);
			return ret;
		}
		break;
	case AW88395_CFG_HDR_VER_V1:
		ret = aw_dev_load_cfg_by_hdr_v1(aw_dev, aw_cfg);
		if (ret < 0) {
			dev_err(aw_dev->dev, "hdr_version[0x%x] parse failed",
						cfg_hdr->hdr_version);
			return ret;
		}
		break;
	default:
		dev_err(aw_dev->dev, "unsupported hdr_version [0x%x]", cfg_hdr->hdr_version);
		return -EINVAL;
	}
	aw_dev->fw_status = AW88395_DEV_FW_OK;

	return 0;
}
EXPORT_SYMBOL_GPL(aw88395_dev_cfg_load);

static int aw_dev_check_cfg_by_hdr(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	unsigned int end_data_offset;
	struct aw_cfg_hdr *cfg_hdr;
	struct aw_cfg_dde *cfg_dde;
	unsigned int act_data = 0;
	unsigned int hdr_ddt_len;
	unsigned int i;
	u8 act_crc8;

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	/* check file type id is awinic acf file */
	if (cfg_hdr->id != ACF_FILE_ID) {
		dev_err(aw_dev->dev, "not acf type file");
		return -EINVAL;
	}

	hdr_ddt_len = cfg_hdr->hdr_offset + cfg_hdr->ddt_size;
	if (hdr_ddt_len > aw_cfg->len) {
		dev_err(aw_dev->dev, "hdr_len with ddt_len [%d] overflow file size[%d]",
		cfg_hdr->hdr_offset, aw_cfg->len);
		return -EINVAL;
	}

	/* check data size */
	cfg_dde = (struct aw_cfg_dde *)((char *)aw_cfg->data + cfg_hdr->hdr_offset);
	act_data += hdr_ddt_len;
	for (i = 0; i < cfg_hdr->ddt_num; i++)
		act_data += cfg_dde[i].data_size;

	if (act_data != aw_cfg->len) {
		dev_err(aw_dev->dev, "act_data[%d] not equal to file size[%d]!",
			act_data, aw_cfg->len);
		return -EINVAL;
	}

	for (i = 0; i < cfg_hdr->ddt_num; i++) {
		/* data check */
		end_data_offset = cfg_dde[i].data_offset + cfg_dde[i].data_size;
		if (end_data_offset > aw_cfg->len) {
			dev_err(aw_dev->dev, "ddt_num[%d] end_data_offset[%d] overflow size[%d]",
				i, end_data_offset, aw_cfg->len);
			return -EINVAL;
		}

		/* crc check */
		act_crc8 = crc8(aw_crc8_table, aw_cfg->data + cfg_dde[i].data_offset,
							cfg_dde[i].data_size, 0);
		if (act_crc8 != cfg_dde[i].data_crc) {
			dev_err(aw_dev->dev, "ddt_num[%d] act_crc8:0x%x != data_crc:0x%x",
				i, (u32)act_crc8, cfg_dde[i].data_crc);
			return -EINVAL;
		}
	}

	return 0;
}

static int aw_dev_check_acf_by_hdr_v1(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	struct aw_cfg_dde_v1 *cfg_dde;
	unsigned int end_data_offset;
	struct aw_cfg_hdr *cfg_hdr;
	unsigned int act_data = 0;
	unsigned int hdr_ddt_len;
	u8 act_crc8;
	int i;

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;

	/* check file type id is awinic acf file */
	if (cfg_hdr->id != ACF_FILE_ID) {
		dev_err(aw_dev->dev, "not acf type file");
		return -EINVAL;
	}

	hdr_ddt_len = cfg_hdr->hdr_offset + cfg_hdr->ddt_size;
	if (hdr_ddt_len > aw_cfg->len) {
		dev_err(aw_dev->dev, "hdrlen with ddt_len [%d] overflow file size[%d]",
		cfg_hdr->hdr_offset, aw_cfg->len);
		return -EINVAL;
	}

	/* check data size */
	cfg_dde = (struct aw_cfg_dde_v1 *)((char *)aw_cfg->data + cfg_hdr->hdr_offset);
	act_data += hdr_ddt_len;
	for (i = 0; i < cfg_hdr->ddt_num; i++)
		act_data += cfg_dde[i].data_size;

	if (act_data != aw_cfg->len) {
		dev_err(aw_dev->dev, "act_data[%d] not equal to file size[%d]!",
			act_data, aw_cfg->len);
		return -EINVAL;
	}

	for (i = 0; i < cfg_hdr->ddt_num; i++) {
		/* data check */
		end_data_offset = cfg_dde[i].data_offset + cfg_dde[i].data_size;
		if (end_data_offset > aw_cfg->len) {
			dev_err(aw_dev->dev, "ddt_num[%d] end_data_offset[%d] overflow size[%d]",
				i, end_data_offset, aw_cfg->len);
			return -EINVAL;
		}

		/* crc check */
		act_crc8 = crc8(aw_crc8_table, aw_cfg->data + cfg_dde[i].data_offset,
									cfg_dde[i].data_size, 0);
		if (act_crc8 != cfg_dde[i].data_crc) {
			dev_err(aw_dev->dev, "ddt_num[%d] act_crc8:0x%x != data_crc 0x%x",
				i, (u32)act_crc8, cfg_dde[i].data_crc);
			return -EINVAL;
		}
	}

	return 0;
}

int aw88395_dev_load_acf_check(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	struct aw_cfg_hdr *cfg_hdr;

	if (!aw_cfg) {
		dev_err(aw_dev->dev, "aw_prof is NULL");
		return -EINVAL;
	}

	if (aw_cfg->len < sizeof(struct aw_cfg_hdr)) {
		dev_err(aw_dev->dev, "cfg hdr size[%d] overflow file size[%d]",
			aw_cfg->len, (int)sizeof(struct aw_cfg_hdr));
		return -EINVAL;
	}

	crc8_populate_lsb(aw_crc8_table, AW88395_CRC8_POLYNOMIAL);

	cfg_hdr = (struct aw_cfg_hdr *)aw_cfg->data;
	switch (cfg_hdr->hdr_version) {
	case AW88395_CFG_HDR_VER:
		return aw_dev_check_cfg_by_hdr(aw_dev, aw_cfg);
	case AW88395_CFG_HDR_VER_V1:
		return aw_dev_check_acf_by_hdr_v1(aw_dev, aw_cfg);
	default:
		dev_err(aw_dev->dev, "unsupported hdr_version [0x%x]", cfg_hdr->hdr_version);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(aw88395_dev_load_acf_check);

MODULE_DESCRIPTION("AW88395 ACF File Parsing Lib");
MODULE_LICENSE("GPL v2");
