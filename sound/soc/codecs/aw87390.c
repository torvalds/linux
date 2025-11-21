// SPDX-License-Identifier: GPL-2.0-only
//
// aw87390.c  --  AW87390 ALSA SoC Audio driver
//
// Copyright (c) 2023 awinic Technology CO., LTD
//
// Author: Weidong Wang <wangweidong.a@awinic.com>
//

#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "aw87390.h"
#include "aw88395/aw88395_data_type.h"
#include "aw88395/aw88395_device.h"

static const struct regmap_config aw87390_remap_config = {
	.val_bits = 8,
	.reg_bits = 8,
	.max_register = AW87390_REG_MAX,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static int aw87390_dev_reg_update(struct aw_device *aw_dev,
					unsigned char *data, unsigned int len)
{
	int i, ret;

	if (!data) {
		dev_err(aw_dev->dev, "data is NULL\n");
		return -EINVAL;
	}

	for (i = 0; i < len-1; i += 2) {
		if (data[i] == AW87390_DELAY_REG_ADDR) {
			usleep_range(data[i + 1] * AW87390_REG_DELAY_TIME,
					data[i + 1] * AW87390_REG_DELAY_TIME + 10);
			continue;
		}
		ret = regmap_write(aw_dev->regmap, data[i], data[i + 1]);
		if (ret)
			return ret;
	}

	return 0;
}

static int aw87390_dev_get_prof_name(struct aw_device *aw_dev, int index, char **prof_name)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_prof_desc *prof_desc;

	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		dev_err(aw_dev->dev, "index[%d] overflow count[%d]\n",
			index, aw_dev->prof_info.count);
		return -EINVAL;
	}

	prof_desc = &aw_dev->prof_info.prof_desc[index];

	*prof_name = prof_info->prof_name_list[prof_desc->id];

	return 0;
}

static int aw87390_dev_get_prof_data(struct aw_device *aw_dev, int index,
			struct aw_prof_desc **prof_desc)
{
	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		dev_err(aw_dev->dev, "%s: index[%d] overflow count[%d]\n",
				__func__, index, aw_dev->prof_info.count);
		return -EINVAL;
	}

	*prof_desc = &aw_dev->prof_info.prof_desc[index];

	return 0;
}

static int aw87390_dev_fw_update(struct aw_device *aw_dev)
{
	struct aw_prof_desc *prof_index_desc;
	struct aw_sec_data_desc *sec_desc;
	char *prof_name;
	int ret;

	ret = aw87390_dev_get_prof_name(aw_dev, aw_dev->prof_index, &prof_name);
	if (ret) {
		dev_err(aw_dev->dev, "get prof name failed\n");
		return -EINVAL;
	}

	dev_dbg(aw_dev->dev, "start update %s", prof_name);

	ret = aw87390_dev_get_prof_data(aw_dev, aw_dev->prof_index, &prof_index_desc);
	if (ret) {
		dev_err(aw_dev->dev, "aw87390_dev_get_prof_data failed\n");
		return ret;
	}

	/* update reg */
	sec_desc = prof_index_desc->sec_desc;
	ret = aw87390_dev_reg_update(aw_dev, sec_desc[AW88395_DATA_TYPE_REG].data,
					sec_desc[AW88395_DATA_TYPE_REG].len);
	if (ret) {
		dev_err(aw_dev->dev, "update reg failed\n");
		return ret;
	}

	aw_dev->prof_cur = aw_dev->prof_index;

	return 0;
}

static int aw87390_power_off(struct aw_device *aw_dev)
{
	int ret;

	if (aw_dev->status == AW87390_DEV_PW_OFF) {
		dev_dbg(aw_dev->dev, "already power off\n");
		return 0;
	}

	ret = regmap_write(aw_dev->regmap, AW87390_SYSCTRL_REG, AW87390_POWER_DOWN_VALUE);
	if (ret)
		return ret;
	aw_dev->status = AW87390_DEV_PW_OFF;

	return 0;
}

static int aw87390_power_on(struct aw_device *aw_dev)
{
	int ret;

	if (aw_dev->status == AW87390_DEV_PW_ON) {
		dev_dbg(aw_dev->dev, "already power on\n");
		return 0;
	}

	if (!aw_dev->fw_status) {
		dev_err(aw_dev->dev, "fw not load\n");
		return -EINVAL;
	}

	ret = regmap_write(aw_dev->regmap, AW87390_SYSCTRL_REG, AW87390_POWER_DOWN_VALUE);
	if (ret)
		return ret;

	ret = aw87390_dev_fw_update(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "%s load profile failed\n", __func__);
		return ret;
	}
	aw_dev->status = AW87390_DEV_PW_ON;

	return 0;
}

static int aw87390_dev_set_profile_index(struct aw_device *aw_dev, int index)
{
	if ((index >= aw_dev->prof_info.count) || (index < 0))
		return -EINVAL;

	if (aw_dev->prof_index == index)
		return -EPERM;

	aw_dev->prof_index = index;

	return 0;
}

static int aw87390_profile_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw87390 *aw87390 = snd_soc_component_get_drvdata(codec);
	char *prof_name;
	int count, ret;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	count = aw87390->aw_pa->prof_info.count;
	if (count <= 0) {
		uinfo->value.enumerated.items = 0;
		return 0;
	}

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	count = uinfo->value.enumerated.item;

	ret = aw87390_dev_get_prof_name(aw87390->aw_pa, count, &prof_name);
	if (ret) {
		strscpy(uinfo->value.enumerated.name, "null");
		return 0;
	}

	strscpy(uinfo->value.enumerated.name, prof_name);

	return 0;
}

static int aw87390_profile_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw87390 *aw87390 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw87390->aw_pa->prof_index;

	return 0;
}

static int aw87390_profile_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw87390 *aw87390 = snd_soc_component_get_drvdata(codec);
	int ret;

	mutex_lock(&aw87390->lock);
	ret = aw87390_dev_set_profile_index(aw87390->aw_pa, ucontrol->value.integer.value[0]);
	if (ret) {
		dev_dbg(codec->dev, "profile index does not change\n");
		mutex_unlock(&aw87390->lock);
		return 0;
	}

	if (aw87390->aw_pa->status == AW87390_DEV_PW_ON) {
		aw87390_power_off(aw87390->aw_pa);
		aw87390_power_on(aw87390->aw_pa);
	}

	mutex_unlock(&aw87390->lock);

	return 1;
}

static const struct snd_kcontrol_new aw87390_controls[] = {
	AW87390_PROFILE_EXT("AW87390 Profile Set", aw87390_profile_info,
		aw87390_profile_get, aw87390_profile_set),
};

static int aw87390_request_firmware_file(struct aw87390 *aw87390)
{
	const struct firmware *cont = NULL;
	int ret;

	aw87390->aw_pa->fw_status = AW87390_DEV_FW_FAILED;

	ret = request_firmware(&cont, AW87390_ACF_FILE, aw87390->aw_pa->dev);
	if (ret)
		return dev_err_probe(aw87390->aw_pa->dev, ret,
					"load [%s] failed!\n", AW87390_ACF_FILE);

	dev_dbg(aw87390->aw_pa->dev, "loaded %s - size: %zu\n",
			AW87390_ACF_FILE, cont ? cont->size : 0);

	aw87390->aw_cfg = devm_kzalloc(aw87390->aw_pa->dev,
				struct_size(aw87390->aw_cfg, data, cont->size), GFP_KERNEL);
	if (!aw87390->aw_cfg) {
		release_firmware(cont);
		return -ENOMEM;
	}

	aw87390->aw_cfg->len = cont->size;
	memcpy(aw87390->aw_cfg->data, cont->data, cont->size);
	release_firmware(cont);

	ret = aw88395_dev_load_acf_check(aw87390->aw_pa, aw87390->aw_cfg);
	if (ret) {
		dev_err(aw87390->aw_pa->dev, "load [%s] failed!\n", AW87390_ACF_FILE);
		return ret;
	}

	mutex_lock(&aw87390->lock);

	ret = aw88395_dev_cfg_load(aw87390->aw_pa, aw87390->aw_cfg);
	if (ret)
		dev_err(aw87390->aw_pa->dev, "aw_dev acf parse failed\n");

	mutex_unlock(&aw87390->lock);

	return ret;
}

static int aw87390_drv_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct aw87390 *aw87390 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw87390->aw_pa;
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = aw87390_power_on(aw_dev);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = aw87390_power_off(aw_dev);
		break;
	default:
		dev_err(aw_dev->dev, "%s: invalid event %d\n", __func__, event);
		ret = -EINVAL;
	}

	return ret;
}

static const struct snd_soc_dapm_widget aw87390_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),
	SND_SOC_DAPM_PGA_E("SPK PA", SND_SOC_NOPM, 0, 0, NULL, 0, aw87390_drv_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route aw87390_dapm_routes[] = {
	{ "SPK PA", NULL, "IN" },
	{ "OUT", NULL, "SPK PA" },
};

static int aw87390_codec_probe(struct snd_soc_component *component)
{
	struct aw87390 *aw87390 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = aw87390_request_firmware_file(aw87390);
	if (ret)
		return dev_err_probe(aw87390->aw_pa->dev, ret,
				"aw87390_request_firmware_file failed\n");

	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_aw87390 = {
	.probe = aw87390_codec_probe,
	.dapm_widgets = aw87390_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aw87390_dapm_widgets),
	.dapm_routes = aw87390_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(aw87390_dapm_routes),
	.controls = aw87390_controls,
	.num_controls = ARRAY_SIZE(aw87390_controls),
};

static void aw87390_parse_channel_dt(struct aw87390 *aw87390)
{
	struct aw_device *aw_dev = aw87390->aw_pa;
	struct device_node *np = aw_dev->dev->of_node;
	u32 channel_value = AW87390_DEV_DEFAULT_CH;

	of_property_read_u32(np, "awinic,audio-channel", &channel_value);

	aw_dev->channel = channel_value;
}

static int aw87390_init(struct aw87390 **aw87390, struct i2c_client *i2c, struct regmap *regmap)
{
	struct aw_device *aw_dev;
	unsigned int chip_id;
	int ret;

	/* read chip id */
	ret = regmap_read(regmap, AW87390_ID_REG, &chip_id);
	if (ret) {
		dev_err(&i2c->dev, "%s read chipid error. ret = %d\n", __func__, ret);
		return ret;
	}

	if (chip_id != AW87390_CHIP_ID) {
		dev_err(&i2c->dev, "unsupported device\n");
		return -ENXIO;
	}

	dev_dbg(&i2c->dev, "chip id = 0x%x\n", chip_id);

	aw_dev = devm_kzalloc(&i2c->dev, sizeof(*aw_dev), GFP_KERNEL);
	if (!aw_dev)
		return -ENOMEM;

	(*aw87390)->aw_pa = aw_dev;
	aw_dev->i2c = i2c;
	aw_dev->regmap = regmap;
	aw_dev->dev = &i2c->dev;
	aw_dev->chip_id = AW87390_CHIP_ID;
	aw_dev->acf = NULL;
	aw_dev->prof_info.prof_desc = NULL;
	aw_dev->prof_info.count = 0;
	aw_dev->prof_info.prof_type = AW88395_DEV_NONE_TYPE_ID;
	aw_dev->channel = AW87390_DEV_DEFAULT_CH;
	aw_dev->fw_status = AW87390_DEV_FW_FAILED;
	aw_dev->prof_index = AW87390_INIT_PROFILE;
	aw_dev->status = AW87390_DEV_PW_OFF;

	aw87390_parse_channel_dt(*aw87390);

	return 0;
}

static int aw87390_i2c_probe(struct i2c_client *i2c)
{
	struct aw87390 *aw87390;
	int ret;

	ret = i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C);
	if (!ret)
		return dev_err_probe(&i2c->dev, -ENXIO, "check_functionality failed\n");

	aw87390 = devm_kzalloc(&i2c->dev, sizeof(*aw87390), GFP_KERNEL);
	if (!aw87390)
		return -ENOMEM;

	mutex_init(&aw87390->lock);

	i2c_set_clientdata(i2c, aw87390);

	aw87390->regmap = devm_regmap_init_i2c(i2c, &aw87390_remap_config);
	if (IS_ERR(aw87390->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(aw87390->regmap),
					"failed to init regmap\n");

	/* aw pa init */
	ret = aw87390_init(&aw87390, i2c, aw87390->regmap);
	if (ret)
		return ret;

	ret = regmap_write(aw87390->regmap, AW87390_ID_REG, AW87390_SOFT_RESET_VALUE);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_component(&i2c->dev,
				&soc_codec_dev_aw87390, NULL, 0);
	if (ret)
		dev_err(&i2c->dev, "failed to register aw87390: %d\n", ret);

	return ret;
}

static const struct i2c_device_id aw87390_i2c_id[] = {
	{ AW87390_I2C_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw87390_i2c_id);

static struct i2c_driver aw87390_i2c_driver = {
	.driver = {
		.name = AW87390_I2C_NAME,
	},
	.probe = aw87390_i2c_probe,
	.id_table = aw87390_i2c_id,
};
module_i2c_driver(aw87390_i2c_driver);

MODULE_DESCRIPTION("ASoC AW87390 PA Driver");
MODULE_LICENSE("GPL v2");
