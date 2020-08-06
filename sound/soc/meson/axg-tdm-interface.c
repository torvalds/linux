// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "axg-tdm.h"

enum {
	TDM_IFACE_PAD,
	TDM_IFACE_LOOPBACK,
};

static unsigned int axg_tdm_slots_total(u32 *mask)
{
	unsigned int slots = 0;
	int i;

	if (!mask)
		return 0;

	/* Count the total number of slots provided by all 4 lanes */
	for (i = 0; i < AXG_TDM_NUM_LANES; i++)
		slots += hweight32(mask[i]);

	return slots;
}

int axg_tdm_set_tdm_slots(struct snd_soc_dai *dai, u32 *tx_mask,
			  u32 *rx_mask, unsigned int slots,
			  unsigned int slot_width)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	struct axg_tdm_stream *tx = (struct axg_tdm_stream *)
		dai->playback_dma_data;
	struct axg_tdm_stream *rx = (struct axg_tdm_stream *)
		dai->capture_dma_data;
	unsigned int tx_slots, rx_slots;
	unsigned int fmt = 0;

	tx_slots = axg_tdm_slots_total(tx_mask);
	rx_slots = axg_tdm_slots_total(rx_mask);

	/* We should at least have a slot for a valid interface */
	if (!tx_slots && !rx_slots) {
		dev_err(dai->dev, "interface has no slot\n");
		return -EINVAL;
	}

	iface->slots = slots;

	switch (slot_width) {
	case 0:
		slot_width = 32;
		/* Fall-through */
	case 32:
		fmt |= SNDRV_PCM_FMTBIT_S32_LE;
		/* Fall-through */
	case 24:
		fmt |= SNDRV_PCM_FMTBIT_S24_LE;
		fmt |= SNDRV_PCM_FMTBIT_S20_LE;
		/* Fall-through */
	case 16:
		fmt |= SNDRV_PCM_FMTBIT_S16_LE;
		/* Fall-through */
	case 8:
		fmt |= SNDRV_PCM_FMTBIT_S8;
		break;
	default:
		dev_err(dai->dev, "unsupported slot width: %d\n", slot_width);
		return -EINVAL;
	}

	iface->slot_width = slot_width;

	/* Amend the dai driver and let dpcm merge do its job */
	if (tx) {
		tx->mask = tx_mask;
		dai->driver->playback.channels_max = tx_slots;
		dai->driver->playback.formats = fmt;
	}

	if (rx) {
		rx->mask = rx_mask;
		dai->driver->capture.channels_max = rx_slots;
		dai->driver->capture.formats = fmt;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(axg_tdm_set_tdm_slots);

static int axg_tdm_iface_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				    unsigned int freq, int dir)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	int ret = -ENOTSUPP;

	if (dir == SND_SOC_CLOCK_OUT && clk_id == 0) {
		if (!iface->mclk) {
			dev_warn(dai->dev, "master clock not provided\n");
		} else {
			ret = clk_set_rate(iface->mclk, freq);
			if (!ret)
				iface->mclk_rate = freq;
		}
	}

	return ret;
}

static int axg_tdm_iface_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		if (!iface->mclk) {
			dev_err(dai->dev, "cpu clock master: mclk missing\n");
			return -ENODEV;
		}
		break;

	case SND_SOC_DAIFMT_CBM_CFM:
		break;

	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
		dev_err(dai->dev, "only CBS_CFS and CBM_CFM are supported\n");
		/* Fall-through */
	default:
		return -EINVAL;
	}

	iface->fmt = fmt;
	return 0;
}

static int axg_tdm_iface_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	struct axg_tdm_stream *ts =
		snd_soc_dai_get_dma_data(dai, substream);
	int ret;

	if (!axg_tdm_slots_total(ts->mask)) {
		dev_err(dai->dev, "interface has not slots\n");
		return -EINVAL;
	}

	/* Apply component wide rate symmetry */
	if (snd_soc_component_active(dai->component)) {
		ret = snd_pcm_hw_constraint_single(substream->runtime,
						   SNDRV_PCM_HW_PARAM_RATE,
						   iface->rate);
		if (ret < 0) {
			dev_err(dai->dev,
				"can't set iface rate constraint\n");
			return ret;
		}
	}

	return 0;
}

static int axg_tdm_iface_set_stream(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	struct axg_tdm_stream *ts = snd_soc_dai_get_dma_data(dai, substream);
	unsigned int channels = params_channels(params);
	unsigned int width = params_width(params);

	/* Save rate and sample_bits for component symmetry */
	iface->rate = params_rate(params);

	/* Make sure this interface can cope with the stream */
	if (axg_tdm_slots_total(ts->mask) < channels) {
		dev_err(dai->dev, "not enough slots for channels\n");
		return -EINVAL;
	}

	if (iface->slot_width < width) {
		dev_err(dai->dev, "incompatible slots width for stream\n");
		return -EINVAL;
	}

	/* Save the parameter for tdmout/tdmin widgets */
	ts->physical_width = params_physical_width(params);
	ts->width = params_width(params);
	ts->channels = params_channels(params);

	return 0;
}

static int axg_tdm_iface_set_lrclk(struct snd_soc_dai *dai,
				   struct snd_pcm_hw_params *params)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	unsigned int ratio_num;
	int ret;

	ret = clk_set_rate(iface->lrclk, params_rate(params));
	if (ret) {
		dev_err(dai->dev, "setting sample clock failed: %d\n", ret);
		return ret;
	}

	switch (iface->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		/* 50% duty cycle ratio */
		ratio_num = 1;
		break;

	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/*
		 * A zero duty cycle ratio will result in setting the mininum
		 * ratio possible which, for this clock, is 1 cycle of the
		 * parent bclk clock high and the rest low, This is exactly
		 * what we want here.
		 */
		ratio_num = 0;
		break;

	default:
		return -EINVAL;
	}

	ret = clk_set_duty_cycle(iface->lrclk, ratio_num, 2);
	if (ret) {
		dev_err(dai->dev,
			"setting sample clock duty cycle failed: %d\n", ret);
		return ret;
	}

	/* Set sample clock inversion */
	ret = clk_set_phase(iface->lrclk,
			    axg_tdm_lrclk_invert(iface->fmt) ? 180 : 0);
	if (ret) {
		dev_err(dai->dev,
			"setting sample clock phase failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int axg_tdm_iface_set_sclk(struct snd_soc_dai *dai,
				  struct snd_pcm_hw_params *params)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	unsigned long srate;
	int ret;

	srate = iface->slots * iface->slot_width * params_rate(params);

	if (!iface->mclk_rate) {
		/* If no specific mclk is requested, default to bit clock * 4 */
		clk_set_rate(iface->mclk, 4 * srate);
	} else {
		/* Check if we can actually get the bit clock from mclk */
		if (iface->mclk_rate % srate) {
			dev_err(dai->dev,
				"can't derive sclk %lu from mclk %lu\n",
				srate, iface->mclk_rate);
			return -EINVAL;
		}
	}

	ret = clk_set_rate(iface->sclk, srate);
	if (ret) {
		dev_err(dai->dev, "setting bit clock failed: %d\n", ret);
		return ret;
	}

	/* Set the bit clock inversion */
	ret = clk_set_phase(iface->sclk,
			    axg_tdm_sclk_invert(iface->fmt) ? 0 : 180);
	if (ret) {
		dev_err(dai->dev, "setting bit clock phase failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static int axg_tdm_iface_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);
	int ret;

	switch (iface->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		if (iface->slots > 2) {
			dev_err(dai->dev, "bad slot number for format: %d\n",
				iface->slots);
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		break;

	default:
		dev_err(dai->dev, "unsupported dai format\n");
		return -EINVAL;
	}

	ret = axg_tdm_iface_set_stream(substream, params, dai);
	if (ret)
		return ret;

	if ((iface->fmt & SND_SOC_DAIFMT_MASTER_MASK) ==
	    SND_SOC_DAIFMT_CBS_CFS) {
		ret = axg_tdm_iface_set_sclk(dai, params);
		if (ret)
			return ret;

		ret = axg_tdm_iface_set_lrclk(dai, params);
		if (ret)
			return ret;
	}

	return 0;
}

static int axg_tdm_iface_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_tdm_stream *ts = snd_soc_dai_get_dma_data(dai, substream);

	/* Stop all attached formatters */
	axg_tdm_stream_stop(ts);

	return 0;
}

static int axg_tdm_iface_prepare(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_tdm_stream *ts = snd_soc_dai_get_dma_data(dai, substream);

	/* Force all attached formatters to update */
	return axg_tdm_stream_reset(ts);
}

static int axg_tdm_iface_remove_dai(struct snd_soc_dai *dai)
{
	if (dai->capture_dma_data)
		axg_tdm_stream_free(dai->capture_dma_data);

	if (dai->playback_dma_data)
		axg_tdm_stream_free(dai->playback_dma_data);

	return 0;
}

static int axg_tdm_iface_probe_dai(struct snd_soc_dai *dai)
{
	struct axg_tdm_iface *iface = snd_soc_dai_get_drvdata(dai);

	if (dai->capture_widget) {
		dai->capture_dma_data = axg_tdm_stream_alloc(iface);
		if (!dai->capture_dma_data)
			return -ENOMEM;
	}

	if (dai->playback_widget) {
		dai->playback_dma_data = axg_tdm_stream_alloc(iface);
		if (!dai->playback_dma_data) {
			axg_tdm_iface_remove_dai(dai);
			return -ENOMEM;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops axg_tdm_iface_ops = {
	.set_sysclk	= axg_tdm_iface_set_sysclk,
	.set_fmt	= axg_tdm_iface_set_fmt,
	.startup	= axg_tdm_iface_startup,
	.hw_params	= axg_tdm_iface_hw_params,
	.prepare	= axg_tdm_iface_prepare,
	.hw_free	= axg_tdm_iface_hw_free,
};

/* TDM Backend DAIs */
static const struct snd_soc_dai_driver axg_tdm_iface_dai_drv[] = {
	[TDM_IFACE_PAD] = {
		.name = "TDM Pad",
		.playback = {
			.stream_name	= "Playback",
			.channels_min	= 1,
			.channels_max	= AXG_TDM_CHANNEL_MAX,
			.rates		= AXG_TDM_RATES,
			.formats	= AXG_TDM_FORMATS,
		},
		.capture = {
			.stream_name	= "Capture",
			.channels_min	= 1,
			.channels_max	= AXG_TDM_CHANNEL_MAX,
			.rates		= AXG_TDM_RATES,
			.formats	= AXG_TDM_FORMATS,
		},
		.id = TDM_IFACE_PAD,
		.ops = &axg_tdm_iface_ops,
		.probe = axg_tdm_iface_probe_dai,
		.remove = axg_tdm_iface_remove_dai,
	},
	[TDM_IFACE_LOOPBACK] = {
		.name = "TDM Loopback",
		.capture = {
			.stream_name	= "Loopback",
			.channels_min	= 1,
			.channels_max	= AXG_TDM_CHANNEL_MAX,
			.rates		= AXG_TDM_RATES,
			.formats	= AXG_TDM_FORMATS,
		},
		.id = TDM_IFACE_LOOPBACK,
		.ops = &axg_tdm_iface_ops,
		.probe = axg_tdm_iface_probe_dai,
		.remove = axg_tdm_iface_remove_dai,
	},
};

static int axg_tdm_iface_set_bias_level(struct snd_soc_component *component,
					enum snd_soc_bias_level level)
{
	struct axg_tdm_iface *iface = snd_soc_component_get_drvdata(component);
	enum snd_soc_bias_level now =
		snd_soc_component_get_bias_level(component);
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (now == SND_SOC_BIAS_STANDBY)
			ret = clk_prepare_enable(iface->mclk);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (now == SND_SOC_BIAS_PREPARE)
			clk_disable_unprepare(iface->mclk);
		break;

	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_ON:
		break;
	}

	return ret;
}

static const struct snd_soc_component_driver axg_tdm_iface_component_drv = {
	.set_bias_level	= axg_tdm_iface_set_bias_level,
};

static const struct of_device_id axg_tdm_iface_of_match[] = {
	{ .compatible = "amlogic,axg-tdm-iface", },
	{}
};
MODULE_DEVICE_TABLE(of, axg_tdm_iface_of_match);

static int axg_tdm_iface_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_dai_driver *dai_drv;
	struct axg_tdm_iface *iface;
	int ret, i;

	iface = devm_kzalloc(dev, sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return -ENOMEM;
	platform_set_drvdata(pdev, iface);

	/*
	 * Duplicate dai driver: depending on the slot masks configuration
	 * We'll change the number of channel provided by DAI stream, so dpcm
	 * channel merge can be done properly
	 */
	dai_drv = devm_kcalloc(dev, ARRAY_SIZE(axg_tdm_iface_dai_drv),
			       sizeof(*dai_drv), GFP_KERNEL);
	if (!dai_drv)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(axg_tdm_iface_dai_drv); i++)
		memcpy(&dai_drv[i], &axg_tdm_iface_dai_drv[i],
		       sizeof(*dai_drv));

	/* Bit clock provided on the pad */
	iface->sclk = devm_clk_get(dev, "sclk");
	if (IS_ERR(iface->sclk)) {
		ret = PTR_ERR(iface->sclk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get sclk: %d\n", ret);
		return ret;
	}

	/* Sample clock provided on the pad */
	iface->lrclk = devm_clk_get(dev, "lrclk");
	if (IS_ERR(iface->lrclk)) {
		ret = PTR_ERR(iface->lrclk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get lrclk: %d\n", ret);
		return ret;
	}

	/*
	 * mclk maybe be missing when the cpu dai is in slave mode and
	 * the codec does not require it to provide a master clock.
	 * At this point, ignore the error if mclk is missing. We'll
	 * throw an error if the cpu dai is master and mclk is missing
	 */
	iface->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(iface->mclk)) {
		ret = PTR_ERR(iface->mclk);
		if (ret == -ENOENT) {
			iface->mclk = NULL;
		} else {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "failed to get mclk: %d\n", ret);
			return ret;
		}
	}

	return devm_snd_soc_register_component(dev,
					&axg_tdm_iface_component_drv, dai_drv,
					ARRAY_SIZE(axg_tdm_iface_dai_drv));
}

static struct platform_driver axg_tdm_iface_pdrv = {
	.probe = axg_tdm_iface_probe,
	.driver = {
		.name = "axg-tdm-iface",
		.of_match_table = axg_tdm_iface_of_match,
	},
};
module_platform_driver(axg_tdm_iface_pdrv);

MODULE_DESCRIPTION("Amlogic AXG TDM interface driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
