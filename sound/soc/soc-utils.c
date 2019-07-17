// SPDX-License-Identifier: GPL-2.0+
//
// soc-util.c  --  ALSA SoC Audio Layer utility functions
//
// Copyright 2009 Wolfson Microelectronics PLC.
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
//         Liam Girdwood <lrg@slimlogic.co.uk>

#include <linux/platform_device.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

int snd_soc_calc_frame_size(int sample_size, int channels, int tdm_slots)
{
	return sample_size * channels * tdm_slots;
}
EXPORT_SYMBOL_GPL(snd_soc_calc_frame_size);

int snd_soc_params_to_frame_size(struct snd_pcm_hw_params *params)
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

int snd_soc_params_to_bclk(struct snd_pcm_hw_params *params)
{
	int ret;

	ret = snd_soc_params_to_frame_size(params);

	if (ret > 0)
		return ret * params_rate(params);
	else
		return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_params_to_bclk);

int snd_soc_component_enable_pin(struct snd_soc_component *component,
				 const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_enable_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_enable_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_enable_pin);

int snd_soc_component_enable_pin_unlocked(struct snd_soc_component *component,
					  const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_enable_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_enable_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_enable_pin_unlocked);

int snd_soc_component_disable_pin(struct snd_soc_component *component,
				  const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_disable_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_disable_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_disable_pin);

int snd_soc_component_disable_pin_unlocked(struct snd_soc_component *component,
					   const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_disable_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_disable_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_disable_pin_unlocked);

int snd_soc_component_nc_pin(struct snd_soc_component *component,
			     const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_nc_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_nc_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_nc_pin);

int snd_soc_component_nc_pin_unlocked(struct snd_soc_component *component,
				      const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_nc_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_nc_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_nc_pin_unlocked);

int snd_soc_component_get_pin_status(struct snd_soc_component *component,
				     const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_get_pin_status(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_get_pin_status(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_get_pin_status);

int snd_soc_component_force_enable_pin(struct snd_soc_component *component,
				       const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_force_enable_pin(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_force_enable_pin(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_force_enable_pin);

int snd_soc_component_force_enable_pin_unlocked(
					struct snd_soc_component *component,
					const char *pin)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	char *full_name;
	int ret;

	if (!component->name_prefix)
		return snd_soc_dapm_force_enable_pin_unlocked(dapm, pin);

	full_name = kasprintf(GFP_KERNEL, "%s %s", component->name_prefix, pin);
	if (!full_name)
		return -ENOMEM;

	ret = snd_soc_dapm_force_enable_pin_unlocked(dapm, full_name);
	kfree(full_name);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_force_enable_pin_unlocked);

static const struct snd_pcm_hardware dummy_dma_hardware = {
	/* Random values to keep userspace happy when checking constraints */
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= PAGE_SIZE*2,
	.periods_min		= 2,
	.periods_max		= 128,
};

static int dummy_dma_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* BE's dont need dummy params */
	if (!rtd->dai_link->no_pcm)
		snd_soc_set_runtime_hwparams(substream, &dummy_dma_hardware);

	return 0;
}

static const struct snd_pcm_ops snd_dummy_dma_ops = {
	.open		= dummy_dma_open,
	.ioctl		= snd_pcm_lib_ioctl,
};

static const struct snd_soc_component_driver dummy_platform = {
	.ops = &snd_dummy_dma_ops,
};

static const struct snd_soc_component_driver dummy_codec = {
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_U8 | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_U32_LE | \
			SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)
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
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates = STUB_RATES,
		.formats = STUB_FORMATS,
	 },
};

int snd_soc_dai_is_dummy(struct snd_soc_dai *dai)
{
	if (dai->driver == &dummy_dai)
		return 1;
	return 0;
}

static int snd_soc_dummy_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &dummy_codec, &dummy_dai, 1);
	if (ret < 0)
		return ret;

	ret = devm_snd_soc_register_component(&pdev->dev, &dummy_platform,
					      NULL, 0);

	return ret;
}

static struct platform_driver soc_dummy_driver = {
	.driver = {
		.name = "snd-soc-dummy",
	},
	.probe = snd_soc_dummy_probe,
};

static struct platform_device *soc_dummy_dev;

int __init snd_soc_util_init(void)
{
	int ret;

	soc_dummy_dev =
		platform_device_register_simple("snd-soc-dummy", -1, NULL, 0);
	if (IS_ERR(soc_dummy_dev))
		return PTR_ERR(soc_dummy_dev);

	ret = platform_driver_register(&soc_dummy_driver);
	if (ret != 0)
		platform_device_unregister(soc_dummy_dev);

	return ret;
}

void __exit snd_soc_util_exit(void)
{
	platform_driver_unregister(&soc_dummy_driver);
	platform_device_unregister(soc_dummy_dev);
}
