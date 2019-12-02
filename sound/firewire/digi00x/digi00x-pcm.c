// SPDX-License-Identifier: GPL-2.0-only
/*
 * digi00x-pcm.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 */

#include "digi00x.h"

static int hw_rule_rate(struct snd_pcm_hw_params *params,
			struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1,
	};
	unsigned int i;

	for (i = 0; i < SND_DG00X_RATE_COUNT; i++) {
		if (!snd_interval_test(c,
				       snd_dg00x_stream_pcm_channels[i]))
			continue;

		t.min = min(t.min, snd_dg00x_stream_rates[i]);
		t.max = max(t.max, snd_dg00x_stream_rates[i]);
	}

	return snd_interval_refine(r, &t);
}

static int hw_rule_channels(struct snd_pcm_hw_params *params,
			    struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1,
	};
	unsigned int i;

	for (i = 0; i < SND_DG00X_RATE_COUNT; i++) {
		if (!snd_interval_test(r, snd_dg00x_stream_rates[i]))
			continue;

		t.min = min(t.min, snd_dg00x_stream_pcm_channels[i]);
		t.max = max(t.max, snd_dg00x_stream_pcm_channels[i]);
	}

	return snd_interval_refine(c, &t);
}

static int pcm_init_hw_params(struct snd_dg00x *dg00x,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct amdtp_stream *s;
	int err;


	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		substream->runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		s = &dg00x->tx_stream;
	} else {
		substream->runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		s = &dg00x->rx_stream;
	}

	hw->channels_min = 10;
	hw->channels_max = 18;

	hw->rates = SNDRV_PCM_RATE_44100 |
		    SNDRV_PCM_RATE_48000 |
		    SNDRV_PCM_RATE_88200 |
		    SNDRV_PCM_RATE_96000;
	snd_pcm_limit_hw_rates(runtime);

	err = snd_pcm_hw_rule_add(substream->runtime, 0,
				  SNDRV_PCM_HW_PARAM_CHANNELS,
				  hw_rule_channels, NULL,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;

	err = snd_pcm_hw_rule_add(substream->runtime, 0,
				  SNDRV_PCM_HW_PARAM_RATE,
				  hw_rule_rate, NULL,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;

	return amdtp_dot_add_pcm_hw_constraints(s, substream->runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;
	struct amdtp_domain *d = &dg00x->domain;
	enum snd_dg00x_clock clock;
	bool detect;
	int err;

	err = snd_dg00x_stream_lock_try(dg00x);
	if (err < 0)
		return err;

	err = pcm_init_hw_params(dg00x, substream);
	if (err < 0)
		goto err_locked;

	/* Check current clock source. */
	err = snd_dg00x_stream_get_clock(dg00x, &clock);
	if (err < 0)
		goto err_locked;
	if (clock != SND_DG00X_CLOCK_INTERNAL) {
		err = snd_dg00x_stream_check_external_clock(dg00x, &detect);
		if (err < 0)
			goto err_locked;
		if (!detect) {
			err = -EBUSY;
			goto err_locked;
		}
	}

	mutex_lock(&dg00x->mutex);

	// When source of clock is not internal or any stream is reserved for
	// transmission of PCM frames, the available sampling rate is limited
	// at current one.
	if ((clock != SND_DG00X_CLOCK_INTERNAL) ||
	    (dg00x->substreams_counter > 0 && d->events_per_period > 0)) {
		unsigned int frames_per_period = d->events_per_period;
		unsigned int frames_per_buffer = d->events_per_buffer;
		unsigned int rate;

		err = snd_dg00x_stream_get_external_rate(dg00x, &rate);
		if (err < 0) {
			mutex_unlock(&dg00x->mutex);
			goto err_locked;
		}
		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;

		if (frames_per_period > 0) {
			err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					frames_per_period, frames_per_period);
			if (err < 0) {
				mutex_unlock(&dg00x->mutex);
				goto err_locked;
			}

			err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					frames_per_buffer, frames_per_buffer);
			if (err < 0) {
				mutex_unlock(&dg00x->mutex);
				goto err_locked;
			}
		}
	}

	mutex_unlock(&dg00x->mutex);

	snd_pcm_set_sync(substream);

	return 0;
err_locked:
	snd_dg00x_stream_lock_release(dg00x);
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;

	snd_dg00x_stream_lock_release(dg00x);

	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_dg00x *dg00x = substream->private_data;
	int err;

	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);
		unsigned int frames_per_period = params_period_size(hw_params);
		unsigned int frames_per_buffer = params_buffer_size(hw_params);

		mutex_lock(&dg00x->mutex);
		err = snd_dg00x_stream_reserve_duplex(dg00x, rate,
					frames_per_period, frames_per_buffer);
		if (err >= 0)
			++dg00x->substreams_counter;
		mutex_unlock(&dg00x->mutex);
	}

	return err;
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;

	mutex_lock(&dg00x->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		--dg00x->substreams_counter;

	snd_dg00x_stream_stop_duplex(dg00x);

	mutex_unlock(&dg00x->mutex);

	return snd_pcm_lib_free_pages(substream);
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;
	int err;

	mutex_lock(&dg00x->mutex);

	err = snd_dg00x_stream_start_duplex(dg00x);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&dg00x->tx_stream);

	mutex_unlock(&dg00x->mutex);

	return err;
}

static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;
	int err;

	mutex_lock(&dg00x->mutex);

	err = snd_dg00x_stream_start_duplex(dg00x);
	if (err >= 0) {
		amdtp_stream_pcm_prepare(&dg00x->rx_stream);
		amdtp_dot_reset(&dg00x->rx_stream);
	}

	mutex_unlock(&dg00x->mutex);

	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_dg00x *dg00x = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&dg00x->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&dg00x->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_dg00x *dg00x = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&dg00x->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&dg00x->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_dg00x *dg00x = sbstrm->private_data;

	return amdtp_domain_stream_pcm_pointer(&dg00x->domain, &dg00x->tx_stream);
}

static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_dg00x *dg00x = sbstrm->private_data;

	return amdtp_domain_stream_pcm_pointer(&dg00x->domain, &dg00x->rx_stream);
}

static int pcm_capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&dg00x->domain, &dg00x->tx_stream);
}

static int pcm_playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_dg00x *dg00x = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&dg00x->domain, &dg00x->rx_stream);
}

int snd_dg00x_create_pcm_devices(struct snd_dg00x *dg00x)
{
	static const struct snd_pcm_ops capture_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_hw_params,
		.hw_free	= pcm_hw_free,
		.prepare	= pcm_capture_prepare,
		.trigger	= pcm_capture_trigger,
		.pointer	= pcm_capture_pointer,
		.ack		= pcm_capture_ack,
	};
	static const struct snd_pcm_ops playback_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_hw_params,
		.hw_free	= pcm_hw_free,
		.prepare	= pcm_playback_prepare,
		.trigger	= pcm_playback_trigger,
		.pointer	= pcm_playback_pointer,
		.ack		= pcm_playback_ack,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(dg00x->card, dg00x->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = dg00x;
	snprintf(pcm->name, sizeof(pcm->name),
		 "%s PCM", dg00x->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_VMALLOC,
					      NULL, 0, 0);

	return 0;
}
