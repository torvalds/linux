// SPDX-License-Identifier: GPL-2.0-only
/*
 * motu-pcm.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#include <sound/pcm_params.h>
#include "motu.h"

static int motu_rate_constraint(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_motu_packet_format *formats = rule->private;

	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval rates = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, pcm_channels, rate, mode;

	for (i = 0; i < ARRAY_SIZE(snd_motu_clock_rates); ++i) {
		rate = snd_motu_clock_rates[i];
		mode = i / 2;

		pcm_channels = formats->fixed_part_pcm_chunks[mode] +
			       formats->differed_part_pcm_chunks[mode];
		if (!snd_interval_test(c, pcm_channels))
			continue;

		rates.min = min(rates.min, rate);
		rates.max = max(rates.max, rate);
	}

	return snd_interval_refine(r, &rates);
}

static int motu_channels_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_motu_packet_format *formats = rule->private;

	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval channels = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, pcm_channels, rate, mode;

	for (i = 0; i < ARRAY_SIZE(snd_motu_clock_rates); ++i) {
		rate = snd_motu_clock_rates[i];
		mode = i / 2;

		if (!snd_interval_test(r, rate))
			continue;

		pcm_channels = formats->fixed_part_pcm_chunks[mode] +
			       formats->differed_part_pcm_chunks[mode];
		channels.min = min(channels.min, pcm_channels);
		channels.max = max(channels.max, pcm_channels);
	}

	return snd_interval_refine(c, &channels);
}

static void limit_channels_and_rates(struct snd_motu *motu,
				     struct snd_pcm_runtime *runtime,
				     struct snd_motu_packet_format *formats)
{
	struct snd_pcm_hardware *hw = &runtime->hw;
	unsigned int i, pcm_channels, rate, mode;

	hw->channels_min = UINT_MAX;
	hw->channels_max = 0;

	for (i = 0; i < ARRAY_SIZE(snd_motu_clock_rates); ++i) {
		rate = snd_motu_clock_rates[i];
		mode = i / 2;

		pcm_channels = formats->fixed_part_pcm_chunks[mode] +
			       formats->differed_part_pcm_chunks[mode];
		if (pcm_channels == 0)
			continue;

		hw->rates |= snd_pcm_rate_to_rate_bit(rate);
		hw->channels_min = min(hw->channels_min, pcm_channels);
		hw->channels_max = max(hw->channels_max, pcm_channels);
	}

	snd_pcm_limit_hw_rates(runtime);
}

static int init_hw_info(struct snd_motu *motu,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct amdtp_stream *stream;
	struct snd_motu_packet_format *formats;
	int err;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		hw->formats = SNDRV_PCM_FMTBIT_S32;
		stream = &motu->tx_stream;
		formats = &motu->tx_packet_formats;
	} else {
		hw->formats = SNDRV_PCM_FMTBIT_S32;
		stream = &motu->rx_stream;
		formats = &motu->rx_packet_formats;
	}

	limit_channels_and_rates(motu, runtime, formats);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  motu_rate_constraint, formats,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  motu_channels_constraint, formats,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;

	return amdtp_motu_add_pcm_hw_constraints(stream, runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;
	const struct snd_motu_protocol *const protocol = motu->spec->protocol;
	struct amdtp_domain *d = &motu->domain;
	enum snd_motu_clock_source src;
	int err;

	err = snd_motu_stream_lock_try(motu);
	if (err < 0)
		return err;

	mutex_lock(&motu->mutex);

	err = snd_motu_stream_cache_packet_formats(motu);
	if (err < 0)
		goto err_locked;

	err = init_hw_info(motu, substream);
	if (err < 0)
		goto err_locked;

	err = protocol->get_clock_source(motu, &src);
	if (err < 0)
		goto err_locked;

	// When source of clock is not internal or any stream is reserved for
	// transmission of PCM frames, the available sampling rate is limited
	// at current one.
	if ((src != SND_MOTU_CLOCK_SOURCE_INTERNAL &&
	     src != SND_MOTU_CLOCK_SOURCE_SPH) ||
	    (motu->substreams_counter > 0 && d->events_per_period > 0)) {
		unsigned int frames_per_period = d->events_per_period;
		unsigned int frames_per_buffer = d->events_per_buffer;
		unsigned int rate;

		err = protocol->get_clock_rate(motu, &rate);
		if (err < 0)
			goto err_locked;

		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;

		if (frames_per_period > 0) {
			err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					frames_per_period, frames_per_period);
			if (err < 0)
				goto err_locked;

			err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					frames_per_buffer, frames_per_buffer);
			if (err < 0)
				goto err_locked;
		}
	}

	snd_pcm_set_sync(substream);

	mutex_unlock(&motu->mutex);

	return 0;
err_locked:
	mutex_unlock(&motu->mutex);
	snd_motu_stream_lock_release(motu);
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;

	snd_motu_stream_lock_release(motu);

	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_motu *motu = substream->private_data;
	int err = 0;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);
		unsigned int frames_per_period = params_period_size(hw_params);
		unsigned int frames_per_buffer = params_buffer_size(hw_params);

		mutex_lock(&motu->mutex);
		err = snd_motu_stream_reserve_duplex(motu, rate,
					frames_per_period, frames_per_buffer);
		if (err >= 0)
			++motu->substreams_counter;
		mutex_unlock(&motu->mutex);
	}

	return err;
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;

	mutex_lock(&motu->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		--motu->substreams_counter;

	snd_motu_stream_stop_duplex(motu);

	mutex_unlock(&motu->mutex);

	return 0;
}

static int capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;
	int err;

	mutex_lock(&motu->mutex);
	err = snd_motu_stream_start_duplex(motu);
	mutex_unlock(&motu->mutex);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&motu->tx_stream);

	return 0;
}
static int playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;
	int err;

	mutex_lock(&motu->mutex);
	err = snd_motu_stream_start_duplex(motu);
	mutex_unlock(&motu->mutex);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&motu->rx_stream);

	return err;
}

static int capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_motu *motu = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&motu->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&motu->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static int playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_motu *motu = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&motu->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&motu->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;

	return amdtp_domain_stream_pcm_pointer(&motu->domain, &motu->tx_stream);
}
static snd_pcm_uframes_t playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;

	return amdtp_domain_stream_pcm_pointer(&motu->domain, &motu->rx_stream);
}

static int capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&motu->domain, &motu->tx_stream);
}

static int playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_motu *motu = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&motu->domain, &motu->rx_stream);
}

int snd_motu_create_pcm_devices(struct snd_motu *motu)
{
	static const struct snd_pcm_ops capture_ops = {
		.open      = pcm_open,
		.close     = pcm_close,
		.hw_params = pcm_hw_params,
		.hw_free   = pcm_hw_free,
		.prepare   = capture_prepare,
		.trigger   = capture_trigger,
		.pointer   = capture_pointer,
		.ack       = capture_ack,
	};
	static const struct snd_pcm_ops playback_ops = {
		.open      = pcm_open,
		.close     = pcm_close,
		.hw_params = pcm_hw_params,
		.hw_free   = pcm_hw_free,
		.prepare   = playback_prepare,
		.trigger   = playback_trigger,
		.pointer   = playback_pointer,
		.ack       = playback_ack,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(motu->card, motu->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = motu;
	strcpy(pcm->name, motu->card->shortname);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

	return 0;
}
