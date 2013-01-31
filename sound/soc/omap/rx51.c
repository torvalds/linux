/*
 * rx51.c  --  SoC audio for Nokia RX-51
 *
 * Copyright (C) 2008 - 2009 Nokia Corporation
 *
 * Contact: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *          Eduardo Valentin <eduardo.valentin@nokia.com>
 *          Jarkko Nikula <jarkko.nikula@bitmer.com>
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>
#include "../codecs/tpa6130a2.h"

#include <asm/mach-types.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"

#define RX51_TVOUT_SEL_GPIO		40
#define RX51_JACK_DETECT_GPIO		177
#define RX51_ECI_SW_GPIO		182
/*
 * REVISIT: TWL4030 GPIO base in RX-51. Now statically defined to 192. This
 * gpio is reserved in arch/arm/mach-omap2/board-rx51-peripherals.c
 */
#define RX51_SPEAKER_AMP_TWL_GPIO	(192 + 7)

enum {
	RX51_JACK_DISABLED,
	RX51_JACK_TVOUT,		/* tv-out with stereo output */
	RX51_JACK_HP,			/* headphone: stereo output, no mic */
	RX51_JACK_HS,			/* headset: stereo output with mic */
};

static int rx51_spk_func;
static int rx51_dmic_func;
static int rx51_jack_func;

static void rx51_ext_control(struct snd_soc_dapm_context *dapm)
{
	int hp = 0, hs = 0, tvout = 0;

	switch (rx51_jack_func) {
	case RX51_JACK_TVOUT:
		tvout = 1;
		hp = 1;
		break;
	case RX51_JACK_HS:
		hs = 1;
	case RX51_JACK_HP:
		hp = 1;
		break;
	}

	if (rx51_spk_func)
		snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin(dapm, "Ext Spk");
	if (rx51_dmic_func)
		snd_soc_dapm_enable_pin(dapm, "DMic");
	else
		snd_soc_dapm_disable_pin(dapm, "DMic");
	if (hp)
		snd_soc_dapm_enable_pin(dapm, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin(dapm, "Headphone Jack");
	if (hs)
		snd_soc_dapm_enable_pin(dapm, "HS Mic");
	else
		snd_soc_dapm_disable_pin(dapm, "HS Mic");

	gpio_set_value(RX51_TVOUT_SEL_GPIO, tvout);

	snd_soc_dapm_sync(dapm);
}

static int rx51_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	snd_pcm_hw_constraint_minmax(runtime,
				     SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);
	rx51_ext_control(&card->dapm);

	return 0;
}

static int rx51_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	/* Set the codec system clock for DAC and ADC */
	return snd_soc_dai_set_sysclk(codec_dai, 0, 19200000,
				      SND_SOC_CLOCK_IN);
}

static struct snd_soc_ops rx51_ops = {
	.startup = rx51_startup,
	.hw_params = rx51_hw_params,
};

static int rx51_get_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_spk_func;

	return 0;
}

static int rx51_set_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (rx51_spk_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_spk_func = ucontrol->value.integer.value[0];
	rx51_ext_control(&card->dapm);

	return 1;
}

static int rx51_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value_cansleep(RX51_SPEAKER_AMP_TWL_GPIO, 1);
	else
		gpio_set_value_cansleep(RX51_SPEAKER_AMP_TWL_GPIO, 0);

	return 0;
}

static int rx51_hp_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->dapm->codec;

	if (SND_SOC_DAPM_EVENT_ON(event))
		tpa6130a2_stereo_enable(codec, 1);
	else
		tpa6130a2_stereo_enable(codec, 0);

	return 0;
}

static int rx51_get_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_dmic_func;

	return 0;
}

static int rx51_set_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (rx51_dmic_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_dmic_func = ucontrol->value.integer.value[0];
	rx51_ext_control(&card->dapm);

	return 1;
}

static int rx51_get_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_jack_func;

	return 0;
}

static int rx51_set_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (rx51_jack_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_jack_func = ucontrol->value.integer.value[0];
	rx51_ext_control(&card->dapm);

	return 1;
}

static struct snd_soc_jack rx51_av_jack;

static struct snd_soc_jack_gpio rx51_av_jack_gpios[] = {
	{
		.gpio = RX51_JACK_DETECT_GPIO,
		.name = "avdet-gpio",
		.report = SND_JACK_HEADSET,
		.invert = 1,
		.debounce_time = 200,
	},
};

static const struct snd_soc_dapm_widget aic34_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", rx51_spk_event),
	SND_SOC_DAPM_MIC("DMic", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", rx51_hp_event),
	SND_SOC_DAPM_MIC("HS Mic", NULL),
	SND_SOC_DAPM_LINE("FM Transmitter", NULL),
};

static const struct snd_soc_dapm_widget aic34_dapm_widgetsb[] = {
	SND_SOC_DAPM_SPK("Earphone", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Ext Spk", NULL, "HPLOUT"},
	{"Ext Spk", NULL, "HPROUT"},
	{"Headphone Jack", NULL, "LLOUT"},
	{"Headphone Jack", NULL, "RLOUT"},
	{"FM Transmitter", NULL, "LLOUT"},
	{"FM Transmitter", NULL, "RLOUT"},

	{"DMic Rate 64", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "DMic"},
};

static const struct snd_soc_dapm_route audio_mapb[] = {
	{"b LINE2R", NULL, "MONO_LOUT"},
	{"Earphone", NULL, "b HPLOUT"},

	{"LINE1L", NULL, "b Mic Bias"},
	{"b Mic Bias", NULL, "HS Mic"}
};

static const char *spk_function[] = {"Off", "On"};
static const char *input_function[] = {"ADC", "Digital Mic"};
static const char *jack_function[] = {"Off", "TV-OUT", "Headphone", "Headset"};

static const struct soc_enum rx51_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(spk_function), spk_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(input_function), input_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_function), jack_function),
};

static const struct snd_kcontrol_new aic34_rx51_controls[] = {
	SOC_ENUM_EXT("Speaker Function", rx51_enum[0],
		     rx51_get_spk, rx51_set_spk),
	SOC_ENUM_EXT("Input Select",  rx51_enum[1],
		     rx51_get_input, rx51_set_input),
	SOC_ENUM_EXT("Jack Function", rx51_enum[2],
		     rx51_get_jack, rx51_set_jack),
	SOC_DAPM_PIN_SWITCH("FM Transmitter"),
};

static const struct snd_kcontrol_new aic34_rx51_controlsb[] = {
	SOC_DAPM_PIN_SWITCH("Earphone"),
};

static int rx51_aic34_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int err;

	/* Set up NC codec pins */
	snd_soc_dapm_nc_pin(dapm, "MIC3L");
	snd_soc_dapm_nc_pin(dapm, "MIC3R");
	snd_soc_dapm_nc_pin(dapm, "LINE1R");

	/* Add RX-51 specific controls */
	err = snd_soc_add_card_controls(rtd->card, aic34_rx51_controls,
				   ARRAY_SIZE(aic34_rx51_controls));
	if (err < 0)
		return err;

	/* Add RX-51 specific widgets */
	snd_soc_dapm_new_controls(dapm, aic34_dapm_widgets,
				  ARRAY_SIZE(aic34_dapm_widgets));

	/* Set up RX-51 specific audio path audio_map */
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	err = tpa6130a2_add_controls(codec);
	if (err < 0)
		return err;
	snd_soc_limit_volume(codec, "TPA6130A2 Headphone Playback Volume", 42);

	err = omap_mcbsp_st_add_controls(rtd);
	if (err < 0)
		return err;

	/* AV jack detection */
	err = snd_soc_jack_new(codec, "AV Jack",
			       SND_JACK_HEADSET | SND_JACK_VIDEOOUT,
			       &rx51_av_jack);
	if (err)
		return err;
	err = snd_soc_jack_add_gpios(&rx51_av_jack,
				     ARRAY_SIZE(rx51_av_jack_gpios),
				     rx51_av_jack_gpios);

	return err;
}

static int rx51_aic34b_init(struct snd_soc_dapm_context *dapm)
{
	int err;

	err = snd_soc_add_card_controls(dapm->card, aic34_rx51_controlsb,
				   ARRAY_SIZE(aic34_rx51_controlsb));
	if (err < 0)
		return err;

	err = snd_soc_dapm_new_controls(dapm, aic34_dapm_widgetsb,
					ARRAY_SIZE(aic34_dapm_widgetsb));
	if (err < 0)
		return 0;

	return snd_soc_dapm_add_routes(dapm, audio_mapb,
				       ARRAY_SIZE(audio_mapb));
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link rx51_dai[] = {
	{
		.name = "TLV320AIC34",
		.stream_name = "AIC34",
		.cpu_dai_name = "omap-mcbsp.2",
		.codec_dai_name = "tlv320aic3x-hifi",
		.platform_name = "omap-pcm-audio",
		.codec_name = "tlv320aic3x-codec.2-0018",
		.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
		.init = rx51_aic34_init,
		.ops = &rx51_ops,
	},
};

static struct snd_soc_aux_dev rx51_aux_dev[] = {
	{
		.name = "TLV320AIC34b",
		.codec_name = "tlv320aic3x-codec.2-0019",
		.init = rx51_aic34b_init,
	},
};

static struct snd_soc_codec_conf rx51_codec_conf[] = {
	{
		.dev_name = "tlv320aic3x-codec.2-0019",
		.name_prefix = "b",
	},
};

/* Audio card */
static struct snd_soc_card rx51_sound_card = {
	.name = "RX-51",
	.owner = THIS_MODULE,
	.dai_link = rx51_dai,
	.num_links = ARRAY_SIZE(rx51_dai),
	.aux_dev = rx51_aux_dev,
	.num_aux_devs = ARRAY_SIZE(rx51_aux_dev),
	.codec_conf = rx51_codec_conf,
	.num_configs = ARRAY_SIZE(rx51_codec_conf),
};

static struct platform_device *rx51_snd_device;

static int __init rx51_soc_init(void)
{
	int err;

	if (!machine_is_nokia_rx51())
		return -ENODEV;

	err = gpio_request_one(RX51_TVOUT_SEL_GPIO,
			       GPIOF_DIR_OUT | GPIOF_INIT_LOW, "tvout_sel");
	if (err)
		goto err_gpio_tvout_sel;
	err = gpio_request_one(RX51_ECI_SW_GPIO,
			       GPIOF_DIR_OUT | GPIOF_INIT_HIGH, "eci_sw");
	if (err)
		goto err_gpio_eci_sw;

	rx51_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rx51_snd_device) {
		err = -ENOMEM;
		goto err1;
	}

	platform_set_drvdata(rx51_snd_device, &rx51_sound_card);

	err = platform_device_add(rx51_snd_device);
	if (err)
		goto err2;

	return 0;
err2:
	platform_device_put(rx51_snd_device);
err1:
	gpio_free(RX51_ECI_SW_GPIO);
err_gpio_eci_sw:
	gpio_free(RX51_TVOUT_SEL_GPIO);
err_gpio_tvout_sel:

	return err;
}

static void __exit rx51_soc_exit(void)
{
	snd_soc_jack_free_gpios(&rx51_av_jack, ARRAY_SIZE(rx51_av_jack_gpios),
				rx51_av_jack_gpios);

	platform_device_unregister(rx51_snd_device);
	gpio_free(RX51_ECI_SW_GPIO);
	gpio_free(RX51_TVOUT_SEL_GPIO);
}

module_init(rx51_soc_init);
module_exit(rx51_soc_exit);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("ALSA SoC Nokia RX-51");
MODULE_LICENSE("GPL");
