// SPDX-License-Identifier: GPL-2.0-only
/*
 * n810.c  --  SoC audio for Nokia N810
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include "omap-mcbsp.h"

static struct gpio_desc *n810_headset_amp;
static struct gpio_desc *n810_speaker_amp;

enum {
	N810_JACK_DISABLED,
	N810_JACK_HP,
	N810_JACK_HS,
	N810_JACK_MIC,
};

static struct clk *sys_clkout2;
static struct clk *sys_clkout2_src;
static struct clk *func96m_clk;

static int n810_spk_func;
static int n810_jack_func;
static int n810_dmic_func;

static void n810_ext_control(struct snd_soc_dapm_context *dapm)
{
	int hp = 0, line1l = 0;

	switch (n810_jack_func) {
	case N810_JACK_HS:
		line1l = 1;
		fallthrough;
	case N810_JACK_HP:
		hp = 1;
		break;
	case N810_JACK_MIC:
		line1l = 1;
		break;
	}

	snd_soc_dapm_mutex_lock(dapm);

	if (n810_spk_func)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Ext Spk");

	if (hp)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
	if (line1l)
		snd_soc_dapm_enable_pin_unlocked(dapm, "HS Mic");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "HS Mic");

	if (n810_dmic_func)
		snd_soc_dapm_enable_pin_unlocked(dapm, "DMic");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "DMic");

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static int n810_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	snd_pcm_hw_constraint_single(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 2);

	n810_ext_control(&rtd->card->dapm);
	return clk_prepare_enable(sys_clkout2);
}

static void n810_shutdown(struct snd_pcm_substream *substream)
{
	clk_disable_unprepare(sys_clkout2);
}

static int n810_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int err;

	/* Set the codec system clock for DAC and ADC */
	err = snd_soc_dai_set_sysclk(codec_dai, 0, 12000000,
					    SND_SOC_CLOCK_IN);

	return err;
}

static const struct snd_soc_ops n810_ops = {
	.startup = n810_startup,
	.hw_params = n810_hw_params,
	.shutdown = n810_shutdown,
};

static int n810_get_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = n810_spk_func;

	return 0;
}

static int n810_set_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card =  snd_kcontrol_chip(kcontrol);

	if (n810_spk_func == ucontrol->value.enumerated.item[0])
		return 0;

	n810_spk_func = ucontrol->value.enumerated.item[0];
	n810_ext_control(&card->dapm);

	return 1;
}

static int n810_get_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = n810_jack_func;

	return 0;
}

static int n810_set_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card =  snd_kcontrol_chip(kcontrol);

	if (n810_jack_func == ucontrol->value.enumerated.item[0])
		return 0;

	n810_jack_func = ucontrol->value.enumerated.item[0];
	n810_ext_control(&card->dapm);

	return 1;
}

static int n810_get_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = n810_dmic_func;

	return 0;
}

static int n810_set_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card =  snd_kcontrol_chip(kcontrol);

	if (n810_dmic_func == ucontrol->value.enumerated.item[0])
		return 0;

	n810_dmic_func = ucontrol->value.enumerated.item[0];
	n810_ext_control(&card->dapm);

	return 1;
}

static int n810_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpiod_set_value(n810_speaker_amp, 1);
	else
		gpiod_set_value(n810_speaker_amp, 0);

	return 0;
}

static int n810_jack_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpiod_set_value(n810_headset_amp, 1);
	else
		gpiod_set_value(n810_headset_amp, 0);

	return 0;
}

static const struct snd_soc_dapm_widget aic33_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", n810_spk_event),
	SND_SOC_DAPM_HP("Headphone Jack", n810_jack_event),
	SND_SOC_DAPM_MIC("DMic", NULL),
	SND_SOC_DAPM_MIC("HS Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "HPLOUT"},
	{"Headphone Jack", NULL, "HPROUT"},

	{"Ext Spk", NULL, "LLOUT"},
	{"Ext Spk", NULL, "RLOUT"},

	{"DMic Rate 64", NULL, "DMic"},
	{"DMic", NULL, "Mic Bias"},

	/*
	 * Note that the mic bias is coming from Retu/Vilma and we don't have
	 * control over it atm. The analog HS mic is not working. <- TODO
	 */
	{"LINE1L", NULL, "HS Mic"},
};

static const char *spk_function[] = {"Off", "On"};
static const char *jack_function[] = {"Off", "Headphone", "Headset", "Mic"};
static const char *input_function[] = {"ADC", "Digital Mic"};
static const struct soc_enum n810_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(spk_function), spk_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_function), jack_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(input_function), input_function),
};

static const struct snd_kcontrol_new aic33_n810_controls[] = {
	SOC_ENUM_EXT("Speaker Function", n810_enum[0],
		     n810_get_spk, n810_set_spk),
	SOC_ENUM_EXT("Jack Function", n810_enum[1],
		     n810_get_jack, n810_set_jack),
	SOC_ENUM_EXT("Input Select",  n810_enum[2],
		     n810_get_input, n810_set_input),
};

/* Digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(aic33,
	DAILINK_COMP_ARRAY(COMP_CPU("48076000.mcbsp")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tlv320aic3x-codec.1-0018",
				      "tlv320aic3x-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("48076000.mcbsp")));

static struct snd_soc_dai_link n810_dai = {
	.name = "TLV320AIC33",
	.stream_name = "AIC33",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	.ops = &n810_ops,
	SND_SOC_DAILINK_REG(aic33),
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_n810 = {
	.name = "N810",
	.owner = THIS_MODULE,
	.dai_link = &n810_dai,
	.num_links = 1,

	.controls = aic33_n810_controls,
	.num_controls = ARRAY_SIZE(aic33_n810_controls),
	.dapm_widgets = aic33_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aic33_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
};

static struct platform_device *n810_snd_device;

static int __init n810_soc_init(void)
{
	int err;
	struct device *dev;

	if (!of_have_populated_dt() ||
	    (!of_machine_is_compatible("nokia,n810") &&
	     !of_machine_is_compatible("nokia,n810-wimax")))
		return -ENODEV;

	n810_snd_device = platform_device_alloc("soc-audio", -1);
	if (!n810_snd_device)
		return -ENOMEM;

	platform_set_drvdata(n810_snd_device, &snd_soc_n810);
	err = platform_device_add(n810_snd_device);
	if (err)
		goto err1;

	dev = &n810_snd_device->dev;

	sys_clkout2_src = clk_get(dev, "sys_clkout2_src");
	if (IS_ERR(sys_clkout2_src)) {
		dev_err(dev, "Could not get sys_clkout2_src clock\n");
		err = PTR_ERR(sys_clkout2_src);
		goto err2;
	}
	sys_clkout2 = clk_get(dev, "sys_clkout2");
	if (IS_ERR(sys_clkout2)) {
		dev_err(dev, "Could not get sys_clkout2\n");
		err = PTR_ERR(sys_clkout2);
		goto err3;
	}
	/*
	 * Configure 12 MHz output on SYS_CLKOUT2. Therefore we must use
	 * 96 MHz as its parent in order to get 12 MHz
	 */
	func96m_clk = clk_get(dev, "func_96m_ck");
	if (IS_ERR(func96m_clk)) {
		dev_err(dev, "Could not get func 96M clock\n");
		err = PTR_ERR(func96m_clk);
		goto err4;
	}
	clk_set_parent(sys_clkout2_src, func96m_clk);
	clk_set_rate(sys_clkout2, 12000000);

	n810_headset_amp = devm_gpiod_get(&n810_snd_device->dev,
					  "headphone", GPIOD_OUT_LOW);
	if (IS_ERR(n810_headset_amp)) {
		err = PTR_ERR(n810_headset_amp);
		goto err4;
	}

	n810_speaker_amp = devm_gpiod_get(&n810_snd_device->dev,
					  "speaker", GPIOD_OUT_LOW);
	if (IS_ERR(n810_speaker_amp)) {
		err = PTR_ERR(n810_speaker_amp);
		goto err4;
	}

	return 0;
err4:
	clk_put(sys_clkout2);
err3:
	clk_put(sys_clkout2_src);
err2:
	platform_device_del(n810_snd_device);
err1:
	platform_device_put(n810_snd_device);

	return err;
}

static void __exit n810_soc_exit(void)
{
	clk_put(sys_clkout2_src);
	clk_put(sys_clkout2);
	clk_put(func96m_clk);

	platform_device_unregister(n810_snd_device);
}

module_init(n810_soc_init);
module_exit(n810_soc_exit);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@bitmer.com>");
MODULE_DESCRIPTION("ALSA SoC Nokia N810");
MODULE_LICENSE("GPL");
