// SPDX-License-Identifier: GPL-2.0-only
/*
 * ff-pcm.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 */

#include "ff.h"

static int hw_rule_rate(struct snd_pcm_hw_params *params,
			struct snd_pcm_hw_rule *rule)
{
	const unsigned int *pcm_channels = rule->private;
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(amdtp_rate_table); i++) {
		enum snd_ff_stream_mode mode;
		int err;

		err = snd_ff_stream_get_multiplier_mode(i, &mode);
		if (err < 0)
			continue;

		if (!snd_interval_test(c, pcm_channels[mode]))
			continue;

		t.min = min(t.min, amdtp_rate_table[i]);
		t.max = max(t.max, amdtp_rate_table[i]);
	}

	return snd_interval_refine(r, &t);
}

static int hw_rule_channels(struct snd_pcm_hw_params *params,
			    struct snd_pcm_hw_rule *rule)
{
	const unsigned int *pcm_channels = rule->private;
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(amdtp_rate_table); i++) {
		enum snd_ff_stream_mode mode;
		int err;

		err = snd_ff_stream_get_multiplier_mode(i, &mode);
		if (err < 0)
			continue;

		if (!snd_interval_test(r, amdtp_rate_table[i]))
			continue;

		t.min = min(t.min, pcm_channels[mode]);
		t.max = max(t.max, pcm_channels[mode]);
	}

	return snd_interval_refine(c, &t);
}

static void limit_channels_and_rates(struct snd_pcm_hardware *hw,
				     const unsigned int *pcm_channels)
{
	unsigned int rate, channels;
	int i;

	hw->channels_min = UINT_MAX;
	hw->channels_max = 0;
	hw->rate_min = UINT_MAX;
	hw->rate_max = 0;

	for (i = 0; i < ARRAY_SIZE(amdtp_rate_table); i++) {
		enum snd_ff_stream_mode mode;
		int err;

		err = snd_ff_stream_get_multiplier_mode(i, &mode);
		if (err < 0)
			continue;

		channels = pcm_channels[mode];
		if (pcm_channels[mode] == 0)
			continue;
		hw->channels_min = min(hw->channels_min, channels);
		hw->channels_max = max(hw->channels_max, channels);

		rate = amdtp_rate_table[i];
		hw->rates |= snd_pcm_rate_to_rate_bit(rate);
		hw->rate_min = min(hw->rate_min, rate);
		hw->rate_max = max(hw->rate_max, rate);
	}
}

static int pcm_init_hw_params(struct snd_ff *ff,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct amdtp_stream *s;
	const unsigned int *pcm_channels;
	int err;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		s = &ff->tx_stream;
		pcm_channels = ff->spec->pcm_capture_channels;
	} else {
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		s = &ff->rx_stream;
		pcm_channels = ff->spec->pcm_playback_channels;
	}

	limit_channels_and_rates(&runtime->hw, pcm_channels);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  hw_rule_channels, (void *)pcm_channels,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  hw_rule_rate, (void *)pcm_channels,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;

	return amdtp_ff_add_pcm_hw_constraints(s, runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;
	struct amdtp_domain *d = &ff->domain;
	unsigned int rate;
	enum snd_ff_clock_src src;
	int i, err;

	err = snd_ff_stream_lock_try(ff);
	if (err < 0)
		return err;

	err = pcm_init_hw_params(ff, substream);
	if (err < 0)
		goto release_lock;

	err = ff->spec->protocol->get_clock(ff, &rate, &src);
	if (err < 0)
		goto release_lock;

	scoped_guard(mutex, &ff->mutex) {
		// When source of clock is not internal or any stream is reserved for
		// transmission of PCM frames, the available sampling rate is limited
		// at current one.
		if (src != SND_FF_CLOCK_SRC_INTERNAL) {
			for (i = 0; i < CIP_SFC_COUNT; ++i) {
				if (amdtp_rate_table[i] == rate)
					break;
			}

			// The unit is configured at sampling frequency which packet
			// streaming engine can't support.
			if (i >= CIP_SFC_COUNT) {
				err = -EIO;
				goto release_lock;
			}

			substream->runtime->hw.rate_min = rate;
			substream->runtime->hw.rate_max = rate;
		} else {
			if (ff->substreams_counter > 0) {
				unsigned int frames_per_period = d->events_per_period;
				unsigned int frames_per_buffer = d->events_per_buffer;

				rate = amdtp_rate_table[ff->rx_stream.sfc];
				substream->runtime->hw.rate_min = rate;
				substream->runtime->hw.rate_max = rate;

				err = snd_pcm_hw_constraint_minmax(substream->runtime,
								   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
								   frames_per_period, frames_per_period);
				if (err < 0)
					goto release_lock;

				err = snd_pcm_hw_constraint_minmax(substream->runtime,
								   SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
								   frames_per_buffer, frames_per_buffer);
				if (err < 0)
					goto release_lock;
			}
		}
	}

	snd_pcm_set_sync(substream);

	return 0;

release_lock:
	snd_ff_stream_lock_release(ff);
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;

	snd_ff_stream_lock_release(ff);

	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_ff *ff = substream->private_data;
	int err = 0;

	if (substream->runtime->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);
		unsigned int frames_per_period = params_period_size(hw_params);
		unsigned int frames_per_buffer = params_buffer_size(hw_params);

		guard(mutex)(&ff->mutex);
		err = snd_ff_stream_reserve_duplex(ff, rate, frames_per_period,
						   frames_per_buffer);
		if (err >= 0)
			++ff->substreams_counter;
	}

	return err;
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;

	guard(mutex)(&ff->mutex);

	if (substream->runtime->state != SNDRV_PCM_STATE_OPEN)
		--ff->substreams_counter;

	snd_ff_stream_stop_duplex(ff);

	return 0;
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	guard(mutex)(&ff->mutex);

	err = snd_ff_stream_start_duplex(ff, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&ff->tx_stream);

	return err;
}

static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	guard(mutex)(&ff->mutex);

	err = snd_ff_stream_start_duplex(ff, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&ff->rx_stream);

	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_ff *ff = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&ff->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&ff->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_ff *ff = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&ff->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&ff->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_ff *ff = sbstrm->private_data;

	return amdtp_domain_stream_pcm_pointer(&ff->domain, &ff->tx_stream);
}

static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_ff *ff = sbstrm->private_data;

	return amdtp_domain_stream_pcm_pointer(&ff->domain, &ff->rx_stream);
}

static int pcm_capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&ff->domain, &ff->tx_stream);
}

static int pcm_playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_ff *ff = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&ff->domain, &ff->rx_stream);
}

int snd_ff_create_pcm_devices(struct snd_ff *ff)
{
	static const struct snd_pcm_ops pcm_capture_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.hw_params	= pcm_hw_params,
		.hw_free	= pcm_hw_free,
		.prepare	= pcm_capture_prepare,
		.trigger	= pcm_capture_trigger,
		.pointer	= pcm_capture_pointer,
		.ack		= pcm_capture_ack,
	};
	static const struct snd_pcm_ops pcm_playback_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.hw_params	= pcm_hw_params,
		.hw_free	= pcm_hw_free,
		.prepare	= pcm_playback_prepare,
		.trigger	= pcm_playback_trigger,
		.pointer	= pcm_playback_pointer,
		.ack		= pcm_playback_ack,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(ff->card, ff->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = ff;
	pcm->nonatomic = true;
	snprintf(pcm->name, sizeof(pcm->name),
		 "%s PCM", ff->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);

	return 0;
}
