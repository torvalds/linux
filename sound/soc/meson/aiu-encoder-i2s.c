// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"
#include "gx-formatter.h"
#include "gx-interface.h"

#define AIU_I2S_SOURCE_DESC_MODE_SPLIT	BIT(11)

#define AIU_CLK_CTRL_I2S_DIV_EN		BIT(0)
#define AIU_CLK_CTRL_I2S_DIV		GENMASK(3, 2)
#define AIU_CLK_CTRL_AOCLK_INVERT	BIT(6)
#define AIU_CLK_CTRL_LRCLK_INVERT	BIT(7)
#define AIU_CLK_CTRL_LRCLK_SKEW		GENMASK(9, 8)
#define AIU_CLK_CTRL_MORE_HDMI_AMCLK	BIT(6)
#define AIU_CLK_CTRL_MORE_I2S_DIV	GENMASK(5, 0)
#define AIU_CODEC_DAC_LRCLK_CTRL_DIV	GENMASK(11, 0)

static void aiu_encoder_i2s_divider_enable(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV_EN,
				      enable ? AIU_CLK_CTRL_I2S_DIV_EN : 0);
}

static int aiu_encoder_i2s_set_legacy_div(struct snd_soc_component *component,
					  struct snd_pcm_hw_params *params,
					  unsigned int bs)
{
	switch (bs) {
	case 1:
	case 2:
	case 4:
	case 8:
		/* These are the only valid legacy dividers */
		break;

	default:
		dev_err(component->dev, "Unsupported i2s divider: %u\n", bs);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_I2S_DIV,
						 __ffs(bs)));

	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_MORE_I2S_DIV,
						 0));

	return 0;
}

/*
 * Return true if the given combination of channels and sample width requires
 * the bs quirk. Return false otherwise.
 */
static bool aiu_encoder_is_bs_quirk(unsigned int channels, int width)
{
	return (channels == 8) && (width == 16);
}

static int aiu_encoder_check_bs_quirk(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct gx_stream *other_stream = snd_soc_dai_dma_data_get(dai, !substream->stream);

	/* Nothing to do if the other stream doesn't exist or it's not configured yet. */
	if (!other_stream || !other_stream->channels)
		return 0;

	if (aiu_encoder_is_bs_quirk(other_stream->channels, other_stream->width) !=
	    aiu_encoder_is_bs_quirk(params_channels(params), params_width(params)))
		return -EINVAL;

	return 0;
}

static int aiu_encoder_i2s_set_more_div(struct snd_soc_component *component,
					struct snd_pcm_hw_params *params,
					unsigned int bs)
{
	/*
	 * NOTE: this HW is odd.
	 * In most configuration, the i2s divider is 'mclk / blck'.
	 * However, in 16 bits - 8ch mode, this factor needs to be
	 * increased by 50% to get the correct output rate.
	 * No idea why !
	 */
	if (aiu_encoder_is_bs_quirk(params_channels(params), params_width(params))) {
		if (bs % 2) {
			dev_err(component->dev,
				"Cannot increase i2s divider by 50%%\n");
			return -EINVAL;
		}
		bs += bs / 2;
	}

	/* Use CLK_MORE for mclk to bclk divider */
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_I2S_DIV, 0));

	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_MORE_I2S_DIV,
						 bs - 1));

	return 0;
}

static int aiu_encoder_i2s_set_clocks(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	struct gx_iface *iface = &aiu->i2s.iface;
	unsigned int srate = params_rate(params);
	unsigned int fs, bs;
	int ret;

	/* Get the oversampling factor */
	fs = DIV_ROUND_CLOSEST(iface->mclk_rate, srate);

	if ((fs % 64) || (fs == 0))
		return -EINVAL;

	/* Set bclk to lrlck ratio */
	snd_soc_component_update_bits(component, AIU_CODEC_DAC_LRCLK_CTRL,
				      AIU_CODEC_DAC_LRCLK_CTRL_DIV,
				      FIELD_PREP(AIU_CODEC_DAC_LRCLK_CTRL_DIV,
						 64 - 1));

	bs = fs / 64;

	if (aiu->platform->has_clk_ctrl_more_i2s_div) {
		/*
		 * The hw rules added in startup() make this unreachable in the
		 * sequential case, but both streams may be refined concurrently
		 * before either commits its config, since only ops->hw_params
		 * runs under the card's pcm_mutex. Re-check against the committed
		 * state of the other stream, which is stable under that mutex.
		 */
		if (aiu_encoder_check_bs_quirk(substream, params, dai)) {
			dev_err(dai->dev, "bclk requirements incompatible with other stream\n");
			return -EINVAL;
		}
		ret = aiu_encoder_i2s_set_more_div(component, params, bs);
	} else {
		ret = aiu_encoder_i2s_set_legacy_div(component, params, bs);
	}

	if (ret)
		return ret;

	/* Make sure amclk is used for HDMI i2s as well */
	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK);

	return 0;
}

static int aiu_encoder_i2s_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct gx_stream *ts = snd_soc_dai_get_dma_data(dai, substream);
	int ret;

	ret = aiu_encoder_i2s_set_clocks(substream, params, dai);
	if (ret) {
		dev_err(dai->dev, "setting i2s clocks failed: %d\n", ret);
		return ret;
	}

	ts->physical_width = params_physical_width(params);
	ts->width = params_width(params);
	ts->channels = params_channels(params);

	return 0;
}

static int aiu_encoder_i2s_prepare(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct gx_stream *ts = snd_soc_dai_get_dma_data(dai, substream);
	struct snd_soc_component *component = dai->component;
	int ret;

	if (ts->clk_enabled)
		return 0;

	ret = clk_prepare_enable(ts->iface->mclk);
	if (ret)
		return ret;

	ts->clk_enabled = true;

	aiu_encoder_i2s_divider_enable(component, true);

	return 0;
}

static int aiu_encoder_i2s_hw_free(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct gx_stream *ts = snd_soc_dai_get_dma_data(dai, substream);
	struct snd_soc_component *component = dai->component;

	/*
	 * If this is the last substream being closed then disable the i2s
	 * clock divider.
	 */
	if (snd_soc_dai_active(dai) <= 1)
		aiu_encoder_i2s_divider_enable(component, 0);

	if (ts->clk_enabled) {
		clk_disable_unprepare(ts->iface->mclk);
		ts->clk_enabled = false;
	}

	ts->channels = 0;
	ts->width = 0;
	ts->physical_width = 0;

	return 0;
}

static int aiu_encoder_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	struct gx_iface *iface = &aiu->i2s.iface;
	unsigned int inv = fmt & SND_SOC_DAIFMT_INV_MASK;
	unsigned int val = 0;
	unsigned int skew;

	/* Only CPU Master / Codec Slave supported ATM */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP)
		return -EINVAL;

	if (inv == SND_SOC_DAIFMT_NB_IF ||
	    inv == SND_SOC_DAIFMT_IB_IF)
		val |= AIU_CLK_CTRL_LRCLK_INVERT;

	/*
	 * The SoC changes data on the rising edge of the bitclock
	 * so an inversion of the bitclock is required in normal mode
	 */
	if (inv == SND_SOC_DAIFMT_NB_NF ||
	    inv == SND_SOC_DAIFMT_NB_IF)
		val |= AIU_CLK_CTRL_AOCLK_INVERT;

	/* Signal skew */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* Invert sample clock for i2s */
		val ^= AIU_CLK_CTRL_LRCLK_INVERT;
		skew = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		skew = 0;
		break;
	default:
		dev_err(dai->dev, "unsupported dai format\n");
		return -EINVAL;
	}

	iface->fmt = fmt;

	val |= FIELD_PREP(AIU_CLK_CTRL_LRCLK_SKEW, skew);
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_LRCLK_INVERT |
				      AIU_CLK_CTRL_AOCLK_INVERT |
				      AIU_CLK_CTRL_LRCLK_SKEW,
				      val);

	return 0;
}

static int aiu_encoder_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	struct gx_iface *iface = &aiu->i2s.iface;
	int ret;

	if (WARN_ON(clk_id != 0))
		return -EINVAL;

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	ret = clk_set_rate(iface->mclk, freq);
	if (ret) {
		dev_err(dai->dev, "Failed to set sysclk to %uHz: %d", freq, ret);
		return ret;
	}

	iface->mclk_rate = freq;

	return 0;
}

static const unsigned int hw_channels[] = {2, 8};
static const struct snd_pcm_hw_constraint_list hw_channel_constraints = {
	.list = hw_channels,
	.count = ARRAY_SIZE(hw_channels),
	.mask = 0,
};

static int aiu_encoder_i2s_pcm_hw_rule(struct snd_pcm_hw_params *params,
				       struct snd_pcm_hw_rule *rule)
{
	struct gx_stream *other = rule->private;
	struct snd_interval *ch = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	/*
	 * The quirk is technically based on the significant bits whereas here
	 * we're using the physical width for simplicity. This works because
	 * S16_LE is the only format supported by this encoder that has:
	 * significant bits = physical width = 16-bits
	 */
	struct snd_interval *phys_width = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
	struct snd_interval new_i;

	if (other->channels == 0)
		return 0;

	snd_interval_any(&new_i);

	if (rule->var == SNDRV_PCM_HW_PARAM_CHANNELS) {
		if (aiu_encoder_is_bs_quirk(other->channels, other->width))
			new_i.min = new_i.max = 8;
		else if (snd_interval_single(phys_width) && phys_width->min == 16)
			new_i.max = 2; /* Force 2ch */
	} else { /* SNDRV_PCM_HW_PARAM_SAMPLE_BITS */
		if (aiu_encoder_is_bs_quirk(other->channels, other->width))
			new_i.min = new_i.max = 16;
		else if (snd_interval_single(ch) && ch->min == 8)
			new_i.min = 17; /* Request physical width > 16 bits */
	}

	return snd_interval_refine(hw_param_interval(params, rule->var), &new_i);
}

static int aiu_encoder_i2s_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	struct gx_stream *other_stream = snd_soc_dai_dma_data_get(dai, !substream->stream);
	int ret;

	/* Make sure the encoder gets either 2 or 8 channels */
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 &hw_channel_constraints);
	if (ret) {
		dev_err(dai->dev, "adding channels constraints failed: %d\n", ret);
		return ret;
	}

	/*
	 * If DAI supports both playback and capture streams ensure the bs-quirk is
	 * handled correctly.
	 * This is only valid for GX platforms (has_clk_ctrl_more_i2s_div=true).
	 */
	if (aiu->platform->has_clk_ctrl_more_i2s_div && other_stream) {
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  aiu_encoder_i2s_pcm_hw_rule,
					  other_stream,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
		if (ret)
			return ret;

		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
					  aiu_encoder_i2s_pcm_hw_rule,
					  other_stream,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
		if (ret)
			return ret;
	}

	/*
	 * Enable only clocks which are required for the interface internal
	 * logic. MCLK is enabled/disabled from the formatter and the I2S
	 * divider is enabled/disabled in "hw_params"/"hw_free", respectively.
	 */
	ret = clk_prepare_enable(aiu->i2s.clks[PCLK].clk);
	if (ret) {
		dev_err(dai->dev, "failed to enable PCLK: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(aiu->i2s.clks[MIXER].clk);
	if (ret) {
		dev_err(dai->dev, "failed to enable MIXER: %d\n", ret);
		clk_disable_unprepare(aiu->i2s.clks[PCLK].clk);
		return ret;
	}
	ret = clk_prepare_enable(aiu->i2s.clks[AOCLK].clk);
	if (ret) {
		dev_err(dai->dev, "failed to enable AOCLK: %d\n", ret);
		clk_disable_unprepare(aiu->i2s.clks[MIXER].clk);
		clk_disable_unprepare(aiu->i2s.clks[PCLK].clk);
		return ret;
	}

	/*
	 * We're always operating in split mode for the playback stream.
	 *
	 * This setting arguably belong to the 'aiu-formatter', but it's kept
	 * here for backward compatibility reason. At reset the I2S encoder
	 * operates in normal mode which would only support 8ch, but by default
	 * only 2ch are enabled. If a playback stream is started without
	 * changing to split mode, then the I2S encoder doesn't consume audio
	 * samples and the playback fails.
	 * Moving this to 'aiu-formatter' would cause the split mode to be set
	 * only when the formatter is enabled, which doesn't happen at boot as
	 * the default value for "HDMI CTRL SRC" is "DISABLED".
	 */
	ret = snd_soc_component_update_bits(dai->component, AIU_I2S_SOURCE_DESC,
					    AIU_I2S_SOURCE_DESC_MODE_SPLIT,
					    AIU_I2S_SOURCE_DESC_MODE_SPLIT);
	if (ret < 0)
		dev_err(dai->dev, "failed to update AIU_I2S_SOURCE_DESC: %d", ret);

	return 0;
}

static void aiu_encoder_i2s_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);

	clk_disable_unprepare(aiu->i2s.clks[AOCLK].clk);
	clk_disable_unprepare(aiu->i2s.clks[MIXER].clk);
	clk_disable_unprepare(aiu->i2s.clks[PCLK].clk);
}

static int aiu_encoder_i2s_trigger(struct snd_pcm_substream *substream,
				   int cmd,
				   struct snd_soc_dai *dai)
{
	struct gx_stream *ts = snd_soc_dai_get_dma_data(dai, substream);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = gx_stream_start(ts);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		gx_stream_stop(ts);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int aiu_encoder_i2s_remove_dai(struct snd_soc_dai *dai)
{
	int stream;

	for_each_pcm_streams(stream) {
		struct gx_stream *ts;

		ts = snd_soc_dai_dma_data_get(dai, stream);
		if (ts)
			gx_stream_free(ts);

		snd_soc_dai_dma_data_set(dai, stream, NULL);
	}

	return 0;
}

static int aiu_encoder_i2s_probe_dai(struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_dai_get_drvdata(dai);
	struct gx_iface *iface = &aiu->i2s.iface;
	int stream;

	for_each_pcm_streams(stream) {
		struct gx_stream *ts;

		if (!snd_soc_dai_get_widget(dai, stream))
			continue;

		ts = gx_stream_alloc(iface);
		if (!ts) {
			aiu_encoder_i2s_remove_dai(dai);
			return -ENOMEM;
		}
		snd_soc_dai_dma_data_set(dai, stream, ts);
	}

	iface->mclk = aiu->i2s.clks[MCLK].clk;
	iface->mclk_rate = clk_get_rate(iface->mclk);

	return 0;
}

const struct snd_soc_dai_ops aiu_encoder_i2s_dai_ops = {
	.probe		= aiu_encoder_i2s_probe_dai,
	.remove		= aiu_encoder_i2s_remove_dai,
	.hw_params	= aiu_encoder_i2s_hw_params,
	.prepare	= aiu_encoder_i2s_prepare,
	.hw_free	= aiu_encoder_i2s_hw_free,
	.set_fmt	= aiu_encoder_i2s_set_fmt,
	.set_sysclk	= aiu_encoder_i2s_set_sysclk,
	.startup	= aiu_encoder_i2s_startup,
	.shutdown	= aiu_encoder_i2s_shutdown,
	.trigger	= aiu_encoder_i2s_trigger,
};
