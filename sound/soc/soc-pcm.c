// SPDX-License-Identifier: GPL-2.0+
//
// soc-pcm.c  --  ALSA SoC PCM
//
// Copyright 2005 Wolfson Microelectronics PLC.
// Copyright 2005 Openedhand Ltd.
// Copyright (C) 2010 Slimlogic Ltd.
// Copyright (C) 2010 Texas Instruments Inc.
//
// Authors: Liam Girdwood <lrg@ti.com>
//          Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dpcm.h>
#include <sound/soc-link.h>
#include <sound/initval.h>

#define soc_pcm_ret(rtd, ret) _soc_pcm_ret(rtd, __func__, ret)
static inline int _soc_pcm_ret(struct snd_soc_pcm_runtime *rtd,
			       const char *func, int ret)
{
	/* Positive, Zero values are not errors */
	if (ret >= 0)
		return ret;

	/* Negative values might be errors */
	switch (ret) {
	case -EPROBE_DEFER:
	case -ENOTSUPP:
	case -EINVAL:
		break;
	default:
		dev_err(rtd->dev,
			"ASoC: error at %s on %s: %d\n",
			func, rtd->dai_link->name, ret);
	}

	return ret;
}

static inline void snd_soc_dpcm_stream_lock_irq(struct snd_soc_pcm_runtime *rtd,
						int stream)
{
	snd_pcm_stream_lock_irq(snd_soc_dpcm_get_substream(rtd, stream));
}

#define snd_soc_dpcm_stream_lock_irqsave_nested(rtd, stream, flags) \
	snd_pcm_stream_lock_irqsave_nested(snd_soc_dpcm_get_substream(rtd, stream), flags)

static inline void snd_soc_dpcm_stream_unlock_irq(struct snd_soc_pcm_runtime *rtd,
						  int stream)
{
	snd_pcm_stream_unlock_irq(snd_soc_dpcm_get_substream(rtd, stream));
}

#define snd_soc_dpcm_stream_unlock_irqrestore(rtd, stream, flags) \
	snd_pcm_stream_unlock_irqrestore(snd_soc_dpcm_get_substream(rtd, stream), flags)

#define DPCM_MAX_BE_USERS	8

static inline const char *soc_cpu_dai_name(struct snd_soc_pcm_runtime *rtd)
{
	return (rtd)->dai_link->num_cpus == 1 ? snd_soc_rtd_to_cpu(rtd, 0)->name : "multicpu";
}
static inline const char *soc_codec_dai_name(struct snd_soc_pcm_runtime *rtd)
{
	return (rtd)->dai_link->num_codecs == 1 ? snd_soc_rtd_to_codec(rtd, 0)->name : "multicodec";
}

#ifdef CONFIG_DEBUG_FS
static const char *dpcm_state_string(enum snd_soc_dpcm_state state)
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
	offset += scnprintf(buf + offset, size - offset,
			   "[%s - %s]\n", fe->dai_link->name,
			   stream ? "Capture" : "Playback");

	offset += scnprintf(buf + offset, size - offset, "State: %s\n",
			   dpcm_state_string(fe->dpcm[stream].state));

	if ((fe->dpcm[stream].state >= SND_SOC_DPCM_STATE_HW_PARAMS) &&
	    (fe->dpcm[stream].state <= SND_SOC_DPCM_STATE_STOP))
		offset += scnprintf(buf + offset, size - offset,
				   "Hardware Params: "
				   "Format = %s, Channels = %d, Rate = %d\n",
				   snd_pcm_format_name(params_format(params)),
				   params_channels(params),
				   params_rate(params));

	/* BEs state */
	offset += scnprintf(buf + offset, size - offset, "Backends:\n");

	if (list_empty(&fe->dpcm[stream].be_clients)) {
		offset += scnprintf(buf + offset, size - offset,
				   " No active DSP links\n");
		goto out;
	}

	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		params = &be->dpcm[stream].hw_params;

		offset += scnprintf(buf + offset, size - offset,
				   "- %s\n", be->dai_link->name);

		offset += scnprintf(buf + offset, size - offset,
				   "   State: %s\n",
				   dpcm_state_string(be->dpcm[stream].state));

		if ((be->dpcm[stream].state >= SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state <= SND_SOC_DPCM_STATE_STOP))
			offset += scnprintf(buf + offset, size - offset,
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
	int stream;
	char *buf;

	if (fe->dai_link->num_cpus > 1) {
		dev_err(fe->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	snd_soc_dpcm_mutex_lock(fe);
	for_each_pcm_streams(stream)
		if (snd_soc_dai_stream_valid(snd_soc_rtd_to_cpu(fe, 0), stream))
			offset += dpcm_show_state(fe, stream,
						  buf + offset,
						  out_count - offset);
	snd_soc_dpcm_mutex_unlock(fe);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, offset);

	kfree(buf);
	return ret;
}

static const struct file_operations dpcm_state_fops = {
	.open = simple_open,
	.read = dpcm_state_read_file,
	.llseek = default_llseek,
};

void soc_dpcm_debugfs_add(struct snd_soc_pcm_runtime *rtd)
{
	if (!rtd->dai_link->dynamic)
		return;

	if (!rtd->card->debugfs_card_root)
		return;

	rtd->debugfs_dpcm_root = debugfs_create_dir(rtd->dai_link->name,
						    rtd->card->debugfs_card_root);

	debugfs_create_file("state", 0444, rtd->debugfs_dpcm_root,
			    rtd, &dpcm_state_fops);
}

static void dpcm_create_debugfs_state(struct snd_soc_dpcm *dpcm, int stream)
{
	char *name;

	name = kasprintf(GFP_KERNEL, "%s:%s", dpcm->be->dai_link->name,
			 stream ? "capture" : "playback");
	if (name) {
		dpcm->debugfs_state = debugfs_create_dir(
			name, dpcm->fe->debugfs_dpcm_root);
		debugfs_create_u32("state", 0644, dpcm->debugfs_state,
				   &dpcm->state);
		kfree(name);
	}
}

static void dpcm_remove_debugfs_state(struct snd_soc_dpcm *dpcm)
{
	debugfs_remove_recursive(dpcm->debugfs_state);
}

#else
static inline void dpcm_create_debugfs_state(struct snd_soc_dpcm *dpcm,
					     int stream)
{
}

static inline void dpcm_remove_debugfs_state(struct snd_soc_dpcm *dpcm)
{
}
#endif

/* Set FE's runtime_update state; the state is protected via PCM stream lock
 * for avoiding the race with trigger callback.
 * If the state is unset and a trigger is pending while the previous operation,
 * process the pending trigger action here.
 */
static int dpcm_fe_dai_do_trigger(struct snd_pcm_substream *substream, int cmd);
static void dpcm_set_fe_update_state(struct snd_soc_pcm_runtime *fe,
				     int stream, enum snd_soc_dpcm_update state)
{
	struct snd_pcm_substream *substream =
		snd_soc_dpcm_get_substream(fe, stream);

	snd_soc_dpcm_stream_lock_irq(fe, stream);
	if (state == SND_SOC_DPCM_UPDATE_NO && fe->dpcm[stream].trigger_pending) {
		dpcm_fe_dai_do_trigger(substream,
				       fe->dpcm[stream].trigger_pending - 1);
		fe->dpcm[stream].trigger_pending = 0;
	}
	fe->dpcm[stream].runtime_update = state;
	snd_soc_dpcm_stream_unlock_irq(fe, stream);
}

static void dpcm_set_be_update_state(struct snd_soc_pcm_runtime *be,
				     int stream, enum snd_soc_dpcm_update state)
{
	be->dpcm[stream].runtime_update = state;
}

/**
 * snd_soc_runtime_action() - Increment/Decrement active count for
 * PCM runtime components
 * @rtd: ASoC PCM runtime that is activated
 * @stream: Direction of the PCM stream
 * @action: Activate stream if 1. Deactivate if -1.
 *
 * Increments/Decrements the active count for all the DAIs and components
 * attached to a PCM runtime.
 * Should typically be called when a stream is opened.
 *
 * Must be called with the rtd->card->pcm_mutex being held
 */
void snd_soc_runtime_action(struct snd_soc_pcm_runtime *rtd,
			    int stream, int action)
{
	struct snd_soc_dai *dai;
	int i;

	snd_soc_dpcm_mutex_assert_held(rtd);

	for_each_rtd_dais(rtd, i, dai)
		snd_soc_dai_action(dai, stream, action);
}
EXPORT_SYMBOL_GPL(snd_soc_runtime_action);

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
	struct snd_soc_component *component;
	bool ignore = true;
	int i;

	if (!rtd->pmdown_time || rtd->dai_link->ignore_pmdown_time)
		return true;

	for_each_rtd_components(rtd, i, component)
		ignore &= !component->driver->use_pmdown_time;

	return ignore;
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
	substream->runtime->hw = *hw;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_set_runtime_hwparams);

/* DPCM stream event, send event to FE and all active BEs. */
int dpcm_dapm_stream_event(struct snd_soc_pcm_runtime *fe, int dir,
	int event)
{
	struct snd_soc_dpcm *dpcm;

	snd_soc_dpcm_mutex_assert_held(fe);

	for_each_dpcm_be(fe, dir, dpcm) {

		struct snd_soc_pcm_runtime *be = dpcm->be;

		dev_dbg(be->dev, "ASoC: BE %s event %d dir %d\n",
				be->dai_link->name, event, dir);

		if ((event == SND_SOC_DAPM_STREAM_STOP) &&
		    (be->dpcm[dir].users >= 1))
			continue;

		snd_soc_dapm_stream_event(be, dir, event);
	}

	snd_soc_dapm_stream_event(fe, dir, event);

	return 0;
}

static void soc_pcm_set_dai_params(struct snd_soc_dai *dai,
				   struct snd_pcm_hw_params *params)
{
	if (params) {
		dai->rate	 = params_rate(params);
		dai->channels	 = params_channels(params);
		dai->sample_bits = snd_pcm_format_physical_width(params_format(params));
	} else {
		dai->rate	 = 0;
		dai->channels	 = 0;
		dai->sample_bits = 0;
	}
}

static int soc_pcm_apply_symmetry(struct snd_pcm_substream *substream,
					struct snd_soc_dai *soc_dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ret;

	if (!snd_soc_dai_active(soc_dai))
		return 0;

#define __soc_pcm_apply_symmetry(name, NAME)				\
	if (soc_dai->name && (soc_dai->driver->symmetric_##name ||	\
			      rtd->dai_link->symmetric_##name)) {	\
		dev_dbg(soc_dai->dev, "ASoC: Symmetry forces %s to %d\n",\
			#name, soc_dai->name);				\
									\
		ret = snd_pcm_hw_constraint_single(substream->runtime,	\
						   SNDRV_PCM_HW_PARAM_##NAME,\
						   soc_dai->name);	\
		if (ret < 0) {						\
			dev_err(soc_dai->dev,				\
				"ASoC: Unable to apply %s constraint: %d\n",\
				#name, ret);				\
			return ret;					\
		}							\
	}

	__soc_pcm_apply_symmetry(rate,		RATE);
	__soc_pcm_apply_symmetry(channels,	CHANNELS);
	__soc_pcm_apply_symmetry(sample_bits,	SAMPLE_BITS);

	return 0;
}

static int soc_pcm_params_symmetry(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai d;
	struct snd_soc_dai *dai;
	struct snd_soc_dai *cpu_dai;
	unsigned int symmetry, i;

	d.name = __func__;
	soc_pcm_set_dai_params(&d, params);

#define __soc_pcm_params_symmetry(xxx)					\
	symmetry = rtd->dai_link->symmetric_##xxx;			\
	for_each_rtd_dais(rtd, i, dai)					\
		symmetry |= dai->driver->symmetric_##xxx;		\
									\
	if (symmetry)							\
		for_each_rtd_cpu_dais(rtd, i, cpu_dai)			\
			if (!snd_soc_dai_is_dummy(cpu_dai) &&		\
			    cpu_dai->xxx && cpu_dai->xxx != d.xxx) {	\
				dev_err(rtd->dev, "ASoC: unmatched %s symmetry: %s:%d - %s:%d\n", \
					#xxx, cpu_dai->name, cpu_dai->xxx, d.name, d.xxx); \
				return -EINVAL;				\
			}

	/* reject unmatched parameters when applying symmetry */
	__soc_pcm_params_symmetry(rate);
	__soc_pcm_params_symmetry(channels);
	__soc_pcm_params_symmetry(sample_bits);

	return 0;
}

static void soc_pcm_update_symmetry(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link *link = rtd->dai_link;
	struct snd_soc_dai *dai;
	unsigned int symmetry, i;

	symmetry = link->symmetric_rate ||
		link->symmetric_channels ||
		link->symmetric_sample_bits;

	for_each_rtd_dais(rtd, i, dai)
		symmetry = symmetry ||
			dai->driver->symmetric_rate ||
			dai->driver->symmetric_channels ||
			dai->driver->symmetric_sample_bits;

	if (symmetry)
		substream->runtime->hw.info |= SNDRV_PCM_INFO_JOINT_DUPLEX;
}

static void soc_pcm_set_msb(struct snd_pcm_substream *substream, int bits)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ret;

	if (!bits)
		return;

	ret = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 0, bits);
	if (ret != 0)
		dev_warn(rtd->dev, "ASoC: Failed to set MSB %d: %d\n",
				 bits, ret);
}

static void soc_pcm_apply_msb(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai;
	struct snd_soc_dai *codec_dai;
	int stream = substream->stream;
	int i;
	unsigned int bits = 0, cpu_bits = 0;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		struct snd_soc_pcm_stream *pcm_codec = snd_soc_dai_get_pcm_stream(codec_dai, stream);

		if (pcm_codec->sig_bits == 0) {
			bits = 0;
			break;
		}
		bits = max(pcm_codec->sig_bits, bits);
	}

	for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
		struct snd_soc_pcm_stream *pcm_cpu = snd_soc_dai_get_pcm_stream(cpu_dai, stream);

		if (pcm_cpu->sig_bits == 0) {
			cpu_bits = 0;
			break;
		}
		cpu_bits = max(pcm_cpu->sig_bits, cpu_bits);
	}

	soc_pcm_set_msb(substream, bits);
	soc_pcm_set_msb(substream, cpu_bits);
}

static void soc_pcm_hw_init(struct snd_pcm_hardware *hw)
{
	hw->rates		= UINT_MAX;
	hw->rate_min		= 0;
	hw->rate_max		= UINT_MAX;
	hw->channels_min	= 0;
	hw->channels_max	= UINT_MAX;
	hw->formats		= ULLONG_MAX;
}

static void soc_pcm_hw_update_rate(struct snd_pcm_hardware *hw,
				   struct snd_soc_pcm_stream *p)
{
	hw->rates = snd_pcm_rate_mask_intersect(hw->rates, p->rates);

	/* setup hw->rate_min/max via hw->rates first */
	snd_pcm_hw_limit_rates(hw);

	/* update hw->rate_min/max by snd_soc_pcm_stream */
	hw->rate_min = max(hw->rate_min, p->rate_min);
	hw->rate_max = min_not_zero(hw->rate_max, p->rate_max);
}

static void soc_pcm_hw_update_chan(struct snd_pcm_hardware *hw,
				   struct snd_soc_pcm_stream *p)
{
	hw->channels_min = max(hw->channels_min, p->channels_min);
	hw->channels_max = min(hw->channels_max, p->channels_max);
}

static void soc_pcm_hw_update_format(struct snd_pcm_hardware *hw,
				     struct snd_soc_pcm_stream *p)
{
	hw->formats &= p->formats;
}

/**
 * snd_soc_runtime_calc_hw() - Calculate hw limits for a PCM stream
 * @rtd: ASoC PCM runtime
 * @hw: PCM hardware parameters (output)
 * @stream: Direction of the PCM stream
 *
 * Calculates the subset of stream parameters supported by all DAIs
 * associated with the PCM stream.
 */
int snd_soc_runtime_calc_hw(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hardware *hw, int stream)
{
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;
	struct snd_soc_pcm_stream *codec_stream;
	struct snd_soc_pcm_stream *cpu_stream;
	unsigned int cpu_chan_min = 0, cpu_chan_max = UINT_MAX;
	int i;

	soc_pcm_hw_init(hw);

	/* first calculate min/max only for CPUs in the DAI link */
	for_each_rtd_cpu_dais(rtd, i, cpu_dai) {

		/*
		 * Skip CPUs which don't support the current stream type.
		 * Otherwise, since the rate, channel, and format values will
		 * zero in that case, we would have no usable settings left,
		 * causing the resulting setup to fail.
		 */
		if (!snd_soc_dai_stream_valid(cpu_dai, stream))
			continue;

		cpu_stream = snd_soc_dai_get_pcm_stream(cpu_dai, stream);

		soc_pcm_hw_update_chan(hw, cpu_stream);
		soc_pcm_hw_update_rate(hw, cpu_stream);
		soc_pcm_hw_update_format(hw, cpu_stream);
	}
	cpu_chan_min = hw->channels_min;
	cpu_chan_max = hw->channels_max;

	/* second calculate min/max only for CODECs in the DAI link */
	for_each_rtd_codec_dais(rtd, i, codec_dai) {

		/*
		 * Skip CODECs which don't support the current stream type.
		 * Otherwise, since the rate, channel, and format values will
		 * zero in that case, we would have no usable settings left,
		 * causing the resulting setup to fail.
		 */
		if (!snd_soc_dai_stream_valid(codec_dai, stream))
			continue;

		codec_stream = snd_soc_dai_get_pcm_stream(codec_dai, stream);

		soc_pcm_hw_update_chan(hw, codec_stream);
		soc_pcm_hw_update_rate(hw, codec_stream);
		soc_pcm_hw_update_format(hw, codec_stream);
	}

	/* Verify both a valid CPU DAI and a valid CODEC DAI were found */
	if (!hw->channels_min)
		return -EINVAL;

	/*
	 * chan min/max cannot be enforced if there are multiple CODEC DAIs
	 * connected to CPU DAI(s), use CPU DAI's directly and let
	 * channel allocation be fixed up later
	 */
	if (rtd->dai_link->num_codecs > 1) {
		hw->channels_min = cpu_chan_min;
		hw->channels_max = cpu_chan_max;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_runtime_calc_hw);

static void soc_pcm_init_runtime_hw(struct snd_pcm_substream *substream)
{
	struct snd_pcm_hardware *hw = &substream->runtime->hw;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	u64 formats = hw->formats;

	/*
	 * At least one CPU and one CODEC should match. Otherwise, we should
	 * have bailed out on a higher level, since there would be no CPU or
	 * CODEC to support the transfer direction in that case.
	 */
	snd_soc_runtime_calc_hw(rtd, hw, substream->stream);

	if (formats)
		hw->formats &= formats;
}

static int soc_pcm_components_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component;
	int i, ret = 0;

	for_each_rtd_components(rtd, i, component) {
		ret = snd_soc_component_module_get_when_open(component, substream);
		if (ret < 0)
			break;

		ret = snd_soc_component_open(component, substream);
		if (ret < 0)
			break;
	}

	return ret;
}

static int soc_pcm_components_close(struct snd_pcm_substream *substream,
				    int rollback)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component;
	int i, ret = 0;

	for_each_rtd_components(rtd, i, component) {
		int r = snd_soc_component_close(component, substream, rollback);
		if (r < 0)
			ret = r; /* use last ret */

		snd_soc_component_module_put_when_close(component, substream, rollback);
	}

	return ret;
}

static int soc_pcm_clean(struct snd_soc_pcm_runtime *rtd,
			 struct snd_pcm_substream *substream, int rollback)
{
	struct snd_soc_component *component;
	struct snd_soc_dai *dai;
	int i;

	snd_soc_dpcm_mutex_assert_held(rtd);

	if (!rollback) {
		snd_soc_runtime_deactivate(rtd, substream->stream);

		/* Make sure DAI parameters cleared if the DAI becomes inactive */
		for_each_rtd_dais(rtd, i, dai) {
			if (snd_soc_dai_active(dai) == 0 &&
			    (dai->rate || dai->channels || dai->sample_bits))
				soc_pcm_set_dai_params(dai, NULL);

			if (snd_soc_dai_stream_active(dai, substream->stream) ==  0) {
				if (dai->driver->ops && !dai->driver->ops->mute_unmute_on_trigger)
					snd_soc_dai_digital_mute(dai, 1, substream->stream);
			}
		}
	}

	for_each_rtd_dais(rtd, i, dai)
		snd_soc_dai_shutdown(dai, substream, rollback);

	snd_soc_link_shutdown(substream, rollback);

	soc_pcm_components_close(substream, rollback);

	snd_soc_pcm_component_pm_runtime_put(rtd, substream, rollback);

	for_each_rtd_components(rtd, i, component)
		if (!snd_soc_component_active(component))
			pinctrl_pm_select_sleep_state(component->dev);

	return 0;
}

/*
 * Called by ALSA when a PCM substream is closed. Private data can be
 * freed here. The cpu DAI, codec DAI, machine and components are also
 * shutdown.
 */
static int __soc_pcm_close(struct snd_soc_pcm_runtime *rtd,
			   struct snd_pcm_substream *substream)
{
	return soc_pcm_clean(rtd, substream, 0);
}

/* PCM close ops for non-DPCM streams */
static int soc_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	snd_soc_dpcm_mutex_lock(rtd);
	__soc_pcm_close(rtd, substream);
	snd_soc_dpcm_mutex_unlock(rtd);
	return 0;
}

static int soc_hw_sanity_check(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_hardware *hw = &substream->runtime->hw;
	const char *name_cpu = soc_cpu_dai_name(rtd);
	const char *name_codec = soc_codec_dai_name(rtd);
	const char *err_msg;
	struct device *dev = rtd->dev;

	err_msg = "rates";
	if (!hw->rates)
		goto config_err;

	err_msg = "formats";
	if (!hw->formats)
		goto config_err;

	err_msg = "channels";
	if (!hw->channels_min || !hw->channels_max ||
	     hw->channels_min  >  hw->channels_max)
		goto config_err;

	dev_dbg(dev, "ASoC: %s <-> %s info:\n",		name_codec,
							name_cpu);
	dev_dbg(dev, "ASoC: rate mask 0x%x\n",		hw->rates);
	dev_dbg(dev, "ASoC: ch   min %d max %d\n",	hw->channels_min,
							hw->channels_max);
	dev_dbg(dev, "ASoC: rate min %d max %d\n",	hw->rate_min,
							hw->rate_max);

	return 0;

config_err:
	dev_err(dev, "ASoC: %s <-> %s No matching %s\n",
		name_codec, name_cpu, err_msg);
	return -EINVAL;
}

/*
 * Called by ALSA when a PCM substream is opened, the runtime->hw record is
 * then initialized and any private data can be allocated. This also calls
 * startup for the cpu DAI, component, machine and codec DAI.
 */
static int __soc_pcm_open(struct snd_soc_pcm_runtime *rtd,
			  struct snd_pcm_substream *substream)
{
	struct snd_soc_component *component;
	struct snd_soc_dai *dai;
	int i, ret = 0;

	snd_soc_dpcm_mutex_assert_held(rtd);

	for_each_rtd_components(rtd, i, component)
		pinctrl_pm_select_default_state(component->dev);

	ret = snd_soc_pcm_component_pm_runtime_get(rtd, substream);
	if (ret < 0)
		goto err;

	ret = soc_pcm_components_open(substream);
	if (ret < 0)
		goto err;

	ret = snd_soc_link_startup(substream);
	if (ret < 0)
		goto err;

	/* startup the audio subsystem */
	for_each_rtd_dais(rtd, i, dai) {
		ret = snd_soc_dai_startup(dai, substream);
		if (ret < 0)
			goto err;
	}

	/* Dynamic PCM DAI links compat checks use dynamic capabilities */
	if (rtd->dai_link->dynamic || rtd->dai_link->no_pcm)
		goto dynamic;

	/* Check that the codec and cpu DAIs are compatible */
	soc_pcm_init_runtime_hw(substream);

	soc_pcm_update_symmetry(substream);

	ret = soc_hw_sanity_check(substream);
	if (ret < 0)
		goto err;

	soc_pcm_apply_msb(substream);

	/* Symmetry only applies if we've already got an active stream. */
	for_each_rtd_dais(rtd, i, dai) {
		ret = soc_pcm_apply_symmetry(substream, dai);
		if (ret != 0)
			goto err;
	}
dynamic:
	snd_soc_runtime_activate(rtd, substream->stream);
	ret = 0;
err:
	if (ret < 0)
		soc_pcm_clean(rtd, substream, 1);

	return soc_pcm_ret(rtd, ret);
}

/* PCM open ops for non-DPCM streams */
static int soc_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);
	ret = __soc_pcm_open(rtd, substream);
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

/*
 * Called by ALSA when the PCM substream is prepared, can set format, sample
 * rate, etc.  This function is non atomic and can be called multiple times,
 * it can refer to the runtime info.
 */
static int __soc_pcm_prepare(struct snd_soc_pcm_runtime *rtd,
			     struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai;
	int i, ret = 0;

	snd_soc_dpcm_mutex_assert_held(rtd);

	ret = snd_soc_link_prepare(substream);
	if (ret < 0)
		goto out;

	ret = snd_soc_pcm_component_prepare(substream);
	if (ret < 0)
		goto out;

	ret = snd_soc_pcm_dai_prepare(substream);
	if (ret < 0)
		goto out;

	/* cancel any delayed stream shutdown that is pending */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    rtd->pop_wait) {
		rtd->pop_wait = 0;
		cancel_delayed_work(&rtd->delayed_work);
	}

	snd_soc_dapm_stream_event(rtd, substream->stream,
			SND_SOC_DAPM_STREAM_START);

	for_each_rtd_dais(rtd, i, dai) {
		if (dai->driver->ops && !dai->driver->ops->mute_unmute_on_trigger)
			snd_soc_dai_digital_mute(dai, 0, substream->stream);
	}

out:
	return soc_pcm_ret(rtd, ret);
}

/* PCM prepare ops for non-DPCM streams */
static int soc_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);
	ret = __soc_pcm_prepare(rtd, substream);
	snd_soc_dpcm_mutex_unlock(rtd);
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

static int soc_pcm_hw_clean(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_substream *substream, int rollback)
{
	struct snd_soc_dai *dai;
	int i;

	snd_soc_dpcm_mutex_assert_held(rtd);

	/* clear the corresponding DAIs parameters when going to be inactive */
	for_each_rtd_dais(rtd, i, dai) {
		if (snd_soc_dai_active(dai) == 1)
			soc_pcm_set_dai_params(dai, NULL);

		if (snd_soc_dai_stream_active(dai, substream->stream) == 1)
			snd_soc_dai_digital_mute(dai, 1, substream->stream);
	}

	/* run the stream event */
	snd_soc_dapm_stream_stop(rtd, substream->stream);

	/* free any machine hw params */
	snd_soc_link_hw_free(substream, rollback);

	/* free any component resources */
	snd_soc_pcm_component_hw_free(substream, rollback);

	/* now free hw params for the DAIs  */
	for_each_rtd_dais(rtd, i, dai)
		if (snd_soc_dai_stream_valid(dai, substream->stream))
			snd_soc_dai_hw_free(dai, substream, rollback);

	return 0;
}

/*
 * Frees resources allocated by hw_params, can be called multiple times
 */
static int __soc_pcm_hw_free(struct snd_soc_pcm_runtime *rtd,
			     struct snd_pcm_substream *substream)
{
	return soc_pcm_hw_clean(rtd, substream, 0);
}

/* hw_free PCM ops for non-DPCM streams */
static int soc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);
	ret = __soc_pcm_hw_free(rtd, substream);
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

/*
 * Called by ALSA when the hardware params are set by application. This
 * function can also be called multiple times and can allocate buffers
 * (using snd_pcm_lib_* ). It's non-atomic.
 */
static int __soc_pcm_hw_params(struct snd_soc_pcm_runtime *rtd,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_soc_dai *cpu_dai;
	struct snd_soc_dai *codec_dai;
	struct snd_pcm_hw_params tmp_params;
	int i, ret = 0;

	snd_soc_dpcm_mutex_assert_held(rtd);

	ret = soc_pcm_params_symmetry(substream, params);
	if (ret)
		goto out;

	ret = snd_soc_link_hw_params(substream, params);
	if (ret < 0)
		goto out;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		unsigned int tdm_mask = snd_soc_dai_tdm_mask_get(codec_dai, substream->stream);

		/*
		 * Skip CODECs which don't support the current stream type,
		 * the idea being that if a CODEC is not used for the currently
		 * set up transfer direction, it should not need to be
		 * configured, especially since the configuration used might
		 * not even be supported by that CODEC. There may be cases
		 * however where a CODEC needs to be set up although it is
		 * actually not being used for the transfer, e.g. if a
		 * capture-only CODEC is acting as an LRCLK and/or BCLK master
		 * for the DAI link including a playback-only CODEC.
		 * If this becomes necessary, we will have to augment the
		 * machine driver setup with information on how to act, so
		 * we can do the right thing here.
		 */
		if (!snd_soc_dai_stream_valid(codec_dai, substream->stream))
			continue;

		/* copy params for each codec */
		tmp_params = *params;

		/* fixup params based on TDM slot masks */
		if (tdm_mask)
			soc_pcm_codec_params_fixup(&tmp_params, tdm_mask);

		ret = snd_soc_dai_hw_params(codec_dai, substream,
					    &tmp_params);
		if(ret < 0)
			goto out;

		soc_pcm_set_dai_params(codec_dai, &tmp_params);
		snd_soc_dapm_update_dai(substream, &tmp_params, codec_dai);
	}

	for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
		unsigned int ch_mask = 0;
		int j;

		/*
		 * Skip CPUs which don't support the current stream
		 * type. See soc_pcm_init_runtime_hw() for more details
		 */
		if (!snd_soc_dai_stream_valid(cpu_dai, substream->stream))
			continue;

		/* copy params for each cpu */
		tmp_params = *params;

		if (!rtd->dai_link->codec_ch_maps)
			goto hw_params;
		/*
		 * construct cpu channel mask by combining ch_mask of each
		 * codec which maps to the cpu.
		 */
		for_each_rtd_codec_dais(rtd, j, codec_dai) {
			if (rtd->dai_link->codec_ch_maps[j].connected_cpu_id == i)
				ch_mask |= rtd->dai_link->codec_ch_maps[j].ch_mask;
		}

		/* fixup cpu channel number */
		if (ch_mask)
			soc_pcm_codec_params_fixup(&tmp_params, ch_mask);

hw_params:
		ret = snd_soc_dai_hw_params(cpu_dai, substream, &tmp_params);
		if (ret < 0)
			goto out;

		/* store the parameters for each DAI */
		soc_pcm_set_dai_params(cpu_dai, &tmp_params);
		snd_soc_dapm_update_dai(substream, &tmp_params, cpu_dai);
	}

	ret = snd_soc_pcm_component_hw_params(substream, params);
out:
	if (ret < 0)
		soc_pcm_hw_clean(rtd, substream, 1);

	return soc_pcm_ret(rtd, ret);
}

/* hw_params PCM ops for non-DPCM streams */
static int soc_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ret;

	snd_soc_dpcm_mutex_lock(rtd);
	ret = __soc_pcm_hw_params(rtd, substream, params);
	snd_soc_dpcm_mutex_unlock(rtd);
	return ret;
}

#define TRIGGER_MAX 3
static int (* const trigger[][TRIGGER_MAX])(struct snd_pcm_substream *substream, int cmd, int rollback) = {
	[SND_SOC_TRIGGER_ORDER_DEFAULT] = {
		snd_soc_link_trigger,
		snd_soc_pcm_component_trigger,
		snd_soc_pcm_dai_trigger,
	},
	[SND_SOC_TRIGGER_ORDER_LDC] = {
		snd_soc_link_trigger,
		snd_soc_pcm_dai_trigger,
		snd_soc_pcm_component_trigger,
	},
};

static int soc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component;
	int ret = 0, r = 0, i;
	int rollback = 0;
	int start = 0, stop = 0;

	/*
	 * select START/STOP sequence
	 */
	for_each_rtd_components(rtd, i, component) {
		if (component->driver->trigger_start)
			start = component->driver->trigger_start;
		if (component->driver->trigger_stop)
			stop = component->driver->trigger_stop;
	}
	if (rtd->dai_link->trigger_start)
		start = rtd->dai_link->trigger_start;
	if (rtd->dai_link->trigger_stop)
		stop  = rtd->dai_link->trigger_stop;

	if (start < 0 || start >= SND_SOC_TRIGGER_ORDER_MAX ||
	    stop  < 0 || stop  >= SND_SOC_TRIGGER_ORDER_MAX)
		return -EINVAL;

	/*
	 * START
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		for (i = 0; i < TRIGGER_MAX; i++) {
			r = trigger[start][i](substream, cmd, 0);
			if (r < 0)
				break;
		}
	}

	/*
	 * Rollback if START failed
	 * find correspond STOP command
	 */
	if (r < 0) {
		rollback = 1;
		ret = r;
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			cmd = SNDRV_PCM_TRIGGER_STOP;
			break;
		case SNDRV_PCM_TRIGGER_RESUME:
			cmd = SNDRV_PCM_TRIGGER_SUSPEND;
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			cmd = SNDRV_PCM_TRIGGER_PAUSE_PUSH;
			break;
		}
	}

	/*
	 * STOP
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		for (i = TRIGGER_MAX; i > 0; i--) {
			r = trigger[stop][i - 1](substream, cmd, rollback);
			if (r < 0)
				ret = r;
		}
	}

	return ret;
}

/*
 * soc level wrapper for pointer callback
 * If cpu_dai, codec_dai, component driver has the delay callback, then
 * the runtime->delay will be updated via snd_soc_pcm_component/dai_delay().
 */
static snd_pcm_uframes_t soc_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t offset = 0;
	snd_pcm_sframes_t codec_delay = 0;
	snd_pcm_sframes_t cpu_delay = 0;

	offset = snd_soc_pcm_component_pointer(substream);

	/* should be called *after* snd_soc_pcm_component_pointer() */
	snd_soc_pcm_dai_delay(substream, &cpu_delay, &codec_delay);
	snd_soc_pcm_component_delay(substream, &cpu_delay, &codec_delay);

	runtime->delay = cpu_delay + codec_delay;

	return offset;
}

/* connect a FE and BE */
static int dpcm_be_connect(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	struct snd_pcm_substream *fe_substream;
	struct snd_pcm_substream *be_substream;
	struct snd_soc_dpcm *dpcm;

	snd_soc_dpcm_mutex_assert_held(fe);

	/* only add new dpcms */
	for_each_dpcm_be(fe, stream, dpcm) {
		if (dpcm->be == be && dpcm->fe == fe)
			return 0;
	}

	fe_substream = snd_soc_dpcm_get_substream(fe, stream);
	be_substream = snd_soc_dpcm_get_substream(be, stream);

	if (!fe_substream->pcm->nonatomic && be_substream->pcm->nonatomic) {
		dev_err(be->dev, "%s: FE is atomic but BE is nonatomic, invalid configuration\n",
			__func__);
		return -EINVAL;
	}
	if (fe_substream->pcm->nonatomic && !be_substream->pcm->nonatomic) {
		dev_dbg(be->dev, "FE is nonatomic but BE is not, forcing BE as nonatomic\n");
		be_substream->pcm->nonatomic = 1;
	}

	dpcm = kzalloc(sizeof(struct snd_soc_dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;

	dpcm->be = be;
	dpcm->fe = fe;
	dpcm->state = SND_SOC_DPCM_LINK_STATE_NEW;
	snd_soc_dpcm_stream_lock_irq(fe, stream);
	list_add(&dpcm->list_be, &fe->dpcm[stream].be_clients);
	list_add(&dpcm->list_fe, &be->dpcm[stream].fe_clients);
	snd_soc_dpcm_stream_unlock_irq(fe, stream);

	dev_dbg(fe->dev, "connected new DPCM %s path %s %s %s\n",
			stream ? "capture" : "playback",  fe->dai_link->name,
			stream ? "<-" : "->", be->dai_link->name);

	dpcm_create_debugfs_state(dpcm, stream);

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
	if (!be_substream)
		return;

	for_each_dpcm_fe(be, stream, dpcm) {
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
	LIST_HEAD(deleted_dpcms);

	snd_soc_dpcm_mutex_assert_held(fe);

	snd_soc_dpcm_stream_lock_irq(fe, stream);
	for_each_dpcm_be_safe(fe, stream, dpcm, d) {
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

		list_del(&dpcm->list_be);
		list_move(&dpcm->list_fe, &deleted_dpcms);
	}
	snd_soc_dpcm_stream_unlock_irq(fe, stream);

	while (!list_empty(&deleted_dpcms)) {
		dpcm = list_first_entry(&deleted_dpcms, struct snd_soc_dpcm,
					list_fe);
		list_del(&dpcm->list_fe);
		dpcm_remove_debugfs_state(dpcm);
		kfree(dpcm);
	}
}

/* get BE for DAI widget and stream */
static struct snd_soc_pcm_runtime *dpcm_get_be(struct snd_soc_card *card,
		struct snd_soc_dapm_widget *widget, int stream)
{
	struct snd_soc_pcm_runtime *be;
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dai *dai;
	int i;

	dev_dbg(card->dev, "ASoC: find BE for widget %s\n", widget->name);

	for_each_card_rtds(card, be) {

		if (!be->dai_link->no_pcm)
			continue;

		if (!snd_soc_dpcm_get_substream(be, stream))
			continue;

		for_each_rtd_dais(be, i, dai) {
			w = snd_soc_dai_get_widget(dai, stream);

			dev_dbg(card->dev, "ASoC: try BE : %s\n",
				w ? w->name : "(not set)");

			if (w == widget)
				return be;
		}
	}

	/* Widget provided is not a BE */
	return NULL;
}

int widget_in_list(struct snd_soc_dapm_widget_list *list,
		struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_widget *w;
	int i;

	for_each_dapm_widgets(list, i, w)
		if (widget == w)
			return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(widget_in_list);

bool dpcm_end_walk_at_be(struct snd_soc_dapm_widget *widget, enum snd_soc_dapm_direction dir)
{
	struct snd_soc_card *card = widget->dapm->card;
	struct snd_soc_pcm_runtime *rtd;
	int stream;

	/* adjust dir to stream */
	if (dir == SND_SOC_DAPM_DIR_OUT)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		stream = SNDRV_PCM_STREAM_CAPTURE;

	rtd = dpcm_get_be(card, widget, stream);
	if (rtd)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(dpcm_end_walk_at_be);

int dpcm_path_get(struct snd_soc_pcm_runtime *fe,
	int stream, struct snd_soc_dapm_widget_list **list)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(fe, 0);
	int paths;

	if (fe->dai_link->num_cpus > 1) {
		dev_err(fe->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	/* get number of valid DAI paths and their widgets */
	paths = snd_soc_dapm_dai_get_connected_widgets(cpu_dai, stream, list,
			fe->card->component_chaining ?
				NULL : dpcm_end_walk_at_be);

	if (paths > 0)
		dev_dbg(fe->dev, "ASoC: found %d audio %s paths\n", paths,
			stream ? "capture" : "playback");
	else if (paths == 0)
		dev_dbg(fe->dev, "ASoC: %s no valid %s path\n", fe->dai_link->name,
			 stream ? "capture" : "playback");

	return paths;
}

void dpcm_path_put(struct snd_soc_dapm_widget_list **list)
{
	snd_soc_dapm_dai_free_widgets(list);
}

static bool dpcm_be_is_active(struct snd_soc_dpcm *dpcm, int stream,
			      struct snd_soc_dapm_widget_list *list)
{
	struct snd_soc_dai *dai;
	unsigned int i;

	/* is there a valid DAI widget for this BE */
	for_each_rtd_dais(dpcm->be, i, dai) {
		struct snd_soc_dapm_widget *widget = snd_soc_dai_get_widget(dai, stream);

		/*
		 * The BE is pruned only if none of the dai
		 * widgets are in the active list.
		 */
		if (widget && widget_in_list(list, widget))
			return true;
	}

	return false;
}

static int dpcm_prune_paths(struct snd_soc_pcm_runtime *fe, int stream,
			    struct snd_soc_dapm_widget_list **list_)
{
	struct snd_soc_dpcm *dpcm;
	int prune = 0;

	/* Destroy any old FE <--> BE connections */
	for_each_dpcm_be(fe, stream, dpcm) {
		if (dpcm_be_is_active(dpcm, stream, *list_))
			continue;

		dev_dbg(fe->dev, "ASoC: pruning %s BE %s for %s\n",
			stream ? "capture" : "playback",
			dpcm->be->dai_link->name, fe->dai_link->name);
		dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;
		dpcm_set_be_update_state(dpcm->be, stream, SND_SOC_DPCM_UPDATE_BE);
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
	struct snd_soc_dapm_widget *widget;
	struct snd_pcm_substream *fe_substream = snd_soc_dpcm_get_substream(fe, stream);
	int i, new = 0, err;

	/* don't connect if FE is not running */
	if (!fe_substream->runtime && !fe->fe_compr)
		return new;

	/* Create any new FE <--> BE connections */
	for_each_dapm_widgets(list, i, widget) {

		switch (widget->id) {
		case snd_soc_dapm_dai_in:
			if (stream != SNDRV_PCM_STREAM_PLAYBACK)
				continue;
			break;
		case snd_soc_dapm_dai_out:
			if (stream != SNDRV_PCM_STREAM_CAPTURE)
				continue;
			break;
		default:
			continue;
		}

		/* is there a valid BE rtd for this widget */
		be = dpcm_get_be(card, widget, stream);
		if (!be) {
			dev_dbg(fe->dev, "ASoC: no BE found for %s\n",
				widget->name);
			continue;
		}

		/*
		 * Filter for systems with 'component_chaining' enabled.
		 * This helps to avoid unnecessary re-configuration of an
		 * already active BE on such systems.
		 */
		if (fe->card->component_chaining &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_NEW) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_CLOSE))
			continue;

		/* newly connected FE and BE */
		err = dpcm_be_connect(fe, be, stream);
		if (err < 0) {
			dev_err(fe->dev, "ASoC: can't connect %s\n",
				widget->name);
			break;
		} else if (err == 0) /* already connected */
			continue;

		/* new */
		dpcm_set_be_update_state(be, stream, SND_SOC_DPCM_UPDATE_BE);
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

	for_each_dpcm_be(fe, stream, dpcm)
		dpcm_set_be_update_state(dpcm->be, stream, SND_SOC_DPCM_UPDATE_NO);
}

void dpcm_be_dai_stop(struct snd_soc_pcm_runtime *fe, int stream,
		      int do_hw_free, struct snd_soc_dpcm *last)
{
	struct snd_soc_dpcm *dpcm;

	/* disable any enabled and non active backends */
	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		if (dpcm == last)
			return;

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		if (be->dpcm[stream].users == 0) {
			dev_err(be->dev, "ASoC: no users %s at close - state %d\n",
				stream ? "capture" : "playback",
				be->dpcm[stream].state);
			continue;
		}

		if (--be->dpcm[stream].users != 0)
			continue;

		if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN) {
			if (!do_hw_free)
				continue;

			if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_FREE) {
				__soc_pcm_hw_free(be, be_substream);
				be->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_FREE;
			}
		}

		__soc_pcm_close(be, be_substream);
		be_substream->runtime = NULL;
		be->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	}
}

int dpcm_be_dai_startup(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_pcm_substream *fe_substream = snd_soc_dpcm_get_substream(fe, stream);
	struct snd_soc_pcm_runtime *be;
	struct snd_soc_dpcm *dpcm;
	int err, count = 0;

	/* only startup BE DAIs that are either sinks or sources to this FE DAI */
	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_pcm_substream *be_substream;

		be = dpcm->be;
		be_substream = snd_soc_dpcm_get_substream(be, stream);

		if (!be_substream) {
			dev_err(be->dev, "ASoC: no backend %s stream\n",
				stream ? "capture" : "playback");
			continue;
		}

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		/* first time the dpcm is open ? */
		if (be->dpcm[stream].users == DPCM_MAX_BE_USERS) {
			dev_err(be->dev, "ASoC: too many users %s at open %d\n",
				stream ? "capture" : "playback",
				be->dpcm[stream].state);
			continue;
		}

		if (be->dpcm[stream].users++ != 0)
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_NEW) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_CLOSE))
			continue;

		dev_dbg(be->dev, "ASoC: open %s BE %s\n",
			stream ? "capture" : "playback", be->dai_link->name);

		be_substream->runtime = fe_substream->runtime;
		err = __soc_pcm_open(be, be_substream);
		if (err < 0) {
			be->dpcm[stream].users--;
			if (be->dpcm[stream].users < 0)
				dev_err(be->dev, "ASoC: no users %s at unwind %d\n",
					stream ? "capture" : "playback",
					be->dpcm[stream].state);

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
			goto unwind;
		}
		be->dpcm[stream].be_start = 0;
		be->dpcm[stream].state = SND_SOC_DPCM_STATE_OPEN;
		count++;
	}

	return count;

unwind:
	dpcm_be_dai_startup_rollback(fe, stream, dpcm);

	return soc_pcm_ret(fe, err);
}

static void dpcm_runtime_setup_fe(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct snd_soc_dai *dai;
	int stream = substream->stream;
	u64 formats = hw->formats;
	int i;

	soc_pcm_hw_init(hw);

	if (formats)
		hw->formats &= formats;

	for_each_rtd_cpu_dais(fe, i, dai) {
		struct snd_soc_pcm_stream *cpu_stream;

		/*
		 * Skip CPUs which don't support the current stream
		 * type. See soc_pcm_init_runtime_hw() for more details
		 */
		if (!snd_soc_dai_stream_valid(dai, stream))
			continue;

		cpu_stream = snd_soc_dai_get_pcm_stream(dai, stream);

		soc_pcm_hw_update_rate(hw, cpu_stream);
		soc_pcm_hw_update_chan(hw, cpu_stream);
		soc_pcm_hw_update_format(hw, cpu_stream);
	}

}

static void dpcm_runtime_setup_be_format(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct snd_soc_dpcm *dpcm;
	struct snd_soc_dai *dai;
	int stream = substream->stream;

	if (!fe->dai_link->dpcm_merged_format)
		return;

	/*
	 * It returns merged BE codec format
	 * if FE want to use it (= dpcm_merged_format)
	 */

	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_soc_pcm_stream *codec_stream;
		int i;

		for_each_rtd_codec_dais(be, i, dai) {
			/*
			 * Skip CODECs which don't support the current stream
			 * type. See soc_pcm_init_runtime_hw() for more details
			 */
			if (!snd_soc_dai_stream_valid(dai, stream))
				continue;

			codec_stream = snd_soc_dai_get_pcm_stream(dai, stream);

			soc_pcm_hw_update_format(hw, codec_stream);
		}
	}
}

static void dpcm_runtime_setup_be_chan(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct snd_soc_dpcm *dpcm;
	int stream = substream->stream;

	if (!fe->dai_link->dpcm_merged_chan)
		return;

	/*
	 * It returns merged BE codec channel;
	 * if FE want to use it (= dpcm_merged_chan)
	 */

	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_soc_pcm_stream *cpu_stream;
		struct snd_soc_dai *dai;
		int i;

		for_each_rtd_cpu_dais(be, i, dai) {
			/*
			 * Skip CPUs which don't support the current stream
			 * type. See soc_pcm_init_runtime_hw() for more details
			 */
			if (!snd_soc_dai_stream_valid(dai, stream))
				continue;

			cpu_stream = snd_soc_dai_get_pcm_stream(dai, stream);

			soc_pcm_hw_update_chan(hw, cpu_stream);
		}

		/*
		 * chan min/max cannot be enforced if there are multiple CODEC
		 * DAIs connected to a single CPU DAI, use CPU DAI's directly
		 */
		if (be->dai_link->num_codecs == 1) {
			struct snd_soc_pcm_stream *codec_stream = snd_soc_dai_get_pcm_stream(
				snd_soc_rtd_to_codec(be, 0), stream);

			soc_pcm_hw_update_chan(hw, codec_stream);
		}
	}
}

static void dpcm_runtime_setup_be_rate(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct snd_soc_dpcm *dpcm;
	int stream = substream->stream;

	if (!fe->dai_link->dpcm_merged_rate)
		return;

	/*
	 * It returns merged BE codec channel;
	 * if FE want to use it (= dpcm_merged_chan)
	 */

	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_soc_pcm_stream *pcm;
		struct snd_soc_dai *dai;
		int i;

		for_each_rtd_dais(be, i, dai) {
			/*
			 * Skip DAIs which don't support the current stream
			 * type. See soc_pcm_init_runtime_hw() for more details
			 */
			if (!snd_soc_dai_stream_valid(dai, stream))
				continue;

			pcm = snd_soc_dai_get_pcm_stream(dai, stream);

			soc_pcm_hw_update_rate(hw, pcm);
		}
	}
}

static int dpcm_apply_symmetry(struct snd_pcm_substream *fe_substream,
			       int stream)
{
	struct snd_soc_dpcm *dpcm;
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(fe_substream);
	struct snd_soc_dai *fe_cpu_dai;
	int err = 0;
	int i;

	/* apply symmetry for FE */
	soc_pcm_update_symmetry(fe_substream);

	for_each_rtd_cpu_dais (fe, i, fe_cpu_dai) {
		/* Symmetry only applies if we've got an active stream. */
		err = soc_pcm_apply_symmetry(fe_substream, fe_cpu_dai);
		if (err < 0)
			goto error;
	}

	/* apply symmetry for BE */
	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);
		struct snd_soc_pcm_runtime *rtd;
		struct snd_soc_dai *dai;

		/* A backend may not have the requested substream */
		if (!be_substream)
			continue;

		rtd = snd_soc_substream_to_rtd(be_substream);
		if (rtd->dai_link->be_hw_params_fixup)
			continue;

		soc_pcm_update_symmetry(be_substream);

		/* Symmetry only applies if we've got an active stream. */
		for_each_rtd_dais(rtd, i, dai) {
			err = soc_pcm_apply_symmetry(fe_substream, dai);
			if (err < 0)
				goto error;
		}
	}
error:
	return soc_pcm_ret(fe, err);
}

static int dpcm_fe_dai_startup(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(fe_substream);
	int stream = fe_substream->stream, ret = 0;

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	ret = dpcm_be_dai_startup(fe, stream);
	if (ret < 0)
		goto be_err;

	dev_dbg(fe->dev, "ASoC: open FE %s\n", fe->dai_link->name);

	/* start the DAI frontend */
	ret = __soc_pcm_open(fe, fe_substream);
	if (ret < 0)
		goto unwind;

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_OPEN;

	dpcm_runtime_setup_fe(fe_substream);

	dpcm_runtime_setup_be_format(fe_substream);
	dpcm_runtime_setup_be_chan(fe_substream);
	dpcm_runtime_setup_be_rate(fe_substream);

	ret = dpcm_apply_symmetry(fe_substream, stream);

unwind:
	if (ret < 0)
		dpcm_be_dai_startup_unwind(fe, stream);
be_err:
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);

	return soc_pcm_ret(fe, ret);
}

static int dpcm_fe_dai_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	int stream = substream->stream;

	snd_soc_dpcm_mutex_assert_held(fe);

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	/* shutdown the BEs */
	dpcm_be_dai_shutdown(fe, stream);

	dev_dbg(fe->dev, "ASoC: close FE %s\n", fe->dai_link->name);

	/* now shutdown the frontend */
	__soc_pcm_close(fe, substream);

	/* run the stream stop event */
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_STOP);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_CLOSE;
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	return 0;
}

void dpcm_be_dai_hw_free(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dpcm *dpcm;

	/* only hw_params backends that are either sinks or sources
	 * to this frontend DAI */
	for_each_dpcm_be(fe, stream, dpcm) {

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
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_SUSPEND))
			continue;

		dev_dbg(be->dev, "ASoC: hw_free BE %s\n",
			be->dai_link->name);

		__soc_pcm_hw_free(be, be_substream);

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_FREE;
	}
}

static int dpcm_fe_dai_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	int stream = substream->stream;

	snd_soc_dpcm_mutex_lock(fe);
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	dev_dbg(fe->dev, "ASoC: hw_free FE %s\n", fe->dai_link->name);

	/* call hw_free on the frontend */
	soc_pcm_hw_clean(fe, substream, 0);

	/* only hw_params backends that are either sinks or sources
	 * to this frontend DAI */
	dpcm_be_dai_hw_free(fe, stream);

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_FREE;
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);

	snd_soc_dpcm_mutex_unlock(fe);
	return 0;
}

int dpcm_be_dai_hw_params(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_pcm_runtime *be;
	struct snd_pcm_substream *be_substream;
	struct snd_soc_dpcm *dpcm;
	int ret;

	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_pcm_hw_params hw_params;

		be = dpcm->be;
		be_substream = snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		/* copy params for each dpcm */
		memcpy(&hw_params, &fe->dpcm[stream].hw_params,
				sizeof(struct snd_pcm_hw_params));

		/* perform any hw_params fixups */
		ret = snd_soc_link_be_hw_params_fixup(be, &hw_params);
		if (ret < 0)
			goto unwind;

		/* copy the fixed-up hw params for BE dai */
		memcpy(&be->dpcm[stream].hw_params, &hw_params,
		       sizeof(struct snd_pcm_hw_params));

		/* only allow hw_params() if no connected FEs are running */
		if (!snd_soc_dpcm_can_be_params(fe, be, stream))
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_OPEN) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_FREE))
			continue;

		dev_dbg(be->dev, "ASoC: hw_params BE %s\n",
			be->dai_link->name);

		ret = __soc_pcm_hw_params(be, be_substream, &hw_params);
		if (ret < 0)
			goto unwind;

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_PARAMS;
	}
	return 0;

unwind:
	dev_dbg(fe->dev, "ASoC: %s() failed at %s (%d)\n",
		__func__, be->dai_link->name, ret);

	/* disable any enabled and non active backends */
	for_each_dpcm_be_rollback(fe, stream, dpcm) {
		be = dpcm->be;
		be_substream = snd_soc_dpcm_get_substream(be, stream);

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

		__soc_pcm_hw_free(be, be_substream);
	}

	return ret;
}

static int dpcm_fe_dai_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	int ret, stream = substream->stream;

	snd_soc_dpcm_mutex_lock(fe);
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	memcpy(&fe->dpcm[stream].hw_params, params,
			sizeof(struct snd_pcm_hw_params));
	ret = dpcm_be_dai_hw_params(fe, stream);
	if (ret < 0)
		goto out;

	dev_dbg(fe->dev, "ASoC: hw_params FE %s rate %d chan %x fmt %d\n",
			fe->dai_link->name, params_rate(params),
			params_channels(params), params_format(params));

	/* call hw_params on the frontend */
	ret = __soc_pcm_hw_params(fe, substream, params);
	if (ret < 0)
		dpcm_be_dai_hw_free(fe, stream);
	else
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_HW_PARAMS;

out:
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	snd_soc_dpcm_mutex_unlock(fe);

	return soc_pcm_ret(fe, ret);
}

int dpcm_be_dai_trigger(struct snd_soc_pcm_runtime *fe, int stream,
			       int cmd)
{
	struct snd_soc_pcm_runtime *be;
	bool pause_stop_transition;
	struct snd_soc_dpcm *dpcm;
	unsigned long flags;
	int ret = 0;

	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_pcm_substream *be_substream;

		be = dpcm->be;
		be_substream = snd_soc_dpcm_get_substream(be, stream);

		snd_soc_dpcm_stream_lock_irqsave_nested(be, stream, flags);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			goto next;

		dev_dbg(be->dev, "ASoC: trigger BE %s cmd %d\n",
			be->dai_link->name, cmd);

		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
			if (!be->dpcm[stream].be_start &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PREPARE) &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP) &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PAUSED))
				goto next;

			be->dpcm[stream].be_start++;
			if (be->dpcm[stream].be_start != 1)
				goto next;

			if (be->dpcm[stream].state == SND_SOC_DPCM_STATE_PAUSED)
				ret = soc_pcm_trigger(be_substream,
						      SNDRV_PCM_TRIGGER_PAUSE_RELEASE);
			else
				ret = soc_pcm_trigger(be_substream,
						      SNDRV_PCM_TRIGGER_START);
			if (ret) {
				be->dpcm[stream].be_start--;
				goto next;
			}

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
			break;
		case SNDRV_PCM_TRIGGER_RESUME:
			if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_SUSPEND))
				goto next;

			be->dpcm[stream].be_start++;
			if (be->dpcm[stream].be_start != 1)
				goto next;

			ret = soc_pcm_trigger(be_substream, cmd);
			if (ret) {
				be->dpcm[stream].be_start--;
				goto next;
			}

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			if (!be->dpcm[stream].be_start &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START) &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PAUSED))
				goto next;

			fe->dpcm[stream].fe_pause = false;
			be->dpcm[stream].be_pause--;

			be->dpcm[stream].be_start++;
			if (be->dpcm[stream].be_start != 1)
				goto next;

			ret = soc_pcm_trigger(be_substream, cmd);
			if (ret) {
				be->dpcm[stream].be_start--;
				goto next;
			}

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_START;
			break;
		case SNDRV_PCM_TRIGGER_STOP:
			if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_START) &&
			    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PAUSED))
				goto next;

			if (be->dpcm[stream].state == SND_SOC_DPCM_STATE_START)
				be->dpcm[stream].be_start--;

			if (be->dpcm[stream].be_start != 0)
				goto next;

			pause_stop_transition = false;
			if (fe->dpcm[stream].fe_pause) {
				pause_stop_transition = true;
				fe->dpcm[stream].fe_pause = false;
				be->dpcm[stream].be_pause--;
			}

			if (be->dpcm[stream].be_pause != 0)
				ret = soc_pcm_trigger(be_substream, SNDRV_PCM_TRIGGER_PAUSE_PUSH);
			else
				ret = soc_pcm_trigger(be_substream, SNDRV_PCM_TRIGGER_STOP);

			if (ret) {
				if (be->dpcm[stream].state == SND_SOC_DPCM_STATE_START)
					be->dpcm[stream].be_start++;
				if (pause_stop_transition) {
					fe->dpcm[stream].fe_pause = true;
					be->dpcm[stream].be_pause++;
				}
				goto next;
			}

			if (be->dpcm[stream].be_pause != 0)
				be->dpcm[stream].state = SND_SOC_DPCM_STATE_PAUSED;
			else
				be->dpcm[stream].state = SND_SOC_DPCM_STATE_STOP;

			break;
		case SNDRV_PCM_TRIGGER_SUSPEND:
			if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START)
				goto next;

			be->dpcm[stream].be_start--;
			if (be->dpcm[stream].be_start != 0)
				goto next;

			ret = soc_pcm_trigger(be_substream, cmd);
			if (ret) {
				be->dpcm[stream].be_start++;
				goto next;
			}

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_SUSPEND;
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (be->dpcm[stream].state != SND_SOC_DPCM_STATE_START)
				goto next;

			fe->dpcm[stream].fe_pause = true;
			be->dpcm[stream].be_pause++;

			be->dpcm[stream].be_start--;
			if (be->dpcm[stream].be_start != 0)
				goto next;

			ret = soc_pcm_trigger(be_substream, cmd);
			if (ret) {
				be->dpcm[stream].be_start++;
				goto next;
			}

			be->dpcm[stream].state = SND_SOC_DPCM_STATE_PAUSED;
			break;
		}
next:
		snd_soc_dpcm_stream_unlock_irqrestore(be, stream, flags);
		if (ret)
			break;
	}
	return soc_pcm_ret(fe, ret);
}
EXPORT_SYMBOL_GPL(dpcm_be_dai_trigger);

static int dpcm_dai_trigger_fe_be(struct snd_pcm_substream *substream,
				  int cmd, bool fe_first)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	int ret;

	/* call trigger on the frontend before the backend. */
	if (fe_first) {
		dev_dbg(fe->dev, "ASoC: pre trigger FE %s cmd %d\n",
			fe->dai_link->name, cmd);

		ret = soc_pcm_trigger(substream, cmd);
		if (ret < 0)
			return ret;

		ret = dpcm_be_dai_trigger(fe, substream->stream, cmd);
		return ret;
	}

	/* call trigger on the frontend after the backend. */
	ret = dpcm_be_dai_trigger(fe, substream->stream, cmd);
	if (ret < 0)
		return ret;

	dev_dbg(fe->dev, "ASoC: post trigger FE %s cmd %d\n",
		fe->dai_link->name, cmd);

	ret = soc_pcm_trigger(substream, cmd);

	return ret;
}

static int dpcm_fe_dai_do_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	int stream = substream->stream;
	int ret = 0;
	enum snd_soc_dpcm_trigger trigger = fe->dai_link->trigger[stream];

	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_FE;

	switch (trigger) {
	case SND_SOC_DPCM_TRIGGER_PRE:
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		case SNDRV_PCM_TRIGGER_DRAIN:
			ret = dpcm_dai_trigger_fe_be(substream, cmd, true);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			ret = dpcm_dai_trigger_fe_be(substream, cmd, false);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case SND_SOC_DPCM_TRIGGER_POST:
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		case SNDRV_PCM_TRIGGER_DRAIN:
			ret = dpcm_dai_trigger_fe_be(substream, cmd, false);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			ret = dpcm_dai_trigger_fe_be(substream, cmd, true);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case SND_SOC_DPCM_TRIGGER_BESPOKE:
		/* bespoke trigger() - handles both FE and BEs */

		dev_dbg(fe->dev, "ASoC: bespoke trigger FE %s cmd %d\n",
				fe->dai_link->name, cmd);

		ret = snd_soc_pcm_dai_bespoke_trigger(substream, cmd);
		break;
	default:
		dev_err(fe->dev, "ASoC: invalid trigger cmd %d for %s\n", cmd,
				fe->dai_link->name);
		ret = -EINVAL;
		goto out;
	}

	if (ret < 0) {
		dev_err(fe->dev, "ASoC: trigger FE cmd: %d failed: %d\n",
			cmd, ret);
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
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_STOP;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		fe->dpcm[stream].state = SND_SOC_DPCM_STATE_PAUSED;
		break;
	}

out:
	fe->dpcm[stream].runtime_update = SND_SOC_DPCM_UPDATE_NO;
	return ret;
}

static int dpcm_fe_dai_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
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

	for_each_dpcm_be(fe, stream, dpcm) {

		struct snd_soc_pcm_runtime *be = dpcm->be;
		struct snd_pcm_substream *be_substream =
			snd_soc_dpcm_get_substream(be, stream);

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		if (!snd_soc_dpcm_can_be_prepared(fe, be, stream))
			continue;

		if ((be->dpcm[stream].state != SND_SOC_DPCM_STATE_HW_PARAMS) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_STOP) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_SUSPEND) &&
		    (be->dpcm[stream].state != SND_SOC_DPCM_STATE_PAUSED))
			continue;

		dev_dbg(be->dev, "ASoC: prepare BE %s\n",
			be->dai_link->name);

		ret = __soc_pcm_prepare(be, be_substream);
		if (ret < 0)
			break;

		be->dpcm[stream].state = SND_SOC_DPCM_STATE_PREPARE;
	}

	return soc_pcm_ret(fe, ret);
}

static int dpcm_fe_dai_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	int stream = substream->stream, ret = 0;

	snd_soc_dpcm_mutex_lock(fe);

	dev_dbg(fe->dev, "ASoC: prepare FE %s\n", fe->dai_link->name);

	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_FE);

	/* there is no point preparing this FE if there are no BEs */
	if (list_empty(&fe->dpcm[stream].be_clients)) {
		/* dev_err_once() for visibility, dev_dbg() for debugging UCM profiles */
		dev_err_once(fe->dev, "ASoC: no backend DAIs enabled for %s, possibly missing ALSA mixer-based routing or UCM profile\n",
			     fe->dai_link->name);
		dev_dbg(fe->dev, "ASoC: no backend DAIs enabled for %s\n",
			fe->dai_link->name);
		ret = -EINVAL;
		goto out;
	}

	ret = dpcm_be_dai_prepare(fe, stream);
	if (ret < 0)
		goto out;

	/* call prepare on the frontend */
	ret = __soc_pcm_prepare(fe, substream);
	if (ret < 0)
		goto out;

	fe->dpcm[stream].state = SND_SOC_DPCM_STATE_PREPARE;

out:
	dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);
	snd_soc_dpcm_mutex_unlock(fe);

	return soc_pcm_ret(fe, ret);
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

		err = snd_soc_pcm_dai_bespoke_trigger(substream, SNDRV_PCM_TRIGGER_STOP);
	} else {
		dev_dbg(fe->dev, "ASoC: trigger FE %s cmd stop\n",
			fe->dai_link->name);

		err = dpcm_be_dai_trigger(fe, stream, SNDRV_PCM_TRIGGER_STOP);
	}

	dpcm_be_dai_hw_free(fe, stream);

	dpcm_be_dai_shutdown(fe, stream);

	/* run the stream event for each BE */
	dpcm_dapm_stream_event(fe, stream, SND_SOC_DAPM_STREAM_NOP);

	return soc_pcm_ret(fe, err);
}

static int dpcm_run_update_startup(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_pcm_substream *substream =
		snd_soc_dpcm_get_substream(fe, stream);
	struct snd_soc_dpcm *dpcm;
	enum snd_soc_dpcm_trigger trigger = fe->dai_link->trigger[stream];
	int ret = 0;

	dev_dbg(fe->dev, "ASoC: runtime %s open on FE %s\n",
			stream ? "capture" : "playback", fe->dai_link->name);

	/* Only start the BE if the FE is ready */
	if (fe->dpcm[stream].state == SND_SOC_DPCM_STATE_HW_FREE ||
		fe->dpcm[stream].state == SND_SOC_DPCM_STATE_CLOSE) {
		dev_err(fe->dev, "ASoC: FE %s is not ready %d\n",
			fe->dai_link->name, fe->dpcm[stream].state);
		ret = -EINVAL;
		goto disconnect;
	}

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

		ret = snd_soc_pcm_dai_bespoke_trigger(substream, SNDRV_PCM_TRIGGER_START);
		if (ret < 0)
			goto hw_free;
	} else {
		dev_dbg(fe->dev, "ASoC: trigger FE %s cmd start\n",
			fe->dai_link->name);

		ret = dpcm_be_dai_trigger(fe, stream,
					SNDRV_PCM_TRIGGER_START);
		if (ret < 0)
			goto hw_free;
	}

	return 0;

hw_free:
	dpcm_be_dai_hw_free(fe, stream);
close:
	dpcm_be_dai_shutdown(fe, stream);
disconnect:
	/* disconnect any pending BEs */
	for_each_dpcm_be(fe, stream, dpcm) {
		struct snd_soc_pcm_runtime *be = dpcm->be;

		/* is this op for this BE ? */
		if (!snd_soc_dpcm_be_can_update(fe, be, stream))
			continue;

		if (be->dpcm[stream].state == SND_SOC_DPCM_STATE_CLOSE ||
			be->dpcm[stream].state == SND_SOC_DPCM_STATE_NEW)
				dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;
	}

	return soc_pcm_ret(fe, ret);
}

static int soc_dpcm_fe_runtime_update(struct snd_soc_pcm_runtime *fe, int new)
{
	struct snd_soc_dapm_widget_list *list;
	int stream;
	int count, paths;

	if (!fe->dai_link->dynamic)
		return 0;

	if (fe->dai_link->num_cpus > 1) {
		dev_err(fe->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	/* only check active links */
	if (!snd_soc_dai_active(snd_soc_rtd_to_cpu(fe, 0)))
		return 0;

	/* DAPM sync will call this to update DSP paths */
	dev_dbg(fe->dev, "ASoC: DPCM %s runtime update for FE %s\n",
		new ? "new" : "old", fe->dai_link->name);

	for_each_pcm_streams(stream) {

		/* skip if FE doesn't have playback/capture capability */
		if (!snd_soc_dai_stream_valid(snd_soc_rtd_to_cpu(fe, 0),   stream) ||
		    !snd_soc_dai_stream_valid(snd_soc_rtd_to_codec(fe, 0), stream))
			continue;

		/* skip if FE isn't currently playing/capturing */
		if (!snd_soc_dai_stream_active(snd_soc_rtd_to_cpu(fe, 0), stream) ||
		    !snd_soc_dai_stream_active(snd_soc_rtd_to_codec(fe, 0), stream))
			continue;

		paths = dpcm_path_get(fe, stream, &list);
		if (paths < 0)
			return paths;

		/* update any playback/capture paths */
		count = dpcm_process_paths(fe, stream, &list, new);
		if (count) {
			dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_BE);
			if (new)
				dpcm_run_update_startup(fe, stream);
			else
				dpcm_run_update_shutdown(fe, stream);
			dpcm_set_fe_update_state(fe, stream, SND_SOC_DPCM_UPDATE_NO);

			dpcm_clear_pending_state(fe, stream);
			dpcm_be_disconnect(fe, stream);
		}

		dpcm_path_put(&list);
	}

	return 0;
}

/* Called by DAPM mixer/mux changes to update audio routing between PCMs and
 * any DAI links.
 */
int snd_soc_dpcm_runtime_update(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *fe;
	int ret = 0;

	snd_soc_dpcm_mutex_lock(card);
	/* shutdown all old paths first */
	for_each_card_rtds(card, fe) {
		ret = soc_dpcm_fe_runtime_update(fe, 0);
		if (ret)
			goto out;
	}

	/* bring new paths up */
	for_each_card_rtds(card, fe) {
		ret = soc_dpcm_fe_runtime_update(fe, 1);
		if (ret)
			goto out;
	}

out:
	snd_soc_dpcm_mutex_unlock(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_runtime_update);

static void dpcm_fe_dai_cleanup(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(fe_substream);
	struct snd_soc_dpcm *dpcm;
	int stream = fe_substream->stream;

	snd_soc_dpcm_mutex_assert_held(fe);

	/* mark FE's links ready to prune */
	for_each_dpcm_be(fe, stream, dpcm)
		dpcm->state = SND_SOC_DPCM_LINK_STATE_FREE;

	dpcm_be_disconnect(fe, stream);
}

static int dpcm_fe_dai_close(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(fe_substream);
	int ret;

	snd_soc_dpcm_mutex_lock(fe);
	ret = dpcm_fe_dai_shutdown(fe_substream);

	dpcm_fe_dai_cleanup(fe_substream);

	snd_soc_dpcm_mutex_unlock(fe);
	return ret;
}

static int dpcm_fe_dai_open(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(fe_substream);
	struct snd_soc_dapm_widget_list *list;
	int ret;
	int stream = fe_substream->stream;

	snd_soc_dpcm_mutex_lock(fe);

	ret = dpcm_path_get(fe, stream, &list);
	if (ret < 0)
		goto open_end;

	/* calculate valid and active FE <-> BE dpcms */
	dpcm_process_paths(fe, stream, &list, 1);

	ret = dpcm_fe_dai_startup(fe_substream);
	if (ret < 0)
		dpcm_fe_dai_cleanup(fe_substream);

	dpcm_clear_pending_state(fe, stream);
	dpcm_path_put(&list);
open_end:
	snd_soc_dpcm_mutex_unlock(fe);
	return ret;
}

static int soc_get_playback_capture(struct snd_soc_pcm_runtime *rtd,
				    int *playback, int *capture)
{
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dai *cpu_dai;
	int has_playback = 0;
	int has_capture  = 0;
	int i;

	if (dai_link->dynamic && dai_link->num_cpus > 1) {
		dev_err(rtd->dev, "DPCM doesn't support Multi CPU for Front-Ends yet\n");
		return -EINVAL;
	}

	if (dai_link->dynamic || dai_link->no_pcm) {
		int stream;

		if (dai_link->dpcm_playback) {
			stream = SNDRV_PCM_STREAM_PLAYBACK;

			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				if (snd_soc_dai_stream_valid(cpu_dai, stream)) {
					has_playback = 1;
					break;
				}
			}
			if (!has_playback) {
				dev_err(rtd->card->dev,
					"No CPU DAIs support playback for stream %s\n",
					dai_link->stream_name);
				return -EINVAL;
			}
		}
		if (dai_link->dpcm_capture) {
			stream = SNDRV_PCM_STREAM_CAPTURE;

			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				if (snd_soc_dai_stream_valid(cpu_dai, stream)) {
					has_capture = 1;
					break;
				}
			}

			if (!has_capture) {
				dev_err(rtd->card->dev,
					"No CPU DAIs support capture for stream %s\n",
					dai_link->stream_name);
				return -EINVAL;
			}
		}
	} else {
		struct snd_soc_dai *codec_dai;

		/* Adapt stream for codec2codec links */
		int cpu_capture  = snd_soc_get_stream_cpu(dai_link, SNDRV_PCM_STREAM_CAPTURE);
		int cpu_playback = snd_soc_get_stream_cpu(dai_link, SNDRV_PCM_STREAM_PLAYBACK);

		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			if (dai_link->num_cpus == 1) {
				cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
			} else if (dai_link->num_cpus == dai_link->num_codecs) {
				cpu_dai = snd_soc_rtd_to_cpu(rtd, i);
			} else if (rtd->dai_link->num_codecs > rtd->dai_link->num_cpus) {
				int cpu_id;

				if (!rtd->dai_link->codec_ch_maps) {
					dev_err(rtd->card->dev, "%s: no codec channel mapping table provided\n",
						__func__);
					return -EINVAL;
				}

				cpu_id = rtd->dai_link->codec_ch_maps[i].connected_cpu_id;
				cpu_dai = snd_soc_rtd_to_cpu(rtd, cpu_id);
			} else {
				dev_err(rtd->card->dev,
					"%s codec number %d < cpu number %d is not supported\n",
					__func__, rtd->dai_link->num_codecs,
					rtd->dai_link->num_cpus);
				return -EINVAL;
			}

			if (snd_soc_dai_stream_valid(codec_dai, SNDRV_PCM_STREAM_PLAYBACK) &&
			    snd_soc_dai_stream_valid(cpu_dai,   cpu_playback))
				has_playback = 1;
			if (snd_soc_dai_stream_valid(codec_dai, SNDRV_PCM_STREAM_CAPTURE) &&
			    snd_soc_dai_stream_valid(cpu_dai,   cpu_capture))
				has_capture = 1;
		}
	}

	if (dai_link->playback_only)
		has_capture = 0;

	if (dai_link->capture_only)
		has_playback = 0;

	if (!has_playback && !has_capture) {
		dev_err(rtd->dev, "substream %s has no playback, no capture\n",
			dai_link->stream_name);

		return -EINVAL;
	}

	*playback = has_playback;
	*capture  = has_capture;

	return 0;
}

static int soc_create_pcm(struct snd_pcm **pcm,
			  struct snd_soc_pcm_runtime *rtd,
			  int playback, int capture, int num)
{
	char new_name[64];
	int ret;

	/* create the PCM */
	if (rtd->dai_link->c2c_params) {
		snprintf(new_name, sizeof(new_name), "codec2codec(%s)",
			 rtd->dai_link->stream_name);

		ret = snd_pcm_new_internal(rtd->card->snd_card, new_name, num,
					   playback, capture, pcm);
	} else if (rtd->dai_link->no_pcm) {
		snprintf(new_name, sizeof(new_name), "(%s)",
			rtd->dai_link->stream_name);

		ret = snd_pcm_new_internal(rtd->card->snd_card, new_name, num,
				playback, capture, pcm);
	} else {
		if (rtd->dai_link->dynamic)
			snprintf(new_name, sizeof(new_name), "%s (*)",
				rtd->dai_link->stream_name);
		else
			snprintf(new_name, sizeof(new_name), "%s %s-%d",
				rtd->dai_link->stream_name,
				soc_codec_dai_name(rtd), num);

		ret = snd_pcm_new(rtd->card->snd_card, new_name, num, playback,
			capture, pcm);
	}
	if (ret < 0) {
		dev_err(rtd->card->dev, "ASoC: can't create pcm %s for dailink %s: %d\n",
			new_name, rtd->dai_link->name, ret);
		return ret;
	}
	dev_dbg(rtd->card->dev, "ASoC: registered pcm #%d %s\n",num, new_name);

	return 0;
}

/* create a new pcm */
int soc_new_pcm(struct snd_soc_pcm_runtime *rtd, int num)
{
	struct snd_soc_component *component;
	struct snd_pcm *pcm;
	int ret = 0, playback = 0, capture = 0;
	int i;

	ret = soc_get_playback_capture(rtd, &playback, &capture);
	if (ret < 0)
		return ret;

	ret = soc_create_pcm(&pcm, rtd, playback, capture, num);
	if (ret < 0)
		return ret;

	/* DAPM dai link stream work */
	/*
	 * Currently nothing to do for c2c links
	 * Since c2c links are internal nodes in the DAPM graph and
	 * don't interface with the outside world or application layer
	 * we don't have to do any special handling on close.
	 */
	if (!rtd->dai_link->c2c_params)
		rtd->close_delayed_work_func = snd_soc_close_delayed_work;

	rtd->pcm = pcm;
	pcm->nonatomic = rtd->dai_link->nonatomic;
	pcm->private_data = rtd;
	pcm->no_device_suspend = true;

	if (rtd->dai_link->no_pcm || rtd->dai_link->c2c_params) {
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
	} else {
		rtd->ops.open		= soc_pcm_open;
		rtd->ops.hw_params	= soc_pcm_hw_params;
		rtd->ops.prepare	= soc_pcm_prepare;
		rtd->ops.trigger	= soc_pcm_trigger;
		rtd->ops.hw_free	= soc_pcm_hw_free;
		rtd->ops.close		= soc_pcm_close;
		rtd->ops.pointer	= soc_pcm_pointer;
	}

	for_each_rtd_components(rtd, i, component) {
		const struct snd_soc_component_driver *drv = component->driver;

		if (drv->ioctl)
			rtd->ops.ioctl		= snd_soc_pcm_component_ioctl;
		if (drv->sync_stop)
			rtd->ops.sync_stop	= snd_soc_pcm_component_sync_stop;
		if (drv->copy)
			rtd->ops.copy		= snd_soc_pcm_component_copy;
		if (drv->page)
			rtd->ops.page		= snd_soc_pcm_component_page;
		if (drv->mmap)
			rtd->ops.mmap		= snd_soc_pcm_component_mmap;
		if (drv->ack)
			rtd->ops.ack            = snd_soc_pcm_component_ack;
	}

	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &rtd->ops);

	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &rtd->ops);

	ret = snd_soc_pcm_component_new(rtd);
	if (ret < 0)
		return ret;
out:
	dev_dbg(rtd->card->dev, "%s <-> %s mapping ok\n",
		soc_codec_dai_name(rtd), soc_cpu_dai_name(rtd));
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

static int snd_soc_dpcm_check_state(struct snd_soc_pcm_runtime *fe,
				    struct snd_soc_pcm_runtime *be,
				    int stream,
				    const enum snd_soc_dpcm_state *states,
				    int num_states)
{
	struct snd_soc_dpcm *dpcm;
	int state;
	int ret = 1;
	int i;

	for_each_dpcm_fe(be, stream, dpcm) {

		if (dpcm->fe == fe)
			continue;

		state = dpcm->fe->dpcm[stream].state;
		for (i = 0; i < num_states; i++) {
			if (state == states[i]) {
				ret = 0;
				break;
			}
		}
	}

	/* it's safe to do this BE DAI */
	return ret;
}

/*
 * We can only hw_free, stop, pause or suspend a BE DAI if any of it's FE
 * are not running, paused or suspended for the specified stream direction.
 */
int snd_soc_dpcm_can_be_free_stop(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	const enum snd_soc_dpcm_state state[] = {
		SND_SOC_DPCM_STATE_START,
		SND_SOC_DPCM_STATE_PAUSED,
		SND_SOC_DPCM_STATE_SUSPEND,
	};

	return snd_soc_dpcm_check_state(fe, be, stream, state, ARRAY_SIZE(state));
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_can_be_free_stop);

/*
 * We can only change hw params a BE DAI if any of it's FE are not prepared,
 * running, paused or suspended for the specified stream direction.
 */
int snd_soc_dpcm_can_be_params(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	const enum snd_soc_dpcm_state state[] = {
		SND_SOC_DPCM_STATE_START,
		SND_SOC_DPCM_STATE_PAUSED,
		SND_SOC_DPCM_STATE_SUSPEND,
		SND_SOC_DPCM_STATE_PREPARE,
	};

	return snd_soc_dpcm_check_state(fe, be, stream, state, ARRAY_SIZE(state));
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_can_be_params);

/*
 * We can only prepare a BE DAI if any of it's FE are not prepared,
 * running or paused for the specified stream direction.
 */
int snd_soc_dpcm_can_be_prepared(struct snd_soc_pcm_runtime *fe,
				 struct snd_soc_pcm_runtime *be, int stream)
{
	const enum snd_soc_dpcm_state state[] = {
		SND_SOC_DPCM_STATE_START,
		SND_SOC_DPCM_STATE_PAUSED,
		SND_SOC_DPCM_STATE_PREPARE,
	};

	return snd_soc_dpcm_check_state(fe, be, stream, state, ARRAY_SIZE(state));
}
EXPORT_SYMBOL_GPL(snd_soc_dpcm_can_be_prepared);
