/*
 * n810.c  --  SoC audio for Nokia N810
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <plat/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"

#define N810_HEADSET_AMP_GPIO	10
#define N810_SPEAKER_AMP_GPIO	101

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

static void n810_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int hp = 0, line1l = 0;

	switch (n810_jack_func) {
	case N810_JACK_HS:
		line1l = 1;
	case N810_JACK_HP:
		hp = 1;
		break;
	case N810_JACK_MIC:
		line1l = 1;
		break;
	}

	if (n810_spk_func)
		snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin(dapm, "Ext Spk");

	if (hp)
		snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
	if (line1l)
		snd_soc_dapm_enable_pin(dapm, "LINE1L");
	else
		snd_soc_dapm_disable_pin(dapm, "LINE1L");

	if (n810_dmic_func)
		snd_soc_dapm_enable_pin(dapm, "DMic");
	else
		snd_soc_dapm_disable_pin(dapm, "DMic");

	snd_soc_dapm_sync(dapm);
}

static int n810_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	snd_pcm_hw_constraint_minmax(runtime,
				     SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);

	n810_ext_control(codec);
	return clk_enable(sys_clkout2);
}

static void n810_shutdown(struct snd_pcm_substream *substream)
{
	clk_disable(sys_clkout2);
}

static int n810_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int err;

	/* Set the codec system clock for DAC and ADC */
	err = snd_soc_dai_set_sysclk(codec_dai, 0, 12000000,
					    SND_SOC_CLOCK_IN);

	return err;
}

static struct snd_soc_ops n810_ops = {
	.startup = n810_startup,
	.hw_params = n810_hw_params,
	.shutdown = n810_shutdown,
};

static int n810_get_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = n810_spk_func;

	return 0;
}

static int n810_set_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (n810_spk_func == ucontrol->value.integer.value[0])
		return 0;

	n810_spk_func = ucontrol->value.integer.value[0];
	n810_ext_control(codec);

	return 1;
}

static int n810_get_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = n810_jack_func;

	return 0;
}

static int n810_set_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (n810_jack_func == ucontrol->value.integer.value[0])
		return 0;

	n810_jack_func = ucontrol->value.integer.value[0];
	n810_ext_control(codec);

	return 1;
}

static int n810_get_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = n810_dmic_func;

	return 0;
}

static int n810_set_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (n810_dmic_func == ucontrol->value.integer.value[0])
		return 0;

	n810_dmic_func = ucontrol->value.integer.value[0];
	n810_ext_control(codec);

	return 1;
}

static int n810_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(N810_SPEAKER_AMP_GPIO, 1);
	else
		gpio_set_value(N810_SPEAKER_AMP_GPIO, 0);

	return 0;
}

static int n810_jack_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(N810_HEADSET_AMP_GPIO, 1);
	else
		gpio_set_value(N810_HEADSET_AMP_GPIO, 0);

	return 0;
}

static const struct snd_soc_dapm_widget aic33_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", n810_spk_event),
	SND_SOC_DAPM_HP("Headphone Jack", n810_jack_event),
	SND_SOC_DAPM_MIC("DMic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "HPLOUT"},
	{"Headphone Jack", NULL, "HPROUT"},

	{"Ext Spk", NULL, "LLOUT"},
	{"Ext Spk", NULL, "RLOUT"},

	{"DMic Rate 64", NULL, "Mic Bias 2V"},
	{"Mic Bias 2V", NULL, "DMic"},
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

static int n810_aic33_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* Not connected */
	snd_soc_dapm_nc_pin(dapm, "MONO_LOUT");
	snd_soc_dapm_nc_pin(dapm, "HPLCOM");
	snd_soc_dapm_nc_pin(dapm, "HPRCOM");
	snd_soc_dapm_nc_pin(dapm, "MIC3L");
	snd_soc_dapm_nc_pin(dapm, "MIC3R");
	snd_soc_dapm_nc_pin(dapm, "LINE1R");
	snd_soc_dapm_nc_pin(dapm, "LINE2L");
	snd_soc_dapm_nc_pin(dapm, "LINE2R");

	return 0;
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link n810_dai = {
	.name = "TLV320AIC33",
	.stream_name = "AIC33",
	.cpu_dai_name = "omap-mcbsp-dai.1",
	.platform_name = "omap-pcm-audio",
	.codec_name = "tlv320aic3x-codec.2-0018",
	.codec_dai_name = "tlv320aic3x-hifi",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	.init = n810_aic33_init,
	.ops = &n810_ops,
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
};

static struct platform_device *n810_snd_device;

static int __init n810_soc_init(void)
{
	int err;
	struct device *dev;

	if (!(machine_is_nokia_n810() || machine_is_nokia_n810_wimax()))
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

	BUG_ON((gpio_request(N810_HEADSET_AMP_GPIO, "hs_amp") < 0) ||
	       (gpio_request(N810_SPEAKER_AMP_GPIO, "spk_amp") < 0));

	gpio_direction_output(N810_HEADSET_AMP_GPIO, 0);
	gpio_direction_output(N810_SPEAKER_AMP_GPIO, 0);

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
	gpio_free(N810_SPEAKER_AMP_GPIO);
	gpio_free(N810_HEADSET_AMP_GPIO);
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
