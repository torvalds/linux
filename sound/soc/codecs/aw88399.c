// SPDX-License-Identifier: GPL-2.0-only
//
// aw88399.c --  ALSA SoC AW88399 codec support
//
// Copyright (c) 2023 AWINIC Technology CO., LTD
//
// Author: Weidong Wang <wangweidong.a@awinic.com>
//

#include <linux/crc32.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/sort.h>
#include <sound/soc.h>
#include "aw88399.h"
#include "aw88395/aw88395_device.h"

static struct snd_soc_dai_driver aw88399_dai[] = {
	{
		.name = "aw88399-aif",
		.id = 1,
		.playback = {
			.stream_name = "Speaker_Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AW88399_RATES,
			.formats = AW88399_FORMATS,
		},
		.capture = {
			.stream_name = "Speaker_Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AW88399_RATES,
			.formats = AW88399_FORMATS,
		},
	},
};

static void aw_cali_svc_run_mute(struct aw_device *aw_dev, uint16_t cali_result)
{
	if (cali_result == CALI_RESULT_ERROR)
		aw88399_dev_mute(aw_dev, true);
	else if (cali_result == CALI_RESULT_NORMAL)
		aw88399_dev_mute(aw_dev, false);
}

static int aw_cali_svc_get_cali_cfg(struct aw_device *aw_dev)
{
	struct cali_cfg *cali_cfg = &aw_dev->cali_desc.cali_cfg;
	int ret;

	ret = aw_dev_dsp_read(aw_dev, AW88399_DSP_REG_CFG_MBMEC_ACTAMPTH,
					&cali_cfg->data[0], AW_DSP_32_DATA);
	if (ret)
		return ret;

	ret = aw_dev_dsp_read(aw_dev, AW88399_DSP_REG_CFG_MBMEC_NOISEAMPTH,
					&cali_cfg->data[1], AW_DSP_32_DATA);
	if (ret)
		return ret;

	ret = aw_dev_dsp_read(aw_dev, AW88399_DSP_REG_CFG_ADPZ_USTEPN,
					&cali_cfg->data[2], AW_DSP_16_DATA);
	if (ret)
		return ret;

	ret = aw_dev_dsp_read(aw_dev, AW88399_DSP_REG_CFG_RE_ALPHA,
					&cali_cfg->data[3], AW_DSP_16_DATA);

	return ret;
}

static int aw_cali_svc_set_cali_cfg(struct aw_device *aw_dev,
				struct cali_cfg cali_cfg)
{
	int ret;

	ret = aw_dev_dsp_write(aw_dev, AW88399_DSP_REG_CFG_MBMEC_ACTAMPTH,
					cali_cfg.data[0], AW_DSP_32_DATA);
	if (ret)
		return ret;

	ret = aw_dev_dsp_write(aw_dev, AW88399_DSP_REG_CFG_MBMEC_NOISEAMPTH,
					cali_cfg.data[1], AW_DSP_32_DATA);
	if (ret)
		return ret;

	ret = aw_dev_dsp_write(aw_dev, AW88399_DSP_REG_CFG_ADPZ_USTEPN,
					cali_cfg.data[2], AW_DSP_16_DATA);
	if (ret)
		return ret;

	ret = aw_dev_dsp_write(aw_dev, AW88399_DSP_REG_CFG_RE_ALPHA,
					cali_cfg.data[3], AW_DSP_16_DATA);

	return ret;
}

static int aw_cali_svc_cali_en(struct aw_device *aw_dev, bool cali_en)
{
	struct cali_cfg set_cfg;
	int ret;

	aw_dev_dsp_enable(aw_dev, false);
	if (cali_en) {
		regmap_update_bits(aw_dev->regmap, AW88399_DBGCTRL_REG,
				~AW883XX_DSP_NG_EN_MASK, AW883XX_DSP_NG_EN_DISABLE_VALUE);
		aw_dev_dsp_write(aw_dev, AW88399_DSP_LOW_POWER_SWITCH_CFG_ADDR,
				AW88399_DSP_LOW_POWER_SWITCH_DISABLE, AW_DSP_16_DATA);

		ret = aw_cali_svc_get_cali_cfg(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "get cali cfg failed\n");
			aw_dev_dsp_enable(aw_dev, true);
			return ret;
		}
		set_cfg.data[0] = 0;
		set_cfg.data[1] = 0;
		set_cfg.data[2] = -1;
		set_cfg.data[3] = 1;

		ret = aw_cali_svc_set_cali_cfg(aw_dev, set_cfg);
		if (ret) {
			dev_err(aw_dev->dev, "set cali cfg failed\n");
			aw_cali_svc_set_cali_cfg(aw_dev, aw_dev->cali_desc.cali_cfg);
			aw_dev_dsp_enable(aw_dev, true);
			return ret;
		}
	} else {
		aw_cali_svc_set_cali_cfg(aw_dev, aw_dev->cali_desc.cali_cfg);
	}

	aw_dev_dsp_enable(aw_dev, true);

	return 0;
}

static int aw_cali_svc_cali_run_dsp_vol(struct aw_device *aw_dev, bool enable)
{
	unsigned int reg_val;
	int ret;

	if (enable) {
		ret = regmap_read(aw_dev->regmap, AW88399_DSPCFG_REG, &reg_val);
		if (ret) {
			dev_err(aw_dev->dev, "read reg 0x%x failed\n", AW88399_DSPCFG_REG);
			return ret;
		}

		aw_dev->cali_desc.store_vol = reg_val & (~AW88399_DSP_VOL_MASK);
		ret = regmap_update_bits(aw_dev->regmap, AW88399_DSPCFG_REG,
				~AW88399_DSP_VOL_MASK, AW88399_DSP_VOL_MUTE);
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88399_DSPCFG_REG,
				~AW88399_DSP_VOL_MASK, aw_dev->cali_desc.store_vol);
	}

	return ret;
}

static void aw_cali_svc_backup_info(struct aw_device *aw_dev)
{
	struct aw_cali_backup_desc *backup_desc = &aw_dev->cali_desc.backup_info;
	unsigned int reg_val, dsp_val;

	regmap_read(aw_dev->regmap, AW88399_DBGCTRL_REG, &reg_val);
	backup_desc->dsp_ng_cfg = reg_val & (~AW883XX_DSP_NG_EN_MASK);

	aw_dev_dsp_read(aw_dev, AW88399_DSP_LOW_POWER_SWITCH_CFG_ADDR, &dsp_val, AW_DSP_16_DATA);

	backup_desc->dsp_lp_cfg = dsp_val;
}

static void aw_cali_svc_recover_info(struct aw_device *aw_dev)
{
	struct aw_cali_backup_desc *backup_desc = &aw_dev->cali_desc.backup_info;

	regmap_update_bits(aw_dev->regmap, AW88399_DBGCTRL_REG,
			~AW883XX_DSP_NG_EN_MASK, backup_desc->dsp_ng_cfg);

	aw_dev_dsp_write(aw_dev, AW88399_DSP_LOW_POWER_SWITCH_CFG_ADDR,
			backup_desc->dsp_lp_cfg, AW_DSP_16_DATA);
}

static int aw_cali_svc_cali_re_mode_enable(struct aw_device *aw_dev, bool is_enable)
{
	int ret;

	if (is_enable) {
		ret = aw_dev_check_syspll(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "pll check failed cannot start\n");
			return ret;
		}

		ret = aw_dev_get_dsp_status(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "dsp status error\n");
			return ret;
		}

		aw_cali_svc_backup_info(aw_dev);
		ret = aw_cali_svc_cali_en(aw_dev, true);
		if (ret) {
			dev_err(aw_dev->dev, "aw_cali_svc_cali_en failed\n");
			return ret;
		}

		ret = aw_cali_svc_cali_run_dsp_vol(aw_dev, true);
		if (ret) {
			aw_cali_svc_cali_en(aw_dev, false);
			return ret;
		}

	} else {
		aw_cali_svc_cali_run_dsp_vol(aw_dev, false);
		aw_cali_svc_recover_info(aw_dev);
		aw_cali_svc_cali_en(aw_dev, false);
	}

	return 0;
}

static int aw_cali_svc_get_dev_re(struct aw_device *aw_dev, uint32_t *re)
{
	uint32_t dsp_re, show_re;
	int ret;

	ret = aw_dev_dsp_read(aw_dev, AW88399_DSP_REG_CALRE, &dsp_re, AW_DSP_16_DATA);
	if (ret)
		return ret;

	show_re = AW88399_DSP_RE_TO_SHOW_RE(dsp_re, AW88399_DSP_REG_CALRE_SHIFT);

	*re = (uint32_t)(show_re - aw_dev->cali_desc.ra);

	return 0;
}

static void aw_cali_svc_del_max_min_ave_algo(uint32_t *data, int data_size, uint32_t *dsp_re)
{
	int sum = 0, i;

	for (i = 1; i < data_size - 1; i++)
		sum += data[i];

	*dsp_re = sum / (data_size - AW_CALI_DATA_SUM_RM);
}

static int aw_cali_svc_get_iv_st(struct aw_device *aw_dev)
{
	unsigned int reg_data;
	int ret, i;

	for (i = 0; i < AW_GET_IV_CNT_MAX; i++) {
		ret = regmap_read(aw_dev->regmap, AW88399_ASR1_REG, &reg_data);
		if (ret) {
			dev_err(aw_dev->dev, "read 0x%x failed\n", AW88399_ASR1_REG);
			return ret;
		}

		reg_data &= (~AW88399_REABS_MASK);
		if (!reg_data)
			return 0;
		msleep(30);
	}

	dev_err(aw_dev->dev, "IV data abnormal, please check\n");

	return -EINVAL;
}

static int compare_ints(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int aw_cali_svc_get_smooth_cali_re(struct aw_device *aw_dev)
{
	uint32_t re_temp[AW_CALI_READ_CNT_MAX];
	uint32_t dsp_re;
	int ret, i;

	for (i = 0; i < AW_CALI_READ_CNT_MAX; i++) {
		ret = aw_cali_svc_get_dev_re(aw_dev, &re_temp[i]);
		if (ret)
			goto cali_re_fail;
		msleep(30);
	}

	sort(re_temp, AW_CALI_READ_CNT_MAX, sizeof(uint32_t), compare_ints, NULL);

	aw_cali_svc_del_max_min_ave_algo(re_temp, AW_CALI_READ_CNT_MAX, &dsp_re);

	ret = aw_cali_svc_get_iv_st(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "get iv data failed");
		goto cali_re_fail;
	}

	if (dsp_re < AW88399_CALI_RE_MIN || dsp_re > AW88399_CALI_RE_MAX) {
		dev_err(aw_dev->dev, "out range re value: [%d]mohm\n", dsp_re);
		aw_dev->cali_desc.cali_re = dsp_re;
		aw_dev->cali_desc.cali_result = CALI_RESULT_ERROR;
		aw_cali_svc_run_mute(aw_dev, aw_dev->cali_desc.cali_result);

		return 0;
	}

	aw_dev->cali_desc.cali_result = CALI_RESULT_NORMAL;

	aw_dev->cali_desc.cali_re = dsp_re;
	dev_dbg(aw_dev->dev, "re[%d]mohm\n", aw_dev->cali_desc.cali_re);

	aw_dev_dsp_enable(aw_dev, false);
	aw_dev_update_cali_re(&aw_dev->cali_desc);
	aw_dev_dsp_enable(aw_dev, true);

	return 0;

cali_re_fail:
	aw_dev->cali_desc.cali_result = CALI_RESULT_ERROR;
	aw_cali_svc_run_mute(aw_dev, aw_dev->cali_desc.cali_result);
	return -EINVAL;
}

static int aw_cali_svc_dev_cali_re(struct aw88399 *aw88399)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	struct aw_cali_desc *cali_desc = &aw_dev->cali_desc;
	int ret;

	if (cali_desc->cali_running) {
		dev_err(aw_dev->dev, "calibration in progress\n");
		return -EINVAL;
	}

	cali_desc->cali_running = true;
	aw_cali_svc_run_mute(aw_dev, CALI_RESULT_NORMAL);

	ret = aw_cali_svc_cali_re_mode_enable(aw_dev, true);
	if (ret) {
		dev_err(aw_dev->dev, "start cali re failed\n");
		goto re_mode_err;
	}

	msleep(1000);

	ret = aw_cali_svc_get_smooth_cali_re(aw_dev);
	if (ret)
		dev_err(aw_dev->dev, "get cali re failed\n");

	aw_cali_svc_cali_re_mode_enable(aw_dev, false);

re_mode_err:
	cali_desc->cali_running = false;

	return ret;
}

static int aw88399_get_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->fade_in_time;

	return 0;
}

static int aw88399_set_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88399->aw_pa;
	int time;

	time = ucontrol->value.integer.value[0];

	if (time < mc->min || time > mc->max)
		return -EINVAL;

	if (time != aw_dev->fade_in_time) {
		aw_dev->fade_in_time = time;
		return 1;
	}

	return 0;
}

static int aw88399_get_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->fade_out_time;

	return 0;
}

static int aw88399_set_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88399->aw_pa;
	int time;

	time = ucontrol->value.integer.value[0];
	if (time < mc->min || time > mc->max)
		return -EINVAL;

	if (time != aw_dev->fade_out_time) {
		aw_dev->fade_out_time = time;
		return 1;
	}

	return 0;
}

static int aw88399_dev_set_profile_index(struct aw_device *aw_dev, int index)
{
	/* check the index whether is valid */
	if ((index >= aw_dev->prof_info.count) || (index < 0))
		return -EINVAL;
	/* check the index whether change */
	if (aw_dev->prof_index == index)
		return -EINVAL;

	aw_dev->prof_index = index;
	dev_dbg(aw_dev->dev, "set prof[%s]",
		aw_dev->prof_info.prof_name_list[aw_dev->prof_info.prof_desc[index].id]);

	return 0;
}

static int aw88399_profile_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	char *prof_name;
	int count, ret;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	count = aw88399->aw_pa->prof_info.count;
	if (count <= 0) {
		uinfo->value.enumerated.items = 0;
		return 0;
	}

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	count = uinfo->value.enumerated.item;

	ret = aw88399_dev_get_prof_name(aw88399->aw_pa, count, &prof_name);
	if (ret) {
		strscpy(uinfo->value.enumerated.name, "null");
		return 0;
	}

	strscpy(uinfo->value.enumerated.name, prof_name);

	return 0;
}

static int aw88399_profile_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw88399->aw_pa->prof_index;

	return 0;
}

static int aw88399_profile_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	int ret;

	mutex_lock(&aw88399->lock);
	ret = aw88399_dev_set_profile_index(aw88399->aw_pa, ucontrol->value.integer.value[0]);
	if (ret) {
		dev_dbg(codec->dev, "profile index does not change");
		mutex_unlock(&aw88399->lock);
		return 0;
	}

	if (aw88399->aw_pa->status) {
		aw88399_stop(aw88399->aw_pa);
		aw88399_start(aw88399, AW88399_SYNC_START);
	}

	mutex_unlock(&aw88399->lock);

	return 1;
}

static int aw88399_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_volume_desc *vol_desc = &aw88399->aw_pa->volume_desc;

	ucontrol->value.integer.value[0] = vol_desc->ctl_volume;

	return 0;
}

static int aw88399_volume_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_volume_desc *vol_desc = &aw88399->aw_pa->volume_desc;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (vol_desc->ctl_volume != value) {
		vol_desc->ctl_volume = value;
		aw_dev_set_volume(aw88399->aw_pa, vol_desc->ctl_volume);

		return 1;
	}

	return 0;
}

static int aw88399_get_fade_step(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw88399->aw_pa->fade_step;

	return 0;
}

static int aw88399_set_fade_step(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (aw88399->aw_pa->fade_step != value) {
		aw88399->aw_pa->fade_step = value;
		return 1;
	}

	return 0;
}

static int aw88399_re_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->cali_desc.cali_re;

	return 0;
}

static int aw88399_re_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88399->aw_pa;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (aw_dev->cali_desc.cali_re != value) {
		aw_dev->cali_desc.cali_re = value;
		return 1;
	}

	return 0;
}

static int aw88399_calib_switch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->cali_desc.cali_switch;

	return 0;
}

static int aw88399_calib_switch_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_device *aw_dev = aw88399->aw_pa;

	if (aw_dev->cali_desc.cali_switch == ucontrol->value.integer.value[0])
		return 0;

	aw_dev->cali_desc.cali_switch = ucontrol->value.integer.value[0];

	return 1;
}

static int aw88399_calib_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	/* do nothing */
	return 0;
}

static int aw88399_calib_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_kcontrol_chip(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_device *aw_dev = aw88399->aw_pa;

	if (aw_dev->status && aw_dev->cali_desc.cali_switch)
		aw_cali_svc_dev_cali_re(aw88399);

	return 0;
}

static const struct snd_kcontrol_new aw88399_controls[] = {
	SOC_SINGLE_EXT("PCM Playback Volume", AW88399_SYSCTRL2_REG,
		6, AW88399_MUTE_VOL, 0, aw88399_volume_get,
		aw88399_volume_set),
	SOC_SINGLE_EXT("Fade Step", 0, 0, AW88399_MUTE_VOL, 0,
		aw88399_get_fade_step, aw88399_set_fade_step),
	SOC_SINGLE_EXT("Volume Ramp Up Step", 0, 0, FADE_TIME_MAX, FADE_TIME_MIN,
		aw88399_get_fade_in_time, aw88399_set_fade_in_time),
	SOC_SINGLE_EXT("Volume Ramp Down Step", 0, 0, FADE_TIME_MAX, FADE_TIME_MIN,
		aw88399_get_fade_out_time, aw88399_set_fade_out_time),
	SOC_SINGLE_EXT("Calib", 0, 0, AW88399_CALI_RE_MAX, 0,
		aw88399_re_get, aw88399_re_set),
	SOC_SINGLE_BOOL_EXT("Calib Switch", 0,
		aw88399_calib_switch_get, aw88399_calib_switch_set),
	SOC_SINGLE_EXT("Trigger Calib", SND_SOC_NOPM, 0, 1, 0,
		aw88399_calib_get, aw88399_calib_set),
	AW88399_PROFILE_EXT("AW88399 Profile Set", aw88399_profile_info,
		aw88399_profile_get, aw88399_profile_set),
};

static int aw88399_playback_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);

	mutex_lock(&aw88399->lock);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		aw88399_start(aw88399, AW88399_ASYNC_START);
		break;
	case SND_SOC_DAPM_POST_PMD:
		aw88399_stop(aw88399->aw_pa);
		break;
	default:
		break;
	}
	mutex_unlock(&aw88399->lock);

	return 0;
}

static const struct snd_soc_dapm_widget aw88399_dapm_widgets[] = {
	 /* playback */
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "Speaker_Playback", 0, 0, 0, 0,
					aw88399_playback_event,
					SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("DAC Output"),

	/* capture */
	SND_SOC_DAPM_AIF_OUT("AIF_TX", "Speaker_Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("ADC Input"),
};

static const struct snd_soc_dapm_route aw88399_audio_map[] = {
	{"DAC Output", NULL, "AIF_RX"},
	{"AIF_TX", NULL, "ADC Input"},
};

static int aw88399_codec_probe(struct snd_soc_component *component)
{
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	int ret;

	INIT_DELAYED_WORK(&aw88399->start_work, aw88399_startup_work);

	ret = aw88399_request_firmware_file(aw88399);
	if (ret)
		dev_err(aw88399->aw_pa->dev, "%s failed\n", __func__);

	return ret;
}

static void aw88399_codec_remove(struct snd_soc_component *aw_codec)
{
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(aw_codec);

	cancel_delayed_work_sync(&aw88399->start_work);
}

static const struct snd_soc_component_driver soc_codec_dev_aw88399 = {
	.probe = aw88399_codec_probe,
	.remove = aw88399_codec_remove,
	.dapm_widgets = aw88399_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aw88399_dapm_widgets),
	.dapm_routes = aw88399_audio_map,
	.num_dapm_routes = ARRAY_SIZE(aw88399_audio_map),
	.controls = aw88399_controls,
	.num_controls = ARRAY_SIZE(aw88399_controls),
};

static int aw88399_i2c_probe(struct i2c_client *i2c)
{
	struct aw88399 *aw88399;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C))
		return dev_err_probe(&i2c->dev, -ENXIO, "check_functionality failed");

	aw88399 = devm_kzalloc(&i2c->dev, sizeof(*aw88399), GFP_KERNEL);
	if (!aw88399)
		return -ENOMEM;

	mutex_init(&aw88399->lock);

	i2c_set_clientdata(i2c, aw88399);

	aw88399->reset_gpio = devm_gpiod_get_optional(&i2c->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(aw88399->reset_gpio))
		return dev_err_probe(&i2c->dev, PTR_ERR(aw88399->reset_gpio),
							"reset gpio not defined\n");
	aw88399_hw_reset(aw88399);

	aw88399->regmap = devm_regmap_init_i2c(i2c, &aw88399_remap_config);
	if (IS_ERR(aw88399->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(aw88399->regmap),
							"failed to init regmap\n");

	/* aw pa init */
	ret = aw88399_init(aw88399, i2c, aw88399->regmap);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_aw88399,
			aw88399_dai, ARRAY_SIZE(aw88399_dai));
	if (ret)
		dev_err(&i2c->dev, "failed to register aw88399: %d", ret);

	return ret;
}

static const struct i2c_device_id aw88399_i2c_id[] = {
	{ .name = AW88399_I2C_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw88399_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id aw88399_acpi_match[] = {
	{ "AWDZ8399", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, aw88399_acpi_match);
#endif

static struct i2c_driver aw88399_i2c_driver = {
	.driver = {
		.name = AW88399_I2C_NAME,
		.acpi_match_table = ACPI_PTR(aw88399_acpi_match),
	},
	.probe = aw88399_i2c_probe,
	.id_table = aw88399_i2c_id,
};
module_i2c_driver(aw88399_i2c_driver);

MODULE_DESCRIPTION("ASoC AW88399 Smart PA Driver");
MODULE_LICENSE("GPL v2");
