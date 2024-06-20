// SPDX-License-Identifier: GPL-2.0
//
// Freescale Generic ASoC Sound Card driver with ASRC
//
// Copyright (C) 2014 Freescale Semiconductor, Inc.
//
// Author: Nicolin Chen <nicoleotsuka@gmail.com>

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#if IS_ENABLED(CONFIG_SND_AC97_CODEC)
#include <sound/ac97_codec.h>
#endif
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/simple_card_utils.h>

#include "fsl_esai.h"
#include "fsl_sai.h"
#include "imx-audmux.h"

#include "../codecs/sgtl5000.h"
#include "../codecs/wm8962.h"
#include "../codecs/wm8960.h"
#include "../codecs/wm8994.h"
#include "../codecs/tlv320aic31xx.h"

#define CS427x_SYSCLK_MCLK 0

#define RX 0
#define TX 1

/* Default DAI format without Master and Slave flag */
#define DAI_FMT_BASE (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF)

/**
 * struct codec_priv - CODEC private data
 * @mclk_freq: Clock rate of MCLK
 * @free_freq: Clock rate of MCLK for hw_free()
 * @mclk_id: MCLK (or main clock) id for set_sysclk()
 * @fll_id: FLL (or secordary clock) id for set_sysclk()
 * @pll_id: PLL id for set_pll()
 */
struct codec_priv {
	unsigned long mclk_freq;
	unsigned long free_freq;
	u32 mclk_id;
	u32 fll_id;
	u32 pll_id;
};

/**
 * struct cpu_priv - CPU private data
 * @sysclk_freq: SYSCLK rates for set_sysclk()
 * @sysclk_dir: SYSCLK directions for set_sysclk()
 * @sysclk_id: SYSCLK ids for set_sysclk()
 * @slot_width: Slot width of each frame
 *
 * Note: [1] for tx and [0] for rx
 */
struct cpu_priv {
	unsigned long sysclk_freq[2];
	u32 sysclk_dir[2];
	u32 sysclk_id[2];
	u32 slot_width;
};

/**
 * struct fsl_asoc_card_priv - Freescale Generic ASOC card private data
 * @dai_link: DAI link structure including normal one and DPCM link
 * @hp_jack: Headphone Jack structure
 * @mic_jack: Microphone Jack structure
 * @pdev: platform device pointer
 * @codec_priv: CODEC private data
 * @cpu_priv: CPU private data
 * @card: ASoC card structure
 * @streams: Mask of current active streams
 * @sample_rate: Current sample rate
 * @sample_format: Current sample format
 * @asrc_rate: ASRC sample rate used by Back-Ends
 * @asrc_format: ASRC sample format used by Back-Ends
 * @dai_fmt: DAI format between CPU and CODEC
 * @name: Card name
 */

struct fsl_asoc_card_priv {
	struct snd_soc_dai_link dai_link[3];
	struct asoc_simple_jack hp_jack;
	struct asoc_simple_jack mic_jack;
	struct platform_device *pdev;
	struct codec_priv codec_priv;
	struct cpu_priv cpu_priv;
	struct snd_soc_card card;
	u8 streams;
	u32 sample_rate;
	snd_pcm_format_t sample_format;
	u32 asrc_rate;
	snd_pcm_format_t asrc_format;
	u32 dai_fmt;
	char name[32];
};

/*
 * This dapm route map exists for DPCM link only.
 * The other routes shall go through Device Tree.
 *
 * Note: keep all ASRC routes in the second half
 *	 to drop them easily for non-ASRC cases.
 */
static const struct snd_soc_dapm_route audio_map[] = {
	/* 1st half -- Normal DAPM routes */
	{"Playback",  NULL, "CPU-Playback"},
	{"CPU-Capture",  NULL, "Capture"},
	/* 2nd half -- ASRC DAPM routes */
	{"CPU-Playback",  NULL, "ASRC-Playback"},
	{"ASRC-Capture",  NULL, "CPU-Capture"},
};

static const struct snd_soc_dapm_route audio_map_ac97[] = {
	/* 1st half -- Normal DAPM routes */
	{"AC97 Playback",  NULL, "CPU AC97 Playback"},
	{"CPU AC97 Capture",  NULL, "AC97 Capture"},
	/* 2nd half -- ASRC DAPM routes */
	{"CPU AC97 Playback",  NULL, "ASRC-Playback"},
	{"ASRC-Capture",  NULL, "CPU AC97 Capture"},
};

static const struct snd_soc_dapm_route audio_map_tx[] = {
	/* 1st half -- Normal DAPM routes */
	{"Playback",  NULL, "CPU-Playback"},
	/* 2nd half -- ASRC DAPM routes */
	{"CPU-Playback",  NULL, "ASRC-Playback"},
};

static const struct snd_soc_dapm_route audio_map_rx[] = {
	/* 1st half -- Normal DAPM routes */
	{"CPU-Capture",  NULL, "Capture"},
	/* 2nd half -- ASRC DAPM routes */
	{"ASRC-Capture",  NULL, "CPU-Capture"},
};

/* Add all possible widgets into here without being redundant */
static const struct snd_soc_dapm_widget fsl_asoc_card_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line Out Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static bool fsl_asoc_card_is_ac97(struct fsl_asoc_card_priv *priv)
{
	return priv->dai_fmt == SND_SOC_DAIFMT_AC97;
}

static int fsl_asoc_card_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct codec_priv *codec_priv = &priv->codec_priv;
	struct cpu_priv *cpu_priv = &priv->cpu_priv;
	struct device *dev = rtd->card->dev;
	unsigned int pll_out;
	int ret;

	priv->sample_rate = params_rate(params);
	priv->sample_format = params_format(params);
	priv->streams |= BIT(substream->stream);

	if (fsl_asoc_card_is_ac97(priv))
		return 0;

	/* Specific configurations of DAIs starts from here */
	ret = snd_soc_dai_set_sysclk(asoc_rtd_to_cpu(rtd, 0), cpu_priv->sysclk_id[tx],
				     cpu_priv->sysclk_freq[tx],
				     cpu_priv->sysclk_dir[tx]);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set sysclk for cpu dai\n");
		goto fail;
	}

	if (cpu_priv->slot_width) {
		ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), 0x3, 0x3, 2,
					       cpu_priv->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set TDM slot for cpu dai\n");
			goto fail;
		}
	}

	/* Specific configuration for PLL */
	if (codec_priv->pll_id && codec_priv->fll_id) {
		if (priv->sample_format == SNDRV_PCM_FORMAT_S24_LE)
			pll_out = priv->sample_rate * 384;
		else
			pll_out = priv->sample_rate * 256;

		ret = snd_soc_dai_set_pll(asoc_rtd_to_codec(rtd, 0),
					  codec_priv->pll_id,
					  codec_priv->mclk_id,
					  codec_priv->mclk_freq, pll_out);
		if (ret) {
			dev_err(dev, "failed to start FLL: %d\n", ret);
			goto fail;
		}

		ret = snd_soc_dai_set_sysclk(asoc_rtd_to_codec(rtd, 0),
					     codec_priv->fll_id,
					     pll_out, SND_SOC_CLOCK_IN);

		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set SYSCLK: %d\n", ret);
			goto fail;
		}
	}

	return 0;

fail:
	priv->streams &= ~BIT(substream->stream);
	return ret;
}

static int fsl_asoc_card_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct codec_priv *codec_priv = &priv->codec_priv;
	struct device *dev = rtd->card->dev;
	int ret;

	priv->streams &= ~BIT(substream->stream);

	if (!priv->streams && codec_priv->pll_id && codec_priv->fll_id) {
		/* Force freq to be free_freq to avoid error message in codec */
		ret = snd_soc_dai_set_sysclk(asoc_rtd_to_codec(rtd, 0),
					     codec_priv->mclk_id,
					     codec_priv->free_freq,
					     SND_SOC_CLOCK_IN);
		if (ret) {
			dev_err(dev, "failed to switch away from FLL: %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_pll(asoc_rtd_to_codec(rtd, 0),
					  codec_priv->pll_id, 0, 0, 0);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to stop FLL: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_ops fsl_asoc_card_ops = {
	.hw_params = fsl_asoc_card_hw_params,
	.hw_free = fsl_asoc_card_hw_free,
};

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
			      struct snd_pcm_hw_params *params)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_interval *rate;
	struct snd_mask *mask;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	rate->max = rate->min = priv->asrc_rate;

	mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(mask);
	snd_mask_set_format(mask, priv->asrc_format);

	return 0;
}

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(hifi_fe,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(hifi_be,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

static struct snd_soc_dai_link fsl_asoc_card_dai[] = {
	/* Default ASoC DAI Link*/
	{
		.name = "HiFi",
		.stream_name = "HiFi",
		.ops = &fsl_asoc_card_ops,
		SND_SOC_DAILINK_REG(hifi),
	},
	/* DPCM Link between Front-End and Back-End (Optional) */
	{
		.name = "HiFi-ASRC-FE",
		.stream_name = "HiFi-ASRC-FE",
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hifi_fe),
	},
	{
		.name = "HiFi-ASRC-BE",
		.stream_name = "HiFi-ASRC-BE",
		.be_hw_params_fixup = be_hw_params_fixup,
		.ops = &fsl_asoc_card_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(hifi_be),
	},
};

static int fsl_asoc_card_audmux_init(struct device_node *np,
				     struct fsl_asoc_card_priv *priv)
{
	struct device *dev = &priv->pdev->dev;
	u32 int_ptcr = 0, ext_ptcr = 0;
	int int_port, ext_port;
	int ret;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(dev, "mux-int-port missing or invalid\n");
		return ret;
	}
	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the AUDMUX API expects it starts at 0.
	 */
	int_port--;
	ext_port--;

	/*
	 * Use asynchronous mode (6 wires) for all cases except AC97.
	 * If only 4 wires are needed, just set SSI into
	 * synchronous mode and enable 4 PADs in IOMUX.
	 */
	switch (priv->dai_fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		int_ptcr = IMX_AUDMUX_V2_PTCR_RFSEL(8 | ext_port) |
			   IMX_AUDMUX_V2_PTCR_RCSEL(8 | ext_port) |
			   IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			   IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			   IMX_AUDMUX_V2_PTCR_RFSDIR |
			   IMX_AUDMUX_V2_PTCR_RCLKDIR |
			   IMX_AUDMUX_V2_PTCR_TFSDIR |
			   IMX_AUDMUX_V2_PTCR_TCLKDIR;
		break;
	case SND_SOC_DAIFMT_CBP_CFC:
		int_ptcr = IMX_AUDMUX_V2_PTCR_RCSEL(8 | ext_port) |
			   IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			   IMX_AUDMUX_V2_PTCR_RCLKDIR |
			   IMX_AUDMUX_V2_PTCR_TCLKDIR;
		ext_ptcr = IMX_AUDMUX_V2_PTCR_RFSEL(8 | int_port) |
			   IMX_AUDMUX_V2_PTCR_TFSEL(int_port) |
			   IMX_AUDMUX_V2_PTCR_RFSDIR |
			   IMX_AUDMUX_V2_PTCR_TFSDIR;
		break;
	case SND_SOC_DAIFMT_CBC_CFP:
		int_ptcr = IMX_AUDMUX_V2_PTCR_RFSEL(8 | ext_port) |
			   IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			   IMX_AUDMUX_V2_PTCR_RFSDIR |
			   IMX_AUDMUX_V2_PTCR_TFSDIR;
		ext_ptcr = IMX_AUDMUX_V2_PTCR_RCSEL(8 | int_port) |
			   IMX_AUDMUX_V2_PTCR_TCSEL(int_port) |
			   IMX_AUDMUX_V2_PTCR_RCLKDIR |
			   IMX_AUDMUX_V2_PTCR_TCLKDIR;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		ext_ptcr = IMX_AUDMUX_V2_PTCR_RFSEL(8 | int_port) |
			   IMX_AUDMUX_V2_PTCR_RCSEL(8 | int_port) |
			   IMX_AUDMUX_V2_PTCR_TFSEL(int_port) |
			   IMX_AUDMUX_V2_PTCR_TCSEL(int_port) |
			   IMX_AUDMUX_V2_PTCR_RFSDIR |
			   IMX_AUDMUX_V2_PTCR_RCLKDIR |
			   IMX_AUDMUX_V2_PTCR_TFSDIR |
			   IMX_AUDMUX_V2_PTCR_TCLKDIR;
		break;
	default:
		if (!fsl_asoc_card_is_ac97(priv))
			return -EINVAL;
	}

	if (fsl_asoc_card_is_ac97(priv)) {
		int_ptcr = IMX_AUDMUX_V2_PTCR_SYN |
			   IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			   IMX_AUDMUX_V2_PTCR_TCLKDIR;
		ext_ptcr = IMX_AUDMUX_V2_PTCR_SYN |
			   IMX_AUDMUX_V2_PTCR_TFSEL(int_port) |
			   IMX_AUDMUX_V2_PTCR_TFSDIR;
	}

	/* Asynchronous mode can not be set along with RCLKDIR */
	if (!fsl_asoc_card_is_ac97(priv)) {
		unsigned int pdcr =
				IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port);

		ret = imx_audmux_v2_configure_port(int_port, 0,
						   pdcr);
		if (ret) {
			dev_err(dev, "audmux internal port setup failed\n");
			return ret;
		}
	}

	ret = imx_audmux_v2_configure_port(int_port, int_ptcr,
					   IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(dev, "audmux internal port setup failed\n");
		return ret;
	}

	if (!fsl_asoc_card_is_ac97(priv)) {
		unsigned int pdcr =
				IMX_AUDMUX_V2_PDCR_RXDSEL(int_port);

		ret = imx_audmux_v2_configure_port(ext_port, 0,
						   pdcr);
		if (ret) {
			dev_err(dev, "audmux external port setup failed\n");
			return ret;
		}
	}

	ret = imx_audmux_v2_configure_port(ext_port, ext_ptcr,
					   IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(dev, "audmux external port setup failed\n");
		return ret;
	}

	return 0;
}

static int hp_jack_event(struct notifier_block *nb, unsigned long event,
			 void *data)
{
	struct snd_soc_jack *jack = (struct snd_soc_jack *)data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;

	if (event & SND_JACK_HEADPHONE)
		/* Disable speaker if headphone is plugged in */
		return snd_soc_dapm_disable_pin(dapm, "Ext Spk");
	else
		return snd_soc_dapm_enable_pin(dapm, "Ext Spk");
}

static struct notifier_block hp_jack_nb = {
	.notifier_call = hp_jack_event,
};

static int mic_jack_event(struct notifier_block *nb, unsigned long event,
			  void *data)
{
	struct snd_soc_jack *jack = (struct snd_soc_jack *)data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;

	if (event & SND_JACK_MICROPHONE)
		/* Disable dmic if microphone is plugged in */
		return snd_soc_dapm_disable_pin(dapm, "DMIC");
	else
		return snd_soc_dapm_enable_pin(dapm, "DMIC");
}

static struct notifier_block mic_jack_nb = {
	.notifier_call = mic_jack_event,
};

static int fsl_asoc_card_late_probe(struct snd_soc_card *card)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_pcm_runtime *rtd = list_first_entry(
			&card->rtd_list, struct snd_soc_pcm_runtime, list);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct codec_priv *codec_priv = &priv->codec_priv;
	struct device *dev = card->dev;
	int ret;

	if (fsl_asoc_card_is_ac97(priv)) {
#if IS_ENABLED(CONFIG_SND_AC97_CODEC)
		struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
		struct snd_ac97 *ac97 = snd_soc_component_get_drvdata(component);

		/*
		 * Use slots 3/4 for S/PDIF so SSI won't try to enable
		 * other slots and send some samples there
		 * due to SLOTREQ bits for S/PDIF received from codec
		 */
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS,
				     AC97_EA_SPSA_SLOT_MASK, AC97_EA_SPSA_3_4);
#endif

		return 0;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, codec_priv->mclk_id,
				     codec_priv->mclk_freq, SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set sysclk in %s\n", __func__);
		return ret;
	}

	return 0;
}

static int fsl_asoc_card_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np, *codec_np, *asrc_np;
	struct device_node *np = pdev->dev.of_node;
	struct platform_device *asrc_pdev = NULL;
	struct device_node *bitclkprovider = NULL;
	struct device_node *frameprovider = NULL;
	struct platform_device *cpu_pdev;
	struct fsl_asoc_card_priv *priv;
	struct device *codec_dev = NULL;
	const char *codec_dai_name;
	const char *codec_dev_name;
	u32 asrc_fmt = 0;
	u32 width;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;

	cpu_np = of_parse_phandle(np, "audio-cpu", 0);
	/* Give a chance to old DT binding */
	if (!cpu_np)
		cpu_np = of_parse_phandle(np, "ssi-controller", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "CPU phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find CPU DAI device\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_np = of_parse_phandle(np, "audio-codec", 0);
	if (codec_np) {
		struct platform_device *codec_pdev;
		struct i2c_client *codec_i2c;

		codec_i2c = of_find_i2c_device_by_node(codec_np);
		if (codec_i2c) {
			codec_dev = &codec_i2c->dev;
			codec_dev_name = codec_i2c->name;
		}
		if (!codec_dev) {
			codec_pdev = of_find_device_by_node(codec_np);
			if (codec_pdev) {
				codec_dev = &codec_pdev->dev;
				codec_dev_name = codec_pdev->name;
			}
		}
	}

	asrc_np = of_parse_phandle(np, "audio-asrc", 0);
	if (asrc_np)
		asrc_pdev = of_find_device_by_node(asrc_np);

	/* Get the MCLK rate only, and leave it controlled by CODEC drivers */
	if (codec_dev) {
		struct clk *codec_clk = clk_get(codec_dev, NULL);

		if (!IS_ERR(codec_clk)) {
			priv->codec_priv.mclk_freq = clk_get_rate(codec_clk);
			clk_put(codec_clk);
		}
	}

	/* Default sample rate and format, will be updated in hw_params() */
	priv->sample_rate = 44100;
	priv->sample_format = SNDRV_PCM_FORMAT_S16_LE;

	/* Assign a default DAI format, and allow each card to overwrite it */
	priv->dai_fmt = DAI_FMT_BASE;

	memcpy(priv->dai_link, fsl_asoc_card_dai,
	       sizeof(struct snd_soc_dai_link) * ARRAY_SIZE(priv->dai_link));

	priv->card.dapm_routes = audio_map;
	priv->card.num_dapm_routes = ARRAY_SIZE(audio_map);
	/* Diversify the card configurations */
	if (of_device_is_compatible(np, "fsl,imx-audio-cs42888")) {
		codec_dai_name = "cs42888";
		priv->cpu_priv.sysclk_freq[TX] = priv->codec_priv.mclk_freq;
		priv->cpu_priv.sysclk_freq[RX] = priv->codec_priv.mclk_freq;
		priv->cpu_priv.sysclk_dir[TX] = SND_SOC_CLOCK_OUT;
		priv->cpu_priv.sysclk_dir[RX] = SND_SOC_CLOCK_OUT;
		priv->cpu_priv.slot_width = 32;
		priv->dai_fmt |= SND_SOC_DAIFMT_CBC_CFC;
	} else if (of_device_is_compatible(np, "fsl,imx-audio-cs427x")) {
		codec_dai_name = "cs4271-hifi";
		priv->codec_priv.mclk_id = CS427x_SYSCLK_MCLK;
		priv->dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
	} else if (of_device_is_compatible(np, "fsl,imx-audio-sgtl5000")) {
		codec_dai_name = "sgtl5000";
		priv->codec_priv.mclk_id = SGTL5000_SYSCLK;
		priv->dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
	} else if (of_device_is_compatible(np, "fsl,imx-audio-tlv320aic32x4")) {
		codec_dai_name = "tlv320aic32x4-hifi";
		priv->dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
	} else if (of_device_is_compatible(np, "fsl,imx-audio-tlv320aic31xx")) {
		codec_dai_name = "tlv320dac31xx-hifi";
		priv->dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
		priv->dai_link[1].dpcm_capture = 0;
		priv->dai_link[2].dpcm_capture = 0;
		priv->cpu_priv.sysclk_dir[TX] = SND_SOC_CLOCK_OUT;
		priv->cpu_priv.sysclk_dir[RX] = SND_SOC_CLOCK_OUT;
		priv->card.dapm_routes = audio_map_tx;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_tx);
	} else if (of_device_is_compatible(np, "fsl,imx-audio-wm8962")) {
		codec_dai_name = "wm8962";
		priv->codec_priv.mclk_id = WM8962_SYSCLK_MCLK;
		priv->codec_priv.fll_id = WM8962_SYSCLK_FLL;
		priv->codec_priv.pll_id = WM8962_FLL;
		priv->dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
	} else if (of_device_is_compatible(np, "fsl,imx-audio-wm8960")) {
		codec_dai_name = "wm8960-hifi";
		priv->codec_priv.fll_id = WM8960_SYSCLK_AUTO;
		priv->codec_priv.pll_id = WM8960_SYSCLK_AUTO;
		priv->dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
	} else if (of_device_is_compatible(np, "fsl,imx-audio-ac97")) {
		codec_dai_name = "ac97-hifi";
		priv->dai_fmt = SND_SOC_DAIFMT_AC97;
		priv->card.dapm_routes = audio_map_ac97;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_ac97);
	} else if (of_device_is_compatible(np, "fsl,imx-audio-mqs")) {
		codec_dai_name = "fsl-mqs-dai";
		priv->dai_fmt = SND_SOC_DAIFMT_LEFT_J |
				SND_SOC_DAIFMT_CBC_CFC |
				SND_SOC_DAIFMT_NB_NF;
		priv->dai_link[1].dpcm_capture = 0;
		priv->dai_link[2].dpcm_capture = 0;
		priv->card.dapm_routes = audio_map_tx;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_tx);
	} else if (of_device_is_compatible(np, "fsl,imx-audio-wm8524")) {
		codec_dai_name = "wm8524-hifi";
		priv->dai_fmt |= SND_SOC_DAIFMT_CBC_CFC;
		priv->dai_link[1].dpcm_capture = 0;
		priv->dai_link[2].dpcm_capture = 0;
		priv->cpu_priv.slot_width = 32;
		priv->card.dapm_routes = audio_map_tx;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_tx);
	} else if (of_device_is_compatible(np, "fsl,imx-audio-si476x")) {
		codec_dai_name = "si476x-codec";
		priv->dai_fmt |= SND_SOC_DAIFMT_CBC_CFC;
		priv->card.dapm_routes = audio_map_rx;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_rx);
	} else if (of_device_is_compatible(np, "fsl,imx-audio-wm8958")) {
		codec_dai_name = "wm8994-aif1";
		priv->dai_fmt |= SND_SOC_DAIFMT_CBP_CFP;
		priv->codec_priv.mclk_id = WM8994_FLL_SRC_MCLK1;
		priv->codec_priv.fll_id = WM8994_SYSCLK_FLL1;
		priv->codec_priv.pll_id = WM8994_FLL1;
		priv->codec_priv.free_freq = priv->codec_priv.mclk_freq;
		priv->card.dapm_routes = NULL;
		priv->card.num_dapm_routes = 0;
	} else {
		dev_err(&pdev->dev, "unknown Device Tree compatible\n");
		ret = -EINVAL;
		goto asrc_fail;
	}

	/*
	 * Allow setting mclk-id from the device-tree node. Otherwise, the
	 * default value for each card configuration is used.
	 */
	of_property_read_u32(np, "mclk-id", &priv->codec_priv.mclk_id);

	/* Format info from DT is optional. */
	snd_soc_daifmt_parse_clock_provider_as_phandle(np, NULL, &bitclkprovider, &frameprovider);
	if (bitclkprovider || frameprovider) {
		unsigned int daifmt = snd_soc_daifmt_parse_format(np, NULL);

		if (codec_np == bitclkprovider)
			daifmt |= (codec_np == frameprovider) ?
				SND_SOC_DAIFMT_CBP_CFP : SND_SOC_DAIFMT_CBP_CFC;
		else
			daifmt |= (codec_np == frameprovider) ?
				SND_SOC_DAIFMT_CBC_CFP : SND_SOC_DAIFMT_CBC_CFC;

		/* Override dai_fmt with value from DT */
		priv->dai_fmt = daifmt;
	}

	/* Change direction according to format */
	if (priv->dai_fmt & SND_SOC_DAIFMT_CBP_CFP) {
		priv->cpu_priv.sysclk_dir[TX] = SND_SOC_CLOCK_IN;
		priv->cpu_priv.sysclk_dir[RX] = SND_SOC_CLOCK_IN;
	}

	of_node_put(bitclkprovider);
	of_node_put(frameprovider);

	if (!fsl_asoc_card_is_ac97(priv) && !codec_dev) {
		dev_dbg(&pdev->dev, "failed to find codec device\n");
		ret = -EPROBE_DEFER;
		goto asrc_fail;
	}

	/* Common settings for corresponding Freescale CPU DAI driver */
	if (of_node_name_eq(cpu_np, "ssi")) {
		/* Only SSI needs to configure AUDMUX */
		ret = fsl_asoc_card_audmux_init(np, priv);
		if (ret) {
			dev_err(&pdev->dev, "failed to init audmux\n");
			goto asrc_fail;
		}
	} else if (of_node_name_eq(cpu_np, "esai")) {
		struct clk *esai_clk = clk_get(&cpu_pdev->dev, "extal");

		if (!IS_ERR(esai_clk)) {
			priv->cpu_priv.sysclk_freq[TX] = clk_get_rate(esai_clk);
			priv->cpu_priv.sysclk_freq[RX] = clk_get_rate(esai_clk);
			clk_put(esai_clk);
		} else if (PTR_ERR(esai_clk) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto asrc_fail;
		}

		priv->cpu_priv.sysclk_id[1] = ESAI_HCKT_EXTAL;
		priv->cpu_priv.sysclk_id[0] = ESAI_HCKR_EXTAL;
	} else if (of_node_name_eq(cpu_np, "sai")) {
		priv->cpu_priv.sysclk_id[1] = FSL_SAI_CLK_MAST1;
		priv->cpu_priv.sysclk_id[0] = FSL_SAI_CLK_MAST1;
	}

	/* Initialize sound card */
	priv->card.dev = &pdev->dev;
	priv->card.owner = THIS_MODULE;
	ret = snd_soc_of_parse_card_name(&priv->card, "model");
	if (ret) {
		snprintf(priv->name, sizeof(priv->name), "%s-audio",
			 fsl_asoc_card_is_ac97(priv) ? "ac97" : codec_dev_name);
		priv->card.name = priv->name;
	}
	priv->card.dai_link = priv->dai_link;
	priv->card.late_probe = fsl_asoc_card_late_probe;
	priv->card.dapm_widgets = fsl_asoc_card_dapm_widgets;
	priv->card.num_dapm_widgets = ARRAY_SIZE(fsl_asoc_card_dapm_widgets);

	/* Drop the second half of DAPM routes -- ASRC */
	if (!asrc_pdev)
		priv->card.num_dapm_routes /= 2;

	if (of_property_read_bool(np, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(&priv->card, "audio-routing");
		if (ret) {
			dev_err(&pdev->dev, "failed to parse audio-routing: %d\n", ret);
			goto asrc_fail;
		}
	}

	/* Normal DAI Link */
	priv->dai_link[0].cpus->of_node = cpu_np;
	priv->dai_link[0].codecs->dai_name = codec_dai_name;

	if (!fsl_asoc_card_is_ac97(priv))
		priv->dai_link[0].codecs->of_node = codec_np;
	else {
		u32 idx;

		ret = of_property_read_u32(cpu_np, "cell-index", &idx);
		if (ret) {
			dev_err(&pdev->dev,
				"cannot get CPU index property\n");
			goto asrc_fail;
		}

		priv->dai_link[0].codecs->name =
				devm_kasprintf(&pdev->dev, GFP_KERNEL,
					       "ac97-codec.%u",
					       (unsigned int)idx);
		if (!priv->dai_link[0].codecs->name) {
			ret = -ENOMEM;
			goto asrc_fail;
		}
	}

	priv->dai_link[0].platforms->of_node = cpu_np;
	priv->dai_link[0].dai_fmt = priv->dai_fmt;
	priv->card.num_links = 1;

	if (asrc_pdev) {
		/* DPCM DAI Links only if ASRC exsits */
		priv->dai_link[1].cpus->of_node = asrc_np;
		priv->dai_link[1].platforms->of_node = asrc_np;
		priv->dai_link[2].codecs->dai_name = codec_dai_name;
		priv->dai_link[2].codecs->of_node = codec_np;
		priv->dai_link[2].codecs->name =
				priv->dai_link[0].codecs->name;
		priv->dai_link[2].cpus->of_node = cpu_np;
		priv->dai_link[2].dai_fmt = priv->dai_fmt;
		priv->card.num_links = 3;

		ret = of_property_read_u32(asrc_np, "fsl,asrc-rate",
					   &priv->asrc_rate);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto asrc_fail;
		}

		ret = of_property_read_u32(asrc_np, "fsl,asrc-format", &asrc_fmt);
		priv->asrc_format = (__force snd_pcm_format_t)asrc_fmt;
		if (ret) {
			/* Fallback to old binding; translate to asrc_format */
			ret = of_property_read_u32(asrc_np, "fsl,asrc-width",
						   &width);
			if (ret) {
				dev_err(&pdev->dev,
					"failed to decide output format\n");
				goto asrc_fail;
			}

			if (width == 24)
				priv->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
			else
				priv->asrc_format = SNDRV_PCM_FORMAT_S16_LE;
		}
	}

	/* Finish card registering */
	platform_set_drvdata(pdev, priv);
	snd_soc_card_set_drvdata(&priv->card, priv);

	ret = devm_snd_soc_register_card(&pdev->dev, &priv->card);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "snd_soc_register_card failed\n");
		goto asrc_fail;
	}

	/*
	 * Properties "hp-det-gpio" and "mic-det-gpio" are optional, and
	 * asoc_simple_init_jack uses these properties for creating
	 * Headphone Jack and Microphone Jack.
	 *
	 * The notifier is initialized in snd_soc_card_jack_new(), then
	 * snd_soc_jack_notifier_register can be called.
	 */
	if (of_property_read_bool(np, "hp-det-gpio")) {
		ret = asoc_simple_init_jack(&priv->card, &priv->hp_jack,
					    1, NULL, "Headphone Jack");
		if (ret)
			goto asrc_fail;

		snd_soc_jack_notifier_register(&priv->hp_jack.jack, &hp_jack_nb);
	}

	if (of_property_read_bool(np, "mic-det-gpio")) {
		ret = asoc_simple_init_jack(&priv->card, &priv->mic_jack,
					    0, NULL, "Mic Jack");
		if (ret)
			goto asrc_fail;

		snd_soc_jack_notifier_register(&priv->mic_jack.jack, &mic_jack_nb);
	}

asrc_fail:
	of_node_put(asrc_np);
	of_node_put(codec_np);
	put_device(&cpu_pdev->dev);
fail:
	of_node_put(cpu_np);

	return ret;
}

static const struct of_device_id fsl_asoc_card_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-ac97", },
	{ .compatible = "fsl,imx-audio-cs42888", },
	{ .compatible = "fsl,imx-audio-cs427x", },
	{ .compatible = "fsl,imx-audio-tlv320aic32x4", },
	{ .compatible = "fsl,imx-audio-tlv320aic31xx", },
	{ .compatible = "fsl,imx-audio-sgtl5000", },
	{ .compatible = "fsl,imx-audio-wm8962", },
	{ .compatible = "fsl,imx-audio-wm8960", },
	{ .compatible = "fsl,imx-audio-mqs", },
	{ .compatible = "fsl,imx-audio-wm8524", },
	{ .compatible = "fsl,imx-audio-si476x", },
	{ .compatible = "fsl,imx-audio-wm8958", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_asoc_card_dt_ids);

static struct platform_driver fsl_asoc_card_driver = {
	.probe = fsl_asoc_card_probe,
	.driver = {
		.name = "fsl-asoc-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = fsl_asoc_card_dt_ids,
	},
};
module_platform_driver(fsl_asoc_card_driver);

MODULE_DESCRIPTION("Freescale Generic ASoC Sound Card driver with ASRC");
MODULE_AUTHOR("Nicolin Chen <nicoleotsuka@gmail.com>");
MODULE_ALIAS("platform:fsl-asoc-card");
MODULE_LICENSE("GPL");
