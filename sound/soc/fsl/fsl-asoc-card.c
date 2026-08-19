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
#include "../codecs/nau8822.h"
#include "../codecs/wm8904.h"

#define DRIVER_NAME "fsl-asoc-card"

#define CS427x_SYSCLK_MCLK 0

#define RX 0
#define TX 1

/* Default DAI format without Master and Slave flag */
#define DAI_FMT_BASE (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF)

static const u32 cs42888_rates_48k[] = {
	48000, 96000, 192000,
};

static const u32 cs42888_rates_44k[] = {
	44100, 88200, 176400,
};

static const u32 cs42888_channels[] = {
	1, 2, 4, 6, 8,
};

static const struct snd_pcm_hw_constraint_list cs42888_rate_48k_constraints = {
	.list = cs42888_rates_48k,
	.count = ARRAY_SIZE(cs42888_rates_48k),
};

static const struct snd_pcm_hw_constraint_list cs42888_rate_44k_constraints = {
	.list = cs42888_rates_44k,
	.count = ARRAY_SIZE(cs42888_rates_44k),
};

static const struct snd_pcm_hw_constraint_list cs42888_channel_constraints = {
	.list = cs42888_channels,
	.count = ARRAY_SIZE(cs42888_channels),
};

/**
 * struct codec_priv - CODEC private data
 * @mclk_freq: Clock rate of MCLK
 * @free_freq: Clock rate of MCLK for hw_free()
 * @mclk_id: MCLK (or main clock) id for set_sysclk()
 * @fll_id: FLL (or secordary clock) id for set_sysclk()
 * @pll_id: PLL id for set_pll()
 * @pll_ratio_s24: PLL output ratio for S24_LE format (PLL_freq = sample_rate × ratio)
 *                 Default is 384, but some codecs (e.g., WM8904) require lower values
 *                 to stay within PLL frequency limits
 */
struct codec_priv {
	unsigned long mclk_freq;
	unsigned long free_freq;
	u32 mclk_id;
	int fll_id;
	int pll_id;
	int pll_ratio_s24;
};

/**
 * struct cpu_priv - CPU private data
 * @sysclk_freq: SYSCLK rates for set_sysclk()
 * @sysclk_dir: SYSCLK directions for set_sysclk()
 * @sysclk_id: SYSCLK ids for set_sysclk()
 * @sysclk_ratio: SYSCLK ratio on sample rate
 * @slot_width: Slot width of each frame
 * @slot_num: Number of slots of each frame
 *
 * Note: [1] for tx and [0] for rx
 */
struct cpu_priv {
	unsigned long sysclk_freq[2];
	u32 sysclk_dir[2];
	u32 sysclk_id[2];
	u32 sysclk_ratio[2];
	u32 slot_width;
	u32 slot_num;
};

struct fsl_asoc_card_priv;

/*
 * struct fsl_asoc_card_pdata - per-compatible static card description
 * @sysclk_dir: initial CPU SYSCLK direction override (0 = leave default IN)
 * @sysclk_ratio: SYSCLK ratio on sample rate (0 = not used)
 * @slot_width: TDM slot width (0 = not TDM)
 * @codec_dai_name: name of the codec DAI
 * @codec_mclk_id: MCLK id passed to set_sysclk() for the codec
 * @codec_fll_id: FLL id; only valid when has_pll is true
 * @codec_pll_id: PLL id; only valid when has_pll is true
 * @codec_pll_ratio_s24: PLL output ratio for S24_LE
 * @has_pll: codec uses PLL/FLL; codec_fll_id and codec_pll_id are valid
 * @dai_fmt: DAI format flags
 * @playback_only: restrict card to playback direction
 * @capture_only: restrict card to capture direction
 * @dapm_routes: DAPM route table override
 * @num_dapm_routes: number of entries in dapm_routes
 * @exclude_format: PCM format bitmask excluded (for SAI + WM8960/WM8962)
 * @codec_init: codec-specific init run after mclk_freq is populated
 * @probe_init: optional DT-driven init run at end of probe() (e.g. SPDIF codec discovery)
 */
struct fsl_asoc_card_pdata {
	u32 sysclk_dir[2];
	u32 sysclk_ratio[2];
	u32 slot_width;
	const char *codec_dai_name;
	u32 codec_mclk_id;
	int codec_fll_id;
	int codec_pll_id;
	int codec_pll_ratio_s24;
	bool has_pll;
	bool playback_only;
	bool capture_only;
	u32 dai_fmt;
	const struct snd_soc_dapm_route *dapm_routes;
	int num_dapm_routes;
	u64 exclude_format;
	int (*codec_init)(struct fsl_asoc_card_priv *priv);
	int (*probe_init)(struct device_node *codec_np[],
			  struct device_node *cpu_np,
			  const char *codec_dai_name[],
			  struct fsl_asoc_card_priv *priv);
};

/**
 * struct fsl_asoc_card_priv - Freescale Generic ASOC card private data
 * @dai_link: DAI link structure including normal one and DPCM link
 * @hp_jack: Headphone Jack structure
 * @mic_jack: Microphone Jack structure
 * @pdev: platform device pointer
 * @pdata: pointer to the per-compatible card platform data
 * @codec_priv: CODEC private data
 * @cpu_priv: CPU private data
 * @card: ASoC card structure
 * @constraint_rates: array of supported rates
 * @constraint_channels: array of supported channels
 * @streams: Mask of current active streams
 * @sample_rate: Current sample rate
 * @sample_format: Current sample format
 * @asrc_rate: ASRC sample rate used by Back-Ends
 * @asrc_format: ASRC sample format used by Back-Ends
 * @dai_fmt: DAI format between CPU and CODEC
 * @exclude_format: excluded format;
 * @name: Card name
 */

struct fsl_asoc_card_priv {
	struct snd_soc_dai_link dai_link[3];
	struct simple_util_jack hp_jack;
	struct simple_util_jack mic_jack;
	struct platform_device *pdev;
	const struct fsl_asoc_card_pdata *pdata;
	struct codec_priv codec_priv[2];
	struct cpu_priv cpu_priv;
	struct snd_soc_card card;
	const struct snd_pcm_hw_constraint_list *constraint_rates;
	const struct snd_pcm_hw_constraint_list *constraint_channels;
	u8 streams;
	u32 sample_rate;
	snd_pcm_format_t sample_format;
	u32 asrc_rate;
	snd_pcm_format_t asrc_format;
	u32 dai_fmt;
	u64 exclude_format;
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
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct codec_priv *codec_priv;
	struct snd_soc_dai *codec_dai;
	struct cpu_priv *cpu_priv = &priv->cpu_priv;
	struct device *dev = rtd->card->dev;
	unsigned int pll_out, sysclk_freq;
	int codec_idx;
	int ret;

	priv->sample_rate = params_rate(params);
	priv->sample_format = params_format(params);
	priv->streams |= BIT(substream->stream);

	if (fsl_asoc_card_is_ac97(priv))
		return 0;

	if (!cpu_priv->sysclk_freq[tx] && cpu_priv->sysclk_ratio[tx])
		sysclk_freq = priv->sample_rate * cpu_priv->sysclk_ratio[tx];
	else
		sysclk_freq = cpu_priv->sysclk_freq[tx];

	/* Specific configurations of DAIs starts from here */
	ret = snd_soc_dai_set_sysclk(snd_soc_rtd_to_cpu(rtd, 0), cpu_priv->sysclk_id[tx],
				     sysclk_freq,
				     cpu_priv->sysclk_dir[tx]);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set sysclk for cpu dai\n");
		goto fail;
	}

	if (cpu_priv->slot_width) {
		if (!cpu_priv->slot_num)
			cpu_priv->slot_num = 2;

		ret = snd_soc_dai_set_tdm_slot(snd_soc_rtd_to_cpu(rtd, 0), 0x3, 0x3,
					       cpu_priv->slot_num,
					       cpu_priv->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set TDM slot for cpu dai\n");
			goto fail;
		}
	}

	/* Specific configuration for PLL */
	for_each_rtd_codec_dais(rtd, codec_idx, codec_dai) {
		codec_priv = &priv->codec_priv[codec_idx];

		if (codec_priv->pll_id >= 0 && codec_priv->fll_id >= 0) {
			if (priv->sample_format == SNDRV_PCM_FORMAT_S24_LE)
				pll_out = priv->sample_rate * codec_priv->pll_ratio_s24;
			else
				pll_out = priv->sample_rate * 256;

			ret = snd_soc_dai_set_pll(codec_dai,
						codec_priv->pll_id,
						codec_priv->mclk_id,
						codec_priv->mclk_freq, pll_out);
			if (ret) {
				dev_err(dev, "failed to start FLL: %d\n", ret);
				goto fail;
			}

			ret = snd_soc_dai_set_sysclk(codec_dai,
						codec_priv->fll_id,
						pll_out, SND_SOC_CLOCK_IN);

			if (ret && ret != -ENOTSUPP) {
				dev_err(dev, "failed to set SYSCLK: %d\n", ret);
				goto fail;
			}
		}
	}

	return 0;

fail:
	priv->streams &= ~BIT(substream->stream);
	return ret;
}

static int fsl_asoc_card_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct codec_priv *codec_priv;
	struct snd_soc_dai *codec_dai;
	struct device *dev = rtd->card->dev;
	int codec_idx;
	int ret;

	priv->streams &= ~BIT(substream->stream);

	for_each_rtd_codec_dais(rtd, codec_idx, codec_dai) {
		codec_priv = &priv->codec_priv[codec_idx];

		if (!priv->streams && codec_priv->pll_id >= 0 && codec_priv->fll_id >= 0) {
			/* Force freq to be free_freq to avoid error message in codec */
			ret = snd_soc_dai_set_sysclk(codec_dai,
						codec_priv->mclk_id,
						codec_priv->free_freq,
						SND_SOC_CLOCK_IN);
			if (ret) {
				dev_err(dev, "failed to switch away from FLL: %d\n", ret);
				return ret;
			}

			ret = snd_soc_dai_set_pll(codec_dai,
						codec_priv->pll_id, 0, 0, 0);
			if (ret && ret != -ENOTSUPP) {
				dev_err(dev, "failed to stop FLL: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int fsl_asoc_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	if (priv->exclude_format && !rtd->dai_link->no_pcm) {
		ret = snd_pcm_hw_constraint_mask64(runtime,
						   SNDRV_PCM_HW_PARAM_FORMAT,
						   ~priv->exclude_format);
		if (ret)
			return ret;
	}

	if (priv->constraint_channels) {
		ret = snd_pcm_hw_constraint_list(runtime, 0,
						 SNDRV_PCM_HW_PARAM_CHANNELS,
						 priv->constraint_channels);
		if (ret)
			return ret;
	}

	/*
	 * Apply rate constraints only to frontend DAI links (no_pcm = 0).
	 * Skip DPCM backend (no_pcm = 1) as rate is fixed by be_hw_params_fixup()
	 * and ASRC frontend handles rate conversion.
	 */
	if (priv->constraint_rates && !rtd->dai_link->no_pcm) {
		ret = snd_pcm_hw_constraint_list(runtime, 0,
						 SNDRV_PCM_HW_PARAM_RATE,
						 priv->constraint_rates);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct snd_soc_ops fsl_asoc_card_ops = {
	.startup = fsl_asoc_card_startup,
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

static const struct snd_soc_dai_link fsl_asoc_card_dai[] = {
	/* Default ASoC DAI Link*/
	{
		.name = "HiFi",
		.stream_name = "HiFi",
		.ops = &fsl_asoc_card_ops,
	},
	/* DPCM Link between Front-End and Back-End (Optional) */
	{
		.name = "HiFi-ASRC-FE",
		.stream_name = "HiFi-ASRC-FE",
		.dynamic = 1,
	},
	{
		.name = "HiFi-ASRC-BE",
		.stream_name = "HiFi-ASRC-BE",
		.be_hw_params_fixup = be_hw_params_fixup,
		.ops = &fsl_asoc_card_ops,
		.no_pcm = 1,
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

static int fsl_asoc_card_spdif_init(struct device_node *codec_np[],
				    struct device_node *cpu_np,
				    const char *codec_dai_name[],
				    struct fsl_asoc_card_priv *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct device_node *np = dev->of_node;

	if (!of_node_name_eq(cpu_np, "spdif")) {
		dev_err(dev, "CPU phandle invalid, should be an SPDIF device\n");
		return -EINVAL;
	}

	priv->dai_link[0].playback_only = true;
	priv->dai_link[0].capture_only = true;

	for (int i = 0; i < 2; i++) {
		if (!codec_np[i])
			break;

		if (of_device_is_compatible(codec_np[i], "linux,spdif-dit")) {
			priv->dai_link[0].capture_only = false;
			codec_dai_name[i] = "dit-hifi";
		} else if (of_device_is_compatible(codec_np[i], "linux,spdif-dir")) {
			priv->dai_link[0].playback_only = false;
			codec_dai_name[i] = "dir-hifi";
		}
	}

	// Old SPDIF DT binding
	if (!codec_np[0]) {
		codec_dai_name[0] = snd_soc_dummy_dlc.dai_name;
		if (of_property_read_bool(np, "spdif-out"))
			priv->dai_link[0].capture_only = false;
		if (of_property_read_bool(np, "spdif-in"))
			priv->dai_link[0].playback_only = false;
	}

	if (priv->dai_link[0].playback_only && priv->dai_link[0].capture_only) {
		dev_err(dev, "no enabled S/PDIF DAI link\n");
		return -EINVAL;
	}

	if (priv->dai_link[0].playback_only) {
		priv->dai_link[1].playback_only = true;
		priv->dai_link[2].playback_only = true;
		priv->card.dapm_routes = audio_map_tx;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_tx);
	} else if (priv->dai_link[0].capture_only) {
		priv->dai_link[1].capture_only = true;
		priv->dai_link[2].capture_only = true;
		priv->card.dapm_routes = audio_map_rx;
		priv->card.num_dapm_routes = ARRAY_SIZE(audio_map_rx);
	}

	// No DAPM routes with old bindings and dummy codec
	if (!codec_np[0]) {
		priv->card.dapm_routes = NULL;
		priv->card.num_dapm_routes = 0;
	}

	if (codec_np[0] && codec_np[1]) {
		priv->dai_link[0].num_codecs = 2;
		priv->dai_link[2].num_codecs = 2;
	}

	return 0;
}

static int fsl_asoc_card_cs42888_codec_init(struct fsl_asoc_card_priv *priv)
{
	unsigned long mclk_freq = priv->codec_priv[0].mclk_freq;

	/*
	 * Set CPU sysclk frequency from codec MCLK only if not already
	 * set by the CPU DAI init (e.g. ESAI extal clock takes precedence).
	 */
	if (!priv->cpu_priv.sysclk_freq[TX])
		priv->cpu_priv.sysclk_freq[TX] = mclk_freq;
	if (!priv->cpu_priv.sysclk_freq[RX])
		priv->cpu_priv.sysclk_freq[RX] = mclk_freq;

	priv->constraint_channels = &cs42888_channel_constraints;
	if (mclk_freq % 12288000 == 0)
		priv->constraint_rates = &cs42888_rate_48k_constraints;
	else if (mclk_freq % 11289600 == 0)
		priv->constraint_rates = &cs42888_rate_44k_constraints;
	else
		dev_warn(&priv->pdev->dev,
			 "Unknown MCLK frequency %lu, no rate constraints\n",
			 mclk_freq);

	return 0;
}

static int fsl_asoc_card_wm8958_codec_init(struct fsl_asoc_card_priv *priv)
{
	priv->codec_priv[0].free_freq = priv->codec_priv[0].mclk_freq;
	return 0;
}

static const struct fsl_asoc_card_pdata fsl_asoc_cs42888_pdata = {
	.codec_dai_name      = "cs42888",
	.dai_fmt             = DAI_FMT_BASE | SND_SOC_DAIFMT_CBC_CFC,
	.sysclk_dir          = { SND_SOC_CLOCK_OUT, SND_SOC_CLOCK_OUT },
	.slot_width          = 32,
	.dapm_routes         = audio_map,
	.num_dapm_routes     = ARRAY_SIZE(audio_map),
	.codec_init          = fsl_asoc_card_cs42888_codec_init,
};

static const struct fsl_asoc_card_pdata fsl_asoc_cs427x_pdata = {
	.codec_dai_name  = "cs4271-hifi",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.codec_mclk_id   = CS427x_SYSCLK_MCLK,
	.dapm_routes     = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_sgtl5000_pdata = {
	.codec_dai_name  = "sgtl5000",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.codec_mclk_id   = SGTL5000_SYSCLK,
	.dapm_routes     = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_tlv320aic32x4_pdata = {
	.codec_dai_name  = "tlv320aic32x4-hifi",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.dapm_routes     = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_tlv320aic31xx_pdata = {
	.codec_dai_name  = "tlv320dac31xx-hifi",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBC_CFC,
	.sysclk_dir      = { SND_SOC_CLOCK_OUT, SND_SOC_CLOCK_OUT },
	.playback_only   = true,
	.dapm_routes     = audio_map_tx,
	.num_dapm_routes = ARRAY_SIZE(audio_map_tx),
};

static const struct fsl_asoc_card_pdata fsl_asoc_wm8962_pdata = {
	.codec_dai_name      = "wm8962",
	.dai_fmt             = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.codec_mclk_id       = WM8962_SYSCLK_MCLK,
	.has_pll             = true,
	.codec_fll_id        = WM8962_SYSCLK_FLL,
	.codec_pll_id        = WM8962_FLL,
	/*
	 * WM8962 has same BCLK generation limitations as WM8960.
	 * See WM8960 section for detailed explanation.
	 */
	.exclude_format      = SNDRV_PCM_FMTBIT_S20_3LE,
	.dapm_routes         = audio_map,
	.num_dapm_routes     = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_wm8960_pdata = {
	.codec_dai_name      = "wm8960-hifi",
	.dai_fmt             = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.has_pll             = true,
	.codec_fll_id        = WM8960_SYSCLK_AUTO,
	.codec_pll_id        = WM8960_SYSCLK_AUTO,
	/*
	 * WM8960 in master mode cannot generate exact 1.92 MHz BCLK
	 * required for S20_3LE (48kHz x 2ch x 20bit). Closest available
	 * is 2.048 MHz (SYSCLK/6), which causes right channel corruption.
	 *
	 * In SAI master mode, SAI derive BCLK from MCLK using integer
	 * dividers only. S20_3LE requires non-integer divider ratios
	 * with standard MCLK frequencies. For example, 48kHz stereo
	 * needs 1.920 MHz BCLK, which requires a divider of 6.4 from
	 * 12.288 MHz MCLK (not an integer).
	 */
	.exclude_format      = SNDRV_PCM_FMTBIT_S20_3LE,
	.dapm_routes         = audio_map,
	.num_dapm_routes     = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_ac97_pdata = {
	.codec_dai_name  = "ac97-hifi",
	.dai_fmt         = SND_SOC_DAIFMT_AC97,
	.dapm_routes     = audio_map_ac97,
	.num_dapm_routes = ARRAY_SIZE(audio_map_ac97),
};

static const struct fsl_asoc_card_pdata fsl_asoc_mqs_pdata = {
	.codec_dai_name  = "fsl-mqs-dai",
	.dai_fmt         = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBC_CFC |
			   SND_SOC_DAIFMT_NB_NF,
	.playback_only   = true,
	.dapm_routes     = audio_map_tx,
	.num_dapm_routes = ARRAY_SIZE(audio_map_tx),
};

static const struct fsl_asoc_card_pdata fsl_asoc_wm8524_pdata = {
	.codec_dai_name  = "wm8524-hifi",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBC_CFC,
	/* RX=0, TX=1: set TX (index 1) to CLOCK_OUT, RX stays at default IN */
	.sysclk_dir      = { 0, SND_SOC_CLOCK_OUT },
	.sysclk_ratio    = { 0, 256 },
	.slot_width      = 32,
	.playback_only   = true,
	.dapm_routes     = audio_map_tx,
	.num_dapm_routes = ARRAY_SIZE(audio_map_tx),
};

static const struct fsl_asoc_card_pdata fsl_asoc_si476x_pdata = {
	.codec_dai_name  = "si476x-codec",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBC_CFC,
	.dapm_routes     = audio_map_rx,
	.num_dapm_routes = ARRAY_SIZE(audio_map_rx),
};

static const struct fsl_asoc_card_pdata fsl_asoc_wm8958_pdata = {
	.codec_dai_name  = "wm8994-aif1",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.codec_mclk_id   = WM8994_FLL_SRC_MCLK1,
	.has_pll         = true,
	.codec_fll_id    = WM8994_SYSCLK_FLL1,
	.codec_pll_id    = WM8994_FLL1,
	.codec_init      = fsl_asoc_card_wm8958_codec_init,
};

static const struct fsl_asoc_card_pdata fsl_asoc_nau8822_pdata = {
	.codec_dai_name  = "nau8822-hifi",
	.dai_fmt         = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.codec_mclk_id   = NAU8822_CLK_MCLK,
	.has_pll         = true,
	.codec_fll_id    = NAU8822_CLK_PLL,
	.codec_pll_id    = NAU8822_CLK_PLL,
	.dapm_routes     = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_wm8904_pdata = {
	.codec_dai_name      = "wm8904-hifi",
	.dai_fmt             = DAI_FMT_BASE | SND_SOC_DAIFMT_CBP_CFP,
	.codec_mclk_id       = WM8904_FLL_MCLK,
	.has_pll             = true,
	.codec_fll_id        = WM8904_CLK_FLL,
	.codec_pll_id        = WM8904_FLL_MCLK,
	.codec_pll_ratio_s24 = 192,
	.dapm_routes         = audio_map,
	.num_dapm_routes     = ARRAY_SIZE(audio_map),
};

static const struct fsl_asoc_card_pdata fsl_asoc_spdif_pdata = {
	.codec_dai_name = "spdif",
	.dai_fmt        = DAI_FMT_BASE,
	.probe_init     = fsl_asoc_card_spdif_init,
};

static int hp_jack_event(struct notifier_block *nb, unsigned long event,
			 void *data)
{
	struct snd_soc_jack *jack = (struct snd_soc_jack *)data;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(jack->card);

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
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(jack->card);

	if (event & SND_JACK_MICROPHONE)
		/* Disable dmic if microphone is plugged in */
		return snd_soc_dapm_disable_pin(dapm, "DMIC");
	else
		return snd_soc_dapm_enable_pin(dapm, "DMIC");
}

static struct notifier_block mic_jack_nb = {
	.notifier_call = mic_jack_event,
};

/*
 * fsl_asoc_card_init_cpu - configure CPU DAI-specific settings.
 *
 * Called from late_probe() when the CPU DAI component is guaranteed bound.
 */
static int fsl_asoc_card_init_cpu(struct snd_soc_card *card,
				  struct snd_soc_pcm_runtime *rtd)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(card);
	struct device_node *np = priv->pdev->dev.of_node;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	const char *comp_drv_name = cpu_dai->component->driver->name;
	struct device *dev = card->dev;
	int ret;

	if (!strcmp(comp_drv_name, "fsl-ssi")) {
		/* Only SSI needs to configure AUDMUX */
		ret = fsl_asoc_card_audmux_init(np, priv);
		if (ret) {
			dev_err(dev, "failed to init audmux\n");
			return ret;
		}
	} else if (!strcmp(comp_drv_name, "fsl-esai")) {
		struct clk *esai_clk = clk_get(cpu_dai->dev, "extal");

		if (!IS_ERR(esai_clk)) {
			priv->cpu_priv.sysclk_freq[TX] = clk_get_rate(esai_clk);
			priv->cpu_priv.sysclk_freq[RX] = clk_get_rate(esai_clk);
			clk_put(esai_clk);
		} else {
			dev_warn(dev, "failed to get ESAI extal clock: %ld\n", PTR_ERR(esai_clk));
		}

		priv->cpu_priv.sysclk_id[TX] = ESAI_HCKT_EXTAL;
		priv->cpu_priv.sysclk_id[RX] = ESAI_HCKR_EXTAL;
	} else if (!strcmp(comp_drv_name, "fsl-sai")) {
		priv->cpu_priv.sysclk_id[TX] = FSL_SAI_CLK_MAST1;
		priv->cpu_priv.sysclk_id[RX] = FSL_SAI_CLK_MAST1;

		if (priv->pdata->exclude_format)
			priv->exclude_format = priv->pdata->exclude_format;
	}

	return 0;
}

/*
 * fsl_asoc_card_init_codecs - read codec MCLK rates and set codec sysclk.
 *
 * Called from late_probe() after all components are bound.
 */
static int fsl_asoc_card_init_codecs(struct snd_soc_card *card,
				     struct snd_soc_pcm_runtime *rtd)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(card);
	const struct fsl_asoc_card_pdata *pdata = priv->pdata;
	struct snd_soc_dai *codec_dai;
	struct codec_priv *codec_priv;
	struct device *dev = card->dev;
	int codec_idx;
	int ret;

	/* Read MCLK rate from each bound codec component */
	for_each_rtd_codec_dais(rtd, codec_idx, codec_dai) {
		struct clk *codec_clk = clk_get(codec_dai->component->dev, NULL);

		codec_priv = &priv->codec_priv[codec_idx];
		if (!IS_ERR(codec_clk)) {
			codec_priv->mclk_freq = clk_get_rate(codec_clk);
			clk_put(codec_clk);
		}
	}

	if (pdata->codec_init) {
		ret = pdata->codec_init(priv);
		if (ret)
			return ret;
	}

	for_each_rtd_codec_dais(rtd, codec_idx, codec_dai) {
		codec_priv = &priv->codec_priv[codec_idx];

		ret = snd_soc_dai_set_sysclk(codec_dai, codec_priv->mclk_id,
					     codec_priv->mclk_freq, SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set sysclk in %s\n", __func__);
			return ret;
		}
	}

	return 0;
}

static void fsl_asoc_card_free_jack(struct snd_soc_card *card)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(card);

	if (priv->hp_jack.gpio.desc) {
		snd_soc_jack_notifier_unregister(&priv->hp_jack.jack, &hp_jack_nb);
		snd_soc_jack_free_gpios(&priv->hp_jack.jack, 1, &priv->hp_jack.gpio);
		priv->hp_jack.gpio.desc = NULL;
	}

	if (priv->mic_jack.gpio.desc) {
		snd_soc_jack_notifier_unregister(&priv->mic_jack.jack, &mic_jack_nb);
		snd_soc_jack_free_gpios(&priv->mic_jack.jack, 1, &priv->mic_jack.gpio);
		priv->mic_jack.gpio.desc = NULL;
	}
}

/*
 * fsl_asoc_card_init_jack - register optional headphone and mic jacks.
 *
 * Called from late_probe() once per card bind cycle.
 */
static int fsl_asoc_card_init_jack(struct snd_soc_card *card)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(card);
	struct device_node *np = priv->pdev->dev.of_node;
	int ret;

	/*
	 * Properties "hp-det-gpios" and "mic-det-gpios" are optional.
	 * simple_util_init_jack() checks for the GPIO property and
	 * does nothing if it is absent.
	 */
	if (of_property_present(np, "hp-det-gpios") ||
	    of_property_present(np, "hp-det-gpio") /* deprecated */) {
		ret = simple_util_init_jack(card, &priv->hp_jack,
					    1, NULL, "Headphone Jack");
		if (ret)
			return ret;

		snd_soc_jack_notifier_register(&priv->hp_jack.jack, &hp_jack_nb);
	}

	if (of_property_present(np, "mic-det-gpios") ||
	    of_property_present(np, "mic-det-gpio") /* deprecated */) {
		ret = simple_util_init_jack(card, &priv->mic_jack,
					    0, NULL, "Mic Jack");
		if (ret)
			return ret;

		snd_soc_jack_notifier_register(&priv->mic_jack.jack, &mic_jack_nb);
	}

	return 0;
}

static int fsl_asoc_card_late_probe(struct snd_soc_card *card)
{
	struct fsl_asoc_card_priv *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_pcm_runtime *rtd;
	int ret;

	/* Use the first rtd which carries the CPU+codec DAIs */
	rtd = list_first_entry(&card->rtd_list,
			       struct snd_soc_pcm_runtime, list);

	ret = fsl_asoc_card_init_jack(card);
	if (ret)
		goto jack_fail;

	ret = fsl_asoc_card_init_cpu(card, rtd);
	if (ret)
		goto jack_fail;

	if (fsl_asoc_card_is_ac97(priv)) {
#if IS_ENABLED(CONFIG_SND_AC97_CODEC)
		struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
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

	ret = fsl_asoc_card_init_codecs(card, rtd);
	if (ret)
		goto jack_fail;

	return 0;

jack_fail:
	fsl_asoc_card_free_jack(card);
	return ret;
}

static int fsl_asoc_card_card_remove(struct snd_soc_card *card)
{
	fsl_asoc_card_free_jack(card);

	return 0;
}

static int fsl_asoc_card_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np, *asrc_np;
	struct snd_soc_dai_link_component *codec_comp;
	struct device_node *codec_np[2];
	struct device_node *np = pdev->dev.of_node;
	struct platform_device *asrc_pdev = NULL;
	struct device_node *bitclkprovider = NULL;
	struct device_node *frameprovider = NULL;
	struct fsl_asoc_card_priv *priv;
	const struct fsl_asoc_card_pdata *pdata;
	struct snd_soc_dai_link_component *dlc;
	const char *codec_dai_name[2] = { NULL, NULL };
	u32 asrc_fmt = 0;
	int codec_idx;
	u32 width;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;

	pdata = of_device_get_match_data(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "unknown Device Tree compatible\n");
		return -EINVAL;
	}
	priv->pdata = pdata;

	cpu_np = of_parse_phandle(np, "audio-cpu", 0);
	/* Give a chance to old DT bindings */
	if (!cpu_np)
		cpu_np = of_parse_phandle(np, "ssi-controller", 0);
	if (!cpu_np)
		cpu_np = of_parse_phandle(np, "spdif-controller", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "CPU phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_np[0] = of_parse_phandle(np, "audio-codec", 0);
	codec_np[1] = of_parse_phandle(np, "audio-codec", 1);

	asrc_np = of_parse_phandle(np, "audio-asrc", 0);
	if (asrc_np)
		asrc_pdev = of_find_device_by_node(asrc_np);

	/* Default sample rate and format, will be updated in hw_params() */
	priv->sample_rate = 44100;
	priv->sample_format = SNDRV_PCM_FORMAT_S16_LE;

	/* Assign a default DAI format, and allow each card to overwrite it */
	priv->dai_fmt = DAI_FMT_BASE;

	memcpy(priv->dai_link, fsl_asoc_card_dai,
	       sizeof(struct snd_soc_dai_link) * ARRAY_SIZE(priv->dai_link));
	/*
	 * "Default ASoC DAI Link": 1 cpus, 2 codecs, 1 platforms
	 * "DPCM Link Front-End":  1 cpus, 1 codecs (dummy), 1 platforms
	 * "DPCM Link Back-End": 1 cpus, 2 codecs
	 * totally 10 components
	 */
	dlc = devm_kcalloc(&pdev->dev, 10, sizeof(*dlc), GFP_KERNEL);
	if (!dlc) {
		ret = -ENOMEM;
		goto asrc_fail;
	}

	priv->dai_link[0].cpus = &dlc[0];
	priv->dai_link[0].num_cpus = 1;
	priv->dai_link[0].codecs = &dlc[1];
	priv->dai_link[0].num_codecs = 1;
	priv->dai_link[0].platforms = &dlc[3];
	priv->dai_link[0].num_platforms = 1;

	priv->dai_link[1].cpus = &dlc[4];
	priv->dai_link[1].num_cpus = 1;
	priv->dai_link[1].codecs = &dlc[5];
	priv->dai_link[1].num_codecs = 0; /* dummy */
	priv->dai_link[1].platforms = &dlc[6];
	priv->dai_link[1].num_platforms = 1;

	priv->dai_link[2].cpus = &dlc[7];
	priv->dai_link[2].num_cpus = 1;
	priv->dai_link[2].codecs = &dlc[8];
	priv->dai_link[2].num_codecs = 1;

	priv->card.dapm_routes = audio_map;
	priv->card.num_dapm_routes = ARRAY_SIZE(audio_map);
	priv->card.driver_name = DRIVER_NAME;

	for (codec_idx = 0; codec_idx < 2; codec_idx++) {
		priv->codec_priv[codec_idx].fll_id = -1;
		priv->codec_priv[codec_idx].pll_id = -1;
		priv->codec_priv[codec_idx].pll_ratio_s24 = 384;
	}

	/* Diversify the card configurations */
	priv->cpu_priv.sysclk_dir[TX] = pdata->sysclk_dir[TX];
	priv->cpu_priv.sysclk_dir[RX] = pdata->sysclk_dir[RX];
	priv->cpu_priv.sysclk_ratio[TX] = pdata->sysclk_ratio[TX];
	priv->cpu_priv.sysclk_ratio[RX] = pdata->sysclk_ratio[RX];
	priv->cpu_priv.slot_width = pdata->slot_width;

	codec_dai_name[0] = pdata->codec_dai_name;
	priv->codec_priv[0].mclk_id = pdata->codec_mclk_id;
	if (pdata->has_pll) {
		priv->codec_priv[0].fll_id = pdata->codec_fll_id;
		priv->codec_priv[0].pll_id = pdata->codec_pll_id;
	}
	if (pdata->codec_pll_ratio_s24)
		priv->codec_priv[0].pll_ratio_s24 = pdata->codec_pll_ratio_s24;

	if (pdata->playback_only) {
		priv->dai_link[1].playback_only = 1;
		priv->dai_link[2].playback_only = 1;
	}
	if (pdata->capture_only) {
		priv->dai_link[1].capture_only = 1;
		priv->dai_link[2].capture_only = 1;
	}

	priv->dai_fmt = pdata->dai_fmt;

	priv->card.dapm_routes = pdata->dapm_routes;
	priv->card.num_dapm_routes = pdata->num_dapm_routes;

	if (pdata->probe_init) {
		ret = pdata->probe_init(codec_np, cpu_np,
					codec_dai_name, priv);
		if (ret)
			goto asrc_fail;
	}

	/*
	 * Allow setting mclk-id from the device-tree node. Otherwise, the
	 * default value for each card configuration is used.
	 */
	for_each_link_codecs((&(priv->dai_link[0])), codec_idx, codec_comp) {
		of_property_read_u32_index(np, "mclk-id", codec_idx,
					&priv->codec_priv[codec_idx].mclk_id);
	}

	/* Format info from DT is optional. */
	snd_soc_daifmt_parse_clock_provider_as_phandle(np, NULL, &bitclkprovider, &frameprovider);
	if (bitclkprovider || frameprovider) {
		unsigned int daifmt = snd_soc_daifmt_parse_format(np, NULL);
		bool codec_bitclkprovider = false;
		bool codec_frameprovider = false;

		for_each_link_codecs((&(priv->dai_link[0])), codec_idx, codec_comp) {
			if (bitclkprovider && codec_np[codec_idx] == bitclkprovider)
				codec_bitclkprovider = true;
			if (frameprovider && codec_np[codec_idx] == frameprovider)
				codec_frameprovider = true;
		}

		if (codec_bitclkprovider)
			daifmt |= (codec_frameprovider) ?
				SND_SOC_DAIFMT_CBP_CFP : SND_SOC_DAIFMT_CBP_CFC;
		else
			daifmt |= (codec_frameprovider) ?
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

	/* Initialize sound card */
	priv->card.dev = &pdev->dev;
	priv->card.owner = THIS_MODULE;
	ret = snd_soc_of_parse_card_name(&priv->card, "model");
	if (ret) {
		/*
		 * "model" is required by the DT binding. Enforce it here so
		 * the driver fails with a clear message.
		 */
		dev_err(&pdev->dev, "Error parsing card name: %d\n", ret);
		goto asrc_fail;
	}
	priv->card.dai_link = priv->dai_link;
	priv->card.late_probe = fsl_asoc_card_late_probe;
	priv->card.remove = fsl_asoc_card_card_remove;
	priv->card.dapm_widgets = fsl_asoc_card_dapm_widgets;
	priv->card.num_dapm_widgets = ARRAY_SIZE(fsl_asoc_card_dapm_widgets);

	/* Drop the second half of DAPM routes -- ASRC */
	if (!asrc_pdev)
		priv->card.num_dapm_routes /= 2;

	if (of_property_present(np, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(&priv->card, "audio-routing");
		if (ret) {
			dev_err(&pdev->dev, "failed to parse audio-routing: %d\n", ret);
			goto asrc_fail;
		}
	}

	/* Normal DAI Link */
	priv->dai_link[0].cpus->of_node = cpu_np;
	for_each_link_codecs((&(priv->dai_link[0])), codec_idx, codec_comp) {
		codec_comp->dai_name = codec_dai_name[codec_idx];
	}

	// Old SPDIF DT binding support
	if (codec_dai_name[0] == snd_soc_dummy_dlc.dai_name)
		priv->dai_link[0].codecs[0].name = snd_soc_dummy_dlc.name;

	if (!fsl_asoc_card_is_ac97(priv)) {
		for_each_link_codecs((&(priv->dai_link[0])), codec_idx, codec_comp) {
			codec_comp->of_node = codec_np[codec_idx];
		}
	} else {
		u32 idx;

		ret = of_property_read_u32(cpu_np, "cell-index", &idx);
		if (ret) {
			dev_err(&pdev->dev,
				"cannot get CPU index property\n");
			goto asrc_fail;
		}

		priv->dai_link[0].codecs[0].name =
				devm_kasprintf(&pdev->dev, GFP_KERNEL,
					       "ac97-codec.%u",
					       (unsigned int)idx);
		if (!priv->dai_link[0].codecs[0].name) {
			ret = -ENOMEM;
			goto asrc_fail;
		}
	}

	priv->dai_link[0].platforms->of_node = cpu_np;
	priv->dai_link[0].dai_fmt = priv->dai_fmt;
	priv->card.num_links = 1;

	if (asrc_pdev) {
		/* DPCM DAI Links only if ASRC exists */
		priv->dai_link[1].dpcm_merged_chan = 1;
		priv->dai_link[1].ignore_pmdown_time = 1;
		priv->dai_link[1].cpus->of_node = asrc_np;
		priv->dai_link[1].platforms->of_node = asrc_np;
		for_each_link_codecs((&(priv->dai_link[2])), codec_idx, codec_comp) {
			codec_comp->dai_name = priv->dai_link[0].codecs[codec_idx].dai_name;
			codec_comp->of_node = priv->dai_link[0].codecs[codec_idx].of_node;
			codec_comp->name = priv->dai_link[0].codecs[codec_idx].name;
		}
		priv->dai_link[2].cpus->of_node = cpu_np;
		priv->dai_link[2].dai_fmt = priv->dai_fmt;
		priv->dai_link[2].ignore_pmdown_time = 1;
		priv->card.num_links = 3;

		ret = of_property_read_u32(asrc_np, "fsl,asrc-rate",
					   &priv->asrc_rate);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto asrc_fail;
		}

		ret = of_property_read_u32(asrc_np, "fsl,asrc-format", &asrc_fmt);
		priv->asrc_format = asrc_fmt;
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

asrc_fail:
	of_node_put(asrc_np);
	of_node_put(codec_np[0]);
	of_node_put(codec_np[1]);
fail:
	of_node_put(cpu_np);

	return ret;
}

static const struct of_device_id fsl_asoc_card_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-ac97",           .data = &fsl_asoc_ac97_pdata },
	{ .compatible = "fsl,imx-audio-cs42888",        .data = &fsl_asoc_cs42888_pdata },
	{ .compatible = "fsl,imx-audio-cs427x",         .data = &fsl_asoc_cs427x_pdata },
	{ .compatible = "fsl,imx-audio-tlv320aic32x4",  .data = &fsl_asoc_tlv320aic32x4_pdata },
	{ .compatible = "fsl,imx-audio-tlv320aic31xx",  .data = &fsl_asoc_tlv320aic31xx_pdata },
	{ .compatible = "fsl,imx-audio-sgtl5000",       .data = &fsl_asoc_sgtl5000_pdata },
	{ .compatible = "fsl,imx-audio-wm8962",         .data = &fsl_asoc_wm8962_pdata },
	{ .compatible = "fsl,imx-audio-wm8960",         .data = &fsl_asoc_wm8960_pdata },
	{ .compatible = "fsl,imx-audio-mqs",            .data = &fsl_asoc_mqs_pdata },
	{ .compatible = "fsl,imx-audio-wm8524",         .data = &fsl_asoc_wm8524_pdata },
	{ .compatible = "fsl,imx-audio-si476x",         .data = &fsl_asoc_si476x_pdata },
	{ .compatible = "fsl,imx-audio-wm8958",         .data = &fsl_asoc_wm8958_pdata },
	{ .compatible = "fsl,imx-audio-nau8822",        .data = &fsl_asoc_nau8822_pdata },
	{ .compatible = "fsl,imx-audio-wm8904",         .data = &fsl_asoc_wm8904_pdata },
	{ .compatible = "fsl,imx-audio-spdif",          .data = &fsl_asoc_spdif_pdata },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_asoc_card_dt_ids);

static struct platform_driver fsl_asoc_card_driver = {
	.probe = fsl_asoc_card_probe,
	.driver = {
		.name = DRIVER_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = fsl_asoc_card_dt_ids,
	},
};
module_platform_driver(fsl_asoc_card_driver);

MODULE_DESCRIPTION("Freescale Generic ASoC Sound Card driver with ASRC");
MODULE_AUTHOR("Nicolin Chen <nicoleotsuka@gmail.com>");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");
