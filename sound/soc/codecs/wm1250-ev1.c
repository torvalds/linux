// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the 1250-EV1 audio I/O module
 *
 * Copyright 2011 Wolfson Microelectronics plc
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>

struct wm1250_priv {
	struct gpio_desc *clk_ena;
	struct gpio_desc *clk_sel0;
	struct gpio_desc *clk_sel1;
	struct gpio_desc *osr;
	struct gpio_desc *master;
};

static int wm1250_ev1_set_bias_level(struct snd_soc_component *component,
				     enum snd_soc_bias_level level)
{
	struct wm1250_priv *wm1250 = dev_get_drvdata(component->dev);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		gpiod_set_value_cansleep(wm1250->clk_ena, 1);
		break;

	case SND_SOC_BIAS_OFF:
		gpiod_set_value_cansleep(wm1250->clk_ena, 0);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget wm1250_ev1_dapm_widgets[] = {
SND_SOC_DAPM_ADC("ADC", "wm1250-ev1 Capture", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DAC", "wm1250-ev1 Playback", SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_INPUT("WM1250 Input"),
SND_SOC_DAPM_OUTPUT("WM1250 Output"),
};

static const struct snd_soc_dapm_route wm1250_ev1_dapm_routes[] = {
	{ "ADC", NULL, "WM1250 Input" },
	{ "WM1250 Output", NULL, "DAC" },
};

static int wm1250_ev1_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct wm1250_priv *wm1250 = snd_soc_component_get_drvdata(dai->component);

	switch (params_rate(params)) {
	case 8000:
		gpiod_set_value(wm1250->clk_sel0, 1);
		gpiod_set_value(wm1250->clk_sel1, 1);
		break;
	case 16000:
		gpiod_set_value(wm1250->clk_sel0, 0);
		gpiod_set_value(wm1250->clk_sel1, 1);
		break;
	case 32000:
		gpiod_set_value(wm1250->clk_sel0, 1);
		gpiod_set_value(wm1250->clk_sel1, 0);
		break;
	case 64000:
		gpiod_set_value(wm1250->clk_sel0, 0);
		gpiod_set_value(wm1250->clk_sel1, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops wm1250_ev1_ops = {
	.hw_params = wm1250_ev1_hw_params,
};

#define WM1250_EV1_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			  SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_64000)

static struct snd_soc_dai_driver wm1250_ev1_dai = {
	.name = "wm1250-ev1",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM1250_EV1_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM1250_EV1_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &wm1250_ev1_ops,
};

static const struct snd_soc_component_driver soc_component_dev_wm1250_ev1 = {
	.dapm_widgets		= wm1250_ev1_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm1250_ev1_dapm_widgets),
	.dapm_routes		= wm1250_ev1_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm1250_ev1_dapm_routes),
	.set_bias_level		= wm1250_ev1_set_bias_level,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int wm1250_ev1_pdata(struct i2c_client *i2c)
{
	struct wm1250_ev1_pdata *pdata = dev_get_platdata(&i2c->dev);
	struct wm1250_priv *wm1250;

	if (!pdata)
		return 0;

	wm1250 = devm_kzalloc(&i2c->dev, sizeof(*wm1250), GFP_KERNEL);
	if (!wm1250)
		return -ENOMEM;

	wm1250->clk_ena = devm_gpiod_get(&i2c->dev, "clk-ena", GPIOD_OUT_LOW);
	if (IS_ERR(wm1250->clk_ena))
		return dev_err_probe(&i2c->dev, PTR_ERR(wm1250->clk_ena),
				     "failed to get clock enable GPIO\n");

	wm1250->clk_sel0 = devm_gpiod_get(&i2c->dev, "clk-sel0", GPIOD_OUT_HIGH);
	if (IS_ERR(wm1250->clk_sel0))
		return dev_err_probe(&i2c->dev, PTR_ERR(wm1250->clk_sel0),
				     "failed to get clock sel0 GPIO\n");

	wm1250->clk_sel1 = devm_gpiod_get(&i2c->dev, "clk-sel1", GPIOD_OUT_HIGH);
	if (IS_ERR(wm1250->clk_sel1))
		return dev_err_probe(&i2c->dev, PTR_ERR(wm1250->clk_sel1),
				     "failed to get clock sel1 GPIO\n");

	wm1250->osr = devm_gpiod_get(&i2c->dev, "osr", GPIOD_OUT_LOW);
	if (IS_ERR(wm1250->osr))
		return dev_err_probe(&i2c->dev, PTR_ERR(wm1250->osr),
				     "failed to get OSR GPIO\n");

	wm1250->master = devm_gpiod_get(&i2c->dev, "master", GPIOD_OUT_LOW);
	if (IS_ERR(wm1250->master))
		return dev_err_probe(&i2c->dev, PTR_ERR(wm1250->master),
				     "failed to get MASTER GPIO\n");

	dev_set_drvdata(&i2c->dev, wm1250);

	return 0;
}

static int wm1250_ev1_probe(struct i2c_client *i2c)
{
	int id, board, rev, ret;

	dev_set_drvdata(&i2c->dev, NULL);

	board = i2c_smbus_read_byte_data(i2c, 0);
	if (board < 0) {
		dev_err(&i2c->dev, "Failed to read ID: %d\n", board);
		return board;
	}

	id = (board & 0xfe) >> 2;
	rev = board & 0x3;

	if (id != 1) {
		dev_err(&i2c->dev, "Unknown board ID %d\n", id);
		return -ENODEV;
	}

	dev_info(&i2c->dev, "revision %d\n", rev + 1);

	ret = wm1250_ev1_pdata(i2c);
	if (ret != 0)
		return ret;

	ret = devm_snd_soc_register_component(&i2c->dev, &soc_component_dev_wm1250_ev1,
				     &wm1250_ev1_dai, 1);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id wm1250_ev1_i2c_id[] = {
	{ "wm1250-ev1" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm1250_ev1_i2c_id);

static struct i2c_driver wm1250_ev1_i2c_driver = {
	.driver = {
		.name = "wm1250-ev1",
	},
	.probe =    wm1250_ev1_probe,
	.id_table = wm1250_ev1_i2c_id,
};

module_i2c_driver(wm1250_ev1_i2c_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM1250-EV1 audio I/O module driver");
MODULE_LICENSE("GPL");
