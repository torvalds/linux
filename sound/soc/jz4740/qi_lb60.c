/*
 * Copyright (C) 2009, Lars-Peter Clausen <lars@metafoo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/gpio.h>

#define QI_LB60_SND_GPIO JZ_GPIO_PORTB(29)
#define QI_LB60_AMP_GPIO JZ_GPIO_PORTD(4)

static int qi_lb60_spk_event(struct snd_soc_dapm_widget *widget,
			     struct snd_kcontrol *ctrl, int event)
{
	int on = 0;
	if (event & SND_SOC_DAPM_POST_PMU)
		on = 1;
	else if (event & SND_SOC_DAPM_PRE_PMD)
		on = 0;

	gpio_set_value(QI_LB60_SND_GPIO, on);
	gpio_set_value(QI_LB60_AMP_GPIO, on);

	return 0;
}

static const struct snd_soc_dapm_widget qi_lb60_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", qi_lb60_spk_event),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static const struct snd_soc_dapm_route qi_lb60_routes[] = {
	{"Mic", NULL, "MIC"},
	{"Speaker", NULL, "LOUT"},
	{"Speaker", NULL, "ROUT"},
};

#define QI_LB60_DAIFMT (SND_SOC_DAIFMT_I2S | \
			SND_SOC_DAIFMT_NB_NF | \
			SND_SOC_DAIFMT_CBM_CFM)

static int qi_lb60_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	snd_soc_dapm_nc_pin(codec, "LIN");
	snd_soc_dapm_nc_pin(codec, "RIN");

	ret = snd_soc_dai_set_fmt(cpu_dai, QI_LB60_DAIFMT);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cpu dai format: %d\n", ret);
		return ret;
	}

	snd_soc_dapm_new_controls(codec, qi_lb60_widgets, ARRAY_SIZE(qi_lb60_widgets));
	snd_soc_dapm_add_routes(codec, qi_lb60_routes, ARRAY_SIZE(qi_lb60_routes));
	snd_soc_dapm_sync(codec);

	return 0;
}

static struct snd_soc_dai_link qi_lb60_dai = {
	.name = "jz4740",
	.stream_name = "jz4740",
	.cpu_dai_name = "jz4740-i2s",
	.platform_name = "jz4740-pcm-audio",
	.codec_dai_name = "jz4740-hifi",
	.codec_name = "jz4740-codec",
	.init = qi_lb60_codec_init,
};

static struct snd_soc_card qi_lb60 = {
	.name = "QI LB60",
	.dai_link = &qi_lb60_dai,
	.num_links = 1,
};

static struct platform_device *qi_lb60_snd_device;

static int __init qi_lb60_init(void)
{
	int ret;

	qi_lb60_snd_device = platform_device_alloc("soc-audio", -1);

	if (!qi_lb60_snd_device)
		return -ENOMEM;

	ret = gpio_request(QI_LB60_SND_GPIO, "SND");
	if (ret) {
		pr_err("qi_lb60 snd: Failed to request SND GPIO(%d): %d\n",
				QI_LB60_SND_GPIO, ret);
		goto err_device_put;
	}

	ret = gpio_request(QI_LB60_AMP_GPIO, "AMP");
	if (ret) {
		pr_err("qi_lb60 snd: Failed to request AMP GPIO(%d): %d\n",
				QI_LB60_AMP_GPIO, ret);
		goto err_gpio_free_snd;
	}

	gpio_direction_output(QI_LB60_SND_GPIO, 0);
	gpio_direction_output(QI_LB60_AMP_GPIO, 0);

	platform_set_drvdata(qi_lb60_snd_device, &qi_lb60);

	ret = platform_device_add(qi_lb60_snd_device);
	if (ret) {
		pr_err("qi_lb60 snd: Failed to add snd soc device: %d\n", ret);
		goto err_unset_pdata;
	}

	 return 0;

err_unset_pdata:
	platform_set_drvdata(qi_lb60_snd_device, NULL);
/*err_gpio_free_amp:*/
	gpio_free(QI_LB60_AMP_GPIO);
err_gpio_free_snd:
	gpio_free(QI_LB60_SND_GPIO);
err_device_put:
	platform_device_put(qi_lb60_snd_device);

	return ret;
}
module_init(qi_lb60_init);

static void __exit qi_lb60_exit(void)
{
	gpio_free(QI_LB60_AMP_GPIO);
	gpio_free(QI_LB60_SND_GPIO);
	platform_device_unregister(qi_lb60_snd_device);
}
module_exit(qi_lb60_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ALSA SoC QI LB60 Audio support");
MODULE_LICENSE("GPL v2");
