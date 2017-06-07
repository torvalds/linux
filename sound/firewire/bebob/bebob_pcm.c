/*
 * bebob_pcm.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./bebob.h"

static int
hw_rule_rate(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	struct snd_bebob_stream_formation *formations = rule->private;
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i;

	for (i = 0; i < SND_BEBOB_STRM_FMT_ENTRIES; i++) {
		/* entry is invalid */
		if (formations[i].pcm == 0)
			continue;

		if (!snd_interval_test(c, formations[i].pcm))
			continue;

		t.min = min(t.min, snd_bebob_rate_table[i]);
		t.max = max(t.max, snd_bebob_rate_table[i]);

	}
	return snd_interval_refine(r, &t);
}

static int
hw_rule_channels(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	struct snd_bebob_stream_formation *formations = rule->private;
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};

	unsigned int i;

	for (i = 0; i < SND_BEBOB_STRM_FMT_ENTRIES; i++) {
		/* entry is invalid */
		if (formations[i].pcm == 0)
			continue;

		if (!snd_interval_test(r, snd_bebob_rate_table[i]))
			continue;

		t.min = min(t.min, formations[i].pcm);
		t.max = max(t.max, formations[i].pcm);
	}

	return snd_interval_refine(c, &t);
}

static void
limit_channels_and_rates(struct snd_pcm_hardware *hw,
			 struct snd_bebob_stream_formation *formations)
{
	unsigned int i;

	hw->channels_min = UINT_MAX;
	hw->channels_max = 0;

	hw->rate_min = UINT_MAX;
	hw->rate_max = 0;
	hw->rates = 0;

	for (i = 0; i < SND_BEBOB_STRM_FMT_ENTRIES; i++) {
		/* entry has no PCM channels */
		if (formations[i].pcm == 0)
			continue;

		hw->channels_min = min(hw->channels_min, formations[i].pcm);
		hw->channels_max = max(hw->channels_max, formations[i].pcm);

		hw->rate_min = min(hw->rate_min, snd_bebob_rate_table[i]);
		hw->rate_max = max(hw->rate_max, snd_bebob_rate_table[i]);
		hw->rates |= snd_pcm_rate_to_rate_bit(snd_bebob_rate_table[i]);
	}
}

static void
limit_period_and_buffer(struct snd_pcm_hardware *hw)
{
	hw->periods_min = 2;		/* SNDRV_PCM_INFO_BATCH */
	hw->periods_max = UINT_MAX;

	hw->period_bytes_min = 4 * hw->channels_max;	/* bytes for a frame */

	/* Just to prevent from allocating much pages. */
	hw->period_bytes_max = hw->period_bytes_min * 2048;
	hw->buffer_bytes_max = hw->period_bytes_max * hw->periods_min;
}

static int
pcm_init_hw_params(struct snd_bebob *bebob,
		   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct amdtp_stream *s;
	struct snd_bebob_stream_formation *formations;
	int err;

	runtime->hw.info = SNDRV_PCM_INFO_BATCH |
			   SNDRV_PCM_INFO_BLOCK_TRANSFER |
			   SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_JOINT_DUPLEX |
			   SNDRV_PCM_INFO_MMAP |
			   SNDRV_PCM_INFO_MMAP_VALID;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.formats = AM824_IN_PCM_FORMAT_BITS;
		s = &bebob->tx_stream;
		formations = bebob->tx_stream_formations;
	} else {
		runtime->hw.formats = AM824_OUT_PCM_FORMAT_BITS;
		s = &bebob->rx_stream;
		formations = bebob->rx_stream_formations;
	}

	limit_channels_and_rates(&runtime->hw, formations);
	limit_period_and_buffer(&runtime->hw);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  hw_rule_channels, formations,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto end;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  hw_rule_rate, formations,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto end;

	err = amdtp_am824_add_pcm_hw_constraints(s, runtime);
end:
	return err;
}

static int
pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	const struct snd_bebob_rate_spec *spec = bebob->spec->rate;
	unsigned int sampling_rate;
	enum snd_bebob_clock_type src;
	int err;

	err = snd_bebob_stream_lock_try(bebob);
	if (err < 0)
		goto end;

	err = pcm_init_hw_params(bebob, substream);
	if (err < 0)
		goto err_locked;

	err = snd_bebob_stream_get_clock_src(bebob, &src);
	if (err < 0)
		goto err_locked;

	/*
	 * When source of clock is internal or any PCM stream are running,
	 * the available sampling rate is limited at current sampling rate.
	 */
	if (src == SND_BEBOB_CLOCK_TYPE_EXTERNAL ||
	    amdtp_stream_pcm_running(&bebob->tx_stream) ||
	    amdtp_stream_pcm_running(&bebob->rx_stream)) {
		err = spec->get(bebob, &sampling_rate);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to get sampling rate: %d\n", err);
			goto err_locked;
		}

		substream->runtime->hw.rate_min = sampling_rate;
		substream->runtime->hw.rate_max = sampling_rate;
	}

	snd_pcm_set_sync(substream);
end:
	return err;
err_locked:
	snd_bebob_stream_lock_release(bebob);
	return err;
}

static int
pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	snd_bebob_stream_lock_release(bebob);
	return 0;
}

static int
pcm_capture_hw_params(struct snd_pcm_substream *substream,
		      struct snd_pcm_hw_params *hw_params)
{
	struct snd_bebob *bebob = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&bebob->mutex);
		bebob->substreams_counter++;
		mutex_unlock(&bebob->mutex);
	}

	return 0;
}
static int
pcm_playback_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *hw_params)
{
	struct snd_bebob *bebob = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&bebob->mutex);
		bebob->substreams_counter++;
		mutex_unlock(&bebob->mutex);
	}

	return 0;
}

static int
pcm_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&bebob->mutex);
		bebob->substreams_counter--;
		mutex_unlock(&bebob->mutex);
	}

	snd_bebob_stream_stop_duplex(bebob);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}
static int
pcm_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&bebob->mutex);
		bebob->substreams_counter--;
		mutex_unlock(&bebob->mutex);
	}

	snd_bebob_stream_stop_duplex(bebob);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int
pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_bebob_stream_start_duplex(bebob, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&bebob->tx_stream);

	return err;
}
static int
pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_bebob_stream_start_duplex(bebob, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&bebob->rx_stream);

	return err;
}

static int
pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_bebob *bebob = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&bebob->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&bebob->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static int
pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_bebob *bebob = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&bebob->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&bebob->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t
pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_bebob *bebob = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&bebob->tx_stream);
}
static snd_pcm_uframes_t
pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_bebob *bebob = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&bebob->rx_stream);
}

static int pcm_capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;

	return amdtp_stream_pcm_ack(&bebob->tx_stream);
}

static int pcm_playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;

	return amdtp_stream_pcm_ack(&bebob->rx_stream);
}

int snd_bebob_create_pcm_devices(struct snd_bebob *bebob)
{
	static const struct snd_pcm_ops capture_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_capture_hw_params,
		.hw_free	= pcm_capture_hw_free,
		.prepare	= pcm_capture_prepare,
		.trigger	= pcm_capture_trigger,
		.pointer	= pcm_capture_pointer,
		.ack		= pcm_capture_ack,
		.page		= snd_pcm_lib_get_vmalloc_page,
	};
	static const struct snd_pcm_ops playback_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_playback_hw_params,
		.hw_free	= pcm_playback_hw_free,
		.prepare	= pcm_playback_prepare,
		.trigger	= pcm_playback_trigger,
		.pointer	= pcm_playback_pointer,
		.ack		= pcm_playback_ack,
		.page		= snd_pcm_lib_get_vmalloc_page,
		.mmap		= snd_pcm_lib_mmap_vmalloc,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(bebob->card, bebob->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		goto end;

	pcm->private_data = bebob;
	snprintf(pcm->name, sizeof(pcm->name),
		 "%s PCM", bebob->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_ops);
end:
	return err;
}
