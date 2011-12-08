/*
 * omap-abe-twl6040.c  --  SoC audio for TI OMAP based boards with ABE and
 *			   twl6040 codec
 *
 * Author: Misael Lopez Cruz <misael.lopez@ti.com>
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
#include <linux/platform_device.h>
#include <linux/mfd/twl6040.h>
#include <linux/platform_data/omap-abe-twl6040.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <plat/hardware.h>
#include <plat/mux.h>

#include "omap-dmic.h"
#include "omap-mcpdm.h"
#include "omap-pcm.h"
#include "../codecs/twl6040.h"

static int omap_abe_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int clk_id, freq;
	int ret;

	clk_id = twl6040_get_clk_id(rtd->codec);
	if (clk_id == TWL6040_SYSCLK_SEL_HPPLL)
		freq = 38400000;
	else if (clk_id == TWL6040_SYSCLK_SEL_LPPLL)
		freq = 32768;
	else
		return -EINVAL;

	/* set the codec mclk */
	ret = snd_soc_dai_set_sysclk(codec_dai, clk_id, freq,
				SND_SOC_CLOCK_IN);
	if (ret) {
		printk(KERN_ERR "can't set codec system clock\n");
		return ret;
	}
	return ret;
}

static struct snd_soc_ops omap_abe_ops = {
	.hw_params = omap_abe_hw_params,
};

static int omap_abe_dmic_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	ret = snd_soc_dai_set_sysclk(cpu_dai, OMAP_DMIC_SYSCLK_PAD_CLKS,
				     19200000, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "can't set DMIC cpu system clock\n");
		return ret;
	}
	ret = snd_soc_dai_set_sysclk(cpu_dai, OMAP_DMIC_ABE_DMIC_CLK, 2400000,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		printk(KERN_ERR "can't set DMIC output clock\n");
		return ret;
	}
	return 0;
}

static struct snd_soc_ops omap_abe_dmic_ops = {
	.hw_params = omap_abe_dmic_hw_params,
};

/* Headset jack */
static struct snd_soc_jack hs_jack;

/*Headset jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE,
	},
};

/* SDP4430 machine DAPM */
static const struct snd_soc_dapm_widget twl6040_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Ext Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_SPK("Earphone Spk", NULL),
	SND_SOC_DAPM_INPUT("FM Stereo In"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* External Mics: MAINMIC, SUBMIC with bias*/
	{"MAINMIC", NULL, "Main Mic Bias"},
	{"SUBMIC", NULL, "Main Mic Bias"},
	{"Main Mic Bias", NULL, "Ext Mic"},

	/* External Speakers: HFL, HFR */
	{"Ext Spk", NULL, "HFL"},
	{"Ext Spk", NULL, "HFR"},

	/* Headset Mic: HSMIC with bias */
	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	/* Headset Stereophone (Headphone): HSOL, HSOR */
	{"Headset Stereophone", NULL, "HSOL"},
	{"Headset Stereophone", NULL, "HSOR"},

	/* Earphone speaker */
	{"Earphone Spk", NULL, "EP"},

	/* Aux/FM Stereo In: AFML, AFMR */
	{"AFML", NULL, "FM Stereo In"},
	{"AFMR", NULL, "FM Stereo In"},
};

static int omap_abe_twl6040_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	int ret, hs_trim;

	/*
	 * Configure McPDM offset cancellation based on the HSOTRIM value from
	 * twl6040.
	 */
	hs_trim = twl6040_get_trim_value(codec, TWL6040_TRIM_HSOTRIM);
	omap_mcpdm_configure_dn_offsets(rtd, TWL6040_HSF_TRIM_LEFT(hs_trim),
					TWL6040_HSF_TRIM_RIGHT(hs_trim));

	/* Headset jack detection */
	ret = snd_soc_jack_new(codec, "Headset Jack",
				SND_JACK_HEADSET, &hs_jack);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
				hs_jack_pins);

	if (machine_is_omap_4430sdp())
		twl6040_hs_jack_detect(codec, &hs_jack, SND_JACK_HEADSET);
	else
		snd_soc_jack_report(&hs_jack, SND_JACK_HEADSET, SND_JACK_HEADSET);

	return ret;
}

static const struct snd_soc_dapm_widget dmic_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Digital Mic", NULL),
};

static const struct snd_soc_dapm_route dmic_audio_map[] = {
	{"DMic", NULL, "Digital Mic1 Bias"},
	{"Digital Mic1 Bias", NULL, "Digital Mic"},
};

static int omap_abe_dmic_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, dmic_dapm_widgets,
				ARRAY_SIZE(dmic_dapm_widgets));
	if (ret)
		return ret;

	return snd_soc_dapm_add_routes(dapm, dmic_audio_map,
				ARRAY_SIZE(dmic_audio_map));
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sdp4430_dai[] = {
	{
		.name = "TWL6040",
		.stream_name = "TWL6040",
		.cpu_dai_name = "omap-mcpdm",
		.codec_dai_name = "twl6040-legacy",
		.platform_name = "omap-pcm-audio",
		.codec_name = "twl6040-codec",
		.init = omap_abe_twl6040_init,
		.ops = &omap_abe_ops,
	},
	{
		.name = "DMIC",
		.stream_name = "DMIC Capture",
		.cpu_dai_name = "omap-dmic",
		.codec_dai_name = "dmic-hifi",
		.platform_name = "omap-pcm-audio",
		.codec_name = "dmic-codec",
		.init = omap_abe_dmic_init,
		.ops = &omap_abe_dmic_ops,
	},
};

/* Audio machine driver */
static struct snd_soc_card omap_abe_card = {
	.owner = THIS_MODULE,
	.dai_link = sdp4430_dai,
	.num_links = ARRAY_SIZE(sdp4430_dai),

	.dapm_widgets = twl6040_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(twl6040_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static __devinit int omap_abe_probe(struct platform_device *pdev)
{
	struct omap_abe_twl6040_data *pdata = dev_get_platdata(&pdev->dev);
	struct snd_soc_card *card = &omap_abe_card;
	int ret;

	card->dev = &pdev->dev;

	if (!pdata) {
		dev_err(&pdev->dev, "Missing pdata\n");
		return -ENODEV;
	}

	if (pdata->card_name) {
		card->name = pdata->card_name;
	} else {
		dev_err(&pdev->dev, "Card name is not provided\n");
		return -ENODEV;
	}

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static int __devexit omap_abe_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver omap_abe_driver = {
	.driver = {
		.name = "omap-abe-twl6040",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = omap_abe_probe,
	.remove = __devexit_p(omap_abe_remove),
};

module_platform_driver(omap_abe_driver);

MODULE_AUTHOR("Misael Lopez Cruz <misael.lopez@ti.com>");
MODULE_DESCRIPTION("ALSA SoC for OMAP boards with ABE and twl6040 codec");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap-abe-twl6040");
