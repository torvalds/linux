// SPDX-License-Identifier: GPL-2.0-only
/*
 * rt5575.c  --  ALC5575 ALSA SoC audio component driver
 *
 * Copyright(c) 2025 Realtek Semiconductor Corp.
 *
 */

#include <linux/i2c.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rt5575.h"
#include "rt5575-spi.h"

static bool rt5575_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5575_BOOT:
	case RT5575_ID:
	case RT5575_ID_1:
	case RT5575_MIXL_VOL:
	case RT5575_MIXR_VOL:
	case RT5575_PROMPT_VOL:
	case RT5575_SPK01_VOL:
	case RT5575_SPK23_VOL:
	case RT5575_MIC1_VOL:
	case RT5575_MIC2_VOL:
	case RT5575_WNC_CTRL:
	case RT5575_MODE_CTRL:
	case RT5575_I2S_RATE_CTRL:
	case RT5575_SLEEP_CTRL:
	case RT5575_ALG_BYPASS_CTRL:
	case RT5575_PINMUX_CTRL_2:
	case RT5575_GPIO_CTRL_1:
	case RT5575_DSP_BUS_CTRL:
	case RT5575_SW_INT:
	case RT5575_DSP_BOOT_ERR:
	case RT5575_DSP_READY:
	case RT5575_DSP_CMD_ADDR:
	case RT5575_EFUSE_DATA_2:
	case RT5575_EFUSE_DATA_3:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(ob_tlv, -9525, 75, 0);

static const struct snd_kcontrol_new rt5575_snd_controls[] = {
	SOC_DOUBLE("Speaker CH-01 Playback Switch", RT5575_SPK01_VOL, 31, 15, 1, 1),
	SOC_DOUBLE_TLV("Speaker CH-01 Playback Volume", RT5575_SPK01_VOL, 17, 1, 167, 0, ob_tlv),
	SOC_DOUBLE("Speaker CH-23 Playback Switch", RT5575_SPK23_VOL, 31, 15, 1, 1),
	SOC_DOUBLE_TLV("Speaker CH-23 Playback Volume", RT5575_SPK23_VOL, 17, 1, 167, 0, ob_tlv),
	SOC_DOUBLE("Mic1 Capture Switch", RT5575_MIC1_VOL, 31, 15, 1, 1),
	SOC_DOUBLE_TLV("Mic1 Capture Volume", RT5575_MIC1_VOL, 17, 1, 167, 0, ob_tlv),
	SOC_DOUBLE("Mic2 Capture Switch", RT5575_MIC2_VOL, 31, 15, 1, 1),
	SOC_DOUBLE_TLV("Mic2 Capture Volume", RT5575_MIC2_VOL, 17, 1, 167, 0, ob_tlv),
	SOC_DOUBLE_R("Mix Playback Switch", RT5575_MIXL_VOL, RT5575_MIXR_VOL, 31, 1, 1),
	SOC_DOUBLE_R_TLV("Mix Playback Volume", RT5575_MIXL_VOL, RT5575_MIXR_VOL, 1, 127, 0,
		ob_tlv),
	SOC_DOUBLE("Prompt Playback Switch", RT5575_PROMPT_VOL, 31, 15, 1, 1),
	SOC_DOUBLE_TLV("Prompt Playback Volume", RT5575_PROMPT_VOL, 17, 1, 167, 0, ob_tlv),
};

static const struct snd_soc_dapm_widget rt5575_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "AIF3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "AIF3 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF4RX", "AIF4 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF4TX", "AIF4 Capture", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_INPUT("INPUT"),
	SND_SOC_DAPM_OUTPUT("OUTPUT"),
};

static const struct snd_soc_dapm_route rt5575_dapm_routes[] = {
	{ "AIF1TX", NULL, "INPUT" },
	{ "AIF2TX", NULL, "INPUT" },
	{ "AIF3TX", NULL, "INPUT" },
	{ "AIF4TX", NULL, "INPUT" },
	{ "OUTPUT", NULL, "AIF1RX" },
	{ "OUTPUT", NULL, "AIF2RX" },
	{ "OUTPUT", NULL, "AIF3RX" },
	{ "OUTPUT", NULL, "AIF4RX" },
};

static long long rt5575_get_priv_id(struct rt5575_priv *rt5575)
{
	int priv_id_low, priv_id_high;

	regmap_write(rt5575->regmap, RT5575_EFUSE_PID, 0xa0000000);
	regmap_read(rt5575->regmap, RT5575_EFUSE_DATA_2, &priv_id_low);
	regmap_read(rt5575->regmap, RT5575_EFUSE_DATA_3, &priv_id_high);
	regmap_write(rt5575->regmap, RT5575_EFUSE_PID, 0);

	return ((long long)priv_id_high << 32) | (long long)priv_id_low;
}

static int rt5575_probe(struct snd_soc_component *component)
{
	struct rt5575_priv *rt5575 = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;

	rt5575->component = component;

	dev_info(dev, "Private ID: %llx\n", rt5575_get_priv_id(rt5575));

	return 0;
}

#define RT5575_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5575_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver rt5575_dai[] = {
	{
		.name = "rt5575-aif1",
		.id = RT5575_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
	},
	{
		.name = "rt5575-aif2",
		.id = RT5575_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
	},
	{
		.name = "rt5575-aif3",
		.id = RT5575_AIF3,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
	},
	{
		.name = "rt5575-aif4",
		.id = RT5575_AIF4,
		.playback = {
			.stream_name = "AIF4 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
		.capture = {
			.stream_name = "AIF4 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = RT5575_STEREO_RATES,
			.formats = RT5575_FORMATS,
		},
	},
};

static const struct snd_soc_component_driver rt5575_soc_component_dev = {
	.probe = rt5575_probe,
	.controls = rt5575_snd_controls,
	.num_controls = ARRAY_SIZE(rt5575_snd_controls),
	.dapm_widgets = rt5575_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5575_dapm_widgets),
	.dapm_routes = rt5575_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5575_dapm_routes),
	.use_pmdown_time = 1,
	.endianness = 1,
};

static const struct regmap_config rt5575_dsp_regmap = {
	.name = "dsp",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 2,
};

static int rt5575_i2c_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	struct rt5575_priv *rt5575 = i2c_get_clientdata(client);

	return regmap_read(rt5575->dsp_regmap, reg | RT5575_DSP_MAPPING, val);
}

static int rt5575_i2c_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	struct rt5575_priv *rt5575 = i2c_get_clientdata(client);

	return regmap_write(rt5575->dsp_regmap, reg | RT5575_DSP_MAPPING, val);
}

static const struct regmap_config rt5575_regmap = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0xfffc,
	.readable_reg = rt5575_readable_register,
	.reg_read = rt5575_i2c_read,
	.reg_write = rt5575_i2c_write,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt5575_fw_load_by_spi(struct rt5575_priv *rt5575)
{
	struct i2c_client *i2c = rt5575->i2c;
	struct spi_device *spi;
	struct device *dev = &i2c->dev;
	int ret;

	spi = rt5575_spi_get_device(dev);
	if (!spi) {
		dev_err(dev, "Failed to get spi_device\n");
		return -ENODEV;
	}

	regmap_write(rt5575->dsp_regmap, 0xfafafafa, 0x00000004);
	regmap_write(rt5575->dsp_regmap, 0x18008064, 0x00000000);
	regmap_write(rt5575->dsp_regmap, 0x18008068, 0x0002ffff);

	ret = rt5575_spi_fw_load(spi);
	if (ret) {
		dev_err(dev, "Load firmware failure: %d\n", ret);
		return -ENODEV;
	}

	regmap_write(rt5575->dsp_regmap, 0x18000000, 0x00000000);
	regmap_update_bits(rt5575->regmap, RT5575_SW_INT, 1, 1);

	regmap_read_poll_timeout(rt5575->regmap, RT5575_SW_INT, ret, !ret, 100000, 10000000);
	if (ret) {
		dev_err(dev, "Run firmware failure: %d\n", ret);
		return -ENODEV;
	}

	return 0;
}

static int rt5575_i2c_probe(struct i2c_client *i2c)
{
	struct rt5575_priv *rt5575;
	int ret, val, boot;
	struct device *dev = &i2c->dev;

	rt5575 = devm_kzalloc(dev, sizeof(struct rt5575_priv), GFP_KERNEL);
	if (!rt5575)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5575);

	rt5575->i2c = i2c;

	rt5575->dsp_regmap = devm_regmap_init_i2c(i2c, &rt5575_dsp_regmap);
	if (IS_ERR(rt5575->dsp_regmap)) {
		ret = PTR_ERR(rt5575->dsp_regmap);
		dev_err(dev, "Failed to allocate DSP register map: %d\n", ret);
		return ret;
	}

	rt5575->regmap = devm_regmap_init(dev, NULL, i2c, &rt5575_regmap);
	if (IS_ERR(rt5575->regmap)) {
		ret = PTR_ERR(rt5575->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	regmap_read(rt5575->regmap, RT5575_ID, &val);
	if (val != RT5575_DEVICE_ID) {
		dev_err(dev, "Device with ID register %08x is not rt5575\n", val);
		return -ENODEV;
	}

	regmap_read(rt5575->regmap, RT5575_BOOT, &boot);
	if ((boot & RT5575_BOOT_MASK) == RT5575_BOOT_SPI) {
		if (!IS_ENABLED(CONFIG_SND_SOC_RT5575_SPI)) {
			dev_err(dev, "Please enable CONFIG_SND_SOC_RT5575_SPI\n");
			return -ENODEV;
		}

		if (rt5575_fw_load_by_spi(rt5575))
			return -ENODEV;
	}

	return devm_snd_soc_register_component(dev, &rt5575_soc_component_dev, rt5575_dai,
					       ARRAY_SIZE(rt5575_dai));
}

static const struct i2c_device_id rt5575_i2c_id[] = {
	{ "rt5575" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5575_i2c_id);

static const struct of_device_id rt5575_of_match[] = {
	{ .compatible = "realtek,rt5575" },
	{ }
};
MODULE_DEVICE_TABLE(of, rt5575_of_match);

static struct i2c_driver rt5575_i2c_driver = {
	.driver = {
		.name = "rt5575",
		.owner = THIS_MODULE,
		.of_match_table = rt5575_of_match,
	},
	.probe = rt5575_i2c_probe,
	.id_table = rt5575_i2c_id,
};
module_i2c_driver(rt5575_i2c_driver);

MODULE_DESCRIPTION("ASoC ALC5575 driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL");
