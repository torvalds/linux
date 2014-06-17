/*
 * Modifications by Christian Pellegrin <chripell@evolware.org>
 *
 * s3c24xx_uda134x.c  --  S3C24XX_UDA134X ALSA SoC Audio board driver
 *
 * Copyright 2007 Dension Audio Systems Ltd.
 * Author: Zoltan Devai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/s3c24xx_uda134x.h>

#include "regs-iis.h"

#include "s3c24xx-i2s.h"

/* #define ENFORCE_RATES 1 */
/*
  Unfortunately the S3C24XX in master mode has a limited capacity of
  generating the clock for the codec. If you define this only rates
  that are really available will be enforced. But be careful, most
  user level application just want the usual sampling frequencies (8,
  11.025, 22.050, 44.1 kHz) and anyway resampling is a costly
  operation for embedded systems. So if you aren't very lucky or your
  hardware engineer wasn't very forward-looking it's better to leave
  this undefined. If you do so an approximate value for the requested
  sampling rate in the range -/+ 5% will be chosen. If this in not
  possible an error will be returned.
*/

static struct clk *xtal;
static struct clk *pclk;
/* this is need because we don't have a place where to keep the
 * pointers to the clocks in each substream. We get the clocks only
 * when we are actually using them so we don't block stuff like
 * frequency change or oscillator power-off */
static int clk_users;
static DEFINE_MUTEX(clk_lock);

static unsigned int rates[33 * 2];
#ifdef ENFORCE_RATES
static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
	.count	= ARRAY_SIZE(rates),
	.list	= rates,
	.mask	= 0,
};
#endif

static struct platform_device *s3c24xx_uda134x_snd_device;

static int s3c24xx_uda134x_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
#ifdef ENFORCE_RATES
	struct snd_pcm_runtime *runtime = substream->runtime;
#endif

	mutex_lock(&clk_lock);
	pr_debug("%s %d\n", __func__, clk_users);
	if (clk_users == 0) {
		xtal = clk_get(&s3c24xx_uda134x_snd_device->dev, "xtal");
		if (IS_ERR(xtal)) {
			printk(KERN_ERR "%s cannot get xtal\n", __func__);
			ret = PTR_ERR(xtal);
		} else {
			pclk = clk_get(&s3c24xx_uda134x_snd_device->dev,
				       "pclk");
			if (IS_ERR(pclk)) {
				printk(KERN_ERR "%s cannot get pclk\n",
				       __func__);
				clk_put(xtal);
				ret = PTR_ERR(pclk);
			}
		}
		if (!ret) {
			int i, j;

			for (i = 0; i < 2; i++) {
				int fs = i ? 256 : 384;

				rates[i*33] = clk_get_rate(xtal) / fs;
				for (j = 1; j < 33; j++)
					rates[i*33 + j] = clk_get_rate(pclk) /
						(j * fs);
			}
		}
	}
	clk_users += 1;
	mutex_unlock(&clk_lock);
	if (!ret) {
#ifdef ENFORCE_RATES
		ret = snd_pcm_hw_constraint_list(runtime, 0,
						 SNDRV_PCM_HW_PARAM_RATE,
						 &hw_constraints_rates);
		if (ret < 0)
			printk(KERN_ERR "%s cannot set constraints\n",
			       __func__);
#endif
	}
	return ret;
}

static void s3c24xx_uda134x_shutdown(struct snd_pcm_substream *substream)
{
	mutex_lock(&clk_lock);
	pr_debug("%s %d\n", __func__, clk_users);
	clk_users -= 1;
	if (clk_users == 0) {
		clk_put(xtal);
		xtal = NULL;
		clk_put(pclk);
		pclk = NULL;
	}
	mutex_unlock(&clk_lock);
}

static int s3c24xx_uda134x_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int clk = 0;
	int ret = 0;
	int clk_source, fs_mode;
	unsigned long rate = params_rate(params);
	long err, cerr;
	unsigned int div;
	int i, bi;

	err = 999999;
	bi = 0;
	for (i = 0; i < 2*33; i++) {
		cerr = rates[i] - rate;
		if (cerr < 0)
			cerr = -cerr;
		if (cerr < err) {
			err = cerr;
			bi = i;
		}
	}
	if (bi / 33 == 1)
		fs_mode = S3C2410_IISMOD_256FS;
	else
		fs_mode = S3C2410_IISMOD_384FS;
	if (bi % 33 == 0) {
		clk_source = S3C24XX_CLKSRC_MPLL;
		div = 1;
	} else {
		clk_source = S3C24XX_CLKSRC_PCLK;
		div = bi % 33;
	}
	pr_debug("%s desired rate %lu, %d\n", __func__, rate, bi);

	clk = (fs_mode == S3C2410_IISMOD_384FS ? 384 : 256) * rate;
	pr_debug("%s will use: %s %s %d sysclk %d err %ld\n", __func__,
		 fs_mode == S3C2410_IISMOD_384FS ? "384FS" : "256FS",
		 clk_source == S3C24XX_CLKSRC_MPLL ? "MPLLin" : "PCLK",
		 div, clk, err);

	if ((err * 100 / rate) > 5) {
		printk(KERN_ERR "S3C24XX_UDA134X: effective frequency "
		       "too different from desired (%ld%%)\n",
		       err * 100 / rate);
		return -EINVAL;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, clk_source , clk,
			SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_MCLK, fs_mode);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_BCLK,
			S3C2410_IISMOD_32FS);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, S3C24XX_DIV_PRESCALER,
			S3C24XX_PRESCALE(div, div));
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, clk,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops s3c24xx_uda134x_ops = {
	.startup = s3c24xx_uda134x_startup,
	.shutdown = s3c24xx_uda134x_shutdown,
	.hw_params = s3c24xx_uda134x_hw_params,
};

static struct snd_soc_dai_link s3c24xx_uda134x_dai_link = {
	.name = "UDA134X",
	.stream_name = "UDA134X",
	.codec_name = "uda134x-codec",
	.codec_dai_name = "uda134x-hifi",
	.cpu_dai_name = "s3c24xx-iis",
	.ops = &s3c24xx_uda134x_ops,
	.platform_name	= "s3c24xx-iis",
};

static struct snd_soc_card snd_soc_s3c24xx_uda134x = {
	.name = "S3C24XX_UDA134X",
	.owner = THIS_MODULE,
	.dai_link = &s3c24xx_uda134x_dai_link,
	.num_links = 1,
};

static struct s3c24xx_uda134x_platform_data *s3c24xx_uda134x_l3_pins;

static void setdat(int v)
{
	gpio_set_value(s3c24xx_uda134x_l3_pins->l3_data, v > 0);
}

static void setclk(int v)
{
	gpio_set_value(s3c24xx_uda134x_l3_pins->l3_clk, v > 0);
}

static void setmode(int v)
{
	gpio_set_value(s3c24xx_uda134x_l3_pins->l3_mode, v > 0);
}

/* FIXME - This must be codec platform data but in which board file ?? */
static struct uda134x_platform_data s3c24xx_uda134x = {
	.l3 = {
		.setdat = setdat,
		.setclk = setclk,
		.setmode = setmode,
		.data_hold = 1,
		.data_setup = 1,
		.clock_high = 1,
		.mode_hold = 1,
		.mode = 1,
		.mode_setup = 1,
	},
};

static int s3c24xx_uda134x_setup_pin(int pin, char *fun)
{
	if (gpio_request(pin, "s3c24xx_uda134x") < 0) {
		printk(KERN_ERR "S3C24XX_UDA134X SoC Audio: "
		       "l3 %s pin already in use", fun);
		return -EBUSY;
	}
	gpio_direction_output(pin, 0);
	return 0;
}

static int s3c24xx_uda134x_probe(struct platform_device *pdev)
{
	int ret;

	printk(KERN_INFO "S3C24XX_UDA134X SoC Audio driver\n");

	s3c24xx_uda134x_l3_pins = pdev->dev.platform_data;
	if (s3c24xx_uda134x_l3_pins == NULL) {
		printk(KERN_ERR "S3C24XX_UDA134X SoC Audio: "
		       "unable to find platform data\n");
		return -ENODEV;
	}
	s3c24xx_uda134x.power = s3c24xx_uda134x_l3_pins->power;
	s3c24xx_uda134x.model = s3c24xx_uda134x_l3_pins->model;

	if (s3c24xx_uda134x_setup_pin(s3c24xx_uda134x_l3_pins->l3_data,
				      "data") < 0)
		return -EBUSY;
	if (s3c24xx_uda134x_setup_pin(s3c24xx_uda134x_l3_pins->l3_clk,
				      "clk") < 0) {
		gpio_free(s3c24xx_uda134x_l3_pins->l3_data);
		return -EBUSY;
	}
	if (s3c24xx_uda134x_setup_pin(s3c24xx_uda134x_l3_pins->l3_mode,
				      "mode") < 0) {
		gpio_free(s3c24xx_uda134x_l3_pins->l3_data);
		gpio_free(s3c24xx_uda134x_l3_pins->l3_clk);
		return -EBUSY;
	}

	s3c24xx_uda134x_snd_device = platform_device_alloc("soc-audio", -1);
	if (!s3c24xx_uda134x_snd_device) {
		printk(KERN_ERR "S3C24XX_UDA134X SoC Audio: "
		       "Unable to register\n");
		return -ENOMEM;
	}

	platform_set_drvdata(s3c24xx_uda134x_snd_device,
			     &snd_soc_s3c24xx_uda134x);
	platform_device_add_data(s3c24xx_uda134x_snd_device, &s3c24xx_uda134x, sizeof(s3c24xx_uda134x));
	ret = platform_device_add(s3c24xx_uda134x_snd_device);
	if (ret) {
		printk(KERN_ERR "S3C24XX_UDA134X SoC Audio: Unable to add\n");
		platform_device_put(s3c24xx_uda134x_snd_device);
	}

	return ret;
}

static int s3c24xx_uda134x_remove(struct platform_device *pdev)
{
	platform_device_unregister(s3c24xx_uda134x_snd_device);
	gpio_free(s3c24xx_uda134x_l3_pins->l3_data);
	gpio_free(s3c24xx_uda134x_l3_pins->l3_clk);
	gpio_free(s3c24xx_uda134x_l3_pins->l3_mode);
	return 0;
}

static struct platform_driver s3c24xx_uda134x_driver = {
	.probe  = s3c24xx_uda134x_probe,
	.remove = s3c24xx_uda134x_remove,
	.driver = {
		.name = "s3c24xx_uda134x",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(s3c24xx_uda134x_driver);

MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_DESCRIPTION("S3C24XX_UDA134X ALSA SoC audio driver");
MODULE_LICENSE("GPL");
