/* sound/soc/at32/playpaq_wm8510.c
 * ASoC machine driver for PlayPaq using WM8510 codec
 *
 * Copyright (C) 2008 Long Range Systems
 *    Geoffrey Wossum <gwossum@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code is largely inspired by sound/soc/at91/eti_b1_wm8731.c
 *
 * NOTE: If you don't have the AT32 enhanced portmux configured (which
 * isn't currently in the mainline or Atmel patched kernel), you will
 * need to set the MCLK pin (PA30) to peripheral A in your board initialization
 * code.  Something like:
 *	at32_select_periph(GPIO_PIN_PA(30), GPIO_PERIPH_A, 0);
 *
 */

/* #define DEBUG */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <mach/at32ap700x.h>
#include <mach/portmux.h>

#include "../codecs/wm8510.h"
#include "at32-pcm.h"
#include "at32-ssc.h"


/*-------------------------------------------------------------------------*\
 * constants
\*-------------------------------------------------------------------------*/
#define MCLK_PIN		GPIO_PIN_PA(30)
#define MCLK_PERIPH		GPIO_PERIPH_A


/*-------------------------------------------------------------------------*\
 * data types
\*-------------------------------------------------------------------------*/
/* SSC clocking data */
struct ssc_clock_data {
	/* CMR div */
	unsigned int cmr_div;

	/* Frame period (as needed by xCMR.PERIOD) */
	unsigned int period;

	/* The SSC clock rate these settings where calculated for */
	unsigned long ssc_rate;
};


/*-------------------------------------------------------------------------*\
 * module data
\*-------------------------------------------------------------------------*/
static struct clk *_gclk0;
static struct clk *_pll0;

#define CODEC_CLK (_gclk0)


/*-------------------------------------------------------------------------*\
 * Sound SOC operations
\*-------------------------------------------------------------------------*/
#if defined CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE
static struct ssc_clock_data playpaq_wm8510_calc_ssc_clock(
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *cpu_dai)
{
	struct at32_ssc_info *ssc_p = cpu_dai->private_data;
	struct ssc_device *ssc = ssc_p->ssc;
	struct ssc_clock_data cd;
	unsigned int rate, width_bits, channels;
	unsigned int bitrate, ssc_div;
	unsigned actual_rate;


	/*
	 * Figure out required bitrate
	 */
	rate = params_rate(params);
	channels = params_channels(params);
	width_bits = snd_pcm_format_physical_width(params_format(params));
	bitrate = rate * width_bits * channels;


	/*
	 * Figure out required SSC divider and period for required bitrate
	 */
	cd.ssc_rate = clk_get_rate(ssc->clk);
	ssc_div = cd.ssc_rate / bitrate;
	cd.cmr_div = ssc_div / 2;
	if (ssc_div & 1) {
		/* round cmr_div up */
		cd.cmr_div++;
	}
	cd.period = width_bits - 1;


	/*
	 * Find actual rate, compare to requested rate
	 */
	actual_rate = (cd.ssc_rate / (cd.cmr_div * 2)) / (2 * (cd.period + 1));
	pr_debug("playpaq_wm8510: Request rate = %d, actual rate = %d\n",
		 rate, actual_rate);


	return cd;
}
#endif /* CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE */



static int playpaq_wm8510_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct at32_ssc_info *ssc_p = cpu_dai->private_data;
	struct ssc_device *ssc = ssc_p->ssc;
	unsigned int pll_out = 0, bclk = 0, mclk_div = 0;
	int ret;


	/* Due to difficulties with getting the correct clocks from the AT32's
	 * PLL0, we're going to let the CODEC be in charge of all the clocks
	 */
#if !defined CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE
	const unsigned int fmt = (SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
#else
	struct ssc_clock_data cd;
	const unsigned int fmt = (SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBS_CFS);
#endif

	if (ssc == NULL) {
		pr_warning("playpaq_wm8510_hw_params: ssc is NULL!\n");
		return -EINVAL;
	}


	/*
	 * Figure out PLL and BCLK dividers for WM8510
	 */
	switch (params_rate(params)) {
	case 48000:
		pll_out = 12288000;
		mclk_div = WM8510_MCLKDIV_1;
		bclk = WM8510_BCLKDIV_8;
		break;

	case 44100:
		pll_out = 11289600;
		mclk_div = WM8510_MCLKDIV_1;
		bclk = WM8510_BCLKDIV_8;
		break;

	case 22050:
		pll_out = 11289600;
		mclk_div = WM8510_MCLKDIV_2;
		bclk = WM8510_BCLKDIV_8;
		break;

	case 16000:
		pll_out = 12288000;
		mclk_div = WM8510_MCLKDIV_3;
		bclk = WM8510_BCLKDIV_8;
		break;

	case 11025:
		pll_out = 11289600;
		mclk_div = WM8510_MCLKDIV_4;
		bclk = WM8510_BCLKDIV_8;
		break;

	case 8000:
		pll_out = 12288000;
		mclk_div = WM8510_MCLKDIV_6;
		bclk = WM8510_BCLKDIV_8;
		break;

	default:
		pr_warning("playpaq_wm8510: Unsupported sample rate %d\n",
			   params_rate(params));
		return -EINVAL;
	}


	/*
	 * set CPU and CODEC DAI configuration
	 */
	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		pr_warning("playpaq_wm8510: "
			   "Failed to set CODEC DAI format (%d)\n",
			   ret);
		return ret;
	}
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		pr_warning("playpaq_wm8510: "
			   "Failed to set CPU DAI format (%d)\n",
			   ret);
		return ret;
	}


	/*
	 * Set CPU clock configuration
	 */
#if defined CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE
	cd = playpaq_wm8510_calc_ssc_clock(params, cpu_dai);
	pr_debug("playpaq_wm8510: cmr_div = %d, period = %d\n",
		 cd.cmr_div, cd.period);
	ret = snd_soc_dai_set_clkdiv(cpu_dai, AT32_SSC_CMR_DIV, cd.cmr_div);
	if (ret < 0) {
		pr_warning("playpaq_wm8510: Failed to set CPU CMR_DIV (%d)\n",
			   ret);
		return ret;
	}
	ret = snd_soc_dai_set_clkdiv(cpu_dai, AT32_SSC_TCMR_PERIOD,
					  cd.period);
	if (ret < 0) {
		pr_warning("playpaq_wm8510: "
			   "Failed to set CPU transmit period (%d)\n",
			   ret);
		return ret;
	}
#endif /* CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE */


	/*
	 * Set CODEC clock configuration
	 */
	pr_debug("playpaq_wm8510: "
		 "pll_in = %ld, pll_out = %u, bclk = %x, mclk = %x\n",
		 clk_get_rate(CODEC_CLK), pll_out, bclk, mclk_div);


#if !defined CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE
	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8510_BCLKDIV, bclk);
	if (ret < 0) {
		pr_warning
		    ("playpaq_wm8510: Failed to set CODEC DAI BCLKDIV (%d)\n",
		     ret);
		return ret;
	}
#endif /* CONFIG_SND_AT32_SOC_PLAYPAQ_SLAVE */


	ret = snd_soc_dai_set_pll(codec_dai, 0,
					 clk_get_rate(CODEC_CLK), pll_out);
	if (ret < 0) {
		pr_warning("playpaq_wm8510: Failed to set CODEC DAI PLL (%d)\n",
			   ret);
		return ret;
	}


	ret = snd_soc_dai_set_clkdiv(codec_dai, WM8510_MCLKDIV, mclk_div);
	if (ret < 0) {
		pr_warning("playpaq_wm8510: Failed to set CODEC MCLKDIV (%d)\n",
			   ret);
		return ret;
	}


	return 0;
}



static struct snd_soc_ops playpaq_wm8510_ops = {
	.hw_params = playpaq_wm8510_hw_params,
};



static const struct snd_soc_dapm_widget playpaq_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};



static const char *intercon[][3] = {
	/* speaker connected to SPKOUT */
	{"Ext Spk", NULL, "SPKOUTP"},
	{"Ext Spk", NULL, "SPKOUTN"},

	{"Mic Bias", NULL, "Int Mic"},
	{"MICN", NULL, "Mic Bias"},
	{"MICP", NULL, "Mic Bias"},

	/* Terminator */
	{NULL, NULL, NULL},
};



static int playpaq_wm8510_init(struct snd_soc_codec *codec)
{
	int i;

	/*
	 * Add DAPM widgets
	 */
	for (i = 0; i < ARRAY_SIZE(playpaq_dapm_widgets); i++)
		snd_soc_dapm_new_control(codec, &playpaq_dapm_widgets[i]);



	/*
	 * Setup audio path interconnects
	 */
	for (i = 0; intercon[i][0] != NULL; i++) {
		snd_soc_dapm_connect_input(codec,
					   intercon[i][0],
					   intercon[i][1], intercon[i][2]);
	}


	/* always connected pins */
	snd_soc_dapm_enable_pin(codec, "Int Mic");
	snd_soc_dapm_enable_pin(codec, "Ext Spk");
	snd_soc_dapm_sync(codec);



	/* Make CSB show PLL rate */
	snd_soc_dai_set_clkdiv(codec->dai, WM8510_OPCLKDIV,
				       WM8510_OPCLKDIV_1 | 4);

	return 0;
}



static struct snd_soc_dai_link playpaq_wm8510_dai = {
	.name = "WM8510",
	.stream_name = "WM8510 PCM",
	.cpu_dai = &at32_ssc_dai[0],
	.codec_dai = &wm8510_dai,
	.init = playpaq_wm8510_init,
	.ops = &playpaq_wm8510_ops,
};



static struct snd_soc_machine snd_soc_machine_playpaq = {
	.name = "LRS_PlayPaq_WM8510",
	.dai_link = &playpaq_wm8510_dai,
	.num_links = 1,
};



static struct wm8510_setup_data playpaq_wm8510_setup = {
	.i2c_bus = 0,
	.i2c_address = 0x1a,
};



static struct snd_soc_device playpaq_wm8510_snd_devdata = {
	.machine = &snd_soc_machine_playpaq,
	.platform = &at32_soc_platform,
	.codec_dev = &soc_codec_dev_wm8510,
	.codec_data = &playpaq_wm8510_setup,
};

static struct platform_device *playpaq_snd_device;


static int __init playpaq_asoc_init(void)
{
	int ret = 0;
	struct at32_ssc_info *ssc_p = playpaq_wm8510_dai.cpu_dai->private_data;
	struct ssc_device *ssc = NULL;


	/*
	 * Request SSC device
	 */
	ssc = ssc_request(0);
	if (IS_ERR(ssc)) {
		ret = PTR_ERR(ssc);
		goto err_ssc;
	}
	ssc_p->ssc = ssc;


	/*
	 * Configure MCLK for WM8510
	 */
	_gclk0 = clk_get(NULL, "gclk0");
	if (IS_ERR(_gclk0)) {
		_gclk0 = NULL;
		goto err_gclk0;
	}
	_pll0 = clk_get(NULL, "pll0");
	if (IS_ERR(_pll0)) {
		_pll0 = NULL;
		goto err_pll0;
	}
	if (clk_set_parent(_gclk0, _pll0)) {
		pr_warning("snd-soc-playpaq: "
			   "Failed to set PLL0 as parent for DAC clock\n");
		goto err_set_clk;
	}
	clk_set_rate(CODEC_CLK, 12000000);
	clk_enable(CODEC_CLK);

#if defined CONFIG_AT32_ENHANCED_PORTMUX
	at32_select_periph(MCLK_PIN, MCLK_PERIPH, 0);
#endif


	/*
	 * Create and register platform device
	 */
	playpaq_snd_device = platform_device_alloc("soc-audio", 0);
	if (playpaq_snd_device == NULL) {
		ret = -ENOMEM;
		goto err_device_alloc;
	}

	platform_set_drvdata(playpaq_snd_device, &playpaq_wm8510_snd_devdata);
	playpaq_wm8510_snd_devdata.dev = &playpaq_snd_device->dev;

	ret = platform_device_add(playpaq_snd_device);
	if (ret) {
		pr_warning("playpaq_wm8510: platform_device_add failed (%d)\n",
			   ret);
		goto err_device_add;
	}

	return 0;


err_device_add:
	if (playpaq_snd_device != NULL) {
		platform_device_put(playpaq_snd_device);
		playpaq_snd_device = NULL;
	}
err_device_alloc:
err_set_clk:
	if (_pll0 != NULL) {
		clk_put(_pll0);
		_pll0 = NULL;
	}
err_pll0:
	if (_gclk0 != NULL) {
		clk_put(_gclk0);
		_gclk0 = NULL;
	}
err_gclk0:
	ssc_free(ssc);
err_ssc:
	return ret;
}


static void __exit playpaq_asoc_exit(void)
{
	struct at32_ssc_info *ssc_p = playpaq_wm8510_dai.cpu_dai->private_data;
	struct ssc_device *ssc;

	if (ssc_p != NULL) {
		ssc = ssc_p->ssc;
		if (ssc != NULL)
			ssc_free(ssc);
		ssc_p->ssc = NULL;
	}

	if (_gclk0 != NULL) {
		clk_put(_gclk0);
		_gclk0 = NULL;
	}
	if (_pll0 != NULL) {
		clk_put(_pll0);
		_pll0 = NULL;
	}

#if defined CONFIG_AT32_ENHANCED_PORTMUX
	at32_free_pin(MCLK_PIN);
#endif

	platform_device_unregister(playpaq_snd_device);
	playpaq_snd_device = NULL;
}

module_init(playpaq_asoc_init);
module_exit(playpaq_asoc_exit);

MODULE_AUTHOR("Geoffrey Wossum <gwossum@acm.org>");
MODULE_DESCRIPTION("ASoC machine driver for LRS PlayPaq");
MODULE_LICENSE("GPL");
