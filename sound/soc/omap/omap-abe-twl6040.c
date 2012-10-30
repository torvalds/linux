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
#include <linux/of.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "omap-dmic.h"
#include "omap-mcpdm.h"
#include "omap-pcm.h"
#include "../codecs/twl6040.h"

struct abe_twl6040 {
	int	jack_detection;	/* board can detect jack events */
	int	mclk_freq;	/* MCLK frequency speed for twl6040 */

	struct platform_device *dmic_codec_dev;
};

static int omap_abe_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct abe_twl6040 *priv = snd_soc_card_get_drvdata(card);
	int clk_id, freq;
	int ret;

	clk_id = twl6040_get_clk_id(rtd->codec);
	if (clk_id == TWL6040_SYSCLK_SEL_HPPLL)
		freq = priv->mclk_freq;
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
	/* Outputs */
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_SPK("Earphone Spk", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_SPK("Vibrator", NULL),

	/* Inputs */
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Main Handset Mic", NULL),
	SND_SOC_DAPM_MIC("Sub Handset Mic", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),

	/* Digital microphones */
	SND_SOC_DAPM_MIC("Digital Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Routings for outputs */
	{"Headset Stereophone", NULL, "HSOL"},
	{"Headset Stereophone", NULL, "HSOR"},

	{"Earphone Spk", NULL, "EP"},

	{"Ext Spk", NULL, "HFL"},
	{"Ext Spk", NULL, "HFR"},

	{"Line Out", NULL, "AUXL"},
	{"Line Out", NULL, "AUXR"},

	{"Vibrator", NULL, "VIBRAL"},
	{"Vibrator", NULL, "VIBRAR"},

	/* Routings for inputs */
	{"HSMIC", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "Headset Mic Bias"},

	{"MAINMIC", NULL, "Main Handset Mic"},
	{"Main Handset Mic", NULL, "Main Mic Bias"},

	{"SUBMIC", NULL, "Sub Handset Mic"},
	{"Sub Handset Mic", NULL, "Main Mic Bias"},

	{"AFML", NULL, "Line In"},
	{"AFMR", NULL, "Line In"},
};

static inline void twl6040_disconnect_pin(struct snd_soc_dapm_context *dapm,
					  int connected, char *pin)
{
	if (!connected)
		snd_soc_dapm_disable_pin(dapm, pin);
}

static int omap_abe_twl6040_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = codec->card;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct omap_abe_twl6040_data *pdata = dev_get_platdata(card->dev);
	struct abe_twl6040 *priv = snd_soc_card_get_drvdata(card);
	int hs_trim;
	int ret = 0;

	/*
	 * Configure McPDM offset cancellation based on the HSOTRIM value from
	 * twl6040.
	 */
	hs_trim = twl6040_get_trim_value(codec, TWL6040_TRIM_HSOTRIM);
	omap_mcpdm_configure_dn_offsets(rtd, TWL6040_HSF_TRIM_LEFT(hs_trim),
					TWL6040_HSF_TRIM_RIGHT(hs_trim));

	/* Headset jack detection only if it is supported */
	if (priv->jack_detection) {
		ret = snd_soc_jack_new(codec, "Headset Jack",
					SND_JACK_HEADSET, &hs_jack);
		if (ret)
			return ret;

		ret = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
					hs_jack_pins);
		twl6040_hs_jack_detect(codec, &hs_jack, SND_JACK_HEADSET);
	}

	/*
	 * NULL pdata means we booted with DT. In this case the routing is
	 * provided and the card is fully routed, no need to mark pins.
	 */
	if (!pdata)
		return ret;

	/* Disable not connected paths if not used */
	twl6040_disconnect_pin(dapm, pdata->has_hs, "Headset Stereophone");
	twl6040_disconnect_pin(dapm, pdata->has_hf, "Ext Spk");
	twl6040_disconnect_pin(dapm, pdata->has_ep, "Earphone Spk");
	twl6040_disconnect_pin(dapm, pdata->has_aux, "Line Out");
	twl6040_disconnect_pin(dapm, pdata->has_vibra, "Vibrator");
	twl6040_disconnect_pin(dapm, pdata->has_hsmic, "Headset Mic");
	twl6040_disconnect_pin(dapm, pdata->has_mainmic, "Main Handset Mic");
	twl6040_disconnect_pin(dapm, pdata->has_submic, "Sub Handset Mic");
	twl6040_disconnect_pin(dapm, pdata->has_afm, "Line In");

	return ret;
}

static const struct snd_soc_dapm_route dmic_audio_map[] = {
	{"DMic", NULL, "Digital Mic"},
	{"Digital Mic", NULL, "Digital Mic1 Bias"},
};

static int omap_abe_dmic_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	return snd_soc_dapm_add_routes(dapm, dmic_audio_map,
				ARRAY_SIZE(dmic_audio_map));
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link abe_twl6040_dai_links[] = {
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

	.dapm_widgets = twl6040_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(twl6040_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static __devinit int omap_abe_probe(struct platform_device *pdev)
{
	struct omap_abe_twl6040_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *node = pdev->dev.of_node;
	struct snd_soc_card *card = &omap_abe_card;
	struct abe_twl6040 *priv;
	int num_links = 0;
	int ret = 0;

	card->dev = &pdev->dev;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct abe_twl6040), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->dmic_codec_dev = ERR_PTR(-EINVAL);

	if (node) {
		struct device_node *dai_node;

		if (snd_soc_of_parse_card_name(card, "ti,model")) {
			dev_err(&pdev->dev, "Card name is not provided\n");
			return -ENODEV;
		}

		ret = snd_soc_of_parse_audio_routing(card,
						"ti,audio-routing");
		if (ret) {
			dev_err(&pdev->dev,
				"Error while parsing DAPM routing\n");
			return ret;
		}

		dai_node = of_parse_phandle(node, "ti,mcpdm", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McPDM node is not provided\n");
			return -EINVAL;
		}
		abe_twl6040_dai_links[0].cpu_dai_name  = NULL;
		abe_twl6040_dai_links[0].cpu_of_node = dai_node;

		dai_node = of_parse_phandle(node, "ti,dmic", 0);
		if (dai_node) {
			num_links = 2;
			abe_twl6040_dai_links[1].cpu_dai_name  = NULL;
			abe_twl6040_dai_links[1].cpu_of_node = dai_node;

			priv->dmic_codec_dev = platform_device_register_simple(
						"dmic-codec", -1, NULL, 0);
			if (IS_ERR(priv->dmic_codec_dev)) {
				dev_err(&pdev->dev,
					"Can't instantiate dmic-codec\n");
				return PTR_ERR(priv->dmic_codec_dev);
			}
		} else {
			num_links = 1;
		}

		of_property_read_u32(node, "ti,jack-detection",
				     &priv->jack_detection);
		of_property_read_u32(node, "ti,mclk-freq",
				     &priv->mclk_freq);
		if (!priv->mclk_freq) {
			dev_err(&pdev->dev, "MCLK frequency not provided\n");
			ret = -EINVAL;
			goto err_unregister;
		}

		omap_abe_card.fully_routed = 1;
	} else if (pdata) {
		if (pdata->card_name) {
			card->name = pdata->card_name;
		} else {
			dev_err(&pdev->dev, "Card name is not provided\n");
			return -ENODEV;
		}

		if (pdata->has_dmic)
			num_links = 2;
		else
			num_links = 1;

		priv->jack_detection = pdata->jack_detection;
		priv->mclk_freq = pdata->mclk_freq;
	} else {
		dev_err(&pdev->dev, "Missing pdata\n");
		return -ENODEV;
	}


	if (!priv->mclk_freq) {
		dev_err(&pdev->dev, "MCLK frequency missing\n");
		ret = -ENODEV;
		goto err_unregister;
	}

	card->dai_link = abe_twl6040_dai_links;
	card->num_links = num_links;

	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		goto err_unregister;
	}

	return 0;

err_unregister:
	if (!IS_ERR(priv->dmic_codec_dev))
		platform_device_unregister(priv->dmic_codec_dev);

	return ret;
}

static int __devexit omap_abe_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct abe_twl6040 *priv = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);

	if (!IS_ERR(priv->dmic_codec_dev))
		platform_device_unregister(priv->dmic_codec_dev);

	return 0;
}

static const struct of_device_id omap_abe_of_match[] = {
	{.compatible = "ti,abe-twl6040", },
	{ },
};
MODULE_DEVICE_TABLE(of, omap_abe_of_match);

static struct platform_driver omap_abe_driver = {
	.driver = {
		.name = "omap-abe-twl6040",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = omap_abe_of_match,
	},
	.probe = omap_abe_probe,
	.remove = __devexit_p(omap_abe_remove),
};

module_platform_driver(omap_abe_driver);

MODULE_AUTHOR("Misael Lopez Cruz <misael.lopez@ti.com>");
MODULE_DESCRIPTION("ALSA SoC for OMAP boards with ABE and twl6040 codec");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap-abe-twl6040");
