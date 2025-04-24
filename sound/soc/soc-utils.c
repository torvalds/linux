// SPDX-License-Identifier: GPL-2.0+
//
// soc-util.c  --  ALSA SoC Audio Layer utility functions
//
// Copyright 2009 Wolfson Microelectronics PLC.
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
//         Liam Girdwood <lrg@slimlogic.co.uk>

#include <linux/device/faux.h>
#include <linux/export.h>
#include <linux/math.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

int snd_soc_ret(const struct device *dev, int ret, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	/* Positive, Zero values are not errors */
	if (ret >= 0)
		return ret;

	/* Negative values might be errors */
	switch (ret) {
	case -EPROBE_DEFER:
	case -ENOTSUPP:
	case -EOPNOTSUPP:
		break;
	default:
		va_start(args, fmt);
		vaf.fmt = fmt;
		vaf.va = &args;

		dev_err(dev, "ASoC error (%d): %pV", ret, &vaf);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_ret);

int snd_soc_calc_frame_size(int sample_size, int channels, int tdm_slots)
{
	return sample_size * channels * tdm_slots;
}
EXPORT_SYMBOL_GPL(snd_soc_calc_frame_size);

int snd_soc_params_to_frame_size(const struct snd_pcm_hw_params *params)
{
	int sample_size;

	sample_size = snd_pcm_format_width(params_format(params));
	if (sample_size < 0)
		return sample_size;

	return snd_soc_calc_frame_size(sample_size, params_channels(params),
				       1);
}
EXPORT_SYMBOL_GPL(snd_soc_params_to_frame_size);

int snd_soc_calc_bclk(int fs, int sample_size, int channels, int tdm_slots)
{
	return fs * snd_soc_calc_frame_size(sample_size, channels, tdm_slots);
}
EXPORT_SYMBOL_GPL(snd_soc_calc_bclk);

int snd_soc_params_to_bclk(const struct snd_pcm_hw_params *params)
{
	int ret;

	ret = snd_soc_params_to_frame_size(params);

	if (ret > 0)
		return ret * params_rate(params);
	else
		return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_params_to_bclk);

/**
 * snd_soc_tdm_params_to_bclk - calculate bclk from params and tdm slot info.
 *
 * Calculate the bclk from the params sample rate, the tdm slot count and the
 * tdm slot width. Optionally round-up the slot count to a given multiple.
 * Either or both of tdm_width and tdm_slots can be 0.
 *
 * If tdm_width == 0:	use params_width() as the slot width.
 * If tdm_slots == 0:	use params_channels() as the slot count.
 *
 * If slot_multiple > 1 the slot count (or params_channels() if tdm_slots == 0)
 * will be rounded up to a multiple of slot_multiple. This is mainly useful for
 * I2S mode, which has a left and right phase so the number of slots is always
 * a multiple of 2.
 *
 * If tdm_width == 0 && tdm_slots == 0 && slot_multiple < 2, this is equivalent
 * to calling snd_soc_params_to_bclk().
 *
 * @params:        Pointer to struct_pcm_hw_params.
 * @tdm_width:     Width in bits of the tdm slots. Must be >= 0.
 * @tdm_slots:     Number of tdm slots per frame. Must be >= 0.
 * @slot_multiple: If >1 roundup slot count to a multiple of this value.
 *
 * Return: bclk frequency in Hz, else a negative error code if params format
 *	   is invalid.
 */
int snd_soc_tdm_params_to_bclk(const struct snd_pcm_hw_params *params,
			       int tdm_width, int tdm_slots, int slot_multiple)
{
	if (!tdm_slots)
		tdm_slots = params_channels(params);

	if (slot_multiple > 1)
		tdm_slots = roundup(tdm_slots, slot_multiple);

	if (!tdm_width) {
		tdm_width = snd_pcm_format_width(params_format(params));
		if (tdm_width < 0)
			return tdm_width;
	}

	return snd_soc_calc_bclk(params_rate(params), tdm_width, 1, tdm_slots);
}
EXPORT_SYMBOL_GPL(snd_soc_tdm_params_to_bclk);

static const struct snd_pcm_hardware dummy_dma_hardware = {
	/* Random values to keep userspace happy when checking constraints */
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= 4096,
	.period_bytes_max	= 4096*2,
	.periods_min		= 2,
	.periods_max		= 128,
};


static const struct snd_soc_component_driver dummy_platform;

static int dummy_dma_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int i;

	/*
	 * If there are other components associated with rtd, we shouldn't
	 * override their hwparams
	 */
	for_each_rtd_components(rtd, i, component) {
		if (component->driver == &dummy_platform)
			return 0;
	}

	/* BE's dont need dummy params */
	if (!rtd->dai_link->no_pcm)
		snd_soc_set_runtime_hwparams(substream, &dummy_dma_hardware);

	return 0;
}

static const struct snd_soc_component_driver dummy_platform = {
	.open		= dummy_dma_open,
};

static const struct snd_soc_component_driver dummy_codec = {
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_U8 | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S24_3LE | \
			SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_U32_LE | \
			SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

/*
 * Select these from Sound Card Manually
 *	SND_SOC_POSSIBLE_DAIFMT_CBP_CFP
 *	SND_SOC_POSSIBLE_DAIFMT_CBP_CFC
 *	SND_SOC_POSSIBLE_DAIFMT_CBC_CFP
 *	SND_SOC_POSSIBLE_DAIFMT_CBC_CFC
 */
static const u64 dummy_dai_formats =
	SND_SOC_POSSIBLE_DAIFMT_I2S	|
	SND_SOC_POSSIBLE_DAIFMT_RIGHT_J	|
	SND_SOC_POSSIBLE_DAIFMT_LEFT_J	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_A	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_B	|
	SND_SOC_POSSIBLE_DAIFMT_AC97	|
	SND_SOC_POSSIBLE_DAIFMT_PDM	|
	SND_SOC_POSSIBLE_DAIFMT_GATED	|
	SND_SOC_POSSIBLE_DAIFMT_CONT	|
	SND_SOC_POSSIBLE_DAIFMT_NB_NF	|
	SND_SOC_POSSIBLE_DAIFMT_NB_IF	|
	SND_SOC_POSSIBLE_DAIFMT_IB_NF	|
	SND_SOC_POSSIBLE_DAIFMT_IB_IF;

static const struct snd_soc_dai_ops dummy_dai_ops = {
	.auto_selectable_formats	= &dummy_dai_formats,
	.num_auto_selectable_formats	= 1,
};

/*
 * The dummy CODEC is only meant to be used in situations where there is no
 * actual hardware.
 *
 * If there is actual hardware even if it does not have a control bus
 * the hardware will still have constraints like supported samplerates, etc.
 * which should be modelled. And the data flow graph also should be modelled
 * using DAPM.
 */
static struct snd_soc_dai_driver dummy_dai = {
	.name = "snd-soc-dummy-dai",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 5512,
		.rate_max	= 768000,
		.formats	= STUB_FORMATS,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min	= 5512,
		.rate_max	= 768000,
		.formats = STUB_FORMATS,
	 },
	.ops = &dummy_dai_ops,
};

int snd_soc_dai_is_dummy(const struct snd_soc_dai *dai)
{
	if (dai->driver == &dummy_dai)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_is_dummy);

int snd_soc_component_is_dummy(struct snd_soc_component *component)
{
	return ((component->driver == &dummy_platform) ||
		(component->driver == &dummy_codec));
}

struct snd_soc_dai_link_component snd_soc_dummy_dlc = {
	.of_node	= NULL,
	.dai_name	= "snd-soc-dummy-dai",
	.name		= "snd-soc-dummy",
};
EXPORT_SYMBOL_GPL(snd_soc_dummy_dlc);

static int snd_soc_dummy_probe(struct faux_device *fdev)
{
	int ret;

	ret = devm_snd_soc_register_component(&fdev->dev,
					      &dummy_codec, &dummy_dai, 1);
	if (ret < 0)
		return ret;

	ret = devm_snd_soc_register_component(&fdev->dev, &dummy_platform,
					      NULL, 0);

	return ret;
}

static struct faux_device_ops soc_dummy_ops = {
	.probe = snd_soc_dummy_probe,
};

static struct faux_device *soc_dummy_dev;

int __init snd_soc_util_init(void)
{
	soc_dummy_dev = faux_device_create("snd-soc-dummy", NULL,
					   &soc_dummy_ops);
	if (!soc_dummy_dev)
		return -ENODEV;

	return 0;
}

void snd_soc_util_exit(void)
{
	faux_device_destroy(soc_dummy_dev);
}
