// SPDX-License-Identifier: GPL-2.0
//
// Modifications by Christian Pellegrin <chripell@evolware.org>
//
// s3c24xx_uda134x.c - S3C24XX_UDA134X ALSA SoC Audio board driver
//
// Copyright 2007 Dension Audio Systems Ltd.
// Author: Zoltan Devai

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/s3c24xx_uda134x.h>

#include "regs-iis.h"
#include "s3c24xx-i2s.h"

struct s3c24xx_uda134x {
	struct clk *xtal;
	struct clk *pclk;
	struct mutex clk_lock;
	int clk_users;
};

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

static unsigned int rates[33 * 2];
#ifdef ENFORCE_RATES
static const struct snd_pcm_hw_constraint_list hw_constraints_rates = {
	.count	= ARRAY_SIZE(rates),
	.list	= rates,
	.mask	= 0,
};
#endif

static int s3c24xx_uda134x_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s3c24xx_uda134x *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	mutex_lock(&priv->clk_lock);

	if (priv->clk_users == 0) {
		priv->xtal = clk_get(rtd->dev, "xtal");
		if (IS_ERR(priv->xtal)) {
			dev_err(rtd->dev, "%s cannot get xtal\n", __func__);
			ret = PTR_ERR(priv->xtal);
		} else {
			priv->pclk = clk_get(cpu_dai->dev, "iis");
			if (IS_ERR(priv->pclk)) {
				dev_err(rtd->dev, "%s cannot get pclk\n",
					__func__);
				clk_put(priv->xtal);
				ret = PTR_ERR(priv->pclk);
			}
		}
		if (!ret) {
			int i, j;

			for (i = 0; i < 2; i++) {
				int fs = i ? 256 : 384;

				rates[i*33] = clk_get_rate(priv->xtal) / fs;
				for (j = 1; j < 33; j++)
					rates[i*33 + j] = clk_get_rate(priv->pclk) /
						(j * fs);
			}
		}
	}
	priv->clk_users += 1;
	mutex_unlock(&priv->clk_lock);

	if (!ret) {
#ifdef ENFORCE_RATES
		ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
						 SNDRV_PCM_HW_PARAM_RATE,
						 &hw_constraints_rates);
		if (ret < 0)
			dev_err(rtd->dev, "%s cannot set constraints\n",
				__func__);
#endif
	}
	return ret;
}

static void s3c24xx_uda134x_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s3c24xx_uda134x *priv = snd_soc_card_get_drvdata(rtd->card);

	mutex_lock(&priv->clk_lock);
	priv->clk_users -= 1;
	if (priv->clk_users == 0) {
		clk_put(priv->xtal);
		priv->xtal = NULL;
		clk_put(priv->pclk);
		priv->pclk = NULL;
	}
	mutex_unlock(&priv->clk_lock);
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

	dev_dbg(rtd->dev, "%s desired rate %lu, %d\n", __func__, rate, bi);

	clk = (fs_mode == S3C2410_IISMOD_384FS ? 384 : 256) * rate;

	dev_dbg(rtd->dev, "%s will use: %s %s %d sysclk %d err %ld\n", __func__,
		fs_mode == S3C2410_IISMOD_384FS ? "384FS" : "256FS",
		clk_source == S3C24XX_CLKSRC_MPLL ? "MPLLin" : "PCLK",
		div, clk, err);

	if ((err * 100 / rate) > 5) {
		dev_err(rtd->dev, "effective frequency too different "
				  "from desired (%ld%%)\n", err * 100 / rate);
		return -EINVAL;
	}

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

static const struct snd_soc_ops s3c24xx_uda134x_ops = {
	.startup = s3c24xx_uda134x_startup,
	.shutdown = s3c24xx_uda134x_shutdown,
	.hw_params = s3c24xx_uda134x_hw_params,
};

SND_SOC_DAILINK_DEFS(uda134x,
	DAILINK_COMP_ARRAY(COMP_CPU("s3c24xx-iis")),
	DAILINK_COMP_ARRAY(COMP_CODEC("uda134x-codec", "uda134x-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("s3c24xx-iis")));

static struct snd_soc_dai_link s3c24xx_uda134x_dai_link = {
	.name = "UDA134X",
	.stream_name = "UDA134X",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &s3c24xx_uda134x_ops,
	SND_SOC_DAILINK_REG(uda134x),
};

static struct snd_soc_card snd_soc_s3c24xx_uda134x = {
	.name = "S3C24XX_UDA134X",
	.owner = THIS_MODULE,
	.dai_link = &s3c24xx_uda134x_dai_link,
	.num_links = 1,
};

static int s3c24xx_uda134x_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_s3c24xx_uda134x;
	struct s3c24xx_uda134x *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->clk_lock);

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, priv);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "failed to register card: %d\n", ret);

	return ret;
}

static struct platform_driver s3c24xx_uda134x_driver = {
	.probe  = s3c24xx_uda134x_probe,
	.driver = {
		.name = "s3c24xx_uda134x",
	},
};
module_platform_driver(s3c24xx_uda134x_driver);

MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_DESCRIPTION("S3C24XX_UDA134X ALSA SoC audio driver");
MODULE_LICENSE("GPL");
