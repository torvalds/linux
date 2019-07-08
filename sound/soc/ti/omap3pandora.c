// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap3pandora.c  --  SoC audio for Pandora Handheld Console
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include "omap-mcbsp.h"

#define OMAP3_PANDORA_DAC_POWER_GPIO	118
#define OMAP3_PANDORA_AMP_POWER_GPIO	14

#define PREFIX "ASoC omap3pandora: "

static struct regulator *omap3pandora_dac_reg;

static int omap3pandora_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	/* Set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 26000000,
					    SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err(PREFIX "can't set codec system clock\n");
		return ret;
	}

	/* Set McBSP clock to external */
	ret = snd_soc_dai_set_sysclk(cpu_dai, OMAP_MCBSP_SYSCLK_CLKS_EXT,
				     256 * params_rate(params),
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err(PREFIX "can't set cpu system clock\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai, OMAP_MCBSP_CLKGDV, 8);
	if (ret < 0) {
		pr_err(PREFIX "can't set SRG clock divider\n");
		return ret;
	}

	return 0;
}

static int omap3pandora_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	int ret;

	/*
	 * The PCM1773 DAC datasheet requires 1ms delay between switching
	 * VCC power on/off and /PD pin high/low
	 */
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		ret = regulator_enable(omap3pandora_dac_reg);
		if (ret) {
			dev_err(w->dapm->dev, "Failed to power DAC: %d\n", ret);
			return ret;
		}
		mdelay(1);
		gpio_set_value(OMAP3_PANDORA_DAC_POWER_GPIO, 1);
	} else {
		gpio_set_value(OMAP3_PANDORA_DAC_POWER_GPIO, 0);
		mdelay(1);
		regulator_disable(omap3pandora_dac_reg);
	}

	return 0;
}

static int omap3pandora_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(OMAP3_PANDORA_AMP_POWER_GPIO, 1);
	else
		gpio_set_value(OMAP3_PANDORA_AMP_POWER_GPIO, 0);

	return 0;
}

/*
 * Audio paths on Pandora board:
 *
 *  |O| ---> PCM DAC +-> AMP -> Headphone Jack
 *  |M|         A    +--------> Line Out
 *  |A| <~~clk~~+
 *  |P| <--- TWL4030 <--------- Line In and MICs
 */
static const struct snd_soc_dapm_widget omap3pandora_dapm_widgets[] = {
	SND_SOC_DAPM_DAC_E("PCM DAC", "HiFi Playback", SND_SOC_NOPM,
			   0, 0, omap3pandora_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("Headphone Amplifier", SND_SOC_NOPM,
			   0, 0, NULL, 0, omap3pandora_hp_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),

	SND_SOC_DAPM_MIC("Mic (internal)", NULL),
	SND_SOC_DAPM_MIC("Mic (external)", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct snd_soc_dapm_route omap3pandora_map[] = {
	{"PCM DAC", NULL, "APLL Enable"},
	{"Headphone Amplifier", NULL, "PCM DAC"},
	{"Line Out", NULL, "PCM DAC"},
	{"Headphone Jack", NULL, "Headphone Amplifier"},

	{"AUXL", NULL, "Line In"},
	{"AUXR", NULL, "Line In"},

	{"MAINMIC", NULL, "Mic (internal)"},
	{"Mic (internal)", NULL, "Mic Bias 1"},

	{"SUBMIC", NULL, "Mic (external)"},
	{"Mic (external)", NULL, "Mic Bias 2"},
};

static int omap3pandora_out_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm = &rtd->card->dapm;

	/* All TWL4030 output pins are floating */
	snd_soc_dapm_nc_pin(dapm, "EARPIECE");
	snd_soc_dapm_nc_pin(dapm, "PREDRIVEL");
	snd_soc_dapm_nc_pin(dapm, "PREDRIVER");
	snd_soc_dapm_nc_pin(dapm, "HSOL");
	snd_soc_dapm_nc_pin(dapm, "HSOR");
	snd_soc_dapm_nc_pin(dapm, "CARKITL");
	snd_soc_dapm_nc_pin(dapm, "CARKITR");
	snd_soc_dapm_nc_pin(dapm, "HFL");
	snd_soc_dapm_nc_pin(dapm, "HFR");
	snd_soc_dapm_nc_pin(dapm, "VIBRA");

	return 0;
}

static int omap3pandora_in_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm = &rtd->card->dapm;

	/* Not comnnected */
	snd_soc_dapm_nc_pin(dapm, "HSMIC");
	snd_soc_dapm_nc_pin(dapm, "CARKITMIC");
	snd_soc_dapm_nc_pin(dapm, "DIGIMIC0");
	snd_soc_dapm_nc_pin(dapm, "DIGIMIC1");

	return 0;
}

static const struct snd_soc_ops omap3pandora_ops = {
	.hw_params = omap3pandora_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(out,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("twl4030-codec", "twl4030-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("omap-mcbsp.2")));

SND_SOC_DAILINK_DEFS(in,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.4")),
	DAILINK_COMP_ARRAY(COMP_CODEC("twl4030-codec", "twl4030-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("omap-mcbsp.4")));

static struct snd_soc_dai_link omap3pandora_dai[] = {
	{
		.name = "PCM1773",
		.stream_name = "HiFi Out",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.ops = &omap3pandora_ops,
		.init = omap3pandora_out_init,
		SND_SOC_DAILINK_REG(out),
	}, {
		.name = "TWL4030",
		.stream_name = "Line/Mic In",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.ops = &omap3pandora_ops,
		.init = omap3pandora_in_init,
		SND_SOC_DAILINK_REG(in),
	}
};

/* SoC card */
static struct snd_soc_card snd_soc_card_omap3pandora = {
	.name = "omap3pandora",
	.owner = THIS_MODULE,
	.dai_link = omap3pandora_dai,
	.num_links = ARRAY_SIZE(omap3pandora_dai),

	.dapm_widgets = omap3pandora_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(omap3pandora_dapm_widgets),
	.dapm_routes = omap3pandora_map,
	.num_dapm_routes = ARRAY_SIZE(omap3pandora_map),
};

static struct platform_device *omap3pandora_snd_device;

static int __init omap3pandora_soc_init(void)
{
	int ret;

	if (!machine_is_omap3_pandora())
		return -ENODEV;

	pr_info("OMAP3 Pandora SoC init\n");

	ret = gpio_request(OMAP3_PANDORA_DAC_POWER_GPIO, "dac_power");
	if (ret) {
		pr_err(PREFIX "Failed to get DAC power GPIO\n");
		return ret;
	}

	ret = gpio_direction_output(OMAP3_PANDORA_DAC_POWER_GPIO, 0);
	if (ret) {
		pr_err(PREFIX "Failed to set DAC power GPIO direction\n");
		goto fail0;
	}

	ret = gpio_request(OMAP3_PANDORA_AMP_POWER_GPIO, "amp_power");
	if (ret) {
		pr_err(PREFIX "Failed to get amp power GPIO\n");
		goto fail0;
	}

	ret = gpio_direction_output(OMAP3_PANDORA_AMP_POWER_GPIO, 0);
	if (ret) {
		pr_err(PREFIX "Failed to set amp power GPIO direction\n");
		goto fail1;
	}

	omap3pandora_snd_device = platform_device_alloc("soc-audio", -1);
	if (omap3pandora_snd_device == NULL) {
		pr_err(PREFIX "Platform device allocation failed\n");
		ret = -ENOMEM;
		goto fail1;
	}

	platform_set_drvdata(omap3pandora_snd_device, &snd_soc_card_omap3pandora);

	ret = platform_device_add(omap3pandora_snd_device);
	if (ret) {
		pr_err(PREFIX "Unable to add platform device\n");
		goto fail2;
	}

	omap3pandora_dac_reg = regulator_get(&omap3pandora_snd_device->dev, "vcc");
	if (IS_ERR(omap3pandora_dac_reg)) {
		pr_err(PREFIX "Failed to get DAC regulator from %s: %ld\n",
			dev_name(&omap3pandora_snd_device->dev),
			PTR_ERR(omap3pandora_dac_reg));
		ret = PTR_ERR(omap3pandora_dac_reg);
		goto fail3;
	}

	return 0;

fail3:
	platform_device_del(omap3pandora_snd_device);
fail2:
	platform_device_put(omap3pandora_snd_device);
fail1:
	gpio_free(OMAP3_PANDORA_AMP_POWER_GPIO);
fail0:
	gpio_free(OMAP3_PANDORA_DAC_POWER_GPIO);
	return ret;
}
module_init(omap3pandora_soc_init);

static void __exit omap3pandora_soc_exit(void)
{
	regulator_put(omap3pandora_dac_reg);
	platform_device_unregister(omap3pandora_snd_device);
	gpio_free(OMAP3_PANDORA_AMP_POWER_GPIO);
	gpio_free(OMAP3_PANDORA_DAC_POWER_GPIO);
}
module_exit(omap3pandora_soc_exit);

MODULE_AUTHOR("Grazvydas Ignotas <notasas@gmail.com>");
MODULE_DESCRIPTION("ALSA SoC OMAP3 Pandora");
MODULE_LICENSE("GPL");
