/*
 * oxfw_pcm.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

static int hw_rule_rate(struct snd_pcm_hw_params *params,
			struct snd_pcm_hw_rule *rule)
{
	u8 **formats = rule->private;
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	struct snd_oxfw_stream_formation formation;
	unsigned int i, err;

	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		if (formats[i] == NULL)
			continue;

		err = snd_oxfw_stream_parse_format(formats[i], &formation);
		if (err < 0)
			continue;
		if (!snd_interval_test(c, formation.pcm))
			continue;

		t.min = min(t.min, formation.rate);
		t.max = max(t.max, formation.rate);

	}
	return snd_interval_refine(r, &t);
}

static int hw_rule_channels(struct snd_pcm_hw_params *params,
			    struct snd_pcm_hw_rule *rule)
{
	u8 **formats = rule->private;
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_oxfw_stream_formation formation;
	unsigned int i, j, err;
	unsigned int count, list[SND_OXFW_STREAM_FORMAT_ENTRIES] = {0};

	count = 0;
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		if (formats[i] == NULL)
			break;

		err = snd_oxfw_stream_parse_format(formats[i], &formation);
		if (err < 0)
			continue;
		if (!snd_interval_test(r, formation.rate))
			continue;
		if (list[count] == formation.pcm)
			continue;

		for (j = 0; j < ARRAY_SIZE(list); j++) {
			if (list[j] == formation.pcm)
				break;
		}
		if (j == ARRAY_SIZE(list)) {
			list[count] = formation.pcm;
			if (++count == ARRAY_SIZE(list))
				break;
		}
	}

	return snd_interval_list(c, count, list, 0);
}

static void limit_channels_and_rates(struct snd_pcm_hardware *hw, u8 **formats)
{
	struct snd_oxfw_stream_formation formation;
	unsigned int i, err;

	hw->channels_min = UINT_MAX;
	hw->channels_max = 0;

	hw->rate_min = UINT_MAX;
	hw->rate_max = 0;
	hw->rates = 0;

	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		if (formats[i] == NULL)
			break;

		err = snd_oxfw_stream_parse_format(formats[i], &formation);
		if (err < 0)
			continue;

		hw->channels_min = min(hw->channels_min, formation.pcm);
		hw->channels_max = max(hw->channels_max, formation.pcm);

		hw->rate_min = min(hw->rate_min, formation.rate);
		hw->rate_max = max(hw->rate_max, formation.rate);
		hw->rates |= snd_pcm_rate_to_rate_bit(formation.rate);
	}
}

static void limit_period_and_buffer(struct snd_pcm_hardware *hw)
{
	hw->periods_min = 2;		/* SNDRV_PCM_INFO_BATCH */
	hw->periods_max = UINT_MAX;

	hw->period_bytes_min = 4 * hw->channels_max;	/* bytes for a frame */

	/* Just to prevent from allocating much pages. */
	hw->period_bytes_max = hw->period_bytes_min * 2048;
	hw->buffer_bytes_max = hw->period_bytes_max * hw->periods_min;
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	u8 **formats;
	int err;

	formats = oxfw->rx_stream_formats;

	runtime->hw.info = SNDRV_PCM_INFO_BATCH |
			   SNDRV_PCM_INFO_BLOCK_TRANSFER |
			   SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_MMAP |
			   SNDRV_PCM_INFO_MMAP_VALID;

	limit_channels_and_rates(&runtime->hw, formats);
	limit_period_and_buffer(&runtime->hw);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  hw_rule_channels, formats,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto end;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  hw_rule_rate, formats,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto end;

	err = amdtp_stream_add_pcm_hw_constraints(&oxfw->rx_stream, runtime);
	if (err < 0)
		goto end;

	snd_pcm_set_sync(substream);
end:
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_oxfw *oxfw = substream->private_data;

	amdtp_stream_set_pcm_format(&oxfw->rx_stream, params_format(hw_params));
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	mutex_lock(&oxfw->mutex);
	snd_oxfw_stream_stop_simplex(oxfw);
	mutex_unlock(&oxfw->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	mutex_lock(&oxfw->mutex);
	err = snd_oxfw_stream_start_simplex(oxfw, runtime->rate,
					    runtime->channels);
	mutex_unlock(&oxfw->mutex);
	if (err < 0)
		goto end;

	amdtp_stream_pcm_prepare(&oxfw->rx_stream);
end:
	return err;
}

static int pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_substream *pcm;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pcm = NULL;
		break;
	default:
		return -EINVAL;
	}
	amdtp_stream_pcm_trigger(&oxfw->rx_stream, pcm);
	return 0;
}

static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	return amdtp_stream_pcm_pointer(&oxfw->rx_stream);
}

int snd_oxfw_create_pcm(struct snd_oxfw *oxfw)
{
	static struct snd_pcm_ops ops = {
		.open      = pcm_open,
		.close     = pcm_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = pcm_hw_params,
		.hw_free   = pcm_hw_free,
		.prepare   = pcm_prepare,
		.trigger   = pcm_trigger,
		.pointer   = pcm_pointer,
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(oxfw->card, oxfw->card->driver, 0, 1, 0, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = oxfw;
	strcpy(pcm->name, oxfw->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &ops);
	return 0;
}
