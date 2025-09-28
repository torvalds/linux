// SPDX-License-Identifier: GPL-2.0
//
// TAS2781 HDA Shared Lib for I2C&SPI driver
//
// Copyright 2025 Texas Instruments, Inc.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>

#include <linux/component.h>
#include <linux/crc8.h>
#include <linux/crc32.h>
#include <linux/efi.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/tas2781.h>

#include "tas2781_hda.h"

#define CALIBRATION_DATA_AREA_NUM 2

const efi_guid_t tasdev_fct_efi_guid[] = {
	/* DELL */
	EFI_GUID(0xcc92382d, 0x6337, 0x41cb, 0xa8, 0x8b, 0x8e, 0xce, 0x74,
		0x91, 0xea, 0x9f),
	/* HP */
	EFI_GUID(0x02f9af02, 0x7734, 0x4233, 0xb4, 0x3d, 0x93, 0xfe, 0x5a,
		0xa3, 0x5d, 0xb3),
	/* LENOVO & OTHERS */
	EFI_GUID(0x1f52d2a1, 0xbb3a, 0x457d, 0xbc, 0x09, 0x43, 0xa3, 0xf4,
		0x31, 0x0a, 0x92),
};
EXPORT_SYMBOL_NS_GPL(tasdev_fct_efi_guid, "SND_HDA_SCODEC_TAS2781");

/*
 * The order of calibrated-data writing function is a bit different from the
 * order in UEFI. Here is the conversion to match the order of calibrated-data
 * writing function.
 */
static void cali_cnv(unsigned char *data, unsigned int base, int offset)
{
	struct cali_reg reg_data;

	memcpy(&reg_data, &data[base], sizeof(reg_data));
	/* the data order has to be swapped between r0_low_reg and inv0_reg */
	swap(reg_data.r0_low_reg, reg_data.invr0_reg);

	cpu_to_be32_array((__force __be32 *)(data + offset + 1),
		(u32 *)&reg_data, TASDEV_CALIB_N);
}

static void tas2781_apply_calib(struct tasdevice_priv *p)
{
	struct calidata *cali_data = &p->cali_data;
	struct cali_reg *r = &cali_data->cali_reg_array;
	unsigned char *data = cali_data->data;
	unsigned int *tmp_val = (unsigned int *)data;
	unsigned int cali_reg[TASDEV_CALIB_N] = {
		TASDEVICE_REG(0, 0x17, 0x74),
		TASDEVICE_REG(0, 0x18, 0x0c),
		TASDEVICE_REG(0, 0x18, 0x14),
		TASDEVICE_REG(0, 0x13, 0x70),
		TASDEVICE_REG(0, 0x18, 0x7c),
	};
	unsigned int crc, oft, node_num;
	unsigned char *buf;
	int i, j, k, l;

	if (tmp_val[0] == 2781) {
		/*
		 * New features were added in calibrated Data V3:
		 *     1. Added calibration registers address define in
		 *	    a node, marked as Device id == 0x80.
		 * New features were added in calibrated Data V2:
		 *     1. Added some the fields to store the link_id and
		 *	    uniqie_id for multi-link solutions
		 *     2. Support flexible number of devices instead of
		 *	    fixed one in V1.
		 * Layout of calibrated data V2 in UEFI(total 256 bytes):
		 *     ChipID (2781, 4 bytes)
		 *     Data-Group-Sum (4 bytes)
		 *     TimeStamp of Calibration (4 bytes)
		 *     for (i = 0; i < Data-Group-Sum; i++) {
		 *	    if (Data type != 0x80) (4 bytes)
		 *		 Calibrated Data of Device #i (20 bytes)
		 *	    else
		 *		 Calibration registers address (5*4 = 20 bytes)
		 *		 # V2: No reg addr in data grp section.
		 *		 # V3: Normally the last grp is the reg addr.
		 *     }
		 *     CRC (4 bytes)
		 *     Reserved (the rest)
		 */
		crc = crc32(~0, data, (3 + tmp_val[1] * 6) * 4) ^ ~0;

		if (crc != tmp_val[3 + tmp_val[1] * 6]) {
			cali_data->total_sz = 0;
			dev_err(p->dev, "%s: CRC error\n", __func__);
			return;
		}
		node_num = tmp_val[1];

		for (j = 0, k = 0; j < node_num; j++) {
			oft = j * 6 + 3;
			if (tmp_val[oft] == TASDEV_UEFI_CALI_REG_ADDR_FLG) {
				for (i = 0; i < TASDEV_CALIB_N; i++) {
					buf = &data[(oft + i + 1) * 4];
					cali_reg[i] = TASDEVICE_REG(buf[1],
						buf[2], buf[3]);
				}
			} else {
				l = j * (cali_data->cali_dat_sz_per_dev + 1);
				if (k >= p->ndev || l > oft * 4) {
					dev_err(p->dev, "%s: dev sum error\n",
						__func__);
					cali_data->total_sz = 0;
					return;
				}

				data[l] = k;
				oft++;
				cali_cnv(data, 4 * oft, l);
				k++;
			}
		}
	} else {
		/*
		 * Calibration data is in V1 format.
		 * struct cali_data {
		 *     char cali_data[20];
		 * }
		 *
		 * struct {
		 *     struct cali_data cali_data[4];
		 *     int  TimeStamp of Calibration (4 bytes)
		 *     int CRC (4 bytes)
		 * } ueft;
		 */
		crc = crc32(~0, data, 84) ^ ~0;
		if (crc != tmp_val[21]) {
			cali_data->total_sz = 0;
			dev_err(p->dev, "%s: V1 CRC error\n", __func__);
			return;
		}

		for (j = p->ndev - 1; j >= 0; j--) {
			l = j * (cali_data->cali_dat_sz_per_dev + 1);
			cali_cnv(data, cali_data->cali_dat_sz_per_dev * j, l);
			data[l] = j;
		}
	}

	if (p->dspbin_typ == TASDEV_BASIC) {
		r->r0_reg = cali_reg[0];
		r->invr0_reg = cali_reg[1];
		r->r0_low_reg = cali_reg[2];
		r->pow_reg = cali_reg[3];
		r->tlimit_reg = cali_reg[4];
	}

	p->is_user_space_calidata = true;
	cali_data->total_sz = p->ndev * (cali_data->cali_dat_sz_per_dev + 1);
}

/*
 * Update the calibration data, including speaker impedance, f0, etc,
 * into algo. Calibrate data is done by manufacturer in the factory.
 * The data is used by Algo for calculating the speaker temperature,
 * speaker membrane excursion and f0 in real time during playback.
 * Calibration data format in EFI is V2, since 2024.
 */
int tas2781_save_calibration(struct tas2781_hda *hda)
{
	/*
	 * GUID was used for data access in BIOS, it was provided by board
	 * manufactory.
	 */
	efi_guid_t efi_guid = tasdev_fct_efi_guid[LENOVO];
	/*
	 * Some devices save the calibrated data into L"CALI_DATA",
	 * and others into L"SmartAmpCalibrationData".
	 */
	static efi_char16_t *efi_name[CALIBRATION_DATA_AREA_NUM] = {
		L"CALI_DATA",
		L"SmartAmpCalibrationData",
	};
	struct tasdevice_priv *p = hda->priv;
	struct calidata *cali_data = &p->cali_data;
	unsigned long total_sz = 0;
	unsigned int attr, size;
	unsigned char *data;
	efi_status_t status;
	int i;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE)) {
		dev_err(p->dev, "%s: NO EFI FOUND!\n", __func__);
		return -EINVAL;
	}

	if (hda->catlog_id < LENOVO)
		efi_guid = tasdev_fct_efi_guid[hda->catlog_id];

	cali_data->cali_dat_sz_per_dev = 20;
	size = p->ndev * (cali_data->cali_dat_sz_per_dev + 1);
	for (i = 0; i < CALIBRATION_DATA_AREA_NUM; i++) {
		/* Get real size of UEFI variable */
		status = efi.get_variable(efi_name[i], &efi_guid, &attr,
			&total_sz, NULL);
		cali_data->total_sz = total_sz > size ? total_sz : size;
		if (status == EFI_BUFFER_TOO_SMALL) {
			/* Allocate data buffer of data_size bytes */
			data = cali_data->data = devm_kzalloc(p->dev,
				cali_data->total_sz, GFP_KERNEL);
			if (!data) {
				status = -ENOMEM;
				continue;
			}
			/* Get variable contents into buffer */
			status = efi.get_variable(efi_name[i], &efi_guid,
				&attr, &cali_data->total_sz, data);
		}
		/* Check whether get the calibrated data */
		if (status == EFI_SUCCESS)
			break;
	}

	if (status != EFI_SUCCESS) {
		cali_data->total_sz = 0;
		return status;
	}

	tas2781_apply_calib(p);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tas2781_save_calibration, "SND_HDA_SCODEC_TAS2781");

void tas2781_hda_remove(struct device *dev,
	const struct component_ops *ops)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);

	component_del(tas_hda->dev, ops);

	pm_runtime_get_sync(tas_hda->dev);
	pm_runtime_disable(tas_hda->dev);

	pm_runtime_put_noidle(tas_hda->dev);

	tasdevice_remove(tas_hda->priv);
}
EXPORT_SYMBOL_NS_GPL(tas2781_hda_remove, "SND_HDA_SCODEC_TAS2781");

int tasdevice_info_profile(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_priv->rcabin.ncfgs - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_info_profile, "SND_HDA_SCODEC_TAS2781");

int tasdevice_info_programs(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_priv->fmw->nr_programs - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_info_programs, "SND_HDA_SCODEC_TAS2781");

int tasdevice_info_config(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_fw->nr_configurations - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_info_config, "SND_HDA_SCODEC_TAS2781");

int tasdevice_get_profile_id(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = tas_priv->rcabin.profile_cfg_id;

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n", __func__,
		kcontrol->id.name, tas_priv->rcabin.profile_cfg_id);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_get_profile_id, "SND_HDA_SCODEC_TAS2781");

int tasdevice_set_profile_id(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	int profile_id = ucontrol->value.integer.value[0];
	int max = tas_priv->rcabin.ncfgs - 1;
	int val, ret = 0;

	val = clamp(profile_id, 0, max);

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n", __func__,
		kcontrol->id.name, tas_priv->rcabin.profile_cfg_id, val);

	if (tas_priv->rcabin.profile_cfg_id != val) {
		tas_priv->rcabin.profile_cfg_id = val;
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_set_profile_id, "SND_HDA_SCODEC_TAS2781");

int tasdevice_program_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = tas_priv->cur_prog;

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_prog);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_program_get, "SND_HDA_SCODEC_TAS2781");

int tasdevice_program_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;
	int nr_program = ucontrol->value.integer.value[0];
	int max = tas_fw->nr_programs - 1;
	int val, ret = 0;

	val = clamp(nr_program, 0, max);

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_prog, val);

	if (tas_priv->cur_prog != val) {
		tas_priv->cur_prog = val;
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_program_put, "SND_HDA_SCODEC_TAS2781");

int tasdevice_config_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = tas_priv->cur_conf;

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_conf);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_config_get, "SND_HDA_SCODEC_TAS2781");

int tasdevice_config_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;
	int nr_config = ucontrol->value.integer.value[0];
	int max = tas_fw->nr_configurations - 1;
	int val, ret = 0;

	val = clamp(nr_config, 0, max);

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_conf, val);

	if (tas_priv->cur_conf != val) {
		tas_priv->cur_conf = val;
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_config_put, "SND_HDA_SCODEC_TAS2781");

MODULE_DESCRIPTION("TAS2781 HDA Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shenghao Ding, TI, <shenghao-ding@ti.com>");
