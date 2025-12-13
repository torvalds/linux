// SPDX-License-Identifier: GPL-2.0
//
// TAS2781 HDA I2C driver
//
// Copyright 2023 - 2025 Texas Instruments, Inc.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
// Current maintainer: Baojun Xu <baojun.xu@ti.com>

#include <linux/unaligned.h>
#include <linux/acpi.h>
#include <linux/crc8.h>
#include <linux/crc32.h>
#include <linux/efi.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/hda_codec.h>
#include <sound/soc.h>
#include <sound/tas2781.h>
#include <sound/tas2781-comlib-i2c.h>
#include <sound/tlv.h>
#include <sound/tas2770-tlv.h>
#include <sound/tas2781-tlv.h>
#include <sound/tas5825-tlv.h>

#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_component.h"
#include "hda_jack.h"
#include "../generic.h"
#include "tas2781_hda.h"

#define TAS2563_CAL_VAR_NAME_MAX	16
#define TAS2563_CAL_ARRAY_SIZE		80
#define TAS2563_CAL_DATA_SIZE		4
#define TAS2563_MAX_CHANNELS		4
#define TAS2563_CAL_CH_SIZE		20

#define TAS2563_CAL_R0_LOW		TASDEVICE_REG(0, 0x0f, 0x48)
#define TAS2563_CAL_POWER		TASDEVICE_REG(0, 0x0d, 0x3c)
#define TAS2563_CAL_INVR0		TASDEVICE_REG(0, 0x0f, 0x40)
#define TAS2563_CAL_TLIM		TASDEVICE_REG(0, 0x10, 0x14)
#define TAS2563_CAL_R0			TASDEVICE_REG(0, 0x0f, 0x34)

enum device_chip_id {
	HDA_TAS2563,
	HDA_TAS2770,
	HDA_TAS2781,
	HDA_TAS5825,
	HDA_OTHERS
};

struct tas2781_hda_i2c_priv {
	struct snd_kcontrol *snd_ctls[2];
	int (*save_calibration)(struct tas2781_hda *h);

	int hda_chip_id;
};

static int tas2781_get_i2c_res(struct acpi_resource *ares, void *data)
{
	struct tasdevice_priv *tas_priv = data;
	struct acpi_resource_i2c_serialbus *sb;

	if (i2c_acpi_get_i2c_resource(ares, &sb)) {
		if (tas_priv->ndev < TASDEVICE_MAX_CHANNELS &&
			sb->slave_address != tas_priv->global_addr) {
			tas_priv->tasdevice[tas_priv->ndev].dev_addr =
				(unsigned int)sb->slave_address;
			tas_priv->ndev++;
		}
	}
	return 1;
}

static const struct acpi_gpio_params speakerid_gpios = { 0, 0, false };

static const struct acpi_gpio_mapping tas2781_speaker_id_gpios[] = {
	{ "speakerid-gpios", &speakerid_gpios, 1 },
	{ }
};

static int tas2781_read_acpi(struct tasdevice_priv *p, const char *hid)
{
	struct acpi_device *adev;
	struct device *physdev;
	LIST_HEAD(resources);
	const char *sub;
	uint32_t subid;
	int ret;

	adev = acpi_dev_get_first_match_dev(hid, NULL, -1);
	if (!adev) {
		dev_err(p->dev,
			"Failed to find an ACPI device for %s\n", hid);
		return -ENODEV;
	}

	physdev = get_device(acpi_get_first_physical_node(adev));
	ret = acpi_dev_get_resources(adev, &resources, tas2781_get_i2c_res, p);
	if (ret < 0) {
		dev_err(p->dev, "Failed to get ACPI resource.\n");
		goto err;
	}
	sub = acpi_get_subsystem_id(ACPI_HANDLE(physdev));
	if (IS_ERR(sub)) {
		/* No subsys id in older tas2563 projects. */
		if (!strncmp(hid, "INT8866", sizeof("INT8866")))
			goto end_2563;
		dev_err(p->dev, "Failed to get SUBSYS ID.\n");
		ret = PTR_ERR(sub);
		goto err;
	}
	/* Speaker id was needed for ASUS projects. */
	ret = kstrtou32(sub, 16, &subid);
	if (!ret && upper_16_bits(subid) == PCI_VENDOR_ID_ASUSTEK) {
		ret = devm_acpi_dev_add_driver_gpios(p->dev,
			tas2781_speaker_id_gpios);
		if (ret < 0)
			dev_err(p->dev, "Failed to add driver gpio %d.\n",
				ret);
		p->speaker_id = devm_gpiod_get(p->dev, "speakerid", GPIOD_IN);
		if (IS_ERR(p->speaker_id)) {
			dev_err(p->dev, "Failed to get Speaker id.\n");
			ret = PTR_ERR(p->speaker_id);
			goto err;
		}
	} else {
		p->speaker_id = NULL;
	}

end_2563:
	acpi_dev_free_resource_list(&resources);
	strscpy(p->dev_name, hid, sizeof(p->dev_name));
	put_device(physdev);
	acpi_dev_put(adev);

	return 0;

err:
	dev_err(p->dev, "read acpi error, ret: %d\n", ret);
	put_device(physdev);
	acpi_dev_put(adev);

	return ret;
}

static void tas2781_hda_playback_hook(struct device *dev, int action)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);

	dev_dbg(tas_hda->dev, "%s: action = %d\n", __func__, action);
	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		pm_runtime_get_sync(dev);
		scoped_guard(mutex, &tas_hda->priv->codec_lock) {
			tasdevice_tuning_switch(tas_hda->priv, 0);
			tas_hda->priv->playback_started = true;
		}
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		scoped_guard(mutex, &tas_hda->priv->codec_lock) {
			tasdevice_tuning_switch(tas_hda->priv, 1);
			tas_hda->priv->playback_started = false;
		}

		pm_runtime_put_autosuspend(dev);
		break;
	default:
		break;
	}
}

static int tas2781_amp_getvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int ret;

	guard(mutex)(&tas_priv->codec_lock);

	ret = tasdevice_amp_getvol(tas_priv, ucontrol, mc);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %ld\n",
		__func__, kcontrol->id.name, ucontrol->value.integer.value[0]);

	return ret;
}

static int tas2781_amp_putvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: -> %ld\n",
		__func__, kcontrol->id.name, ucontrol->value.integer.value[0]);

	/* The check of the given value is in tasdevice_amp_putvol. */
	return tasdevice_amp_putvol(tas_priv, ucontrol, mc);
}

static int tas2781_force_fwload_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&tas_priv->codec_lock);

	ucontrol->value.integer.value[0] = (int)tas_priv->force_fwload_status;
	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n",
		__func__, kcontrol->id.name, tas_priv->force_fwload_status);

	return 0;
}

static int tas2781_force_fwload_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	bool change, val = (bool)ucontrol->value.integer.value[0];

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n",
		__func__, kcontrol->id.name,
		tas_priv->force_fwload_status, val);

	if (tas_priv->force_fwload_status == val)
		change = false;
	else {
		change = true;
		tas_priv->force_fwload_status = val;
	}

	return change;
}

static const struct snd_kcontrol_new tas2770_snd_controls[] = {
	ACARD_SINGLE_RANGE_EXT_TLV("Speaker Analog Volume", TAS2770_AMP_LEVEL,
		0, 0, 20, 0, tas2781_amp_getvol,
		tas2781_amp_putvol, tas2770_amp_tlv),
	ACARD_SINGLE_RANGE_EXT_TLV("Speaker Digital Volume", TAS2770_DVC_LEVEL,
		0, 0, 200, 1, tas2781_amp_getvol,
		tas2781_amp_putvol, tas2770_dvc_tlv),
};

static const struct snd_kcontrol_new tas2781_snd_controls[] = {
	ACARD_SINGLE_RANGE_EXT_TLV("Speaker Analog Volume", TAS2781_AMP_LEVEL,
		1, 0, 20, 0, tas2781_amp_getvol,
		tas2781_amp_putvol, tas2781_amp_tlv),
	ACARD_SINGLE_BOOL_EXT("Speaker Force Firmware Load", 0,
		tas2781_force_fwload_get, tas2781_force_fwload_put),
};

static const struct snd_kcontrol_new tas5825_snd_controls[] = {
	ACARD_SINGLE_RANGE_EXT_TLV("Speaker Analog Volume", TAS5825_AMP_LEVEL,
		0, 0, 31, 1, tas2781_amp_getvol,
		tas2781_amp_putvol, tas5825_amp_tlv),
	ACARD_SINGLE_RANGE_EXT_TLV("Speaker Digital Volume", TAS5825_DVC_LEVEL,
		0, 0, 254, 1, tas2781_amp_getvol,
		tas2781_amp_putvol, tas5825_dvc_tlv),
	ACARD_SINGLE_BOOL_EXT("Speaker Force Firmware Load", 0,
		tas2781_force_fwload_get, tas2781_force_fwload_put),
};

static const struct snd_kcontrol_new tasdevice_prof_ctrl = {
	.name = "Speaker Profile Id",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = tasdevice_info_profile,
	.get = tasdevice_get_profile_id,
	.put = tasdevice_set_profile_id,
};

static const struct snd_kcontrol_new tasdevice_dsp_prog_ctrl = {
	.name = "Speaker Program Id",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = tasdevice_info_programs,
	.get = tasdevice_program_get,
	.put = tasdevice_program_put,
};

static const struct snd_kcontrol_new tasdevice_dsp_conf_ctrl = {
	.name = "Speaker Config Id",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = tasdevice_info_config,
	.get = tasdevice_config_get,
	.put = tasdevice_config_put,
};

static int tas2563_save_calibration(struct tas2781_hda *h)
{
	efi_guid_t efi_guid = tasdev_fct_efi_guid[LENOVO];
	char *vars[TASDEV_CALIB_N] = {
		"R0_%d", "R0_Low_%d", "InvR0_%d", "Power_%d", "TLim_%d"
	};
	efi_char16_t efi_name[TAS2563_CAL_VAR_NAME_MAX];
	unsigned long max_size = TAS2563_CAL_DATA_SIZE;
	unsigned char var8[TAS2563_CAL_VAR_NAME_MAX];
	struct tasdevice_priv *p = h->priv;
	struct calidata *cd = &p->cali_data;
	struct cali_reg *r = &cd->cali_reg_array;
	unsigned int offset = 0;
	unsigned char *data;
	__be32 bedata;
	efi_status_t status;
	unsigned int attr;
	int ret, i, j, k;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE)) {
		dev_err(p->dev, "%s: NO EFI FOUND!\n", __func__);
		return -EINVAL;
	}

	cd->cali_dat_sz_per_dev = TAS2563_CAL_DATA_SIZE * TASDEV_CALIB_N;

	/* extra byte for each device is the device number */
	cd->total_sz = (cd->cali_dat_sz_per_dev + 1) * p->ndev;
	data = cd->data = devm_kzalloc(p->dev, cd->total_sz,
		GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < p->ndev; ++i) {
		data[offset] = i;
		offset++;
		for (j = 0; j < TASDEV_CALIB_N; ++j) {
			/* EFI name for calibration started with 1, not 0 */
			ret = snprintf(var8, sizeof(var8), vars[j], i + 1);
			if (ret < 0 || ret >= sizeof(var8) - 1) {
				dev_err(p->dev, "%s: Read %s failed\n",
					__func__, var8);
				return -EINVAL;
			}
			/*
			 * Our variable names are ASCII by construction, but
			 * EFI names are wide chars.  Convert and zero-pad.
			 */
			memset(efi_name, 0, sizeof(efi_name));
			for (k = 0; k < sizeof(var8) && var8[k]; k++)
				efi_name[k] = var8[k];
			status = efi.get_variable(efi_name,
				&efi_guid, &attr, &max_size,
				&data[offset]);
			if (status != EFI_SUCCESS ||
				max_size != TAS2563_CAL_DATA_SIZE) {
				dev_warn(p->dev,
					"Dev %d: Caldat[%d] read failed %ld\n",
					i, j, status);
				return -EINVAL;
			}
			bedata = cpu_to_be32(*(uint32_t *)&data[offset]);
			memcpy(&data[offset], &bedata, sizeof(bedata));
			offset += TAS2563_CAL_DATA_SIZE;
		}
	}

	if (cd->total_sz != offset) {
		dev_err(p->dev, "%s: tot_size(%lu) and offset(%u) mismatch\n",
			__func__, cd->total_sz, offset);
		return -EINVAL;
	}

	r->r0_reg = TAS2563_CAL_R0;
	r->invr0_reg = TAS2563_CAL_INVR0;
	r->r0_low_reg = TAS2563_CAL_R0_LOW;
	r->pow_reg = TAS2563_CAL_POWER;
	r->tlimit_reg = TAS2563_CAL_TLIM;

	/*
	 * TAS2781_FMWLIB supports two solutions of calibrated data. One is
	 * from the driver itself: driver reads the calibrated files directly
	 * during probe; The other from user space: during init of audio hal,
	 * the audio hal will pass the calibrated data via kcontrol interface.
	 * Driver will store this data in "struct calidata" for use. For hda
	 * device, calibrated data are usunally saved into UEFI. So Hda side
	 * codec driver use the mixture of these two solutions, driver reads
	 * the data from UEFI, then store this data in "struct calidata" for
	 * use.
	 */
	p->is_user_space_calidata = true;

	return 0;
}

static void tas2781_hda_remove_controls(struct tas2781_hda *tas_hda)
{
	struct tas2781_hda_i2c_priv *hda_priv = tas_hda->hda_priv;
	struct hda_codec *codec = tas_hda->priv->codec;

	snd_ctl_remove(codec->card, tas_hda->dsp_prog_ctl);
	snd_ctl_remove(codec->card, tas_hda->dsp_conf_ctl);

	for (int i = ARRAY_SIZE(hda_priv->snd_ctls) - 1; i >= 0; i--)
		snd_ctl_remove(codec->card, hda_priv->snd_ctls[i]);

	snd_ctl_remove(codec->card, tas_hda->prof_ctl);
}

static void tasdev_add_kcontrols(struct tasdevice_priv *tas_priv,
	struct snd_kcontrol **ctls, struct hda_codec *codec,
	const struct snd_kcontrol_new *tas_snd_ctrls, int num_ctls)
{
	int i, ret;

	for (i = 0; i < num_ctls; i++) {
		ctls[i] = snd_ctl_new1(
			&tas_snd_ctrls[i], tas_priv);
		ret = snd_ctl_add(codec->card, ctls[i]);
		if (ret) {
			dev_err(tas_priv->dev,
				"Failed to add KControl %s = %d\n",
				tas_snd_ctrls[i].name, ret);
			break;
		}
	}
}

static void tasdevice_dspfw_init(void *context)
{
	struct tasdevice_priv *tas_priv = context;
	struct tas2781_hda *tas_hda = dev_get_drvdata(tas_priv->dev);
	struct tas2781_hda_i2c_priv *hda_priv = tas_hda->hda_priv;
	struct hda_codec *codec = tas_priv->codec;
	int ret, spk_id;

	tasdevice_dsp_remove(tas_priv);
	tas_priv->fw_state = TASDEVICE_DSP_FW_PENDING;
	if (tas_priv->speaker_id != NULL) {
		// Speaker id need to be checked for ASUS only.
		spk_id = gpiod_get_value(tas_priv->speaker_id);
		if (spk_id < 0) {
			// Speaker id is not valid, use default.
			dev_dbg(tas_priv->dev, "Wrong spk_id = %d\n", spk_id);
			spk_id = 0;
		}
		snprintf(tas_priv->coef_binaryname,
			  sizeof(tas_priv->coef_binaryname),
			  "TAS2XXX%04X%d.bin",
			  lower_16_bits(codec->core.subsystem_id),
			  spk_id);
	} else {
		snprintf(tas_priv->coef_binaryname,
			  sizeof(tas_priv->coef_binaryname),
			  "TAS2XXX%04X.bin",
			  lower_16_bits(codec->core.subsystem_id));
	}
	ret = tasdevice_dsp_parser(tas_priv);
	if (ret) {
		dev_err(tas_priv->dev, "dspfw load %s error\n",
			tas_priv->coef_binaryname);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		return;
	}
	tasdev_add_kcontrols(tas_priv, &tas_hda->dsp_prog_ctl, codec,
			     &tasdevice_dsp_prog_ctrl, 1);
	tasdev_add_kcontrols(tas_priv, &tas_hda->dsp_conf_ctl, codec,
			     &tasdevice_dsp_conf_ctrl, 1);

	tas_priv->fw_state = TASDEVICE_DSP_FW_ALL_OK;
	tasdevice_prmg_load(tas_priv, 0);
	if (tas_priv->fmw->nr_programs > 0)
		tas_priv->cur_prog = 0;
	if (tas_priv->fmw->nr_configurations > 0)
		tas_priv->cur_conf = 0;

	/* Init common setting for different audio profiles */
	if (tas_priv->rcabin.init_profile_id >= 0)
		tasdevice_select_cfg_blk(tas_priv,
			tas_priv->rcabin.init_profile_id,
			TASDEVICE_BIN_BLK_PRE_POWER_UP);

	/* If calibrated data occurs error, dsp will still works with default
	 * calibrated data inside algo.
	 */
	hda_priv->save_calibration(tas_hda);
}

static void tasdev_fw_ready(const struct firmware *fmw, void *context)
{
	struct tasdevice_priv *tas_priv = context;
	struct tas2781_hda *tas_hda = dev_get_drvdata(tas_priv->dev);
	struct tas2781_hda_i2c_priv *hda_priv = tas_hda->hda_priv;
	struct hda_codec *codec = tas_priv->codec;
	int ret;

	pm_runtime_get_sync(tas_priv->dev);
	mutex_lock(&tas_priv->codec_lock);

	ret = tasdevice_rca_parser(tas_priv, fmw);
	if (ret)
		goto out;

	tas_priv->fw_state = TASDEVICE_RCA_FW_OK;
	tasdev_add_kcontrols(tas_priv, &tas_hda->prof_ctl, codec,
		&tasdevice_prof_ctrl, 1);

	switch (hda_priv->hda_chip_id) {
	case HDA_TAS2770:
		tasdev_add_kcontrols(tas_priv, hda_priv->snd_ctls, codec,
				     &tas2770_snd_controls[0],
				     ARRAY_SIZE(tas2770_snd_controls));
		break;
	case HDA_TAS2781:
		tasdev_add_kcontrols(tas_priv, hda_priv->snd_ctls, codec,
				     &tas2781_snd_controls[0],
				     ARRAY_SIZE(tas2781_snd_controls));
		tasdevice_dspfw_init(context);
		break;
	case HDA_TAS5825:
		tasdev_add_kcontrols(tas_priv, hda_priv->snd_ctls, codec,
				     &tas5825_snd_controls[0],
				     ARRAY_SIZE(tas5825_snd_controls));
		tasdevice_dspfw_init(context);
		break;
	case HDA_TAS2563:
		tasdevice_dspfw_init(context);
		break;
	default:
		break;
	}

out:
	mutex_unlock(&tas_hda->priv->codec_lock);
	release_firmware(fmw);
	pm_runtime_put_autosuspend(tas_hda->dev);
}

static int tas2781_hda_bind(struct device *dev, struct device *master,
	void *master_data)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;
	struct hda_codec *codec;
	unsigned int subid;
	int ret;

	comp = hda_component_from_index(parent, tas_hda->priv->index);
	if (!comp)
		return -EINVAL;

	if (comp->dev)
		return -EBUSY;

	codec = parent->codec;
	subid = codec->core.subsystem_id >> 16;

	switch (subid) {
	case 0x1028:
		tas_hda->catlog_id = DELL;
		break;
	default:
		tas_hda->catlog_id = LENOVO;
		break;
	}

	pm_runtime_get_sync(dev);

	comp->dev = dev;

	strscpy(comp->name, dev_name(dev), sizeof(comp->name));

	ret = tascodec_init(tas_hda->priv, codec, THIS_MODULE, tasdev_fw_ready);
	if (!ret)
		comp->playback_hook = tas2781_hda_playback_hook;

	pm_runtime_put_autosuspend(dev);

	return ret;
}

static void tas2781_hda_unbind(struct device *dev,
	struct device *master, void *master_data)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;

	comp = hda_component_from_index(parent, tas_hda->priv->index);
	if (comp && (comp->dev == dev)) {
		comp->dev = NULL;
		memset(comp->name, 0, sizeof(comp->name));
		comp->playback_hook = NULL;
	}

	tas2781_hda_remove_controls(tas_hda);

	tasdevice_config_info_remove(tas_hda->priv);
	tasdevice_dsp_remove(tas_hda->priv);

	tas_hda->priv->fw_state = TASDEVICE_DSP_FW_PENDING;
}

static const struct component_ops tas2781_hda_comp_ops = {
	.bind = tas2781_hda_bind,
	.unbind = tas2781_hda_unbind,
};

static int tas2781_hda_i2c_probe(struct i2c_client *clt)
{
	struct tas2781_hda_i2c_priv *hda_priv;
	struct tas2781_hda *tas_hda;
	const char *device_name;
	int ret;

	tas_hda = devm_kzalloc(&clt->dev, sizeof(*tas_hda), GFP_KERNEL);
	if (!tas_hda)
		return -ENOMEM;

	hda_priv = devm_kzalloc(&clt->dev, sizeof(*hda_priv), GFP_KERNEL);
	if (!hda_priv)
		return -ENOMEM;

	tas_hda->hda_priv = hda_priv;

	dev_set_drvdata(&clt->dev, tas_hda);
	tas_hda->dev = &clt->dev;

	tas_hda->priv = tasdevice_kzalloc(clt);
	if (!tas_hda->priv)
		return -ENOMEM;

	if (strstr(dev_name(&clt->dev), "TIAS2781")) {
		/*
		 * TAS2781, integrated on-chip DSP with
		 * global I2C address supported.
		 */
		device_name = "TIAS2781";
		hda_priv->hda_chip_id = HDA_TAS2781;
		hda_priv->save_calibration = tas2781_save_calibration;
		tas_hda->priv->global_addr = TAS2781_GLOBAL_ADDR;
	} else if (strstarts(dev_name(&clt->dev), "i2c-TXNW2770")) {
		/*
		 * TAS2770, has no on-chip DSP, so no calibration data
		 * required; has no global I2C address supported.
		 */
		device_name = "TXNW2770";
		hda_priv->hda_chip_id = HDA_TAS2770;
	} else if (strstarts(dev_name(&clt->dev),
			     "i2c-TXNW2781:00-tas2781-hda.0")) {
		device_name = "TXNW2781";
		hda_priv->hda_chip_id = HDA_TAS2781;
		hda_priv->save_calibration = tas2781_save_calibration;
		tas_hda->priv->global_addr = TAS2781_GLOBAL_ADDR;
	} else if (strstr(dev_name(&clt->dev), "INT8866")) {
		/*
		 * TAS2563, integrated on-chip DSP with
		 * global I2C address supported.
		 */
		device_name = "INT8866";
		hda_priv->hda_chip_id = HDA_TAS2563;
		hda_priv->save_calibration = tas2563_save_calibration;
		tas_hda->priv->global_addr = TAS2563_GLOBAL_ADDR;
	} else if (strstarts(dev_name(&clt->dev), "i2c-TXNW5825")) {
		/*
		 * TAS5825, integrated on-chip DSP without
		 * global I2C address and calibration supported.
		 */
		device_name = "TXNW5825";
		hda_priv->hda_chip_id = HDA_TAS5825;
		tas_hda->priv->chip_id = TAS5825;
	} else {
		return -ENODEV;
	}

	tas_hda->priv->irq = clt->irq;
	ret = tas2781_read_acpi(tas_hda->priv, device_name);
	if (ret)
		return dev_err_probe(tas_hda->dev, ret,
			"Platform not supported\n");

	ret = tasdevice_init(tas_hda->priv);
	if (ret)
		goto err;

	pm_runtime_set_autosuspend_delay(tas_hda->dev, 3000);
	pm_runtime_use_autosuspend(tas_hda->dev);
	pm_runtime_mark_last_busy(tas_hda->dev);
	pm_runtime_set_active(tas_hda->dev);
	pm_runtime_enable(tas_hda->dev);

	tasdevice_reset(tas_hda->priv);

	ret = component_add(tas_hda->dev, &tas2781_hda_comp_ops);
	if (ret) {
		dev_err(tas_hda->dev, "Register component failed: %d\n", ret);
		pm_runtime_disable(tas_hda->dev);
	}

err:
	if (ret)
		tas2781_hda_remove(&clt->dev, &tas2781_hda_comp_ops);
	return ret;
}

static void tas2781_hda_i2c_remove(struct i2c_client *clt)
{
	tas2781_hda_remove(&clt->dev, &tas2781_hda_comp_ops);
}

static int tas2781_runtime_suspend(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);

	dev_dbg(tas_hda->dev, "Runtime Suspend\n");

	guard(mutex)(&tas_hda->priv->codec_lock);

	/* The driver powers up the amplifiers at module load time.
	 * Stop the playback if it's unused.
	 */
	if (tas_hda->priv->playback_started) {
		tasdevice_tuning_switch(tas_hda->priv, 1);
		tas_hda->priv->playback_started = false;
	}

	return 0;
}

static int tas2781_runtime_resume(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);

	dev_dbg(tas_hda->dev, "Runtime Resume\n");

	guard(mutex)(&tas_hda->priv->codec_lock);

	tasdevice_prmg_load(tas_hda->priv, tas_hda->priv->cur_prog);

	return 0;
}

static int tas2781_system_suspend(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);

	dev_dbg(tas_hda->priv->dev, "System Suspend\n");

	guard(mutex)(&tas_hda->priv->codec_lock);

	/* Shutdown chip before system suspend */
	if (tas_hda->priv->playback_started)
		tasdevice_tuning_switch(tas_hda->priv, 1);

	/*
	 * Reset GPIO may be shared, so cannot reset here.
	 * However beyond this point, amps may be powered down.
	 */
	return 0;
}

static int tas2781_system_resume(struct device *dev)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);
	int i;

	dev_dbg(tas_hda->priv->dev, "System Resume\n");

	guard(mutex)(&tas_hda->priv->codec_lock);

	for (i = 0; i < tas_hda->priv->ndev; i++) {
		tas_hda->priv->tasdevice[i].cur_book = -1;
		tas_hda->priv->tasdevice[i].cur_prog = -1;
		tas_hda->priv->tasdevice[i].cur_conf = -1;
	}
	tasdevice_reset(tas_hda->priv);
	tasdevice_prmg_load(tas_hda->priv, tas_hda->priv->cur_prog);

	/* Init common setting for different audio profiles */
	if (tas_hda->priv->rcabin.init_profile_id >= 0)
		tasdevice_select_cfg_blk(tas_hda->priv,
			tas_hda->priv->rcabin.init_profile_id,
			TASDEVICE_BIN_BLK_PRE_POWER_UP);

	if (tas_hda->priv->playback_started)
		tasdevice_tuning_switch(tas_hda->priv, 0);

	return 0;
}

static const struct dev_pm_ops tas2781_hda_pm_ops = {
	RUNTIME_PM_OPS(tas2781_runtime_suspend, tas2781_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(tas2781_system_suspend, tas2781_system_resume)
};

static const struct i2c_device_id tas2781_hda_i2c_id[] = {
	{ "tas2781-hda" },
	{}
};

static const struct acpi_device_id tas2781_acpi_hda_match[] = {
	{"INT8866", 0 },
	{"TIAS2781", 0 },
	{"TXNW2770", 0 },
	{"TXNW2781", 0 },
	{"TXNW5825", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, tas2781_acpi_hda_match);

static struct i2c_driver tas2781_hda_i2c_driver = {
	.driver = {
		.name		= "tas2781-hda",
		.acpi_match_table = tas2781_acpi_hda_match,
		.pm		= &tas2781_hda_pm_ops,
	},
	.id_table	= tas2781_hda_i2c_id,
	.probe		= tas2781_hda_i2c_probe,
	.remove		= tas2781_hda_i2c_remove,
};
module_i2c_driver(tas2781_hda_i2c_driver);

MODULE_DESCRIPTION("TAS2781 HDA Driver");
MODULE_AUTHOR("Shenghao Ding, TI, <shenghao-ding@ti.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_TAS2781_FMWLIB");
MODULE_IMPORT_NS("SND_HDA_SCODEC_TAS2781");
