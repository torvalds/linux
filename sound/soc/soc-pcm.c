/*
 * soc-pcm.c  --  ALSA SoC PCM
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 * Copyright (C) 2010 Slimlogic Ltd.
 * Copyright (C) 2010 Texas Instruments Inc.
 *
 * Authors: Liam Girdwood <lrg@ti.com>
 *          Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dpcm.h>
#include <sound/initval.h>

#define DPCM_MAX_BE_USERS	8

/**
 * snd_soc_runtime_activate() - Increment active count for PCM runtime components
 * @rtd: ASoC PCM runtime that is activated
 * @stream: Direction of the PCM stream
 *
 * Increments the active count for all the DAIs and components attached to a PCM
 * runtime. Should typically be called when a stream is opened.
 *
 * Must be called with the rtd->pcm_mutex being held
 */
void snd_soc_runtime_activate(struct snd_soc_pcm_runtime *rtd, int stream)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int i;

	lockdep_assert_held(&rtd->pcm_mutex);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cpu_dai->playback_active++;
		for (i = 0; i < rtd->num_codecs; i++)
			rtd->codec_dais[i]->playback_active++;
	} else {
		cpu_dai->capture_active++;
		for (i = 0; i < rtd->num_codecs; i++)
			rtd->codec_dais[i]->capture_active++;
	}

	cpu_dai->active++;
	cpu_dai->component->active++;
	for (i = 0; i < rtd->num_codecs; i++) {
		rtd->codec_dais[i]->active++;
		rtd->codec_dais[i]->component->active++;
	}
}

/**
 * snd_soc_runtime_deactivate() - Decrement active count for PCM runtime components
 * @rtd: ASoC PCM runtime that is deactivated
 * @stream: Direction of the PCM stream
 *
 * Decrements the active count for all the DAIs and components attached to a PCM
 * runtime. Should typically be called when a stream is closed.
 *
 * Must be called with the rtd->pcm_mutex being held
 */
void snd_soc_runtime_deactivate(struct snd_soc_pcm_runtime *rtd, int stream)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int i;

	lockdep_assert_held(&rtd->pcm_mutex);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		cpu_dai->playback_active--;
		for (i = 0; i < rtd->num_codecs; i++)
			rtd->codec_dais[i]->playback_active--;
	} else {
		cpu_dai->capture_active--;
		for (i = 0; i < rtd->num_codecs; i++)
			rtd->codec_dais[i]->capture_active--;
	}

	cpu_dai->active--;
	cpu_dai->component->active--;
	for (i = 0; i < rtd->num_codecs; i++) {
		rtd->codec_dais[i]->component->active--;
		rtd->codec_dais[i]->active--;
	}
}

/**
 * snd_soc_runtime_ignore_pmdown_time() - Check whether to ignore the power down delay
 * @rtd: The ASoC PCM runtime that should be checked.
 *
 * This function checks whether the power down delay should be ignored for a
 * specific PCM runtime. Returns true if the delay is 0, if it the DAI link has
 * been configured to ignore the delay, or if none of the components benefits
 * from having the delay.
 */
bool snd_soc_runtime_ignore_pmdown_time(struct snd_soc_pcm_runtime *rtd)
{
	int i;
	bool ignore = true;

	if (!rtd->pmdown_time || rtd->dai_link->ignore_pmdown_time)
		return true;

	for (i = 0; i < rtd->num_codecs; i++)
		ignore &= rtd->codec_dais[i]->component->ignore_pmdown_time;

	return rtd->cpu_dai->component->ignore_pmdown_time && ignore;
}

/**
 * snd_soc_set_runtime_hwparams - set the runtime hardware parameters
 * @substream: the pcm substream
 * @hw: the hardware parameters
 *
 * Sets the substream runtime hardware parameters.
 */
int snd_soc_set_runtime_hwparams(struct snd_pcm_substream *substream,
	const struct snd_pcm_hardware *hw)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->hw.info = hw->info;
	runtime->hw.formats = hw->formats;
	runtime->hw.period_bytes_min = hw->period_bytes_min;
	runtime->hw.period_bytes_max = hw->period_bytes_max;
	runtime->hw.periods_min = hw->periods_min;
	runtime->hw.periods_max = hw->periods_max;
	runtime->hw.buffer_bytes_max = hw->buffer_bytes_max;
	runtime->hw.fifo_size = hw->fifo_size;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_runtime_hwparams);

/* DPCM stream event, send event to FE and all active BEs. */
int dpcm_dapm_stream_event(struct snd_soc_pcm_runtime *fe, int dir,
	int event)
{
	struct snd_soc_dpcm *dpcm;

	list_for_each_entry(dpcm, &fe->dpcm[dir].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;

		dev_dbg(be->dev, "ASoC: BE %s event %d dir %d\n",
				be->dai_link->name, event, dir);

		snd_soc_dapm_stream_event(be, dir, event);
	}

	snd_soc_dapm_stream_event(fe, dir, event);

	return 0;
}

static int soc_pcm_apply_symmetry(struct snd_pcm_substream *substream,
					struct snd_soc_dai *soc_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int ret;

	if (soc_dai->rate && (soc_dai->driver->symmetric_rates ||
				rtd->dai_link->symmetric_rates)) {
		dev_dbg(soc_dai->dev, "ASoC: Symmetry forces %dHz rate\n",
				soc_dai->rate);

		ret = snd_pcm_hw_constraint_minmax(substream->runtime,
						SNDRV_PCM_HW_PARAM_RATE,
						soc_dai->rate, soc_dai->rate);
		if (ret < 0) {
			dev_err(soc_dai->dev,
				"ASoC: Unable to apply rate constraint: %d\n",
				ret);
			return ret;
		}
	}

	if (soc_dai->channels && (soc_dai->driver->symmetric_channels ||
				rtd->dai_link->symmetric_channels)) {
		dev_dbg(soc_dai->dev, "ASoC: Symmetry forces %d channel(s)\n",
				soc_dai->channels);

		ret = snd_pcm_hw_constraint_minmax(substream->runtime,
						SNDRV_PCM_HW_PARAM_CHANNELS,
						soc_dai->channels,
						soc_dai->channels);
		if (ret < 0) {
			dev_err(soc_dai->dev,
				"ASoC: Unable to apply channel symmetry constraint: %d\n",
				ret);
			return ret;
		}
	}

	if (soc_dai->sample_bits && (soc_dai->driver->symmetric_samplebits ||
				rtd->dai_link->symmetric_samplebits)) {
		dev_dbg(soc_dai->dev, "ASoC: Symmetry forces %d sample bits\n",
				soc_dai->sample_bits);

		ret = snd_pcm_hw_constraint_minmax(substream->runtime,
						SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
						soc_dai->sample_bits,
						soc_dai->sample_bits);
		if (ret < 0) {
			dev_err(soc_dai->dev,
				"ASoC: Unable to apply sample bits symmetry constraint: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int soc_pcm_params_symmetry(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int rate, channels, sample_bits, symmetry, i;

	rate = params_rate(params);
	channels = params_channels(params);
	sample_bits = snd_pcm_format_physical_width(params_format(params));

	/* reject unmatched parameters when applying symmetry */
	symmetry = cpu_dai->driver->symmetric_rates ||
		rtd->dai_link->symmetric_rates;

	for (i = 0; i < rtd->num_codecs; i++)
		symmetry |= rtd->codec_dais[i]->driver->symmetric_rates;

	if (symmetry && cpu_dai->rate && cpu_dai->rate != rate) {
		dev_err(rtd->dev, "ASoC: unmatched rate symmetry: %d - %d\n",
				cpu_dai->rate, rate);
		return -EINVAL;
	}

	symmetry = cpu_dai->driver->symmetric_channels ||
		rtd->dai_link->symmetric_channels;

	for (i = 0; i < rtd->num_codecs; i++)
		symmetry |= rtd->codec_dais[i]->driver->symmetric_channels;

	if (symmetry && cpu_dai->channels && cpu_dai->channels != channels) {
		dev_err(rtd->dev, "ASoC: unmatched channel symmetry: %d - %d\n",
				cpu_dai->channels, channels);
		return -EINVAL;
	}

	symmetry = cpu_dai->driver->symmetric_samplebits ||
		rtd->dai_link->symmetric_samplebits;

	for (i = 0; i < rtd->num_codecs; i++)
		symmetry |= rtd->codec_dais[i]->driver->symmetric_samplebits;

	if (symmetry && cpu_dai->sample_bits && cpu_dai->sample_bits != sample_bits) {
		dev_err(rtd->dev, "ASoC: unmatched sample bits symmetry: %d - %d\n",
				cpu_dai->sample_bits, sample_bits);
		return -EINVAL;
	}

	return 0;
}

static bool soc_pcm_has_symmetry(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai_driver *cpu_driver = rtd->cpu_dai->driver;
	struct snd_soc_dai_link *link = rtd->dai_link;
	unsigned int symmetry, i;

	symmetry = cpu_driver->symmetric_rates || link->symmetric_rates ||
		cpu_driver->symmetric_channels || link->symmetric_channels ||
		cpu_driver->symmetric_samplebits || link->symmetric_samplebits;

	for (i = 0; i < rtd->num_codecs; i++)
		symmetry = symmetry ||
			rtd->codec_dais[i]->driver->symmetric_rates ||
			rtd->codec_dais[i]->driver->symmetric_channels ||
			rtd->codec_dais[i]->driver->symmetric_samplebits;

	return symmetry;
}

/*
 * List of sample sizes that might go over the bus for parameter
 * application.  There ought to be a wildcard sample size for things
 * like the DAC/ADC resolution to use but there isn't right now.
 */
static int sample_sizes[] = {
	24, 32,
};

static void soc_pcm_set_msb(struct snd_pcm_substream *substream, int bits)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int ret, i;

	if (!bits)
		return;

	for (i = 0; i < ARRAY_SIZE(sample_sizes); i++) {
		if (bits >= sample_sizes[i])
			continue;

		ret = snd_pcm_hw_constraint_msbits(substream->runtime, 0,
						   sample_sizes[i], bits);
		if (ret != 0)
			dev_warn(rtd->dev,
				 "ASoC: Failed to set MSB %d/%d: %d\n",
				 bits, sample_sizes[i], ret);
	}
}

static void soc_pcm_apply_msb(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	int i;
	unsigned int bits = 0, cpu_bits;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < rtd->num_codecs; i++) {
			codec_dai = rtd->codec_dais[i];
			if (codec_dai->driver->playback.sig_bits == 0) {
				bits = 0;
				break;
			}
			bits = max(codec_dai->driver->playback.sig_bits, bits);
		}
		cpu_bits = cpu_dai->driver->playback.sig_bits;
	} else {
		for (i = 0; i < rtd->num_codecs; i++) {
			codec_dai = rtd->codec_dais[i];
			if (codec_dai->driver->capture.sig_bits == 0) {
				bits = 0;
				break;
			}
			bits = max(codec_dai->driver->capture.sig_bits, bits);
		}
		cpu_bits = cpu_dai->driver->capture.sig_bits;
	}

	soc_pcm_set_msb(substream, bits);
	soc_pcm_set_msb(substream, cpu_bits);
}

static void soc_pcm_init_runtime_hw(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai_driver *cpu_dai_drv = rtd->cpu_dai->driver;
	struct snd_soc_dai_driver *codec_dai_drv;
	struct snd_soc_pcm_stream *codec_stream;
	struct snd_soc_pcm_stream *cpu_stream;
	unsigned int chan_min = 0, chan_max = UINT_MAX;
	unsigned int rate_min = 0, rate_max = UINT_MAX;
	unsigned int rates = UINT_MAX;
	u64 formats = ULLONG_MAX;
	int i;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_stream = &cpu_dai_drv->playback;
	else
		cpu_stream = &cpu_dai_drv->capture;

	/* first calculate min/max only for CODECs in the DAI link */
	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai_drv = rtd->codec_dais[i]->driver;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			codec_stream = &codec_dai_drv->playback;
		else
			codec_stream = &codec_dai_drv->capture;
		chan_min = max(chan_min, codec_stream->channels_min);
		chan_max = min(chan_max, codec_stream->channels_max);
		rate_min = max(rate_min, codec_stream->rate_min);
		rate_max = min_not_zero(rate_max, codec_stream->rate_max);
		formats &= codec_stream->formats;
		rates = snd_pcm_rate_mask_intersect(codec_stream->rates, rates);
	}

	/*
	 * chan min/max cannot be enforced if there are multiple CODEC DAIs
	 * connected to a single CPU DAI, use CPU DAI's directly and let
	 * channel allocation be fixed up later
	 */
	if (rtd->num_codecs > 1) {
		chan_min = cpu_stream->channels_min;
		chan_max = cpu_stream->channels_max;
	}

	hw->channels_min = max(chan_min, cpu_stream->channels_min);
	hw->channels_max = min(chan_max, cpu_stream->channels_max);
	if (hw->formats)
		hw->formats &= formats & cpu_stream->formats;
	else
		hw->formats = formats & cpu_stream->formats;
	hw->rates = snd_pcm_rate_mask_intersect(rates, cpu_stream->rates);

	snd_pcm_limit_hw_rates(runtime);

	hw->rate_min = max(hw->rate_min, cpu_stream->rate_min);
	hw->rate_min = max(hw->rate_min, rate_min);
	hw->rate_max = min_not_zero(hw->rate_max, cpu_stream->rate_max);
	hw->rate_max = min_not_zero(hw->rate_max, rate_max);
}

/*
 * Called by ALSA when a PCM substream is opened, the runtime->hw record is
 * then initialized and any private data can be allocated. This also calls
 * startup for the cpu DAI, platform, machine and codec DAI.
 */
static int soc_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	const char *codec_dai_name = "multicodec";
	int i, ret = 0;

	pinctrl_pm_select_default_state(cpu_dai->dev);
	for (i = 0; i < rtd->num_codecs; i++)
		pinctrl_pm_select_default_state(rtd->codec_dais[i]->dev);
	pm_runtime_get_sync(cpu_dai->dev);
	for (i = 0; i < rtd->num_codecs; i++)
		pm_runtime_get_sync(rtd->codec_dais[i]->dev);
	pm_runtime_get_sync(platform->dev);

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	/* startup the audio subsystem */
	if (cpu_dai->driver->ops && cpu_dai->driver->ops->startup) {
		ret = cpu_dai->driver->ops->startup(substream, cpu_dai);
		if (ret < 0) {
			dev_err(cpu_dai->dev, "ASoC: can't open interface"
				" %s: %d\n", cpu_dai->name, ret);
			goto out;
		}
	}

	if (platform->driver->ops && platform->driver->ops->open) {
		ret = platform->driver->ops->open(substream);
		if (ret < 0) {
			dev_err(platform->dev, "ASoC: can't open platform"
				" %s: %d\n", platform->component.name, ret);
			goto platform_err;
		}
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops && codec_dai->driver->ops->startup) {
			ret = codec_dai->driver->ops->startup(substream,
							      codec_dai);
			if (ret < 0) {
				dev_err(codec_dai->dev,
					"ASoC: can't open codec %s: %d\n",
					codec_dai->name, ret);
				goto codec_dai_err;
			}
		}

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			codec_dai->tx_mask = 0;
		else
			codec_dai->rx_mask = 0;
	}

	if (rtd->dai_link->ops && rtd->dai_link->ops->startup) {
		ret = rtd->dai_link->ops->startup(substream);
		if (ret < 0) {
			pr_err("ASoC: %s startup failed: %d\n",
			       rtd->dai_link->name, ret);
			goto machine_err;
		}
	}

	/* Dynamic PCM DAI links compat checks use dynamic capabilities */
	if (rtd->dai_link->dynamic || rtd->dai_link->no_pcm)
		goto dynamic;

	/* Check that the codec and cpu DAIs are compatible */
	soc_pcm_init_runtime_hw(substream);

	if (rtd->num_codecs == 1)
		codec_dai_name = rtd->codec_dai->name;

	if (soc_pcm_has_symmetry(substream))
		runtime->hw.info |= SNDRV_PCM_INFO_JOINT_DUPLEX;

	ret = -EINVAL;
	if (!runtime->hw.rates) {
		printk(KERN_ERR "ASoC: %s <-> %s No matching rates\n",
			codec_dai_name, cpu_dai->name);
		goto config_err;
	}
	if (!runtime->hw.formats) {
		printk(KERN_ERR "ASoC: %s <-> %s No matching formats\n",
			codec_dai_name, cpu_dai->name);
		goto config_err;
	}
	if (!runtime->hw.channels_min || !runtime->hw.channels_max ||
	    runtime->hw.channels_min > runtime->hw.channels_max) {
		printk(KERN_ERR "ASoC: %s <-> %s No matching channels\n",
				codec_dai_name, cpu_dai->name);
		goto config_err;
	}

	soc_pcm_apply_msb(substream);

	/* Symmetry only applies if we've already got an active stream. */
	if (cpu_dai->active) {
		ret = soc_pcm_apply_symmetry(substream, cpu_dai);
		if (ret != 0)
			goto config_err;
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		if (rtd->codec_dais[i]->active) {
			ret = soc_pcm_apply_symmetry(substream,
						     rtd->codec_dais[i]);
			if (ret != 0)
				goto config_err;
		}
	}

	pr_debug("ASoC: %s <-> %s info:\n",
			codec_dai_name, cpu_dai->name);
	pr_debug("ASoC: rate mask 0x%x\n", runtime->hw.rates);
	pr_debug("ASoC: min ch %d max ch %d\n", runtime->hw.channels_min,
		 runtime->hw.channels_max);
	pr_debug("ASoC: min rate %d max rate %d\n", runtime->hw.rate_min,
		 runtime->hw.rate_max);

dynamic:

	snd_soc_runtime_activate(rtd, substream->stream);

	mutex_unlock(&rtd->pcm_mutex);
	return 0;

config_err:
	if (rtd->dai_link->ops && rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

machine_err:
	i = rtd->num_codecs;

codec_dai_err:
	while (--i >= 0) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops->shutdown)
			codec_dai->driver->ops->shutdown(substream, codec_dai);
	}

	if (platform->driver->ops && platform->driver->ops->close)
		platform->driver->ops->close(substream);

platform_err:
	if (cpu_dai->driver->ops->shutdown)
		cpu_dai->driver->ops->shutdown(substream, cpu_dai);
out:
	mutex_unlock(&rtd->pcm_mutex);

	pm_runtime_put(platform->dev);
	for (i = 0; i < rtd->num_codecs; i++)
		pm_runtime_put(rtd->codec_dais[i]->dev);
	pm_runtime_put(cpu_dai->dev);
	for (i = 0; i < rtd->num_codecs; i++) {
		if (!rtd->codec_dais[i]->active)
			pinctrl_pm_select_sleep_state(rtd->codec_dais[i]->dev);
	}
	if (!cpu_dai->active)
		pinctrl_pm_select_sleep_state(cpu_dai->dev);

	return ret;
}

/*
 * Power down the audio subsystem pmdown_time msecs after close is called.
 * This is to ensure there are no pops or clicks in between any music tracks
 * due to DAPM power cycling.
 */
static void close_delayed_work(struct work_struct *work)
{
	struct snd_soc_pcm_runtime *rtd =
			container_of(work, struct snd_soc_pcm_runtime, delayed_work.work);
	struct snd_soc_dai *codec_dai = rtd->codec_dais[0];

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	dev_dbg(rtd->dev, "ASoC: pop wq checking: %s status: %s waiting: %s\n",
		 codec_dai->driver->playback.stream_name,
		 codec_dai->playback_active ? "active" : "inactive",
		 rtd->pop_wait ? "yes" : "no");

	/* are we waiting on this codec DAI stream */
	if (rtd->pop_wait == 1) {
		rtd->pop_wait = 0;
		snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_PLAYBACK,
					  SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->pcm_mutex);
}

/*
 * Called by ALSA when a PCM substream is closed. Private data can be
 * freed here. The cpu DAI, codec DAI, machine and platform are also
 * shutdown.
 */
static int soc_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	int i;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	snd_soc_runtime_deactivate(rtd, substream->stream);

	/* clear the corresponding DAIs rate when inactive */
	if (!cpu_dai->active)
		cpu_dai->rate = 0;

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (!codec_dai->active)
			codec_dai->rate = 0;
	}

	if (cpu_dai->driver->ops->shutdown)
		cpu_dai->driver->ops->shutdown(substream, cpu_dai);

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops->shutdown)
			codec_dai->driver->ops->shutdown(substream, codec_dai);
	}

	if (rtd->dai_link->ops && rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

	if (platform->driver->ops && platform->driver->ops->close)
		platform->driver->ops->close(substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (snd_soc_runtime_ignore_pmdown_time(rtd)) {
			/* powered down playback stream now */
			snd_soc_dapm_stream_event(rtd,
						  SNDRV_PCM_STREAM_PLAYBACK,
						  SND_SOC_DAPM_STREAM_STOP);
		} else {
			/* start delayed pop wq here for playback streams */
			rtd->pop_wait = 1;
			queue_delayed_work(system_power_efficient_wq,
					   &rtd->delayed_work,
					   msecs_to_jiffies(rtd->pmdown_time));
		}
	} else {
		/* capture streams can be powered down now */
		snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_CAPTURE,
					  SND_SOC_DAPM_STREAM_STOP);
	}

	mutex_unlock(&rtd->pcm_mutex);

	pm_runtime_put(platform->dev);
	for (i = 0; i < rtd->num_codecs; i++)
		pm_runtime_put(rtd->codec_dais[i]->dev);
	pm_runtime_put(cpu_dai->dev);
	for (i = 0; i < rtd->num_codecs; i++) {
		if (!rtd->codec_dais[i]->active)
			pinctrl_pm_select_sleep_state(rtd->codec_dais[i]->dev);
	}
	if (!cpu_dai->active)
		pinctrl_pm_select_sleep_state(cpu_dai->dev);

	return 0;
}

/*
 * Called by ALSA when the PCM substream is prepared, can set format, sample
 * rate, etc.  This function is non atomic and can be called multiple times,
 * it can refer to the runtime info.
 */
static int soc_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	int i, ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	if (rtd->dai_link->ops && rtd->dai_link->ops->prepare) {
		ret = rtd->dai_link->ops->prepare(substream);
		if (ret < 0) {
			dev_err(rtd->card->dev, "ASoC: machine prepare error:"
				" %d\n", ret);
			goto out;
		}
	}

	if (platform->driver->ops && platform->driver->ops->prepare) {
		ret = platform->driver->ops->prepare(substream);
		if (ret < 0) {
			dev_err(platform->dev, "ASoC: platform prepare error:"
				" %d\n", ret);
			goto out;
		}
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops && codec_dai->driver->ops->prepare) {
			ret = codec_dai->driver->ops->prepare(substream,
							      codec_dai);
			if (ret < 0) {
				dev_err(codec_dai->dev,
					"ASoC: DAI prepare error: %d\n", ret);
				goto out;
			}
		}
	}

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->prepare) {
		ret = cpu_dai->driver->ops->prepare(substream, cpu_dai);
		if (ret < 0) {
			dev_err(cpu_dai->dev, "ASoC: DAI prepare error: %d\n",
				ret);
			goto out;
		}
	}

	/* cancel any delayed stream shutdown that is pending */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    rtd->pop_wait) {
		rtd->pop_wait = 0;
		cancel_delayed_work(&rtd->delayed_work);
	}

	snd_soc_dapm_stream_event(rtd, substream->stream,
			SND_SOC_DAPM_STREAM_START);

	for (i = 0; i < rtd->num_codecs; i++)
		snd_soc_dai_digital_mute(rtd->codec_dais[i], 0,
					 substream->stream);

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

static void soc_pcm_codec_params_fixup(struct snd_pcm_hw_params *params,
				       unsigned int mask)
{
	struct snd_interval *interval;
	int channels = hweight_long(mask);

	interval = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	interval->min = channels;
	interval->max = channels;
}

int soc_dai_hw_params(struct snd_pcm_substream *substream,
		      struct snd_pcm_hw_params *params,
		      struct snd_soc_dai *dai)
{
	int ret;

	if (dai->driver->ops && dai->driver->ops->hw_params) {
		ret = dai->driver->ops->hw_params(substream, params, dai);
		if (ret < 0) {
			dev_err(dai->dev, "ASoC: can't set %s hw params: %d\n",
				dai->name, ret);
			return ret;
		}
	}

	return 0;
}

/*
 * Called by ALSA when the hardware params are set by application. This
 * function can also be called multiple times and can allocate buffers
 * (using snd_pcm_lib_* ). It's non-atomic.
 */
static int soc_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int i, ret = 0;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	ret = soc_pcm_params_symmetry(substream, params);
	if (ret)
		goto out;

	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_params) {
		ret = rtd->dai_link->ops->hw_params(substream, params);
		if (ret < 0) {
			dev_err(rtd->card->dev, "ASoC: machine hw_params"
				" failed: %d\n", ret);
			goto out;
		}
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];
		struct snd_pcm_hw_params codec_params;

		/* copy params for each codec */
		codec_params = *params;

		/* fixup params based on TDM slot masks */
		if (codec_dai->tx_mask)
			soc_pcm_codec_params_fixup(&codec_params,
						   codec_dai->tx_mask);
		if (codec_dai->rx_mask)
			soc_pcm_codec_params_fixup(&codec_params,
						   codec_dai->rx_mask);

		ret = soc_dai_hw_params(substream, &codec_params, codec_dai);
		if(ret < 0)
			goto codec_err;

		codec_dai->rate = params_rate(&codec_params);
		codec_dai->channels = params_channels(&codec_params);
		codec_dai->sample_bits = snd_pcm_format_physical_width(
						params_format(&codec_params));
	}

	ret = soc_dai_hw_params(substream, params, cpu_dai);
	if (ret < 0)
		goto interface_err;

	if (platform->driver->ops && platform->driver->ops->hw_params) {
		ret = platform->driver->ops->hw_params(substream, params);
		if (ret < 0) {
			dev_err(platform->dev, "ASoC: %s hw params failed: %d\n",
			       platform->component.name, ret);
			goto platform_err;
		}
	}

	/* store the parameters for each DAIs */
	cpu_dai->rate = params_rate(params);
	cpu_dai->channels = params_channels(params);
	cpu_dai->sample_bits =
		snd_pcm_format_physical_width(params_format(params));

out:
	mutex_unlock(&rtd->pcm_mutex);
	return ret;

platform_err:
	if (cpu_dai->driver->ops && cpu_dai->driver->ops->hw_free)
		cpu_dai->driver->ops->hw_free(substream, cpu_dai);

interface_err:
	i = rtd->num_codecs;

codec_err:
	while (--i >= 0) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops && codec_dai->driver->ops->hw_free)
			codec_dai->driver->ops->hw_free(substream, codec_dai);
		codec_dai->rate = 0;
	}

	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);

	mutex_unlock(&rtd->pcm_mutex);
	return ret;
}

/*
 * Frees resources allocated by hw_params, can be called multiple times
 */
static int soc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	bool playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int i;

	mutex_lock_nested(&rtd->pcm_mutex, rtd->pcm_subclass);

	/* clear the corresponding DAIs parameters when going to be inactive */
	if (cpu_dai->active == 1) {
		cpu_dai->rate = 0;
		cpu_dai->channels = 0;
		cpu_dai->sample_bits = 0;
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->active == 1) {
			codec_dai->rate = 0;
			codec_dai->channels = 0;
			codec_dai->sample_bits = 0;
		}
	}

	/* apply codec digital mute */
	for (i = 0; i < rtd->num_codecs; i++) {
		if ((playback && rtd->codec_dais[i]->playback_active == 1) ||
		    (!playback && rtd->codec_dais[i]->capture_active == 1))
			snd_soc_dai_digital_mute(rtd->codec_dais[i], 1,
						 substream->stream);
	}

	/* free any machine hw params */
	if (rtd->dai_link->ops && rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);

	/* free any DMA resources */
	if (platform->driver->ops && platform->driver->ops->hw_free)
		platform->driver->ops->hw_free(substream);

	/* now free hw params for the DAIs  */
	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops && codec_dai->driver->ops->hw_free)
			codec_dai->driver->ops->hw_free(substream, codec_dai);
	}

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->hw_free)
		cpu_dai->driver->ops->hw_free(substream, cpu_dai);

	mutex_unlock(&rtd->pcm_mutex);
	return 0;
}

static int soc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	int i, ret;

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops && codec_dai->driver->ops->trigger) {
			ret = codec_dai->driver->ops->trigger(substream,
							      cmd, codec_dai);
			if (ret < 0)
				return ret;
		}
	}

	if (platform->driver->ops && platform->driver->ops->trigger) {
		ret = platform->driver->ops->trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->trigger) {
		ret = cpu_dai->driver->ops->trigger(substream, cmd, cpu_dai);
		if (ret < 0)
			return ret;
	}

	if (rtd->dai_link->ops && rtd->dai_link->ops->trigger) {
		ret = rtd->dai_link->ops->trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int soc_pcm_bespoke_trigger(struct snd_pcm_substream *substream,
				   int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	int i, ret;

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops &&
		    codec_dai->driver->ops->bespoke_trigger) {
			ret = codec_dai->driver->ops->bespoke_trigger(substream,
								cmd, codec_dai);
			if (ret < 0)
				return ret;
		}
	}

	if (platform->driver->bespoke_trigger) {
		ret = platform->driver->bespoke_trigger(substream, cmd);
		if (ret < 0)
			return ret;
	}

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->bespoke_trigger) {
		ret = cpu_dai->driver->ops->bespoke_trigger(substream, cmd, cpu_dai);
		if (ret < 0)
			return ret;
	}
	return 0;
}
/*
 * soc level wrapper for pointer callback
 * If cpu_dai, codec_dai, platform driver has the delay callback, than
 * the runtime->delay will be updated accordingly.
 */
static snd_pcm_uframes_t soc_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai;
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t offset = 0;
	snd_pcm_sframes_t delay = 0;
	snd_pcm_sframes_t codec_delay = 0;
	int i;

	if (platform->driver->ops && platform->driver->ops->pointer)
		offset = platform->driver->ops->pointer(substream);

	if (cpu_dai->driver->ops && cpu_dai->driver->ops->delay)
		delay += cpu_dai->driver->ops->delay(substream, cpu_dai);

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		if (codec_dai->driver->ops && codec_dai->driver->ops->delay)
			codec_delay = max(codec_delay,
					codec_dai->driver->ops->delay(substream,
								    codec_dai));
	}
	delay += codec_delay;

	/*
	 * None of the existing platform drivers implement delay(), so
	 * for now the codec_dai of first multicodec entry is used
	 */
	if (platform->driver->delay)
		delay += platform->driver->delay(substream, rtd->codec_dais[0]);

	runtime->delay = delay;

	return offset;
}

/* connect a FE and BE */
static int dpcm_be_connect(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	struct snd_soc_dpcm *dpcm;

	/* only add new dpcms */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {
		if (dpcm->be == be && dpcm->fe == fe)
			return 0;
	}

	dpcm = kzalloc(sizeof(struct snd_soc_dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;

	dpcm->be = be;
	dpcm->fe = fe;
	be->dpcm[stream].runtime = fe->dpcm[stream].runtime;
	dpcm->state = SND_SOC_DPCM_LINK_STATE_NEW;
	list_add(&dpcm->list_be, &fe->dpcm[stream].be_clients);
	list_add(&dpcm->list_fe, &be->dpcm[stream].fe_clients);

	dev_dbg(fe->dev, "connected new DPCM %s path %s %s %s\n",
			stream ? "capture" : "playback",  fe->dai_link->name,
			stream ? "<-" : "->", be->dai_link->name);

#ifdef CONFIG_DEBUG_FS
	dpcm->debugfs_state = debugfs_create_u32(be->dai_link->name, 0644,
			fe->debugfs_dpcm_root, &dpcm->state);
#endif
	return 1;
}

/* reparent a BE onto another FE */
static void dpcm_be_reparent(struct snd_soc_pcm_runtime *fe,
			struct snd_soc_pcm_runtime *be, int stream)
{
	struct snd_soc_dpcm *dpcm;
	struct snd_pcm_substream *fe_substream, *be_substream;

	/* reparent if BE is connected to other FEs */
	if (!be->dpcm[stream].users)
		return;

	be_substream = snd_soc_dpcm_get_substream(be, stream);

	list_for_each_entry(dpcm, &be->dpcm[stream].fe_clients, list_fe) {
		if (dpcm->fe == fe)
			continue;

		dev_dbg(fe->dev, "reparent %s path %s %s %s\n",
			stream ? "capture" : "playback",
			dpcm->fe->dai_link->name,
			stream ? "<-" : "->", dpcm->be->dai_link->name);

		fe_substream = snd_soc_dpcm_get_substream(dpcm->fe, stream);
		be_substream->runtime = fe_substream->runtime;
		break;
	}
}

/* disconnect a BE and FE */
void dpcm_be_disconnect(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm, *d;

	list_for_each_entry_safe(dpcm, d, &fe->dpcm[stream].be_clients, list_be) {
		dev_dbg(fe->dev, "ASoC: BE %s disconnect check for %s\n",
				stream ? "capture" : "playback",
				dpcm->be->dai_link->name);

		if (dpcm->state != SND_SOC_DPCM_LINK_STATE_FREE)
			continue;

		dev_dbg(fe->dev, "freed DSP %s path %s %s %s\n",
			stream ? "capture" : "playback", fe->dai_link->name,
			stream ? "<-" : "->", dpcm->be->dai_link->name);

		/* BEs still alive need new FE */
		dpcm_be_reparent(fe, dpcm->be, stream);

#ifdef CONFIG_DEBUG_FS
		debugfs_remove(dpcm->debugfs_state);
#endif
		list_del(&dpcm->list_be);
		list_del(&dpcm->list_fe);
		kfree(dpcm);
	}
}

/* get BE for DAI widget and stream */
static struct snd_soc_pcm_runtime *dpcm_get_be(struct snd_soc_card *card,
		struct snd_soc_dapm_widget *widget, int stream)
{
	struct snd_soc_pcm_runtime *be;
	int i, j;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		for (i = 0; i < card->num_links; i++) {
			be = &card->rtd[i];

			if (!be->dai_link->no_pcm)
				continue;

			if (be->cpu_dai->playback_widget == widget)
				return be;

			for (j = 0; j < be->num_codecs; j++) {
				struct snd_soc_dai *dai = be->codec_dais[j];
				if (dai->playback_widget == widget)
					return be;
			}
		}
	} else {

		for (i = 0; i < card->num_links; i++) {
			be = &card->rtd[i];

			if (!be->dai_link->no_pcm)
				continue;

			if (be->cpu_dai->capture_widget == widget)
				return be;

			for (j = 0; j < be->num_codecs; j++) {
				struct snd_soc_dai *dai = be->codec_dais[j];
				if (dai->capture_widget == widget)
					return be;
			}
		}
	}

	dev_err(card->dev, "ASoC: can't get %s BE for %s\n",
		stream ? "capture" : "playback", widget->name);
	return NULL;
}

static inline struct snd_soc_dapm_widget *
	dai_get_widget(struct snd_soc_dai *dai, int stream)
{
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		return dai->playback_widget;
	else
		return dai->capture_widget;
}

static int widget_in_list(struct snd_soc_dapm_widget_list *list,
		struct snd_soc_dapm_widget *widget)
{
	int i;

	for (i = 0; i < list->num_widgets; i++) {
		if (widget == list->widgets[i])
			return 1;
	}

	return 0;
}

int dpcm_path_get(struct snd_soc_pcm_runtime *fe,
	int stream, struct snd_soc_dapm_widget_list **list_)
{
	struct snd_soc_dai *cpu_dai = fe->cpu_dai;
	struct snd_soc_dapm_widget_list *list;
	int paths;

	list = kzalloc(sizeof(struct snd_soc_dapm_widget_list) +
			sizeof(struct snd_soc_dapm_widget *), GFP_KERNEL);
	if (list == NULL)
		return -ENOMEM;

	/* get number of valid DAI paths and their widgets */
	paths = snd_soc_dapm_dai_get_connected_widgets(cpu_dai, stream, &list);

	dev_dbg(fe->dev, "ASoC: found %d audio %s paths\n", paths,
			stream ? "capture" : "playback");

	*list_ = list;
	return paths;
}

static int dpcm_prune_paths(struct snd_soc_pcm_runtime *fe, int stream,
	struct snd_soc_dapm_widget_list **list_)
{
	struct snd_soc_dpcm *dpcm;
	struct snd_soc_dapm_widget_list *list = *list_;
	struct snd_soc_dapm_widget *widget;
	int prune = 0;

	/* Destroy any old FE <--> BE connections */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {
		unsigned int i;

		/* is there a valid CPU DAI widget for this BE */
		widget = dai_get_widget(dpcm->be->cpu_dai, stream);

		/* prune the BE if it's no longer in our active list */
		if (widget && widget_in_list(list, widget))
			continue;

		/* is there a valid CODEC DAI widget for this BE */
		for (i = 0; i < dpcm->be->num_codecs; i++) {
			struct snd_soc_dai *dai = dpcm->be->codec_dais[i];
			widget = dai_get_widget(dai, stream);

			/* prune the BE if it's no longer in our active list */
			if (widget && widget_in_list(list, widget))
				continue;
		}

		dev_dbg(fe->dev, "ASoC: pruning %s BE %s for %s\n",
			stream ? "capture" : "playback",
			dpcm->be->dai_link->name, fe->dai_link->name);
		dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;
		dpcm->be->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_BE;
		prune++;
	}

	dev_dbg(fe->dev, "ASoC: found %d old BE paths for pruning\n", prune);
	return prune;
}

static int dpcm_add_paths(struct snd_soc_pcm_runtime *fe, int stream,
	struct snd_soc_dapm_widget_list **list_)
{
	struct snd_soc_card *card = fe->card;
	struct snd_soc_dapm_widget_list *list = *list_;
	struct snd_soc_pcm_runtime *be;
	int i, new = 0, err;

	/* Create any new FE <--> BE connections */
	for (i = 0; i < list->num_widgets; i++) {

		switch (list->widgets[i]->id) {
		case snd_soc_dapm_dai_in:
		case snd_soc_dapm_dai_out:
			break;
		default:
			continue;
		}

		/* is there a valid BE rtd for this widget */
		be = dpcm_get_be(card, list->widgets[i], stream);
		if (!be) {
			dev_err(fe->dev, "ASoC: no BE found for %s\n",
					list->widgets[i]->name);
			continue;
		}

		/* make sure BE is a real BE */
		if (!be->dai_link->no_pcm)
			continue;

		/* don't connect if FE is not running */
		if (!fe->dpcm[stream].runtime && !fe->fe_compr)
			continue;

		/* newly connected FE and BE */
		err = dpcm_be_connect(fe, be, stream);
		if (err < 0) {
			dev_err(fe->dev, "ASoC: can't connect %s\n",
				list->widgets[i]->name);
			break;
		} else if (err == 0) /* already connected */
			continue;

		/* new */
		be->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_BE;
		new++;
	}

	dev_dbg(fe->dev, "ASoC: found %d new BE paths\n", new);
	return new;
}

/*
 * Find the corresponding BE DAIs that source or sink audio to this
 * FE substream.
 */
int dpcm_process_paths(struct snd_soc_pcm_runtime *fe,
	int stream, struct snd_soc_dapm_widget_list **list, int new)
{
	if (new)
		return dpcm_add_paths(fe, stream, list);
	else
		return dpcm_prune_paths(fe, stream, list);
}

void dpcm_clear_pending_state(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;

	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be)
		dpcm->be->dpcm[stream].runtime_update =
						SND_SOC_DPCM_UPDATE_NO;
}

static void dpcm_be_dai_startup_unwind(struct snd_soc_pcm_runtime *fe,
	int stream)
{
	struct snd_soc_dpcm *dpcm;

	/* disable any enabled and non active backends */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		if (be->dpcm[stream].users == 0)
			dev_err(be->dev, "ASoC: no users %s at close - state %d\n",
				stream ? "capture" : "playback",
				be->dpcm[stream].state);

		if (--be->dpcm[stream].users != 0)
			continue;

		if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN)
			continue;

		soc_pcm_close(be_substream);
		be_substream->runtime = NULL;
		be->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	}
}

int dpcm_be_dai_startup(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;
	int err, count = 0;

	/* only startup BE DAIs that are either sinks or sources to this FE DAI */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		if (!be_substream) {
			dev_err(be->dev, "ASoC: no backend %s stream\n",
				stream ? "capture" : "playback");
			continue;
		}

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		/* first time the dpcm is open ? */
		if (be->dpcm[stream].users == DPCM_MAX_BE_USERS)
			dev_err(be->dev, "ASoC: too many users %s at open %d\n",
				stream ? "capture" : "playback",
				be->dpcm[stream].state);

		if (be->dpcm[stream].users++ != 0)
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_NEW) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_CLOSE))
			continue;

		dev_dbg(be->dev, "ASoC: open %s BE %s\n",
			stream ? "capture" : "playback", be->dai_link->name);

		be_substream->runtime = be->dpcm[stream].runtime;
		err = soc_pcm_open(be_substream);
		if (err < 0) {
			dev_err(be->dev, "ASoC: BE open failed %d\n", err);
			be->dpcm[stream].users--;
			if (be->dpcm[stream].users < 0)
				dev_err(be->dev, "ASoC: no users %s at unwind %d\n",
					stream ? "capture" : "playback",
					be->dpcm[stream].state);

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
			goto unwind;
		}

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_OPEN;
		count++;
	}

	return count;

unwind:
	/* disable any enabled and non active backends */
	list_for_each_entry_continue_reverse(dpcm, &fe->dpcm[stream].be_clients, list_be) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		if (be->dpcm[stream].users == 0)
			dev_err(be->dev, "ASoC: no users %s at close %d\n",
				stream ? "capture" : "playback",
				be->dpcm[stream].state);

		if (--be->dpcm[stream].users != 0)
			continue;

		if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN)
			continue;

		soc_pcm_close(be_substream);
		be_substream->runtime = NULL;
		be->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	}

	return err;
}

static void dpcm_init_runtime_hw(struct snd_pcm_runtime *runtime,
	struct snd_soc_pcm_stream *stream)
{
	runtime->hw.rate_min = stream->rate_min;
	runtime->hw.rate_max = stream->rate_max;
	runtime->hw.channels_min = stream->channels_min;
	runtime->hw.channels_max = stream->channels_max;
	if (runtime->hw.formats)
		runtime->hw.formats &= stream->formats;
	else
		runtime->hw.formats = stream->formats;
	runtime->hw.rates = stream->rates;
}

static void dpcm_set_fe_runtime(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai_driver *cpu_dai_drv = cpu_dai->driver;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dpcm_init_runtime_hw(runtime, &cpu_dai_drv->playback);
	else
		dpcm_init_runtime_hw(runtime, &cpu_dai_drv->capture);
}

static int dpcm_fe_dai_do_trigger(struct snd_pcm_substream *substream, int cmd);

/* Set FE's runtime_update state; the state is protected via PCM stream lock
 * for avoiding the race with trigger callback.
 * If the state is unset and a trigger is pending while the previous operation,
 * process the pending trigger action here.
 */
static void dpcm_set_fe_update_state(struct snd_soc_pcm_runtime *fe,
				     int stream, enum snd_soc_dpcm_update state)
{
	struct snd_pcm_substream *substream =
		snd_soc_dpcm_get_substream(fe, stream);

	snd_pcm_stream_lock_irq(substream);
	if (state == SND_SOC_DPCM_UPDATE_NO && fe->dpcm[stream].trigger_pending) {
		dpcm_fe_dai_do_trigger(substream,
				       fe->dpcm[stream].trigger_pending - 1);
		fe->dpcm[stream].trigger_pending = 0;
	}
	fe->dpcm[stream].runtime_update = state;
	snd_pcm_stream_unlock_irq(substream);
}

static int dpcm_fe_dai_startup(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = fe_substream->private_data;
	struct snd_pcm_runtime *runtime = fe_substream->runtime;
	int stream = fe_substream->stream, ret = 0;

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	ret = dpcm_be_dai_startup(fe, fe_substream->stream);
	if (ret < 0) {
		dev_err(fe->dev,"ASoC: failed to start some BEs %d\n", ret);
		goto be_err;
	}

	dev_dbg(fe->dev, "ASoC: open FE %s\n", fe->dai_link->name);

	/* start the DAI frontend */
	ret = soc_pcm_open(fe_substream);
	if (ret < 0) {
		dev_err(fe->dev,"ASoC: failed to start FE %d\n", ret);
		goto unwind;
	}

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_OPEN;

	dpcm_set_fe_runtime(fe_substream);
	snd_pcm_limit_hw_rates(runtime);

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	return 0;

unwind:
	dpcm_be_dai_startup_unwind(fe, fe_substream->stream);
be_err:
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	return ret;
}

int dpcm_be_dai_shutdown(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;

	/* only shutdown BEs that are either sinks or sources to this FE DAI */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		if (be->dpcm[stream].users == 0)
			dev_err(be->dev, "ASoC: no users %s at close - state %d\n",
				stream ? "capture" : "playback",
				be->dpcm[stream].state);

		if (--be->dpcm[stream].users != 0)
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_FREE) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN))
			continue;

		dev_dbg(be->dev, "ASoC: close BE %s\n",
			dpcm->fe->dai_link->name);

		soc_pcm_close(be_substream);
		be_substream->runtime = NULL;

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	}
	return 0;
}

static int dpcm_fe_dai_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int stream = substream->stream;

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	/* shutdown the BEs */
	dpcm_be_dai_shutdown(fe, substream->stream);

	dev_dbg(fe->dev, "ASoC: close FE %s\n", fe->dai_link->name);

	/* now shutdown the frontend */
	soc_pcm_close(substream);

	/* run the stream event for each BE */
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_STOP);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	return 0;
}

int dpcm_be_dai_hw_free(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;

	/* only hw_params backends that are either sinks or sources
	 * to this frontend DAI */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		/* only free hw when no longer used - check all FEs */
		if (!snd_soc_dpcm_can_be_free_stop(fe, be, stream))
				continue;

		/* do not free hw if this BE is used by other FE */
		if (be->dpcm[stream].users > 1)
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PREPARE) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_FREE) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PAUSED) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP))
			continue;

		dev_dbg(be->dev, "ASoC: hw_free BE %s\n",
			dpcm->fe->dai_link->name);

		soc_pcm_hw_free(be_substream);

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_FREE;
	}

	return 0;
}

static int dpcm_fe_dai_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int err, stream = substream->stream;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	dev_dbg(fe->dev, "ASoC: hw_free FE %s\n", fe->dai_link->name);

	/* call hw_free on the frontend */
	err = soc_pcm_hw_free(substream);
	if (err < 0)
		dev_err(fe->dev,"ASoC: hw_free FE %s failed\n",
			fe->dai_link->name);

	/* only hw_params backends that are either sinks or sources
	 * to this frontend DAI */
	err = dpcm_be_dai_hw_free(fe, stream);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_FREE;
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);

	mutex_unlock(&fe->card->mutex);
	return 0;
}

int dpcm_be_dai_hw_params(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;
	int ret;

	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		/* only allow hw_params() if no connected FEs are running */
		if (!snd_soc_dpcm_can_be_params(fe, be, stream))
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_FREE))
			continue;

		dev_dbg(be->dev, "ASoC: hw_params BE %s\n",
			dpcm->fe->dai_link->name);

		/* copy params for each dpcm */
		memcpy(&dpcm->hw_params, &fe->dpcm[stream].hw_params,
				sizeof(struct snd_pcm_hw_params));

		/* perform any hw_params fixups */
		if (be->dai_link->be_hw_params_fixup) {
			ret = be->dai_link->be_hw_params_fixup(be,
					&dpcm->hw_params);
			if (ret < 0) {
				dev_err(be->dev,
					"ASoC: hw_params BE fixup failed %d\n",
					ret);
				goto unwind;
			}
		}

		ret = soc_pcm_hw_params(be_substream, &dpcm->hw_params);
		if (ret < 0) {
			dev_err(dpcm->be->dev,
				"ASoC: hw_params BE failed %d\n", ret);
			goto unwind;
		}

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_PARAMS;
	}
	return 0;

unwind:
	/* disable any enabled and non active backends */
	list_for_each_entry_continue_reverse(dpcm, &fe->dpcm[stream].be_clients, list_be) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		/* only allow hw_free() if no connected FEs are running */
		if (!snd_soc_dpcm_can_be_free_stop(fe, be, stream))
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN) &&
		   (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_PARAMS) &&
		   (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_FREE) &&
		   (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP))
			continue;

		soc_pcm_hw_free(be_substream);
	}

	return ret;
}

static int dpcm_fe_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int ret, stream = substream->stream;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	memcpy(&fe->dpcm[substream->stream].hw_params, params,
			sizeof(struct snd_pcm_hw_params));
	ret = dpcm_be_dai_hw_params(fe, substream->stream);
	if (ret < 0) {
		dev_err(fe->dev,"ASoC: hw_params BE failed %d\n", ret);
		goto out;
	}

	dev_dbg(fe->dev, "ASoC: hw_params FE %s rate %d chan %x fmt %d\n",
			fe->dai_link->name, params_rate(params),
			params_channels(params), params_format(params));

	/* call hw_params on the frontend */
	ret = soc_pcm_hw_params(substream, params);
	if (ret < 0) {
		dev_err(fe->dev,"ASoC: hw_params FE failed %d\n", ret);
		dpcm_be_dai_hw_free(fe, stream);
	 } else
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_PARAMS;

out:
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	mutex_unlock(&fe->card->mutex);
	return ret;
}

static int dpcm_do_trigger(struct snd_soc_dpcm *dpcm,
		struct snd_pcm_substream *substream, int cmd)
{
	int ret;

	dev_dbg(dpcm->be->dev, "ASoC: trigger BE %s cmd %d\n",
			dpcm->fe->dai_link->name, cmd);

	ret = soc_pcm_trigger(substream, cmd);
	if (ret < 0)
		dev_err(dpcm->be->dev,"ASoC: trigger BE failed %d\n", ret);

	return ret;
}

int dpcm_be_dai_trigger(struct snd_soc_pcm_runtime *fe, int stream,
			       int cmd)
{
	struct snd_soc_dpcm *dpcm;
	int ret = 0;

	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_PREPARE) &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP))
				continue;

			ret = dpcm_do_trigger(dpcm, be_substream, cmd);
			if (ret)
				return ret;

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
			break;
		case SNDRV_PCM_TRIGGER_RESUME:
			if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_SUSPEND))
				continue;

			ret = dpcm_do_trigger(dpcm, be_substream, cmd);
			if (ret)
				return ret;

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_PAUSED))
				continue;

			ret = dpcm_do_trigger(dpcm, be_substream, cmd);
			if (ret)
				return ret;

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
			break;
		case SNDRV_PCM_TRIGGER_STOP:
			if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START)
				continue;

			if (!snd_soc_dpcm_can_be_free_stop(fe, be, stream))
				continue;

			ret = dpcm_do_trigger(dpcm, be_substream, cmd);
			if (ret)
				return ret;

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_STOP;
			break;
		case SNDRV_PCM_TRIGGER_SUSPEND:
			if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START)
				continue;

			if (!snd_soc_dpcm_can_be_free_stop(fe, be, stream))
				continue;

			ret = dpcm_do_trigger(dpcm, be_substream, cmd);
			if (ret)
				return ret;

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_SUSPEND;
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START)
				continue;

			if (!snd_soc_dpcm_can_be_free_stop(fe, be, stream))
				continue;

			ret = dpcm_do_trigger(dpcm, be_substream, cmd);
			if (ret)
				return ret;

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_PAUSED;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(dpcm_be_dai_trigger);

static int dpcm_fe_dai_do_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int stream = substream->stream, ret;
	enum snd_soc_dpcm_trigger trigger = fe->dai_link->trigger[stream];

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	switch (trigger) {
	case SND_SOC_DPCM_TRIGGER_PRE:
		/* call trigger on the frontend before the backend. */

		dev_dbg(fe->dev, "ASoC: pre trigger FE %s cmd %d\n",
				fe->dai_link->name, cmd);

		ret = soc_pcm_trigger(substream, cmd);
		if (ret < 0) {
			dev_err(fe->dev,"ASoC: trigger FE failed %d\n", ret);
			goto out;
		}

		ret = dpcm_be_dai_trigger(fe, substream->stream, cmd);
		break;
	case SND_SOC_DPCM_TRIGGER_POST:
		/* call trigger on the frontend after the backend. */

		ret = dpcm_be_dai_trigger(fe, substream->stream, cmd);
		if (ret < 0) {
			dev_err(fe->dev,"ASoC: trigger FE failed %d\n", ret);
			goto out;
		}

		dev_dbg(fe->dev, "ASoC: post trigger FE %s cmd %d\n",
				fe->dai_link->name, cmd);

		ret = soc_pcm_trigger(substream, cmd);
		break;
	case SND_SOC_DPCM_TRIGGER_BESPOKE:
		/* bespoke trigger() - handles both FE and BEs */

		dev_dbg(fe->dev, "ASoC: bespoke trigger FE %s cmd %d\n",
				fe->dai_link->name, cmd);

		ret = soc_pcm_bespoke_trigger(substream, cmd);
		if (ret < 0) {
			dev_err(fe->dev,"ASoC: trigger FE failed %d\n", ret);
			goto out;
		}
		break;
	default:
		dev_err(fe->dev, "ASoC: invalid trigger cmd %d for %s\n", cmd,
				fe->dai_link->name);
		ret = -EINVAL;
		goto out;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_STOP;
		break;
	}

out:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	return ret;
}

static int dpcm_fe_dai_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int stream = substream->stream;

	/* if FE's runtime_update is already set, we're in race;
	 * process this trigger later at exit
	 */
	if (fe->dpcm[stream].runtime_update != SND_SOC_DPCM_UPDATE_NO) {
		fe->dpcm[stream].trigger_pending = cmd + 1;
		return 0; /* delayed, assuming it's successful */
	}

	/* we're alone, let's trigger */
	return dpcm_fe_dai_do_trigger(substream, cmd);
}

int dpcm_be_dai_prepare(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;
	int ret = 0;

	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP))
			continue;

		dev_dbg(be->dev, "ASoC: prepare BE %s\n",
			dpcm->fe->dai_link->name);

		ret = soc_pcm_prepare(be_substream);
		if (ret < 0) {
			dev_err(be->dev, "ASoC: backend prepare failed %d\n",
				ret);
			break;
		}

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_PREPARE;
	}
	return ret;
}

static int dpcm_fe_dai_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int stream = substream->stream, ret = 0;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);

	dev_dbg(fe->dev, "ASoC: prepare FE %s\n", fe->dai_link->name);

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	/* there is no point preparing this FE if there are no BEs */
	if (list_empty(&fe->dpcm[stream].be_clients)) {
		dev_err(fe->dev, "ASoC: no backend DAIs enabled for %s\n",
				fe->dai_link->name);
		ret = -EINVAL;
		goto out;
	}

	ret = dpcm_be_dai_prepare(fe, substream->stream);
	if (ret < 0)
		goto out;

	/* call prepare on the frontend */
	ret = soc_pcm_prepare(substream);
	if (ret < 0) {
		dev_err(fe->dev,"ASoC: prepare FE %s failed\n",
			fe->dai_link->name);
		goto out;
	}

	/* run the stream event for each BE */
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_START);
	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_PREPARE;

out:
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	mutex_unlock(&fe->card->mutex);

	return ret;
}

static int soc_pcm_ioctl(struct snd_pcm_substream *substream,
		     unsigned int cmd, void *arg)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_platform *platform = rtd->platform;

	if (platform->driver->ops && platform->driver->ops->ioctl)
		return platform->driver->ops->ioctl(substream, cmd, arg);
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int dpcm_run_update_shutdown(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_pcm_substream *substream =
		snd_soc_dpcm_get_substream(fe, stream);
	enum snd_soc_dpcm_trigger trigger = fe->dai_link->trigger[stream];
	int err;

	dev_dbg(fe->dev, "ASoC: runtime %s close on FE %s\n",
			stream ? "capture" : "playback", fe->dai_link->name);

	if (trigger == SND_SOC_DPCM_TRIGGER_BESPOKE) {
		/* call bespoke trigger - FE takes care of all BE triggers */
		dev_dbg(fe->dev, "ASoC: bespoke trigger FE %s cmd stop\n",
				fe->dai_link->name);

		err = soc_pcm_bespoke_trigger(substream, SNDRV_PCM_TRIGGER_STOP);
		if (err < 0)
			dev_err(fe->dev,"ASoC: trigger FE failed %d\n", err);
	} else {
		dev_dbg(fe->dev, "ASoC: trigger FE %s cmd stop\n",
			fe->dai_link->name);

		err = dpcm_be_dai_trigger(fe, stream, SNDRV_PCM_TRIGGER_STOP);
		if (err < 0)
			dev_err(fe->dev,"ASoC: trigger FE failed %d\n", err);
	}

	err = dpcm_be_dai_hw_free(fe, stream);
	if (err < 0)
		dev_err(fe->dev,"ASoC: hw_free FE failed %d\n", err);

	err = dpcm_be_dai_shutdown(fe, stream);
	if (err < 0)
		dev_err(fe->dev,"ASoC: shutdown FE failed %d\n", err);

	/* run the stream event for each BE */
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_NOP);

	return 0;
}

static int dpcm_run_update_startup(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_pcm_substream *substream =
		snd_soc_dpcm_get_substream(fe, stream);
	struct snd_soc_dpcm *dpcm;
	enum snd_soc_dpcm_trigger trigger = fe->dai_link->trigger[stream];
	int ret;

	dev_dbg(fe->dev, "ASoC: runtime %s open on FE %s\n",
			stream ? "capture" : "playback", fe->dai_link->name);

	/* Only start the BE if the FE is ready */
	if (fe->dpcm[stream].state == SND_SOC_DPCM_STATE_HW_FREE ||
		fe->dpcm[stream].state == SND_SOC_DPCM_STATE_CLOSE)
		return -EINVAL;

	/* startup must always be called for new BEs */
	ret = dpcm_be_dai_startup(fe, stream);
	if (ret < 0)
		goto disconnect;

	/* keep going if FE state is > open */
	if (fe->dpcm[stream].state == SND_SOC_DPCM_STATE_OPEN)
		return 0;

	ret = dpcm_be_dai_hw_params(fe, stream);
	if (ret < 0)
		goto close;

	/* keep going if FE state is > hw_params */
	if (fe->dpcm[stream].state == SND_SOC_DPCM_STATE_HW_PARAMS)
		return 0;


	ret = dpcm_be_dai_prepare(fe, stream);
	if (ret < 0)
		goto hw_free;

	/* run the stream event for each BE */
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_NOP);

	/* keep going if FE state is > prepare */
	if (fe->dpcm[stream].state == SND_SOC_DPCM_STATE_PREPARE ||
		fe->dpcm[stream].state == SND_SOC_DPCM_STATE_STOP)
		return 0;

	if (trigger == SND_SOC_DPCM_TRIGGER_BESPOKE) {
		/* call trigger on the frontend - FE takes care of all BE triggers */
		dev_dbg(fe->dev, "ASoC: bespoke trigger FE %s cmd start\n",
				fe->dai_link->name);

		ret = soc_pcm_bespoke_trigger(substream, SNDRV_PCM_TRIGGER_START);
		if (ret < 0) {
			dev_err(fe->dev,"ASoC: bespoke trigger FE failed %d\n", ret);
			goto hw_free;
		}
	} else {
		dev_dbg(fe->dev, "ASoC: trigger FE %s cmd start\n",
			fe->dai_link->name);

		ret = dpcm_be_dai_trigger(fe, stream,
					SNDRV_PCM_TRIGGER_START);
		if (ret < 0) {
			dev_err(fe->dev,"ASoC: trigger FE failed %d\n", ret);
			goto hw_free;
		}
	}

	return 0;

hw_free:
	dpcm_be_dai_hw_free(fe, stream);
close:
	dpcm_be_dai_shutdown(fe, stream);
disconnect:
	/* disconnect any non started BEs */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START)
				dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;
	}

	return ret;
}

static int dpcm_run_new_update(struct snd_soc_pcm_runtime *fe, int stream)
{
	int ret;

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_BE);
	ret = dpcm_run_update_startup(fe, stream);
	if (ret < 0)
		dev_err(fe->dev, "ASoC: failed to startup some BEs\n");
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);

	return ret;
}

static int dpcm_run_old_update(struct snd_soc_pcm_runtime *fe, int stream)
{
	int ret;

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_BE);
	ret = dpcm_run_update_shutdown(fe, stream);
	if (ret < 0)
		dev_err(fe->dev, "ASoC: failed to shutdown some BEs\n");
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);

	return ret;
}

/* Called by DAPM mixer/mux changes to update audio routing between PCMs and
 * any DAI links.
 */
int soc_dpcm_runtime_update(struct snd_soc_card *card)
{
	int i, old, new, paths;

	mutex_lock_nested(&card->mutex, SND_SOC_CARD_CLASS_RUNTIME);
	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_dapm_widget_list *list;
		struct snd_soc_pcm_runtime *fe = &card->rtd[i];

		/* make sure link is FE */
		if (!fe->dai_link->dynamic)
			continue;

		/* only check active links */
		if (!fe->cpu_dai->active)
			continue;

		/* DAPM sync will call this to update DSP paths */
		dev_dbg(fe->dev, "ASoC: DPCM runtime update for FE %s\n",
			fe->dai_link->name);

		/* skip if FE doesn't have playback capability */
		if (!fe->cpu_dai->driver->playback.channels_min)
			goto capture;

		paths = dpcm_path_get(fe, SNDRV_PCM_STREAM_PLAYBACK, &list);
		if (paths < 0) {
			dev_warn(fe->dev, "ASoC: %s no valid %s path\n",
					fe->dai_link->name,  "playback");
			mutex_unlock(&card->mutex);
			return paths;
		}

		/* update any new playback paths */
		new = dpcm_process_paths(fe, SNDRV_PCM_STREAM_PLAYBACK, &list, 1);
		if (new) {
			dpcm_run_new_update(fe, SNDRV_PCM_STREAM_PLAYBACK);
			dpcm_clear_pending_state(fe, SNDRV_PCM_STREAM_PLAYBACK);
			dpcm_be_disconnect(fe, SNDRV_PCM_STREAM_PLAYBACK);
		}

		/* update any old playback paths */
		old = dpcm_process_paths(fe, SNDRV_PCM_STREAM_PLAYBACK, &list, 0);
		if (old) {
			dpcm_run_old_update(fe, SNDRV_PCM_STREAM_PLAYBACK);
			dpcm_clear_pending_state(fe, SNDRV_PCM_STREAM_PLAYBACK);
			dpcm_be_disconnect(fe, SNDRV_PCM_STREAM_PLAYBACK);
		}

		dpcm_path_put(&list);
capture:
		/* skip if FE doesn't have capture capability */
		if (!fe->cpu_dai->driver->capture.channels_min)
			continue;

		paths = dpcm_path_get(fe, SNDRV_PCM_STREAM_CAPTURE, &list);
		if (paths < 0) {
			dev_warn(fe->dev, "ASoC: %s no valid %s path\n",
					fe->dai_link->name,  "capture");
			mutex_unlock(&card->mutex);
			return paths;
		}

		/* update any new capture paths */
		new = dpcm_process_paths(fe, SNDRV_PCM_STREAM_CAPTURE, &list, 1);
		if (new) {
			dpcm_run_new_update(fe, SNDRV_PCM_STREAM_CAPTURE);
			dpcm_clear_pending_state(fe, SNDRV_PCM_STREAM_CAPTURE);
			dpcm_be_disconnect(fe, SNDRV_PCM_STREAM_CAPTURE);
		}

		/* update any old capture paths */
		old = dpcm_process_paths(fe, SNDRV_PCM_STREAM_CAPTURE, &list, 0);
		if (old) {
			dpcm_run_old_update(fe, SNDRV_PCM_STREAM_CAPTURE);
			dpcm_clear_pending_state(fe, SNDRV_PCM_STREAM_CAPTURE);
			dpcm_be_disconnect(fe, SNDRV_PCM_STREAM_CAPTURE);
		}

		dpcm_path_put(&list);
	}

	mutex_unlock(&card->mutex);
	return 0;
}
int soc_dpcm_be_digital_mute(struct snd_soc_pcm_runtime *fe, int mute)
{
	struct snd_soc_dpcm *dpcm;
	struct list_head *clients =
		&fe->dpcm[SNDRV_PCM_STREAM_PLAYBACK].be_clients;

	list_for_each_entry(dpcm, clients, list_be) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		int i;

		if (be->dai_link->ignore_suspend)
			continue;

		for (i = 0; i < be->num_codecs; i++) {
			struct snd_soc_dai *dai = be->codec_dais[i];
			struct snd_soc_dai_driver *drv = dai->driver;

			dev_dbg(be->dev, "ASoC: BE digital mute %s\n",
					 be->dai_link->name);

			if (drv->ops && drv->ops->digital_mute &&
							dai->playback_active)
				drv->ops->digital_mute(dai, mute);
		}
	}

	return 0;
}

static int dpcm_fe_dai_open(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = fe_substream->private_data;
	struct snd_soc_dpcm *dpcm;
	struct snd_soc_dapm_widget_list *list;
	int ret;
	int stream = fe_substream->stream;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);
	fe->dpcm[stream].runtime = fe_substream->runtime;

	ret = dpcm_path_get(fe, stream, &list);
	if (ret < 0) {
		mutex_unlock(&fe->card->mutex);
		return ret;
	} else if (ret == 0) {
		dev_dbg(fe->dev, "ASoC: %s no valid %s route\n",
			fe->dai_link->name, stream ? "capture" : "playback");
	}

	/* calculate valid and active FE <-> BE dpcms */
	dpcm_process_paths(fe, stream, &list, 1);

	ret = dpcm_fe_dai_startup(fe_substream);
	if (ret < 0) {
		/* clean up all links */
		list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be)
			dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

		dpcm_be_disconnect(fe, stream);
		fe->dpcm[stream].runtime = NULL;
	}

	dpcm_clear_pending_state(fe, stream);
	dpcm_path_put(&list);
	mutex_unlock(&fe->card->mutex);
	return ret;
}

static int dpcm_fe_dai_close(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = fe_substream->private_data;
	struct snd_soc_dpcm *dpcm;
	int stream = fe_substream->stream, ret;

	mutex_lock_nested(&fe->card->mutex, SND_SOC_CARD_CLASS_RUNTIME);
	ret = dpcm_fe_dai_shutdown(fe_substream);

	/* mark FE's links ready to prune */
	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be)
		dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

	dpcm_be_disconnect(fe, stream);

	fe->dpcm[stream].runtime = NULL;
	mutex_unlock(&fe->card->mutex);
	return ret;
}

/* create a new pcm */
int soc_new_pcm(struct snd_soc_pcm_runtime *rtd, int num)
{
	struct snd_soc_platform *platform = rtd->platform;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_pcm *pcm;
	char new_name[64];
	int ret = 0, playback = 0, capture = 0;
	int i;

	if (rtd->dai_link->dynamic || rtd->dai_link->no_pcm) {
		playback = rtd->dai_link->dpcm_playback;
		capture = rtd->dai_link->dpcm_capture;
	} else {
		for (i = 0; i < rtd->num_codecs; i++) {
			codec_dai = rtd->codec_dais[i];
			if (codec_dai->driver->playback.channels_min)
				playback = 1;
			if (codec_dai->driver->capture.channels_min)
				capture = 1;
		}

		capture = capture && cpu_dai->driver->capture.channels_min;
		playback = playback && cpu_dai->driver->playback.channels_min;
	}

	if (rtd->dai_link->playback_only) {
		playback = 1;
		capture = 0;
	}

	if (rtd->dai_link->capture_only) {
		playback = 0;
		capture = 1;
	}

	/* create the PCM */
	if (rtd->dai_link->no_pcm) {
		snprintf(new_name, sizeof(new_name), "(%s)",
			rtd->dai_link->stream_name);

		ret = snd_pcm_new_internal(rtd->card->snd_card, new_name, num,
				playback, capture, &pcm);
	} else {
		if (rtd->dai_link->dynamic)
			snprintf(new_name, sizeof(new_name), "%s (*)",
				rtd->dai_link->stream_name);
		else
			snprintf(new_name, sizeof(new_name), "%s %s-%d",
				rtd->dai_link->stream_name,
				(rtd->num_codecs > 1) ?
				"multicodec" : rtd->codec_dai->name, num);

		ret = snd_pcm_new(rtd->card->snd_card, new_name, num, playback,
			capture, &pcm);
	}
	if (ret < 0) {
		dev_err(rtd->card->dev, "ASoC: can't create pcm for %s\n",
			rtd->dai_link->name);
		return ret;
	}
	dev_dbg(rtd->card->dev, "ASoC: registered pcm #%d %s\n",num, new_name);

	/* DAPM dai link stream work */
	INIT_DELAYED_WORK(&rtd->delayed_work, close_delayed_work);

	rtd->pcm = pcm;
	pcm->private_data = rtd;

	if (rtd->dai_link->no_pcm) {
		if (playback)
			pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream->private_data = rtd;
		if (capture)
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream->private_data = rtd;
		goto out;
	}

	/* ASoC PCM operations */
	if (rtd->dai_link->dynamic) {
		rtd->ops.open		= dpcm_fe_dai_open;
		rtd->ops.hw_params	= dpcm_fe_dai_hw_params;
		rtd->ops.prepare	= dpcm_fe_dai_prepare;
		rtd->ops.trigger	= dpcm_fe_dai_trigger;
		rtd->ops.hw_free	= dpcm_fe_dai_hw_free;
		rtd->ops.close		= dpcm_fe_dai_close;
		rtd->ops.pointer	= soc_pcm_pointer;
		rtd->ops.ioctl		= soc_pcm_ioctl;
	} else {
		rtd->ops.open		= soc_pcm_open;
		rtd->ops.hw_params	= soc_pcm_hw_params;
		rtd->ops.prepare	= soc_pcm_prepare;
		rtd->ops.trigger	= soc_pcm_trigger;
		rtd->ops.hw_free	= soc_pcm_hw_free;
		rtd->ops.close		= soc_pcm_close;
		rtd->ops.pointer	= soc_pcm_pointer;
		rtd->ops.ioctl		= soc_pcm_ioctl;
	}

	if (platform->driver->ops) {
		rtd->ops.ack		= platform->driver->ops->ack;
		rtd->ops.copy		= platform->driver->ops->copy;
		rtd->ops.silence	= platform->driver->ops->silence;
		rtd->ops.page		= platform->driver->ops->page;
		rtd->ops.mmap		= platform->driver->ops->mmap;
	}

	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &rtd->ops);

	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &rtd->ops);

	if (platform->driver->pcm_new) {
		ret = platform->driver->pcm_new(rtd);
		if (ret < 0) {
			dev_err(platform->dev,
				"ASoC: pcm constructor failed: %d\n",
				ret);
			return ret;
		}
	}

	pcm->private_free = platform->driver->pcm_free;
out:
	dev_info(rtd->card->dev, "%s <-> %s mapping ok\n",
		 (rtd->num_codecs > 1) ? "multicodec" : rtd->codec_dai->name,
		 cpu_dai->name);
	return ret;
}

/* is the current PCM operation for this FE ? */
int snd_soc_dpcm_fe_can_update(struct snd_soc_pcm_runtime *fe, int stream)
{
	if (fe->dpcm[stream].runtime_update == SND_SOC_DPCM_UPDATE_FE)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_fe_can_update);

/* is the current PCM operation for this BE ? */
int snd_soc_dpcm_be_can_update(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	if ((fe->dpcm[stream].runtime_update == SND_SOC_DPCM_UPDATE_FE) ||
	   ((fe->dpcm[stream].runtime_update == SND_SOC_DPCM_UPDATE_BE) &&
		  be->dpcm[stream].runtime_update))
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_be_can_update);

/* get the substream for this BE */
struct snd_pcm_substream *
	snd_soc_dpcm_get_substream(struct snd_soc_pcm_runtime *be, int stream)
{
	return be->pcm->streams[stream].substream;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_get_substream);

/* get the BE runtime state */
enum snd_soc_dpcm_state
	snd_soc_dpcm_be_get_state(struct snd_soc_pcm_runtime *be, int stream)
{
	return be->dpcm[stream].state;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_be_get_state);

/* set the BE runtime state */
void snd_soc_dpcm_be_set_state(struct snd_soc_pcm_runtime *be,
		int stream, enum snd_soc_dpcm_state state)
{
	be->dpcm[stream].state = state;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_be_set_state);

/*
 * We can only hw_free, stop, pause or suspend a BE DAI if any of it's FE
 * are not running, paused or suspended for the specified stream direction.
 */
int snd_soc_dpcm_can_be_free_stop(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	struct snd_soc_dpcm *dpcm;
	int state;

	list_for_each_entry(dpcm, &be->dpcm[stream].fe_clients, list_fe) {

		if (dpcm->fe == fe)
			continue;

		state = dpcm->fe->dpcm[stream].state;
		if (state == SND_SOC_DPCM_STATE_START ||
			state == SND_SOC_DPCM_STATE_PAUSED ||
			state == SND_SOC_DPCM_STATE_SUSPEND)
			return 0;
	}

	/* it's safe to free/stop this BE DAI */
	return 1;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_can_be_free_stop);

/*
 * We can only change hw params a BE DAI if any of it's FE are not prepared,
 * running, paused or suspended for the specified stream direction.
 */
int snd_soc_dpcm_can_be_params(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	struct snd_soc_dpcm *dpcm;
	int state;

	list_for_each_entry(dpcm, &be->dpcm[stream].fe_clients, list_fe) {

		if (dpcm->fe == fe)
			continue;

		state = dpcm->fe->dpcm[stream].state;
		if (state == SND_SOC_DPCM_STATE_START ||
			state == SND_SOC_DPCM_STATE_PAUSED ||
			state == SND_SOC_DPCM_STATE_SUSPEND ||
			state == SND_SOC_DPCM_STATE_PREPARE)
			return 0;
	}

	/* it's safe to change hw_params */
	return 1;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_can_be_params);

int snd_soc_platform_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_platform *platform)
{
	if (platform->driver->ops && platform->driver->ops->trigger)
		return platform->driver->ops->trigger(substream, cmd);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_platform_trigger);

#ifdef CONFIG_DEBUG_FS
static char *dpcm_state_string(enum snd_soc_dpcm_state state)
{
	switch (state) {
	case SND_SOC_DPCM_STATE_NEW:
		return "new";
	case SND_SOC_DPCM_STATE_OPEN:
		return "open";
	case SND_SOC_DPCM_STATE_HW_PARAMS:
		return "hw_params";
	case SND_SOC_DPCM_STATE_PREPARE:
		return "prepare";
	case SND_SOC_DPCM_STATE_START:
		return "start";
	case SND_SOC_DPCM_STATE_STOP:
		return "stop";
	case SND_SOC_DPCM_STATE_SUSPEND:
		return "suspend";
	case SND_SOC_DPCM_STATE_PAUSED:
		return "paused";
	case SND_SOC_DPCM_STATE_HW_FREE:
		return "hw_free";
	case SND_SOC_DPCM_STATE_CLOSE:
		return "close";
	}

	return "unknown";
}

static ssize_t dpcm_show_state(struct snd_soc_pcm_runtime *fe,
				int stream, char *buf, size_t size)
{
	struct snd_pcm_hw_params *params = &fe->dpcm[stream].hw_params;
	struct snd_soc_dpcm *dpcm;
	ssize_t offset = 0;

	/* FE state */
	offset += snprintf(buf + offset, size - offset,
			"[%s - %s]\n", fe->dai_link->name,
			stream ? "Capture" : "Playback");

	offset += snprintf(buf + offset, size - offset, "State: %s\n",
	                dpcm_state_string(fe->dpcm[stream].state));

	if ((fe->dpcm[stream].state >= SND_SOC_DPCM_STATE_HW_PARAMS) &&
	    (fe->dpcm[stream].state <= SND_SOC_DPCM_STATE_STOP))
		offset += snprintf(buf + offset, size - offset,
				"Hardware Params: "
				"Format = %s, Channels = %d, Rate = %d\n",
				snd_pcm_format_name(params_format(params)),
				params_channels(params),
				params_rate(params));

	/* BEs state */
	offset += snprintf(buf + offset, size - offset, "Backends:\n");

	if (list_empty(&fe->dpcm[stream].be_clients)) {
		offset += snprintf(buf + offset, size - offset,
				" No active DSP links\n");
		goto out;
	}

	list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		params = &dpcm->hw_params;

		offset += snprintf(buf + offset, size - offset,
				"- %s\n", be->dai_link->name);

		offset += snprintf(buf + offset, size - offset,
				"   State: %s\n",
				dpcm_state_string(be->dpcm[stream].state));

		if ((be->dpcm[stream].state >= SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state <= SND_SOC_DPCM_STATE_STOP))
			offset += snprintf(buf + offset, size - offset,
				"   Hardware Params: "
				"Format = %s, Channels = %d, Rate = %d\n",
				snd_pcm_format_name(params_format(params)),
				params_channels(params),
				params_rate(params));
	}

out:
	return offset;
}

static ssize_t dpcm_state_read_file(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct snd_soc_pcm_runtime *fe = file->private_data;
	ssize_t out_count = PAGE_SIZE, offset = 0, ret = 0;
	char *buf;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (fe->cpu_dai->driver->playback.channels_min)
		offset += dpcm_show_state(fe, SNDRV_PCM_STREAM_PLAYBACK,
					buf + offset, out_count - offset);

	if (fe->cpu_dai->driver->capture.channels_min)
		offset += dpcm_show_state(fe, SNDRV_PCM_STREAM_CAPTURE,
					buf + offset, out_count - offset);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, offset);

	kfree(buf);
	return ret;
}

static const struct file_operations dpcm_state_fops = {
	.open = simple_open,
	.read = dpcm_state_read_file,
	.llseek = default_llseek,
};

int soc_dpcm_debugfs_add(struct snd_soc_pcm_runtime *rtd)
{
	if (!rtd->dai_link)
		return 0;

	rtd->debugfs_dpcm_root = debugfs_create_dir(rtd->dai_link->name,
			rtd->card->debugfs_card_root);
	if (!rtd->debugfs_dpcm_root) {
		dev_dbg(rtd->dev,
			 "ASoC: Failed to create dpcm debugfs directory %s\n",
			 rtd->dai_link->name);
		return -EINVAL;
	}

	rtd->debugfs_dpcm_state = debugfs_create_file("state", 0444,
						rtd->debugfs_dpcm_root,
						rtd, &dpcm_state_fops);

	return 0;
}
#endif
