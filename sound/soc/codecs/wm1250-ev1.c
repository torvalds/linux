/*
 * Driver for the 1250-EV1 audio I/O module
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/wm1250-ev1.h>

static const char *wm1250_gpio_names[WM1250_EV1_NUM_GPIOS] = {
	"WM1250 CLK_ENA",
	"WM1250 CLK_SEL0",
	"WM1250 CLK_SEL1",
	"WM1250 OSR",
	"WM1250 MASTER",
};

struct wm1250_priv {
	struct gpio gpios[WM1250_EV1_NUM_GPIOS];
};

static int wm1250_ev1_set_bias_level(struct snd_soc_codec *codec,
				     enum snd_soc_bias_level level)
{
	struct wm1250_priv *wm1250 = dev_get_drvdata(codec->dev);
	int ena;

	if (wm1250)
		ena = wm1250->gpios[WM1250_EV1_GPIO_CLK_ENA].gpio;
	else
		ena = -1;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (ena >= 0)
			gpio_set_value_cansleep(ena, 1);
		break;

	case SND_SOC_BIAS_OFF:
		if (ena >= 0)
			gpio_set_value_cansleep(ena, 0);
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
	struct wm1250_priv *wm1250 = snd_soc_codec_get_drvdata(dai->codec);

	switch (params_rate(params)) {
	case 8000:
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL0].gpio,
			       1);
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL1].gpio,
			       1);
		break;
	case 16000:
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL0].gpio,
			       0);
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL1].gpio,
			       1);
		break;
	case 32000:
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL0].gpio,
			       1);
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL1].gpio,
			       0);
		break;
	case 64000:
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL0].gpio,
			       0);
		gpio_set_value(wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL1].gpio,
			       0);
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

static const struct snd_soc_codec_driver soc_codec_dev_wm1250_ev1 = {
	.component_driver = {
		.dapm_widgets		= wm1250_ev1_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(wm1250_ev1_dapm_widgets),
		.dapm_routes		= wm1250_ev1_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(wm1250_ev1_dapm_routes),
	},
	.set_bias_level = wm1250_ev1_set_bias_level,
	.idle_bias_off = true,
};

static int wm1250_ev1_pdata(struct i2c_client *i2c)
{
	struct wm1250_ev1_pdata *pdata = dev_get_platdata(&i2c->dev);
	struct wm1250_priv *wm1250;
	int i, ret;

	if (!pdata)
		return 0;

	wm1250 = devm_kzalloc(&i2c->dev, sizeof(*wm1250), GFP_KERNEL);
	if (!wm1250) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(wm1250->gpios); i++) {
		wm1250->gpios[i].gpio = pdata->gpios[i];
		wm1250->gpios[i].label = wm1250_gpio_names[i];
		wm1250->gpios[i].flags = GPIOF_OUT_INIT_LOW;
	}
	wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL0].flags = GPIOF_OUT_INIT_HIGH;
	wm1250->gpios[WM1250_EV1_GPIO_CLK_SEL1].flags = GPIOF_OUT_INIT_HIGH;

	ret = gpio_request_array(wm1250->gpios, ARRAY_SIZE(wm1250->gpios));
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to get GPIOs: %d\n", ret);
		goto err;
	}

	dev_set_drvdata(&i2c->dev, wm1250);

	return ret;

err:
	return ret;
}

static void wm1250_ev1_free(struct i2c_client *i2c)
{
	struct wm1250_priv *wm1250 = dev_get_drvdata(&i2c->dev);

	if (wm1250)
		gpio_free_array(wm1250->gpios, ARRAY_SIZE(wm1250->gpios));
}

static int wm1250_ev1_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *i2c_id)
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

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_wm1250_ev1,
				     &wm1250_ev1_dai, 1);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);
		wm1250_ev1_free(i2c);
		return ret;
	}

	return 0;
}

static int wm1250_ev1_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	wm1250_ev1_free(i2c);

	return 0;
}

static const struct i2c_device_id wm1250_ev1_i2c_id[] = {
	{ "wm1250-ev1", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm1250_ev1_i2c_id);

static struct i2c_driver wm1250_ev1_i2c_driver = {
	.driver = {
		.name = "wm1250-ev1",
	},
	.probe =    wm1250_ev1_probe,
	.remove =   wm1250_ev1_remove,
	.id_table = wm1250_ev1_i2c_id,
};

module_i2c_driver(wm1250_ev1_i2c_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM1250-EV1 audio I/O module driver");
MODULE_LICENSE("GPL");
