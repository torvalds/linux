// SPDX-License-Identifier: GPL-2.0
//
// TAS2781 HDA SPI driver
//
// Copyright 2024 - 2025 Texas Instruments, Inc.
//
// Author: Baojun Xu <baojun.xu@ti.com>

#include <linux/crc8.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tas2781-dsp.h>
#include <sound/tlv.h>

#include "tas2781-spi.h"

#define OFFSET_ERROR_BIT			BIT(31)

#define ERROR_PRAM_CRCCHK			0x0000000
#define ERROR_YRAM_CRCCHK			0x0000001
#define PPC_DRIVER_CRCCHK			0x00000200

#define TAS2781_SA_COEFF_SWAP_REG		TASDEVICE_REG(0, 0x35, 0x2c)
#define TAS2781_YRAM_BOOK1			140
#define TAS2781_YRAM1_PAGE			42
#define TAS2781_YRAM1_START_REG			88

#define TAS2781_YRAM2_START_PAGE		43
#define TAS2781_YRAM2_END_PAGE			49
#define TAS2781_YRAM2_START_REG			8
#define TAS2781_YRAM2_END_REG			127

#define TAS2781_YRAM3_PAGE			50
#define TAS2781_YRAM3_START_REG			8
#define TAS2781_YRAM3_END_REG			27

/* should not include B0_P53_R44-R47 */
#define TAS2781_YRAM_BOOK2			0
#define TAS2781_YRAM4_START_PAGE		50
#define TAS2781_YRAM4_END_PAGE			60

#define TAS2781_YRAM5_PAGE			61
#define TAS2781_YRAM5_START_REG			TAS2781_YRAM3_START_REG
#define TAS2781_YRAM5_END_REG			TAS2781_YRAM3_END_REG

#define TASDEVICE_MAXPROGRAM_NUM_KERNEL			5
#define TASDEVICE_MAXCONFIG_NUM_KERNEL_MULTIPLE_AMPS	64
#define TASDEVICE_MAXCONFIG_NUM_KERNEL			10
#define MAIN_ALL_DEVICES_1X				0x01
#define MAIN_DEVICE_A_1X				0x02
#define MAIN_DEVICE_B_1X				0x03
#define MAIN_DEVICE_C_1X				0x04
#define MAIN_DEVICE_D_1X				0x05
#define COEFF_DEVICE_A_1X				0x12
#define COEFF_DEVICE_B_1X				0x13
#define COEFF_DEVICE_C_1X				0x14
#define COEFF_DEVICE_D_1X				0x15
#define PRE_DEVICE_A_1X					0x22
#define PRE_DEVICE_B_1X					0x23
#define PRE_DEVICE_C_1X					0x24
#define PRE_DEVICE_D_1X					0x25
#define PRE_SOFTWARE_RESET_DEVICE_A			0x41
#define PRE_SOFTWARE_RESET_DEVICE_B			0x42
#define PRE_SOFTWARE_RESET_DEVICE_C			0x43
#define PRE_SOFTWARE_RESET_DEVICE_D			0x44
#define POST_SOFTWARE_RESET_DEVICE_A			0x45
#define POST_SOFTWARE_RESET_DEVICE_B			0x46
#define POST_SOFTWARE_RESET_DEVICE_C			0x47
#define POST_SOFTWARE_RESET_DEVICE_D			0x48

struct tas_crc {
	unsigned char offset;
	unsigned char len;
};

struct blktyp_devidx_map {
	unsigned char blktyp;
	unsigned char dev_idx;
};

/* fixed m68k compiling issue: mapping table can save code field */
static const struct blktyp_devidx_map ppc3_tas2781_mapping_table[] = {
	{ MAIN_ALL_DEVICES_1X, 0x80 },
	{ MAIN_DEVICE_A_1X, 0x81 },
	{ COEFF_DEVICE_A_1X, 0x81 },
	{ PRE_DEVICE_A_1X, 0x81 },
	{ PRE_SOFTWARE_RESET_DEVICE_A, 0xC1 },
	{ POST_SOFTWARE_RESET_DEVICE_A, 0xC1 },
	{ MAIN_DEVICE_B_1X, 0x82 },
	{ COEFF_DEVICE_B_1X, 0x82 },
	{ PRE_DEVICE_B_1X, 0x82 },
	{ PRE_SOFTWARE_RESET_DEVICE_B, 0xC2 },
	{ POST_SOFTWARE_RESET_DEVICE_B, 0xC2 },
	{ MAIN_DEVICE_C_1X, 0x83 },
	{ COEFF_DEVICE_C_1X, 0x83 },
	{ PRE_DEVICE_C_1X, 0x83 },
	{ PRE_SOFTWARE_RESET_DEVICE_C, 0xC3 },
	{ POST_SOFTWARE_RESET_DEVICE_C, 0xC3 },
	{ MAIN_DEVICE_D_1X, 0x84 },
	{ COEFF_DEVICE_D_1X, 0x84 },
	{ PRE_DEVICE_D_1X, 0x84 },
	{ PRE_SOFTWARE_RESET_DEVICE_D, 0xC4 },
	{ POST_SOFTWARE_RESET_DEVICE_D, 0xC4 },
};

static const struct blktyp_devidx_map ppc3_mapping_table[] = {
	{ MAIN_ALL_DEVICES_1X, 0x80 },
	{ MAIN_DEVICE_A_1X, 0x81 },
	{ COEFF_DEVICE_A_1X, 0xC1 },
	{ PRE_DEVICE_A_1X, 0xC1 },
	{ MAIN_DEVICE_B_1X, 0x82 },
	{ COEFF_DEVICE_B_1X, 0xC2 },
	{ PRE_DEVICE_B_1X, 0xC2 },
	{ MAIN_DEVICE_C_1X, 0x83 },
	{ COEFF_DEVICE_C_1X, 0xC3 },
	{ PRE_DEVICE_C_1X, 0xC3 },
	{ MAIN_DEVICE_D_1X, 0x84 },
	{ COEFF_DEVICE_D_1X, 0xC4 },
	{ PRE_DEVICE_D_1X, 0xC4 },
};

static const struct blktyp_devidx_map non_ppc3_mapping_table[] = {
	{ MAIN_ALL_DEVICES, 0x80 },
	{ MAIN_DEVICE_A, 0x81 },
	{ COEFF_DEVICE_A, 0xC1 },
	{ PRE_DEVICE_A, 0xC1 },
	{ MAIN_DEVICE_B, 0x82 },
	{ COEFF_DEVICE_B, 0xC2 },
	{ PRE_DEVICE_B, 0xC2 },
	{ MAIN_DEVICE_C, 0x83 },
	{ COEFF_DEVICE_C, 0xC3 },
	{ PRE_DEVICE_C, 0xC3 },
	{ MAIN_DEVICE_D, 0x84 },
	{ COEFF_DEVICE_D, 0xC4 },
	{ PRE_DEVICE_D, 0xC4 },
};

/*
 * Device support different configurations for different scene,
 * like voice, music, calibration, was write in regbin file.
 * Will be stored into tas_priv after regbin was loaded.
 */
static struct tasdevice_config_info *tasdevice_add_config(
	struct tasdevice_priv *tas_priv, unsigned char *config_data,
	unsigned int config_size, int *status)
{
	struct tasdevice_config_info *cfg_info;
	struct tasdev_blk_data **bk_da;
	unsigned int config_offset = 0;
	unsigned int i;

	/*
	 * In most projects are many audio cases, such as music, handfree,
	 * receiver, games, audio-to-haptics, PMIC record, bypass mode,
	 * portrait, landscape, etc. Even in multiple audios, one or
	 * two of the chips will work for the special case, such as
	 * ultrasonic application. In order to support these variable-numbers
	 * of audio cases, flexible configs have been introduced in the
	 * DSP firmware.
	 */
	cfg_info = kzalloc(sizeof(*cfg_info), GFP_KERNEL);
	if (!cfg_info) {
		*status = -ENOMEM;
		return NULL;
	}

	if (tas_priv->rcabin.fw_hdr.binary_version_num >= 0x105) {
		if ((config_offset + 64) > config_size) {
			*status = -EINVAL;
			dev_err(tas_priv->dev, "add conf: Out of boundary\n");
			goto config_err;
		}
		config_offset += 64;
	}

	if ((config_offset + 4) > config_size) {
		*status = -EINVAL;
		dev_err(tas_priv->dev, "add config: Out of boundary\n");
		goto config_err;
	}

	/*
	 * convert data[offset], data[offset + 1], data[offset + 2] and
	 * data[offset + 3] into host
	 */
	cfg_info->nblocks = get_unaligned_be32(&config_data[config_offset]);
	config_offset += 4;

	/*
	 * Several kinds of dsp/algorithm firmwares can run on tas2781,
	 * the number and size of blk are not fixed and different among
	 * these firmwares.
	 */
	bk_da = cfg_info->blk_data = kcalloc(cfg_info->nblocks,
		sizeof(*bk_da), GFP_KERNEL);
	if (!bk_da) {
		*status = -ENOMEM;
		goto config_err;
	}
	cfg_info->real_nblocks = 0;
	for (i = 0; i < cfg_info->nblocks; i++) {
		if (config_offset + 12 > config_size) {
			*status = -EINVAL;
			dev_err(tas_priv->dev,
				"%s: Out of boundary: i = %d nblocks = %u!\n",
				__func__, i, cfg_info->nblocks);
			goto block_err;
		}
		bk_da[i] = kzalloc(sizeof(*bk_da[i]), GFP_KERNEL);
		if (!bk_da[i]) {
			*status = -ENOMEM;
			goto block_err;
		}

		bk_da[i]->dev_idx = config_data[config_offset];
		config_offset++;

		bk_da[i]->block_type = config_data[config_offset];
		config_offset++;

		bk_da[i]->yram_checksum =
			get_unaligned_be16(&config_data[config_offset]);
		config_offset += 2;
		bk_da[i]->block_size =
			get_unaligned_be32(&config_data[config_offset]);
		config_offset += 4;

		bk_da[i]->n_subblks =
			get_unaligned_be32(&config_data[config_offset]);

		config_offset += 4;

		if (config_offset + bk_da[i]->block_size > config_size) {
			*status = -EINVAL;
			dev_err(tas_priv->dev,
				"%s: Out of boundary: i = %d blks = %u!\n",
				__func__, i, cfg_info->nblocks);
			goto block_err;
		}
		/* instead of kzalloc+memcpy */
		bk_da[i]->regdata = kmemdup(&config_data[config_offset],
			bk_da[i]->block_size, GFP_KERNEL);
		if (!bk_da[i]->regdata) {
			*status = -ENOMEM;
			i++;
			goto block_err;
		}

		config_offset += bk_da[i]->block_size;
		cfg_info->real_nblocks += 1;
	}

	return cfg_info;
block_err:
	for (int j = 0; j < i; j++)
		kfree(bk_da[j]);
	kfree(bk_da);
config_err:
	kfree(cfg_info);
	return NULL;
}

/* Regbin file parser function. */
int tasdevice_spi_rca_parser(void *context, const struct firmware *fmw)
{
	struct tasdevice_priv *tas_priv = context;
	struct tasdevice_config_info **cfg_info;
	struct tasdevice_rca_hdr *fw_hdr;
	struct tasdevice_rca *rca;
	unsigned int total_config_sz = 0;
	int offset = 0, ret = 0, i;
	unsigned char *buf;

	rca = &tas_priv->rcabin;
	fw_hdr = &rca->fw_hdr;
	if (!fmw || !fmw->data) {
		dev_err(tas_priv->dev, "Failed to read %s\n",
			tas_priv->rca_binaryname);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return -EINVAL;
	}
	buf = (unsigned char *)fmw->data;
	fw_hdr->img_sz = get_unaligned_be32(&buf[offset]);
	offset += 4;
	if (fw_hdr->img_sz != fmw->size) {
		dev_err(tas_priv->dev,
			"File size not match, %d %u", (int)fmw->size,
			fw_hdr->img_sz);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return -EINVAL;
	}

	fw_hdr->checksum = get_unaligned_be32(&buf[offset]);
	offset += 4;
	fw_hdr->binary_version_num = get_unaligned_be32(&buf[offset]);
	if (fw_hdr->binary_version_num < 0x103) {
		dev_err(tas_priv->dev, "File version 0x%04x is too low",
			fw_hdr->binary_version_num);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return -EINVAL;
	}
	offset += 4;
	fw_hdr->drv_fw_version = get_unaligned_be32(&buf[offset]);
	offset += 8;
	fw_hdr->plat_type = buf[offset++];
	fw_hdr->dev_family = buf[offset++];
	fw_hdr->reserve = buf[offset++];
	fw_hdr->ndev = buf[offset++];
	if (offset + TASDEVICE_DEVICE_SUM > fw_hdr->img_sz) {
		dev_err(tas_priv->dev, "rca_ready: Out of boundary!\n");
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return -EINVAL;
	}

	for (i = 0; i < TASDEVICE_DEVICE_SUM; i++, offset++)
		fw_hdr->devs[i] = buf[offset];

	fw_hdr->nconfig = get_unaligned_be32(&buf[offset]);
	offset += 4;

	for (i = 0; i < TASDEVICE_CONFIG_SUM; i++) {
		fw_hdr->config_size[i] = get_unaligned_be32(&buf[offset]);
		offset += 4;
		total_config_sz += fw_hdr->config_size[i];
	}

	if (fw_hdr->img_sz - total_config_sz != (unsigned int)offset) {
		dev_err(tas_priv->dev, "Bin file err %d - %d != %d!\n",
			fw_hdr->img_sz, total_config_sz, (int)offset);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return -EINVAL;
	}

	cfg_info = kcalloc(fw_hdr->nconfig, sizeof(*cfg_info), GFP_KERNEL);
	if (!cfg_info) {
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return -ENOMEM;
	}
	rca->cfg_info = cfg_info;
	rca->ncfgs = 0;
	for (i = 0; i < (int)fw_hdr->nconfig; i++) {
		rca->ncfgs += 1;
		cfg_info[i] = tasdevice_add_config(tas_priv, &buf[offset],
			fw_hdr->config_size[i], &ret);
		if (ret) {
			tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
			return ret;
		}
		offset += (int)fw_hdr->config_size[i];
	}

	return ret;
}

/* fixed m68k compiling issue: mapping table can save code field */
static unsigned char map_dev_idx(struct tasdevice_fw *tas_fmw,
	struct tasdev_blk *block)
{
	struct blktyp_devidx_map *p =
		(struct blktyp_devidx_map *)non_ppc3_mapping_table;
	struct tasdevice_dspfw_hdr *fw_hdr = &tas_fmw->fw_hdr;
	struct tasdevice_fw_fixed_hdr *fw_fixed_hdr = &fw_hdr->fixed_hdr;
	int i, n = ARRAY_SIZE(non_ppc3_mapping_table);
	unsigned char dev_idx = 0;

	if (fw_fixed_hdr->ppcver >= PPC3_VERSION_TAS2781_BASIC_MIN) {
		p = (struct blktyp_devidx_map *)ppc3_tas2781_mapping_table;
		n = ARRAY_SIZE(ppc3_tas2781_mapping_table);
	} else if (fw_fixed_hdr->ppcver >= PPC3_VERSION_BASE) {
		p = (struct blktyp_devidx_map *)ppc3_mapping_table;
		n = ARRAY_SIZE(ppc3_mapping_table);
	}

	for (i = 0; i < n; i++) {
		if (block->type == p[i].blktyp) {
			dev_idx = p[i].dev_idx;
			break;
		}
	}

	return dev_idx;
}

/* Block parser function. */
static int fw_parse_block_data_kernel(struct tasdevice_fw *tas_fmw,
	struct tasdev_blk *block, const struct firmware *fmw, int offset)
{
	const unsigned char *data = fmw->data;

	if (offset + 16 > fmw->size) {
		dev_err(tas_fmw->dev, "%s: File Size error\n", __func__);
		return -EINVAL;
	}

	/*
	 * Convert data[offset], data[offset + 1], data[offset + 2] and
	 * data[offset + 3] into host.
	 */
	block->type = get_unaligned_be32(&data[offset]);
	offset += 4;

	block->is_pchksum_present = data[offset++];
	block->pchksum = data[offset++];
	block->is_ychksum_present = data[offset++];
	block->ychksum = data[offset++];
	block->blk_size = get_unaligned_be32(&data[offset]);
	offset += 4;
	block->nr_subblocks = get_unaligned_be32(&data[offset]);
	offset += 4;

	/*
	 * Fixed m68k compiling issue:
	 * 1. mapping table can save code field.
	 * 2. storing the dev_idx as a member of block can reduce unnecessary
	 *    time and system resource comsumption of dev_idx mapping every
	 *    time the block data writing to the dsp.
	 */
	block->dev_idx = map_dev_idx(tas_fmw, block);

	if (offset + block->blk_size > fmw->size) {
		dev_err(tas_fmw->dev, "%s: nSublocks error\n", __func__);
		return -EINVAL;
	}
	/* instead of kzalloc+memcpy */
	block->data = kmemdup(&data[offset], block->blk_size, GFP_KERNEL);
	if (!block->data)
		return -ENOMEM;

	offset += block->blk_size;

	return offset;
}

/* Data of block parser function. */
static int fw_parse_data_kernel(struct tasdevice_fw *tas_fmw,
	struct tasdevice_data *img_data, const struct firmware *fmw,
	int offset)
{
	const unsigned char *data = fmw->data;
	struct tasdev_blk *blk;
	unsigned int i;

	if (offset + 4 > fmw->size) {
		dev_err(tas_fmw->dev, "%s: File Size error\n", __func__);
		return -EINVAL;
	}
	img_data->nr_blk = get_unaligned_be32(&data[offset]);
	offset += 4;

	img_data->dev_blks = kcalloc(img_data->nr_blk,
		sizeof(struct tasdev_blk), GFP_KERNEL);
	if (!img_data->dev_blks)
		return -ENOMEM;

	for (i = 0; i < img_data->nr_blk; i++) {
		blk = &img_data->dev_blks[i];
		offset = fw_parse_block_data_kernel(
			tas_fmw, blk, fmw, offset);
		if (offset < 0) {
			kfree(img_data->dev_blks);
			return -EINVAL;
		}
	}

	return offset;
}

/* Data of DSP program parser function. */
static int fw_parse_program_data_kernel(
	struct tasdevice_priv *tas_priv, struct tasdevice_fw *tas_fmw,
	const struct firmware *fmw, int offset)
{
	struct tasdevice_prog *program;
	unsigned int i;

	for (i = 0; i < tas_fmw->nr_programs; i++) {
		program = &tas_fmw->programs[i];
		if (offset + 72 > fmw->size) {
			dev_err(tas_priv->dev, "%s: mpName error\n", __func__);
			return -EINVAL;
		}
		/* skip 72 unused byts */
		offset += 72;

		offset = fw_parse_data_kernel(tas_fmw, &program->dev_data,
			fmw, offset);
		if (offset < 0)
			break;
	}

	return offset;
}

/* Data of DSP configurations parser function. */
static int fw_parse_configuration_data_kernel(struct tasdevice_priv *tas_priv,
	struct tasdevice_fw *tas_fmw, const struct firmware *fmw, int offset)
{
	const unsigned char *data = fmw->data;
	struct tasdevice_config *config;
	unsigned int i;

	for (i = 0; i < tas_fmw->nr_configurations; i++) {
		config = &tas_fmw->configs[i];
		if (offset + 80 > fmw->size) {
			dev_err(tas_priv->dev, "%s: mpName error\n", __func__);
			return -EINVAL;
		}
		memcpy(config->name, &data[offset], 64);
		/* skip extra 16 bytes */
		offset += 80;

		offset = fw_parse_data_kernel(tas_fmw, &config->dev_data,
			fmw, offset);
		if (offset < 0)
			break;
	}

	return offset;
}

/* DSP firmware file header parser function for early PPC3 firmware binary. */
static int fw_parse_variable_header_kernel(struct tasdevice_priv *tas_priv,
	const struct firmware *fmw, int offset)
{
	struct tasdevice_fw *tas_fmw = tas_priv->fmw;
	struct tasdevice_dspfw_hdr *fw_hdr = &tas_fmw->fw_hdr;
	struct tasdevice_config *config;
	struct tasdevice_prog *program;
	const unsigned char *buf = fmw->data;
	unsigned short max_confs;
	unsigned int i;

	if (offset + 12 + 4 * TASDEVICE_MAXPROGRAM_NUM_KERNEL > fmw->size) {
		dev_err(tas_priv->dev, "%s: File Size error\n", __func__);
		return -EINVAL;
	}
	fw_hdr->device_family = get_unaligned_be16(&buf[offset]);
	if (fw_hdr->device_family != 0) {
		dev_err(tas_priv->dev, "%s:not TAS device\n", __func__);
		return -EINVAL;
	}
	offset += 2;
	fw_hdr->device = get_unaligned_be16(&buf[offset]);
	if (fw_hdr->device >= TASDEVICE_DSP_TAS_MAX_DEVICE ||
	    fw_hdr->device == 6) {
		dev_err(tas_priv->dev, "Unsupported dev %d\n", fw_hdr->device);
		return -EINVAL;
	}
	offset += 2;

	tas_fmw->nr_programs = get_unaligned_be32(&buf[offset]);
	offset += 4;

	if (tas_fmw->nr_programs == 0 ||
	    tas_fmw->nr_programs > TASDEVICE_MAXPROGRAM_NUM_KERNEL) {
		dev_err(tas_priv->dev, "mnPrograms is invalid\n");
		return -EINVAL;
	}

	tas_fmw->programs = kcalloc(tas_fmw->nr_programs,
		sizeof(*tas_fmw->programs), GFP_KERNEL);
	if (!tas_fmw->programs)
		return -ENOMEM;

	for (i = 0; i < tas_fmw->nr_programs; i++) {
		program = &tas_fmw->programs[i];
		program->prog_size = get_unaligned_be32(&buf[offset]);
		offset += 4;
	}

	/* Skip the unused prog_size */
	offset += 4 * (TASDEVICE_MAXPROGRAM_NUM_KERNEL - tas_fmw->nr_programs);

	tas_fmw->nr_configurations = get_unaligned_be32(&buf[offset]);
	offset += 4;

	/*
	 * The max number of config in firmware greater than 4 pieces of
	 * tas2781s is different from the one lower than 4 pieces of
	 * tas2781s.
	 */
	max_confs = TASDEVICE_MAXCONFIG_NUM_KERNEL;
	if (tas_fmw->nr_configurations == 0 ||
	    tas_fmw->nr_configurations > max_confs) {
		dev_err(tas_priv->dev, "%s: Conf is invalid\n", __func__);
		kfree(tas_fmw->programs);
		return -EINVAL;
	}

	if (offset + 4 * max_confs > fmw->size) {
		dev_err(tas_priv->dev, "%s: mpConfigurations err\n", __func__);
		kfree(tas_fmw->programs);
		return -EINVAL;
	}

	tas_fmw->configs = kcalloc(tas_fmw->nr_configurations,
		sizeof(*tas_fmw->configs), GFP_KERNEL);
	if (!tas_fmw->configs) {
		kfree(tas_fmw->programs);
		return -ENOMEM;
	}

	for (i = 0; i < tas_fmw->nr_programs; i++) {
		config = &tas_fmw->configs[i];
		config->cfg_size = get_unaligned_be32(&buf[offset]);
		offset += 4;
	}

	/* Skip the unused configs */
	offset += 4 * (max_confs - tas_fmw->nr_programs);

	return offset;
}

/*
 * In sub-block data, have three type sub-block:
 * 1. Single byte write.
 * 2. Multi-byte write.
 * 3. Delay.
 * 4. Bits update.
 * This function perform single byte write to device.
 */
static int tasdevice_single_byte_wr(void *context, int dev_idx,
				    unsigned char *data, int sublocksize)
{
	struct tasdevice_priv *tas_priv = context;
	unsigned short len = get_unaligned_be16(&data[2]);
	int i, subblk_offset, rc;

	subblk_offset = 4;
	if (subblk_offset + 4 * len > sublocksize) {
		dev_err(tas_priv->dev, "process_block: Out of boundary\n");
		return 0;
	}

	for (i = 0; i < len; i++) {
		if (dev_idx == (tas_priv->index + 1) || dev_idx == 0) {
			rc = tasdevice_spi_dev_write(tas_priv,
				TASDEVICE_REG(data[subblk_offset],
				data[subblk_offset + 1],
				data[subblk_offset + 2]),
				data[subblk_offset + 3]);
			if (rc < 0) {
				dev_err(tas_priv->dev,
					"process_block: single write error\n");
				subblk_offset |= OFFSET_ERROR_BIT;
			}
		}
		subblk_offset += 4;
	}

	return subblk_offset;
}

/*
 * In sub-block data, have three type sub-block:
 * 1. Single byte write.
 * 2. Multi-byte write.
 * 3. Delay.
 * 4. Bits update.
 * This function perform multi-write to device.
 */
static int tasdevice_burst_wr(void *context, int dev_idx, unsigned char *data,
	int sublocksize)
{
	struct tasdevice_priv *tas_priv = context;
	unsigned short len = get_unaligned_be16(&data[2]);
	int subblk_offset, rc;

	subblk_offset = 4;
	if (subblk_offset + 4 + len > sublocksize) {
		dev_err(tas_priv->dev, "%s: BST Out of boundary\n", __func__);
		subblk_offset |= OFFSET_ERROR_BIT;
	}
	if (len % 4) {
		dev_err(tas_priv->dev, "%s:Bst-len(%u)not div by 4\n",
			__func__, len);
		subblk_offset |= OFFSET_ERROR_BIT;
	}

	if (dev_idx == (tas_priv->index + 1) || dev_idx == 0) {
		rc = tasdevice_spi_dev_bulk_write(tas_priv,
			TASDEVICE_REG(data[subblk_offset],
				      data[subblk_offset + 1],
				      data[subblk_offset + 2]),
				      &data[subblk_offset + 4], len);
		if (rc < 0) {
			dev_err(tas_priv->dev, "%s: bulk_write error = %d\n",
				__func__, rc);
			subblk_offset |= OFFSET_ERROR_BIT;
		}
	}
	subblk_offset += (len + 4);

	return subblk_offset;
}

/* Just delay for ms.*/
static int tasdevice_delay(void *context, int dev_idx, unsigned char *data,
	int sublocksize)
{
	struct tasdevice_priv *tas_priv = context;
	unsigned int sleep_time, subblk_offset = 2;

	if (subblk_offset + 2 > sublocksize) {
		dev_err(tas_priv->dev, "%s: delay Out of boundary\n",
			__func__);
		subblk_offset |= OFFSET_ERROR_BIT;
	}
	if (dev_idx == (tas_priv->index + 1) || dev_idx == 0) {
		sleep_time = get_unaligned_be16(&data[2]) * 1000;
		fsleep(sleep_time);
	}
	subblk_offset += 2;

	return subblk_offset;
}

/*
 * In sub-block data, have three type sub-block:
 * 1. Single byte write.
 * 2. Multi-byte write.
 * 3. Delay.
 * 4. Bits update.
 * This function perform bits update.
 */
static int tasdevice_field_wr(void *context, int dev_idx, unsigned char *data,
	int sublocksize)
{
	struct tasdevice_priv *tas_priv = context;
	int rc, subblk_offset = 2;

	if (subblk_offset + 6 > sublocksize) {
		dev_err(tas_priv->dev, "%s: bit write Out of boundary\n",
			__func__);
		subblk_offset |= OFFSET_ERROR_BIT;
	}
	if (dev_idx == (tas_priv->index + 1) || dev_idx == 0) {
		rc = tasdevice_spi_dev_update_bits(tas_priv,
			TASDEVICE_REG(data[subblk_offset + 2],
			data[subblk_offset + 3],
			data[subblk_offset + 4]),
			data[subblk_offset + 1],
			data[subblk_offset + 5]);
		if (rc < 0) {
			dev_err(tas_priv->dev, "%s: update_bits error = %d\n",
				__func__, rc);
			subblk_offset |= OFFSET_ERROR_BIT;
		}
	}
	subblk_offset += 6;

	return subblk_offset;
}

/* Data block process function. */
static int tasdevice_process_block(void *context, unsigned char *data,
	unsigned char dev_idx, int sublocksize)
{
	struct tasdevice_priv *tas_priv = context;
	int blktyp = dev_idx & 0xC0, subblk_offset;
	unsigned char subblk_typ = data[1];

	switch (subblk_typ) {
	case TASDEVICE_CMD_SING_W:
		subblk_offset = tasdevice_single_byte_wr(tas_priv,
			dev_idx & 0x3f, data, sublocksize);
		break;
	case TASDEVICE_CMD_BURST:
		subblk_offset = tasdevice_burst_wr(tas_priv,
			dev_idx & 0x3f, data, sublocksize);
		break;
	case TASDEVICE_CMD_DELAY:
		subblk_offset = tasdevice_delay(tas_priv,
			dev_idx & 0x3f, data, sublocksize);
		break;
	case TASDEVICE_CMD_FIELD_W:
		subblk_offset = tasdevice_field_wr(tas_priv,
			dev_idx & 0x3f, data, sublocksize);
		break;
	default:
		subblk_offset = 2;
		break;
	}
	if (((subblk_offset & OFFSET_ERROR_BIT) != 0) && blktyp != 0) {
		if (blktyp == 0x80) {
			tas_priv->cur_prog = -1;
			tas_priv->cur_conf = -1;
		} else
			tas_priv->cur_conf = -1;
	}
	subblk_offset &= ~OFFSET_ERROR_BIT;

	return subblk_offset;
}

/*
 * Device support different configurations for different scene,
 * this function was used for choose different config.
 */
void tasdevice_spi_select_cfg_blk(void *pContext, int conf_no,
	unsigned char block_type)
{
	struct tasdevice_priv *tas_priv = pContext;
	struct tasdevice_rca *rca = &tas_priv->rcabin;
	struct tasdevice_config_info **cfg_info = rca->cfg_info;
	struct tasdev_blk_data **blk_data;
	unsigned int j, k;

	if (conf_no >= rca->ncfgs || conf_no < 0 || !cfg_info) {
		dev_err(tas_priv->dev, "conf_no should be not more than %u\n",
			rca->ncfgs);
		return;
	}
	blk_data = cfg_info[conf_no]->blk_data;

	for (j = 0; j < cfg_info[conf_no]->real_nblocks; j++) {
		unsigned int length = 0, rc = 0;

		if (block_type > 5 || block_type < 2) {
			dev_err(tas_priv->dev,
				"block_type should be in range from 2 to 5\n");
			break;
		}
		if (block_type != blk_data[j]->block_type)
			continue;

		for (k = 0; k < blk_data[j]->n_subblks; k++) {
			tas_priv->is_loading = true;

			rc = tasdevice_process_block(tas_priv,
				blk_data[j]->regdata + length,
				blk_data[j]->dev_idx,
				blk_data[j]->block_size - length);
			length += rc;
			if (blk_data[j]->block_size < length) {
				dev_err(tas_priv->dev,
					"%s: %u %u out of boundary\n",
					__func__, length,
					blk_data[j]->block_size);
				break;
			}
		}
		if (length != blk_data[j]->block_size)
			dev_err(tas_priv->dev, "%s: %u %u size is not same\n",
				__func__, length, blk_data[j]->block_size);
	}
}

/* Block process function. */
static int tasdevice_load_block_kernel(
	struct tasdevice_priv *tasdevice, struct tasdev_blk *block)
{
	const unsigned int blk_size = block->blk_size;
	unsigned char *data = block->data;
	unsigned int i, length;

	for (i = 0, length = 0; i < block->nr_subblocks; i++) {
		int rc = tasdevice_process_block(tasdevice, data + length,
			block->dev_idx, blk_size - length);
		if (rc < 0) {
			dev_err(tasdevice->dev,
				"%s: %u %u sublock write error\n",
				__func__, length, blk_size);
			return rc;
		}
		length += rc;
		if (blk_size < length) {
			dev_err(tasdevice->dev, "%s: %u %u out of boundary\n",
				__func__, length, blk_size);
			rc = -ENOMEM;
			return rc;
		}
	}

	return 0;
}

/* DSP firmware file header parser function. */
static int fw_parse_variable_hdr(struct tasdevice_priv *tas_priv,
	struct tasdevice_dspfw_hdr *fw_hdr,
	const struct firmware *fmw, int offset)
{
	const unsigned char *buf = fmw->data;
	int len = strlen((char *)&buf[offset]);

	len++;

	if (offset + len + 8 > fmw->size) {
		dev_err(tas_priv->dev, "%s: File Size error\n", __func__);
		return -EINVAL;
	}

	offset += len;

	fw_hdr->device_family = get_unaligned_be32(&buf[offset]);
	if (fw_hdr->device_family != 0) {
		dev_err(tas_priv->dev, "%s: not TAS device\n", __func__);
		return -EINVAL;
	}
	offset += 4;

	fw_hdr->device = get_unaligned_be32(&buf[offset]);
	if (fw_hdr->device >= TASDEVICE_DSP_TAS_MAX_DEVICE ||
	    fw_hdr->device == 6) {
		dev_err(tas_priv->dev, "Unsupported dev %d\n", fw_hdr->device);
		return -EINVAL;
	}
	offset += 4;
	fw_hdr->ndev = 1;

	return offset;
}

/* DSP firmware file header parser function for size variabled header. */
static int fw_parse_variable_header_git(struct tasdevice_priv
	*tas_priv, const struct firmware *fmw, int offset)
{
	struct tasdevice_fw *tas_fmw = tas_priv->fmw;
	struct tasdevice_dspfw_hdr *fw_hdr = &tas_fmw->fw_hdr;

	offset = fw_parse_variable_hdr(tas_priv, fw_hdr, fmw, offset);

	return offset;
}

/* DSP firmware file block parser function. */
static int fw_parse_block_data(struct tasdevice_fw *tas_fmw,
	struct tasdev_blk *block, const struct firmware *fmw, int offset)
{
	unsigned char *data = (unsigned char *)fmw->data;
	int n;

	if (offset + 8 > fmw->size) {
		dev_err(tas_fmw->dev, "%s: Type error\n", __func__);
		return -EINVAL;
	}
	block->type = get_unaligned_be32(&data[offset]);
	offset += 4;

	if (tas_fmw->fw_hdr.fixed_hdr.drv_ver >= PPC_DRIVER_CRCCHK) {
		if (offset + 8 > fmw->size) {
			dev_err(tas_fmw->dev, "PChkSumPresent error\n");
			return -EINVAL;
		}
		block->is_pchksum_present = data[offset];
		offset++;

		block->pchksum = data[offset];
		offset++;

		block->is_ychksum_present = data[offset];
		offset++;

		block->ychksum = data[offset];
		offset++;
	} else {
		block->is_pchksum_present = 0;
		block->is_ychksum_present = 0;
	}

	block->nr_cmds = get_unaligned_be32(&data[offset]);
	offset += 4;

	n = block->nr_cmds * 4;
	if (offset + n > fmw->size) {
		dev_err(tas_fmw->dev,
			"%s: File Size(%lu) error offset = %d n = %d\n",
			__func__, (unsigned long)fmw->size, offset, n);
		return -EINVAL;
	}
	/* instead of kzalloc+memcpy */
	block->data = kmemdup(&data[offset], n, GFP_KERNEL);
	if (!block->data)
		return -ENOMEM;

	offset += n;

	return offset;
}

/*
 * When parsing error occurs, all the memory resource will be released
 * in the end of tasdevice_rca_ready.
 */
static int fw_parse_data(struct tasdevice_fw *tas_fmw,
	struct tasdevice_data *img_data, const struct firmware *fmw,
	int offset)
{
	const unsigned char *data = (unsigned char *)fmw->data;
	struct tasdev_blk *blk;
	unsigned int i, n;

	if (offset + 64 > fmw->size) {
		dev_err(tas_fmw->dev, "%s: Name error\n", __func__);
		return -EINVAL;
	}
	memcpy(img_data->name, &data[offset], 64);
	offset += 64;

	n = strlen((char *)&data[offset]);
	n++;
	if (offset + n + 2 > fmw->size) {
		dev_err(tas_fmw->dev, "%s: Description error\n", __func__);
		return -EINVAL;
	}
	offset += n;
	img_data->nr_blk = get_unaligned_be16(&data[offset]);
	offset += 2;

	img_data->dev_blks = kcalloc(img_data->nr_blk,
		sizeof(*img_data->dev_blks), GFP_KERNEL);
	if (!img_data->dev_blks)
		return -ENOMEM;

	for (i = 0; i < img_data->nr_blk; i++) {
		blk = &img_data->dev_blks[i];
		offset = fw_parse_block_data(tas_fmw, blk, fmw, offset);
		if (offset < 0)
			return -EINVAL;
	}

	return offset;
}

/*
 * When parsing error occurs, all the memory resource will be released
 * in the end of tasdevice_rca_ready.
 */
static int fw_parse_program_data(struct tasdevice_priv *tas_priv,
	struct tasdevice_fw *tas_fmw, const struct firmware *fmw, int offset)
{
	unsigned char *buf = (unsigned char *)fmw->data;
	struct tasdevice_prog *program;
	int i;

	if (offset + 2 > fmw->size) {
		dev_err(tas_priv->dev, "%s: File Size error\n", __func__);
		return -EINVAL;
	}
	tas_fmw->nr_programs = get_unaligned_be16(&buf[offset]);
	offset += 2;

	if (tas_fmw->nr_programs == 0) {
		/* Not error in calibration Data file, return directly */
		dev_dbg(tas_priv->dev, "%s: No Programs data, maybe calbin\n",
			__func__);
		return offset;
	}

	tas_fmw->programs =
		kcalloc(tas_fmw->nr_programs, sizeof(*tas_fmw->programs),
			GFP_KERNEL);
	if (!tas_fmw->programs)
		return -ENOMEM;

	for (i = 0; i < tas_fmw->nr_programs; i++) {
		int n = 0;

		program = &tas_fmw->programs[i];
		if (offset + 64 > fmw->size) {
			dev_err(tas_priv->dev, "%s: mpName error\n", __func__);
			return -EINVAL;
		}
		offset += 64;

		n = strlen((char *)&buf[offset]);
		/* skip '\0' and 5 unused bytes */
		n += 6;
		if (offset + n > fmw->size) {
			dev_err(tas_priv->dev, "Description err\n");
			return -EINVAL;
		}

		offset += n;

		offset = fw_parse_data(tas_fmw, &program->dev_data, fmw,
			offset);
		if (offset < 0)
			return offset;
	}

	return offset;
}

/*
 * When parsing error occurs, all the memory resource will be released
 * in the end of tasdevice_rca_ready.
 */
static int fw_parse_configuration_data(struct tasdevice_priv *tas_priv,
	struct tasdevice_fw *tas_fmw, const struct firmware *fmw, int offset)
{
	unsigned char *data = (unsigned char *)fmw->data;
	struct tasdevice_config *config;
	unsigned int i, n;

	if (offset + 2 > fmw->size) {
		dev_err(tas_priv->dev, "%s: File Size error\n", __func__);
		return -EINVAL;
	}
	tas_fmw->nr_configurations = get_unaligned_be16(&data[offset]);
	offset += 2;

	if (tas_fmw->nr_configurations == 0) {
		dev_err(tas_priv->dev, "%s: Conf is zero\n", __func__);
		/* Not error for calibration Data file, return directly */
		return offset;
	}
	tas_fmw->configs = kcalloc(tas_fmw->nr_configurations,
			sizeof(*tas_fmw->configs), GFP_KERNEL);
	if (!tas_fmw->configs)
		return -ENOMEM;
	for (i = 0; i < tas_fmw->nr_configurations; i++) {
		config = &tas_fmw->configs[i];
		if (offset + 64 > fmw->size) {
			dev_err(tas_priv->dev, "File Size err\n");
			return -EINVAL;
		}
		memcpy(config->name, &data[offset], 64);
		offset += 64;

		n = strlen((char *)&data[offset]);
		n += 15;
		if (offset + n > fmw->size) {
			dev_err(tas_priv->dev, "Description err\n");
			return -EINVAL;
		}
		offset += n;
		offset = fw_parse_data(tas_fmw, &config->dev_data,
				       fmw, offset);
		if (offset < 0)
			break;
	}

	return offset;
}

/* yram5 page check. */
static bool check_inpage_yram_rg(struct tas_crc *cd,
	unsigned char reg, unsigned char len)
{
	bool in = false;

	if (reg <= TAS2781_YRAM5_END_REG &&
	    reg >= TAS2781_YRAM5_START_REG) {
		if (reg + len > TAS2781_YRAM5_END_REG)
			cd->len = TAS2781_YRAM5_END_REG - reg + 1;
		else
			cd->len = len;
		cd->offset = reg;
		in = true;
	} else if (reg < TAS2781_YRAM5_START_REG) {
		if (reg + len > TAS2781_YRAM5_START_REG) {
			cd->offset = TAS2781_YRAM5_START_REG;
			cd->len = len - TAS2781_YRAM5_START_REG + reg;
			in = true;
		}
	}

	return in;
}

/* DSP firmware yram block check. */
static bool check_inpage_yram_bk1(struct tas_crc *cd,
	unsigned char page, unsigned char reg, unsigned char len)
{
	bool in = false;

	if (page == TAS2781_YRAM1_PAGE) {
		if (reg >= TAS2781_YRAM1_START_REG) {
			cd->offset = reg;
			cd->len = len;
			in = true;
		} else if (reg + len > TAS2781_YRAM1_START_REG) {
			cd->offset = TAS2781_YRAM1_START_REG;
			cd->len = len - TAS2781_YRAM1_START_REG + reg;
			in = true;
		}
	} else if (page == TAS2781_YRAM3_PAGE) {
		in = check_inpage_yram_rg(cd, reg, len);
	}

	return in;
}

/*
 * Return Code:
 * true -- the registers are in the inpage yram
 * false -- the registers are NOT in the inpage yram
 */
static bool check_inpage_yram(struct tas_crc *cd, unsigned char book,
	unsigned char page, unsigned char reg, unsigned char len)
{
	bool in = false;

	if (book == TAS2781_YRAM_BOOK1)
		in = check_inpage_yram_bk1(cd, page, reg, len);
	else if (book == TAS2781_YRAM_BOOK2 && page == TAS2781_YRAM5_PAGE)
		in = check_inpage_yram_rg(cd, reg, len);

	return in;
}

/* yram4 page check. */
static bool check_inblock_yram_bk(struct tas_crc *cd,
	unsigned char page, unsigned char reg, unsigned char len)
{
	bool in = false;

	if ((page >= TAS2781_YRAM4_START_PAGE &&
	     page <= TAS2781_YRAM4_END_PAGE) ||
	    (page >= TAS2781_YRAM2_START_PAGE &&
	     page <= TAS2781_YRAM2_END_PAGE)) {
		if (reg <= TAS2781_YRAM2_END_REG &&
		    reg >= TAS2781_YRAM2_START_REG) {
			cd->offset = reg;
			cd->len = len;
			in = true;
		} else if (reg < TAS2781_YRAM2_START_REG) {
			if (reg + len - 1 >= TAS2781_YRAM2_START_REG) {
				cd->offset = TAS2781_YRAM2_START_REG;
				cd->len = reg + len - TAS2781_YRAM2_START_REG;
				in = true;
			}
		}
	}

	return in;
}

/*
 * Return Code:
 * true -- the registers are in the inblock yram
 * false -- the registers are NOT in the inblock yram
 */
static bool check_inblock_yram(struct tas_crc *cd, unsigned char book,
	unsigned char page, unsigned char reg, unsigned char len)
{
	bool in = false;

	if (book == TAS2781_YRAM_BOOK1 || book == TAS2781_YRAM_BOOK2)
		in = check_inblock_yram_bk(cd, page, reg, len);

	return in;
}

/* yram page check. */
static bool check_yram(struct tas_crc *cd, unsigned char book,
	unsigned char page, unsigned char reg, unsigned char len)
{
	bool in;

	in = check_inpage_yram(cd, book, page, reg, len);
	if (!in)
		in = check_inblock_yram(cd, book, page, reg, len);

	return in;
}

/* Checksum for data block. */
static int tasdev_multibytes_chksum(struct tasdevice_priv *tasdevice,
	unsigned char book, unsigned char page,
	unsigned char reg, unsigned int len)
{
	struct tas_crc crc_data;
	unsigned char crc_chksum = 0;
	unsigned char nBuf1[128];
	int ret = 0, i;
	bool in;

	if ((reg + len - 1) > 127) {
		ret = -EINVAL;
		dev_err(tasdevice->dev, "firmware error\n");
		goto end;
	}

	if ((book == TASDEVICE_BOOK_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
	    (page == TASDEVICE_PAGE_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
	    (reg == TASDEVICE_REG_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
	    (len == 4)) {
		/* DSP swap command, pass */
		ret = 0;
		goto end;
	}

	in = check_yram(&crc_data, book, page, reg, len);
	if (!in)
		goto end;

	if (len == 1) {
		dev_err(tasdevice->dev, "firmware error\n");
		ret = -EINVAL;
		goto end;
	}

	ret = tasdevice_spi_dev_bulk_read(tasdevice,
		TASDEVICE_REG(book, page, crc_data.offset),
		nBuf1, crc_data.len);
	if (ret < 0)
		goto end;

	for (i = 0; i < crc_data.len; i++) {
		if ((book == TASDEVICE_BOOK_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
		    (page == TASDEVICE_PAGE_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
		    ((i + crc_data.offset) >=
			TASDEVICE_REG_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
		    ((i + crc_data.offset) <=
			(TASDEVICE_REG_ID(TAS2781_SA_COEFF_SWAP_REG) + 4)))
			/* DSP swap command, bypass */
			continue;
		else
			crc_chksum += crc8(tasdevice->crc8_lkp_tbl, &nBuf1[i],
				1, 0);
	}

	ret = crc_chksum;

end:
	return ret;
}

/* Checksum for single register. */
static int do_singlereg_checksum(struct tasdevice_priv *tasdevice,
	unsigned char book, unsigned char page,
	unsigned char reg, unsigned char val)
{
	struct tas_crc crc_data;
	unsigned int nData1;
	int ret = 0;
	bool in;

	/* DSP swap command, pass */
	if ((book == TASDEVICE_BOOK_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
	    (page == TASDEVICE_PAGE_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
	    (reg >= TASDEVICE_REG_ID(TAS2781_SA_COEFF_SWAP_REG)) &&
	    (reg <= (TASDEVICE_REG_ID(TAS2781_SA_COEFF_SWAP_REG) + 4)))
		return 0;

	in = check_yram(&crc_data, book, page, reg, 1);
	if (!in)
		return 0;
	ret = tasdevice_spi_dev_read(tasdevice,
		TASDEVICE_REG(book, page, reg), &nData1);
	if (ret < 0)
		return ret;

	if (nData1 != val) {
		dev_err(tasdevice->dev,
			"B[0x%x]P[0x%x]R[0x%x] W[0x%x], R[0x%x]\n",
			book, page, reg, val, nData1);
		tasdevice->err_code |= ERROR_YRAM_CRCCHK;
		return -EAGAIN;
	}

	ret = crc8(tasdevice->crc8_lkp_tbl, &val, 1, 0);

	return ret;
}

/* Block type check. */
static void set_err_prg_cfg(unsigned int type, struct tasdevice_priv *p)
{
	if ((type == MAIN_ALL_DEVICES) || (type == MAIN_DEVICE_A) ||
	    (type == MAIN_DEVICE_B) || (type == MAIN_DEVICE_C) ||
	    (type == MAIN_DEVICE_D))
		p->cur_prog = -1;
	else
		p->cur_conf = -1;
}

/* Checksum for data bytes. */
static int tasdev_bytes_chksum(struct tasdevice_priv *tas_priv,
	struct tasdev_blk *block, unsigned char book,
	unsigned char page, unsigned char reg, unsigned int len,
	unsigned char val, unsigned char *crc_chksum)
{
	int ret;

	if (len > 1)
		ret = tasdev_multibytes_chksum(tas_priv, book, page, reg,
			len);
	else
		ret = do_singlereg_checksum(tas_priv, book, page, reg, val);

	if (ret > 0) {
		*crc_chksum += ret;
		goto end;
	}

	if (ret != -EAGAIN)
		goto end;

	block->nr_retry--;
	if (block->nr_retry > 0)
		goto end;

	set_err_prg_cfg(block->type, tas_priv);

end:
	return ret;
}

/* Multi-data byte write. */
static int tasdev_multibytes_wr(struct tasdevice_priv *tas_priv,
	struct tasdev_blk *block, unsigned char book,
	unsigned char page, unsigned char reg, unsigned char *data,
	unsigned int len, unsigned int *nr_cmds,
	unsigned char *crc_chksum)
{
	int ret;

	if (len > 1) {
		ret = tasdevice_spi_dev_bulk_write(tas_priv,
			TASDEVICE_REG(book, page, reg), data + 3, len);
		if (ret < 0)
			return ret;
		if (block->is_ychksum_present)
			ret = tasdev_bytes_chksum(tas_priv, block,
				book, page, reg, len, 0, crc_chksum);
	} else {
		ret = tasdevice_spi_dev_write(tas_priv,
			TASDEVICE_REG(book, page, reg), data[3]);
		if (ret < 0)
			return ret;
		if (block->is_ychksum_present)
			ret = tasdev_bytes_chksum(tas_priv, block, book,
				page, reg, 1, data[3], crc_chksum);
	}

	if (!block->is_ychksum_present || ret >= 0) {
		*nr_cmds += 1;
		if (len >= 2)
			*nr_cmds += ((len - 2) / 4) + 1;
	}

	return ret;
}

/* Checksum for block. */
static int tasdev_block_chksum(struct tasdevice_priv *tas_priv,
	struct tasdev_blk *block)
{
	unsigned int nr_value;
	int ret;

	ret = tasdevice_spi_dev_read(tas_priv, TASDEVICE_CHECKSUM, &nr_value);
	if (ret < 0) {
		dev_err(tas_priv->dev, "%s: read error %d.\n", __func__, ret);
		set_err_prg_cfg(block->type, tas_priv);
		return ret;
	}

	if ((nr_value & 0xff) != block->pchksum) {
		dev_err(tas_priv->dev, "%s: PChkSum err %d ", __func__, ret);
		dev_err(tas_priv->dev, "PChkSum = 0x%x, Reg = 0x%x\n",
			block->pchksum, (nr_value & 0xff));
		tas_priv->err_code |= ERROR_PRAM_CRCCHK;
		ret = -EAGAIN;
		block->nr_retry--;

		if (block->nr_retry <= 0)
			set_err_prg_cfg(block->type, tas_priv);
	} else {
		tas_priv->err_code &= ~ERROR_PRAM_CRCCHK;
	}

	return ret;
}

/* Firmware block load function. */
static int tasdev_load_blk(struct tasdevice_priv *tas_priv,
	struct tasdev_blk *block)
{
	unsigned int sleep_time, len, nr_cmds;
	unsigned char offset, book, page, val;
	unsigned char *data = block->data;
	unsigned char crc_chksum = 0;
	int ret = 0;

	while (block->nr_retry > 0) {
		if (block->is_pchksum_present) {
			ret = tasdevice_spi_dev_write(tas_priv,
				TASDEVICE_CHECKSUM, 0);
			if (ret < 0)
				break;
		}

		if (block->is_ychksum_present)
			crc_chksum = 0;

		nr_cmds = 0;

		while (nr_cmds < block->nr_cmds) {
			data = block->data + nr_cmds * 4;

			book = data[0];
			page = data[1];
			offset = data[2];
			val = data[3];

			nr_cmds++;
			/* Single byte write */
			if (offset <= 0x7F) {
				ret = tasdevice_spi_dev_write(tas_priv,
					TASDEVICE_REG(book, page, offset),
					val);
				if (ret < 0)
					break;
				if (block->is_ychksum_present) {
					ret = tasdev_bytes_chksum(tas_priv,
						block, book, page, offset,
						1, val, &crc_chksum);
					if (ret < 0)
						break;
				}
				continue;
			}
			/* sleep command */
			if (offset == 0x81) {
				/* book -- data[0] page -- data[1] */
				sleep_time = ((book << 8) + page)*1000;
				fsleep(sleep_time);
				continue;
			}
			/* Multiple bytes write */
			if (offset == 0x85) {
				data += 4;
				len = (book << 8) + page;
				book = data[0];
				page = data[1];
				offset = data[2];
				ret = tasdev_multibytes_wr(tas_priv,
					block, book, page, offset, data,
					len, &nr_cmds, &crc_chksum);
				if (ret < 0)
					break;
			}
		}
		if (ret == -EAGAIN) {
			if (block->nr_retry > 0)
				continue;
		} else if (ret < 0) {
			/* err in current device, skip it */
			break;
		}

		if (block->is_pchksum_present) {
			ret = tasdev_block_chksum(tas_priv, block);
			if (ret == -EAGAIN) {
				if (block->nr_retry > 0)
					continue;
			} else if (ret < 0) {
				/* err in current device, skip it */
				break;
			}
		}

		if (block->is_ychksum_present) {
			/* TBD, open it when FW ready */
			dev_err(tas_priv->dev,
				"Blk YChkSum: FW = 0x%x, YCRC = 0x%x\n",
				block->ychksum, crc_chksum);

			tas_priv->err_code &=
				~ERROR_YRAM_CRCCHK;
			ret = 0;
		}
		/* skip current blk */
		break;
	}

	return ret;
}

/* Firmware block load function. */
static int tasdevice_load_block(struct tasdevice_priv *tas_priv,
	struct tasdev_blk *block)
{
	int ret = 0;

	block->nr_retry = 6;
	if (tas_priv->is_loading == false)
		return 0;
	ret = tasdev_load_blk(tas_priv, block);
	if (ret < 0)
		dev_err(tas_priv->dev, "Blk (%d) load error\n", block->type);

	return ret;
}

/*
 * Select firmware binary parser & load callback functions by ppc3 version
 * and firmware binary version.
 */
static int dspfw_default_callback(struct tasdevice_priv *tas_priv,
	unsigned int drv_ver, unsigned int ppcver)
{
	int rc = 0;

	if (drv_ver == 0x100) {
		if (ppcver >= PPC3_VERSION_BASE) {
			tas_priv->fw_parse_variable_header =
				fw_parse_variable_header_kernel;
			tas_priv->fw_parse_program_data =
				fw_parse_program_data_kernel;
			tas_priv->fw_parse_configuration_data =
				fw_parse_configuration_data_kernel;
			tas_priv->tasdevice_load_block =
				tasdevice_load_block_kernel;
		} else if (ppcver == 0x00) {
			tas_priv->fw_parse_variable_header =
				fw_parse_variable_header_git;
			tas_priv->fw_parse_program_data =
				fw_parse_program_data;
			tas_priv->fw_parse_configuration_data =
				fw_parse_configuration_data;
			tas_priv->tasdevice_load_block =
				tasdevice_load_block;
		} else {
			dev_err(tas_priv->dev,
				"Wrong PPCVer :0x%08x\n", ppcver);
			rc = -EINVAL;
		}
	} else {
		dev_err(tas_priv->dev, "Wrong DrvVer : 0x%02x\n", drv_ver);
		rc = -EINVAL;
	}

	return rc;
}

/* DSP firmware binary file header parser function. */
static int fw_parse_header(struct tasdevice_priv *tas_priv,
	struct tasdevice_fw *tas_fmw, const struct firmware *fmw, int offset)
{
	struct tasdevice_dspfw_hdr *fw_hdr = &tas_fmw->fw_hdr;
	struct tasdevice_fw_fixed_hdr *fw_fixed_hdr = &fw_hdr->fixed_hdr;
	static const unsigned char magic_number[] = {0x35, 0x35, 0x35, 0x32, };
	const unsigned char *buf = (unsigned char *)fmw->data;

	if (offset + 92 > fmw->size) {
		dev_err(tas_priv->dev, "%s: File Size error\n", __func__);
		offset = -EINVAL;
		goto out;
	}
	if (memcmp(&buf[offset], magic_number, 4)) {
		dev_err(tas_priv->dev, "%s: Magic num NOT match\n", __func__);
		offset = -EINVAL;
		goto out;
	}
	offset += 4;

	/*
	 * Convert data[offset], data[offset + 1], data[offset + 2] and
	 * data[offset + 3] into host
	 */
	fw_fixed_hdr->fwsize = get_unaligned_be32(&buf[offset]);
	offset += 4;
	if (fw_fixed_hdr->fwsize != fmw->size) {
		dev_err(tas_priv->dev, "File size not match, %lu %u",
			(unsigned long)fmw->size, fw_fixed_hdr->fwsize);
		offset = -EINVAL;
		goto out;
	}
	offset += 4;
	fw_fixed_hdr->ppcver = get_unaligned_be32(&buf[offset]);
	offset += 8;
	fw_fixed_hdr->drv_ver = get_unaligned_be32(&buf[offset]);
	offset += 72;

out:
	return offset;
}

/* DSP firmware binary file parser function. */
static int tasdevice_dspfw_ready(const struct firmware *fmw, void *context)
{
	struct tasdevice_priv *tas_priv = context;
	struct tasdevice_fw_fixed_hdr *fw_fixed_hdr;
	struct tasdevice_fw *tas_fmw;
	int offset = 0, ret = 0;

	if (!fmw || !fmw->data) {
		dev_err(tas_priv->dev, "%s: Failed to read firmware %s\n",
			__func__, tas_priv->coef_binaryname);
		return -EINVAL;
	}

	tas_priv->fmw = kzalloc(sizeof(*tas_priv->fmw), GFP_KERNEL);
	if (!tas_priv->fmw)
		return -ENOMEM;
	tas_fmw = tas_priv->fmw;
	tas_fmw->dev = tas_priv->dev;
	offset = fw_parse_header(tas_priv, tas_fmw, fmw, offset);

	if (offset == -EINVAL)
		return -EINVAL;

	fw_fixed_hdr = &tas_fmw->fw_hdr.fixed_hdr;
	/* Support different versions of firmware */
	switch (fw_fixed_hdr->drv_ver) {
	case 0x301:
	case 0x302:
	case 0x502:
	case 0x503:
		tas_priv->fw_parse_variable_header =
			fw_parse_variable_header_kernel;
		tas_priv->fw_parse_program_data =
			fw_parse_program_data_kernel;
		tas_priv->fw_parse_configuration_data =
			fw_parse_configuration_data_kernel;
		tas_priv->tasdevice_load_block =
			tasdevice_load_block_kernel;
		break;
	case 0x202:
	case 0x400:
		tas_priv->fw_parse_variable_header =
			fw_parse_variable_header_git;
		tas_priv->fw_parse_program_data =
			fw_parse_program_data;
		tas_priv->fw_parse_configuration_data =
			fw_parse_configuration_data;
		tas_priv->tasdevice_load_block =
			tasdevice_load_block;
		break;
	default:
		ret = dspfw_default_callback(tas_priv,
			fw_fixed_hdr->drv_ver, fw_fixed_hdr->ppcver);
		if (ret)
			return ret;
		break;
	}

	offset = tas_priv->fw_parse_variable_header(tas_priv, fmw, offset);
	if (offset < 0)
		return offset;

	offset = tas_priv->fw_parse_program_data(tas_priv, tas_fmw, fmw,
		offset);
	if (offset < 0)
		return offset;

	offset = tas_priv->fw_parse_configuration_data(tas_priv,
		tas_fmw, fmw, offset);
	if (offset < 0)
		ret = offset;

	return ret;
}

/* DSP firmware binary file parser function. */
int tasdevice_spi_dsp_parser(void *context)
{
	struct tasdevice_priv *tas_priv = context;
	const struct firmware *fw_entry;
	int ret;

	ret = request_firmware(&fw_entry, tas_priv->coef_binaryname,
		tas_priv->dev);
	if (ret) {
		dev_err(tas_priv->dev, "%s: load %s error\n", __func__,
			tas_priv->coef_binaryname);
		return ret;
	}

	ret = tasdevice_dspfw_ready(fw_entry, tas_priv);
	release_firmware(fw_entry);
	fw_entry = NULL;

	return ret;
}

/* DSP firmware program block data remove function. */
static void tasdev_dsp_prog_blk_remove(struct tasdevice_prog *prog)
{
	struct tasdevice_data *tas_dt;
	struct tasdev_blk *blk;
	unsigned int i;

	if (!prog)
		return;

	tas_dt = &prog->dev_data;

	if (!tas_dt->dev_blks)
		return;

	for (i = 0; i < tas_dt->nr_blk; i++) {
		blk = &tas_dt->dev_blks[i];
		kfree(blk->data);
	}
	kfree(tas_dt->dev_blks);
}

/* DSP firmware program block data remove function. */
static void tasdev_dsp_prog_remove(struct tasdevice_prog *prog,
	unsigned short nr)
{
	int i;

	for (i = 0; i < nr; i++)
		tasdev_dsp_prog_blk_remove(&prog[i]);
	kfree(prog);
}

/* DSP firmware config block data remove function. */
static void tasdev_dsp_cfg_blk_remove(struct tasdevice_config *cfg)
{
	struct tasdevice_data *tas_dt;
	struct tasdev_blk *blk;
	unsigned int i;

	if (cfg) {
		tas_dt = &cfg->dev_data;

		if (!tas_dt->dev_blks)
			return;

		for (i = 0; i < tas_dt->nr_blk; i++) {
			blk = &tas_dt->dev_blks[i];
			kfree(blk->data);
		}
		kfree(tas_dt->dev_blks);
	}
}

/* DSP firmware config remove function. */
static void tasdev_dsp_cfg_remove(struct tasdevice_config *config,
	unsigned short nr)
{
	int i;

	for (i = 0; i < nr; i++)
		tasdev_dsp_cfg_blk_remove(&config[i]);
	kfree(config);
}

/* DSP firmware remove function. */
void tasdevice_spi_dsp_remove(void *context)
{
	struct tasdevice_priv *tas_dev = context;

	if (!tas_dev->fmw)
		return;

	if (tas_dev->fmw->programs)
		tasdev_dsp_prog_remove(tas_dev->fmw->programs,
			tas_dev->fmw->nr_programs);
	if (tas_dev->fmw->configs)
		tasdev_dsp_cfg_remove(tas_dev->fmw->configs,
			tas_dev->fmw->nr_configurations);
	kfree(tas_dev->fmw);
	tas_dev->fmw = NULL;
}

/* DSP firmware calibration data remove function. */
static void tas2781_clear_calfirmware(struct tasdevice_fw *tas_fmw)
{
	struct tasdevice_calibration *calibration;
	struct tasdev_blk *block;
	unsigned int blks;
	int i;

	if (!tas_fmw->calibrations)
		goto out;

	for (i = 0; i < tas_fmw->nr_calibrations; i++) {
		calibration = &tas_fmw->calibrations[i];
		if (!calibration)
			continue;

		if (!calibration->dev_data.dev_blks)
			continue;

		for (blks = 0; blks < calibration->dev_data.nr_blk; blks++) {
			block = &calibration->dev_data.dev_blks[blks];
			if (!block)
				continue;
			kfree(block->data);
		}
		kfree(calibration->dev_data.dev_blks);
	}
	kfree(tas_fmw->calibrations);
out:
	kfree(tas_fmw);
}

/* Calibration data from firmware remove function. */
void tasdevice_spi_calbin_remove(void *context)
{
	struct tasdevice_priv *tas_priv = context;

	if (tas_priv->cali_data_fmw) {
		tas2781_clear_calfirmware(tas_priv->cali_data_fmw);
		tas_priv->cali_data_fmw = NULL;
	}
}

/* Configuration remove function. */
void tasdevice_spi_config_info_remove(void *context)
{
	struct tasdevice_priv *tas_priv = context;
	struct tasdevice_rca *rca = &tas_priv->rcabin;
	struct tasdevice_config_info **ci = rca->cfg_info;
	unsigned int i, j;

	if (!ci)
		return;
	for (i = 0; i < rca->ncfgs; i++) {
		if (!ci[i])
			continue;
		if (ci[i]->blk_data) {
			for (j = 0; j < ci[i]->real_nblocks; j++) {
				if (!ci[i]->blk_data[j])
					continue;
				kfree(ci[i]->blk_data[j]->regdata);
				kfree(ci[i]->blk_data[j]);
			}
			kfree(ci[i]->blk_data);
		}
		kfree(ci[i]);
	}
	kfree(ci);
}

/* DSP firmware program block data load function. */
static int tasdevice_load_data(struct tasdevice_priv *tas_priv,
	struct tasdevice_data *dev_data)
{
	struct tasdev_blk *block;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < dev_data->nr_blk; i++) {
		block = &dev_data->dev_blks[i];
		ret = tas_priv->tasdevice_load_block(tas_priv, block);
		if (ret < 0)
			break;
	}

	return ret;
}

/* DSP firmware program load interface function. */
int tasdevice_spi_prmg_load(void *context, int prm_no)
{
	struct tasdevice_priv *tas_priv = context;
	struct tasdevice_fw *tas_fmw = tas_priv->fmw;
	struct tasdevice_prog *program;
	struct tasdevice_config *conf;
	int ret = 0;

	if (!tas_fmw) {
		dev_err(tas_priv->dev, "%s: Firmware is NULL\n", __func__);
		return -EINVAL;
	}
	if (prm_no >= 0 && prm_no <= tas_fmw->nr_programs) {
		tas_priv->cur_conf = 0;
		tas_priv->is_loading = true;
		program = &tas_fmw->programs[prm_no];
		ret = tasdevice_load_data(tas_priv, &program->dev_data);
		if (ret < 0) {
			dev_err(tas_priv->dev, "Program failed %d.\n", ret);
			return ret;
		}
		tas_priv->cur_prog = prm_no;

		conf = &tas_fmw->configs[tas_priv->cur_conf];
		ret = tasdevice_load_data(tas_priv, &conf->dev_data);
		if (ret < 0)
			dev_err(tas_priv->dev, "Config failed %d.\n", ret);
	} else {
		dev_err(tas_priv->dev,
			"%s: prm(%d) is not in range of Programs %u\n",
			__func__, prm_no, tas_fmw->nr_programs);
		return -EINVAL;
	}

	return ret;
}

/* RCABIN configuration switch interface function. */
void tasdevice_spi_tuning_switch(void *context, int state)
{
	struct tasdevice_priv *tas_priv = context;
	int profile_cfg_id = tas_priv->rcabin.profile_cfg_id;

	if (tas_priv->fw_state == TASDEVICE_DSP_FW_FAIL) {
		dev_err(tas_priv->dev, "DSP bin file not loaded\n");
		return;
	}

	if (state == 0)
		tasdevice_spi_select_cfg_blk(tas_priv, profile_cfg_id,
			TASDEVICE_BIN_BLK_PRE_POWER_UP);
	else
		tasdevice_spi_select_cfg_blk(tas_priv, profile_cfg_id,
			TASDEVICE_BIN_BLK_PRE_SHUTDOWN);
}
