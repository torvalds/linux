// SPDX-License-Identifier: GPL-2.0

// Copyright (c) 2019 MediaTek Inc.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>

#include "mt6660.h"

struct reg_size_table {
	u32 addr;
	u8 size;
};

static const struct reg_size_table mt6660_reg_size_table[] = {
	{ MT6660_REG_HPF1_COEF, 4 },
	{ MT6660_REG_HPF2_COEF, 4 },
	{ MT6660_REG_TDM_CFG3, 2 },
	{ MT6660_REG_RESV17, 2 },
	{ MT6660_REG_RESV23, 2 },
	{ MT6660_REG_SIGMAX, 2 },
	{ MT6660_REG_DEVID, 2 },
	{ MT6660_REG_HCLIP_CTRL, 2 },
	{ MT6660_REG_DA_GAIN, 2 },
};

static int mt6660_get_reg_size(uint32_t addr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt6660_reg_size_table); i++) {
		if (mt6660_reg_size_table[i].addr == addr)
			return mt6660_reg_size_table[i].size;
	}
	return 1;
}

static int mt6660_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct mt6660_chip *chip = context;
	int size = mt6660_get_reg_size(reg);
	u8 reg_data[4];
	int i, ret;

	for (i = 0; i < size; i++)
		reg_data[size - i - 1] = (val >> (8 * i)) & 0xff;

	ret = i2c_smbus_write_i2c_block_data(chip->i2c, reg, size, reg_data);
	return ret;
}

static int mt6660_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mt6660_chip *chip = context;
	int size = mt6660_get_reg_size(reg);
	int i, ret;
	u8 data[4];
	u32 reg_data = 0;

	ret = i2c_smbus_read_i2c_block_data(chip->i2c, reg, size, data);
	if (ret < 0)
		return ret;
	for (i = 0; i < size; i++) {
		reg_data <<= 8;
		reg_data |= data[i];
	}
	*val = reg_data;
	return 0;
}

static const struct regmap_config mt6660_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_write = mt6660_reg_write,
	.reg_read = mt6660_reg_read,
};

static int mt6660_codec_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	if (event == SND_SOC_DAPM_POST_PMU)
		usleep_range(1000, 1100);
	return 0;
}

static int mt6660_codec_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(component->dev,
			"%s: before classd turn on\n", __func__);
		/* config to adaptive mode */
		ret = snd_soc_component_update_bits(component,
			MT6660_REG_BST_CTRL, 0x03, 0x03);
		if (ret < 0) {
			dev_err(component->dev, "config mode adaptive fail\n");
			return ret;
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* voltage sensing enable */
		ret = snd_soc_component_update_bits(component,
			MT6660_REG_RESV7, 0x04, 0x04);
		if (ret < 0) {
			dev_err(component->dev,
				"enable voltage sensing fail\n");
			return ret;
		}
		dev_dbg(component->dev, "Amp on\n");
		break;
	case SND_SOC_DAPM_PRE_PMD:
		dev_dbg(component->dev, "Amp off\n");
		/* voltage sensing disable */
		ret = snd_soc_component_update_bits(component,
			MT6660_REG_RESV7, 0x04, 0x00);
		if (ret < 0) {
			dev_err(component->dev,
				"disable voltage sensing fail\n");
			return ret;
		}
		/* pop-noise improvement 1 */
		ret = snd_soc_component_update_bits(component,
			MT6660_REG_RESV10, 0x10, 0x10);
		if (ret < 0) {
			dev_err(component->dev,
				"pop-noise improvement 1 fail\n");
			return ret;
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(component->dev,
			"%s: after classd turn off\n", __func__);
		/* pop-noise improvement 2 */
		ret = snd_soc_component_update_bits(component,
			MT6660_REG_RESV10, 0x10, 0x00);
		if (ret < 0) {
			dev_err(component->dev,
				"pop-noise improvement 2 fail\n");
			return ret;
		}
		/* config to off mode */
		ret = snd_soc_component_update_bits(component,
			MT6660_REG_BST_CTRL, 0x03, 0x00);
		if (ret < 0) {
			dev_err(component->dev, "config mode off fail\n");
			return ret;
		}
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget mt6660_component_dapm_widgets[] = {
	SND_SOC_DAPM_DAC_E("DAC", NULL, MT6660_REG_PLL_CFG1,
		0, 1, mt6660_codec_dac_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC("VI ADC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA("PGA", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV_E("ClassD", MT6660_REG_SYSTEM_CTRL, 2, 0,
			       NULL, 0, mt6660_codec_classd_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			       SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("OUTP"),
	SND_SOC_DAPM_OUTPUT("OUTN"),
};

static const struct snd_soc_dapm_route mt6660_component_dapm_routes[] = {
	{ "DAC", NULL, "aif_playback" },
	{ "PGA", NULL, "DAC" },
	{ "ClassD", NULL, "PGA" },
	{ "OUTP", NULL, "ClassD" },
	{ "OUTN", NULL, "ClassD" },
	{ "VI ADC", NULL, "ClassD" },
	{ "aif_capture", NULL, "VI ADC" },
};

static int mt6660_component_get_volsw(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct mt6660_chip *chip = (struct mt6660_chip *)
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = chip->chip_rev & 0x0f;
	return 0;
}

static const DECLARE_TLV_DB_SCALE(vol_ctl_tlv, -1155, 5, 0);

static const struct snd_kcontrol_new mt6660_component_snd_controls[] = {
	SOC_SINGLE_TLV("Digital Volume", MT6660_REG_VOL_CTRL, 0, 255,
			   1, vol_ctl_tlv),
	SOC_SINGLE("Hard Clip Switch", MT6660_REG_HCLIP_CTRL, 8, 1, 0),
	SOC_SINGLE("Clip Switch", MT6660_REG_SPS_CTRL, 0, 1, 0),
	SOC_SINGLE("Boost Mode", MT6660_REG_BST_CTRL, 0, 3, 0),
	SOC_SINGLE("DRE Switch", MT6660_REG_DRE_CTRL, 0, 1, 0),
	SOC_SINGLE("DC Protect Switch",	MT6660_REG_DC_PROTECT_CTRL, 3, 1, 0),
	SOC_SINGLE("Data Output Left Channel Selection",
		   MT6660_REG_DATAO_SEL, 3, 7, 0),
	SOC_SINGLE("Data Output Right Channel Selection",
		   MT6660_REG_DATAO_SEL, 0, 7, 0),
	SOC_SINGLE_EXT("T0 SEL", MT6660_REG_CALI_T0, 0, 7, 0,
		       snd_soc_get_volsw, NULL),
	SOC_SINGLE_EXT("Chip Rev", MT6660_REG_DEVID, 8, 15, 0,
		       mt6660_component_get_volsw, NULL),
};

static int _mt6660_chip_power_on(struct mt6660_chip *chip, int on_off)
{
	return regmap_write_bits(chip->regmap, MT6660_REG_SYSTEM_CTRL,
				 0x01, on_off ? 0x00 : 0x01);
}

struct reg_table {
	uint32_t addr;
	uint32_t mask;
	uint32_t val;
};

static const struct reg_table mt6660_setting_table[] = {
	{ 0x20, 0x80, 0x00 },
	{ 0x30, 0x01, 0x00 },
	{ 0x50, 0x1c, 0x04 },
	{ 0xB1, 0x0c, 0x00 },
	{ 0xD3, 0x03, 0x03 },
	{ 0xE0, 0x01, 0x00 },
	{ 0x98, 0x44, 0x04 },
	{ 0xB9, 0xff, 0x82 },
	{ 0xB7, 0x7777, 0x7273 },
	{ 0xB6, 0x07, 0x03 },
	{ 0x6B, 0xe0, 0x20 },
	{ 0x07, 0xff, 0x70 },
	{ 0xBB, 0xff, 0x20 },
	{ 0x69, 0xff, 0x40 },
	{ 0xBD, 0xffff, 0x17f8 },
	{ 0x70, 0xff, 0x15 },
	{ 0x7C, 0xff, 0x00 },
	{ 0x46, 0xff, 0x1d },
	{ 0x1A, 0xffffffff, 0x7fdb7ffe },
	{ 0x1B, 0xffffffff, 0x7fdb7ffe },
	{ 0x51, 0xff, 0x58 },
	{ 0xA2, 0xff, 0xce },
	{ 0x33, 0xffff, 0x7fff },
	{ 0x4C, 0xffff, 0x0116 },
	{ 0x16, 0x1800, 0x0800 },
	{ 0x68, 0x1f, 0x07 },
};

static int mt6660_component_setting(struct snd_soc_component *component)
{
	struct mt6660_chip *chip = snd_soc_component_get_drvdata(component);
	int ret = 0;
	size_t i = 0;

	ret = _mt6660_chip_power_on(chip, 1);
	if (ret < 0) {
		dev_err(component->dev, "%s chip power on failed\n", __func__);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(mt6660_setting_table); i++) {
		ret = snd_soc_component_update_bits(component,
				mt6660_setting_table[i].addr,
				mt6660_setting_table[i].mask,
				mt6660_setting_table[i].val);
		if (ret < 0) {
			dev_err(component->dev, "%s update 0x%02x failed\n",
				__func__, mt6660_setting_table[i].addr);
			return ret;
		}
	}

	ret = _mt6660_chip_power_on(chip, 0);
	if (ret < 0) {
		dev_err(component->dev, "%s chip power off failed\n", __func__);
		return ret;
	}

	return 0;
}

static int mt6660_component_probe(struct snd_soc_component *component)
{
	struct mt6660_chip *chip = snd_soc_component_get_drvdata(component);
	int ret;

	dev_dbg(component->dev, "%s\n", __func__);
	snd_soc_component_init_regmap(component, chip->regmap);

	ret = mt6660_component_setting(component);
	if (ret < 0)
		dev_err(chip->dev, "mt6660 component setting failed\n");

	return ret;
}

static void mt6660_component_remove(struct snd_soc_component *component)
{
	dev_dbg(component->dev, "%s\n", __func__);
	snd_soc_component_exit_regmap(component);
}

static const struct snd_soc_component_driver mt6660_component_driver = {
	.probe = mt6660_component_probe,
	.remove = mt6660_component_remove,

	.controls = mt6660_component_snd_controls,
	.num_controls = ARRAY_SIZE(mt6660_component_snd_controls),
	.dapm_widgets = mt6660_component_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6660_component_dapm_widgets),
	.dapm_routes = mt6660_component_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6660_component_dapm_routes),

	.idle_bias_on = false, /* idle_bias_off = true */
};

static int mt6660_component_aif_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *dai)
{
	int word_len = params_physical_width(hw_params);
	int aud_bit = params_width(hw_params);
	u16 reg_data = 0;
	int ret;

	dev_dbg(dai->dev, "%s: ++\n", __func__);
	dev_dbg(dai->dev, "format: 0x%08x\n", params_format(hw_params));
	dev_dbg(dai->dev, "rate: 0x%08x\n", params_rate(hw_params));
	dev_dbg(dai->dev, "word_len: %d, aud_bit: %d\n", word_len, aud_bit);
	if (word_len > 32 || word_len < 16) {
		dev_err(dai->dev, "not supported word length\n");
		return -ENOTSUPP;
	}
	switch (aud_bit) {
	case 16:
		reg_data = 3;
		break;
	case 18:
		reg_data = 2;
		break;
	case 20:
		reg_data = 1;
		break;
	case 24:
	case 32:
		reg_data = 0;
		break;
	default:
		return -ENOTSUPP;
	}
	ret = snd_soc_component_update_bits(dai->component,
		MT6660_REG_SERIAL_CFG1, 0xc0, (reg_data << 6));
	if (ret < 0) {
		dev_err(dai->dev, "config aud bit fail\n");
		return ret;
	}
	ret = snd_soc_component_update_bits(dai->component,
		MT6660_REG_TDM_CFG3, 0x3f0, word_len << 4);
	if (ret < 0) {
		dev_err(dai->dev, "config word len fail\n");
		return ret;
	}
	dev_dbg(dai->dev, "%s: --\n", __func__);
	return 0;
}

static const struct snd_soc_dai_ops mt6660_component_aif_ops = {
	.hw_params = mt6660_component_aif_hw_params,
};

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_U32_LE)

static struct snd_soc_dai_driver mt6660_codec_dai = {
	.name = "mt6660-aif",
	.playback = {
		.stream_name	= "aif_playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
	.capture = {
		.stream_name	= "aif_capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates = STUB_RATES,
		.formats = STUB_FORMATS,
	},
	/* dai properties */
	.symmetric_rates = 1,
	.symmetric_channels = 1,
	.symmetric_samplebits = 1,
	/* dai operations */
	.ops = &mt6660_component_aif_ops,
};

static int _mt6660_chip_id_check(struct mt6660_chip *chip)
{
	int ret;
	unsigned int val;

	ret = regmap_read(chip->regmap, MT6660_REG_DEVID, &val);
	if (ret < 0)
		return ret;
	val &= 0x0ff0;
	if (val != 0x00e0 && val != 0x01e0) {
		dev_err(chip->dev, "%s id(%x) not match\n", __func__, val);
		return -ENODEV;
	}
	return 0;
}

static int _mt6660_chip_sw_reset(struct mt6660_chip *chip)
{
	int ret;

	/* turn on main pll first, then trigger reset */
	ret = regmap_write(chip->regmap, MT6660_REG_SYSTEM_CTRL, 0x00);
	if (ret < 0)
		return ret;
	ret = regmap_write(chip->regmap, MT6660_REG_SYSTEM_CTRL, 0x80);
	if (ret < 0)
		return ret;
	msleep(30);
	return 0;
}

static int _mt6660_read_chip_revision(struct mt6660_chip *chip)
{
	int ret;
	unsigned int val;

	ret = regmap_read(chip->regmap, MT6660_REG_DEVID, &val);
	if (ret < 0) {
		dev_err(chip->dev, "get chip revision fail\n");
		return ret;
	}
	chip->chip_rev = val&0xff;
	dev_info(chip->dev, "%s chip_rev = %x\n", __func__, chip->chip_rev);
	return 0;
}

static int mt6660_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct mt6660_chip *chip = NULL;
	int ret;

	dev_dbg(&client->dev, "%s\n", __func__);
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->i2c = client;
	chip->dev = &client->dev;
	mutex_init(&chip->io_lock);
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init(&client->dev,
		NULL, chip, &mt6660_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "failed to initialise regmap: %d\n", ret);
		return ret;
	}

	/* chip reset first */
	ret = _mt6660_chip_sw_reset(chip);
	if (ret < 0) {
		dev_err(chip->dev, "chip reset fail\n");
		goto probe_fail;
	}
	/* chip power on */
	ret = _mt6660_chip_power_on(chip, 1);
	if (ret < 0) {
		dev_err(chip->dev, "chip power on 2 fail\n");
		goto probe_fail;
	}
	/* chip devid check */
	ret = _mt6660_chip_id_check(chip);
	if (ret < 0) {
		dev_err(chip->dev, "chip id check fail\n");
		goto probe_fail;
	}
	/* chip revision get */
	ret = _mt6660_read_chip_revision(chip);
	if (ret < 0) {
		dev_err(chip->dev, "read chip revision fail\n");
		goto probe_fail;
	}
	pm_runtime_set_active(chip->dev);
	pm_runtime_enable(chip->dev);

	ret = devm_snd_soc_register_component(chip->dev,
					       &mt6660_component_driver,
					       &mt6660_codec_dai, 1);
	return ret;
probe_fail:
	_mt6660_chip_power_on(chip, 0);
	mutex_destroy(&chip->io_lock);
	return ret;
}

static int mt6660_i2c_remove(struct i2c_client *client)
{
	struct mt6660_chip *chip = i2c_get_clientdata(client);

	pm_runtime_disable(chip->dev);
	pm_runtime_set_suspended(chip->dev);
	mutex_destroy(&chip->io_lock);
	return 0;
}

static int __maybe_unused mt6660_i2c_runtime_suspend(struct device *dev)
{
	struct mt6660_chip *chip = dev_get_drvdata(dev);

	dev_dbg(dev, "enter low power mode\n");
	return regmap_update_bits(chip->regmap,
		MT6660_REG_SYSTEM_CTRL, 0x01, 0x01);
}

static int __maybe_unused mt6660_i2c_runtime_resume(struct device *dev)
{
	struct mt6660_chip *chip = dev_get_drvdata(dev);

	dev_dbg(dev, "exit low power mode\n");
	return regmap_update_bits(chip->regmap,
		MT6660_REG_SYSTEM_CTRL, 0x01, 0x00);
}

static const struct dev_pm_ops mt6660_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(mt6660_i2c_runtime_suspend,
			   mt6660_i2c_runtime_resume, NULL)
};

static const struct of_device_id __maybe_unused mt6660_of_id[] = {
	{ .compatible = "mediatek,mt6660",},
	{},
};
MODULE_DEVICE_TABLE(of, mt6660_of_id);

static const struct i2c_device_id mt6660_i2c_id[] = {
	{"mt6660", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, mt6660_i2c_id);

static struct i2c_driver mt6660_i2c_driver = {
	.driver = {
		.name = "mt6660",
		.of_match_table = of_match_ptr(mt6660_of_id),
		.pm = &mt6660_dev_pm_ops,
	},
	.probe = mt6660_i2c_probe,
	.remove = mt6660_i2c_remove,
	.id_table = mt6660_i2c_id,
};
module_i2c_driver(mt6660_i2c_driver);

MODULE_AUTHOR("Jeff Chang <jeff_chang@richtek.com>");
MODULE_DESCRIPTION("MT6660 SPKAMP Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.8_G");
