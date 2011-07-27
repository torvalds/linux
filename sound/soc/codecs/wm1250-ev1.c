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
#include <linux/i2c.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>

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

static struct snd_soc_dai_driver wm1250_ev1_dai = {
	.name = "wm1250-ev1",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_wm1250_ev1 = {
	.dapm_widgets = wm1250_ev1_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm1250_ev1_dapm_widgets),
	.dapm_routes = wm1250_ev1_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm1250_ev1_dapm_routes),
};

static int __devinit wm1250_ev1_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_wm1250_ev1,
				      &wm1250_ev1_dai, 1);
}

static int __devexit wm1250_ev1_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);

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
		.owner = THIS_MODULE,
	},
	.probe =    wm1250_ev1_probe,
	.remove =   __devexit_p(wm1250_ev1_remove),
	.id_table = wm1250_ev1_i2c_id,
};

static int __init wm1250_ev1_modinit(void)
{
	int ret = 0;

	ret = i2c_add_driver(&wm1250_ev1_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register WM1250-EV1 I2C driver: %d\n", ret);

	return ret;
}
module_init(wm1250_ev1_modinit);

static void __exit wm1250_ev1_exit(void)
{
	i2c_del_driver(&wm1250_ev1_i2c_driver);
}
module_exit(wm1250_ev1_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM1250-EV1 audio I/O module driver");
MODULE_LICENSE("GPL");
