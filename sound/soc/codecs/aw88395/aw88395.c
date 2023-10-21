// SPDX-License-Identifier: GPL-2.0-only
//
// aw88395.c --  ALSA SoC AW88395 codec support
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
// Author: Weidong Wang <wangweidong.a@awinic.com>
//

#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "aw88395.h"
#include "aw88395_device.h"
#include "aw88395_lib.h"
#include "aw88395_reg.h"

static const struct regmap_config aw88395_remap_config = {
	.val_bits = 16,
	.reg_bits = 8,
	.max_register = AW88395_REG_MAX - 1,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static void aw88395_start_pa(struct aw88395 *aw88395)
{
	int ret, i;

	for (i = 0; i < AW88395_START_RETRIES; i++) {
		ret = aw88395_dev_start(aw88395->aw_pa);
		if (ret) {
			dev_err(aw88395->aw_pa->dev, "aw88395 device start failed. retry = %d", i);
			ret = aw88395_dev_fw_update(aw88395->aw_pa, AW88395_DSP_FW_UPDATE_ON, true);
			if (ret < 0) {
				dev_err(aw88395->aw_pa->dev, "fw update failed");
				continue;
			}
		} else {
			dev_info(aw88395->aw_pa->dev, "start success\n");
			break;
		}
	}
}

static void aw88395_startup_work(struct work_struct *work)
{
	struct aw88395 *aw88395 =
		container_of(work, struct aw88395, start_work.work);

	mutex_lock(&aw88395->lock);
	aw88395_start_pa(aw88395);
	mutex_unlock(&aw88395->lock);
}

static void aw88395_start(struct aw88395 *aw88395, bool sync_start)
{
	int ret;

	if (aw88395->aw_pa->fw_status != AW88395_DEV_FW_OK)
		return;

	if (aw88395->aw_pa->status == AW88395_DEV_PW_ON)
		return;

	ret = aw88395_dev_fw_update(aw88395->aw_pa, AW88395_DSP_FW_UPDATE_OFF, true);
	if (ret < 0) {
		dev_err(aw88395->aw_pa->dev, "fw update failed.");
		return;
	}

	if (sync_start == AW88395_SYNC_START)
		aw88395_start_pa(aw88395);
	else
		queue_delayed_work(system_wq,
			&aw88395->start_work,
			AW88395_START_WORK_DELAY_MS);
}

static struct snd_soc_dai_driver aw88395_dai[] = {
	{
		.name = "aw88395-aif",
		.id = 1,
		.playback = {
			.stream_name = "Speaker_Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AW88395_RATES,
			.formats = AW88395_FORMATS,
		},
		.capture = {
			.stream_name = "Speaker_Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AW88395_RATES,
			.formats = AW88395_FORMATS,
		},
	},
};

static int aw88395_get_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw88395->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->fade_in_time;

	return 0;
}

static int aw88395_set_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88395->aw_pa;
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

static int aw88395_get_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw88395->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->fade_out_time;

	return 0;
}

static int aw88395_set_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88395->aw_pa;
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

static int aw88395_profile_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	char *prof_name, *name;
	int count, ret;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	count = aw88395_dev_get_profile_count(aw88395->aw_pa);
	if (count <= 0) {
		uinfo->value.enumerated.items = 0;
		return 0;
	}

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	name = uinfo->value.enumerated.name;
	count = uinfo->value.enumerated.item;

	ret = aw88395_dev_get_prof_name(aw88395->aw_pa, count, &prof_name);
	if (ret) {
		strscpy(uinfo->value.enumerated.name, "null",
						strlen("null") + 1);
		return 0;
	}

	strscpy(name, prof_name, sizeof(uinfo->value.enumerated.name));

	return 0;
}

static int aw88395_profile_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw88395_dev_get_profile_index(aw88395->aw_pa);

	return 0;
}

static int aw88395_profile_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	int ret;

	/* pa stop or stopping just set profile */
	mutex_lock(&aw88395->lock);
	ret = aw88395_dev_set_profile_index(aw88395->aw_pa, ucontrol->value.integer.value[0]);
	if (ret < 0) {
		dev_dbg(codec->dev, "profile index does not change");
		mutex_unlock(&aw88395->lock);
		return 0;
	}

	if (aw88395->aw_pa->status) {
		aw88395_dev_stop(aw88395->aw_pa);
		aw88395_start(aw88395, AW88395_SYNC_START);
	}

	mutex_unlock(&aw88395->lock);

	return 1;
}

static int aw88395_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	struct aw_volume_desc *vol_desc = &aw88395->aw_pa->volume_desc;

	ucontrol->value.integer.value[0] = vol_desc->ctl_volume;

	return 0;
}

static int aw88395_volume_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	struct aw_volume_desc *vol_desc = &aw88395->aw_pa->volume_desc;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (vol_desc->ctl_volume != value) {
		vol_desc->ctl_volume = value;
		aw88395_dev_set_volume(aw88395->aw_pa, vol_desc->ctl_volume);

		return 1;
	}

	return 0;
}

static int aw88395_get_fade_step(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw88395->aw_pa->fade_step;

	return 0;
}

static int aw88395_set_fade_step(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (aw88395->aw_pa->fade_step != value) {
		aw88395->aw_pa->fade_step = value;
		return 1;
	}

	return 0;
}

static int aw88395_re_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	struct aw_device *aw_dev = aw88395->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->cali_desc.cali_re;

	return 0;
}

static int aw88395_re_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88395->aw_pa;
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

static const struct snd_kcontrol_new aw88395_controls[] = {
	SOC_SINGLE_EXT("PCM Playback Volume", AW88395_SYSCTRL2_REG,
		6, AW88395_MUTE_VOL, 0, aw88395_volume_get,
		aw88395_volume_set),
	SOC_SINGLE_EXT("Fade Step", 0, 0, AW88395_MUTE_VOL, 0,
		aw88395_get_fade_step, aw88395_set_fade_step),
	SOC_SINGLE_EXT("Volume Ramp Up Step", 0, 0, FADE_TIME_MAX, FADE_TIME_MIN,
		aw88395_get_fade_in_time, aw88395_set_fade_in_time),
	SOC_SINGLE_EXT("Volume Ramp Down Step", 0, 0, FADE_TIME_MAX, FADE_TIME_MIN,
		aw88395_get_fade_out_time, aw88395_set_fade_out_time),
	SOC_SINGLE_EXT("Calib", 0, 0, 100, 0,
		aw88395_re_get, aw88395_re_set),
	AW88395_PROFILE_EXT("Profile Set", aw88395_profile_info,
		aw88395_profile_get, aw88395_profile_set),
};

static int aw88395_playback_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(component);

	mutex_lock(&aw88395->lock);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		aw88395_start(aw88395, AW88395_ASYNC_START);
		break;
	case SND_SOC_DAPM_POST_PMD:
		aw88395_dev_stop(aw88395->aw_pa);
		break;
	default:
		break;
	}
	mutex_unlock(&aw88395->lock);

	return 0;
}

static const struct snd_soc_dapm_widget aw88395_dapm_widgets[] = {
	 /* playback */
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "Speaker_Playback", 0, 0, 0, 0,
					aw88395_playback_event,
					SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("DAC Output"),

	/* capture */
	SND_SOC_DAPM_AIF_OUT("AIF_TX", "Speaker_Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("ADC Input"),
};

static const struct snd_soc_dapm_route aw88395_audio_map[] = {
	{"DAC Output", NULL, "AIF_RX"},
	{"AIF_TX", NULL, "ADC Input"},
};

static int aw88395_codec_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(component);
	int ret;

	INIT_DELAYED_WORK(&aw88395->start_work, aw88395_startup_work);

	/* add widgets */
	ret = snd_soc_dapm_new_controls(dapm, aw88395_dapm_widgets,
							ARRAY_SIZE(aw88395_dapm_widgets));
	if (ret < 0)
		return ret;

	/* add route */
	ret = snd_soc_dapm_add_routes(dapm, aw88395_audio_map,
							ARRAY_SIZE(aw88395_audio_map));
	if (ret < 0)
		return ret;

	ret = snd_soc_add_component_controls(component, aw88395_controls,
							ARRAY_SIZE(aw88395_controls));

	return ret;
}

static void aw88395_codec_remove(struct snd_soc_component *aw_codec)
{
	struct aw88395 *aw88395 = snd_soc_component_get_drvdata(aw_codec);

	cancel_delayed_work_sync(&aw88395->start_work);
}

static const struct snd_soc_component_driver soc_codec_dev_aw88395 = {
	.probe = aw88395_codec_probe,
	.remove = aw88395_codec_remove,
};

static struct aw88395 *aw88395_malloc_init(struct i2c_client *i2c)
{
	struct aw88395 *aw88395 = devm_kzalloc(&i2c->dev,
			sizeof(struct aw88395), GFP_KERNEL);
	if (!aw88395)
		return NULL;

	mutex_init(&aw88395->lock);

	return aw88395;
}

static void aw88395_hw_reset(struct aw88395 *aw88395)
{
	if (aw88395->reset_gpio) {
		gpiod_set_value_cansleep(aw88395->reset_gpio, 0);
		usleep_range(AW88395_1000_US, AW88395_1000_US + 10);
		gpiod_set_value_cansleep(aw88395->reset_gpio, 1);
		usleep_range(AW88395_1000_US, AW88395_1000_US + 10);
	} else {
		dev_err(aw88395->aw_pa->dev, "%s failed", __func__);
	}
}

static int aw88395_request_firmware_file(struct aw88395 *aw88395)
{
	const struct firmware *cont = NULL;
	int ret;

	aw88395->aw_pa->fw_status = AW88395_DEV_FW_FAILED;

	ret = request_firmware(&cont, AW88395_ACF_FILE, aw88395->aw_pa->dev);
	if ((ret < 0) || (!cont)) {
		dev_err(aw88395->aw_pa->dev, "load [%s] failed!", AW88395_ACF_FILE);
		return ret;
	}

	dev_info(aw88395->aw_pa->dev, "loaded %s - size: %zu\n",
			AW88395_ACF_FILE, cont ? cont->size : 0);

	aw88395->aw_cfg = devm_kzalloc(aw88395->aw_pa->dev, cont->size + sizeof(int), GFP_KERNEL);
	if (!aw88395->aw_cfg) {
		release_firmware(cont);
		return -ENOMEM;
	}
	aw88395->aw_cfg->len = (int)cont->size;
	memcpy(aw88395->aw_cfg->data, cont->data, cont->size);
	release_firmware(cont);

	ret = aw88395_dev_load_acf_check(aw88395->aw_pa, aw88395->aw_cfg);
	if (ret < 0) {
		dev_err(aw88395->aw_pa->dev, "Load [%s] failed ....!", AW88395_ACF_FILE);
		return ret;
	}

	dev_dbg(aw88395->aw_pa->dev, "%s : bin load success\n", __func__);

	mutex_lock(&aw88395->lock);
	/* aw device init */
	ret = aw88395_dev_init(aw88395->aw_pa, aw88395->aw_cfg);
	if (ret < 0)
		dev_err(aw88395->aw_pa->dev, "dev init failed");
	mutex_unlock(&aw88395->lock);

	return ret;
}

static int aw88395_i2c_probe(struct i2c_client *i2c)
{
	struct aw88395 *aw88395;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		dev_err(&i2c->dev, "check_functionality failed");
		return -EIO;
	}

	aw88395 = aw88395_malloc_init(i2c);
	if (!aw88395) {
		dev_err(&i2c->dev, "malloc aw88395 failed");
		return -ENOMEM;
	}
	i2c_set_clientdata(i2c, aw88395);

	aw88395->reset_gpio = devm_gpiod_get_optional(&i2c->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(aw88395->reset_gpio))
		dev_info(&i2c->dev, "reset gpio not defined\n");

	/* hardware reset */
	aw88395_hw_reset(aw88395);

	aw88395->regmap = devm_regmap_init_i2c(i2c, &aw88395_remap_config);
	if (IS_ERR(aw88395->regmap)) {
		ret = PTR_ERR(aw88395->regmap);
		dev_err(&i2c->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	/* aw pa init */
	ret = aw88395_init(&aw88395->aw_pa, i2c, aw88395->regmap);
	if (ret < 0)
		return ret;

	ret = aw88395_request_firmware_file(aw88395);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s failed\n", __func__);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_aw88395,
			aw88395_dai, ARRAY_SIZE(aw88395_dai));
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to register aw88395: %d", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id aw88395_i2c_id[] = {
	{ AW88395_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw88395_i2c_id);

static struct i2c_driver aw88395_i2c_driver = {
	.driver = {
		.name = AW88395_I2C_NAME,
	},
	.probe = aw88395_i2c_probe,
	.id_table = aw88395_i2c_id,
};
module_i2c_driver(aw88395_i2c_driver);

MODULE_DESCRIPTION("ASoC AW88395 Smart PA Driver");
MODULE_LICENSE("GPL v2");
